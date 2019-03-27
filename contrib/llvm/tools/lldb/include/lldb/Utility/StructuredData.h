//===-- StructuredData.h ----------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef liblldb_StructuredData_h_
#define liblldb_StructuredData_h_

#include "llvm/ADT/StringRef.h"

#include "lldb/Utility/ConstString.h"
#include "lldb/Utility/FileSpec.h"
#include "lldb/lldb-enumerations.h"

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

namespace lldb_private {
class Status;
}
namespace lldb_private {
class Stream;
}

namespace lldb_private {

//----------------------------------------------------------------------
/// @class StructuredData StructuredData.h "lldb/Utility/StructuredData.h"
/// A class which can hold structured data
///
/// The StructuredData class is designed to hold the data from a JSON or plist
/// style file -- a serialized data structure with dictionaries (maps,
/// hashes), arrays, and concrete values like integers, floating point
/// numbers, strings, booleans.
///
/// StructuredData does not presuppose any knowledge of the schema for the
/// data it is holding; it can parse JSON data, for instance, and other parts
/// of lldb can iterate through the parsed data set to find keys and values
/// that may be present.
//----------------------------------------------------------------------

class StructuredData {
public:
  class Object;
  class Array;
  class Integer;
  class Float;
  class Boolean;
  class String;
  class Dictionary;
  class Generic;

  typedef std::shared_ptr<Object> ObjectSP;
  typedef std::shared_ptr<Array> ArraySP;
  typedef std::shared_ptr<Integer> IntegerSP;
  typedef std::shared_ptr<Float> FloatSP;
  typedef std::shared_ptr<Boolean> BooleanSP;
  typedef std::shared_ptr<String> StringSP;
  typedef std::shared_ptr<Dictionary> DictionarySP;
  typedef std::shared_ptr<Generic> GenericSP;

  class Object : public std::enable_shared_from_this<Object> {
  public:
    Object(lldb::StructuredDataType t = lldb::eStructuredDataTypeInvalid)
        : m_type(t) {}

    virtual ~Object() = default;

    virtual bool IsValid() const { return true; }

    virtual void Clear() { m_type = lldb::eStructuredDataTypeInvalid; }

    lldb::StructuredDataType GetType() const { return m_type; }

    void SetType(lldb::StructuredDataType t) { m_type = t; }

    Array *GetAsArray() {
      return ((m_type == lldb::eStructuredDataTypeArray)
                  ? static_cast<Array *>(this)
                  : nullptr);
    }

    Dictionary *GetAsDictionary() {
      return ((m_type == lldb::eStructuredDataTypeDictionary)
                  ? static_cast<Dictionary *>(this)
                  : nullptr);
    }

    Integer *GetAsInteger() {
      return ((m_type == lldb::eStructuredDataTypeInteger)
                  ? static_cast<Integer *>(this)
                  : nullptr);
    }

    uint64_t GetIntegerValue(uint64_t fail_value = 0) {
      Integer *integer = GetAsInteger();
      return ((integer != nullptr) ? integer->GetValue() : fail_value);
    }

    Float *GetAsFloat() {
      return ((m_type == lldb::eStructuredDataTypeFloat)
                  ? static_cast<Float *>(this)
                  : nullptr);
    }

    double GetFloatValue(double fail_value = 0.0) {
      Float *f = GetAsFloat();
      return ((f != nullptr) ? f->GetValue() : fail_value);
    }

    Boolean *GetAsBoolean() {
      return ((m_type == lldb::eStructuredDataTypeBoolean)
                  ? static_cast<Boolean *>(this)
                  : nullptr);
    }

    bool GetBooleanValue(bool fail_value = false) {
      Boolean *b = GetAsBoolean();
      return ((b != nullptr) ? b->GetValue() : fail_value);
    }

    String *GetAsString() {
      return ((m_type == lldb::eStructuredDataTypeString)
                  ? static_cast<String *>(this)
                  : nullptr);
    }

    llvm::StringRef GetStringValue(const char *fail_value = nullptr) {
      String *s = GetAsString();
      if (s)
        return s->GetValue();

      return fail_value;
    }

    Generic *GetAsGeneric() {
      return ((m_type == lldb::eStructuredDataTypeGeneric)
                  ? static_cast<Generic *>(this)
                  : nullptr);
    }

    ObjectSP GetObjectForDotSeparatedPath(llvm::StringRef path);

    void DumpToStdout(bool pretty_print = true) const;

    virtual void Dump(Stream &s, bool pretty_print = true) const = 0;

  private:
    lldb::StructuredDataType m_type;
  };

  class Array : public Object {
  public:
    Array() : Object(lldb::eStructuredDataTypeArray) {}

    ~Array() override = default;

    bool
    ForEach(std::function<bool(Object *object)> const &foreach_callback) const {
      for (const auto &object_sp : m_items) {
        if (!foreach_callback(object_sp.get()))
          return false;
      }
      return true;
    }

    size_t GetSize() const { return m_items.size(); }

    ObjectSP operator[](size_t idx) {
      if (idx < m_items.size())
        return m_items[idx];
      return ObjectSP();
    }

    ObjectSP GetItemAtIndex(size_t idx) const {
      assert(idx < GetSize());
      if (idx < m_items.size())
        return m_items[idx];
      return ObjectSP();
    }

    template <class IntType>
    bool GetItemAtIndexAsInteger(size_t idx, IntType &result) const {
      ObjectSP value_sp = GetItemAtIndex(idx);
      if (value_sp.get()) {
        if (auto int_value = value_sp->GetAsInteger()) {
          result = static_cast<IntType>(int_value->GetValue());
          return true;
        }
      }
      return false;
    }

    template <class IntType>
    bool GetItemAtIndexAsInteger(size_t idx, IntType &result,
                                 IntType default_val) const {
      bool success = GetItemAtIndexAsInteger(idx, result);
      if (!success)
        result = default_val;
      return success;
    }

    bool GetItemAtIndexAsString(size_t idx, llvm::StringRef &result) const {
      ObjectSP value_sp = GetItemAtIndex(idx);
      if (value_sp.get()) {
        if (auto string_value = value_sp->GetAsString()) {
          result = string_value->GetValue();
          return true;
        }
      }
      return false;
    }

    bool GetItemAtIndexAsString(size_t idx, llvm::StringRef &result,
                                llvm::StringRef default_val) const {
      bool success = GetItemAtIndexAsString(idx, result);
      if (!success)
        result = default_val;
      return success;
    }

    bool GetItemAtIndexAsString(size_t idx, ConstString &result) const {
      ObjectSP value_sp = GetItemAtIndex(idx);
      if (value_sp.get()) {
        if (auto string_value = value_sp->GetAsString()) {
          result = ConstString(string_value->GetValue());
          return true;
        }
      }
      return false;
    }

    bool GetItemAtIndexAsString(size_t idx, ConstString &result,
                                const char *default_val) const {
      bool success = GetItemAtIndexAsString(idx, result);
      if (!success)
        result.SetCString(default_val);
      return success;
    }

    bool GetItemAtIndexAsDictionary(size_t idx, Dictionary *&result) const {
      result = nullptr;
      ObjectSP value_sp = GetItemAtIndex(idx);
      if (value_sp.get()) {
        result = value_sp->GetAsDictionary();
        return (result != nullptr);
      }
      return false;
    }

    bool GetItemAtIndexAsArray(size_t idx, Array *&result) const {
      result = nullptr;
      ObjectSP value_sp = GetItemAtIndex(idx);
      if (value_sp.get()) {
        result = value_sp->GetAsArray();
        return (result != nullptr);
      }
      return false;
    }

    void Push(ObjectSP item) { m_items.push_back(item); }

    void AddItem(ObjectSP item) { m_items.push_back(item); }

    void Dump(Stream &s, bool pretty_print = true) const override;

  protected:
    typedef std::vector<ObjectSP> collection;
    collection m_items;
  };

  class Integer : public Object {
  public:
    Integer(uint64_t i = 0)
        : Object(lldb::eStructuredDataTypeInteger), m_value(i) {}

    ~Integer() override = default;

    void SetValue(uint64_t value) { m_value = value; }

    uint64_t GetValue() { return m_value; }

    void Dump(Stream &s, bool pretty_print = true) const override;

  protected:
    uint64_t m_value;
  };

  class Float : public Object {
  public:
    Float(double d = 0.0)
        : Object(lldb::eStructuredDataTypeFloat), m_value(d) {}

    ~Float() override = default;

    void SetValue(double value) { m_value = value; }

    double GetValue() { return m_value; }

    void Dump(Stream &s, bool pretty_print = true) const override;

  protected:
    double m_value;
  };

  class Boolean : public Object {
  public:
    Boolean(bool b = false)
        : Object(lldb::eStructuredDataTypeBoolean), m_value(b) {}

    ~Boolean() override = default;

    void SetValue(bool value) { m_value = value; }

    bool GetValue() { return m_value; }

    void Dump(Stream &s, bool pretty_print = true) const override;

  protected:
    bool m_value;
  };

  class String : public Object {
  public:
    String() : Object(lldb::eStructuredDataTypeString) {}
    explicit String(llvm::StringRef S)
        : Object(lldb::eStructuredDataTypeString), m_value(S) {}

    void SetValue(llvm::StringRef S) { m_value = S; }

    llvm::StringRef GetValue() { return m_value; }

    void Dump(Stream &s, bool pretty_print = true) const override;

  protected:
    std::string m_value;
  };

  class Dictionary : public Object {
  public:
    Dictionary() : Object(lldb::eStructuredDataTypeDictionary), m_dict() {}

    ~Dictionary() override = default;

    size_t GetSize() const { return m_dict.size(); }

    void ForEach(std::function<bool(ConstString key, Object *object)> const
                     &callback) const {
      for (const auto &pair : m_dict) {
        if (!callback(pair.first, pair.second.get()))
          break;
      }
    }

    ObjectSP GetKeys() const {
      auto object_sp = std::make_shared<Array>();
      collection::const_iterator iter;
      for (iter = m_dict.begin(); iter != m_dict.end(); ++iter) {
        auto key_object_sp = std::make_shared<String>();
        key_object_sp->SetValue(iter->first.AsCString());
        object_sp->Push(key_object_sp);
      }
      return object_sp;
    }

    ObjectSP GetValueForKey(llvm::StringRef key) const {
      ObjectSP value_sp;
      if (!key.empty()) {
        ConstString key_cs(key);
        collection::const_iterator iter = m_dict.find(key_cs);
        if (iter != m_dict.end())
          value_sp = iter->second;
      }
      return value_sp;
    }

    bool GetValueForKeyAsBoolean(llvm::StringRef key, bool &result) const {
      bool success = false;
      ObjectSP value_sp = GetValueForKey(key);
      if (value_sp.get()) {
        Boolean *result_ptr = value_sp->GetAsBoolean();
        if (result_ptr) {
          result = result_ptr->GetValue();
          success = true;
        }
      }
      return success;
    }
    template <class IntType>
    bool GetValueForKeyAsInteger(llvm::StringRef key, IntType &result) const {
      ObjectSP value_sp = GetValueForKey(key);
      if (value_sp) {
        if (auto int_value = value_sp->GetAsInteger()) {
          result = static_cast<IntType>(int_value->GetValue());
          return true;
        }
      }
      return false;
    }

    template <class IntType>
    bool GetValueForKeyAsInteger(llvm::StringRef key, IntType &result,
                                 IntType default_val) const {
      bool success = GetValueForKeyAsInteger<IntType>(key, result);
      if (!success)
        result = default_val;
      return success;
    }

    bool GetValueForKeyAsString(llvm::StringRef key,
                                llvm::StringRef &result) const {
      ObjectSP value_sp = GetValueForKey(key);
      if (value_sp.get()) {
        if (auto string_value = value_sp->GetAsString()) {
          result = string_value->GetValue();
          return true;
        }
      }
      return false;
    }

    bool GetValueForKeyAsString(llvm::StringRef key, llvm::StringRef &result,
                                const char *default_val) const {
      bool success = GetValueForKeyAsString(key, result);
      if (!success) {
        if (default_val)
          result = default_val;
        else
          result = llvm::StringRef();
      }
      return success;
    }

    bool GetValueForKeyAsString(llvm::StringRef key,
                                ConstString &result) const {
      ObjectSP value_sp = GetValueForKey(key);
      if (value_sp.get()) {
        if (auto string_value = value_sp->GetAsString()) {
          result = ConstString(string_value->GetValue());
          return true;
        }
      }
      return false;
    }

    bool GetValueForKeyAsString(llvm::StringRef key, ConstString &result,
                                const char *default_val) const {
      bool success = GetValueForKeyAsString(key, result);
      if (!success)
        result.SetCString(default_val);
      return success;
    }

    bool GetValueForKeyAsDictionary(llvm::StringRef key,
                                    Dictionary *&result) const {
      result = nullptr;
      ObjectSP value_sp = GetValueForKey(key);
      if (value_sp.get()) {
        result = value_sp->GetAsDictionary();
        return (result != nullptr);
      }
      return false;
    }

    bool GetValueForKeyAsArray(llvm::StringRef key, Array *&result) const {
      result = nullptr;
      ObjectSP value_sp = GetValueForKey(key);
      if (value_sp.get()) {
        result = value_sp->GetAsArray();
        return (result != nullptr);
      }
      return false;
    }

    bool HasKey(llvm::StringRef key) const {
      ConstString key_cs(key);
      collection::const_iterator search = m_dict.find(key_cs);
      return search != m_dict.end();
    }

    void AddItem(llvm::StringRef key, ObjectSP value_sp) {
      ConstString key_cs(key);
      m_dict[key_cs] = value_sp;
    }

    void AddIntegerItem(llvm::StringRef key, uint64_t value) {
      AddItem(key, std::make_shared<Integer>(value));
    }

    void AddFloatItem(llvm::StringRef key, double value) {
      AddItem(key, std::make_shared<Float>(value));
    }

    void AddStringItem(llvm::StringRef key, llvm::StringRef value) {
      AddItem(key, std::make_shared<String>(std::move(value)));
    }

    void AddBooleanItem(llvm::StringRef key, bool value) {
      AddItem(key, std::make_shared<Boolean>(value));
    }

    void Dump(Stream &s, bool pretty_print = true) const override;

  protected:
    typedef std::map<ConstString, ObjectSP> collection;
    collection m_dict;
  };

  class Null : public Object {
  public:
    Null() : Object(lldb::eStructuredDataTypeNull) {}

    ~Null() override = default;

    bool IsValid() const override { return false; }

    void Dump(Stream &s, bool pretty_print = true) const override;
  };

  class Generic : public Object {
  public:
    explicit Generic(void *object = nullptr)
        : Object(lldb::eStructuredDataTypeGeneric), m_object(object) {}

    void SetValue(void *value) { m_object = value; }

    void *GetValue() const { return m_object; }

    bool IsValid() const override { return m_object != nullptr; }

    void Dump(Stream &s, bool pretty_print = true) const override;

  private:
    void *m_object;
  };

  static ObjectSP ParseJSON(std::string json_text);

  static ObjectSP ParseJSONFromFile(const FileSpec &file, Status &error);
};

} // namespace lldb_private

#endif // liblldb_StructuredData_h_
