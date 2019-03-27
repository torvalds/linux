//===-- ValueObject.cpp -----------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "lldb/Core/ValueObject.h"

#include "lldb/Core/Address.h"
#include "lldb/Core/Module.h"
#include "lldb/Core/ValueObjectCast.h"
#include "lldb/Core/ValueObjectChild.h"
#include "lldb/Core/ValueObjectConstResult.h"
#include "lldb/Core/ValueObjectDynamicValue.h"
#include "lldb/Core/ValueObjectMemory.h"
#include "lldb/Core/ValueObjectSyntheticFilter.h"
#include "lldb/DataFormatters/DataVisualization.h"
#include "lldb/DataFormatters/DumpValueObjectOptions.h"
#include "lldb/DataFormatters/FormatManager.h"
#include "lldb/DataFormatters/StringPrinter.h"
#include "lldb/DataFormatters/TypeFormat.h"
#include "lldb/DataFormatters/TypeSummary.h"
#include "lldb/DataFormatters/TypeValidator.h"
#include "lldb/DataFormatters/ValueObjectPrinter.h"
#include "lldb/Expression/ExpressionVariable.h"
#include "lldb/Symbol/ClangASTContext.h"
#include "lldb/Symbol/CompileUnit.h"
#include "lldb/Symbol/CompilerType.h"
#include "lldb/Symbol/Declaration.h"
#include "lldb/Symbol/SymbolContext.h"
#include "lldb/Symbol/Type.h"
#include "lldb/Target/ExecutionContext.h"
#include "lldb/Target/Language.h"
#include "lldb/Target/LanguageRuntime.h"
#include "lldb/Target/ObjCLanguageRuntime.h"
#include "lldb/Target/Process.h"
#include "lldb/Target/StackFrame.h"
#include "lldb/Target/Target.h"
#include "lldb/Target/Thread.h"
#include "lldb/Target/ThreadList.h"
#include "lldb/Utility/DataBuffer.h"
#include "lldb/Utility/DataBufferHeap.h"
#include "lldb/Utility/Flags.h"
#include "lldb/Utility/Log.h"
#include "lldb/Utility/Logging.h"
#include "lldb/Utility/Scalar.h"
#include "lldb/Utility/SharingPtr.h"
#include "lldb/Utility/Stream.h"
#include "lldb/Utility/StreamString.h"
#include "lldb/lldb-private-types.h"

#include "llvm/Support/Compiler.h"

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <memory>
#include <tuple>

#include <assert.h>
#include <inttypes.h>
#include <stdio.h>
#include <string.h>

namespace lldb_private {
class ExecutionContextScope;
}
namespace lldb_private {
class SymbolContextScope;
}

using namespace lldb;
using namespace lldb_private;
using namespace lldb_utility;

static user_id_t g_value_obj_uid = 0;

//----------------------------------------------------------------------
// ValueObject constructor
//----------------------------------------------------------------------
ValueObject::ValueObject(ValueObject &parent)
    : UserID(++g_value_obj_uid), // Unique identifier for every value object
      m_parent(&parent), m_root(NULL), m_update_point(parent.GetUpdatePoint()),
      m_name(), m_data(), m_value(), m_error(), m_value_str(),
      m_old_value_str(), m_location_str(), m_summary_str(), m_object_desc_str(),
      m_validation_result(), m_manager(parent.GetManager()), m_children(),
      m_synthetic_children(), m_dynamic_value(NULL), m_synthetic_value(NULL),
      m_deref_valobj(NULL), m_format(eFormatDefault),
      m_last_format(eFormatDefault), m_last_format_mgr_revision(0),
      m_type_summary_sp(), m_type_format_sp(), m_synthetic_children_sp(),
      m_type_validator_sp(), m_user_id_of_forced_summary(),
      m_address_type_of_ptr_or_ref_children(eAddressTypeInvalid),
      m_value_checksum(),
      m_preferred_display_language(lldb::eLanguageTypeUnknown),
      m_language_flags(0), m_value_is_valid(false), m_value_did_change(false),
      m_children_count_valid(false), m_old_value_valid(false),
      m_is_deref_of_parent(false), m_is_array_item_for_pointer(false),
      m_is_bitfield_for_scalar(false), m_is_child_at_offset(false),
      m_is_getting_summary(false),
      m_did_calculate_complete_objc_class_type(false),
      m_is_synthetic_children_generated(
          parent.m_is_synthetic_children_generated) {
  m_manager->ManageObject(this);
}

//----------------------------------------------------------------------
// ValueObject constructor
//----------------------------------------------------------------------
ValueObject::ValueObject(ExecutionContextScope *exe_scope,
                         AddressType child_ptr_or_ref_addr_type)
    : UserID(++g_value_obj_uid), // Unique identifier for every value object
      m_parent(NULL), m_root(NULL), m_update_point(exe_scope), m_name(),
      m_data(), m_value(), m_error(), m_value_str(), m_old_value_str(),
      m_location_str(), m_summary_str(), m_object_desc_str(),
      m_validation_result(), m_manager(), m_children(), m_synthetic_children(),
      m_dynamic_value(NULL), m_synthetic_value(NULL), m_deref_valobj(NULL),
      m_format(eFormatDefault), m_last_format(eFormatDefault),
      m_last_format_mgr_revision(0), m_type_summary_sp(), m_type_format_sp(),
      m_synthetic_children_sp(), m_type_validator_sp(),
      m_user_id_of_forced_summary(),
      m_address_type_of_ptr_or_ref_children(child_ptr_or_ref_addr_type),
      m_value_checksum(),
      m_preferred_display_language(lldb::eLanguageTypeUnknown),
      m_language_flags(0), m_value_is_valid(false), m_value_did_change(false),
      m_children_count_valid(false), m_old_value_valid(false),
      m_is_deref_of_parent(false), m_is_array_item_for_pointer(false),
      m_is_bitfield_for_scalar(false), m_is_child_at_offset(false),
      m_is_getting_summary(false),
      m_did_calculate_complete_objc_class_type(false),
      m_is_synthetic_children_generated(false) {
  m_manager = new ValueObjectManager();
  m_manager->ManageObject(this);
}

//----------------------------------------------------------------------
// Destructor
//----------------------------------------------------------------------
ValueObject::~ValueObject() {}

bool ValueObject::UpdateValueIfNeeded(bool update_format) {

  bool did_change_formats = false;

  if (update_format)
    did_change_formats = UpdateFormatsIfNeeded();

  // If this is a constant value, then our success is predicated on whether we
  // have an error or not
  if (GetIsConstant()) {
    // if you are constant, things might still have changed behind your back
    // (e.g. you are a frozen object and things have changed deeper than you
    // cared to freeze-dry yourself) in this case, your value has not changed,
    // but "computed" entries might have, so you might now have a different
    // summary, or a different object description. clear these so we will
    // recompute them
    if (update_format && !did_change_formats)
      ClearUserVisibleData(eClearUserVisibleDataItemsSummary |
                           eClearUserVisibleDataItemsDescription);
    return m_error.Success();
  }

  bool first_update = IsChecksumEmpty();

  if (NeedsUpdating()) {
    m_update_point.SetUpdated();

    // Save the old value using swap to avoid a string copy which also will
    // clear our m_value_str
    if (m_value_str.empty()) {
      m_old_value_valid = false;
    } else {
      m_old_value_valid = true;
      m_old_value_str.swap(m_value_str);
      ClearUserVisibleData(eClearUserVisibleDataItemsValue);
    }

    ClearUserVisibleData();

    if (IsInScope()) {
      const bool value_was_valid = GetValueIsValid();
      SetValueDidChange(false);

      m_error.Clear();

      // Call the pure virtual function to update the value

      bool need_compare_checksums = false;
      llvm::SmallVector<uint8_t, 16> old_checksum;

      if (!first_update && CanProvideValue()) {
        need_compare_checksums = true;
        old_checksum.resize(m_value_checksum.size());
        std::copy(m_value_checksum.begin(), m_value_checksum.end(),
                  old_checksum.begin());
      }

      bool success = UpdateValue();

      SetValueIsValid(success);

      if (success) {
        const uint64_t max_checksum_size = 128;
        m_data.Checksum(m_value_checksum, max_checksum_size);
      } else {
        need_compare_checksums = false;
        m_value_checksum.clear();
      }

      assert(!need_compare_checksums ||
             (!old_checksum.empty() && !m_value_checksum.empty()));

      if (first_update)
        SetValueDidChange(false);
      else if (!m_value_did_change && !success) {
        // The value wasn't gotten successfully, so we mark this as changed if
        // the value used to be valid and now isn't
        SetValueDidChange(value_was_valid);
      } else if (need_compare_checksums) {
        SetValueDidChange(memcmp(&old_checksum[0], &m_value_checksum[0],
                                 m_value_checksum.size()));
      }

    } else {
      m_error.SetErrorString("out of scope");
    }
  }
  return m_error.Success();
}

bool ValueObject::UpdateFormatsIfNeeded() {
  Log *log(lldb_private::GetLogIfAllCategoriesSet(LIBLLDB_LOG_DATAFORMATTERS));
  if (log)
    log->Printf("[%s %p] checking for FormatManager revisions. ValueObject "
                "rev: %d - Global rev: %d",
                GetName().GetCString(), static_cast<void *>(this),
                m_last_format_mgr_revision,
                DataVisualization::GetCurrentRevision());

  bool any_change = false;

  if ((m_last_format_mgr_revision != DataVisualization::GetCurrentRevision())) {
    m_last_format_mgr_revision = DataVisualization::GetCurrentRevision();
    any_change = true;

    SetValueFormat(DataVisualization::GetFormat(*this, eNoDynamicValues));
    SetSummaryFormat(
        DataVisualization::GetSummaryFormat(*this, GetDynamicValueType()));
#ifndef LLDB_DISABLE_PYTHON
    SetSyntheticChildren(
        DataVisualization::GetSyntheticChildren(*this, GetDynamicValueType()));
#endif
    SetValidator(DataVisualization::GetValidator(*this, GetDynamicValueType()));
  }

  return any_change;
}

void ValueObject::SetNeedsUpdate() {
  m_update_point.SetNeedsUpdate();
  // We have to clear the value string here so ConstResult children will notice
  // if their values are changed by hand (i.e. with SetValueAsCString).
  ClearUserVisibleData(eClearUserVisibleDataItemsValue);
}

void ValueObject::ClearDynamicTypeInformation() {
  m_children_count_valid = false;
  m_did_calculate_complete_objc_class_type = false;
  m_last_format_mgr_revision = 0;
  m_override_type = CompilerType();
  SetValueFormat(lldb::TypeFormatImplSP());
  SetSummaryFormat(lldb::TypeSummaryImplSP());
  SetSyntheticChildren(lldb::SyntheticChildrenSP());
}

CompilerType ValueObject::MaybeCalculateCompleteType() {
  CompilerType compiler_type(GetCompilerTypeImpl());

  if (m_did_calculate_complete_objc_class_type) {
    if (m_override_type.IsValid())
      return m_override_type;
    else
      return compiler_type;
  }

  CompilerType class_type;
  bool is_pointer_type = false;

  if (ClangASTContext::IsObjCObjectPointerType(compiler_type, &class_type)) {
    is_pointer_type = true;
  } else if (ClangASTContext::IsObjCObjectOrInterfaceType(compiler_type)) {
    class_type = compiler_type;
  } else {
    return compiler_type;
  }

  m_did_calculate_complete_objc_class_type = true;

  if (class_type) {
    ConstString class_name(class_type.GetConstTypeName());

    if (class_name) {
      ProcessSP process_sp(
          GetUpdatePoint().GetExecutionContextRef().GetProcessSP());

      if (process_sp) {
        ObjCLanguageRuntime *objc_language_runtime(
            process_sp->GetObjCLanguageRuntime());

        if (objc_language_runtime) {
          TypeSP complete_objc_class_type_sp =
              objc_language_runtime->LookupInCompleteClassCache(class_name);

          if (complete_objc_class_type_sp) {
            CompilerType complete_class(
                complete_objc_class_type_sp->GetFullCompilerType());

            if (complete_class.GetCompleteType()) {
              if (is_pointer_type) {
                m_override_type = complete_class.GetPointerType();
              } else {
                m_override_type = complete_class;
              }

              if (m_override_type.IsValid())
                return m_override_type;
            }
          }
        }
      }
    }
  }
  return compiler_type;
}

CompilerType ValueObject::GetCompilerType() {
  return MaybeCalculateCompleteType();
}

TypeImpl ValueObject::GetTypeImpl() { return TypeImpl(GetCompilerType()); }

DataExtractor &ValueObject::GetDataExtractor() {
  UpdateValueIfNeeded(false);
  return m_data;
}

const Status &ValueObject::GetError() {
  UpdateValueIfNeeded(false);
  return m_error;
}

const ConstString &ValueObject::GetName() const { return m_name; }

const char *ValueObject::GetLocationAsCString() {
  return GetLocationAsCStringImpl(m_value, m_data);
}

const char *ValueObject::GetLocationAsCStringImpl(const Value &value,
                                                  const DataExtractor &data) {
  if (UpdateValueIfNeeded(false)) {
    if (m_location_str.empty()) {
      StreamString sstr;

      Value::ValueType value_type = value.GetValueType();

      switch (value_type) {
      case Value::eValueTypeScalar:
      case Value::eValueTypeVector:
        if (value.GetContextType() == Value::eContextTypeRegisterInfo) {
          RegisterInfo *reg_info = value.GetRegisterInfo();
          if (reg_info) {
            if (reg_info->name)
              m_location_str = reg_info->name;
            else if (reg_info->alt_name)
              m_location_str = reg_info->alt_name;
            if (m_location_str.empty())
              m_location_str = (reg_info->encoding == lldb::eEncodingVector)
                                   ? "vector"
                                   : "scalar";
          }
        }
        if (m_location_str.empty())
          m_location_str =
              (value_type == Value::eValueTypeVector) ? "vector" : "scalar";
        break;

      case Value::eValueTypeLoadAddress:
      case Value::eValueTypeFileAddress:
      case Value::eValueTypeHostAddress: {
        uint32_t addr_nibble_size = data.GetAddressByteSize() * 2;
        sstr.Printf("0x%*.*llx", addr_nibble_size, addr_nibble_size,
                    value.GetScalar().ULongLong(LLDB_INVALID_ADDRESS));
        m_location_str = sstr.GetString();
      } break;
      }
    }
  }
  return m_location_str.c_str();
}

Value &ValueObject::GetValue() { return m_value; }

const Value &ValueObject::GetValue() const { return m_value; }

bool ValueObject::ResolveValue(Scalar &scalar) {
  if (UpdateValueIfNeeded(
          false)) // make sure that you are up to date before returning anything
  {
    ExecutionContext exe_ctx(GetExecutionContextRef());
    Value tmp_value(m_value);
    scalar = tmp_value.ResolveValue(&exe_ctx);
    if (scalar.IsValid()) {
      const uint32_t bitfield_bit_size = GetBitfieldBitSize();
      if (bitfield_bit_size)
        return scalar.ExtractBitfield(bitfield_bit_size,
                                      GetBitfieldBitOffset());
      return true;
    }
  }
  return false;
}

bool ValueObject::IsLogicalTrue(Status &error) {
  if (Language *language = Language::FindPlugin(GetObjectRuntimeLanguage())) {
    LazyBool is_logical_true = language->IsLogicalTrue(*this, error);
    switch (is_logical_true) {
    case eLazyBoolYes:
    case eLazyBoolNo:
      return (is_logical_true == true);
    case eLazyBoolCalculate:
      break;
    }
  }

  Scalar scalar_value;

  if (!ResolveValue(scalar_value)) {
    error.SetErrorString("failed to get a scalar result");
    return false;
  }

  bool ret;
  ret = scalar_value.ULongLong(1) != 0;
  error.Clear();
  return ret;
}

bool ValueObject::GetValueIsValid() const { return m_value_is_valid; }

void ValueObject::SetValueIsValid(bool b) { m_value_is_valid = b; }

bool ValueObject::GetValueDidChange() { return m_value_did_change; }

void ValueObject::SetValueDidChange(bool value_changed) {
  m_value_did_change = value_changed;
}

ValueObjectSP ValueObject::GetChildAtIndex(size_t idx, bool can_create) {
  ValueObjectSP child_sp;
  // We may need to update our value if we are dynamic
  if (IsPossibleDynamicType())
    UpdateValueIfNeeded(false);
  if (idx < GetNumChildren()) {
    // Check if we have already made the child value object?
    if (can_create && !m_children.HasChildAtIndex(idx)) {
      // No we haven't created the child at this index, so lets have our
      // subclass do it and cache the result for quick future access.
      m_children.SetChildAtIndex(idx, CreateChildAtIndex(idx, false, 0));
    }

    ValueObject *child = m_children.GetChildAtIndex(idx);
    if (child != NULL)
      return child->GetSP();
  }
  return child_sp;
}

lldb::ValueObjectSP
ValueObject::GetChildAtIndexPath(llvm::ArrayRef<size_t> idxs,
                                 size_t *index_of_error) {
  if (idxs.size() == 0)
    return GetSP();
  ValueObjectSP root(GetSP());
  for (size_t idx : idxs) {
    root = root->GetChildAtIndex(idx, true);
    if (!root) {
      if (index_of_error)
        *index_of_error = idx;
      return root;
    }
  }
  return root;
}

lldb::ValueObjectSP ValueObject::GetChildAtIndexPath(
  llvm::ArrayRef<std::pair<size_t, bool>> idxs, size_t *index_of_error) {
  if (idxs.size() == 0)
    return GetSP();
  ValueObjectSP root(GetSP());
  for (std::pair<size_t, bool> idx : idxs) {
    root = root->GetChildAtIndex(idx.first, idx.second);
    if (!root) {
      if (index_of_error)
        *index_of_error = idx.first;
      return root;
    }
  }
  return root;
}

lldb::ValueObjectSP
ValueObject::GetChildAtNamePath(llvm::ArrayRef<ConstString> names,
                                ConstString *name_of_error) {
  if (names.size() == 0)
    return GetSP();
  ValueObjectSP root(GetSP());
  for (ConstString name : names) {
    root = root->GetChildMemberWithName(name, true);
    if (!root) {
      if (name_of_error)
        *name_of_error = name;
      return root;
    }
  }
  return root;
}

lldb::ValueObjectSP ValueObject::GetChildAtNamePath(
    llvm::ArrayRef<std::pair<ConstString, bool>> names,
    ConstString *name_of_error) {
  if (names.size() == 0)
    return GetSP();
  ValueObjectSP root(GetSP());
  for (std::pair<ConstString, bool> name : names) {
    root = root->GetChildMemberWithName(name.first, name.second);
    if (!root) {
      if (name_of_error)
        *name_of_error = name.first;
      return root;
    }
  }
  return root;
}

size_t ValueObject::GetIndexOfChildWithName(const ConstString &name) {
  bool omit_empty_base_classes = true;
  return GetCompilerType().GetIndexOfChildWithName(name.GetCString(),
                                                   omit_empty_base_classes);
}

ValueObjectSP ValueObject::GetChildMemberWithName(const ConstString &name,
                                                  bool can_create) {
  // when getting a child by name, it could be buried inside some base classes
  // (which really aren't part of the expression path), so we need a vector of
  // indexes that can get us down to the correct child
  ValueObjectSP child_sp;

  // We may need to update our value if we are dynamic
  if (IsPossibleDynamicType())
    UpdateValueIfNeeded(false);

  std::vector<uint32_t> child_indexes;
  bool omit_empty_base_classes = true;
  const size_t num_child_indexes =
      GetCompilerType().GetIndexOfChildMemberWithName(
          name.GetCString(), omit_empty_base_classes, child_indexes);
  if (num_child_indexes > 0) {
    std::vector<uint32_t>::const_iterator pos = child_indexes.begin();
    std::vector<uint32_t>::const_iterator end = child_indexes.end();

    child_sp = GetChildAtIndex(*pos, can_create);
    for (++pos; pos != end; ++pos) {
      if (child_sp) {
        ValueObjectSP new_child_sp(child_sp->GetChildAtIndex(*pos, can_create));
        child_sp = new_child_sp;
      } else {
        child_sp.reset();
      }
    }
  }
  return child_sp;
}

size_t ValueObject::GetNumChildren(uint32_t max) {
  UpdateValueIfNeeded();

  if (max < UINT32_MAX) {
    if (m_children_count_valid) {
      size_t children_count = m_children.GetChildrenCount();
      return children_count <= max ? children_count : max;
    } else
      return CalculateNumChildren(max);
  }

  if (!m_children_count_valid) {
    SetNumChildren(CalculateNumChildren());
  }
  return m_children.GetChildrenCount();
}

bool ValueObject::MightHaveChildren() {
  bool has_children = false;
  const uint32_t type_info = GetTypeInfo();
  if (type_info) {
    if (type_info & (eTypeHasChildren | eTypeIsPointer | eTypeIsReference))
      has_children = true;
  } else {
    has_children = GetNumChildren() > 0;
  }
  return has_children;
}

// Should only be called by ValueObject::GetNumChildren()
void ValueObject::SetNumChildren(size_t num_children) {
  m_children_count_valid = true;
  m_children.SetChildrenCount(num_children);
}

void ValueObject::SetName(const ConstString &name) { m_name = name; }

ValueObject *ValueObject::CreateChildAtIndex(size_t idx,
                                             bool synthetic_array_member,
                                             int32_t synthetic_index) {
  ValueObject *valobj = NULL;

  bool omit_empty_base_classes = true;
  bool ignore_array_bounds = synthetic_array_member;
  std::string child_name_str;
  uint32_t child_byte_size = 0;
  int32_t child_byte_offset = 0;
  uint32_t child_bitfield_bit_size = 0;
  uint32_t child_bitfield_bit_offset = 0;
  bool child_is_base_class = false;
  bool child_is_deref_of_parent = false;
  uint64_t language_flags = 0;

  const bool transparent_pointers = !synthetic_array_member;
  CompilerType child_compiler_type;

  ExecutionContext exe_ctx(GetExecutionContextRef());

  child_compiler_type = GetCompilerType().GetChildCompilerTypeAtIndex(
      &exe_ctx, idx, transparent_pointers, omit_empty_base_classes,
      ignore_array_bounds, child_name_str, child_byte_size, child_byte_offset,
      child_bitfield_bit_size, child_bitfield_bit_offset, child_is_base_class,
      child_is_deref_of_parent, this, language_flags);
  if (child_compiler_type) {
    if (synthetic_index)
      child_byte_offset += child_byte_size * synthetic_index;

    ConstString child_name;
    if (!child_name_str.empty())
      child_name.SetCString(child_name_str.c_str());

    valobj = new ValueObjectChild(
        *this, child_compiler_type, child_name, child_byte_size,
        child_byte_offset, child_bitfield_bit_size, child_bitfield_bit_offset,
        child_is_base_class, child_is_deref_of_parent, eAddressTypeInvalid,
        language_flags);
  }

  return valobj;
}

bool ValueObject::GetSummaryAsCString(TypeSummaryImpl *summary_ptr,
                                      std::string &destination,
                                      lldb::LanguageType lang) {
  return GetSummaryAsCString(summary_ptr, destination,
                             TypeSummaryOptions().SetLanguage(lang));
}

bool ValueObject::GetSummaryAsCString(TypeSummaryImpl *summary_ptr,
                                      std::string &destination,
                                      const TypeSummaryOptions &options) {
  destination.clear();

  // ideally we would like to bail out if passing NULL, but if we do so we end
  // up not providing the summary for function pointers anymore
  if (/*summary_ptr == NULL ||*/ m_is_getting_summary)
    return false;

  m_is_getting_summary = true;

  TypeSummaryOptions actual_options(options);

  if (actual_options.GetLanguage() == lldb::eLanguageTypeUnknown)
    actual_options.SetLanguage(GetPreferredDisplayLanguage());

  // this is a hot path in code and we prefer to avoid setting this string all
  // too often also clearing out other information that we might care to see in
  // a crash log. might be useful in very specific situations though.
  /*Host::SetCrashDescriptionWithFormat("Trying to fetch a summary for %s %s.
   Summary provider's description is %s",
   GetTypeName().GetCString(),
   GetName().GetCString(),
   summary_ptr->GetDescription().c_str());*/

  if (UpdateValueIfNeeded(false) && summary_ptr) {
    if (HasSyntheticValue())
      m_synthetic_value->UpdateValueIfNeeded(); // the summary might depend on
                                                // the synthetic children being
                                                // up-to-date (e.g. ${svar%#})
    summary_ptr->FormatObject(this, destination, actual_options);
  }
  m_is_getting_summary = false;
  return !destination.empty();
}

const char *ValueObject::GetSummaryAsCString(lldb::LanguageType lang) {
  if (UpdateValueIfNeeded(true) && m_summary_str.empty()) {
    TypeSummaryOptions summary_options;
    summary_options.SetLanguage(lang);
    GetSummaryAsCString(GetSummaryFormat().get(), m_summary_str,
                        summary_options);
  }
  if (m_summary_str.empty())
    return NULL;
  return m_summary_str.c_str();
}

bool ValueObject::GetSummaryAsCString(std::string &destination,
                                      const TypeSummaryOptions &options) {
  return GetSummaryAsCString(GetSummaryFormat().get(), destination, options);
}

bool ValueObject::IsCStringContainer(bool check_pointer) {
  CompilerType pointee_or_element_compiler_type;
  const Flags type_flags(GetTypeInfo(&pointee_or_element_compiler_type));
  bool is_char_arr_ptr(type_flags.AnySet(eTypeIsArray | eTypeIsPointer) &&
                       pointee_or_element_compiler_type.IsCharType());
  if (!is_char_arr_ptr)
    return false;
  if (!check_pointer)
    return true;
  if (type_flags.Test(eTypeIsArray))
    return true;
  addr_t cstr_address = LLDB_INVALID_ADDRESS;
  AddressType cstr_address_type = eAddressTypeInvalid;
  cstr_address = GetAddressOf(true, &cstr_address_type);
  return (cstr_address != LLDB_INVALID_ADDRESS);
}

size_t ValueObject::GetPointeeData(DataExtractor &data, uint32_t item_idx,
                                   uint32_t item_count) {
  CompilerType pointee_or_element_compiler_type;
  const uint32_t type_info = GetTypeInfo(&pointee_or_element_compiler_type);
  const bool is_pointer_type = type_info & eTypeIsPointer;
  const bool is_array_type = type_info & eTypeIsArray;
  if (!(is_pointer_type || is_array_type))
    return 0;

  if (item_count == 0)
    return 0;

  ExecutionContext exe_ctx(GetExecutionContextRef());

  llvm::Optional<uint64_t> item_type_size =
      pointee_or_element_compiler_type.GetByteSize(
          exe_ctx.GetBestExecutionContextScope());
  if (!item_type_size)
    return 0;
  const uint64_t bytes = item_count * *item_type_size;
  const uint64_t offset = item_idx * *item_type_size;

  if (item_idx == 0 && item_count == 1) // simply a deref
  {
    if (is_pointer_type) {
      Status error;
      ValueObjectSP pointee_sp = Dereference(error);
      if (error.Fail() || pointee_sp.get() == NULL)
        return 0;
      return pointee_sp->GetData(data, error);
    } else {
      ValueObjectSP child_sp = GetChildAtIndex(0, true);
      if (child_sp.get() == NULL)
        return 0;
      Status error;
      return child_sp->GetData(data, error);
    }
    return true;
  } else /* (items > 1) */
  {
    Status error;
    lldb_private::DataBufferHeap *heap_buf_ptr = NULL;
    lldb::DataBufferSP data_sp(heap_buf_ptr =
                                   new lldb_private::DataBufferHeap());

    AddressType addr_type;
    lldb::addr_t addr = is_pointer_type ? GetPointerValue(&addr_type)
                                        : GetAddressOf(true, &addr_type);

    switch (addr_type) {
    case eAddressTypeFile: {
      ModuleSP module_sp(GetModule());
      if (module_sp) {
        addr = addr + offset;
        Address so_addr;
        module_sp->ResolveFileAddress(addr, so_addr);
        ExecutionContext exe_ctx(GetExecutionContextRef());
        Target *target = exe_ctx.GetTargetPtr();
        if (target) {
          heap_buf_ptr->SetByteSize(bytes);
          size_t bytes_read = target->ReadMemory(
              so_addr, false, heap_buf_ptr->GetBytes(), bytes, error);
          if (error.Success()) {
            data.SetData(data_sp);
            return bytes_read;
          }
        }
      }
    } break;
    case eAddressTypeLoad: {
      ExecutionContext exe_ctx(GetExecutionContextRef());
      Process *process = exe_ctx.GetProcessPtr();
      if (process) {
        heap_buf_ptr->SetByteSize(bytes);
        size_t bytes_read = process->ReadMemory(
            addr + offset, heap_buf_ptr->GetBytes(), bytes, error);
        if (error.Success() || bytes_read > 0) {
          data.SetData(data_sp);
          return bytes_read;
        }
      }
    } break;
    case eAddressTypeHost: {
      auto max_bytes =
          GetCompilerType().GetByteSize(exe_ctx.GetBestExecutionContextScope());
      if (max_bytes && *max_bytes > offset) {
        size_t bytes_read = std::min<uint64_t>(*max_bytes - offset, bytes);
        addr = m_value.GetScalar().ULongLong(LLDB_INVALID_ADDRESS);
        if (addr == 0 || addr == LLDB_INVALID_ADDRESS)
          break;
        heap_buf_ptr->CopyData((uint8_t *)(addr + offset), bytes_read);
        data.SetData(data_sp);
        return bytes_read;
      }
    } break;
    case eAddressTypeInvalid:
      break;
    }
  }
  return 0;
}

uint64_t ValueObject::GetData(DataExtractor &data, Status &error) {
  UpdateValueIfNeeded(false);
  ExecutionContext exe_ctx(GetExecutionContextRef());
  error = m_value.GetValueAsData(&exe_ctx, data, 0, GetModule().get());
  if (error.Fail()) {
    if (m_data.GetByteSize()) {
      data = m_data;
      error.Clear();
      return data.GetByteSize();
    } else {
      return 0;
    }
  }
  data.SetAddressByteSize(m_data.GetAddressByteSize());
  data.SetByteOrder(m_data.GetByteOrder());
  return data.GetByteSize();
}

bool ValueObject::SetData(DataExtractor &data, Status &error) {
  error.Clear();
  // Make sure our value is up to date first so that our location and location
  // type is valid.
  if (!UpdateValueIfNeeded(false)) {
    error.SetErrorString("unable to read value");
    return false;
  }

  uint64_t count = 0;
  const Encoding encoding = GetCompilerType().GetEncoding(count);

  const size_t byte_size = GetByteSize();

  Value::ValueType value_type = m_value.GetValueType();

  switch (value_type) {
  case Value::eValueTypeScalar: {
    Status set_error =
        m_value.GetScalar().SetValueFromData(data, encoding, byte_size);

    if (!set_error.Success()) {
      error.SetErrorStringWithFormat("unable to set scalar value: %s",
                                     set_error.AsCString());
      return false;
    }
  } break;
  case Value::eValueTypeLoadAddress: {
    // If it is a load address, then the scalar value is the storage location
    // of the data, and we have to shove this value down to that load location.
    ExecutionContext exe_ctx(GetExecutionContextRef());
    Process *process = exe_ctx.GetProcessPtr();
    if (process) {
      addr_t target_addr = m_value.GetScalar().ULongLong(LLDB_INVALID_ADDRESS);
      size_t bytes_written = process->WriteMemory(
          target_addr, data.GetDataStart(), byte_size, error);
      if (!error.Success())
        return false;
      if (bytes_written != byte_size) {
        error.SetErrorString("unable to write value to memory");
        return false;
      }
    }
  } break;
  case Value::eValueTypeHostAddress: {
    // If it is a host address, then we stuff the scalar as a DataBuffer into
    // the Value's data.
    DataBufferSP buffer_sp(new DataBufferHeap(byte_size, 0));
    m_data.SetData(buffer_sp, 0);
    data.CopyByteOrderedData(0, byte_size,
                             const_cast<uint8_t *>(m_data.GetDataStart()),
                             byte_size, m_data.GetByteOrder());
    m_value.GetScalar() = (uintptr_t)m_data.GetDataStart();
  } break;
  case Value::eValueTypeFileAddress:
  case Value::eValueTypeVector:
    break;
  }

  // If we have reached this point, then we have successfully changed the
  // value.
  SetNeedsUpdate();
  return true;
}

static bool CopyStringDataToBufferSP(const StreamString &source,
                                     lldb::DataBufferSP &destination) {
  destination.reset(new DataBufferHeap(source.GetSize() + 1, 0));
  memcpy(destination->GetBytes(), source.GetString().data(), source.GetSize());
  return true;
}

std::pair<size_t, bool>
ValueObject::ReadPointedString(lldb::DataBufferSP &buffer_sp, Status &error,
                               uint32_t max_length, bool honor_array,
                               Format item_format) {
  bool was_capped = false;
  StreamString s;
  ExecutionContext exe_ctx(GetExecutionContextRef());
  Target *target = exe_ctx.GetTargetPtr();

  if (!target) {
    s << "<no target to read from>";
    error.SetErrorString("no target to read from");
    CopyStringDataToBufferSP(s, buffer_sp);
    return {0, was_capped};
  }

  if (max_length == 0)
    max_length = target->GetMaximumSizeOfStringSummary();

  size_t bytes_read = 0;
  size_t total_bytes_read = 0;

  CompilerType compiler_type = GetCompilerType();
  CompilerType elem_or_pointee_compiler_type;
  const Flags type_flags(GetTypeInfo(&elem_or_pointee_compiler_type));
  if (type_flags.AnySet(eTypeIsArray | eTypeIsPointer) &&
      elem_or_pointee_compiler_type.IsCharType()) {
    addr_t cstr_address = LLDB_INVALID_ADDRESS;
    AddressType cstr_address_type = eAddressTypeInvalid;

    size_t cstr_len = 0;
    bool capped_data = false;
    const bool is_array = type_flags.Test(eTypeIsArray);
    if (is_array) {
      // We have an array
      uint64_t array_size = 0;
      if (compiler_type.IsArrayType(NULL, &array_size, NULL)) {
        cstr_len = array_size;
        if (cstr_len > max_length) {
          capped_data = true;
          cstr_len = max_length;
        }
      }
      cstr_address = GetAddressOf(true, &cstr_address_type);
    } else {
      // We have a pointer
      cstr_address = GetPointerValue(&cstr_address_type);
    }

    if (cstr_address == 0 || cstr_address == LLDB_INVALID_ADDRESS) {
      if (cstr_address_type == eAddressTypeHost && is_array) {
        const char *cstr = GetDataExtractor().PeekCStr(0);
        if (cstr == nullptr) {
          s << "<invalid address>";
          error.SetErrorString("invalid address");
          CopyStringDataToBufferSP(s, buffer_sp);
          return {0, was_capped};
        }
        buffer_sp.reset(new DataBufferHeap(cstr_len, 0));
        memcpy(buffer_sp->GetBytes(), cstr, cstr_len);
        return {cstr_len, was_capped};
      } else {
        s << "<invalid address>";
        error.SetErrorString("invalid address");
        CopyStringDataToBufferSP(s, buffer_sp);
        return {0, was_capped};
      }
    }

    Address cstr_so_addr(cstr_address);
    DataExtractor data;
    if (cstr_len > 0 && honor_array) {
      // I am using GetPointeeData() here to abstract the fact that some
      // ValueObjects are actually frozen pointers in the host but the pointed-
      // to data lives in the debuggee, and GetPointeeData() automatically
      // takes care of this
      GetPointeeData(data, 0, cstr_len);

      if ((bytes_read = data.GetByteSize()) > 0) {
        total_bytes_read = bytes_read;
        for (size_t offset = 0; offset < bytes_read; offset++)
          s.Printf("%c", *data.PeekData(offset, 1));
        if (capped_data)
          was_capped = true;
      }
    } else {
      cstr_len = max_length;
      const size_t k_max_buf_size = 64;

      size_t offset = 0;

      int cstr_len_displayed = -1;
      bool capped_cstr = false;
      // I am using GetPointeeData() here to abstract the fact that some
      // ValueObjects are actually frozen pointers in the host but the pointed-
      // to data lives in the debuggee, and GetPointeeData() automatically
      // takes care of this
      while ((bytes_read = GetPointeeData(data, offset, k_max_buf_size)) > 0) {
        total_bytes_read += bytes_read;
        const char *cstr = data.PeekCStr(0);
        size_t len = strnlen(cstr, k_max_buf_size);
        if (cstr_len_displayed < 0)
          cstr_len_displayed = len;

        if (len == 0)
          break;
        cstr_len_displayed += len;
        if (len > bytes_read)
          len = bytes_read;
        if (len > cstr_len)
          len = cstr_len;

        for (size_t offset = 0; offset < bytes_read; offset++)
          s.Printf("%c", *data.PeekData(offset, 1));

        if (len < k_max_buf_size)
          break;

        if (len >= cstr_len) {
          capped_cstr = true;
          break;
        }

        cstr_len -= len;
        offset += len;
      }

      if (cstr_len_displayed >= 0) {
        if (capped_cstr)
          was_capped = true;
      }
    }
  } else {
    error.SetErrorString("not a string object");
    s << "<not a string object>";
  }
  CopyStringDataToBufferSP(s, buffer_sp);
  return {total_bytes_read, was_capped};
}

std::pair<TypeValidatorResult, std::string> ValueObject::GetValidationStatus() {
  if (!UpdateValueIfNeeded(true))
    return {TypeValidatorResult::Success,
            ""}; // not the validator's job to discuss update problems

  if (m_validation_result.hasValue())
    return m_validation_result.getValue();

  if (!m_type_validator_sp)
    return {TypeValidatorResult::Success, ""}; // no validator no failure

  auto outcome = m_type_validator_sp->FormatObject(this);

  return (m_validation_result = {outcome.m_result, outcome.m_message})
      .getValue();
}

const char *ValueObject::GetObjectDescription() {

  if (!UpdateValueIfNeeded(true))
    return NULL;

  if (!m_object_desc_str.empty())
    return m_object_desc_str.c_str();

  ExecutionContext exe_ctx(GetExecutionContextRef());
  Process *process = exe_ctx.GetProcessPtr();
  if (process == NULL)
    return NULL;

  StreamString s;

  LanguageType language = GetObjectRuntimeLanguage();
  LanguageRuntime *runtime = process->GetLanguageRuntime(language);

  if (runtime == NULL) {
    // Aw, hell, if the things a pointer, or even just an integer, let's try
    // ObjC anyway...
    CompilerType compiler_type = GetCompilerType();
    if (compiler_type) {
      bool is_signed;
      if (compiler_type.IsIntegerType(is_signed) ||
          compiler_type.IsPointerType()) {
        runtime = process->GetLanguageRuntime(eLanguageTypeObjC);
      }
    }
  }

  if (runtime && runtime->GetObjectDescription(s, *this)) {
    m_object_desc_str.append(s.GetString());
  }

  if (m_object_desc_str.empty())
    return NULL;
  else
    return m_object_desc_str.c_str();
}

bool ValueObject::GetValueAsCString(const lldb_private::TypeFormatImpl &format,
                                    std::string &destination) {
  if (UpdateValueIfNeeded(false))
    return format.FormatObject(this, destination);
  else
    return false;
}

bool ValueObject::GetValueAsCString(lldb::Format format,
                                    std::string &destination) {
  return GetValueAsCString(TypeFormatImpl_Format(format), destination);
}

const char *ValueObject::GetValueAsCString() {
  if (UpdateValueIfNeeded(true)) {
    lldb::TypeFormatImplSP format_sp;
    lldb::Format my_format = GetFormat();
    if (my_format == lldb::eFormatDefault) {
      if (m_type_format_sp)
        format_sp = m_type_format_sp;
      else {
        if (m_is_bitfield_for_scalar)
          my_format = eFormatUnsigned;
        else {
          if (m_value.GetContextType() == Value::eContextTypeRegisterInfo) {
            const RegisterInfo *reg_info = m_value.GetRegisterInfo();
            if (reg_info)
              my_format = reg_info->format;
          } else {
            my_format = GetValue().GetCompilerType().GetFormat();
          }
        }
      }
    }
    if (my_format != m_last_format || m_value_str.empty()) {
      m_last_format = my_format;
      if (!format_sp)
        format_sp.reset(new TypeFormatImpl_Format(my_format));
      if (GetValueAsCString(*format_sp.get(), m_value_str)) {
        if (!m_value_did_change && m_old_value_valid) {
          // The value was gotten successfully, so we consider the value as
          // changed if the value string differs
          SetValueDidChange(m_old_value_str != m_value_str);
        }
      }
    }
  }
  if (m_value_str.empty())
    return NULL;
  return m_value_str.c_str();
}

// if > 8bytes, 0 is returned. this method should mostly be used to read
// address values out of pointers
uint64_t ValueObject::GetValueAsUnsigned(uint64_t fail_value, bool *success) {
  // If our byte size is zero this is an aggregate type that has children
  if (CanProvideValue()) {
    Scalar scalar;
    if (ResolveValue(scalar)) {
      if (success)
        *success = true;
      return scalar.ULongLong(fail_value);
    }
    // fallthrough, otherwise...
  }

  if (success)
    *success = false;
  return fail_value;
}

int64_t ValueObject::GetValueAsSigned(int64_t fail_value, bool *success) {
  // If our byte size is zero this is an aggregate type that has children
  if (CanProvideValue()) {
    Scalar scalar;
    if (ResolveValue(scalar)) {
      if (success)
        *success = true;
      return scalar.SLongLong(fail_value);
    }
    // fallthrough, otherwise...
  }

  if (success)
    *success = false;
  return fail_value;
}

// if any more "special cases" are added to
// ValueObject::DumpPrintableRepresentation() please keep this call up to date
// by returning true for your new special cases. We will eventually move to
// checking this call result before trying to display special cases
bool ValueObject::HasSpecialPrintableRepresentation(
    ValueObjectRepresentationStyle val_obj_display, Format custom_format) {
  Flags flags(GetTypeInfo());
  if (flags.AnySet(eTypeIsArray | eTypeIsPointer) &&
      val_obj_display == ValueObject::eValueObjectRepresentationStyleValue) {
    if (IsCStringContainer(true) &&
        (custom_format == eFormatCString || custom_format == eFormatCharArray ||
         custom_format == eFormatChar || custom_format == eFormatVectorOfChar))
      return true;

    if (flags.Test(eTypeIsArray)) {
      if ((custom_format == eFormatBytes) ||
          (custom_format == eFormatBytesWithASCII))
        return true;

      if ((custom_format == eFormatVectorOfChar) ||
          (custom_format == eFormatVectorOfFloat32) ||
          (custom_format == eFormatVectorOfFloat64) ||
          (custom_format == eFormatVectorOfSInt16) ||
          (custom_format == eFormatVectorOfSInt32) ||
          (custom_format == eFormatVectorOfSInt64) ||
          (custom_format == eFormatVectorOfSInt8) ||
          (custom_format == eFormatVectorOfUInt128) ||
          (custom_format == eFormatVectorOfUInt16) ||
          (custom_format == eFormatVectorOfUInt32) ||
          (custom_format == eFormatVectorOfUInt64) ||
          (custom_format == eFormatVectorOfUInt8))
        return true;
    }
  }
  return false;
}

bool ValueObject::DumpPrintableRepresentation(
    Stream &s, ValueObjectRepresentationStyle val_obj_display,
    Format custom_format, PrintableRepresentationSpecialCases special,
    bool do_dump_error) {

  Flags flags(GetTypeInfo());

  bool allow_special =
      (special == ValueObject::PrintableRepresentationSpecialCases::eAllow);
  const bool only_special = false;

  if (allow_special) {
    if (flags.AnySet(eTypeIsArray | eTypeIsPointer) &&
        val_obj_display == ValueObject::eValueObjectRepresentationStyleValue) {
      // when being asked to get a printable display an array or pointer type
      // directly, try to "do the right thing"

      if (IsCStringContainer(true) &&
          (custom_format == eFormatCString ||
           custom_format == eFormatCharArray || custom_format == eFormatChar ||
           custom_format ==
               eFormatVectorOfChar)) // print char[] & char* directly
      {
        Status error;
        lldb::DataBufferSP buffer_sp;
        std::pair<size_t, bool> read_string = ReadPointedString(
            buffer_sp, error, 0, (custom_format == eFormatVectorOfChar) ||
                                     (custom_format == eFormatCharArray));
        lldb_private::formatters::StringPrinter::
            ReadBufferAndDumpToStreamOptions options(*this);
        options.SetData(DataExtractor(
            buffer_sp, lldb::eByteOrderInvalid,
            8)); // none of this matters for a string - pass some defaults
        options.SetStream(&s);
        options.SetPrefixToken(0);
        options.SetQuote('"');
        options.SetSourceSize(buffer_sp->GetByteSize());
        options.SetIsTruncated(read_string.second);
        formatters::StringPrinter::ReadBufferAndDumpToStream<
            lldb_private::formatters::StringPrinter::StringElementType::ASCII>(
            options);
        return !error.Fail();
      }

      if (custom_format == eFormatEnum)
        return false;

      // this only works for arrays, because I have no way to know when the
      // pointed memory ends, and no special \0 end of data marker
      if (flags.Test(eTypeIsArray)) {
        if ((custom_format == eFormatBytes) ||
            (custom_format == eFormatBytesWithASCII)) {
          const size_t count = GetNumChildren();

          s << '[';
          for (size_t low = 0; low < count; low++) {

            if (low)
              s << ',';

            ValueObjectSP child = GetChildAtIndex(low, true);
            if (!child.get()) {
              s << "<invalid child>";
              continue;
            }
            child->DumpPrintableRepresentation(
                s, ValueObject::eValueObjectRepresentationStyleValue,
                custom_format);
          }

          s << ']';

          return true;
        }

        if ((custom_format == eFormatVectorOfChar) ||
            (custom_format == eFormatVectorOfFloat32) ||
            (custom_format == eFormatVectorOfFloat64) ||
            (custom_format == eFormatVectorOfSInt16) ||
            (custom_format == eFormatVectorOfSInt32) ||
            (custom_format == eFormatVectorOfSInt64) ||
            (custom_format == eFormatVectorOfSInt8) ||
            (custom_format == eFormatVectorOfUInt128) ||
            (custom_format == eFormatVectorOfUInt16) ||
            (custom_format == eFormatVectorOfUInt32) ||
            (custom_format == eFormatVectorOfUInt64) ||
            (custom_format == eFormatVectorOfUInt8)) // arrays of bytes, bytes
                                                     // with ASCII or any vector
                                                     // format should be printed
                                                     // directly
        {
          const size_t count = GetNumChildren();

          Format format = FormatManager::GetSingleItemFormat(custom_format);

          s << '[';
          for (size_t low = 0; low < count; low++) {

            if (low)
              s << ',';

            ValueObjectSP child = GetChildAtIndex(low, true);
            if (!child.get()) {
              s << "<invalid child>";
              continue;
            }
            child->DumpPrintableRepresentation(
                s, ValueObject::eValueObjectRepresentationStyleValue, format);
          }

          s << ']';

          return true;
        }
      }

      if ((custom_format == eFormatBoolean) ||
          (custom_format == eFormatBinary) || (custom_format == eFormatChar) ||
          (custom_format == eFormatCharPrintable) ||
          (custom_format == eFormatComplexFloat) ||
          (custom_format == eFormatDecimal) || (custom_format == eFormatHex) ||
          (custom_format == eFormatHexUppercase) ||
          (custom_format == eFormatFloat) || (custom_format == eFormatOctal) ||
          (custom_format == eFormatOSType) ||
          (custom_format == eFormatUnicode16) ||
          (custom_format == eFormatUnicode32) ||
          (custom_format == eFormatUnsigned) ||
          (custom_format == eFormatPointer) ||
          (custom_format == eFormatComplexInteger) ||
          (custom_format == eFormatComplex) ||
          (custom_format == eFormatDefault)) // use the [] operator
        return false;
    }
  }

  if (only_special)
    return false;

  bool var_success = false;

  {
    llvm::StringRef str;

    // this is a local stream that we are using to ensure that the data pointed
    // to by cstr survives long enough for us to copy it to its destination -
    // it is necessary to have this temporary storage area for cases where our
    // desired output is not backed by some other longer-term storage
    StreamString strm;

    if (custom_format != eFormatInvalid)
      SetFormat(custom_format);

    switch (val_obj_display) {
    case eValueObjectRepresentationStyleValue:
      str = GetValueAsCString();
      break;

    case eValueObjectRepresentationStyleSummary:
      str = GetSummaryAsCString();
      break;

    case eValueObjectRepresentationStyleLanguageSpecific:
      str = GetObjectDescription();
      break;

    case eValueObjectRepresentationStyleLocation:
      str = GetLocationAsCString();
      break;

    case eValueObjectRepresentationStyleChildrenCount:
      strm.Printf("%" PRIu64 "", (uint64_t)GetNumChildren());
      str = strm.GetString();
      break;

    case eValueObjectRepresentationStyleType:
      str = GetTypeName().GetStringRef();
      break;

    case eValueObjectRepresentationStyleName:
      str = GetName().GetStringRef();
      break;

    case eValueObjectRepresentationStyleExpressionPath:
      GetExpressionPath(strm, false);
      str = strm.GetString();
      break;
    }

    if (str.empty()) {
      if (val_obj_display == eValueObjectRepresentationStyleValue)
        str = GetSummaryAsCString();
      else if (val_obj_display == eValueObjectRepresentationStyleSummary) {
        if (!CanProvideValue()) {
          strm.Printf("%s @ %s", GetTypeName().AsCString(),
                      GetLocationAsCString());
          str = strm.GetString();
        } else
          str = GetValueAsCString();
      }
    }

    if (!str.empty())
      s << str;
    else {
      if (m_error.Fail()) {
        if (do_dump_error)
          s.Printf("<%s>", m_error.AsCString());
        else
          return false;
      } else if (val_obj_display == eValueObjectRepresentationStyleSummary)
        s.PutCString("<no summary available>");
      else if (val_obj_display == eValueObjectRepresentationStyleValue)
        s.PutCString("<no value available>");
      else if (val_obj_display ==
               eValueObjectRepresentationStyleLanguageSpecific)
        s.PutCString("<not a valid Objective-C object>"); // edit this if we
                                                          // have other runtimes
                                                          // that support a
                                                          // description
      else
        s.PutCString("<no printable representation>");
    }

    // we should only return false here if we could not do *anything* even if
    // we have an error message as output, that's a success from our callers'
    // perspective, so return true
    var_success = true;

    if (custom_format != eFormatInvalid)
      SetFormat(eFormatDefault);
  }

  return var_success;
}

addr_t ValueObject::GetAddressOf(bool scalar_is_load_address,
                                 AddressType *address_type) {
  // Can't take address of a bitfield
  if (IsBitfield())
    return LLDB_INVALID_ADDRESS;

  if (!UpdateValueIfNeeded(false))
    return LLDB_INVALID_ADDRESS;

  switch (m_value.GetValueType()) {
  case Value::eValueTypeScalar:
  case Value::eValueTypeVector:
    if (scalar_is_load_address) {
      if (address_type)
        *address_type = eAddressTypeLoad;
      return m_value.GetScalar().ULongLong(LLDB_INVALID_ADDRESS);
    }
    break;

  case Value::eValueTypeLoadAddress:
  case Value::eValueTypeFileAddress: {
    if (address_type)
      *address_type = m_value.GetValueAddressType();
    return m_value.GetScalar().ULongLong(LLDB_INVALID_ADDRESS);
  } break;
  case Value::eValueTypeHostAddress: {
    if (address_type)
      *address_type = m_value.GetValueAddressType();
    return LLDB_INVALID_ADDRESS;
  } break;
  }
  if (address_type)
    *address_type = eAddressTypeInvalid;
  return LLDB_INVALID_ADDRESS;
}

addr_t ValueObject::GetPointerValue(AddressType *address_type) {
  addr_t address = LLDB_INVALID_ADDRESS;
  if (address_type)
    *address_type = eAddressTypeInvalid;

  if (!UpdateValueIfNeeded(false))
    return address;

  switch (m_value.GetValueType()) {
  case Value::eValueTypeScalar:
  case Value::eValueTypeVector:
    address = m_value.GetScalar().ULongLong(LLDB_INVALID_ADDRESS);
    break;

  case Value::eValueTypeHostAddress:
  case Value::eValueTypeLoadAddress:
  case Value::eValueTypeFileAddress: {
    lldb::offset_t data_offset = 0;
    address = m_data.GetPointer(&data_offset);
  } break;
  }

  if (address_type)
    *address_type = GetAddressTypeOfChildren();

  return address;
}

bool ValueObject::SetValueFromCString(const char *value_str, Status &error) {
  error.Clear();
  // Make sure our value is up to date first so that our location and location
  // type is valid.
  if (!UpdateValueIfNeeded(false)) {
    error.SetErrorString("unable to read value");
    return false;
  }

  uint64_t count = 0;
  const Encoding encoding = GetCompilerType().GetEncoding(count);

  const size_t byte_size = GetByteSize();

  Value::ValueType value_type = m_value.GetValueType();

  if (value_type == Value::eValueTypeScalar) {
    // If the value is already a scalar, then let the scalar change itself:
    m_value.GetScalar().SetValueFromCString(value_str, encoding, byte_size);
  } else if (byte_size <= 16) {
    // If the value fits in a scalar, then make a new scalar and again let the
    // scalar code do the conversion, then figure out where to put the new
    // value.
    Scalar new_scalar;
    error = new_scalar.SetValueFromCString(value_str, encoding, byte_size);
    if (error.Success()) {
      switch (value_type) {
      case Value::eValueTypeLoadAddress: {
        // If it is a load address, then the scalar value is the storage
        // location of the data, and we have to shove this value down to that
        // load location.
        ExecutionContext exe_ctx(GetExecutionContextRef());
        Process *process = exe_ctx.GetProcessPtr();
        if (process) {
          addr_t target_addr =
              m_value.GetScalar().ULongLong(LLDB_INVALID_ADDRESS);
          size_t bytes_written = process->WriteScalarToMemory(
              target_addr, new_scalar, byte_size, error);
          if (!error.Success())
            return false;
          if (bytes_written != byte_size) {
            error.SetErrorString("unable to write value to memory");
            return false;
          }
        }
      } break;
      case Value::eValueTypeHostAddress: {
        // If it is a host address, then we stuff the scalar as a DataBuffer
        // into the Value's data.
        DataExtractor new_data;
        new_data.SetByteOrder(m_data.GetByteOrder());

        DataBufferSP buffer_sp(new DataBufferHeap(byte_size, 0));
        m_data.SetData(buffer_sp, 0);
        bool success = new_scalar.GetData(new_data);
        if (success) {
          new_data.CopyByteOrderedData(
              0, byte_size, const_cast<uint8_t *>(m_data.GetDataStart()),
              byte_size, m_data.GetByteOrder());
        }
        m_value.GetScalar() = (uintptr_t)m_data.GetDataStart();

      } break;
      case Value::eValueTypeFileAddress:
      case Value::eValueTypeScalar:
      case Value::eValueTypeVector:
        break;
      }
    } else {
      return false;
    }
  } else {
    // We don't support setting things bigger than a scalar at present.
    error.SetErrorString("unable to write aggregate data type");
    return false;
  }

  // If we have reached this point, then we have successfully changed the
  // value.
  SetNeedsUpdate();
  return true;
}

bool ValueObject::GetDeclaration(Declaration &decl) {
  decl.Clear();
  return false;
}

ConstString ValueObject::GetTypeName() {
  return GetCompilerType().GetConstTypeName();
}

ConstString ValueObject::GetDisplayTypeName() { return GetTypeName(); }

ConstString ValueObject::GetQualifiedTypeName() {
  return GetCompilerType().GetConstQualifiedTypeName();
}

LanguageType ValueObject::GetObjectRuntimeLanguage() {
  return GetCompilerType().GetMinimumLanguage();
}

void ValueObject::AddSyntheticChild(const ConstString &key,
                                    ValueObject *valobj) {
  m_synthetic_children[key] = valobj;
}

ValueObjectSP ValueObject::GetSyntheticChild(const ConstString &key) const {
  ValueObjectSP synthetic_child_sp;
  std::map<ConstString, ValueObject *>::const_iterator pos =
      m_synthetic_children.find(key);
  if (pos != m_synthetic_children.end())
    synthetic_child_sp = pos->second->GetSP();
  return synthetic_child_sp;
}

uint32_t
ValueObject::GetTypeInfo(CompilerType *pointee_or_element_compiler_type) {
  return GetCompilerType().GetTypeInfo(pointee_or_element_compiler_type);
}

bool ValueObject::IsPointerType() { return GetCompilerType().IsPointerType(); }

bool ValueObject::IsArrayType() {
  return GetCompilerType().IsArrayType(NULL, NULL, NULL);
}

bool ValueObject::IsScalarType() { return GetCompilerType().IsScalarType(); }

bool ValueObject::IsIntegerType(bool &is_signed) {
  return GetCompilerType().IsIntegerType(is_signed);
}

bool ValueObject::IsPointerOrReferenceType() {
  return GetCompilerType().IsPointerOrReferenceType();
}

bool ValueObject::IsPossibleDynamicType() {
  ExecutionContext exe_ctx(GetExecutionContextRef());
  Process *process = exe_ctx.GetProcessPtr();
  if (process)
    return process->IsPossibleDynamicValue(*this);
  else
    return GetCompilerType().IsPossibleDynamicType(NULL, true, true);
}

bool ValueObject::IsRuntimeSupportValue() {
  Process *process(GetProcessSP().get());
  if (process) {
    LanguageRuntime *runtime =
        process->GetLanguageRuntime(GetObjectRuntimeLanguage());
    if (!runtime)
      runtime = process->GetObjCLanguageRuntime();
    if (runtime)
      return runtime->IsRuntimeSupportValue(*this);
  }
  return false;
}

bool ValueObject::IsNilReference() {
  if (Language *language = Language::FindPlugin(GetObjectRuntimeLanguage())) {
    return language->IsNilReference(*this);
  }
  return false;
}

bool ValueObject::IsUninitializedReference() {
  if (Language *language = Language::FindPlugin(GetObjectRuntimeLanguage())) {
    return language->IsUninitializedReference(*this);
  }
  return false;
}

// This allows you to create an array member using and index that doesn't not
// fall in the normal bounds of the array. Many times structure can be defined
// as: struct Collection {
//     uint32_t item_count;
//     Item item_array[0];
// };
// The size of the "item_array" is 1, but many times in practice there are more
// items in "item_array".

ValueObjectSP ValueObject::GetSyntheticArrayMember(size_t index,
                                                   bool can_create) {
  ValueObjectSP synthetic_child_sp;
  if (IsPointerType() || IsArrayType()) {
    char index_str[64];
    snprintf(index_str, sizeof(index_str), "[%" PRIu64 "]", (uint64_t)index);
    ConstString index_const_str(index_str);
    // Check if we have already created a synthetic array member in this valid
    // object. If we have we will re-use it.
    synthetic_child_sp = GetSyntheticChild(index_const_str);
    if (!synthetic_child_sp) {
      ValueObject *synthetic_child;
      // We haven't made a synthetic array member for INDEX yet, so lets make
      // one and cache it for any future reference.
      synthetic_child = CreateChildAtIndex(0, true, index);

      // Cache the value if we got one back...
      if (synthetic_child) {
        AddSyntheticChild(index_const_str, synthetic_child);
        synthetic_child_sp = synthetic_child->GetSP();
        synthetic_child_sp->SetName(ConstString(index_str));
        synthetic_child_sp->m_is_array_item_for_pointer = true;
      }
    }
  }
  return synthetic_child_sp;
}

ValueObjectSP ValueObject::GetSyntheticBitFieldChild(uint32_t from, uint32_t to,
                                                     bool can_create) {
  ValueObjectSP synthetic_child_sp;
  if (IsScalarType()) {
    char index_str[64];
    snprintf(index_str, sizeof(index_str), "[%i-%i]", from, to);
    ConstString index_const_str(index_str);
    // Check if we have already created a synthetic array member in this valid
    // object. If we have we will re-use it.
    synthetic_child_sp = GetSyntheticChild(index_const_str);
    if (!synthetic_child_sp) {
      uint32_t bit_field_size = to - from + 1;
      uint32_t bit_field_offset = from;
      if (GetDataExtractor().GetByteOrder() == eByteOrderBig)
        bit_field_offset =
            GetByteSize() * 8 - bit_field_size - bit_field_offset;
      // We haven't made a synthetic array member for INDEX yet, so lets make
      // one and cache it for any future reference.
      ValueObjectChild *synthetic_child = new ValueObjectChild(
          *this, GetCompilerType(), index_const_str, GetByteSize(), 0,
          bit_field_size, bit_field_offset, false, false, eAddressTypeInvalid,
          0);

      // Cache the value if we got one back...
      if (synthetic_child) {
        AddSyntheticChild(index_const_str, synthetic_child);
        synthetic_child_sp = synthetic_child->GetSP();
        synthetic_child_sp->SetName(ConstString(index_str));
        synthetic_child_sp->m_is_bitfield_for_scalar = true;
      }
    }
  }
  return synthetic_child_sp;
}

ValueObjectSP ValueObject::GetSyntheticChildAtOffset(
    uint32_t offset, const CompilerType &type, bool can_create,
    ConstString name_const_str) {

  ValueObjectSP synthetic_child_sp;

  if (name_const_str.IsEmpty()) {
    char name_str[64];
    snprintf(name_str, sizeof(name_str), "@%i", offset);
    name_const_str.SetCString(name_str);
  }

  // Check if we have already created a synthetic array member in this valid
  // object. If we have we will re-use it.
  synthetic_child_sp = GetSyntheticChild(name_const_str);

  if (synthetic_child_sp.get())
    return synthetic_child_sp;

  if (!can_create)
    return {};

  ExecutionContext exe_ctx(GetExecutionContextRef());
  llvm::Optional<uint64_t> size =
      type.GetByteSize(exe_ctx.GetBestExecutionContextScope());
  if (!size)
    return {};
  ValueObjectChild *synthetic_child =
      new ValueObjectChild(*this, type, name_const_str, *size, offset, 0, 0,
                           false, false, eAddressTypeInvalid, 0);
  if (synthetic_child) {
    AddSyntheticChild(name_const_str, synthetic_child);
    synthetic_child_sp = synthetic_child->GetSP();
    synthetic_child_sp->SetName(name_const_str);
    synthetic_child_sp->m_is_child_at_offset = true;
  }
  return synthetic_child_sp;
}

ValueObjectSP ValueObject::GetSyntheticBase(uint32_t offset,
                                            const CompilerType &type,
                                            bool can_create,
                                            ConstString name_const_str) {
  ValueObjectSP synthetic_child_sp;

  if (name_const_str.IsEmpty()) {
    char name_str[128];
    snprintf(name_str, sizeof(name_str), "base%s@%i",
             type.GetTypeName().AsCString("<unknown>"), offset);
    name_const_str.SetCString(name_str);
  }

  // Check if we have already created a synthetic array member in this valid
  // object. If we have we will re-use it.
  synthetic_child_sp = GetSyntheticChild(name_const_str);

  if (synthetic_child_sp.get())
    return synthetic_child_sp;

  if (!can_create)
    return {};

  const bool is_base_class = true;

  ExecutionContext exe_ctx(GetExecutionContextRef());
  llvm::Optional<uint64_t> size =
      type.GetByteSize(exe_ctx.GetBestExecutionContextScope());
  if (!size)
    return {};
  ValueObjectChild *synthetic_child =
      new ValueObjectChild(*this, type, name_const_str, *size, offset, 0, 0,
                           is_base_class, false, eAddressTypeInvalid, 0);
  if (synthetic_child) {
    AddSyntheticChild(name_const_str, synthetic_child);
    synthetic_child_sp = synthetic_child->GetSP();
    synthetic_child_sp->SetName(name_const_str);
  }
  return synthetic_child_sp;
}

// your expression path needs to have a leading . or -> (unless it somehow
// "looks like" an array, in which case it has a leading [ symbol). while the [
// is meaningful and should be shown to the user, . and -> are just parser
// design, but by no means added information for the user.. strip them off
static const char *SkipLeadingExpressionPathSeparators(const char *expression) {
  if (!expression || !expression[0])
    return expression;
  if (expression[0] == '.')
    return expression + 1;
  if (expression[0] == '-' && expression[1] == '>')
    return expression + 2;
  return expression;
}

ValueObjectSP
ValueObject::GetSyntheticExpressionPathChild(const char *expression,
                                             bool can_create) {
  ValueObjectSP synthetic_child_sp;
  ConstString name_const_string(expression);
  // Check if we have already created a synthetic array member in this valid
  // object. If we have we will re-use it.
  synthetic_child_sp = GetSyntheticChild(name_const_string);
  if (!synthetic_child_sp) {
    // We haven't made a synthetic array member for expression yet, so lets
    // make one and cache it for any future reference.
    synthetic_child_sp = GetValueForExpressionPath(
        expression, NULL, NULL,
        GetValueForExpressionPathOptions().SetSyntheticChildrenTraversal(
            GetValueForExpressionPathOptions::SyntheticChildrenTraversal::
                None));

    // Cache the value if we got one back...
    if (synthetic_child_sp.get()) {
      // FIXME: this causes a "real" child to end up with its name changed to
      // the contents of expression
      AddSyntheticChild(name_const_string, synthetic_child_sp.get());
      synthetic_child_sp->SetName(
          ConstString(SkipLeadingExpressionPathSeparators(expression)));
    }
  }
  return synthetic_child_sp;
}

void ValueObject::CalculateSyntheticValue(bool use_synthetic) {
  if (!use_synthetic)
    return;

  TargetSP target_sp(GetTargetSP());
  if (target_sp && !target_sp->GetEnableSyntheticValue()) {
    m_synthetic_value = NULL;
    return;
  }

  lldb::SyntheticChildrenSP current_synth_sp(m_synthetic_children_sp);

  if (!UpdateFormatsIfNeeded() && m_synthetic_value)
    return;

  if (m_synthetic_children_sp.get() == NULL)
    return;

  if (current_synth_sp == m_synthetic_children_sp && m_synthetic_value)
    return;

  m_synthetic_value = new ValueObjectSynthetic(*this, m_synthetic_children_sp);
}

void ValueObject::CalculateDynamicValue(DynamicValueType use_dynamic) {
  if (use_dynamic == eNoDynamicValues)
    return;

  if (!m_dynamic_value && !IsDynamic()) {
    ExecutionContext exe_ctx(GetExecutionContextRef());
    Process *process = exe_ctx.GetProcessPtr();
    if (process && process->IsPossibleDynamicValue(*this)) {
      ClearDynamicTypeInformation();
      m_dynamic_value = new ValueObjectDynamicValue(*this, use_dynamic);
    }
  }
}

ValueObjectSP ValueObject::GetDynamicValue(DynamicValueType use_dynamic) {
  if (use_dynamic == eNoDynamicValues)
    return ValueObjectSP();

  if (!IsDynamic() && m_dynamic_value == NULL) {
    CalculateDynamicValue(use_dynamic);
  }
  if (m_dynamic_value)
    return m_dynamic_value->GetSP();
  else
    return ValueObjectSP();
}

ValueObjectSP ValueObject::GetStaticValue() { return GetSP(); }

lldb::ValueObjectSP ValueObject::GetNonSyntheticValue() { return GetSP(); }

ValueObjectSP ValueObject::GetSyntheticValue(bool use_synthetic) {
  if (!use_synthetic)
    return ValueObjectSP();

  CalculateSyntheticValue(use_synthetic);

  if (m_synthetic_value)
    return m_synthetic_value->GetSP();
  else
    return ValueObjectSP();
}

bool ValueObject::HasSyntheticValue() {
  UpdateFormatsIfNeeded();

  if (m_synthetic_children_sp.get() == NULL)
    return false;

  CalculateSyntheticValue(true);

  return m_synthetic_value != nullptr;
}

bool ValueObject::GetBaseClassPath(Stream &s) {
  if (IsBaseClass()) {
    bool parent_had_base_class =
        GetParent() && GetParent()->GetBaseClassPath(s);
    CompilerType compiler_type = GetCompilerType();
    std::string cxx_class_name;
    bool this_had_base_class =
        ClangASTContext::GetCXXClassName(compiler_type, cxx_class_name);
    if (this_had_base_class) {
      if (parent_had_base_class)
        s.PutCString("::");
      s.PutCString(cxx_class_name);
    }
    return parent_had_base_class || this_had_base_class;
  }
  return false;
}

ValueObject *ValueObject::GetNonBaseClassParent() {
  if (GetParent()) {
    if (GetParent()->IsBaseClass())
      return GetParent()->GetNonBaseClassParent();
    else
      return GetParent();
  }
  return NULL;
}

bool ValueObject::IsBaseClass(uint32_t &depth) {
  if (!IsBaseClass()) {
    depth = 0;
    return false;
  }
  if (GetParent()) {
    GetParent()->IsBaseClass(depth);
    depth = depth + 1;
    return true;
  }
  // TODO: a base of no parent? weird..
  depth = 1;
  return true;
}

void ValueObject::GetExpressionPath(Stream &s, bool qualify_cxx_base_classes,
                                    GetExpressionPathFormat epformat) {
  // synthetic children do not actually "exist" as part of the hierarchy, and
  // sometimes they are consed up in ways that don't make sense from an
  // underlying language/API standpoint. So, use a special code path here to
  // return something that can hopefully be used in expression
  if (m_is_synthetic_children_generated) {
    UpdateValueIfNeeded();

    if (m_value.GetValueType() == Value::eValueTypeLoadAddress) {
      if (IsPointerOrReferenceType()) {
        s.Printf("((%s)0x%" PRIx64 ")", GetTypeName().AsCString("void"),
                 GetValueAsUnsigned(0));
        return;
      } else {
        uint64_t load_addr =
            m_value.GetScalar().ULongLong(LLDB_INVALID_ADDRESS);
        if (load_addr != LLDB_INVALID_ADDRESS) {
          s.Printf("(*( (%s *)0x%" PRIx64 "))", GetTypeName().AsCString("void"),
                   load_addr);
          return;
        }
      }
    }

    if (CanProvideValue()) {
      s.Printf("((%s)%s)", GetTypeName().AsCString("void"),
               GetValueAsCString());
      return;
    }

    return;
  }

  const bool is_deref_of_parent = IsDereferenceOfParent();

  if (is_deref_of_parent &&
      epformat == eGetExpressionPathFormatDereferencePointers) {
    // this is the original format of GetExpressionPath() producing code like
    // *(a_ptr).memberName, which is entirely fine, until you put this into
    // StackFrame::GetValueForVariableExpressionPath() which prefers to see
    // a_ptr->memberName. the eHonorPointers mode is meant to produce strings
    // in this latter format
    s.PutCString("*(");
  }

  ValueObject *parent = GetParent();

  if (parent)
    parent->GetExpressionPath(s, qualify_cxx_base_classes, epformat);

  // if we are a deref_of_parent just because we are synthetic array members
  // made up to allow ptr[%d] syntax to work in variable printing, then add our
  // name ([%d]) to the expression path
  if (m_is_array_item_for_pointer &&
      epformat == eGetExpressionPathFormatHonorPointers)
    s.PutCString(m_name.AsCString());

  if (!IsBaseClass()) {
    if (!is_deref_of_parent) {
      ValueObject *non_base_class_parent = GetNonBaseClassParent();
      if (non_base_class_parent &&
          !non_base_class_parent->GetName().IsEmpty()) {
        CompilerType non_base_class_parent_compiler_type =
            non_base_class_parent->GetCompilerType();
        if (non_base_class_parent_compiler_type) {
          if (parent && parent->IsDereferenceOfParent() &&
              epformat == eGetExpressionPathFormatHonorPointers) {
            s.PutCString("->");
          } else {
            const uint32_t non_base_class_parent_type_info =
                non_base_class_parent_compiler_type.GetTypeInfo();

            if (non_base_class_parent_type_info & eTypeIsPointer) {
              s.PutCString("->");
            } else if ((non_base_class_parent_type_info & eTypeHasChildren) &&
                       !(non_base_class_parent_type_info & eTypeIsArray)) {
              s.PutChar('.');
            }
          }
        }
      }

      const char *name = GetName().GetCString();
      if (name) {
        if (qualify_cxx_base_classes) {
          if (GetBaseClassPath(s))
            s.PutCString("::");
        }
        s.PutCString(name);
      }
    }
  }

  if (is_deref_of_parent &&
      epformat == eGetExpressionPathFormatDereferencePointers) {
    s.PutChar(')');
  }
}

ValueObjectSP ValueObject::GetValueForExpressionPath(
    llvm::StringRef expression, ExpressionPathScanEndReason *reason_to_stop,
    ExpressionPathEndResultType *final_value_type,
    const GetValueForExpressionPathOptions &options,
    ExpressionPathAftermath *final_task_on_target) {

  ExpressionPathScanEndReason dummy_reason_to_stop =
      ValueObject::eExpressionPathScanEndReasonUnknown;
  ExpressionPathEndResultType dummy_final_value_type =
      ValueObject::eExpressionPathEndResultTypeInvalid;
  ExpressionPathAftermath dummy_final_task_on_target =
      ValueObject::eExpressionPathAftermathNothing;

  ValueObjectSP ret_val = GetValueForExpressionPath_Impl(
      expression, reason_to_stop ? reason_to_stop : &dummy_reason_to_stop,
      final_value_type ? final_value_type : &dummy_final_value_type, options,
      final_task_on_target ? final_task_on_target
                           : &dummy_final_task_on_target);

  if (!final_task_on_target ||
      *final_task_on_target == ValueObject::eExpressionPathAftermathNothing)
    return ret_val;

  if (ret_val.get() &&
      ((final_value_type ? *final_value_type : dummy_final_value_type) ==
       eExpressionPathEndResultTypePlain)) // I can only deref and takeaddress
                                           // of plain objects
  {
    if ((final_task_on_target ? *final_task_on_target
                              : dummy_final_task_on_target) ==
        ValueObject::eExpressionPathAftermathDereference) {
      Status error;
      ValueObjectSP final_value = ret_val->Dereference(error);
      if (error.Fail() || !final_value.get()) {
        if (reason_to_stop)
          *reason_to_stop =
              ValueObject::eExpressionPathScanEndReasonDereferencingFailed;
        if (final_value_type)
          *final_value_type = ValueObject::eExpressionPathEndResultTypeInvalid;
        return ValueObjectSP();
      } else {
        if (final_task_on_target)
          *final_task_on_target = ValueObject::eExpressionPathAftermathNothing;
        return final_value;
      }
    }
    if (*final_task_on_target ==
        ValueObject::eExpressionPathAftermathTakeAddress) {
      Status error;
      ValueObjectSP final_value = ret_val->AddressOf(error);
      if (error.Fail() || !final_value.get()) {
        if (reason_to_stop)
          *reason_to_stop =
              ValueObject::eExpressionPathScanEndReasonTakingAddressFailed;
        if (final_value_type)
          *final_value_type = ValueObject::eExpressionPathEndResultTypeInvalid;
        return ValueObjectSP();
      } else {
        if (final_task_on_target)
          *final_task_on_target = ValueObject::eExpressionPathAftermathNothing;
        return final_value;
      }
    }
  }
  return ret_val; // final_task_on_target will still have its original value, so
                  // you know I did not do it
}

ValueObjectSP ValueObject::GetValueForExpressionPath_Impl(
    llvm::StringRef expression, ExpressionPathScanEndReason *reason_to_stop,
    ExpressionPathEndResultType *final_result,
    const GetValueForExpressionPathOptions &options,
    ExpressionPathAftermath *what_next) {
  ValueObjectSP root = GetSP();

  if (!root)
    return nullptr;

  llvm::StringRef remainder = expression;

  while (true) {
    llvm::StringRef temp_expression = remainder;

    CompilerType root_compiler_type = root->GetCompilerType();
    CompilerType pointee_compiler_type;
    Flags pointee_compiler_type_info;

    Flags root_compiler_type_info(
        root_compiler_type.GetTypeInfo(&pointee_compiler_type));
    if (pointee_compiler_type)
      pointee_compiler_type_info.Reset(pointee_compiler_type.GetTypeInfo());

    if (temp_expression.empty()) {
      *reason_to_stop = ValueObject::eExpressionPathScanEndReasonEndOfString;
      return root;
    }

    switch (temp_expression.front()) {
    case '-': {
      temp_expression = temp_expression.drop_front();
      if (options.m_check_dot_vs_arrow_syntax &&
          root_compiler_type_info.Test(eTypeIsPointer)) // if you are trying to
                                                        // use -> on a
                                                        // non-pointer and I
                                                        // must catch the error
      {
        *reason_to_stop =
            ValueObject::eExpressionPathScanEndReasonArrowInsteadOfDot;
        *final_result = ValueObject::eExpressionPathEndResultTypeInvalid;
        return ValueObjectSP();
      }
      if (root_compiler_type_info.Test(eTypeIsObjC) && // if yo are trying to
                                                       // extract an ObjC IVar
                                                       // when this is forbidden
          root_compiler_type_info.Test(eTypeIsPointer) &&
          options.m_no_fragile_ivar) {
        *reason_to_stop =
            ValueObject::eExpressionPathScanEndReasonFragileIVarNotAllowed;
        *final_result = ValueObject::eExpressionPathEndResultTypeInvalid;
        return ValueObjectSP();
      }
      if (!temp_expression.startswith(">")) {
        *reason_to_stop =
            ValueObject::eExpressionPathScanEndReasonUnexpectedSymbol;
        *final_result = ValueObject::eExpressionPathEndResultTypeInvalid;
        return ValueObjectSP();
      }
    }
      LLVM_FALLTHROUGH;
    case '.': // or fallthrough from ->
    {
      if (options.m_check_dot_vs_arrow_syntax &&
          temp_expression.front() == '.' &&
          root_compiler_type_info.Test(eTypeIsPointer)) // if you are trying to
                                                        // use . on a pointer
                                                        // and I must catch the
                                                        // error
      {
        *reason_to_stop =
            ValueObject::eExpressionPathScanEndReasonDotInsteadOfArrow;
        *final_result = ValueObject::eExpressionPathEndResultTypeInvalid;
        return nullptr;
      }
      temp_expression = temp_expression.drop_front(); // skip . or >

      size_t next_sep_pos = temp_expression.find_first_of("-.[", 1);
      ConstString child_name;
      if (next_sep_pos == llvm::StringRef::npos) // if no other separator just
                                                 // expand this last layer
      {
        child_name.SetString(temp_expression);
        ValueObjectSP child_valobj_sp =
            root->GetChildMemberWithName(child_name, true);

        if (child_valobj_sp.get()) // we know we are done, so just return
        {
          *reason_to_stop =
              ValueObject::eExpressionPathScanEndReasonEndOfString;
          *final_result = ValueObject::eExpressionPathEndResultTypePlain;
          return child_valobj_sp;
        } else {
          switch (options.m_synthetic_children_traversal) {
          case GetValueForExpressionPathOptions::SyntheticChildrenTraversal::
              None:
            break;
          case GetValueForExpressionPathOptions::SyntheticChildrenTraversal::
              FromSynthetic:
            if (root->IsSynthetic()) {
              child_valobj_sp = root->GetNonSyntheticValue();
              if (child_valobj_sp.get())
                child_valobj_sp =
                    child_valobj_sp->GetChildMemberWithName(child_name, true);
            }
            break;
          case GetValueForExpressionPathOptions::SyntheticChildrenTraversal::
              ToSynthetic:
            if (!root->IsSynthetic()) {
              child_valobj_sp = root->GetSyntheticValue();
              if (child_valobj_sp.get())
                child_valobj_sp =
                    child_valobj_sp->GetChildMemberWithName(child_name, true);
            }
            break;
          case GetValueForExpressionPathOptions::SyntheticChildrenTraversal::
              Both:
            if (root->IsSynthetic()) {
              child_valobj_sp = root->GetNonSyntheticValue();
              if (child_valobj_sp.get())
                child_valobj_sp =
                    child_valobj_sp->GetChildMemberWithName(child_name, true);
            } else {
              child_valobj_sp = root->GetSyntheticValue();
              if (child_valobj_sp.get())
                child_valobj_sp =
                    child_valobj_sp->GetChildMemberWithName(child_name, true);
            }
            break;
          }
        }

        // if we are here and options.m_no_synthetic_children is true,
        // child_valobj_sp is going to be a NULL SP, so we hit the "else"
        // branch, and return an error
        if (child_valobj_sp.get()) // if it worked, just return
        {
          *reason_to_stop =
              ValueObject::eExpressionPathScanEndReasonEndOfString;
          *final_result = ValueObject::eExpressionPathEndResultTypePlain;
          return child_valobj_sp;
        } else {
          *reason_to_stop =
              ValueObject::eExpressionPathScanEndReasonNoSuchChild;
          *final_result = ValueObject::eExpressionPathEndResultTypeInvalid;
          return nullptr;
        }
      } else // other layers do expand
      {
        llvm::StringRef next_separator = temp_expression.substr(next_sep_pos);

        child_name.SetString(temp_expression.slice(0, next_sep_pos));

        ValueObjectSP child_valobj_sp =
            root->GetChildMemberWithName(child_name, true);
        if (child_valobj_sp.get()) // store the new root and move on
        {
          root = child_valobj_sp;
          remainder = next_separator;
          *final_result = ValueObject::eExpressionPathEndResultTypePlain;
          continue;
        } else {
          switch (options.m_synthetic_children_traversal) {
          case GetValueForExpressionPathOptions::SyntheticChildrenTraversal::
              None:
            break;
          case GetValueForExpressionPathOptions::SyntheticChildrenTraversal::
              FromSynthetic:
            if (root->IsSynthetic()) {
              child_valobj_sp = root->GetNonSyntheticValue();
              if (child_valobj_sp.get())
                child_valobj_sp =
                    child_valobj_sp->GetChildMemberWithName(child_name, true);
            }
            break;
          case GetValueForExpressionPathOptions::SyntheticChildrenTraversal::
              ToSynthetic:
            if (!root->IsSynthetic()) {
              child_valobj_sp = root->GetSyntheticValue();
              if (child_valobj_sp.get())
                child_valobj_sp =
                    child_valobj_sp->GetChildMemberWithName(child_name, true);
            }
            break;
          case GetValueForExpressionPathOptions::SyntheticChildrenTraversal::
              Both:
            if (root->IsSynthetic()) {
              child_valobj_sp = root->GetNonSyntheticValue();
              if (child_valobj_sp.get())
                child_valobj_sp =
                    child_valobj_sp->GetChildMemberWithName(child_name, true);
            } else {
              child_valobj_sp = root->GetSyntheticValue();
              if (child_valobj_sp.get())
                child_valobj_sp =
                    child_valobj_sp->GetChildMemberWithName(child_name, true);
            }
            break;
          }
        }

        // if we are here and options.m_no_synthetic_children is true,
        // child_valobj_sp is going to be a NULL SP, so we hit the "else"
        // branch, and return an error
        if (child_valobj_sp.get()) // if it worked, move on
        {
          root = child_valobj_sp;
          remainder = next_separator;
          *final_result = ValueObject::eExpressionPathEndResultTypePlain;
          continue;
        } else {
          *reason_to_stop =
              ValueObject::eExpressionPathScanEndReasonNoSuchChild;
          *final_result = ValueObject::eExpressionPathEndResultTypeInvalid;
          return nullptr;
        }
      }
      break;
    }
    case '[': {
      if (!root_compiler_type_info.Test(eTypeIsArray) &&
          !root_compiler_type_info.Test(eTypeIsPointer) &&
          !root_compiler_type_info.Test(
              eTypeIsVector)) // if this is not a T[] nor a T*
      {
        if (!root_compiler_type_info.Test(
                eTypeIsScalar)) // if this is not even a scalar...
        {
          if (options.m_synthetic_children_traversal ==
              GetValueForExpressionPathOptions::SyntheticChildrenTraversal::
                  None) // ...only chance left is synthetic
          {
            *reason_to_stop =
                ValueObject::eExpressionPathScanEndReasonRangeOperatorInvalid;
            *final_result = ValueObject::eExpressionPathEndResultTypeInvalid;
            return ValueObjectSP();
          }
        } else if (!options.m_allow_bitfields_syntax) // if this is a scalar,
                                                      // check that we can
                                                      // expand bitfields
        {
          *reason_to_stop =
              ValueObject::eExpressionPathScanEndReasonRangeOperatorNotAllowed;
          *final_result = ValueObject::eExpressionPathEndResultTypeInvalid;
          return ValueObjectSP();
        }
      }
      if (temp_expression[1] ==
          ']') // if this is an unbounded range it only works for arrays
      {
        if (!root_compiler_type_info.Test(eTypeIsArray)) {
          *reason_to_stop =
              ValueObject::eExpressionPathScanEndReasonEmptyRangeNotAllowed;
          *final_result = ValueObject::eExpressionPathEndResultTypeInvalid;
          return nullptr;
        } else // even if something follows, we cannot expand unbounded ranges,
               // just let the caller do it
        {
          *reason_to_stop =
              ValueObject::eExpressionPathScanEndReasonArrayRangeOperatorMet;
          *final_result =
              ValueObject::eExpressionPathEndResultTypeUnboundedRange;
          return root;
        }
      }

      size_t close_bracket_position = temp_expression.find(']', 1);
      if (close_bracket_position ==
          llvm::StringRef::npos) // if there is no ], this is a syntax error
      {
        *reason_to_stop =
            ValueObject::eExpressionPathScanEndReasonUnexpectedSymbol;
        *final_result = ValueObject::eExpressionPathEndResultTypeInvalid;
        return nullptr;
      }

      llvm::StringRef bracket_expr =
          temp_expression.slice(1, close_bracket_position);

      // If this was an empty expression it would have been caught by the if
      // above.
      assert(!bracket_expr.empty());

      if (!bracket_expr.contains('-')) {
        // if no separator, this is of the form [N].  Note that this cannot be
        // an unbounded range of the form [], because that case was handled
        // above with an unconditional return.
        unsigned long index = 0;
        if (bracket_expr.getAsInteger(0, index)) {
          *reason_to_stop =
              ValueObject::eExpressionPathScanEndReasonUnexpectedSymbol;
          *final_result = ValueObject::eExpressionPathEndResultTypeInvalid;
          return nullptr;
        }

        // from here on we do have a valid index
        if (root_compiler_type_info.Test(eTypeIsArray)) {
          ValueObjectSP child_valobj_sp = root->GetChildAtIndex(index, true);
          if (!child_valobj_sp)
            child_valobj_sp = root->GetSyntheticArrayMember(index, true);
          if (!child_valobj_sp)
            if (root->HasSyntheticValue() &&
                root->GetSyntheticValue()->GetNumChildren() > index)
              child_valobj_sp =
                  root->GetSyntheticValue()->GetChildAtIndex(index, true);
          if (child_valobj_sp) {
            root = child_valobj_sp;
            remainder =
                temp_expression.substr(close_bracket_position + 1); // skip ]
            *final_result = ValueObject::eExpressionPathEndResultTypePlain;
            continue;
          } else {
            *reason_to_stop =
                ValueObject::eExpressionPathScanEndReasonNoSuchChild;
            *final_result = ValueObject::eExpressionPathEndResultTypeInvalid;
            return nullptr;
          }
        } else if (root_compiler_type_info.Test(eTypeIsPointer)) {
          if (*what_next ==
                  ValueObject::
                      eExpressionPathAftermathDereference && // if this is a
                                                             // ptr-to-scalar, I
                                                             // am accessing it
                                                             // by index and I
                                                             // would have
                                                             // deref'ed anyway,
                                                             // then do it now
                                                             // and use this as
                                                             // a bitfield
              pointee_compiler_type_info.Test(eTypeIsScalar)) {
            Status error;
            root = root->Dereference(error);
            if (error.Fail() || !root) {
              *reason_to_stop =
                  ValueObject::eExpressionPathScanEndReasonDereferencingFailed;
              *final_result = ValueObject::eExpressionPathEndResultTypeInvalid;
              return nullptr;
            } else {
              *what_next = eExpressionPathAftermathNothing;
              continue;
            }
          } else {
            if (root->GetCompilerType().GetMinimumLanguage() ==
                    eLanguageTypeObjC &&
                pointee_compiler_type_info.AllClear(eTypeIsPointer) &&
                root->HasSyntheticValue() &&
                (options.m_synthetic_children_traversal ==
                     GetValueForExpressionPathOptions::
                         SyntheticChildrenTraversal::ToSynthetic ||
                 options.m_synthetic_children_traversal ==
                     GetValueForExpressionPathOptions::
                         SyntheticChildrenTraversal::Both)) {
              root = root->GetSyntheticValue()->GetChildAtIndex(index, true);
            } else
              root = root->GetSyntheticArrayMember(index, true);
            if (!root) {
              *reason_to_stop =
                  ValueObject::eExpressionPathScanEndReasonNoSuchChild;
              *final_result = ValueObject::eExpressionPathEndResultTypeInvalid;
              return nullptr;
            } else {
              remainder =
                  temp_expression.substr(close_bracket_position + 1); // skip ]
              *final_result = ValueObject::eExpressionPathEndResultTypePlain;
              continue;
            }
          }
        } else if (root_compiler_type_info.Test(eTypeIsScalar)) {
          root = root->GetSyntheticBitFieldChild(index, index, true);
          if (!root) {
            *reason_to_stop =
                ValueObject::eExpressionPathScanEndReasonNoSuchChild;
            *final_result = ValueObject::eExpressionPathEndResultTypeInvalid;
            return nullptr;
          } else // we do not know how to expand members of bitfields, so we
                 // just return and let the caller do any further processing
          {
            *reason_to_stop = ValueObject::
                eExpressionPathScanEndReasonBitfieldRangeOperatorMet;
            *final_result = ValueObject::eExpressionPathEndResultTypeBitfield;
            return root;
          }
        } else if (root_compiler_type_info.Test(eTypeIsVector)) {
          root = root->GetChildAtIndex(index, true);
          if (!root) {
            *reason_to_stop =
                ValueObject::eExpressionPathScanEndReasonNoSuchChild;
            *final_result = ValueObject::eExpressionPathEndResultTypeInvalid;
            return ValueObjectSP();
          } else {
            remainder =
                temp_expression.substr(close_bracket_position + 1); // skip ]
            *final_result = ValueObject::eExpressionPathEndResultTypePlain;
            continue;
          }
        } else if (options.m_synthetic_children_traversal ==
                       GetValueForExpressionPathOptions::
                           SyntheticChildrenTraversal::ToSynthetic ||
                   options.m_synthetic_children_traversal ==
                       GetValueForExpressionPathOptions::
                           SyntheticChildrenTraversal::Both) {
          if (root->HasSyntheticValue())
            root = root->GetSyntheticValue();
          else if (!root->IsSynthetic()) {
            *reason_to_stop =
                ValueObject::eExpressionPathScanEndReasonSyntheticValueMissing;
            *final_result = ValueObject::eExpressionPathEndResultTypeInvalid;
            return nullptr;
          }
          // if we are here, then root itself is a synthetic VO.. should be
          // good to go

          if (!root) {
            *reason_to_stop =
                ValueObject::eExpressionPathScanEndReasonSyntheticValueMissing;
            *final_result = ValueObject::eExpressionPathEndResultTypeInvalid;
            return nullptr;
          }
          root = root->GetChildAtIndex(index, true);
          if (!root) {
            *reason_to_stop =
                ValueObject::eExpressionPathScanEndReasonNoSuchChild;
            *final_result = ValueObject::eExpressionPathEndResultTypeInvalid;
            return nullptr;
          } else {
            remainder =
                temp_expression.substr(close_bracket_position + 1); // skip ]
            *final_result = ValueObject::eExpressionPathEndResultTypePlain;
            continue;
          }
        } else {
          *reason_to_stop =
              ValueObject::eExpressionPathScanEndReasonNoSuchChild;
          *final_result = ValueObject::eExpressionPathEndResultTypeInvalid;
          return nullptr;
        }
      } else {
        // we have a low and a high index
        llvm::StringRef sleft, sright;
        unsigned long low_index, high_index;
        std::tie(sleft, sright) = bracket_expr.split('-');
        if (sleft.getAsInteger(0, low_index) ||
            sright.getAsInteger(0, high_index)) {
          *reason_to_stop =
              ValueObject::eExpressionPathScanEndReasonUnexpectedSymbol;
          *final_result = ValueObject::eExpressionPathEndResultTypeInvalid;
          return nullptr;
        }

        if (low_index > high_index) // swap indices if required
          std::swap(low_index, high_index);

        if (root_compiler_type_info.Test(
                eTypeIsScalar)) // expansion only works for scalars
        {
          root = root->GetSyntheticBitFieldChild(low_index, high_index, true);
          if (!root) {
            *reason_to_stop =
                ValueObject::eExpressionPathScanEndReasonNoSuchChild;
            *final_result = ValueObject::eExpressionPathEndResultTypeInvalid;
            return nullptr;
          } else {
            *reason_to_stop = ValueObject::
                eExpressionPathScanEndReasonBitfieldRangeOperatorMet;
            *final_result = ValueObject::eExpressionPathEndResultTypeBitfield;
            return root;
          }
        } else if (root_compiler_type_info.Test(
                       eTypeIsPointer) && // if this is a ptr-to-scalar, I am
                                          // accessing it by index and I would
                                          // have deref'ed anyway, then do it
                                          // now and use this as a bitfield
                   *what_next ==
                       ValueObject::eExpressionPathAftermathDereference &&
                   pointee_compiler_type_info.Test(eTypeIsScalar)) {
          Status error;
          root = root->Dereference(error);
          if (error.Fail() || !root) {
            *reason_to_stop =
                ValueObject::eExpressionPathScanEndReasonDereferencingFailed;
            *final_result = ValueObject::eExpressionPathEndResultTypeInvalid;
            return nullptr;
          } else {
            *what_next = ValueObject::eExpressionPathAftermathNothing;
            continue;
          }
        } else {
          *reason_to_stop =
              ValueObject::eExpressionPathScanEndReasonArrayRangeOperatorMet;
          *final_result = ValueObject::eExpressionPathEndResultTypeBoundedRange;
          return root;
        }
      }
      break;
    }
    default: // some non-separator is in the way
    {
      *reason_to_stop =
          ValueObject::eExpressionPathScanEndReasonUnexpectedSymbol;
      *final_result = ValueObject::eExpressionPathEndResultTypeInvalid;
      return nullptr;
    }
    }
  }
}

void ValueObject::LogValueObject(Log *log) {
  if (log)
    return LogValueObject(log, DumpValueObjectOptions(*this));
}

void ValueObject::LogValueObject(Log *log,
                                 const DumpValueObjectOptions &options) {
  if (log) {
    StreamString s;
    Dump(s, options);
    if (s.GetSize())
      log->PutCString(s.GetData());
  }
}

void ValueObject::Dump(Stream &s) { Dump(s, DumpValueObjectOptions(*this)); }

void ValueObject::Dump(Stream &s, const DumpValueObjectOptions &options) {
  ValueObjectPrinter printer(this, &s, options);
  printer.PrintValueObject();
}

ValueObjectSP ValueObject::CreateConstantValue(const ConstString &name) {
  ValueObjectSP valobj_sp;

  if (UpdateValueIfNeeded(false) && m_error.Success()) {
    ExecutionContext exe_ctx(GetExecutionContextRef());

    DataExtractor data;
    data.SetByteOrder(m_data.GetByteOrder());
    data.SetAddressByteSize(m_data.GetAddressByteSize());

    if (IsBitfield()) {
      Value v(Scalar(GetValueAsUnsigned(UINT64_MAX)));
      m_error = v.GetValueAsData(&exe_ctx, data, 0, GetModule().get());
    } else
      m_error = m_value.GetValueAsData(&exe_ctx, data, 0, GetModule().get());

    valobj_sp = ValueObjectConstResult::Create(
        exe_ctx.GetBestExecutionContextScope(), GetCompilerType(), name, data,
        GetAddressOf());
  }

  if (!valobj_sp) {
    ExecutionContext exe_ctx(GetExecutionContextRef());
    valobj_sp = ValueObjectConstResult::Create(
        exe_ctx.GetBestExecutionContextScope(), m_error);
  }
  return valobj_sp;
}

ValueObjectSP ValueObject::GetQualifiedRepresentationIfAvailable(
    lldb::DynamicValueType dynValue, bool synthValue) {
  ValueObjectSP result_sp(GetSP());

  switch (dynValue) {
  case lldb::eDynamicCanRunTarget:
  case lldb::eDynamicDontRunTarget: {
    if (!result_sp->IsDynamic()) {
      if (result_sp->GetDynamicValue(dynValue))
        result_sp = result_sp->GetDynamicValue(dynValue);
    }
  } break;
  case lldb::eNoDynamicValues: {
    if (result_sp->IsDynamic()) {
      if (result_sp->GetStaticValue())
        result_sp = result_sp->GetStaticValue();
    }
  } break;
  }

  if (synthValue) {
    if (!result_sp->IsSynthetic()) {
      if (result_sp->GetSyntheticValue())
        result_sp = result_sp->GetSyntheticValue();
    }
  } else {
    if (result_sp->IsSynthetic()) {
      if (result_sp->GetNonSyntheticValue())
        result_sp = result_sp->GetNonSyntheticValue();
    }
  }

  return result_sp;
}

ValueObjectSP ValueObject::Dereference(Status &error) {
  if (m_deref_valobj)
    return m_deref_valobj->GetSP();

  const bool is_pointer_or_reference_type = IsPointerOrReferenceType();
  if (is_pointer_or_reference_type) {
    bool omit_empty_base_classes = true;
    bool ignore_array_bounds = false;

    std::string child_name_str;
    uint32_t child_byte_size = 0;
    int32_t child_byte_offset = 0;
    uint32_t child_bitfield_bit_size = 0;
    uint32_t child_bitfield_bit_offset = 0;
    bool child_is_base_class = false;
    bool child_is_deref_of_parent = false;
    const bool transparent_pointers = false;
    CompilerType compiler_type = GetCompilerType();
    CompilerType child_compiler_type;
    uint64_t language_flags;

    ExecutionContext exe_ctx(GetExecutionContextRef());

    child_compiler_type = compiler_type.GetChildCompilerTypeAtIndex(
        &exe_ctx, 0, transparent_pointers, omit_empty_base_classes,
        ignore_array_bounds, child_name_str, child_byte_size, child_byte_offset,
        child_bitfield_bit_size, child_bitfield_bit_offset, child_is_base_class,
        child_is_deref_of_parent, this, language_flags);
    if (child_compiler_type && child_byte_size) {
      ConstString child_name;
      if (!child_name_str.empty())
        child_name.SetCString(child_name_str.c_str());

      m_deref_valobj = new ValueObjectChild(
          *this, child_compiler_type, child_name, child_byte_size,
          child_byte_offset, child_bitfield_bit_size, child_bitfield_bit_offset,
          child_is_base_class, child_is_deref_of_parent, eAddressTypeInvalid,
          language_flags);
    }
  } else if (HasSyntheticValue()) {
    m_deref_valobj =
        GetSyntheticValue()
            ->GetChildMemberWithName(ConstString("$$dereference$$"), true)
            .get();
  }

  if (m_deref_valobj) {
    error.Clear();
    return m_deref_valobj->GetSP();
  } else {
    StreamString strm;
    GetExpressionPath(strm, true);

    if (is_pointer_or_reference_type)
      error.SetErrorStringWithFormat("dereference failed: (%s) %s",
                                     GetTypeName().AsCString("<invalid type>"),
                                     strm.GetData());
    else
      error.SetErrorStringWithFormat("not a pointer or reference type: (%s) %s",
                                     GetTypeName().AsCString("<invalid type>"),
                                     strm.GetData());
    return ValueObjectSP();
  }
}

ValueObjectSP ValueObject::AddressOf(Status &error) {
  if (m_addr_of_valobj_sp)
    return m_addr_of_valobj_sp;

  AddressType address_type = eAddressTypeInvalid;
  const bool scalar_is_load_address = false;
  addr_t addr = GetAddressOf(scalar_is_load_address, &address_type);
  error.Clear();
  if (addr != LLDB_INVALID_ADDRESS && address_type != eAddressTypeHost) {
    switch (address_type) {
    case eAddressTypeInvalid: {
      StreamString expr_path_strm;
      GetExpressionPath(expr_path_strm, true);
      error.SetErrorStringWithFormat("'%s' is not in memory",
                                     expr_path_strm.GetData());
    } break;

    case eAddressTypeFile:
    case eAddressTypeLoad: {
      CompilerType compiler_type = GetCompilerType();
      if (compiler_type) {
        std::string name(1, '&');
        name.append(m_name.AsCString(""));
        ExecutionContext exe_ctx(GetExecutionContextRef());
        m_addr_of_valobj_sp = ValueObjectConstResult::Create(
            exe_ctx.GetBestExecutionContextScope(),
            compiler_type.GetPointerType(), ConstString(name.c_str()), addr,
            eAddressTypeInvalid, m_data.GetAddressByteSize());
      }
    } break;
    default:
      break;
    }
  } else {
    StreamString expr_path_strm;
    GetExpressionPath(expr_path_strm, true);
    error.SetErrorStringWithFormat("'%s' doesn't have a valid address",
                                   expr_path_strm.GetData());
  }

  return m_addr_of_valobj_sp;
}

ValueObjectSP ValueObject::Cast(const CompilerType &compiler_type) {
  return ValueObjectCast::Create(*this, GetName(), compiler_type);
}

lldb::ValueObjectSP ValueObject::Clone(const ConstString &new_name) {
  return ValueObjectCast::Create(*this, new_name, GetCompilerType());
}

ValueObjectSP ValueObject::CastPointerType(const char *name,
                                           CompilerType &compiler_type) {
  ValueObjectSP valobj_sp;
  AddressType address_type;
  addr_t ptr_value = GetPointerValue(&address_type);

  if (ptr_value != LLDB_INVALID_ADDRESS) {
    Address ptr_addr(ptr_value);
    ExecutionContext exe_ctx(GetExecutionContextRef());
    valobj_sp = ValueObjectMemory::Create(
        exe_ctx.GetBestExecutionContextScope(), name, ptr_addr, compiler_type);
  }
  return valobj_sp;
}

ValueObjectSP ValueObject::CastPointerType(const char *name, TypeSP &type_sp) {
  ValueObjectSP valobj_sp;
  AddressType address_type;
  addr_t ptr_value = GetPointerValue(&address_type);

  if (ptr_value != LLDB_INVALID_ADDRESS) {
    Address ptr_addr(ptr_value);
    ExecutionContext exe_ctx(GetExecutionContextRef());
    valobj_sp = ValueObjectMemory::Create(
        exe_ctx.GetBestExecutionContextScope(), name, ptr_addr, type_sp);
  }
  return valobj_sp;
}

ValueObject::EvaluationPoint::EvaluationPoint()
    : m_mod_id(), m_exe_ctx_ref(), m_needs_update(true) {}

ValueObject::EvaluationPoint::EvaluationPoint(ExecutionContextScope *exe_scope,
                                              bool use_selected)
    : m_mod_id(), m_exe_ctx_ref(), m_needs_update(true) {
  ExecutionContext exe_ctx(exe_scope);
  TargetSP target_sp(exe_ctx.GetTargetSP());
  if (target_sp) {
    m_exe_ctx_ref.SetTargetSP(target_sp);
    ProcessSP process_sp(exe_ctx.GetProcessSP());
    if (!process_sp)
      process_sp = target_sp->GetProcessSP();

    if (process_sp) {
      m_mod_id = process_sp->GetModID();
      m_exe_ctx_ref.SetProcessSP(process_sp);

      ThreadSP thread_sp(exe_ctx.GetThreadSP());

      if (!thread_sp) {
        if (use_selected)
          thread_sp = process_sp->GetThreadList().GetSelectedThread();
      }

      if (thread_sp) {
        m_exe_ctx_ref.SetThreadSP(thread_sp);

        StackFrameSP frame_sp(exe_ctx.GetFrameSP());
        if (!frame_sp) {
          if (use_selected)
            frame_sp = thread_sp->GetSelectedFrame();
        }
        if (frame_sp)
          m_exe_ctx_ref.SetFrameSP(frame_sp);
      }
    }
  }
}

ValueObject::EvaluationPoint::EvaluationPoint(
    const ValueObject::EvaluationPoint &rhs)
    : m_mod_id(), m_exe_ctx_ref(rhs.m_exe_ctx_ref), m_needs_update(true) {}

ValueObject::EvaluationPoint::~EvaluationPoint() {}

// This function checks the EvaluationPoint against the current process state.
// If the current state matches the evaluation point, or the evaluation point
// is already invalid, then we return false, meaning "no change".  If the
// current state is different, we update our state, and return true meaning
// "yes, change".  If we did see a change, we also set m_needs_update to true,
// so future calls to NeedsUpdate will return true. exe_scope will be set to
// the current execution context scope.

bool ValueObject::EvaluationPoint::SyncWithProcessState(
    bool accept_invalid_exe_ctx) {
  // Start with the target, if it is NULL, then we're obviously not going to
  // get any further:
  const bool thread_and_frame_only_if_stopped = true;
  ExecutionContext exe_ctx(
      m_exe_ctx_ref.Lock(thread_and_frame_only_if_stopped));

  if (exe_ctx.GetTargetPtr() == NULL)
    return false;

  // If we don't have a process nothing can change.
  Process *process = exe_ctx.GetProcessPtr();
  if (process == NULL)
    return false;

  // If our stop id is the current stop ID, nothing has changed:
  ProcessModID current_mod_id = process->GetModID();

  // If the current stop id is 0, either we haven't run yet, or the process
  // state has been cleared. In either case, we aren't going to be able to sync
  // with the process state.
  if (current_mod_id.GetStopID() == 0)
    return false;

  bool changed = false;
  const bool was_valid = m_mod_id.IsValid();
  if (was_valid) {
    if (m_mod_id == current_mod_id) {
      // Everything is already up to date in this object, no need to update the
      // execution context scope.
      changed = false;
    } else {
      m_mod_id = current_mod_id;
      m_needs_update = true;
      changed = true;
    }
  }

  // Now re-look up the thread and frame in case the underlying objects have
  // gone away & been recreated. That way we'll be sure to return a valid
  // exe_scope. If we used to have a thread or a frame but can't find it
  // anymore, then mark ourselves as invalid.

  if (!accept_invalid_exe_ctx) {
    if (m_exe_ctx_ref.HasThreadRef()) {
      ThreadSP thread_sp(m_exe_ctx_ref.GetThreadSP());
      if (thread_sp) {
        if (m_exe_ctx_ref.HasFrameRef()) {
          StackFrameSP frame_sp(m_exe_ctx_ref.GetFrameSP());
          if (!frame_sp) {
            // We used to have a frame, but now it is gone
            SetInvalid();
            changed = was_valid;
          }
        }
      } else {
        // We used to have a thread, but now it is gone
        SetInvalid();
        changed = was_valid;
      }
    }
  }

  return changed;
}

void ValueObject::EvaluationPoint::SetUpdated() {
  ProcessSP process_sp(m_exe_ctx_ref.GetProcessSP());
  if (process_sp)
    m_mod_id = process_sp->GetModID();
  m_needs_update = false;
}

void ValueObject::ClearUserVisibleData(uint32_t clear_mask) {
  if ((clear_mask & eClearUserVisibleDataItemsValue) ==
      eClearUserVisibleDataItemsValue)
    m_value_str.clear();

  if ((clear_mask & eClearUserVisibleDataItemsLocation) ==
      eClearUserVisibleDataItemsLocation)
    m_location_str.clear();

  if ((clear_mask & eClearUserVisibleDataItemsSummary) ==
      eClearUserVisibleDataItemsSummary)
    m_summary_str.clear();

  if ((clear_mask & eClearUserVisibleDataItemsDescription) ==
      eClearUserVisibleDataItemsDescription)
    m_object_desc_str.clear();

  if ((clear_mask & eClearUserVisibleDataItemsSyntheticChildren) ==
      eClearUserVisibleDataItemsSyntheticChildren) {
    if (m_synthetic_value)
      m_synthetic_value = NULL;
  }

  if ((clear_mask & eClearUserVisibleDataItemsValidator) ==
      eClearUserVisibleDataItemsValidator)
    m_validation_result.reset();
}

SymbolContextScope *ValueObject::GetSymbolContextScope() {
  if (m_parent) {
    if (!m_parent->IsPointerOrReferenceType())
      return m_parent->GetSymbolContextScope();
  }
  return NULL;
}

lldb::ValueObjectSP
ValueObject::CreateValueObjectFromExpression(llvm::StringRef name,
                                             llvm::StringRef expression,
                                             const ExecutionContext &exe_ctx) {
  return CreateValueObjectFromExpression(name, expression, exe_ctx,
                                         EvaluateExpressionOptions());
}

lldb::ValueObjectSP ValueObject::CreateValueObjectFromExpression(
    llvm::StringRef name, llvm::StringRef expression,
    const ExecutionContext &exe_ctx, const EvaluateExpressionOptions &options) {
  lldb::ValueObjectSP retval_sp;
  lldb::TargetSP target_sp(exe_ctx.GetTargetSP());
  if (!target_sp)
    return retval_sp;
  if (expression.empty())
    return retval_sp;
  target_sp->EvaluateExpression(expression, exe_ctx.GetFrameSP().get(),
                                retval_sp, options);
  if (retval_sp && !name.empty())
    retval_sp->SetName(ConstString(name));
  return retval_sp;
}

lldb::ValueObjectSP ValueObject::CreateValueObjectFromAddress(
    llvm::StringRef name, uint64_t address, const ExecutionContext &exe_ctx,
    CompilerType type) {
  if (type) {
    CompilerType pointer_type(type.GetPointerType());
    if (pointer_type) {
      lldb::DataBufferSP buffer(
          new lldb_private::DataBufferHeap(&address, sizeof(lldb::addr_t)));
      lldb::ValueObjectSP ptr_result_valobj_sp(ValueObjectConstResult::Create(
          exe_ctx.GetBestExecutionContextScope(), pointer_type,
          ConstString(name), buffer, exe_ctx.GetByteOrder(),
          exe_ctx.GetAddressByteSize()));
      if (ptr_result_valobj_sp) {
        ptr_result_valobj_sp->GetValue().SetValueType(
            Value::eValueTypeLoadAddress);
        Status err;
        ptr_result_valobj_sp = ptr_result_valobj_sp->Dereference(err);
        if (ptr_result_valobj_sp && !name.empty())
          ptr_result_valobj_sp->SetName(ConstString(name));
      }
      return ptr_result_valobj_sp;
    }
  }
  return lldb::ValueObjectSP();
}

lldb::ValueObjectSP ValueObject::CreateValueObjectFromData(
    llvm::StringRef name, const DataExtractor &data,
    const ExecutionContext &exe_ctx, CompilerType type) {
  lldb::ValueObjectSP new_value_sp;
  new_value_sp = ValueObjectConstResult::Create(
      exe_ctx.GetBestExecutionContextScope(), type, ConstString(name), data,
      LLDB_INVALID_ADDRESS);
  new_value_sp->SetAddressTypeOfChildren(eAddressTypeLoad);
  if (new_value_sp && !name.empty())
    new_value_sp->SetName(ConstString(name));
  return new_value_sp;
}

ModuleSP ValueObject::GetModule() {
  ValueObject *root(GetRoot());
  if (root != this)
    return root->GetModule();
  return lldb::ModuleSP();
}

ValueObject *ValueObject::GetRoot() {
  if (m_root)
    return m_root;
  return (m_root = FollowParentChain([](ValueObject *vo) -> bool {
            return (vo->m_parent != nullptr);
          }));
}

ValueObject *
ValueObject::FollowParentChain(std::function<bool(ValueObject *)> f) {
  ValueObject *vo = this;
  while (vo) {
    if (!f(vo))
      break;
    vo = vo->m_parent;
  }
  return vo;
}

AddressType ValueObject::GetAddressTypeOfChildren() {
  if (m_address_type_of_ptr_or_ref_children == eAddressTypeInvalid) {
    ValueObject *root(GetRoot());
    if (root != this)
      return root->GetAddressTypeOfChildren();
  }
  return m_address_type_of_ptr_or_ref_children;
}

lldb::DynamicValueType ValueObject::GetDynamicValueType() {
  ValueObject *with_dv_info = this;
  while (with_dv_info) {
    if (with_dv_info->HasDynamicValueTypeInfo())
      return with_dv_info->GetDynamicValueTypeImpl();
    with_dv_info = with_dv_info->m_parent;
  }
  return lldb::eNoDynamicValues;
}

lldb::Format ValueObject::GetFormat() const {
  const ValueObject *with_fmt_info = this;
  while (with_fmt_info) {
    if (with_fmt_info->m_format != lldb::eFormatDefault)
      return with_fmt_info->m_format;
    with_fmt_info = with_fmt_info->m_parent;
  }
  return m_format;
}

lldb::LanguageType ValueObject::GetPreferredDisplayLanguage() {
  lldb::LanguageType type = m_preferred_display_language;
  if (m_preferred_display_language == lldb::eLanguageTypeUnknown) {
    if (GetRoot()) {
      if (GetRoot() == this) {
        if (StackFrameSP frame_sp = GetFrameSP()) {
          const SymbolContext &sc(
              frame_sp->GetSymbolContext(eSymbolContextCompUnit));
          if (CompileUnit *cu = sc.comp_unit)
            type = cu->GetLanguage();
        }
      } else {
        type = GetRoot()->GetPreferredDisplayLanguage();
      }
    }
  }
  return (m_preferred_display_language = type); // only compute it once
}

void ValueObject::SetPreferredDisplayLanguage(lldb::LanguageType lt) {
  m_preferred_display_language = lt;
}

void ValueObject::SetPreferredDisplayLanguageIfNeeded(lldb::LanguageType lt) {
  if (m_preferred_display_language == lldb::eLanguageTypeUnknown)
    SetPreferredDisplayLanguage(lt);
}

bool ValueObject::CanProvideValue() {
  // we need to support invalid types as providers of values because some bare-
  // board debugging scenarios have no notion of types, but still manage to
  // have raw numeric values for things like registers. sigh.
  const CompilerType &type(GetCompilerType());
  return (!type.IsValid()) || (0 != (type.GetTypeInfo() & eTypeHasValue));
}

bool ValueObject::IsChecksumEmpty() { return m_value_checksum.empty(); }

ValueObjectSP ValueObject::Persist() {
  if (!UpdateValueIfNeeded())
    return nullptr;

  TargetSP target_sp(GetTargetSP());
  if (!target_sp)
    return nullptr;

  PersistentExpressionState *persistent_state =
      target_sp->GetPersistentExpressionStateForLanguage(
          GetPreferredDisplayLanguage());

  if (!persistent_state)
    return nullptr;

  auto prefix = persistent_state->GetPersistentVariablePrefix();
  ConstString name =
      persistent_state->GetNextPersistentVariableName(*target_sp, prefix);

  ValueObjectSP const_result_sp =
      ValueObjectConstResult::Create(target_sp.get(), GetValue(), name);

  ExpressionVariableSP clang_var_sp =
      persistent_state->CreatePersistentVariable(const_result_sp);
  clang_var_sp->m_live_sp = clang_var_sp->m_frozen_sp;
  clang_var_sp->m_flags |= ExpressionVariable::EVIsProgramReference;

  return clang_var_sp->GetValueObject();
}

bool ValueObject::IsSyntheticChildrenGenerated() {
  return m_is_synthetic_children_generated;
}

void ValueObject::SetSyntheticChildrenGenerated(bool b) {
  m_is_synthetic_children_generated = b;
}

uint64_t ValueObject::GetLanguageFlags() { return m_language_flags; }

void ValueObject::SetLanguageFlags(uint64_t flags) { m_language_flags = flags; }

ValueObjectManager::ValueObjectManager(lldb::ValueObjectSP in_valobj_sp,
                                       lldb::DynamicValueType use_dynamic,
                                       bool use_synthetic) : m_root_valobj_sp(),
    m_user_valobj_sp(), m_use_dynamic(use_dynamic), m_stop_id(UINT32_MAX),
    m_use_synthetic(use_synthetic) {
  if (!in_valobj_sp)
    return;
  // If the user passes in a value object that is dynamic or synthetic, then
  // water it down to the static type.
  m_root_valobj_sp = in_valobj_sp->GetQualifiedRepresentationIfAvailable(lldb::eNoDynamicValues, false);
}

bool ValueObjectManager::IsValid() const {
  if (!m_root_valobj_sp)
    return false;
  lldb::TargetSP target_sp = GetTargetSP();
  if (target_sp)
    return target_sp->IsValid();
  return false;
}

lldb::ValueObjectSP ValueObjectManager::GetSP() {
  lldb::ProcessSP process_sp = GetProcessSP();
  if (!process_sp)
    return lldb::ValueObjectSP();
  
  const uint32_t current_stop_id = process_sp->GetLastNaturalStopID();
  if (current_stop_id == m_stop_id)
    return m_user_valobj_sp;
  
  m_stop_id = current_stop_id;
  
  if (!m_root_valobj_sp) {
    m_user_valobj_sp.reset();
    return m_root_valobj_sp;
  }
  
  m_user_valobj_sp = m_root_valobj_sp;
  
  if (m_use_dynamic != lldb::eNoDynamicValues) {
    lldb::ValueObjectSP dynamic_sp = m_user_valobj_sp->GetDynamicValue(m_use_dynamic);
    if (dynamic_sp)
      m_user_valobj_sp = dynamic_sp;
  }
  
  if (m_use_synthetic) {
    lldb::ValueObjectSP synthetic_sp = m_user_valobj_sp->GetSyntheticValue(m_use_synthetic);
    if (synthetic_sp)
      m_user_valobj_sp = synthetic_sp;
  }
  
  return m_user_valobj_sp;
}

void ValueObjectManager::SetUseDynamic(lldb::DynamicValueType use_dynamic) {
  if (use_dynamic != m_use_dynamic) {
    m_use_dynamic = use_dynamic;
    m_user_valobj_sp.reset();
    m_stop_id = UINT32_MAX;
  }
}

void ValueObjectManager::SetUseSynthetic(bool use_synthetic) {
  if (m_use_synthetic != use_synthetic) {
    m_use_synthetic = use_synthetic;
    m_user_valobj_sp.reset();
    m_stop_id = UINT32_MAX;
  }
}

lldb::TargetSP ValueObjectManager::GetTargetSP() const {
  if (!m_root_valobj_sp)
    return m_root_valobj_sp->GetTargetSP();
  return lldb::TargetSP();
}

lldb::ProcessSP ValueObjectManager::GetProcessSP() const {
  if (m_root_valobj_sp)
    return m_root_valobj_sp->GetProcessSP();
  return lldb::ProcessSP();
}

lldb::ThreadSP ValueObjectManager::GetThreadSP() const {
  if (m_root_valobj_sp)
    return m_root_valobj_sp->GetThreadSP();
  return lldb::ThreadSP();
}

lldb::StackFrameSP ValueObjectManager::GetFrameSP() const {
  if (m_root_valobj_sp)
    return m_root_valobj_sp->GetFrameSP();
  return lldb::StackFrameSP();
}

