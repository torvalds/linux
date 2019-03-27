//===-- OptionValueProperties.cpp --------------------------------*- C++-*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "lldb/Interpreter/OptionValueProperties.h"

#include "lldb/Utility/Flags.h"

#include "lldb/Core/UserSettingsController.h"
#include "lldb/Interpreter/OptionValues.h"
#include "lldb/Interpreter/Property.h"
#include "lldb/Utility/Args.h"
#include "lldb/Utility/Stream.h"
#include "lldb/Utility/StringList.h"

using namespace lldb;
using namespace lldb_private;

OptionValueProperties::OptionValueProperties(const ConstString &name)
    : OptionValue(), m_name(name), m_properties(), m_name_to_index() {}

OptionValueProperties::OptionValueProperties(
    const OptionValueProperties &global_properties)
    : OptionValue(global_properties),
      std::enable_shared_from_this<OptionValueProperties>(),
      m_name(global_properties.m_name),
      m_properties(global_properties.m_properties),
      m_name_to_index(global_properties.m_name_to_index) {
  // We now have an exact copy of "global_properties". We need to now find all
  // non-global settings and copy the property values so that all non-global
  // settings get new OptionValue instances created for them.
  const size_t num_properties = m_properties.size();
  for (size_t i = 0; i < num_properties; ++i) {
    // Duplicate any values that are not global when constructing properties
    // from a global copy
    if (!m_properties[i].IsGlobal()) {
      lldb::OptionValueSP new_value_sp(m_properties[i].GetValue()->DeepCopy());
      m_properties[i].SetOptionValue(new_value_sp);
    }
  }
}

size_t OptionValueProperties::GetNumProperties() const {
  return m_properties.size();
}

void OptionValueProperties::Initialize(const PropertyDefinitions &defs) {
  for (const auto &definition : defs) {
    Property property(definition);
    assert(property.IsValid());
    m_name_to_index.Append(ConstString(property.GetName()), m_properties.size());
    property.GetValue()->SetParent(shared_from_this());
    m_properties.push_back(property);
  }
  m_name_to_index.Sort();
}

void OptionValueProperties::SetValueChangedCallback(
    uint32_t property_idx, OptionValueChangedCallback callback, void *baton) {
  Property *property = ProtectedGetPropertyAtIndex(property_idx);
  if (property)
    property->SetValueChangedCallback(callback, baton);
}

void OptionValueProperties::AppendProperty(const ConstString &name,
                                           const ConstString &desc,
                                           bool is_global,
                                           const OptionValueSP &value_sp) {
  Property property(name, desc, is_global, value_sp);
  m_name_to_index.Append(name, m_properties.size());
  m_properties.push_back(property);
  value_sp->SetParent(shared_from_this());
  m_name_to_index.Sort();
}

// bool
// OptionValueProperties::GetQualifiedName (Stream &strm)
//{
//    bool dumped_something = false;
////    lldb::OptionValuePropertiesSP parent_sp(GetParent ());
////    if (parent_sp)
////    {
////        parent_sp->GetQualifiedName (strm);
////        strm.PutChar('.');
////        dumped_something = true;
////    }
//    if (m_name)
//    {
//        strm << m_name;
//        dumped_something = true;
//    }
//    return dumped_something;
//}
//
lldb::OptionValueSP
OptionValueProperties::GetValueForKey(const ExecutionContext *exe_ctx,
                                      const ConstString &key,
                                      bool will_modify) const {
  lldb::OptionValueSP value_sp;
  size_t idx = m_name_to_index.Find(key, SIZE_MAX);
  if (idx < m_properties.size())
    value_sp = GetPropertyAtIndex(exe_ctx, will_modify, idx)->GetValue();
  return value_sp;
}

lldb::OptionValueSP
OptionValueProperties::GetSubValue(const ExecutionContext *exe_ctx,
                                   llvm::StringRef name, bool will_modify,
                                   Status &error) const {
  lldb::OptionValueSP value_sp;
  if (name.empty())
    return OptionValueSP();

  llvm::StringRef sub_name;
  ConstString key;
  size_t key_len = name.find_first_of(".[{");
  if (key_len != llvm::StringRef::npos) {
    key.SetString(name.take_front(key_len));
    sub_name = name.drop_front(key_len);
  } else
    key.SetString(name);

  value_sp = GetValueForKey(exe_ctx, key, will_modify);
  if (sub_name.empty() || !value_sp)
    return value_sp;

  switch (sub_name[0]) {
  case '.': {
    lldb::OptionValueSP return_val_sp;
    return_val_sp =
        value_sp->GetSubValue(exe_ctx, sub_name.drop_front(), will_modify, error);
    if (!return_val_sp) {
      if (Properties::IsSettingExperimental(sub_name.drop_front())) {
        size_t experimental_len =
            strlen(Properties::GetExperimentalSettingsName());
        if (sub_name[experimental_len + 1] == '.')
          return_val_sp = value_sp->GetSubValue(
              exe_ctx, sub_name.drop_front(experimental_len + 2), will_modify, error);
        // It isn't an error if an experimental setting is not present.
        if (!return_val_sp)
          error.Clear();
      }
    }
    return return_val_sp;
  }
  case '{':
    // Predicate matching for predicates like
    // "<setting-name>{<predicate>}"
    // strings are parsed by the current OptionValueProperties subclass to mean
    // whatever they want to. For instance a subclass of OptionValueProperties
    // for a lldb_private::Target might implement: "target.run-
    // args{arch==i386}"   -- only set run args if the arch is i386 "target
    // .run-args{path=/tmp/a/b/c/a.out}" -- only set run args if the path
    // matches "target.run-args{basename==test&&arch==x86_64}" -- only set run
    // args if executable basename is "test" and arch is "x86_64"
    if (sub_name[1]) {
      llvm::StringRef predicate_start = sub_name.drop_front();
      size_t pos = predicate_start.find_first_of('}');
      if (pos != llvm::StringRef::npos) {
        auto predicate = predicate_start.take_front(pos);
        auto rest = predicate_start.drop_front(pos);
        if (PredicateMatches(exe_ctx, predicate)) {
          if (!rest.empty()) {
            // Still more subvalue string to evaluate
            return value_sp->GetSubValue(exe_ctx, rest,
                                          will_modify, error);
          } else {
            // We have a match!
            break;
          }
        }
      }
    }
    // Predicate didn't match or wasn't correctly formed
    value_sp.reset();
    break;

  case '[':
    // Array or dictionary access for subvalues like: "[12]"       -- access
    // 12th array element "['hello']"  -- dictionary access of key named hello
    return value_sp->GetSubValue(exe_ctx, sub_name, will_modify, error);

  default:
    value_sp.reset();
    break;
  }
  return value_sp;
}

Status OptionValueProperties::SetSubValue(const ExecutionContext *exe_ctx,
                                          VarSetOperationType op,
                                          llvm::StringRef name,
                                          llvm::StringRef value) {
  Status error;
  const bool will_modify = true;
  llvm::SmallVector<llvm::StringRef, 8> components;
  name.split(components, '.');
  bool name_contains_experimental = false;
  for (const auto &part : components)
    if (Properties::IsSettingExperimental(part))
      name_contains_experimental = true;

  
  lldb::OptionValueSP value_sp(GetSubValue(exe_ctx, name, will_modify, error));
  if (value_sp)
    error = value_sp->SetValueFromString(value, op);
  else {
    // Don't set an error if the path contained .experimental. - those are
    // allowed to be missing and should silently fail.
    if (!name_contains_experimental && error.AsCString() == nullptr) {
      error.SetErrorStringWithFormat("invalid value path '%s'", name.str().c_str());
    }
  }
  return error;
}

uint32_t
OptionValueProperties::GetPropertyIndex(const ConstString &name) const {
  return m_name_to_index.Find(name, SIZE_MAX);
}

const Property *
OptionValueProperties::GetProperty(const ExecutionContext *exe_ctx,
                                   bool will_modify,
                                   const ConstString &name) const {
  return GetPropertyAtIndex(
      exe_ctx, will_modify,
      m_name_to_index.Find(name, SIZE_MAX));
}

const Property *OptionValueProperties::GetPropertyAtIndex(
    const ExecutionContext *exe_ctx, bool will_modify, uint32_t idx) const {
  return ProtectedGetPropertyAtIndex(idx);
}

lldb::OptionValueSP OptionValueProperties::GetPropertyValueAtIndex(
    const ExecutionContext *exe_ctx, bool will_modify, uint32_t idx) const {
  const Property *setting = GetPropertyAtIndex(exe_ctx, will_modify, idx);
  if (setting)
    return setting->GetValue();
  return OptionValueSP();
}

OptionValuePathMappings *
OptionValueProperties::GetPropertyAtIndexAsOptionValuePathMappings(
    const ExecutionContext *exe_ctx, bool will_modify, uint32_t idx) const {
  OptionValueSP value_sp(GetPropertyValueAtIndex(exe_ctx, will_modify, idx));
  if (value_sp)
    return value_sp->GetAsPathMappings();
  return nullptr;
}

OptionValueFileSpecList *
OptionValueProperties::GetPropertyAtIndexAsOptionValueFileSpecList(
    const ExecutionContext *exe_ctx, bool will_modify, uint32_t idx) const {
  OptionValueSP value_sp(GetPropertyValueAtIndex(exe_ctx, will_modify, idx));
  if (value_sp)
    return value_sp->GetAsFileSpecList();
  return nullptr;
}

OptionValueArch *OptionValueProperties::GetPropertyAtIndexAsOptionValueArch(
    const ExecutionContext *exe_ctx, uint32_t idx) const {
  const Property *property = GetPropertyAtIndex(exe_ctx, false, idx);
  if (property)
    return property->GetValue()->GetAsArch();
  return nullptr;
}

OptionValueLanguage *
OptionValueProperties::GetPropertyAtIndexAsOptionValueLanguage(
    const ExecutionContext *exe_ctx, uint32_t idx) const {
  const Property *property = GetPropertyAtIndex(exe_ctx, false, idx);
  if (property)
    return property->GetValue()->GetAsLanguage();
  return nullptr;
}

bool OptionValueProperties::GetPropertyAtIndexAsArgs(
    const ExecutionContext *exe_ctx, uint32_t idx, Args &args) const {
  const Property *property = GetPropertyAtIndex(exe_ctx, false, idx);
  if (property) {
    OptionValue *value = property->GetValue().get();
    if (value) {
      const OptionValueArray *array = value->GetAsArray();
      if (array)
        return array->GetArgs(args);
      else {
        const OptionValueDictionary *dict = value->GetAsDictionary();
        if (dict)
          return dict->GetArgs(args);
      }
    }
  }
  return false;
}

bool OptionValueProperties::SetPropertyAtIndexFromArgs(
    const ExecutionContext *exe_ctx, uint32_t idx, const Args &args) {
  const Property *property = GetPropertyAtIndex(exe_ctx, true, idx);
  if (property) {
    OptionValue *value = property->GetValue().get();
    if (value) {
      OptionValueArray *array = value->GetAsArray();
      if (array)
        return array->SetArgs(args, eVarSetOperationAssign).Success();
      else {
        OptionValueDictionary *dict = value->GetAsDictionary();
        if (dict)
          return dict->SetArgs(args, eVarSetOperationAssign).Success();
      }
    }
  }
  return false;
}

bool OptionValueProperties::GetPropertyAtIndexAsBoolean(
    const ExecutionContext *exe_ctx, uint32_t idx, bool fail_value) const {
  const Property *property = GetPropertyAtIndex(exe_ctx, false, idx);
  if (property) {
    OptionValue *value = property->GetValue().get();
    if (value)
      return value->GetBooleanValue(fail_value);
  }
  return fail_value;
}

bool OptionValueProperties::SetPropertyAtIndexAsBoolean(
    const ExecutionContext *exe_ctx, uint32_t idx, bool new_value) {
  const Property *property = GetPropertyAtIndex(exe_ctx, true, idx);
  if (property) {
    OptionValue *value = property->GetValue().get();
    if (value) {
      value->SetBooleanValue(new_value);
      return true;
    }
  }
  return false;
}

OptionValueDictionary *
OptionValueProperties::GetPropertyAtIndexAsOptionValueDictionary(
    const ExecutionContext *exe_ctx, uint32_t idx) const {
  const Property *property = GetPropertyAtIndex(exe_ctx, false, idx);
  if (property)
    return property->GetValue()->GetAsDictionary();
  return nullptr;
}

int64_t OptionValueProperties::GetPropertyAtIndexAsEnumeration(
    const ExecutionContext *exe_ctx, uint32_t idx, int64_t fail_value) const {
  const Property *property = GetPropertyAtIndex(exe_ctx, false, idx);
  if (property) {
    OptionValue *value = property->GetValue().get();
    if (value)
      return value->GetEnumerationValue(fail_value);
  }
  return fail_value;
}

bool OptionValueProperties::SetPropertyAtIndexAsEnumeration(
    const ExecutionContext *exe_ctx, uint32_t idx, int64_t new_value) {
  const Property *property = GetPropertyAtIndex(exe_ctx, true, idx);
  if (property) {
    OptionValue *value = property->GetValue().get();
    if (value)
      return value->SetEnumerationValue(new_value);
  }
  return false;
}

const FormatEntity::Entry *
OptionValueProperties::GetPropertyAtIndexAsFormatEntity(
    const ExecutionContext *exe_ctx, uint32_t idx) {
  const Property *property = GetPropertyAtIndex(exe_ctx, true, idx);
  if (property) {
    OptionValue *value = property->GetValue().get();
    if (value)
      return value->GetFormatEntity();
  }
  return nullptr;
}

OptionValueFileSpec *
OptionValueProperties::GetPropertyAtIndexAsOptionValueFileSpec(
    const ExecutionContext *exe_ctx, bool will_modify, uint32_t idx) const {
  const Property *property = GetPropertyAtIndex(exe_ctx, false, idx);
  if (property) {
    OptionValue *value = property->GetValue().get();
    if (value)
      return value->GetAsFileSpec();
  }
  return nullptr;
}

FileSpec OptionValueProperties::GetPropertyAtIndexAsFileSpec(
    const ExecutionContext *exe_ctx, uint32_t idx) const {
  const Property *property = GetPropertyAtIndex(exe_ctx, false, idx);
  if (property) {
    OptionValue *value = property->GetValue().get();
    if (value)
      return value->GetFileSpecValue();
  }
  return FileSpec();
}

bool OptionValueProperties::SetPropertyAtIndexAsFileSpec(
    const ExecutionContext *exe_ctx, uint32_t idx,
    const FileSpec &new_file_spec) {
  const Property *property = GetPropertyAtIndex(exe_ctx, true, idx);
  if (property) {
    OptionValue *value = property->GetValue().get();
    if (value)
      return value->SetFileSpecValue(new_file_spec);
  }
  return false;
}

const RegularExpression *
OptionValueProperties::GetPropertyAtIndexAsOptionValueRegex(
    const ExecutionContext *exe_ctx, uint32_t idx) const {
  const Property *property = GetPropertyAtIndex(exe_ctx, false, idx);
  if (property) {
    OptionValue *value = property->GetValue().get();
    if (value)
      return value->GetRegexValue();
  }
  return nullptr;
}

OptionValueSInt64 *OptionValueProperties::GetPropertyAtIndexAsOptionValueSInt64(
    const ExecutionContext *exe_ctx, uint32_t idx) const {
  const Property *property = GetPropertyAtIndex(exe_ctx, false, idx);
  if (property) {
    OptionValue *value = property->GetValue().get();
    if (value)
      return value->GetAsSInt64();
  }
  return nullptr;
}

int64_t OptionValueProperties::GetPropertyAtIndexAsSInt64(
    const ExecutionContext *exe_ctx, uint32_t idx, int64_t fail_value) const {
  const Property *property = GetPropertyAtIndex(exe_ctx, false, idx);
  if (property) {
    OptionValue *value = property->GetValue().get();
    if (value)
      return value->GetSInt64Value(fail_value);
  }
  return fail_value;
}

bool OptionValueProperties::SetPropertyAtIndexAsSInt64(
    const ExecutionContext *exe_ctx, uint32_t idx, int64_t new_value) {
  const Property *property = GetPropertyAtIndex(exe_ctx, true, idx);
  if (property) {
    OptionValue *value = property->GetValue().get();
    if (value)
      return value->SetSInt64Value(new_value);
  }
  return false;
}

llvm::StringRef OptionValueProperties::GetPropertyAtIndexAsString(
    const ExecutionContext *exe_ctx, uint32_t idx,
    llvm::StringRef fail_value) const {
  const Property *property = GetPropertyAtIndex(exe_ctx, false, idx);
  if (property) {
    OptionValue *value = property->GetValue().get();
    if (value)
      return value->GetStringValue(fail_value);
  }
  return fail_value;
}

bool OptionValueProperties::SetPropertyAtIndexAsString(
    const ExecutionContext *exe_ctx, uint32_t idx, llvm::StringRef new_value) {
  const Property *property = GetPropertyAtIndex(exe_ctx, true, idx);
  if (property) {
    OptionValue *value = property->GetValue().get();
    if (value)
      return value->SetStringValue(new_value);
  }
  return false;
}

OptionValueString *OptionValueProperties::GetPropertyAtIndexAsOptionValueString(
    const ExecutionContext *exe_ctx, bool will_modify, uint32_t idx) const {
  OptionValueSP value_sp(GetPropertyValueAtIndex(exe_ctx, will_modify, idx));
  if (value_sp)
    return value_sp->GetAsString();
  return nullptr;
}

uint64_t OptionValueProperties::GetPropertyAtIndexAsUInt64(
    const ExecutionContext *exe_ctx, uint32_t idx, uint64_t fail_value) const {
  const Property *property = GetPropertyAtIndex(exe_ctx, false, idx);
  if (property) {
    OptionValue *value = property->GetValue().get();
    if (value)
      return value->GetUInt64Value(fail_value);
  }
  return fail_value;
}

bool OptionValueProperties::SetPropertyAtIndexAsUInt64(
    const ExecutionContext *exe_ctx, uint32_t idx, uint64_t new_value) {
  const Property *property = GetPropertyAtIndex(exe_ctx, true, idx);
  if (property) {
    OptionValue *value = property->GetValue().get();
    if (value)
      return value->SetUInt64Value(new_value);
  }
  return false;
}

bool OptionValueProperties::Clear() {
  const size_t num_properties = m_properties.size();
  for (size_t i = 0; i < num_properties; ++i)
    m_properties[i].GetValue()->Clear();
  return true;
}

Status OptionValueProperties::SetValueFromString(llvm::StringRef value,
                                                 VarSetOperationType op) {
  Status error;

  //    Args args(value_cstr);
  //    const size_t argc = args.GetArgumentCount();
  switch (op) {
  case eVarSetOperationClear:
    Clear();
    break;

  case eVarSetOperationReplace:
  case eVarSetOperationAssign:
  case eVarSetOperationRemove:
  case eVarSetOperationInsertBefore:
  case eVarSetOperationInsertAfter:
  case eVarSetOperationAppend:
  case eVarSetOperationInvalid:
    error = OptionValue::SetValueFromString(value, op);
    break;
  }

  return error;
}

void OptionValueProperties::DumpValue(const ExecutionContext *exe_ctx,
                                      Stream &strm, uint32_t dump_mask) {
  const size_t num_properties = m_properties.size();
  for (size_t i = 0; i < num_properties; ++i) {
    const Property *property = GetPropertyAtIndex(exe_ctx, false, i);
    if (property) {
      OptionValue *option_value = property->GetValue().get();
      assert(option_value);
      const bool transparent_value = option_value->ValueIsTransparent();
      property->Dump(exe_ctx, strm, dump_mask);
      if (!transparent_value)
        strm.EOL();
    }
  }
}

Status OptionValueProperties::DumpPropertyValue(const ExecutionContext *exe_ctx,
                                                Stream &strm,
                                                llvm::StringRef property_path,
                                                uint32_t dump_mask) {
  Status error;
  const bool will_modify = false;
  lldb::OptionValueSP value_sp(
      GetSubValue(exe_ctx, property_path, will_modify, error));
  if (value_sp) {
    if (!value_sp->ValueIsTransparent()) {
      if (dump_mask & eDumpOptionName)
        strm.PutCString(property_path);
      if (dump_mask & ~eDumpOptionName)
        strm.PutChar(' ');
    }
    value_sp->DumpValue(exe_ctx, strm, dump_mask);
  }
  return error;
}

lldb::OptionValueSP OptionValueProperties::DeepCopy() const {
  llvm_unreachable("this shouldn't happen");
}

const Property *OptionValueProperties::GetPropertyAtPath(
    const ExecutionContext *exe_ctx, bool will_modify, llvm::StringRef name) const {
  const Property *property = nullptr;
  if (name.empty())
    return nullptr;
  llvm::StringRef sub_name;
  ConstString key;
  size_t key_len = name.find_first_of(".[{");

  if (key_len != llvm::StringRef::npos) {
    key.SetString(name.take_front(key_len));
    sub_name = name.drop_front(key_len);
  } else
    key.SetString(name);

  property = GetProperty(exe_ctx, will_modify, key);
  if (sub_name.empty() || !property)
    return property;

  if (sub_name[0] == '.') {
    OptionValueProperties *sub_properties =
        property->GetValue()->GetAsProperties();
    if (sub_properties)
      return sub_properties->GetPropertyAtPath(exe_ctx, will_modify,
                                                sub_name.drop_front());
  }
  return nullptr;
}

void OptionValueProperties::DumpAllDescriptions(CommandInterpreter &interpreter,
                                                Stream &strm) const {
  size_t max_name_len = 0;
  const size_t num_properties = m_properties.size();
  for (size_t i = 0; i < num_properties; ++i) {
    const Property *property = ProtectedGetPropertyAtIndex(i);
    if (property)
      max_name_len = std::max<size_t>(property->GetName().size(), max_name_len);
  }
  for (size_t i = 0; i < num_properties; ++i) {
    const Property *property = ProtectedGetPropertyAtIndex(i);
    if (property)
      property->DumpDescription(interpreter, strm, max_name_len, false);
  }
}

void OptionValueProperties::Apropos(
    llvm::StringRef keyword,
    std::vector<const Property *> &matching_properties) const {
  const size_t num_properties = m_properties.size();
  StreamString strm;
  for (size_t i = 0; i < num_properties; ++i) {
    const Property *property = ProtectedGetPropertyAtIndex(i);
    if (property) {
      const OptionValueProperties *properties =
          property->GetValue()->GetAsProperties();
      if (properties) {
        properties->Apropos(keyword, matching_properties);
      } else {
        bool match = false;
        llvm::StringRef name = property->GetName();
        if (name.contains_lower(keyword))
          match = true;
        else {
          llvm::StringRef desc = property->GetDescription();
          if (desc.contains_lower(keyword))
            match = true;
        }
        if (match) {
          matching_properties.push_back(property);
        }
      }
    }
  }
}

lldb::OptionValuePropertiesSP
OptionValueProperties::GetSubProperty(const ExecutionContext *exe_ctx,
                                      const ConstString &name) {
  lldb::OptionValueSP option_value_sp(GetValueForKey(exe_ctx, name, false));
  if (option_value_sp) {
    OptionValueProperties *ov_properties = option_value_sp->GetAsProperties();
    if (ov_properties)
      return ov_properties->shared_from_this();
  }
  return lldb::OptionValuePropertiesSP();
}
