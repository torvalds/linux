//===-- OptionValue.cpp -----------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "lldb/Interpreter/OptionValue.h"

#include "lldb/Interpreter/OptionValues.h"
#include "lldb/Utility/StringList.h"

using namespace lldb;
using namespace lldb_private;

//-------------------------------------------------------------------------
// Get this value as a uint64_t value if it is encoded as a boolean, uint64_t
// or int64_t. Other types will cause "fail_value" to be returned
//-------------------------------------------------------------------------
uint64_t OptionValue::GetUInt64Value(uint64_t fail_value, bool *success_ptr) {
  if (success_ptr)
    *success_ptr = true;
  switch (GetType()) {
  case OptionValue::eTypeBoolean:
    return static_cast<OptionValueBoolean *>(this)->GetCurrentValue();
  case OptionValue::eTypeSInt64:
    return static_cast<OptionValueSInt64 *>(this)->GetCurrentValue();
  case OptionValue::eTypeUInt64:
    return static_cast<OptionValueUInt64 *>(this)->GetCurrentValue();
  default:
    break;
  }
  if (success_ptr)
    *success_ptr = false;
  return fail_value;
}

Status OptionValue::SetSubValue(const ExecutionContext *exe_ctx,
                                VarSetOperationType op, llvm::StringRef name,
                                llvm::StringRef value) {
  Status error;
  error.SetErrorStringWithFormat("SetSubValue is not supported");
  return error;
}

OptionValueBoolean *OptionValue::GetAsBoolean() {
  if (GetType() == OptionValue::eTypeBoolean)
    return static_cast<OptionValueBoolean *>(this);
  return nullptr;
}

const OptionValueBoolean *OptionValue::GetAsBoolean() const {
  if (GetType() == OptionValue::eTypeBoolean)
    return static_cast<const OptionValueBoolean *>(this);
  return nullptr;
}

const OptionValueChar *OptionValue::GetAsChar() const {
  if (GetType() == OptionValue::eTypeChar)
    return static_cast<const OptionValueChar *>(this);
  return nullptr;
}

OptionValueChar *OptionValue::GetAsChar() {
  if (GetType() == OptionValue::eTypeChar)
    return static_cast<OptionValueChar *>(this);
  return nullptr;
}

OptionValueFileSpec *OptionValue::GetAsFileSpec() {
  if (GetType() == OptionValue::eTypeFileSpec)
    return static_cast<OptionValueFileSpec *>(this);
  return nullptr;
}

const OptionValueFileSpec *OptionValue::GetAsFileSpec() const {
  if (GetType() == OptionValue::eTypeFileSpec)
    return static_cast<const OptionValueFileSpec *>(this);
  return nullptr;
}

OptionValueFileSpecList *OptionValue::GetAsFileSpecList() {
  if (GetType() == OptionValue::eTypeFileSpecList)
    return static_cast<OptionValueFileSpecList *>(this);
  return nullptr;
}

const OptionValueFileSpecList *OptionValue::GetAsFileSpecList() const {
  if (GetType() == OptionValue::eTypeFileSpecList)
    return static_cast<const OptionValueFileSpecList *>(this);
  return nullptr;
}

OptionValueArch *OptionValue::GetAsArch() {
  if (GetType() == OptionValue::eTypeArch)
    return static_cast<OptionValueArch *>(this);
  return nullptr;
}

const OptionValueArch *OptionValue::GetAsArch() const {
  if (GetType() == OptionValue::eTypeArch)
    return static_cast<const OptionValueArch *>(this);
  return nullptr;
}

OptionValueArray *OptionValue::GetAsArray() {
  if (GetType() == OptionValue::eTypeArray)
    return static_cast<OptionValueArray *>(this);
  return nullptr;
}

const OptionValueArray *OptionValue::GetAsArray() const {
  if (GetType() == OptionValue::eTypeArray)
    return static_cast<const OptionValueArray *>(this);
  return nullptr;
}

OptionValueArgs *OptionValue::GetAsArgs() {
  if (GetType() == OptionValue::eTypeArgs)
    return static_cast<OptionValueArgs *>(this);
  return nullptr;
}

const OptionValueArgs *OptionValue::GetAsArgs() const {
  if (GetType() == OptionValue::eTypeArgs)
    return static_cast<const OptionValueArgs *>(this);
  return nullptr;
}

OptionValueDictionary *OptionValue::GetAsDictionary() {
  if (GetType() == OptionValue::eTypeDictionary)
    return static_cast<OptionValueDictionary *>(this);
  return nullptr;
}

const OptionValueDictionary *OptionValue::GetAsDictionary() const {
  if (GetType() == OptionValue::eTypeDictionary)
    return static_cast<const OptionValueDictionary *>(this);
  return nullptr;
}

OptionValueEnumeration *OptionValue::GetAsEnumeration() {
  if (GetType() == OptionValue::eTypeEnum)
    return static_cast<OptionValueEnumeration *>(this);
  return nullptr;
}

const OptionValueEnumeration *OptionValue::GetAsEnumeration() const {
  if (GetType() == OptionValue::eTypeEnum)
    return static_cast<const OptionValueEnumeration *>(this);
  return nullptr;
}

OptionValueFormat *OptionValue::GetAsFormat() {
  if (GetType() == OptionValue::eTypeFormat)
    return static_cast<OptionValueFormat *>(this);
  return nullptr;
}

const OptionValueFormat *OptionValue::GetAsFormat() const {
  if (GetType() == OptionValue::eTypeFormat)
    return static_cast<const OptionValueFormat *>(this);
  return nullptr;
}

OptionValueLanguage *OptionValue::GetAsLanguage() {
  if (GetType() == OptionValue::eTypeLanguage)
    return static_cast<OptionValueLanguage *>(this);
  return NULL;
}

const OptionValueLanguage *OptionValue::GetAsLanguage() const {
  if (GetType() == OptionValue::eTypeLanguage)
    return static_cast<const OptionValueLanguage *>(this);
  return NULL;
}

OptionValueFormatEntity *OptionValue::GetAsFormatEntity() {
  if (GetType() == OptionValue::eTypeFormatEntity)
    return static_cast<OptionValueFormatEntity *>(this);
  return nullptr;
}

const OptionValueFormatEntity *OptionValue::GetAsFormatEntity() const {
  if (GetType() == OptionValue::eTypeFormatEntity)
    return static_cast<const OptionValueFormatEntity *>(this);
  return nullptr;
}

OptionValuePathMappings *OptionValue::GetAsPathMappings() {
  if (GetType() == OptionValue::eTypePathMap)
    return static_cast<OptionValuePathMappings *>(this);
  return nullptr;
}

const OptionValuePathMappings *OptionValue::GetAsPathMappings() const {
  if (GetType() == OptionValue::eTypePathMap)
    return static_cast<const OptionValuePathMappings *>(this);
  return nullptr;
}

OptionValueProperties *OptionValue::GetAsProperties() {
  if (GetType() == OptionValue::eTypeProperties)
    return static_cast<OptionValueProperties *>(this);
  return nullptr;
}

const OptionValueProperties *OptionValue::GetAsProperties() const {
  if (GetType() == OptionValue::eTypeProperties)
    return static_cast<const OptionValueProperties *>(this);
  return nullptr;
}

OptionValueRegex *OptionValue::GetAsRegex() {
  if (GetType() == OptionValue::eTypeRegex)
    return static_cast<OptionValueRegex *>(this);
  return nullptr;
}

const OptionValueRegex *OptionValue::GetAsRegex() const {
  if (GetType() == OptionValue::eTypeRegex)
    return static_cast<const OptionValueRegex *>(this);
  return nullptr;
}

OptionValueSInt64 *OptionValue::GetAsSInt64() {
  if (GetType() == OptionValue::eTypeSInt64)
    return static_cast<OptionValueSInt64 *>(this);
  return nullptr;
}

const OptionValueSInt64 *OptionValue::GetAsSInt64() const {
  if (GetType() == OptionValue::eTypeSInt64)
    return static_cast<const OptionValueSInt64 *>(this);
  return nullptr;
}

OptionValueString *OptionValue::GetAsString() {
  if (GetType() == OptionValue::eTypeString)
    return static_cast<OptionValueString *>(this);
  return nullptr;
}

const OptionValueString *OptionValue::GetAsString() const {
  if (GetType() == OptionValue::eTypeString)
    return static_cast<const OptionValueString *>(this);
  return nullptr;
}

OptionValueUInt64 *OptionValue::GetAsUInt64() {
  if (GetType() == OptionValue::eTypeUInt64)
    return static_cast<OptionValueUInt64 *>(this);
  return nullptr;
}

const OptionValueUInt64 *OptionValue::GetAsUInt64() const {
  if (GetType() == OptionValue::eTypeUInt64)
    return static_cast<const OptionValueUInt64 *>(this);
  return nullptr;
}

OptionValueUUID *OptionValue::GetAsUUID() {
  if (GetType() == OptionValue::eTypeUUID)
    return static_cast<OptionValueUUID *>(this);
  return nullptr;
}

const OptionValueUUID *OptionValue::GetAsUUID() const {
  if (GetType() == OptionValue::eTypeUUID)
    return static_cast<const OptionValueUUID *>(this);
  return nullptr;
}

bool OptionValue::GetBooleanValue(bool fail_value) const {
  const OptionValueBoolean *option_value = GetAsBoolean();
  if (option_value)
    return option_value->GetCurrentValue();
  return fail_value;
}

bool OptionValue::SetBooleanValue(bool new_value) {
  OptionValueBoolean *option_value = GetAsBoolean();
  if (option_value) {
    option_value->SetCurrentValue(new_value);
    return true;
  }
  return false;
}

char OptionValue::GetCharValue(char fail_value) const {
  const OptionValueChar *option_value = GetAsChar();
  if (option_value)
    return option_value->GetCurrentValue();
  return fail_value;
}

char OptionValue::SetCharValue(char new_value) {
  OptionValueChar *option_value = GetAsChar();
  if (option_value) {
    option_value->SetCurrentValue(new_value);
    return true;
  }
  return false;
}

int64_t OptionValue::GetEnumerationValue(int64_t fail_value) const {
  const OptionValueEnumeration *option_value = GetAsEnumeration();
  if (option_value)
    return option_value->GetCurrentValue();
  return fail_value;
}

bool OptionValue::SetEnumerationValue(int64_t value) {
  OptionValueEnumeration *option_value = GetAsEnumeration();
  if (option_value) {
    option_value->SetCurrentValue(value);
    return true;
  }
  return false;
}

FileSpec OptionValue::GetFileSpecValue() const {
  const OptionValueFileSpec *option_value = GetAsFileSpec();
  if (option_value)
    return option_value->GetCurrentValue();
  return FileSpec();
}

bool OptionValue::SetFileSpecValue(const FileSpec &file_spec) {
  OptionValueFileSpec *option_value = GetAsFileSpec();
  if (option_value) {
    option_value->SetCurrentValue(file_spec, false);
    return true;
  }
  return false;
}

FileSpecList OptionValue::GetFileSpecListValue() const {
  const OptionValueFileSpecList *option_value = GetAsFileSpecList();
  if (option_value)
    return option_value->GetCurrentValue();
  return FileSpecList();
}

lldb::Format OptionValue::GetFormatValue(lldb::Format fail_value) const {
  const OptionValueFormat *option_value = GetAsFormat();
  if (option_value)
    return option_value->GetCurrentValue();
  return fail_value;
}

bool OptionValue::SetFormatValue(lldb::Format new_value) {
  OptionValueFormat *option_value = GetAsFormat();
  if (option_value) {
    option_value->SetCurrentValue(new_value);
    return true;
  }
  return false;
}

lldb::LanguageType
OptionValue::GetLanguageValue(lldb::LanguageType fail_value) const {
  const OptionValueLanguage *option_value = GetAsLanguage();
  if (option_value)
    return option_value->GetCurrentValue();
  return fail_value;
}

bool OptionValue::SetLanguageValue(lldb::LanguageType new_language) {
  OptionValueLanguage *option_value = GetAsLanguage();
  if (option_value) {
    option_value->SetCurrentValue(new_language);
    return true;
  }
  return false;
}

const FormatEntity::Entry *OptionValue::GetFormatEntity() const {
  const OptionValueFormatEntity *option_value = GetAsFormatEntity();
  if (option_value)
    return &option_value->GetCurrentValue();
  return nullptr;
}

const RegularExpression *OptionValue::GetRegexValue() const {
  const OptionValueRegex *option_value = GetAsRegex();
  if (option_value)
    return option_value->GetCurrentValue();
  return nullptr;
}

int64_t OptionValue::GetSInt64Value(int64_t fail_value) const {
  const OptionValueSInt64 *option_value = GetAsSInt64();
  if (option_value)
    return option_value->GetCurrentValue();
  return fail_value;
}

bool OptionValue::SetSInt64Value(int64_t new_value) {
  OptionValueSInt64 *option_value = GetAsSInt64();
  if (option_value) {
    option_value->SetCurrentValue(new_value);
    return true;
  }
  return false;
}

llvm::StringRef OptionValue::GetStringValue(llvm::StringRef fail_value) const {
  const OptionValueString *option_value = GetAsString();
  if (option_value)
    return option_value->GetCurrentValueAsRef();
  return fail_value;
}

bool OptionValue::SetStringValue(llvm::StringRef new_value) {
  OptionValueString *option_value = GetAsString();
  if (option_value) {
    option_value->SetCurrentValue(new_value);
    return true;
  }
  return false;
}

uint64_t OptionValue::GetUInt64Value(uint64_t fail_value) const {
  const OptionValueUInt64 *option_value = GetAsUInt64();
  if (option_value)
    return option_value->GetCurrentValue();
  return fail_value;
}

bool OptionValue::SetUInt64Value(uint64_t new_value) {
  OptionValueUInt64 *option_value = GetAsUInt64();
  if (option_value) {
    option_value->SetCurrentValue(new_value);
    return true;
  }
  return false;
}

UUID OptionValue::GetUUIDValue() const {
  const OptionValueUUID *option_value = GetAsUUID();
  if (option_value)
    return option_value->GetCurrentValue();
  return UUID();
}

bool OptionValue::SetUUIDValue(const UUID &uuid) {
  OptionValueUUID *option_value = GetAsUUID();
  if (option_value) {
    option_value->SetCurrentValue(uuid);
    return true;
  }
  return false;
}

const char *OptionValue::GetBuiltinTypeAsCString(Type t) {
  switch (t) {
  case eTypeInvalid:
    return "invalid";
  case eTypeArch:
    return "arch";
  case eTypeArgs:
    return "arguments";
  case eTypeArray:
    return "array";
  case eTypeBoolean:
    return "boolean";
  case eTypeChar:
    return "char";
  case eTypeDictionary:
    return "dictionary";
  case eTypeEnum:
    return "enum";
  case eTypeFileSpec:
    return "file";
  case eTypeFileSpecList:
    return "file-list";
  case eTypeFormat:
    return "format";
  case eTypeFormatEntity:
    return "format-string";
  case eTypeLanguage:
    return "language";
  case eTypePathMap:
    return "path-map";
  case eTypeProperties:
    return "properties";
  case eTypeRegex:
    return "regex";
  case eTypeSInt64:
    return "int";
  case eTypeString:
    return "string";
  case eTypeUInt64:
    return "unsigned";
  case eTypeUUID:
    return "uuid";
  }
  return nullptr;
}

lldb::OptionValueSP OptionValue::CreateValueFromCStringForTypeMask(
    const char *value_cstr, uint32_t type_mask, Status &error) {
  // If only 1 bit is set in the type mask for a dictionary or array then we
  // know how to decode a value from a cstring
  lldb::OptionValueSP value_sp;
  switch (type_mask) {
  case 1u << eTypeArch:
    value_sp.reset(new OptionValueArch());
    break;
  case 1u << eTypeBoolean:
    value_sp.reset(new OptionValueBoolean(false));
    break;
  case 1u << eTypeChar:
    value_sp.reset(new OptionValueChar('\0'));
    break;
  case 1u << eTypeFileSpec:
    value_sp.reset(new OptionValueFileSpec());
    break;
  case 1u << eTypeFormat:
    value_sp.reset(new OptionValueFormat(eFormatInvalid));
    break;
  case 1u << eTypeFormatEntity:
    value_sp.reset(new OptionValueFormatEntity(NULL));
    break;
  case 1u << eTypeLanguage:
    value_sp.reset(new OptionValueLanguage(eLanguageTypeUnknown));
    break;
  case 1u << eTypeSInt64:
    value_sp.reset(new OptionValueSInt64());
    break;
  case 1u << eTypeString:
    value_sp.reset(new OptionValueString());
    break;
  case 1u << eTypeUInt64:
    value_sp.reset(new OptionValueUInt64());
    break;
  case 1u << eTypeUUID:
    value_sp.reset(new OptionValueUUID());
    break;
  }

  if (value_sp)
    error = value_sp->SetValueFromString(
        llvm::StringRef::withNullAsEmpty(value_cstr), eVarSetOperationAssign);
  else
    error.SetErrorString("unsupported type mask");
  return value_sp;
}

bool OptionValue::DumpQualifiedName(Stream &strm) const {
  bool dumped_something = false;
  lldb::OptionValueSP m_parent_sp(m_parent_wp.lock());
  if (m_parent_sp) {
    if (m_parent_sp->DumpQualifiedName(strm))
      dumped_something = true;
  }
  ConstString name(GetName());
  if (name) {
    if (dumped_something)
      strm.PutChar('.');
    else
      dumped_something = true;
    strm << name;
  }
  return dumped_something;
}

size_t OptionValue::AutoComplete(CommandInterpreter &interpreter,
                                 CompletionRequest &request) {
  request.SetWordComplete(false);
  return request.GetNumberOfMatches();
}

Status OptionValue::SetValueFromString(llvm::StringRef value,
                                       VarSetOperationType op) {
  Status error;
  switch (op) {
  case eVarSetOperationReplace:
    error.SetErrorStringWithFormat(
        "%s objects do not support the 'replace' operation",
        GetTypeAsCString());
    break;
  case eVarSetOperationInsertBefore:
    error.SetErrorStringWithFormat(
        "%s objects do not support the 'insert-before' operation",
        GetTypeAsCString());
    break;
  case eVarSetOperationInsertAfter:
    error.SetErrorStringWithFormat(
        "%s objects do not support the 'insert-after' operation",
        GetTypeAsCString());
    break;
  case eVarSetOperationRemove:
    error.SetErrorStringWithFormat(
        "%s objects do not support the 'remove' operation", GetTypeAsCString());
    break;
  case eVarSetOperationAppend:
    error.SetErrorStringWithFormat(
        "%s objects do not support the 'append' operation", GetTypeAsCString());
    break;
  case eVarSetOperationClear:
    error.SetErrorStringWithFormat(
        "%s objects do not support the 'clear' operation", GetTypeAsCString());
    break;
  case eVarSetOperationAssign:
    error.SetErrorStringWithFormat(
        "%s objects do not support the 'assign' operation", GetTypeAsCString());
    break;
  case eVarSetOperationInvalid:
    error.SetErrorStringWithFormat("invalid operation performed on a %s object",
                                   GetTypeAsCString());
    break;
  }
  return error;
}
