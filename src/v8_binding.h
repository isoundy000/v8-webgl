// Copyright (c) 2012 Hewlett-Packard Development Company, L.P. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8WEBGL_V8BINDING_H
#define V8WEBGL_V8BINDING_H

#include <v8.h>
#include <assert.h>
#include <string>

namespace v8_webgl {

inline v8::Handle<v8::Value> ThrowError(const char* msg) {
  return v8::ThrowException(v8::String::New(msg));
}

inline v8::Handle<v8::Value> ThrowObjectDisposed() {
  return v8::ThrowException(v8::Exception::ReferenceError(v8::String::New("Object has been disposed")));
}

inline v8::Handle<v8::Value> ThrowArgCount() {
  return v8::ThrowException(v8::Exception::TypeError(v8::String::New("Not enough arguments")));
}

inline v8::Handle<v8::Value> ThrowRangeError(const char* msg) {
  return v8::ThrowException(v8::Exception::RangeError(v8::String::New(msg)));
}

template<typename T>
v8::Handle<v8::Value> TypeToV8(T value);

template<>
inline v8::Handle<v8::Value> TypeToV8<bool>(bool value) {
  return value ? v8::True() : v8::False();
}
template<>
inline v8::Handle<v8::Value> TypeToV8<int32_t>(int32_t value) {
  return v8::Integer::New(value);
}
template<>
inline v8::Handle<v8::Value> TypeToV8<uint32_t>(uint32_t value) {
  return v8::Integer::NewFromUnsigned(value);
}
template<>
inline v8::Handle<v8::Value> TypeToV8<double>(double value) {
  return v8::Number::New(value);
}
template<>
inline v8::Handle<v8::Value> TypeToV8<const char*>(const char* value) {
  return v8::String::New(value);
}

template<typename T>
v8::Handle<v8::Array> ArrayToV8(T* values, uint32_t length) {
  v8::Local<v8::Array> array = v8::Array::New(length);
  for (uint32_t i = 0; i < length; i++)
    array->Set(TypeToV8<uint32_t>(i), TypeToV8<T>(values[i]));
  return array;
}

std::string V8ToString(v8::Handle<v8::Value> value, bool& ok);
inline double V8ToBoolean(v8::Handle<v8::Value> value, bool& ok) {
  ok = true;
  return value->BooleanValue();
}
double V8ToNumber(v8::Handle<v8::Value> value, bool& ok);
inline float V8ToFloat(v8::Handle<v8::Value> value, bool& ok) {
  return static_cast<float>(V8ToNumber(value, ok));
}
int32_t V8ToInt32(v8::Handle<v8::Value> value, bool& ok);
uint32_t V8ToUint32(v8::Handle<v8::Value> value, bool& ok);

template<class T>
T* V8ToNative(v8::Handle<v8::Value> value, bool& ok) {
  ok = true;

  // Not an error if undefined/null
  if (value->IsUndefined() || value->IsNull())
    return 0;

  if (!T::HasInstance(value)) {
    ok = false;
    return 0;
  }
  v8::Handle<v8::Object> object = value->ToObject();
  T* native = T::ToNative(object);
  if (!native) {
    ok = false;
    ThrowObjectDisposed();
    return 0;
  }
  return native;
}

//////

class V8ObjectBase {
 protected:
  V8ObjectBase() {}
  virtual ~V8ObjectBase() {}

  inline static void AddCallback(v8::Handle<v8::ObjectTemplate> proto, const char* name, v8::InvocationCallback callback, v8::Local<v8::Signature> signature) {
    proto->Set(v8::String::New(name),
               v8::FunctionTemplate::New(callback, v8::Handle<v8::Value>(), signature),
               v8::DontDelete);
  };

  inline static void AddConstant(const char* name, v8::Handle<v8::Value> value, v8::Handle<v8::ObjectTemplate> proto, v8::Handle<v8::FunctionTemplate> constructor) {
    v8::Handle<v8::String> name_handle = v8::String::New(name);
    constructor->Set(name_handle, value, static_cast<v8::PropertyAttribute>(v8::ReadOnly|v8::DontDelete));
    proto->Set(name_handle, value, static_cast<v8::PropertyAttribute>(v8::ReadOnly|v8::DontDelete));
  };

  inline static void SetProperty(v8::Handle<v8::Object> self, const char* name, v8::Handle<v8::Value> value) {
    self->Set(v8::String::New(name), value,
              static_cast<v8::PropertyAttribute>(v8::ReadOnly|v8::DontDelete));
  }

  static v8::Persistent<v8::FunctionTemplate> CreateConstructorTemplate(const char* class_name, v8::InvocationCallback callback);

 private:
  V8ObjectBase(const V8ObjectBase&);
  V8ObjectBase& operator = (const V8ObjectBase&);
};

//////

template<class T>
class ConstructorMode {
 public:
  ConstructorMode() { s_allow_construction = true; }
  ~ConstructorMode() { s_allow_construction = false; }
  static bool IsConstructionAllowed() { return s_allow_construction; }
 private:
  static bool s_allow_construction;
};
template<class T>
bool ConstructorMode<T>::s_allow_construction = false;

//////

template<class T>
class V8Object : public V8ObjectBase {
 public:
  static void Initialize(v8::Handle<v8::ObjectTemplate> global) {
    if (!s_constructor_template.IsEmpty())
      return;
    v8::HandleScope scope;
    s_constructor_template = CreateConstructorTemplate(T::ClassName(), T::ConstructorCallback);
    T::ConfigureConstructorTemplate(s_constructor_template);
    T::ConfigureGlobal(global);
  }

  static void Uninitialize() {
    if (s_constructor_template.IsEmpty())
      return;
    s_constructor_template.Dispose();
    s_constructor_template.Clear();
  }

  inline static T* ToNative(v8::Handle<v8::Object> value) {
    return static_cast<T*>(value->GetPointerFromInternalField(0));
  }

  inline static bool HasInstance(v8::Handle<v8::Value> value){
    return s_constructor_template->HasInstance(value);
  }

  inline v8::Handle<v8::Object> ToV8() {
    return instance_;
  }

 protected:
  V8Object(bool weak = false, v8::Handle<v8::Object> instance = v8::Local<v8::Object>()) {
    // If no instance, construct a new one
    if (instance.IsEmpty()) {
      ConstructorMode<T> mode;
      instance = s_constructor_template->GetFunction()->NewInstance();
    }

    instance_ = v8::Persistent<v8::Object>::New(instance);
    instance_->SetPointerInInternalField(0, this);

    if (weak) {
      // Since we aren't using object grouping API, mark independent
      // so our weak callback is called earlier.
      instance_.MarkIndependent();
      instance_.MakeWeak(this, WeakCallback);
    }
  }

  ~V8Object() {
    if (!instance_.IsEmpty()) {
      instance_.ClearWeak();
      instance_->SetPointerInInternalField(0, 0);
      instance_.Dispose();
      instance_.Clear();
    }
  }

  static v8::Handle<v8::Object> Create(int argc = 0, v8::Handle<v8::Value> argv[] = NULL) {
    return s_constructor_template->GetFunction()->NewInstance(argc, argv);
  }

  // Subclasses should reimplement
  static void ConfigureConstructorTemplate(v8::Persistent<v8::FunctionTemplate> /* constructor */) {}

  // Subclasses can reimplement this if they don't want their constructor name exposed
  static void ConfigureGlobal(v8::Handle<v8::ObjectTemplate> global) {
    global->Set(v8::String::New(T::ClassName()), s_constructor_template);
  }

  static v8::Handle<v8::Value> ConstructorCallback(const v8::Arguments& args) {
    if (!ConstructorMode<T>::IsConstructionAllowed())
      return v8::ThrowException(v8::Exception::TypeError(v8::String::New("Illegal constructor")));

    return args.This();
  }

 private:
  static void WeakCallback(v8::Persistent<v8::Value> value, void* data) {
    V8Object<T>* object = static_cast<V8Object<T>*>(data);
    assert(value == object->instance_);
    delete object;
  }

  static v8::Persistent<v8::FunctionTemplate> s_constructor_template;
  v8::Persistent<v8::Object> instance_;
};

template<class T>
v8::Persistent<v8::FunctionTemplate> V8Object<T>::s_constructor_template;

}

#endif
