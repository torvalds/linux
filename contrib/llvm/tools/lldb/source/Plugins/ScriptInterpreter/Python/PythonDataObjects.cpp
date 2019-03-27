//===-- PythonDataObjects.cpp -----------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifdef LLDB_DISABLE_PYTHON

// Python is disabled in this build

#else

#include "PythonDataObjects.h"
#include "ScriptInterpreterPython.h"

#include "lldb/Host/File.h"
#include "lldb/Host/FileSystem.h"
#include "lldb/Interpreter/ScriptInterpreter.h"
#include "lldb/Utility/Stream.h"

#include "llvm/Support/ConvertUTF.h"

#include <stdio.h>

#include "llvm/ADT/StringSwitch.h"

using namespace lldb_private;
using namespace lldb;

void StructuredPythonObject::Dump(Stream &s, bool pretty_print) const {
  s << "Python Obj: 0x" << GetValue();
}

//----------------------------------------------------------------------
// PythonObject
//----------------------------------------------------------------------

void PythonObject::Dump(Stream &strm) const {
  if (m_py_obj) {
    FILE *file = ::tmpfile();
    if (file) {
      ::PyObject_Print(m_py_obj, file, 0);
      const long length = ftell(file);
      if (length) {
        ::rewind(file);
        std::vector<char> file_contents(length, '\0');
        const size_t length_read =
            ::fread(file_contents.data(), 1, file_contents.size(), file);
        if (length_read > 0)
          strm.Write(file_contents.data(), length_read);
      }
      ::fclose(file);
    }
  } else
    strm.PutCString("NULL");
}

PyObjectType PythonObject::GetObjectType() const {
  if (!IsAllocated())
    return PyObjectType::None;

  if (PythonModule::Check(m_py_obj))
    return PyObjectType::Module;
  if (PythonList::Check(m_py_obj))
    return PyObjectType::List;
  if (PythonTuple::Check(m_py_obj))
    return PyObjectType::Tuple;
  if (PythonDictionary::Check(m_py_obj))
    return PyObjectType::Dictionary;
  if (PythonString::Check(m_py_obj))
    return PyObjectType::String;
#if PY_MAJOR_VERSION >= 3
  if (PythonBytes::Check(m_py_obj))
    return PyObjectType::Bytes;
#endif
  if (PythonByteArray::Check(m_py_obj))
    return PyObjectType::ByteArray;
  if (PythonInteger::Check(m_py_obj))
    return PyObjectType::Integer;
  if (PythonFile::Check(m_py_obj))
    return PyObjectType::File;
  if (PythonCallable::Check(m_py_obj))
    return PyObjectType::Callable;
  return PyObjectType::Unknown;
}

PythonString PythonObject::Repr() const {
  if (!m_py_obj)
    return PythonString();
  PyObject *repr = PyObject_Repr(m_py_obj);
  if (!repr)
    return PythonString();
  return PythonString(PyRefType::Owned, repr);
}

PythonString PythonObject::Str() const {
  if (!m_py_obj)
    return PythonString();
  PyObject *str = PyObject_Str(m_py_obj);
  if (!str)
    return PythonString();
  return PythonString(PyRefType::Owned, str);
}

PythonObject
PythonObject::ResolveNameWithDictionary(llvm::StringRef name,
                                        const PythonDictionary &dict) {
  size_t dot_pos = name.find_first_of('.');
  llvm::StringRef piece = name.substr(0, dot_pos);
  PythonObject result = dict.GetItemForKey(PythonString(piece));
  if (dot_pos == llvm::StringRef::npos) {
    // There was no dot, we're done.
    return result;
  }

  // There was a dot.  The remaining portion of the name should be looked up in
  // the context of the object that was found in the dictionary.
  return result.ResolveName(name.substr(dot_pos + 1));
}

PythonObject PythonObject::ResolveName(llvm::StringRef name) const {
  // Resolve the name in the context of the specified object.  If, for example,
  // `this` refers to a PyModule, then this will look for `name` in this
  // module.  If `this` refers to a PyType, then it will resolve `name` as an
  // attribute of that type.  If `this` refers to an instance of an object,
  // then it will resolve `name` as the value of the specified field.
  //
  // This function handles dotted names so that, for example, if `m_py_obj`
  // refers to the `sys` module, and `name` == "path.append", then it will find
  // the function `sys.path.append`.

  size_t dot_pos = name.find_first_of('.');
  if (dot_pos == llvm::StringRef::npos) {
    // No dots in the name, we should be able to find the value immediately as
    // an attribute of `m_py_obj`.
    return GetAttributeValue(name);
  }

  // Look up the first piece of the name, and resolve the rest as a child of
  // that.
  PythonObject parent = ResolveName(name.substr(0, dot_pos));
  if (!parent.IsAllocated())
    return PythonObject();

  // Tail recursion.. should be optimized by the compiler
  return parent.ResolveName(name.substr(dot_pos + 1));
}

bool PythonObject::HasAttribute(llvm::StringRef attr) const {
  if (!IsValid())
    return false;
  PythonString py_attr(attr);
  return !!PyObject_HasAttr(m_py_obj, py_attr.get());
}

PythonObject PythonObject::GetAttributeValue(llvm::StringRef attr) const {
  if (!IsValid())
    return PythonObject();

  PythonString py_attr(attr);
  if (!PyObject_HasAttr(m_py_obj, py_attr.get()))
    return PythonObject();

  return PythonObject(PyRefType::Owned,
                      PyObject_GetAttr(m_py_obj, py_attr.get()));
}

bool PythonObject::IsNone() const { return m_py_obj == Py_None; }

bool PythonObject::IsValid() const { return m_py_obj != nullptr; }

bool PythonObject::IsAllocated() const { return IsValid() && !IsNone(); }

StructuredData::ObjectSP PythonObject::CreateStructuredObject() const {
  switch (GetObjectType()) {
  case PyObjectType::Dictionary:
    return PythonDictionary(PyRefType::Borrowed, m_py_obj)
        .CreateStructuredDictionary();
  case PyObjectType::Integer:
    return PythonInteger(PyRefType::Borrowed, m_py_obj)
        .CreateStructuredInteger();
  case PyObjectType::List:
    return PythonList(PyRefType::Borrowed, m_py_obj).CreateStructuredArray();
  case PyObjectType::String:
    return PythonString(PyRefType::Borrowed, m_py_obj).CreateStructuredString();
  case PyObjectType::Bytes:
    return PythonBytes(PyRefType::Borrowed, m_py_obj).CreateStructuredString();
  case PyObjectType::ByteArray:
    return PythonByteArray(PyRefType::Borrowed, m_py_obj)
        .CreateStructuredString();
  case PyObjectType::None:
    return StructuredData::ObjectSP();
  default:
    return StructuredData::ObjectSP(new StructuredPythonObject(m_py_obj));
  }
}

//----------------------------------------------------------------------
// PythonString
//----------------------------------------------------------------------
PythonBytes::PythonBytes() : PythonObject() {}

PythonBytes::PythonBytes(llvm::ArrayRef<uint8_t> bytes) : PythonObject() {
  SetBytes(bytes);
}

PythonBytes::PythonBytes(const uint8_t *bytes, size_t length) : PythonObject() {
  SetBytes(llvm::ArrayRef<uint8_t>(bytes, length));
}

PythonBytes::PythonBytes(PyRefType type, PyObject *py_obj) : PythonObject() {
  Reset(type, py_obj); // Use "Reset()" to ensure that py_obj is a string
}

PythonBytes::PythonBytes(const PythonBytes &object) : PythonObject(object) {}

PythonBytes::~PythonBytes() {}

bool PythonBytes::Check(PyObject *py_obj) {
  if (!py_obj)
    return false;
  return PyBytes_Check(py_obj);
}

void PythonBytes::Reset(PyRefType type, PyObject *py_obj) {
  // Grab the desired reference type so that if we end up rejecting `py_obj` it
  // still gets decremented if necessary.
  PythonObject result(type, py_obj);

  if (!PythonBytes::Check(py_obj)) {
    PythonObject::Reset();
    return;
  }

  // Calling PythonObject::Reset(const PythonObject&) will lead to stack
  // overflow since it calls back into the virtual implementation.
  PythonObject::Reset(PyRefType::Borrowed, result.get());
}

llvm::ArrayRef<uint8_t> PythonBytes::GetBytes() const {
  if (!IsValid())
    return llvm::ArrayRef<uint8_t>();

  Py_ssize_t size;
  char *c;

  PyBytes_AsStringAndSize(m_py_obj, &c, &size);
  return llvm::ArrayRef<uint8_t>(reinterpret_cast<uint8_t *>(c), size);
}

size_t PythonBytes::GetSize() const {
  if (!IsValid())
    return 0;
  return PyBytes_Size(m_py_obj);
}

void PythonBytes::SetBytes(llvm::ArrayRef<uint8_t> bytes) {
  const char *data = reinterpret_cast<const char *>(bytes.data());
  PyObject *py_bytes = PyBytes_FromStringAndSize(data, bytes.size());
  PythonObject::Reset(PyRefType::Owned, py_bytes);
}

StructuredData::StringSP PythonBytes::CreateStructuredString() const {
  StructuredData::StringSP result(new StructuredData::String);
  Py_ssize_t size;
  char *c;
  PyBytes_AsStringAndSize(m_py_obj, &c, &size);
  result->SetValue(std::string(c, size));
  return result;
}

PythonByteArray::PythonByteArray(llvm::ArrayRef<uint8_t> bytes)
    : PythonByteArray(bytes.data(), bytes.size()) {}

PythonByteArray::PythonByteArray(const uint8_t *bytes, size_t length) {
  const char *str = reinterpret_cast<const char *>(bytes);
  Reset(PyRefType::Owned, PyByteArray_FromStringAndSize(str, length));
}

PythonByteArray::PythonByteArray(PyRefType type, PyObject *o) {
  Reset(type, o);
}

PythonByteArray::PythonByteArray(const PythonBytes &object)
    : PythonObject(object) {}

PythonByteArray::~PythonByteArray() {}

bool PythonByteArray::Check(PyObject *py_obj) {
  if (!py_obj)
    return false;
  return PyByteArray_Check(py_obj);
}

void PythonByteArray::Reset(PyRefType type, PyObject *py_obj) {
  // Grab the desired reference type so that if we end up rejecting `py_obj` it
  // still gets decremented if necessary.
  PythonObject result(type, py_obj);

  if (!PythonByteArray::Check(py_obj)) {
    PythonObject::Reset();
    return;
  }

  // Calling PythonObject::Reset(const PythonObject&) will lead to stack
  // overflow since it calls back into the virtual implementation.
  PythonObject::Reset(PyRefType::Borrowed, result.get());
}

llvm::ArrayRef<uint8_t> PythonByteArray::GetBytes() const {
  if (!IsValid())
    return llvm::ArrayRef<uint8_t>();

  char *c = PyByteArray_AsString(m_py_obj);
  size_t size = GetSize();
  return llvm::ArrayRef<uint8_t>(reinterpret_cast<uint8_t *>(c), size);
}

size_t PythonByteArray::GetSize() const {
  if (!IsValid())
    return 0;

  return PyByteArray_Size(m_py_obj);
}

StructuredData::StringSP PythonByteArray::CreateStructuredString() const {
  StructuredData::StringSP result(new StructuredData::String);
  llvm::ArrayRef<uint8_t> bytes = GetBytes();
  const char *str = reinterpret_cast<const char *>(bytes.data());
  result->SetValue(std::string(str, bytes.size()));
  return result;
}

//----------------------------------------------------------------------
// PythonString
//----------------------------------------------------------------------

PythonString::PythonString(PyRefType type, PyObject *py_obj) : PythonObject() {
  Reset(type, py_obj); // Use "Reset()" to ensure that py_obj is a string
}

PythonString::PythonString(const PythonString &object) : PythonObject(object) {}

PythonString::PythonString(llvm::StringRef string) : PythonObject() {
  SetString(string);
}

PythonString::PythonString(const char *string) : PythonObject() {
  SetString(llvm::StringRef(string));
}

PythonString::PythonString() : PythonObject() {}

PythonString::~PythonString() {}

bool PythonString::Check(PyObject *py_obj) {
  if (!py_obj)
    return false;

  if (PyUnicode_Check(py_obj))
    return true;
#if PY_MAJOR_VERSION < 3
  if (PyString_Check(py_obj))
    return true;
#endif
  return false;
}

void PythonString::Reset(PyRefType type, PyObject *py_obj) {
  // Grab the desired reference type so that if we end up rejecting `py_obj` it
  // still gets decremented if necessary.
  PythonObject result(type, py_obj);

  if (!PythonString::Check(py_obj)) {
    PythonObject::Reset();
    return;
  }
#if PY_MAJOR_VERSION < 3
  // In Python 2, Don't store PyUnicode objects directly, because we need
  // access to their underlying character buffers which Python 2 doesn't
  // provide.
  if (PyUnicode_Check(py_obj))
    result.Reset(PyRefType::Owned, PyUnicode_AsUTF8String(result.get()));
#endif
  // Calling PythonObject::Reset(const PythonObject&) will lead to stack
  // overflow since it calls back into the virtual implementation.
  PythonObject::Reset(PyRefType::Borrowed, result.get());
}

llvm::StringRef PythonString::GetString() const {
  if (!IsValid())
    return llvm::StringRef();

  Py_ssize_t size;
  const char *data;

#if PY_MAJOR_VERSION >= 3
  data = PyUnicode_AsUTF8AndSize(m_py_obj, &size);
#else
  char *c;
  PyString_AsStringAndSize(m_py_obj, &c, &size);
  data = c;
#endif
  return llvm::StringRef(data, size);
}

size_t PythonString::GetSize() const {
  if (IsValid()) {
#if PY_MAJOR_VERSION >= 3
    return PyUnicode_GetSize(m_py_obj);
#else
    return PyString_Size(m_py_obj);
#endif
  }
  return 0;
}

void PythonString::SetString(llvm::StringRef string) {
#if PY_MAJOR_VERSION >= 3
  PyObject *unicode = PyUnicode_FromStringAndSize(string.data(), string.size());
  PythonObject::Reset(PyRefType::Owned, unicode);
#else
  PyObject *str = PyString_FromStringAndSize(string.data(), string.size());
  PythonObject::Reset(PyRefType::Owned, str);
#endif
}

StructuredData::StringSP PythonString::CreateStructuredString() const {
  StructuredData::StringSP result(new StructuredData::String);
  result->SetValue(GetString());
  return result;
}

//----------------------------------------------------------------------
// PythonInteger
//----------------------------------------------------------------------

PythonInteger::PythonInteger() : PythonObject() {}

PythonInteger::PythonInteger(PyRefType type, PyObject *py_obj)
    : PythonObject() {
  Reset(type, py_obj); // Use "Reset()" to ensure that py_obj is a integer type
}

PythonInteger::PythonInteger(const PythonInteger &object)
    : PythonObject(object) {}

PythonInteger::PythonInteger(int64_t value) : PythonObject() {
  SetInteger(value);
}

PythonInteger::~PythonInteger() {}

bool PythonInteger::Check(PyObject *py_obj) {
  if (!py_obj)
    return false;

#if PY_MAJOR_VERSION >= 3
  // Python 3 does not have PyInt_Check.  There is only one type of integral
  // value, long.
  return PyLong_Check(py_obj);
#else
  return PyLong_Check(py_obj) || PyInt_Check(py_obj);
#endif
}

void PythonInteger::Reset(PyRefType type, PyObject *py_obj) {
  // Grab the desired reference type so that if we end up rejecting `py_obj` it
  // still gets decremented if necessary.
  PythonObject result(type, py_obj);

  if (!PythonInteger::Check(py_obj)) {
    PythonObject::Reset();
    return;
  }

#if PY_MAJOR_VERSION < 3
  // Always store this as a PyLong, which makes interoperability between Python
  // 2.x and Python 3.x easier.  This is only necessary in 2.x, since 3.x
  // doesn't even have a PyInt.
  if (PyInt_Check(py_obj)) {
    // Since we converted the original object to a different type, the new
    // object is an owned object regardless of the ownership semantics
    // requested by the user.
    result.Reset(PyRefType::Owned, PyLong_FromLongLong(PyInt_AsLong(py_obj)));
  }
#endif

  assert(PyLong_Check(result.get()) &&
         "Couldn't get a PyLong from this PyObject");

  // Calling PythonObject::Reset(const PythonObject&) will lead to stack
  // overflow since it calls back into the virtual implementation.
  PythonObject::Reset(PyRefType::Borrowed, result.get());
}

int64_t PythonInteger::GetInteger() const {
  if (m_py_obj) {
    assert(PyLong_Check(m_py_obj) &&
           "PythonInteger::GetInteger has a PyObject that isn't a PyLong");

    int overflow = 0;
    int64_t result = PyLong_AsLongLongAndOverflow(m_py_obj, &overflow);
    if (overflow != 0) {
      // We got an integer that overflows, like 18446744072853913392L we can't
      // use PyLong_AsLongLong() as it will return 0xffffffffffffffff. If we
      // use the unsigned long long it will work as expected.
      const uint64_t uval = PyLong_AsUnsignedLongLong(m_py_obj);
      result = static_cast<int64_t>(uval);
    }
    return result;
  }
  return UINT64_MAX;
}

void PythonInteger::SetInteger(int64_t value) {
  PythonObject::Reset(PyRefType::Owned, PyLong_FromLongLong(value));
}

StructuredData::IntegerSP PythonInteger::CreateStructuredInteger() const {
  StructuredData::IntegerSP result(new StructuredData::Integer);
  result->SetValue(GetInteger());
  return result;
}

//----------------------------------------------------------------------
// PythonList
//----------------------------------------------------------------------

PythonList::PythonList(PyInitialValue value) : PythonObject() {
  if (value == PyInitialValue::Empty)
    Reset(PyRefType::Owned, PyList_New(0));
}

PythonList::PythonList(int list_size) : PythonObject() {
  Reset(PyRefType::Owned, PyList_New(list_size));
}

PythonList::PythonList(PyRefType type, PyObject *py_obj) : PythonObject() {
  Reset(type, py_obj); // Use "Reset()" to ensure that py_obj is a list
}

PythonList::PythonList(const PythonList &list) : PythonObject(list) {}

PythonList::~PythonList() {}

bool PythonList::Check(PyObject *py_obj) {
  if (!py_obj)
    return false;
  return PyList_Check(py_obj);
}

void PythonList::Reset(PyRefType type, PyObject *py_obj) {
  // Grab the desired reference type so that if we end up rejecting `py_obj` it
  // still gets decremented if necessary.
  PythonObject result(type, py_obj);

  if (!PythonList::Check(py_obj)) {
    PythonObject::Reset();
    return;
  }

  // Calling PythonObject::Reset(const PythonObject&) will lead to stack
  // overflow since it calls back into the virtual implementation.
  PythonObject::Reset(PyRefType::Borrowed, result.get());
}

uint32_t PythonList::GetSize() const {
  if (IsValid())
    return PyList_GET_SIZE(m_py_obj);
  return 0;
}

PythonObject PythonList::GetItemAtIndex(uint32_t index) const {
  if (IsValid())
    return PythonObject(PyRefType::Borrowed, PyList_GetItem(m_py_obj, index));
  return PythonObject();
}

void PythonList::SetItemAtIndex(uint32_t index, const PythonObject &object) {
  if (IsAllocated() && object.IsValid()) {
    // PyList_SetItem is documented to "steal" a reference, so we need to
    // convert it to an owned reference by incrementing it.
    Py_INCREF(object.get());
    PyList_SetItem(m_py_obj, index, object.get());
  }
}

void PythonList::AppendItem(const PythonObject &object) {
  if (IsAllocated() && object.IsValid()) {
    // `PyList_Append` does *not* steal a reference, so do not call `Py_INCREF`
    // here like we do with `PyList_SetItem`.
    PyList_Append(m_py_obj, object.get());
  }
}

StructuredData::ArraySP PythonList::CreateStructuredArray() const {
  StructuredData::ArraySP result(new StructuredData::Array);
  uint32_t count = GetSize();
  for (uint32_t i = 0; i < count; ++i) {
    PythonObject obj = GetItemAtIndex(i);
    result->AddItem(obj.CreateStructuredObject());
  }
  return result;
}

//----------------------------------------------------------------------
// PythonTuple
//----------------------------------------------------------------------

PythonTuple::PythonTuple(PyInitialValue value) : PythonObject() {
  if (value == PyInitialValue::Empty)
    Reset(PyRefType::Owned, PyTuple_New(0));
}

PythonTuple::PythonTuple(int tuple_size) : PythonObject() {
  Reset(PyRefType::Owned, PyTuple_New(tuple_size));
}

PythonTuple::PythonTuple(PyRefType type, PyObject *py_obj) : PythonObject() {
  Reset(type, py_obj); // Use "Reset()" to ensure that py_obj is a tuple
}

PythonTuple::PythonTuple(const PythonTuple &tuple) : PythonObject(tuple) {}

PythonTuple::PythonTuple(std::initializer_list<PythonObject> objects) {
  m_py_obj = PyTuple_New(objects.size());

  uint32_t idx = 0;
  for (auto object : objects) {
    if (object.IsValid())
      SetItemAtIndex(idx, object);
    idx++;
  }
}

PythonTuple::PythonTuple(std::initializer_list<PyObject *> objects) {
  m_py_obj = PyTuple_New(objects.size());

  uint32_t idx = 0;
  for (auto py_object : objects) {
    PythonObject object(PyRefType::Borrowed, py_object);
    if (object.IsValid())
      SetItemAtIndex(idx, object);
    idx++;
  }
}

PythonTuple::~PythonTuple() {}

bool PythonTuple::Check(PyObject *py_obj) {
  if (!py_obj)
    return false;
  return PyTuple_Check(py_obj);
}

void PythonTuple::Reset(PyRefType type, PyObject *py_obj) {
  // Grab the desired reference type so that if we end up rejecting `py_obj` it
  // still gets decremented if necessary.
  PythonObject result(type, py_obj);

  if (!PythonTuple::Check(py_obj)) {
    PythonObject::Reset();
    return;
  }

  // Calling PythonObject::Reset(const PythonObject&) will lead to stack
  // overflow since it calls back into the virtual implementation.
  PythonObject::Reset(PyRefType::Borrowed, result.get());
}

uint32_t PythonTuple::GetSize() const {
  if (IsValid())
    return PyTuple_GET_SIZE(m_py_obj);
  return 0;
}

PythonObject PythonTuple::GetItemAtIndex(uint32_t index) const {
  if (IsValid())
    return PythonObject(PyRefType::Borrowed, PyTuple_GetItem(m_py_obj, index));
  return PythonObject();
}

void PythonTuple::SetItemAtIndex(uint32_t index, const PythonObject &object) {
  if (IsAllocated() && object.IsValid()) {
    // PyTuple_SetItem is documented to "steal" a reference, so we need to
    // convert it to an owned reference by incrementing it.
    Py_INCREF(object.get());
    PyTuple_SetItem(m_py_obj, index, object.get());
  }
}

StructuredData::ArraySP PythonTuple::CreateStructuredArray() const {
  StructuredData::ArraySP result(new StructuredData::Array);
  uint32_t count = GetSize();
  for (uint32_t i = 0; i < count; ++i) {
    PythonObject obj = GetItemAtIndex(i);
    result->AddItem(obj.CreateStructuredObject());
  }
  return result;
}

//----------------------------------------------------------------------
// PythonDictionary
//----------------------------------------------------------------------

PythonDictionary::PythonDictionary(PyInitialValue value) : PythonObject() {
  if (value == PyInitialValue::Empty)
    Reset(PyRefType::Owned, PyDict_New());
}

PythonDictionary::PythonDictionary(PyRefType type, PyObject *py_obj)
    : PythonObject() {
  Reset(type, py_obj); // Use "Reset()" to ensure that py_obj is a dictionary
}

PythonDictionary::PythonDictionary(const PythonDictionary &object)
    : PythonObject(object) {}

PythonDictionary::~PythonDictionary() {}

bool PythonDictionary::Check(PyObject *py_obj) {
  if (!py_obj)
    return false;

  return PyDict_Check(py_obj);
}

void PythonDictionary::Reset(PyRefType type, PyObject *py_obj) {
  // Grab the desired reference type so that if we end up rejecting `py_obj` it
  // still gets decremented if necessary.
  PythonObject result(type, py_obj);

  if (!PythonDictionary::Check(py_obj)) {
    PythonObject::Reset();
    return;
  }

  // Calling PythonObject::Reset(const PythonObject&) will lead to stack
  // overflow since it calls back into the virtual implementation.
  PythonObject::Reset(PyRefType::Borrowed, result.get());
}

uint32_t PythonDictionary::GetSize() const {
  if (IsValid())
    return PyDict_Size(m_py_obj);
  return 0;
}

PythonList PythonDictionary::GetKeys() const {
  if (IsValid())
    return PythonList(PyRefType::Owned, PyDict_Keys(m_py_obj));
  return PythonList(PyInitialValue::Invalid);
}

PythonObject PythonDictionary::GetItemForKey(const PythonObject &key) const {
  if (IsAllocated() && key.IsValid())
    return PythonObject(PyRefType::Borrowed,
                        PyDict_GetItem(m_py_obj, key.get()));
  return PythonObject();
}

void PythonDictionary::SetItemForKey(const PythonObject &key,
                                     const PythonObject &value) {
  if (IsAllocated() && key.IsValid() && value.IsValid())
    PyDict_SetItem(m_py_obj, key.get(), value.get());
}

StructuredData::DictionarySP
PythonDictionary::CreateStructuredDictionary() const {
  StructuredData::DictionarySP result(new StructuredData::Dictionary);
  PythonList keys(GetKeys());
  uint32_t num_keys = keys.GetSize();
  for (uint32_t i = 0; i < num_keys; ++i) {
    PythonObject key = keys.GetItemAtIndex(i);
    PythonObject value = GetItemForKey(key);
    StructuredData::ObjectSP structured_value = value.CreateStructuredObject();
    result->AddItem(key.Str().GetString(), structured_value);
  }
  return result;
}

PythonModule::PythonModule() : PythonObject() {}

PythonModule::PythonModule(PyRefType type, PyObject *py_obj) {
  Reset(type, py_obj); // Use "Reset()" to ensure that py_obj is a module
}

PythonModule::PythonModule(const PythonModule &dict) : PythonObject(dict) {}

PythonModule::~PythonModule() {}

PythonModule PythonModule::BuiltinsModule() {
#if PY_MAJOR_VERSION >= 3
  return AddModule("builtins");
#else
  return AddModule("__builtin__");
#endif
}

PythonModule PythonModule::MainModule() { return AddModule("__main__"); }

PythonModule PythonModule::AddModule(llvm::StringRef module) {
  std::string str = module.str();
  return PythonModule(PyRefType::Borrowed, PyImport_AddModule(str.c_str()));
}

PythonModule PythonModule::ImportModule(llvm::StringRef module) {
  std::string str = module.str();
  return PythonModule(PyRefType::Owned, PyImport_ImportModule(str.c_str()));
}

bool PythonModule::Check(PyObject *py_obj) {
  if (!py_obj)
    return false;

  return PyModule_Check(py_obj);
}

void PythonModule::Reset(PyRefType type, PyObject *py_obj) {
  // Grab the desired reference type so that if we end up rejecting `py_obj` it
  // still gets decremented if necessary.
  PythonObject result(type, py_obj);

  if (!PythonModule::Check(py_obj)) {
    PythonObject::Reset();
    return;
  }

  // Calling PythonObject::Reset(const PythonObject&) will lead to stack
  // overflow since it calls back into the virtual implementation.
  PythonObject::Reset(PyRefType::Borrowed, result.get());
}

PythonDictionary PythonModule::GetDictionary() const {
  return PythonDictionary(PyRefType::Borrowed, PyModule_GetDict(m_py_obj));
}

PythonCallable::PythonCallable() : PythonObject() {}

PythonCallable::PythonCallable(PyRefType type, PyObject *py_obj) {
  Reset(type, py_obj); // Use "Reset()" to ensure that py_obj is a callable
}

PythonCallable::PythonCallable(const PythonCallable &callable)
    : PythonObject(callable) {}

PythonCallable::~PythonCallable() {}

bool PythonCallable::Check(PyObject *py_obj) {
  if (!py_obj)
    return false;

  return PyCallable_Check(py_obj);
}

void PythonCallable::Reset(PyRefType type, PyObject *py_obj) {
  // Grab the desired reference type so that if we end up rejecting `py_obj` it
  // still gets decremented if necessary.
  PythonObject result(type, py_obj);

  if (!PythonCallable::Check(py_obj)) {
    PythonObject::Reset();
    return;
  }

  // Calling PythonObject::Reset(const PythonObject&) will lead to stack
  // overflow since it calls back into the virtual implementation.
  PythonObject::Reset(PyRefType::Borrowed, result.get());
}

PythonCallable::ArgInfo PythonCallable::GetNumArguments() const {
  ArgInfo result = {0, false, false, false};
  if (!IsValid())
    return result;

  PyObject *py_func_obj = m_py_obj;
  if (PyMethod_Check(py_func_obj)) {
    py_func_obj = PyMethod_GET_FUNCTION(py_func_obj);
    PythonObject im_self = GetAttributeValue("im_self");
    if (im_self.IsValid() && !im_self.IsNone())
      result.is_bound_method = true;
  } else {
    // see if this is a callable object with an __call__ method
    if (!PyFunction_Check(py_func_obj)) {
      PythonObject __call__ = GetAttributeValue("__call__");
      if (__call__.IsValid()) {
        auto __callable__ = __call__.AsType<PythonCallable>();
        if (__callable__.IsValid()) {
          py_func_obj = PyMethod_GET_FUNCTION(__callable__.get());
          PythonObject im_self = GetAttributeValue("im_self");
          if (im_self.IsValid() && !im_self.IsNone())
            result.is_bound_method = true;
        }
      }
    }
  }

  if (!py_func_obj)
    return result;

  PyCodeObject *code = (PyCodeObject *)PyFunction_GET_CODE(py_func_obj);
  if (!code)
    return result;

  result.count = code->co_argcount;
  result.has_varargs = !!(code->co_flags & CO_VARARGS);
  result.has_kwargs = !!(code->co_flags & CO_VARKEYWORDS);
  return result;
}

PythonObject PythonCallable::operator()() {
  return PythonObject(PyRefType::Owned, PyObject_CallObject(m_py_obj, nullptr));
}

PythonObject PythonCallable::
operator()(std::initializer_list<PyObject *> args) {
  PythonTuple arg_tuple(args);
  return PythonObject(PyRefType::Owned,
                      PyObject_CallObject(m_py_obj, arg_tuple.get()));
}

PythonObject PythonCallable::
operator()(std::initializer_list<PythonObject> args) {
  PythonTuple arg_tuple(args);
  return PythonObject(PyRefType::Owned,
                      PyObject_CallObject(m_py_obj, arg_tuple.get()));
}

PythonFile::PythonFile() : PythonObject() {}

PythonFile::PythonFile(File &file, const char *mode) { Reset(file, mode); }

PythonFile::PythonFile(const char *path, const char *mode) {
  lldb_private::File file;
  FileSystem::Instance().Open(file, FileSpec(path), GetOptionsFromMode(mode));
  Reset(file, mode);
}

PythonFile::PythonFile(PyRefType type, PyObject *o) { Reset(type, o); }

PythonFile::~PythonFile() {}

bool PythonFile::Check(PyObject *py_obj) {
#if PY_MAJOR_VERSION < 3
  return PyFile_Check(py_obj);
#else
  // In Python 3, there is no `PyFile_Check`, and in fact PyFile is not even a
  // first-class object type anymore.  `PyFile_FromFd` is just a thin wrapper
  // over `io.open()`, which returns some object derived from `io.IOBase`. As a
  // result, the only way to detect a file in Python 3 is to check whether it
  // inherits from `io.IOBase`.  Since it is possible for non-files to also
  // inherit from `io.IOBase`, we additionally verify that it has the `fileno`
  // attribute, which should guarantee that it is backed by the file system.
  PythonObject io_module(PyRefType::Owned, PyImport_ImportModule("io"));
  PythonDictionary io_dict(PyRefType::Borrowed,
                           PyModule_GetDict(io_module.get()));
  PythonObject io_base_class = io_dict.GetItemForKey(PythonString("IOBase"));

  PythonObject object_type(PyRefType::Owned, PyObject_Type(py_obj));

  if (1 != PyObject_IsSubclass(object_type.get(), io_base_class.get()))
    return false;
  if (!object_type.HasAttribute("fileno"))
    return false;

  return true;
#endif
}

void PythonFile::Reset(PyRefType type, PyObject *py_obj) {
  // Grab the desired reference type so that if we end up rejecting `py_obj` it
  // still gets decremented if necessary.
  PythonObject result(type, py_obj);

  if (!PythonFile::Check(py_obj)) {
    PythonObject::Reset();
    return;
  }

  // Calling PythonObject::Reset(const PythonObject&) will lead to stack
  // overflow since it calls back into the virtual implementation.
  PythonObject::Reset(PyRefType::Borrowed, result.get());
}

void PythonFile::Reset(File &file, const char *mode) {
  if (!file.IsValid()) {
    Reset();
    return;
  }

  char *cmode = const_cast<char *>(mode);
#if PY_MAJOR_VERSION >= 3
  Reset(PyRefType::Owned, PyFile_FromFd(file.GetDescriptor(), nullptr, cmode,
                                        -1, nullptr, "ignore", nullptr, 0));
#else
  // Read through the Python source, doesn't seem to modify these strings
  Reset(PyRefType::Owned,
        PyFile_FromFile(file.GetStream(), const_cast<char *>(""), cmode,
                        nullptr));
#endif
}

uint32_t PythonFile::GetOptionsFromMode(llvm::StringRef mode) {
  if (mode.empty())
    return 0;

  return llvm::StringSwitch<uint32_t>(mode.str())
      .Case("r", File::eOpenOptionRead)
      .Case("w", File::eOpenOptionWrite)
      .Case("a", File::eOpenOptionWrite | File::eOpenOptionAppend |
                     File::eOpenOptionCanCreate)
      .Case("r+", File::eOpenOptionRead | File::eOpenOptionWrite)
      .Case("w+", File::eOpenOptionRead | File::eOpenOptionWrite |
                      File::eOpenOptionCanCreate | File::eOpenOptionTruncate)
      .Case("a+", File::eOpenOptionRead | File::eOpenOptionWrite |
                      File::eOpenOptionAppend | File::eOpenOptionCanCreate)
      .Default(0);
}

bool PythonFile::GetUnderlyingFile(File &file) const {
  if (!IsValid())
    return false;

  file.Close();
  // We don't own the file descriptor returned by this function, make sure the
  // File object knows about that.
  file.SetDescriptor(PyObject_AsFileDescriptor(m_py_obj), false);
  PythonString py_mode = GetAttributeValue("mode").AsType<PythonString>();
  file.SetOptions(PythonFile::GetOptionsFromMode(py_mode.GetString()));
  return file.IsValid();
}

#endif
