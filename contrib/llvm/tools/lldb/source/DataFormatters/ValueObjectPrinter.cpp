//===-- ValueObjectPrinter.cpp -----------------------------------*- C++-*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "lldb/DataFormatters/ValueObjectPrinter.h"

#include "lldb/Core/ValueObject.h"
#include "lldb/DataFormatters/DataVisualization.h"
#include "lldb/Interpreter/CommandInterpreter.h"
#include "lldb/Target/Language.h"
#include "lldb/Target/Target.h"
#include "lldb/Utility/Stream.h"

using namespace lldb;
using namespace lldb_private;

ValueObjectPrinter::ValueObjectPrinter(ValueObject *valobj, Stream *s) {
  if (valobj) {
    DumpValueObjectOptions options(*valobj);
    Init(valobj, s, options, m_options.m_max_ptr_depth, 0, nullptr);
  } else {
    DumpValueObjectOptions options;
    Init(valobj, s, options, m_options.m_max_ptr_depth, 0, nullptr);
  }
}

ValueObjectPrinter::ValueObjectPrinter(ValueObject *valobj, Stream *s,
                                       const DumpValueObjectOptions &options) {
  Init(valobj, s, options, m_options.m_max_ptr_depth, 0, nullptr);
}

ValueObjectPrinter::ValueObjectPrinter(
    ValueObject *valobj, Stream *s, const DumpValueObjectOptions &options,
    const DumpValueObjectOptions::PointerDepth &ptr_depth, uint32_t curr_depth,
    InstancePointersSetSP printed_instance_pointers) {
  Init(valobj, s, options, ptr_depth, curr_depth, printed_instance_pointers);
}

void ValueObjectPrinter::Init(
    ValueObject *valobj, Stream *s, const DumpValueObjectOptions &options,
    const DumpValueObjectOptions::PointerDepth &ptr_depth, uint32_t curr_depth,
    InstancePointersSetSP printed_instance_pointers) {
  m_orig_valobj = valobj;
  m_valobj = nullptr;
  m_stream = s;
  m_options = options;
  m_ptr_depth = ptr_depth;
  m_curr_depth = curr_depth;
  assert(m_orig_valobj && "cannot print a NULL ValueObject");
  assert(m_stream && "cannot print to a NULL Stream");
  m_should_print = eLazyBoolCalculate;
  m_is_nil = eLazyBoolCalculate;
  m_is_uninit = eLazyBoolCalculate;
  m_is_ptr = eLazyBoolCalculate;
  m_is_ref = eLazyBoolCalculate;
  m_is_aggregate = eLazyBoolCalculate;
  m_is_instance_ptr = eLazyBoolCalculate;
  m_summary_formatter = {nullptr, false};
  m_value.assign("");
  m_summary.assign("");
  m_error.assign("");
  m_val_summary_ok = false;
  m_printed_instance_pointers =
      printed_instance_pointers
          ? printed_instance_pointers
          : InstancePointersSetSP(new InstancePointersSet());
}

bool ValueObjectPrinter::PrintValueObject() {
  if (!GetMostSpecializedValue() || m_valobj == nullptr)
    return false;

  if (ShouldPrintValueObject()) {
    PrintValidationMarkerIfNeeded();

    PrintLocationIfNeeded();
    m_stream->Indent();

    PrintDecl();
  }

  bool value_printed = false;
  bool summary_printed = false;

  m_val_summary_ok =
      PrintValueAndSummaryIfNeeded(value_printed, summary_printed);

  if (m_val_summary_ok)
    PrintChildrenIfNeeded(value_printed, summary_printed);
  else
    m_stream->EOL();

  PrintValidationErrorIfNeeded();

  return true;
}

bool ValueObjectPrinter::GetMostSpecializedValue() {
  if (m_valobj)
    return true;
  bool update_success = m_orig_valobj->UpdateValueIfNeeded(true);
  if (!update_success) {
    m_valobj = m_orig_valobj;
  } else {
    if (m_orig_valobj->IsDynamic()) {
      if (m_options.m_use_dynamic == eNoDynamicValues) {
        ValueObject *static_value = m_orig_valobj->GetStaticValue().get();
        if (static_value)
          m_valobj = static_value;
        else
          m_valobj = m_orig_valobj;
      } else
        m_valobj = m_orig_valobj;
    } else {
      if (m_options.m_use_dynamic != eNoDynamicValues) {
        ValueObject *dynamic_value =
            m_orig_valobj->GetDynamicValue(m_options.m_use_dynamic).get();
        if (dynamic_value)
          m_valobj = dynamic_value;
        else
          m_valobj = m_orig_valobj;
      } else
        m_valobj = m_orig_valobj;
    }

    if (m_valobj->IsSynthetic()) {
      if (!m_options.m_use_synthetic) {
        ValueObject *non_synthetic = m_valobj->GetNonSyntheticValue().get();
        if (non_synthetic)
          m_valobj = non_synthetic;
      }
    } else {
      if (m_options.m_use_synthetic) {
        ValueObject *synthetic = m_valobj->GetSyntheticValue().get();
        if (synthetic)
          m_valobj = synthetic;
      }
    }
  }
  m_compiler_type = m_valobj->GetCompilerType();
  m_type_flags = m_compiler_type.GetTypeInfo();
  return true;
}

const char *ValueObjectPrinter::GetDescriptionForDisplay() {
  const char *str = m_valobj->GetObjectDescription();
  if (!str)
    str = m_valobj->GetSummaryAsCString();
  if (!str)
    str = m_valobj->GetValueAsCString();
  return str;
}

const char *ValueObjectPrinter::GetRootNameForDisplay(const char *if_fail) {
  const char *root_valobj_name = m_options.m_root_valobj_name.empty()
                                     ? m_valobj->GetName().AsCString()
                                     : m_options.m_root_valobj_name.c_str();
  return root_valobj_name ? root_valobj_name : if_fail;
}

bool ValueObjectPrinter::ShouldPrintValueObject() {
  if (m_should_print == eLazyBoolCalculate)
    m_should_print =
        (!m_options.m_flat_output || m_type_flags.Test(eTypeHasValue))
            ? eLazyBoolYes
            : eLazyBoolNo;
  return m_should_print == eLazyBoolYes;
}

bool ValueObjectPrinter::IsNil() {
  if (m_is_nil == eLazyBoolCalculate)
    m_is_nil = m_valobj->IsNilReference() ? eLazyBoolYes : eLazyBoolNo;
  return m_is_nil == eLazyBoolYes;
}

bool ValueObjectPrinter::IsUninitialized() {
  if (m_is_uninit == eLazyBoolCalculate)
    m_is_uninit =
        m_valobj->IsUninitializedReference() ? eLazyBoolYes : eLazyBoolNo;
  return m_is_uninit == eLazyBoolYes;
}

bool ValueObjectPrinter::IsPtr() {
  if (m_is_ptr == eLazyBoolCalculate)
    m_is_ptr = m_type_flags.Test(eTypeIsPointer) ? eLazyBoolYes : eLazyBoolNo;
  return m_is_ptr == eLazyBoolYes;
}

bool ValueObjectPrinter::IsRef() {
  if (m_is_ref == eLazyBoolCalculate)
    m_is_ref = m_type_flags.Test(eTypeIsReference) ? eLazyBoolYes : eLazyBoolNo;
  return m_is_ref == eLazyBoolYes;
}

bool ValueObjectPrinter::IsAggregate() {
  if (m_is_aggregate == eLazyBoolCalculate)
    m_is_aggregate =
        m_type_flags.Test(eTypeHasChildren) ? eLazyBoolYes : eLazyBoolNo;
  return m_is_aggregate == eLazyBoolYes;
}

bool ValueObjectPrinter::IsInstancePointer() {
  // you need to do this check on the value's clang type
  if (m_is_instance_ptr == eLazyBoolCalculate)
    m_is_instance_ptr = (m_valobj->GetValue().GetCompilerType().GetTypeInfo() &
                         eTypeInstanceIsPointer) != 0
                            ? eLazyBoolYes
                            : eLazyBoolNo;
  if ((eLazyBoolYes == m_is_instance_ptr) && m_valobj->IsBaseClass())
    m_is_instance_ptr = eLazyBoolNo;
  return m_is_instance_ptr == eLazyBoolYes;
}

bool ValueObjectPrinter::PrintLocationIfNeeded() {
  if (m_options.m_show_location) {
    m_stream->Printf("%s: ", m_valobj->GetLocationAsCString());
    return true;
  }
  return false;
}

void ValueObjectPrinter::PrintDecl() {
  bool show_type = true;
  // if we are at the root-level and been asked to hide the root's type, then
  // hide it
  if (m_curr_depth == 0 && m_options.m_hide_root_type)
    show_type = false;
  else
    // otherwise decide according to the usual rules (asked to show types -
    // always at the root level)
    show_type = m_options.m_show_types ||
                (m_curr_depth == 0 && !m_options.m_flat_output);

  StreamString typeName;

  // always show the type at the root level if it is invalid
  if (show_type) {
    // Some ValueObjects don't have types (like registers sets). Only print the
    // type if there is one to print
    ConstString type_name;
    if (m_compiler_type.IsValid()) {
      if (m_options.m_use_type_display_name)
        type_name = m_valobj->GetDisplayTypeName();
      else
        type_name = m_valobj->GetQualifiedTypeName();
    } else {
      // only show an invalid type name if the user explicitly triggered
      // show_type
      if (m_options.m_show_types)
        type_name = ConstString("<invalid type>");
      else
        type_name.Clear();
    }

    if (type_name) {
      std::string type_name_str(type_name.GetCString());
      if (m_options.m_hide_pointer_value) {
        for (auto iter = type_name_str.find(" *"); iter != std::string::npos;
             iter = type_name_str.find(" *")) {
          type_name_str.erase(iter, 2);
        }
      }
      typeName.Printf("%s", type_name_str.c_str());
    }
  }

  StreamString varName;

  if (m_options.m_flat_output) {
    // If we are showing types, also qualify the C++ base classes
    const bool qualify_cxx_base_classes = show_type;
    if (!m_options.m_hide_name) {
      m_valobj->GetExpressionPath(varName, qualify_cxx_base_classes);
    }
  } else if (!m_options.m_hide_name) {
    const char *name_cstr = GetRootNameForDisplay("");
    varName.Printf("%s", name_cstr);
  }

  bool decl_printed = false;
  if (!m_options.m_decl_printing_helper) {
    // if the user didn't give us a custom helper, pick one based upon the
    // language, either the one that this printer is bound to, or the preferred
    // one for the ValueObject
    lldb::LanguageType lang_type =
        (m_options.m_varformat_language == lldb::eLanguageTypeUnknown)
            ? m_valobj->GetPreferredDisplayLanguage()
            : m_options.m_varformat_language;
    if (Language *lang_plugin = Language::FindPlugin(lang_type)) {
      m_options.m_decl_printing_helper = lang_plugin->GetDeclPrintingHelper();
    }
  }

  if (m_options.m_decl_printing_helper) {
    ConstString type_name_cstr(typeName.GetString());
    ConstString var_name_cstr(varName.GetString());

    StreamString dest_stream;
    if (m_options.m_decl_printing_helper(type_name_cstr, var_name_cstr,
                                         m_options, dest_stream)) {
      decl_printed = true;
      m_stream->PutCString(dest_stream.GetString());
    }
  }

  // if the helper failed, or there is none, do a default thing
  if (!decl_printed) {
    if (!typeName.Empty())
      m_stream->Printf("(%s) ", typeName.GetData());
    if (!varName.Empty())
      m_stream->Printf("%s =", varName.GetData());
    else if (!m_options.m_hide_name)
      m_stream->Printf(" =");
  }
}

bool ValueObjectPrinter::CheckScopeIfNeeded() {
  if (m_options.m_scope_already_checked)
    return true;
  return m_valobj->IsInScope();
}

TypeSummaryImpl *ValueObjectPrinter::GetSummaryFormatter(bool null_if_omitted) {
  if (!m_summary_formatter.second) {
    TypeSummaryImpl *entry = m_options.m_summary_sp
                                 ? m_options.m_summary_sp.get()
                                 : m_valobj->GetSummaryFormat().get();

    if (m_options.m_omit_summary_depth > 0)
      entry = NULL;
    m_summary_formatter.first = entry;
    m_summary_formatter.second = true;
  }
  if (m_options.m_omit_summary_depth > 0 && null_if_omitted)
    return nullptr;
  return m_summary_formatter.first;
}

static bool IsPointerValue(const CompilerType &type) {
  Flags type_flags(type.GetTypeInfo());
  if (type_flags.AnySet(eTypeInstanceIsPointer | eTypeIsPointer))
    return type_flags.AllClear(eTypeIsBuiltIn);
  return false;
}

void ValueObjectPrinter::GetValueSummaryError(std::string &value,
                                              std::string &summary,
                                              std::string &error) {
  lldb::Format format = m_options.m_format;
  // if I am printing synthetized elements, apply the format to those elements
  // only
  if (m_options.m_pointer_as_array)
    m_valobj->GetValueAsCString(lldb::eFormatDefault, value);
  else if (format != eFormatDefault && format != m_valobj->GetFormat())
    m_valobj->GetValueAsCString(format, value);
  else {
    const char *val_cstr = m_valobj->GetValueAsCString();
    if (val_cstr)
      value.assign(val_cstr);
  }
  const char *err_cstr = m_valobj->GetError().AsCString();
  if (err_cstr)
    error.assign(err_cstr);

  if (ShouldPrintValueObject()) {
    if (IsNil())
      summary.assign("nil");
    else if (IsUninitialized())
      summary.assign("<uninitialized>");
    else if (m_options.m_omit_summary_depth == 0) {
      TypeSummaryImpl *entry = GetSummaryFormatter();
      if (entry)
        m_valobj->GetSummaryAsCString(entry, summary,
                                      m_options.m_varformat_language);
      else {
        const char *sum_cstr =
            m_valobj->GetSummaryAsCString(m_options.m_varformat_language);
        if (sum_cstr)
          summary.assign(sum_cstr);
      }
    }
  }
}

bool ValueObjectPrinter::PrintValueAndSummaryIfNeeded(bool &value_printed,
                                                      bool &summary_printed) {
  bool error_printed = false;
  if (ShouldPrintValueObject()) {
    if (!CheckScopeIfNeeded())
      m_error.assign("out of scope");
    if (m_error.empty()) {
      GetValueSummaryError(m_value, m_summary, m_error);
    }
    if (m_error.size()) {
      // we need to support scenarios in which it is actually fine for a value
      // to have no type but - on the other hand - if we get an error *AND*
      // have no type, we try to get out gracefully, since most often that
      // combination means "could not resolve a type" and the default failure
      // mode is quite ugly
      if (!m_compiler_type.IsValid()) {
        m_stream->Printf(" <could not resolve type>");
        return false;
      }

      error_printed = true;
      m_stream->Printf(" <%s>\n", m_error.c_str());
    } else {
      // Make sure we have a value and make sure the summary didn't specify
      // that the value should not be printed - and do not print the value if
      // this thing is nil (but show the value if the user passes a format
      // explicitly)
      TypeSummaryImpl *entry = GetSummaryFormatter();
      if (!IsNil() && !IsUninitialized() && !m_value.empty() &&
          (entry == NULL || (entry->DoesPrintValue(m_valobj) ||
                             m_options.m_format != eFormatDefault) ||
           m_summary.empty()) &&
          !m_options.m_hide_value) {
        if (m_options.m_hide_pointer_value &&
            IsPointerValue(m_valobj->GetCompilerType())) {
        } else {
          m_stream->Printf(" %s", m_value.c_str());
          value_printed = true;
        }
      }

      if (m_summary.size()) {
        m_stream->Printf(" %s", m_summary.c_str());
        summary_printed = true;
      }
    }
  }
  return !error_printed;
}

bool ValueObjectPrinter::PrintObjectDescriptionIfNeeded(bool value_printed,
                                                        bool summary_printed) {
  if (ShouldPrintValueObject()) {
    // let's avoid the overly verbose no description error for a nil thing
    if (m_options.m_use_objc && !IsNil() && !IsUninitialized() &&
        (!m_options.m_pointer_as_array)) {
      if (!m_options.m_hide_value || !m_options.m_hide_name)
        m_stream->Printf(" ");
      const char *object_desc = nullptr;
      if (value_printed || summary_printed)
        object_desc = m_valobj->GetObjectDescription();
      else
        object_desc = GetDescriptionForDisplay();
      if (object_desc && *object_desc) {
        // If the description already ends with a \n don't add another one.
        size_t object_end = strlen(object_desc) - 1;
        if (object_desc[object_end] == '\n')
            m_stream->Printf("%s", object_desc);
        else
            m_stream->Printf("%s\n", object_desc);        
        return true;
      } else if (!value_printed && !summary_printed)
        return true;
      else
        return false;
    }
  }
  return true;
}

bool DumpValueObjectOptions::PointerDepth::CanAllowExpansion() const {
  switch (m_mode) {
  case Mode::Always:
  case Mode::Default:
    return m_count > 0;
  case Mode::Never:
    return false;
  }
  return false;
}

bool ValueObjectPrinter::ShouldPrintChildren(
    bool is_failed_description,
    DumpValueObjectOptions::PointerDepth &curr_ptr_depth) {
  const bool is_ref = IsRef();
  const bool is_ptr = IsPtr();
  const bool is_uninit = IsUninitialized();

  if (is_uninit)
    return false;

  // if the user has specified an element count, always print children as it is
  // explicit user demand being honored
  if (m_options.m_pointer_as_array)
    return true;

  TypeSummaryImpl *entry = GetSummaryFormatter();

  if (m_options.m_use_objc)
    return false;

  if (is_failed_description || m_curr_depth < m_options.m_max_depth) {
    // We will show children for all concrete types. We won't show pointer
    // contents unless a pointer depth has been specified. We won't reference
    // contents unless the reference is the root object (depth of zero).

    // Use a new temporary pointer depth in case we override the current
    // pointer depth below...

    if (is_ptr || is_ref) {
      // We have a pointer or reference whose value is an address. Make sure
      // that address is not NULL
      AddressType ptr_address_type;
      if (m_valobj->GetPointerValue(&ptr_address_type) == 0)
        return false;

      const bool is_root_level = m_curr_depth == 0;

      if (is_ref && is_root_level) {
        // If this is the root object (depth is zero) that we are showing and
        // it is a reference, and no pointer depth has been supplied print out
        // what it references. Don't do this at deeper depths otherwise we can
        // end up with infinite recursion...
        return true;
      }

      return curr_ptr_depth.CanAllowExpansion();
    }

    return (!entry || entry->DoesPrintChildren(m_valobj) || m_summary.empty());
  }
  return false;
}

bool ValueObjectPrinter::ShouldExpandEmptyAggregates() {
  TypeSummaryImpl *entry = GetSummaryFormatter();

  if (!entry)
    return true;

  return entry->DoesPrintEmptyAggregates();
}

ValueObject *ValueObjectPrinter::GetValueObjectForChildrenGeneration() {
  return m_valobj;
}

void ValueObjectPrinter::PrintChildrenPreamble() {
  if (m_options.m_flat_output) {
    if (ShouldPrintValueObject())
      m_stream->EOL();
  } else {
    if (ShouldPrintValueObject())
      m_stream->PutCString(IsRef() ? ": {\n" : " {\n");
    m_stream->IndentMore();
  }
}

void ValueObjectPrinter::PrintChild(
    ValueObjectSP child_sp,
    const DumpValueObjectOptions::PointerDepth &curr_ptr_depth) {
  const uint32_t consumed_depth = (!m_options.m_pointer_as_array) ? 1 : 0;
  const bool does_consume_ptr_depth =
      ((IsPtr() && !m_options.m_pointer_as_array) || IsRef());

  DumpValueObjectOptions child_options(m_options);
  child_options.SetFormat(m_options.m_format)
      .SetSummary()
      .SetRootValueObjectName();
  child_options.SetScopeChecked(true)
      .SetHideName(m_options.m_hide_name)
      .SetHideValue(m_options.m_hide_value)
      .SetOmitSummaryDepth(child_options.m_omit_summary_depth > 1
                               ? child_options.m_omit_summary_depth -
                                     consumed_depth
                               : 0)
      .SetElementCount(0);

  if (child_sp.get()) {
    ValueObjectPrinter child_printer(
        child_sp.get(), m_stream, child_options,
        does_consume_ptr_depth ? --curr_ptr_depth : curr_ptr_depth,
        m_curr_depth + consumed_depth, m_printed_instance_pointers);
    child_printer.PrintValueObject();
  }
}

uint32_t ValueObjectPrinter::GetMaxNumChildrenToPrint(bool &print_dotdotdot) {
  ValueObject *synth_m_valobj = GetValueObjectForChildrenGeneration();

  if (m_options.m_pointer_as_array)
    return m_options.m_pointer_as_array.m_element_count;

  size_t num_children = synth_m_valobj->GetNumChildren();
  print_dotdotdot = false;
  if (num_children) {
    const size_t max_num_children =
        m_valobj->GetTargetSP()->GetMaximumNumberOfChildrenToDisplay();

    if (num_children > max_num_children && !m_options.m_ignore_cap) {
      print_dotdotdot = true;
      return max_num_children;
    }
  }
  return num_children;
}

void ValueObjectPrinter::PrintChildrenPostamble(bool print_dotdotdot) {
  if (!m_options.m_flat_output) {
    if (print_dotdotdot) {
      m_valobj->GetTargetSP()
          ->GetDebugger()
          .GetCommandInterpreter()
          .ChildrenTruncated();
      m_stream->Indent("...\n");
    }
    m_stream->IndentLess();
    m_stream->Indent("}\n");
  }
}

bool ValueObjectPrinter::ShouldPrintEmptyBrackets(bool value_printed,
                                                  bool summary_printed) {
  ValueObject *synth_m_valobj = GetValueObjectForChildrenGeneration();

  if (!IsAggregate())
    return false;

  if (!m_options.m_reveal_empty_aggregates) {
    if (value_printed || summary_printed)
      return false;
  }

  if (synth_m_valobj->MightHaveChildren())
    return true;

  if (m_val_summary_ok)
    return false;

  return true;
}

static constexpr size_t PhysicalIndexForLogicalIndex(size_t base, size_t stride,
                                                     size_t logical) {
  return base + logical * stride;
}

ValueObjectSP ValueObjectPrinter::GenerateChild(ValueObject *synth_valobj,
                                                size_t idx) {
  if (m_options.m_pointer_as_array) {
    // if generating pointer-as-array children, use GetSyntheticArrayMember
    return synth_valobj->GetSyntheticArrayMember(
        PhysicalIndexForLogicalIndex(
            m_options.m_pointer_as_array.m_base_element,
            m_options.m_pointer_as_array.m_stride, idx),
        true);
  } else {
    // otherwise, do the usual thing
    return synth_valobj->GetChildAtIndex(idx, true);
  }
}

void ValueObjectPrinter::PrintChildren(
    bool value_printed, bool summary_printed,
    const DumpValueObjectOptions::PointerDepth &curr_ptr_depth) {
  ValueObject *synth_m_valobj = GetValueObjectForChildrenGeneration();

  bool print_dotdotdot = false;
  size_t num_children = GetMaxNumChildrenToPrint(print_dotdotdot);
  if (num_children) {
    bool any_children_printed = false;

    for (size_t idx = 0; idx < num_children; ++idx) {
      if (ValueObjectSP child_sp = GenerateChild(synth_m_valobj, idx)) {
        if (!any_children_printed) {
          PrintChildrenPreamble();
          any_children_printed = true;
        }
        PrintChild(child_sp, curr_ptr_depth);
      }
    }

    if (any_children_printed)
      PrintChildrenPostamble(print_dotdotdot);
    else {
      if (ShouldPrintEmptyBrackets(value_printed, summary_printed)) {
        if (ShouldPrintValueObject())
          m_stream->PutCString(" {}\n");
        else
          m_stream->EOL();
      } else
        m_stream->EOL();
    }
  } else if (ShouldPrintEmptyBrackets(value_printed, summary_printed)) {
    // Aggregate, no children...
    if (ShouldPrintValueObject()) {
      // if it has a synthetic value, then don't print {}, the synthetic
      // children are probably only being used to vend a value
      if (m_valobj->DoesProvideSyntheticValue() ||
          !ShouldExpandEmptyAggregates())
        m_stream->PutCString("\n");
      else
        m_stream->PutCString(" {}\n");
    }
  } else {
    if (ShouldPrintValueObject())
      m_stream->EOL();
  }
}

bool ValueObjectPrinter::PrintChildrenOneLiner(bool hide_names) {
  if (!GetMostSpecializedValue() || m_valobj == nullptr)
    return false;

  ValueObject *synth_m_valobj = GetValueObjectForChildrenGeneration();

  bool print_dotdotdot = false;
  size_t num_children = GetMaxNumChildrenToPrint(print_dotdotdot);

  if (num_children) {
    m_stream->PutChar('(');

    for (uint32_t idx = 0; idx < num_children; ++idx) {
      lldb::ValueObjectSP child_sp(synth_m_valobj->GetChildAtIndex(idx, true));
      if (child_sp)
        child_sp = child_sp->GetQualifiedRepresentationIfAvailable(
            m_options.m_use_dynamic, m_options.m_use_synthetic);
      if (child_sp) {
        if (idx)
          m_stream->PutCString(", ");
        if (!hide_names) {
          const char *name = child_sp.get()->GetName().AsCString();
          if (name && *name) {
            m_stream->PutCString(name);
            m_stream->PutCString(" = ");
          }
        }
        child_sp->DumpPrintableRepresentation(
            *m_stream, ValueObject::eValueObjectRepresentationStyleSummary,
            m_options.m_format,
            ValueObject::PrintableRepresentationSpecialCases::eDisable);
      }
    }

    if (print_dotdotdot)
      m_stream->PutCString(", ...)");
    else
      m_stream->PutChar(')');
  }
  return true;
}

void ValueObjectPrinter::PrintChildrenIfNeeded(bool value_printed,
                                               bool summary_printed) {
  // this flag controls whether we tried to display a description for this
  // object and failed if that happens, we want to display the children, if any
  bool is_failed_description =
      !PrintObjectDescriptionIfNeeded(value_printed, summary_printed);

  auto curr_ptr_depth = m_ptr_depth;
  bool print_children =
      ShouldPrintChildren(is_failed_description, curr_ptr_depth);
  bool print_oneline =
      (curr_ptr_depth.CanAllowExpansion() || m_options.m_show_types ||
       !m_options.m_allow_oneliner_mode || m_options.m_flat_output ||
       (m_options.m_pointer_as_array) || m_options.m_show_location)
          ? false
          : DataVisualization::ShouldPrintAsOneLiner(*m_valobj);
  bool is_instance_ptr = IsInstancePointer();
  uint64_t instance_ptr_value = LLDB_INVALID_ADDRESS;

  if (print_children && is_instance_ptr) {
    instance_ptr_value = m_valobj->GetValueAsUnsigned(0);
    if (m_printed_instance_pointers->count(instance_ptr_value)) {
      // we already printed this instance-is-pointer thing, so don't expand it
      m_stream->PutCString(" {...}\n");

      // we're done here - get out fast
      return;
    } else
      m_printed_instance_pointers->emplace(
          instance_ptr_value); // remember this guy for future reference
  }

  if (print_children) {
    if (print_oneline) {
      m_stream->PutChar(' ');
      PrintChildrenOneLiner(false);
      m_stream->EOL();
    } else
      PrintChildren(value_printed, summary_printed, curr_ptr_depth);
  } else if (m_curr_depth >= m_options.m_max_depth && IsAggregate() &&
             ShouldPrintValueObject()) {
    m_stream->PutCString("{...}\n");
  } else
    m_stream->EOL();
}

bool ValueObjectPrinter::ShouldPrintValidation() {
  return m_options.m_run_validator;
}

bool ValueObjectPrinter::PrintValidationMarkerIfNeeded() {
  if (!ShouldPrintValidation())
    return false;

  m_validation = m_valobj->GetValidationStatus();

  if (TypeValidatorResult::Failure == m_validation.first) {
    m_stream->Printf("! ");
    return true;
  }

  return false;
}

bool ValueObjectPrinter::PrintValidationErrorIfNeeded() {
  if (!ShouldPrintValidation())
    return false;

  if (TypeValidatorResult::Success == m_validation.first)
    return false;

  if (m_validation.second.empty())
    m_validation.second.assign("unknown error");

  m_stream->Printf(" ! validation error: %s", m_validation.second.c_str());
  m_stream->EOL();

  return true;
}
