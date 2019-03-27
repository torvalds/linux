//===-- PythonDataObjects.h--------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_PLUGINS_SCRIPTINTERPRETER_PYTHON_PYTHONDATAOBJECTS_H
#define LLDB_PLUGINS_SCRIPTINTERPRETER_PYTHON_PYTHONDATAOBJECTS_H

#ifndef LLDB_DISABLE_PYTHON

// LLDB Python header must be included first
#include "lldb-python.h"

#include "lldb/Utility/Flags.h"

#include "lldb/Host/File.h"
#include "lldb/Interpreter/OptionValue.h"
#include "lldb/Utility/ConstString.h"
#include "lldb/Utility/StructuredData.h"
#include "lldb/lldb-defines.h"

#include "llvm/ADT/ArrayRef.h"

namespace lldb_private {

class PythonBytes;
class PythonString;
class PythonList;
class PythonDictionary;
class PythonInteger;

class StructuredPythonObject : public StructuredData::Generic {
public:
  StructuredPythonObject() : StructuredData::Generic() {}

  StructuredPythonObject(void *obj) : StructuredData::Generic(obj) {
    Py_XINCREF(GetValue());
  }

  ~StructuredPythonObject() override {
    if (Py_IsInitialized())
      Py_XDECREF(GetValue());
    SetValue(nullptr);
  }

  bool IsValid() const override { return GetValue() && GetValue() != Py_None; }

  void Dump(Stream &s, bool pretty_print = true) const override;

private:
  DISALLOW_COPY_AND_ASSIGN(StructuredPythonObject);
};

enum class PyObjectType {
  Unknown,
  None,
  Integer,
  Dictionary,
  List,
  String,
  Bytes,
  ByteArray,
  Module,
  Callable,
  Tuple,
  File
};

enum class PyRefType {
  Borrowed, // We are not given ownership of the incoming PyObject.
            // We cannot safely hold it without calling Py_INCREF.
  Owned     // We have ownership of the incoming PyObject.  We should
            // not call Py_INCREF.
};

enum class PyInitialValue { Invalid, Empty };

class PythonObject {
public:
  PythonObject() : m_py_obj(nullptr) {}

  PythonObject(PyRefType type, PyObject *py_obj) : m_py_obj(nullptr) {
    Reset(type, py_obj);
  }

  PythonObject(const PythonObject &rhs) : m_py_obj(nullptr) { Reset(rhs); }

  virtual ~PythonObject() { Reset(); }

  void Reset() {
    // Avoid calling the virtual method since it's not necessary
    // to actually validate the type of the PyObject if we're
    // just setting to null.
    if (Py_IsInitialized())
      Py_XDECREF(m_py_obj);
    m_py_obj = nullptr;
  }

  void Reset(const PythonObject &rhs) {
    // Avoid calling the virtual method if it's not necessary
    // to actually validate the type of the PyObject.
    if (!rhs.IsValid())
      Reset();
    else
      Reset(PyRefType::Borrowed, rhs.m_py_obj);
  }

  // PythonObject is implicitly convertible to PyObject *, which will call the
  // wrong overload.  We want to explicitly disallow this, since a PyObject
  // *always* owns its reference.  Therefore the overload which takes a
  // PyRefType doesn't make sense, and the copy constructor should be used.
  void Reset(PyRefType type, const PythonObject &ref) = delete;

  virtual void Reset(PyRefType type, PyObject *py_obj) {
    if (py_obj == m_py_obj)
      return;

    if (Py_IsInitialized())
      Py_XDECREF(m_py_obj);

    m_py_obj = py_obj;

    // If this is a borrowed reference, we need to convert it to
    // an owned reference by incrementing it.  If it is an owned
    // reference (for example the caller allocated it with PyDict_New()
    // then we must *not* increment it.
    if (Py_IsInitialized() && type == PyRefType::Borrowed)
      Py_XINCREF(m_py_obj);
  }

  void Dump() const {
    if (m_py_obj)
      _PyObject_Dump(m_py_obj);
    else
      puts("NULL");
  }

  void Dump(Stream &strm) const;

  PyObject *get() const { return m_py_obj; }

  PyObject *release() {
    PyObject *result = m_py_obj;
    m_py_obj = nullptr;
    return result;
  }

  PythonObject &operator=(const PythonObject &other) {
    Reset(PyRefType::Borrowed, other.get());
    return *this;
  }

  PyObjectType GetObjectType() const;

  PythonString Repr() const;

  PythonString Str() const;

  static PythonObject ResolveNameWithDictionary(llvm::StringRef name,
                                                const PythonDictionary &dict);

  template <typename T>
  static T ResolveNameWithDictionary(llvm::StringRef name,
                                     const PythonDictionary &dict) {
    return ResolveNameWithDictionary(name, dict).AsType<T>();
  }

  PythonObject ResolveName(llvm::StringRef name) const;

  template <typename T> T ResolveName(llvm::StringRef name) const {
    return ResolveName(name).AsType<T>();
  }

  bool HasAttribute(llvm::StringRef attribute) const;

  PythonObject GetAttributeValue(llvm::StringRef attribute) const;

  bool IsValid() const;

  bool IsAllocated() const;

  bool IsNone() const;

  template <typename T> T AsType() const {
    if (!T::Check(m_py_obj))
      return T();
    return T(PyRefType::Borrowed, m_py_obj);
  }

  StructuredData::ObjectSP CreateStructuredObject() const;

protected:
  PyObject *m_py_obj;
};

class PythonBytes : public PythonObject {
public:
  PythonBytes();
  explicit PythonBytes(llvm::ArrayRef<uint8_t> bytes);
  PythonBytes(const uint8_t *bytes, size_t length);
  PythonBytes(PyRefType type, PyObject *o);
  PythonBytes(const PythonBytes &object);

  ~PythonBytes() override;

  static bool Check(PyObject *py_obj);

  // Bring in the no-argument base class version
  using PythonObject::Reset;

  void Reset(PyRefType type, PyObject *py_obj) override;

  llvm::ArrayRef<uint8_t> GetBytes() const;

  size_t GetSize() const;

  void SetBytes(llvm::ArrayRef<uint8_t> stringbytes);

  StructuredData::StringSP CreateStructuredString() const;
};

class PythonByteArray : public PythonObject {
public:
  PythonByteArray();
  explicit PythonByteArray(llvm::ArrayRef<uint8_t> bytes);
  PythonByteArray(const uint8_t *bytes, size_t length);
  PythonByteArray(PyRefType type, PyObject *o);
  PythonByteArray(const PythonBytes &object);

  ~PythonByteArray() override;

  static bool Check(PyObject *py_obj);

  // Bring in the no-argument base class version
  using PythonObject::Reset;

  void Reset(PyRefType type, PyObject *py_obj) override;

  llvm::ArrayRef<uint8_t> GetBytes() const;

  size_t GetSize() const;

  void SetBytes(llvm::ArrayRef<uint8_t> stringbytes);

  StructuredData::StringSP CreateStructuredString() const;
};

class PythonString : public PythonObject {
public:
  PythonString();
  explicit PythonString(llvm::StringRef string);
  explicit PythonString(const char *string);
  PythonString(PyRefType type, PyObject *o);
  PythonString(const PythonString &object);

  ~PythonString() override;

  static bool Check(PyObject *py_obj);

  // Bring in the no-argument base class version
  using PythonObject::Reset;

  void Reset(PyRefType type, PyObject *py_obj) override;

  llvm::StringRef GetString() const;

  size_t GetSize() const;

  void SetString(llvm::StringRef string);

  StructuredData::StringSP CreateStructuredString() const;
};

class PythonInteger : public PythonObject {
public:
  PythonInteger();
  explicit PythonInteger(int64_t value);
  PythonInteger(PyRefType type, PyObject *o);
  PythonInteger(const PythonInteger &object);

  ~PythonInteger() override;

  static bool Check(PyObject *py_obj);

  // Bring in the no-argument base class version
  using PythonObject::Reset;

  void Reset(PyRefType type, PyObject *py_obj) override;

  int64_t GetInteger() const;

  void SetInteger(int64_t value);

  StructuredData::IntegerSP CreateStructuredInteger() const;
};

class PythonList : public PythonObject {
public:
  PythonList() {}
  explicit PythonList(PyInitialValue value);
  explicit PythonList(int list_size);
  PythonList(PyRefType type, PyObject *o);
  PythonList(const PythonList &list);

  ~PythonList() override;

  static bool Check(PyObject *py_obj);

  // Bring in the no-argument base class version
  using PythonObject::Reset;

  void Reset(PyRefType type, PyObject *py_obj) override;

  uint32_t GetSize() const;

  PythonObject GetItemAtIndex(uint32_t index) const;

  void SetItemAtIndex(uint32_t index, const PythonObject &object);

  void AppendItem(const PythonObject &object);

  StructuredData::ArraySP CreateStructuredArray() const;
};

class PythonTuple : public PythonObject {
public:
  PythonTuple() {}
  explicit PythonTuple(PyInitialValue value);
  explicit PythonTuple(int tuple_size);
  PythonTuple(PyRefType type, PyObject *o);
  PythonTuple(const PythonTuple &tuple);
  PythonTuple(std::initializer_list<PythonObject> objects);
  PythonTuple(std::initializer_list<PyObject *> objects);

  ~PythonTuple() override;

  static bool Check(PyObject *py_obj);

  // Bring in the no-argument base class version
  using PythonObject::Reset;

  void Reset(PyRefType type, PyObject *py_obj) override;

  uint32_t GetSize() const;

  PythonObject GetItemAtIndex(uint32_t index) const;

  void SetItemAtIndex(uint32_t index, const PythonObject &object);

  StructuredData::ArraySP CreateStructuredArray() const;
};

class PythonDictionary : public PythonObject {
public:
  PythonDictionary() {}
  explicit PythonDictionary(PyInitialValue value);
  PythonDictionary(PyRefType type, PyObject *o);
  PythonDictionary(const PythonDictionary &dict);

  ~PythonDictionary() override;

  static bool Check(PyObject *py_obj);

  // Bring in the no-argument base class version
  using PythonObject::Reset;

  void Reset(PyRefType type, PyObject *py_obj) override;

  uint32_t GetSize() const;

  PythonList GetKeys() const;

  PythonObject GetItemForKey(const PythonObject &key) const;
  void SetItemForKey(const PythonObject &key, const PythonObject &value);

  StructuredData::DictionarySP CreateStructuredDictionary() const;
};

class PythonModule : public PythonObject {
public:
  PythonModule();
  PythonModule(PyRefType type, PyObject *o);
  PythonModule(const PythonModule &dict);

  ~PythonModule() override;

  static bool Check(PyObject *py_obj);

  static PythonModule BuiltinsModule();

  static PythonModule MainModule();

  static PythonModule AddModule(llvm::StringRef module);

  static PythonModule ImportModule(llvm::StringRef module);

  // Bring in the no-argument base class version
  using PythonObject::Reset;

  void Reset(PyRefType type, PyObject *py_obj) override;

  PythonDictionary GetDictionary() const;
};

class PythonCallable : public PythonObject {
public:
  struct ArgInfo {
    size_t count;
    bool is_bound_method : 1;
    bool has_varargs : 1;
    bool has_kwargs : 1;
  };

  PythonCallable();
  PythonCallable(PyRefType type, PyObject *o);
  PythonCallable(const PythonCallable &dict);

  ~PythonCallable() override;

  static bool Check(PyObject *py_obj);

  // Bring in the no-argument base class version
  using PythonObject::Reset;

  void Reset(PyRefType type, PyObject *py_obj) override;

  ArgInfo GetNumArguments() const;

  PythonObject operator()();

  PythonObject operator()(std::initializer_list<PyObject *> args);

  PythonObject operator()(std::initializer_list<PythonObject> args);

  template <typename Arg, typename... Args>
  PythonObject operator()(const Arg &arg, Args... args) {
    return operator()({arg, args...});
  }
};

class PythonFile : public PythonObject {
public:
  PythonFile();
  PythonFile(File &file, const char *mode);
  PythonFile(const char *path, const char *mode);
  PythonFile(PyRefType type, PyObject *o);

  ~PythonFile() override;

  static bool Check(PyObject *py_obj);

  using PythonObject::Reset;

  void Reset(PyRefType type, PyObject *py_obj) override;
  void Reset(File &file, const char *mode);

  static uint32_t GetOptionsFromMode(llvm::StringRef mode);

  bool GetUnderlyingFile(File &file) const;
};

} // namespace lldb_private

#endif

#endif // LLDB_PLUGINS_SCRIPTINTERPRETER_PYTHON_PYTHONDATAOBJECTS_H
