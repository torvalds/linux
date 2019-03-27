//===-- OptionValue.h -------------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef liblldb_OptionValue_h_
#define liblldb_OptionValue_h_

#include "lldb/Core/FormatEntity.h"
#include "lldb/Utility/CompletionRequest.h"
#include "lldb/Utility/ConstString.h"
#include "lldb/Utility/Status.h"
#include "lldb/lldb-defines.h"
#include "lldb/lldb-private-enumerations.h"
#include "lldb/lldb-private-interfaces.h"

namespace lldb_private {

//---------------------------------------------------------------------
// OptionValue
//---------------------------------------------------------------------
class OptionValue {
public:
  typedef enum {
    eTypeInvalid = 0,
    eTypeArch,
    eTypeArgs,
    eTypeArray,
    eTypeBoolean,
    eTypeChar,
    eTypeDictionary,
    eTypeEnum,
    eTypeFileSpec,
    eTypeFileSpecList,
    eTypeFormat,
    eTypeLanguage,
    eTypePathMap,
    eTypeProperties,
    eTypeRegex,
    eTypeSInt64,
    eTypeString,
    eTypeUInt64,
    eTypeUUID,
    eTypeFormatEntity
  } Type;

  enum {
    eDumpOptionName = (1u << 0),
    eDumpOptionType = (1u << 1),
    eDumpOptionValue = (1u << 2),
    eDumpOptionDescription = (1u << 3),
    eDumpOptionRaw = (1u << 4),
    eDumpOptionCommand = (1u << 5),
    eDumpGroupValue = (eDumpOptionName | eDumpOptionType | eDumpOptionValue),
    eDumpGroupHelp =
        (eDumpOptionName | eDumpOptionType | eDumpOptionDescription),
    eDumpGroupExport = (eDumpOptionCommand | eDumpOptionName | eDumpOptionValue)
  };

  OptionValue()
      : m_callback(nullptr), m_baton(nullptr), m_value_was_set(false) {}

  OptionValue(const OptionValue &rhs)
      : m_callback(rhs.m_callback), m_baton(rhs.m_baton),
        m_value_was_set(rhs.m_value_was_set) {}

  virtual ~OptionValue() = default;

  //-----------------------------------------------------------------
  // Subclasses should override these functions
  //-----------------------------------------------------------------
  virtual Type GetType() const = 0;

  // If this value is always hidden, the avoid showing any info on this value,
  // just show the info for the child values.
  virtual bool ValueIsTransparent() const {
    return GetType() == eTypeProperties;
  }

  virtual const char *GetTypeAsCString() const {
    return GetBuiltinTypeAsCString(GetType());
  }

  static const char *GetBuiltinTypeAsCString(Type t);

  virtual void DumpValue(const ExecutionContext *exe_ctx, Stream &strm,
                         uint32_t dump_mask) = 0;

  virtual Status
  SetValueFromString(llvm::StringRef value,
                     VarSetOperationType op = eVarSetOperationAssign);

  virtual bool Clear() = 0;

  virtual lldb::OptionValueSP DeepCopy() const = 0;

  virtual size_t AutoComplete(CommandInterpreter &interpreter,
                              CompletionRequest &request);

  //-----------------------------------------------------------------
  // Subclasses can override these functions
  //-----------------------------------------------------------------
  virtual lldb::OptionValueSP GetSubValue(const ExecutionContext *exe_ctx,
                                          llvm::StringRef name,
                                          bool will_modify,
                                          Status &error) const {
    error.SetErrorStringWithFormat("'%s' is not a value subvalue", name.str().c_str());
    return lldb::OptionValueSP();
  }

  virtual Status SetSubValue(const ExecutionContext *exe_ctx,
                             VarSetOperationType op, llvm::StringRef name,
                             llvm::StringRef value);

  virtual bool IsAggregateValue() const { return false; }

  virtual ConstString GetName() const { return ConstString(); }

  virtual bool DumpQualifiedName(Stream &strm) const;

  //-----------------------------------------------------------------
  // Subclasses should NOT override these functions as they use the above
  // functions to implement functionality
  //-----------------------------------------------------------------
  uint32_t GetTypeAsMask() { return 1u << GetType(); }

  static uint32_t ConvertTypeToMask(OptionValue::Type type) {
    return 1u << type;
  }

  static OptionValue::Type ConvertTypeMaskToType(uint32_t type_mask) {
    // If only one bit is set, then return an appropriate enumeration
    switch (type_mask) {
    case 1u << eTypeArch:
      return eTypeArch;
    case 1u << eTypeArgs:
      return eTypeArgs;
    case 1u << eTypeArray:
      return eTypeArray;
    case 1u << eTypeBoolean:
      return eTypeBoolean;
    case 1u << eTypeChar:
      return eTypeChar;
    case 1u << eTypeDictionary:
      return eTypeDictionary;
    case 1u << eTypeEnum:
      return eTypeEnum;
    case 1u << eTypeFileSpec:
      return eTypeFileSpec;
    case 1u << eTypeFileSpecList:
      return eTypeFileSpecList;
    case 1u << eTypeFormat:
      return eTypeFormat;
    case 1u << eTypeLanguage:
      return eTypeLanguage;
    case 1u << eTypePathMap:
      return eTypePathMap;
    case 1u << eTypeProperties:
      return eTypeProperties;
    case 1u << eTypeRegex:
      return eTypeRegex;
    case 1u << eTypeSInt64:
      return eTypeSInt64;
    case 1u << eTypeString:
      return eTypeString;
    case 1u << eTypeUInt64:
      return eTypeUInt64;
    case 1u << eTypeUUID:
      return eTypeUUID;
    }
    // Else return invalid
    return eTypeInvalid;
  }

  static lldb::OptionValueSP
  CreateValueFromCStringForTypeMask(const char *value_cstr, uint32_t type_mask,
                                    Status &error);

  // Get this value as a uint64_t value if it is encoded as a boolean, uint64_t
  // or int64_t. Other types will cause "fail_value" to be returned
  uint64_t GetUInt64Value(uint64_t fail_value, bool *success_ptr);

  OptionValueArch *GetAsArch();

  const OptionValueArch *GetAsArch() const;

  OptionValueArray *GetAsArray();

  const OptionValueArray *GetAsArray() const;

  OptionValueArgs *GetAsArgs();

  const OptionValueArgs *GetAsArgs() const;

  OptionValueBoolean *GetAsBoolean();

  OptionValueChar *GetAsChar();

  const OptionValueBoolean *GetAsBoolean() const;

  const OptionValueChar *GetAsChar() const;

  OptionValueDictionary *GetAsDictionary();

  const OptionValueDictionary *GetAsDictionary() const;

  OptionValueEnumeration *GetAsEnumeration();

  const OptionValueEnumeration *GetAsEnumeration() const;

  OptionValueFileSpec *GetAsFileSpec();

  const OptionValueFileSpec *GetAsFileSpec() const;

  OptionValueFileSpecList *GetAsFileSpecList();

  const OptionValueFileSpecList *GetAsFileSpecList() const;

  OptionValueFormat *GetAsFormat();

  const OptionValueFormat *GetAsFormat() const;

  OptionValueLanguage *GetAsLanguage();

  const OptionValueLanguage *GetAsLanguage() const;

  OptionValuePathMappings *GetAsPathMappings();

  const OptionValuePathMappings *GetAsPathMappings() const;

  OptionValueProperties *GetAsProperties();

  const OptionValueProperties *GetAsProperties() const;

  OptionValueRegex *GetAsRegex();

  const OptionValueRegex *GetAsRegex() const;

  OptionValueSInt64 *GetAsSInt64();

  const OptionValueSInt64 *GetAsSInt64() const;

  OptionValueString *GetAsString();

  const OptionValueString *GetAsString() const;

  OptionValueUInt64 *GetAsUInt64();

  const OptionValueUInt64 *GetAsUInt64() const;

  OptionValueUUID *GetAsUUID();

  const OptionValueUUID *GetAsUUID() const;

  OptionValueFormatEntity *GetAsFormatEntity();

  const OptionValueFormatEntity *GetAsFormatEntity() const;

  bool GetBooleanValue(bool fail_value = false) const;

  bool SetBooleanValue(bool new_value);

  char GetCharValue(char fail_value) const;

  char SetCharValue(char new_value);

  int64_t GetEnumerationValue(int64_t fail_value = -1) const;

  bool SetEnumerationValue(int64_t value);

  FileSpec GetFileSpecValue() const;

  bool SetFileSpecValue(const FileSpec &file_spec);

  FileSpecList GetFileSpecListValue() const;

  lldb::Format
  GetFormatValue(lldb::Format fail_value = lldb::eFormatDefault) const;

  bool SetFormatValue(lldb::Format new_value);

  lldb::LanguageType GetLanguageValue(
      lldb::LanguageType fail_value = lldb::eLanguageTypeUnknown) const;

  bool SetLanguageValue(lldb::LanguageType new_language);

  const FormatEntity::Entry *GetFormatEntity() const;

  const RegularExpression *GetRegexValue() const;

  int64_t GetSInt64Value(int64_t fail_value = 0) const;

  bool SetSInt64Value(int64_t new_value);

  llvm::StringRef GetStringValue(llvm::StringRef fail_value) const;
  llvm::StringRef GetStringValue() const { return GetStringValue(llvm::StringRef()); }

  bool SetStringValue(llvm::StringRef new_value);

  uint64_t GetUInt64Value(uint64_t fail_value = 0) const;

  bool SetUInt64Value(uint64_t new_value);

  UUID GetUUIDValue() const;

  bool SetUUIDValue(const UUID &uuid);

  bool OptionWasSet() const { return m_value_was_set; }

  void SetOptionWasSet() { m_value_was_set = true; }

  void SetParent(const lldb::OptionValueSP &parent_sp) {
    m_parent_wp = parent_sp;
  }

  void SetValueChangedCallback(OptionValueChangedCallback callback,
                               void *baton) {
    assert(m_callback == nullptr);
    m_callback = callback;
    m_baton = baton;
  }

  void NotifyValueChanged() {
    if (m_callback)
      m_callback(m_baton, this);
  }

protected:
  lldb::OptionValueWP m_parent_wp;
  OptionValueChangedCallback m_callback;
  void *m_baton;
  bool m_value_was_set; // This can be used to see if a value has been set
                        // by a call to SetValueFromCString(). It is often
                        // handy to know if an option value was set from the
                        // command line or as a setting, versus if we just have
                        // the default value that was already populated in the
                        // option value.
};

} // namespace lldb_private

#endif // liblldb_OptionValue_h_
