//===-- FormattersContainer.h -----------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef lldb_FormattersContainer_h_
#define lldb_FormattersContainer_h_

#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <string>

#include "lldb/lldb-public.h"

#include "lldb/Core/ValueObject.h"
#include "lldb/DataFormatters/FormatClasses.h"
#include "lldb/DataFormatters/TypeFormat.h"
#include "lldb/DataFormatters/TypeSummary.h"
#include "lldb/DataFormatters/TypeSynthetic.h"
#include "lldb/DataFormatters/TypeValidator.h"
#include "lldb/Symbol/CompilerType.h"
#include "lldb/Utility/RegularExpression.h"
#include "lldb/Utility/StringLexer.h"

namespace lldb_private {

class IFormatChangeListener {
public:
  virtual ~IFormatChangeListener() = default;

  virtual void Changed() = 0;

  virtual uint32_t GetCurrentRevision() = 0;
};

// if the user tries to add formatters for, say, "struct Foo" those will not
// match any type because of the way we strip qualifiers from typenames this
// method looks for the case where the user is adding a "class","struct","enum"
// or "union" Foo and strips the unnecessary qualifier
static inline ConstString GetValidTypeName_Impl(const ConstString &type) {
  if (type.IsEmpty())
    return type;

  std::string type_cstr(type.AsCString());
  lldb_utility::StringLexer type_lexer(type_cstr);

  type_lexer.AdvanceIf("class ");
  type_lexer.AdvanceIf("enum ");
  type_lexer.AdvanceIf("struct ");
  type_lexer.AdvanceIf("union ");

  while (type_lexer.NextIf({' ', '\t', '\v', '\f'}).first)
    ;

  return ConstString(type_lexer.GetUnlexed());
}

template <typename KeyType, typename ValueType> class FormattersContainer;

template <typename KeyType, typename ValueType> class FormatMap {
public:
  typedef typename ValueType::SharedPointer ValueSP;
  typedef std::map<KeyType, ValueSP> MapType;
  typedef typename MapType::iterator MapIterator;
  typedef std::function<bool(KeyType, const ValueSP &)> ForEachCallback;

  FormatMap(IFormatChangeListener *lst)
      : m_map(), m_map_mutex(), listener(lst) {}

  void Add(KeyType name, const ValueSP &entry) {
    if (listener)
      entry->GetRevision() = listener->GetCurrentRevision();
    else
      entry->GetRevision() = 0;

    std::lock_guard<std::recursive_mutex> guard(m_map_mutex);
    m_map[name] = entry;
    if (listener)
      listener->Changed();
  }

  bool Delete(KeyType name) {
    std::lock_guard<std::recursive_mutex> guard(m_map_mutex);
    MapIterator iter = m_map.find(name);
    if (iter == m_map.end())
      return false;
    m_map.erase(name);
    if (listener)
      listener->Changed();
    return true;
  }

  void Clear() {
    std::lock_guard<std::recursive_mutex> guard(m_map_mutex);
    m_map.clear();
    if (listener)
      listener->Changed();
  }

  bool Get(KeyType name, ValueSP &entry) {
    std::lock_guard<std::recursive_mutex> guard(m_map_mutex);
    MapIterator iter = m_map.find(name);
    if (iter == m_map.end())
      return false;
    entry = iter->second;
    return true;
  }

  void ForEach(ForEachCallback callback) {
    if (callback) {
      std::lock_guard<std::recursive_mutex> guard(m_map_mutex);
      MapIterator pos, end = m_map.end();
      for (pos = m_map.begin(); pos != end; pos++) {
        KeyType type = pos->first;
        if (!callback(type, pos->second))
          break;
      }
    }
  }

  uint32_t GetCount() { return m_map.size(); }

  ValueSP GetValueAtIndex(size_t index) {
    std::lock_guard<std::recursive_mutex> guard(m_map_mutex);
    MapIterator iter = m_map.begin();
    MapIterator end = m_map.end();
    while (index > 0) {
      iter++;
      index--;
      if (end == iter)
        return ValueSP();
    }
    return iter->second;
  }

  KeyType GetKeyAtIndex(size_t index) {
    std::lock_guard<std::recursive_mutex> guard(m_map_mutex);
    MapIterator iter = m_map.begin();
    MapIterator end = m_map.end();
    while (index > 0) {
      iter++;
      index--;
      if (end == iter)
        return KeyType();
    }
    return iter->first;
  }

protected:
  MapType m_map;
  std::recursive_mutex m_map_mutex;
  IFormatChangeListener *listener;

  MapType &map() { return m_map; }

  std::recursive_mutex &mutex() { return m_map_mutex; }

  friend class FormattersContainer<KeyType, ValueType>;
  friend class FormatManager;
};

template <typename KeyType, typename ValueType> class FormattersContainer {
protected:
  typedef FormatMap<KeyType, ValueType> BackEndType;

public:
  typedef typename BackEndType::MapType MapType;
  typedef typename MapType::iterator MapIterator;
  typedef typename MapType::key_type MapKeyType;
  typedef typename MapType::mapped_type MapValueType;
  typedef typename BackEndType::ForEachCallback ForEachCallback;
  typedef typename std::shared_ptr<FormattersContainer<KeyType, ValueType>>
      SharedPointer;

  friend class TypeCategoryImpl;

  FormattersContainer(std::string name, IFormatChangeListener *lst)
      : m_format_map(lst), m_name(name) {}

  void Add(const MapKeyType &type, const MapValueType &entry) {
    Add_Impl(type, entry, static_cast<KeyType *>(nullptr));
  }

  bool Delete(ConstString type) {
    return Delete_Impl(type, static_cast<KeyType *>(nullptr));
  }

  bool Get(ValueObject &valobj, MapValueType &entry,
           lldb::DynamicValueType use_dynamic, uint32_t *why = nullptr) {
    uint32_t value = lldb_private::eFormatterChoiceCriterionDirectChoice;
    CompilerType ast_type(valobj.GetCompilerType());
    bool ret = Get(valobj, ast_type, entry, use_dynamic, value);
    if (ret)
      entry = MapValueType(entry);
    else
      entry = MapValueType();
    if (why)
      *why = value;
    return ret;
  }

  bool Get(ConstString type, MapValueType &entry) {
    return Get_Impl(type, entry, static_cast<KeyType *>(nullptr));
  }

  bool GetExact(ConstString type, MapValueType &entry) {
    return GetExact_Impl(type, entry, static_cast<KeyType *>(nullptr));
  }

  MapValueType GetAtIndex(size_t index) {
    return m_format_map.GetValueAtIndex(index);
  }

  lldb::TypeNameSpecifierImplSP GetTypeNameSpecifierAtIndex(size_t index) {
    return GetTypeNameSpecifierAtIndex_Impl(index,
                                            static_cast<KeyType *>(nullptr));
  }

  void Clear() { m_format_map.Clear(); }

  void ForEach(ForEachCallback callback) { m_format_map.ForEach(callback); }

  uint32_t GetCount() { return m_format_map.GetCount(); }

protected:
  BackEndType m_format_map;
  std::string m_name;

  DISALLOW_COPY_AND_ASSIGN(FormattersContainer);

  void Add_Impl(const MapKeyType &type, const MapValueType &entry,
                lldb::RegularExpressionSP *dummy) {
    m_format_map.Add(type, entry);
  }

  void Add_Impl(const ConstString &type, const MapValueType &entry,
                ConstString *dummy) {
    m_format_map.Add(GetValidTypeName_Impl(type), entry);
  }

  bool Delete_Impl(ConstString type, ConstString *dummy) {
    return m_format_map.Delete(type);
  }

  bool Delete_Impl(ConstString type, lldb::RegularExpressionSP *dummy) {
    std::lock_guard<std::recursive_mutex> guard(m_format_map.mutex());
    MapIterator pos, end = m_format_map.map().end();
    for (pos = m_format_map.map().begin(); pos != end; pos++) {
      lldb::RegularExpressionSP regex = pos->first;
      if (type.GetStringRef() == regex->GetText()) {
        m_format_map.map().erase(pos);
        if (m_format_map.listener)
          m_format_map.listener->Changed();
        return true;
      }
    }
    return false;
  }

  bool Get_Impl(ConstString type, MapValueType &entry, ConstString *dummy) {
    return m_format_map.Get(type, entry);
  }

  bool GetExact_Impl(ConstString type, MapValueType &entry,
                     ConstString *dummy) {
    return Get_Impl(type, entry, static_cast<KeyType *>(nullptr));
  }

  lldb::TypeNameSpecifierImplSP
  GetTypeNameSpecifierAtIndex_Impl(size_t index, ConstString *dummy) {
    ConstString key = m_format_map.GetKeyAtIndex(index);
    if (key)
      return lldb::TypeNameSpecifierImplSP(
          new TypeNameSpecifierImpl(key.AsCString(), false));
    else
      return lldb::TypeNameSpecifierImplSP();
  }

  lldb::TypeNameSpecifierImplSP
  GetTypeNameSpecifierAtIndex_Impl(size_t index,
                                   lldb::RegularExpressionSP *dummy) {
    lldb::RegularExpressionSP regex = m_format_map.GetKeyAtIndex(index);
    if (regex.get() == nullptr)
      return lldb::TypeNameSpecifierImplSP();
    return lldb::TypeNameSpecifierImplSP(
        new TypeNameSpecifierImpl(regex->GetText().str().c_str(), true));
  }

  bool Get_Impl(ConstString key, MapValueType &value,
                lldb::RegularExpressionSP *dummy) {
    llvm::StringRef key_str = key.GetStringRef();
    std::lock_guard<std::recursive_mutex> guard(m_format_map.mutex());
    MapIterator pos, end = m_format_map.map().end();
    for (pos = m_format_map.map().begin(); pos != end; pos++) {
      lldb::RegularExpressionSP regex = pos->first;
      if (regex->Execute(key_str)) {
        value = pos->second;
        return true;
      }
    }
    return false;
  }

  bool GetExact_Impl(ConstString key, MapValueType &value,
                     lldb::RegularExpressionSP *dummy) {
    std::lock_guard<std::recursive_mutex> guard(m_format_map.mutex());
    MapIterator pos, end = m_format_map.map().end();
    for (pos = m_format_map.map().begin(); pos != end; pos++) {
      lldb::RegularExpressionSP regex = pos->first;
      if (regex->GetText() == key.GetStringRef()) {
        value = pos->second;
        return true;
      }
    }
    return false;
  }

  bool Get(const FormattersMatchVector &candidates, MapValueType &entry,
           uint32_t *reason) {
    for (const FormattersMatchCandidate &candidate : candidates) {
      if (Get(candidate.GetTypeName(), entry)) {
        if (candidate.IsMatch(entry) == false) {
          entry.reset();
          continue;
        } else {
          if (reason)
            *reason = candidate.GetReason();
          return true;
        }
      }
    }
    return false;
  }
};

} // namespace lldb_private

#endif // lldb_FormattersContainer_h_
