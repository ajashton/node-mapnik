
// node
#include <node_buffer.h>

// mapnik
#include <mapnik/grid/grid_view.hpp>

// boost
#include <boost/make_shared.hpp>

#include "mapnik_grid_view.hpp"
#include "js_grid_utils.hpp"
#include "utils.hpp"

// std
#include <exception>

Persistent<FunctionTemplate> GridView::constructor;

void GridView::Initialize(Handle<Object> target) {

    HandleScope scope;

    constructor = Persistent<FunctionTemplate>::New(FunctionTemplate::New(GridView::New));
    constructor->InstanceTemplate()->SetInternalFieldCount(1);
    constructor->SetClassName(String::NewSymbol("GridView"));

    NODE_SET_PROTOTYPE_METHOD(constructor, "encodeSync", encodeSync);
    NODE_SET_PROTOTYPE_METHOD(constructor, "encode", encode);
    NODE_SET_PROTOTYPE_METHOD(constructor, "width", width);
    NODE_SET_PROTOTYPE_METHOD(constructor, "height", height);
    NODE_SET_PROTOTYPE_METHOD(constructor, "isSolid", isSolid);
    NODE_SET_PROTOTYPE_METHOD(constructor, "getPixel", getPixel);

    target->Set(String::NewSymbol("GridView"),constructor->GetFunction());
}


GridView::GridView(grid_view_ptr gp) :
    ObjectWrap(),
    this_(gp) {}

GridView::~GridView()
{
}

Handle<Value> GridView::New(const Arguments& args)
{
    HandleScope scope;
    if (!args.IsConstructCall())
        return ThrowException(String::New("Cannot call constructor as function, you need to use 'new' keyword"));

    if (args[0]->IsExternal())
    {
        Local<External> ext = Local<External>::Cast(args[0]);
        void* ptr = ext->Value();
        GridView* g =  static_cast<GridView*>(ptr);
        g->Wrap(args.This());
        return args.This();
    } else {
        return ThrowException(String::New("Cannot create this object from Javascript"));
    }

    return Undefined();
}

Handle<Value> GridView::New(boost::shared_ptr<mapnik::grid> grid_ptr,
                            unsigned x,
                            unsigned y,
                            unsigned w,
                            unsigned h
    )
{
    HandleScope scope;
    grid_view_ptr gb_ptr = boost::make_shared<mapnik::grid_view>(grid_ptr->get_view(x,y,w,h));
    GridView* gv = new GridView(gb_ptr);
    Handle<Value> ext = External::New(gv);
    Handle<Object> obj = constructor->GetFunction()->NewInstance(1, &ext);
    return scope.Close(obj);
}

Handle<Value> GridView::width(const Arguments& args)
{
    HandleScope scope;

    GridView* g = ObjectWrap::Unwrap<GridView>(args.This());
    return scope.Close(Integer::New(g->get()->width()));
}

Handle<Value> GridView::height(const Arguments& args)
{
    HandleScope scope;

    GridView* g = ObjectWrap::Unwrap<GridView>(args.This());
    return scope.Close(Integer::New(g->get()->height()));
}

Handle<Value> GridView::isSolid(const Arguments& args)
{
    HandleScope scope;
    GridView* g = ObjectWrap::Unwrap<GridView>(args.This());
    grid_view_ptr view = g->get();
    if (view->width() > 0 && view->height() > 0)
    {
        mapnik::grid_view::value_type const* first_row = view->getRow(0);
        mapnik::grid_view::value_type const first_pixel = first_row[0];
        for (unsigned y = 0; y < view->height(); ++y)
        {
            mapnik::grid_view::value_type const * row = view->getRow(y);
            for (unsigned x = 0; x < view->width(); ++x)
            {
                if (first_pixel != row[x])
                {
                    return scope.Close(False());
                }
            }
        }
    }
    return scope.Close(True());
}

Handle<Value> GridView::getPixel(const Arguments& args)
{
    HandleScope scope;


    unsigned x(0);
    unsigned y(0);

    if (args.Length() >= 2) {
        if (!args[0]->IsNumber())
            return ThrowException(Exception::TypeError(
                                      String::New("first arg, 'x' must be an integer")));
        if (!args[1]->IsNumber())
            return ThrowException(Exception::TypeError(
                                      String::New("second arg, 'y' must be an integer")));
        x = args[0]->IntegerValue();
        y = args[1]->IntegerValue();
    } else {
        return ThrowException(Exception::TypeError(
                                  String::New("must supply x,y to query pixel color")));
    }

    GridView* g = ObjectWrap::Unwrap<GridView>(args.This());
    grid_view_ptr view = g->get();
    if (x < view->width() && y < view->height())
    {
        mapnik::grid_view::value_type const * row = view->getRow(y);
        mapnik::grid_view::value_type const pixel = row[x];
        return Integer::New(pixel);
    }
    return Undefined();
}

Handle<Value> GridView::encodeSync(const Arguments& args)
{
    HandleScope scope;

    GridView* g = ObjectWrap::Unwrap<GridView>(args.This());

    // defaults
    std::string format("utf");
    unsigned int resolution = 4;
    bool add_features = true;

    // accept custom format
    if (args.Length() >= 1){
        if (!args[0]->IsString())
            return ThrowException(Exception::TypeError(
                                      String::New("first arg, 'format' must be a string")));
        format = TOSTR(args[0]);
    }

    // options hash
    if (args.Length() >= 2) {
        if (!args[1]->IsObject())
            return ThrowException(Exception::TypeError(
                                      String::New("optional second arg must be an options object")));

        Local<Object> options = args[1]->ToObject();

        if (options->Has(String::New("resolution")))
        {
            Local<Value> bind_opt = options->Get(String::New("resolution"));
            if (!bind_opt->IsNumber())
                return ThrowException(Exception::TypeError(
                                          String::New("'resolution' must be an Integer")));

            resolution = bind_opt->IntegerValue();
        }

        if (options->Has(String::New("features")))
        {
            Local<Value> bind_opt = options->Get(String::New("features"));
            if (!bind_opt->IsBoolean())
                return ThrowException(Exception::TypeError(
                                          String::New("'features' must be an Boolean")));

            add_features = bind_opt->BooleanValue();
        }
    }

    try {

        boost::ptr_vector<uint16_t> lines;
        std::vector<mapnik::grid_view::lookup_type> key_order;
        node_mapnik::grid2utf<mapnik::grid_view>(*g->get(),lines,key_order,resolution);

        // convert key order to proper javascript array
        Local<Array> keys_a = Array::New(key_order.size());
        std::vector<std::string>::iterator it;
        unsigned int i;
        for (it = key_order.begin(), i = 0; it < key_order.end(); ++it, ++i)
        {
            keys_a->Set(i, String::New((*it).c_str()));
        }

        mapnik::grid_view const& grid_type = *g->get();

        // gather feature data
        Local<Object> feature_data = Object::New();
        if (add_features) {
            node_mapnik::write_features<mapnik::grid_view>(grid_type,
                                                           feature_data,
                                                           key_order);
        }

        // Create the return hash.
        Local<Object> json = Object::New();
        Local<Array> grid_array = Array::New();
        unsigned array_size = static_cast<unsigned int>(grid_type.width()/resolution);
        for (unsigned j=0;j<lines.size();++j)
        {
            grid_array->Set(j,String::New(&lines[j],array_size));
        }
        json->Set(String::NewSymbol("grid"), grid_array);
        json->Set(String::NewSymbol("keys"), keys_a);
        json->Set(String::NewSymbol("data"), feature_data);
        return json;

    }
    catch (std::exception & ex)
    {
        return ThrowException(Exception::Error(
                                  String::New(ex.what())));
    }

}

typedef struct {
    uv_work_t request;
    GridView* g;
    std::string format;
    bool error;
    std::string error_name;
    Persistent<Function> cb;
    boost::ptr_vector<uint16_t> lines;
    unsigned int resolution;
    bool add_features;
    std::vector<mapnik::grid::lookup_type> key_order;
} encode_grid_view_baton_t;


Handle<Value> GridView::encode(const Arguments& args)
{
    HandleScope scope;

    GridView* g = ObjectWrap::Unwrap<GridView>(args.This());

    // defaults
    std::string format("utf");
    unsigned int resolution = 4;
    bool add_features = true;

    // accept custom format
    if (args.Length() >= 1){
        if (!args[0]->IsString())
            return ThrowException(Exception::TypeError(
                                      String::New("first arg, 'format' must be a string")));
        format = TOSTR(args[0]);
    }

    // options hash
    if (args.Length() >= 2) {
        if (!args[1]->IsObject())
            return ThrowException(Exception::TypeError(
                                      String::New("optional second arg must be an options object")));

        Local<Object> options = args[1]->ToObject();

        if (options->Has(String::New("resolution")))
        {
            Local<Value> bind_opt = options->Get(String::New("resolution"));
            if (!bind_opt->IsNumber())
                return ThrowException(Exception::TypeError(
                                          String::New("'resolution' must be an Integer")));

            resolution = bind_opt->IntegerValue();
        }

        if (options->Has(String::New("features")))
        {
            Local<Value> bind_opt = options->Get(String::New("features"));
            if (!bind_opt->IsBoolean())
                return ThrowException(Exception::TypeError(
                                          String::New("'features' must be an Boolean")));

            add_features = bind_opt->BooleanValue();
        }
    }

    // ensure callback is a function
    if (!args[args.Length()-1]->IsFunction())
        return ThrowException(Exception::TypeError(
                                  String::New("last argument must be a callback function")));
    Local<Function> callback = Local<Function>::Cast(args[args.Length()-1]);

    encode_grid_view_baton_t *closure = new encode_grid_view_baton_t();
    closure->request.data = closure;
    closure->g = g;
    closure->format = format;
    closure->error = false;
    closure->resolution = resolution;
    closure->add_features = add_features;
    closure->cb = Persistent<Function>::New(Handle<Function>::Cast(callback));
    uv_queue_work(uv_default_loop(), &closure->request, EIO_Encode, EIO_AfterEncode);
    uv_ref(uv_default_loop());
    g->Ref();
    return Undefined();
}

void GridView::EIO_Encode(uv_work_t* req)
{
    encode_grid_view_baton_t *closure = static_cast<encode_grid_view_baton_t *>(req->data);

    try
    {
        // TODO - write features and clear here as well?
        node_mapnik::grid2utf<mapnik::grid_view>(*closure->g->get(),
                                                 closure->lines,
                                                 closure->key_order,
                                                 closure->resolution);
    }
    catch (std::exception & ex)
    {
        closure->error = true;
        closure->error_name = ex.what();
    }
    catch (...)
    {
        closure->error = true;
        closure->error_name = "unknown exception happened when encoding grid: please file bug report";
    }
}

void GridView::EIO_AfterEncode(uv_work_t* req)
{
    HandleScope scope;

    encode_grid_view_baton_t *closure = static_cast<encode_grid_view_baton_t *>(req->data);

    TryCatch try_catch;

    if (closure->error) {
        Local<Value> argv[1] = { Exception::Error(String::New(closure->error_name.c_str())) };
        closure->cb->Call(Context::GetCurrent()->Global(), 1, argv);
    } else {

        // convert key order to proper javascript array
        Local<Array> keys_a = Array::New(closure->key_order.size());
        std::vector<std::string>::iterator it;
        unsigned int i;
        for (it = closure->key_order.begin(), i = 0; it < closure->key_order.end(); ++it, ++i)
        {
            keys_a->Set(i, String::New((*it).c_str()));
        }

        mapnik::grid_view const& grid_type = *closure->g->get();

        // gather feature data
        Local<Object> feature_data = Object::New();
        if (closure->add_features) {
            node_mapnik::write_features<mapnik::grid_view>(grid_type,
                                                           feature_data,
                                                           closure->key_order);
        }

        // Create the return hash.
        Local<Object> json = Object::New();
        Local<Array> grid_array = Array::New(closure->lines.size());
        unsigned array_size = static_cast<unsigned int>(grid_type.width()/closure->resolution);
        for (unsigned j=0;j<closure->lines.size();++j)
        {
            grid_array->Set(j,String::New(&closure->lines[j],array_size));
        }
        json->Set(String::NewSymbol("grid"), grid_array);
        json->Set(String::NewSymbol("keys"), keys_a);
        json->Set(String::NewSymbol("data"), feature_data);

        Local<Value> argv[2] = { Local<Value>::New(Null()), Local<Value>::New(json) };
        closure->cb->Call(Context::GetCurrent()->Global(), 2, argv);
    }

    if (try_catch.HasCaught()) {
        FatalException(try_catch);
    }

    uv_unref(uv_default_loop());
    closure->g->Unref();
    closure->cb.Dispose();
    delete closure;
}
