//===-- StructuredDataImpl.h ------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef liblldb_StructuredDataImpl_h_
#define liblldb_StructuredDataImpl_h_

#include "lldb/Target/StructuredDataPlugin.h"
#include "lldb/Utility/Event.h"
#include "lldb/Utility/Status.h"
#include "lldb/Utility/Stream.h"
#include "lldb/Utility/StructuredData.h"
#include "lldb/lldb-enumerations.h"
#include "lldb/lldb-forward.h"
#include "llvm/ADT/StringRef.h"

#pragma mark--
#pragma mark StructuredDataImpl

namespace lldb_private {

class StructuredDataImpl {
public:
  StructuredDataImpl() : m_plugin_wp(), m_data_sp() {}

  StructuredDataImpl(const StructuredDataImpl &rhs) = default;

  StructuredDataImpl(const lldb::EventSP &event_sp)
      : m_plugin_wp(
            EventDataStructuredData::GetPluginFromEvent(event_sp.get())),
        m_data_sp(EventDataStructuredData::GetObjectFromEvent(event_sp.get())) {
  }

  ~StructuredDataImpl() = default;

  StructuredDataImpl &operator=(const StructuredDataImpl &rhs) = default;

  bool IsValid() const { return m_data_sp.get() != nullptr; }

  void Clear() {
    m_plugin_wp.reset();
    m_data_sp.reset();
  }

  Status GetAsJSON(Stream &stream) const {
    Status error;

    if (!m_data_sp) {
      error.SetErrorString("No structured data.");
      return error;
    }

    m_data_sp->Dump(stream);
    return error;
  }

  Status GetDescription(Stream &stream) const {
    Status error;

    if (!m_data_sp) {
      error.SetErrorString("Cannot pretty print structured data: "
                           "no data to print.");
      return error;
    }

    // Grab the plugin.
    auto plugin_sp = lldb::StructuredDataPluginSP(m_plugin_wp);
    if (!plugin_sp) {
      error.SetErrorString("Cannot pretty print structured data: "
                           "plugin doesn't exist.");
      return error;
    }

    // Get the data's description.
    return plugin_sp->GetDescription(m_data_sp, stream);
  }

  StructuredData::ObjectSP GetObjectSP() { return m_data_sp; }

  void SetObjectSP(const StructuredData::ObjectSP &obj) { m_data_sp = obj; }

  lldb::StructuredDataType GetType() const {
    return (m_data_sp ? m_data_sp->GetType() :
        lldb::eStructuredDataTypeInvalid);
  }

  size_t GetSize() const {
    if (!m_data_sp)
      return 0;

    if (m_data_sp->GetType() == lldb::eStructuredDataTypeDictionary) {
      auto dict = m_data_sp->GetAsDictionary();
      return (dict->GetSize());
    } else if (m_data_sp->GetType() == lldb::eStructuredDataTypeArray) {
      auto array = m_data_sp->GetAsArray();
      return (array->GetSize());
    } else
      return 0;
  }

  StructuredData::ObjectSP GetValueForKey(const char *key) const {
    if (m_data_sp) {
      auto dict = m_data_sp->GetAsDictionary();
      if (dict)
        return dict->GetValueForKey(llvm::StringRef(key));
    }
    return StructuredData::ObjectSP();
  }

  StructuredData::ObjectSP GetItemAtIndex(size_t idx) const {
    if (m_data_sp) {
      auto array = m_data_sp->GetAsArray();
      if (array)
        return array->GetItemAtIndex(idx);
    }
    return StructuredData::ObjectSP();
  }

  uint64_t GetIntegerValue(uint64_t fail_value = 0) const {
    return (m_data_sp ? m_data_sp->GetIntegerValue(fail_value) : fail_value);
  }

  double GetFloatValue(double fail_value = 0.0) const {
    return (m_data_sp ? m_data_sp->GetFloatValue(fail_value) : fail_value);
  }

  bool GetBooleanValue(bool fail_value = false) const {
    return (m_data_sp ? m_data_sp->GetBooleanValue(fail_value) : fail_value);
  }

  size_t GetStringValue(char *dst, size_t dst_len) const {
    if (!m_data_sp)
      return 0;

    llvm::StringRef result = m_data_sp->GetStringValue();
    if (result.empty())
      return 0;

    if (!dst || !dst_len) {
      char s[1];
      return (::snprintf(s, 1, "%s", result.data()));
    }
    return (::snprintf(dst, dst_len, "%s", result.data()));
  }

private:
  lldb::StructuredDataPluginWP m_plugin_wp;
  StructuredData::ObjectSP m_data_sp;
};
} // namespace lldb_private
#endif
