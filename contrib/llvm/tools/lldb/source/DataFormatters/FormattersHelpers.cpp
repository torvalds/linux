//===-- FormattersHelpers.cpp -------------------------------------*- C++
//-*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//




#include "lldb/DataFormatters/FormattersHelpers.h"

#include "lldb/Target/StackFrame.h"
#include "lldb/Target/Target.h"
#include "lldb/Target/Thread.h"
#include "lldb/Utility/ConstString.h"
#include "lldb/Utility/RegularExpression.h"

using namespace lldb;
using namespace lldb_private;
using namespace lldb_private::formatters;

void lldb_private::formatters::AddFormat(
    TypeCategoryImpl::SharedPointer category_sp, lldb::Format format,
    ConstString type_name, TypeFormatImpl::Flags flags, bool regex) {
  lldb::TypeFormatImplSP format_sp(new TypeFormatImpl_Format(format, flags));

  if (regex)
    category_sp->GetRegexTypeFormatsContainer()->Add(
        RegularExpressionSP(new RegularExpression(type_name.GetStringRef())),
        format_sp);
  else
    category_sp->GetTypeFormatsContainer()->Add(type_name, format_sp);
}

void lldb_private::formatters::AddSummary(
    TypeCategoryImpl::SharedPointer category_sp, TypeSummaryImplSP summary_sp,
    ConstString type_name, bool regex) {
  if (regex)
    category_sp->GetRegexTypeSummariesContainer()->Add(
        RegularExpressionSP(new RegularExpression(type_name.GetStringRef())),
        summary_sp);
  else
    category_sp->GetTypeSummariesContainer()->Add(type_name, summary_sp);
}

void lldb_private::formatters::AddStringSummary(
    TypeCategoryImpl::SharedPointer category_sp, const char *string,
    ConstString type_name, TypeSummaryImpl::Flags flags, bool regex) {
  lldb::TypeSummaryImplSP summary_sp(new StringSummaryFormat(flags, string));

  if (regex)
    category_sp->GetRegexTypeSummariesContainer()->Add(
        RegularExpressionSP(new RegularExpression(type_name.GetStringRef())),
        summary_sp);
  else
    category_sp->GetTypeSummariesContainer()->Add(type_name, summary_sp);
}

void lldb_private::formatters::AddOneLineSummary(
    TypeCategoryImpl::SharedPointer category_sp, ConstString type_name,
    TypeSummaryImpl::Flags flags, bool regex) {
  flags.SetShowMembersOneLiner(true);
  lldb::TypeSummaryImplSP summary_sp(new StringSummaryFormat(flags, ""));

  if (regex)
    category_sp->GetRegexTypeSummariesContainer()->Add(
        RegularExpressionSP(new RegularExpression(type_name.GetStringRef())),
        summary_sp);
  else
    category_sp->GetTypeSummariesContainer()->Add(type_name, summary_sp);
}

#ifndef LLDB_DISABLE_PYTHON
void lldb_private::formatters::AddCXXSummary(
    TypeCategoryImpl::SharedPointer category_sp,
    CXXFunctionSummaryFormat::Callback funct, const char *description,
    ConstString type_name, TypeSummaryImpl::Flags flags, bool regex) {
  lldb::TypeSummaryImplSP summary_sp(
      new CXXFunctionSummaryFormat(flags, funct, description));
  if (regex)
    category_sp->GetRegexTypeSummariesContainer()->Add(
        RegularExpressionSP(new RegularExpression(type_name.GetStringRef())),
        summary_sp);
  else
    category_sp->GetTypeSummariesContainer()->Add(type_name, summary_sp);
}

void lldb_private::formatters::AddCXXSynthetic(
    TypeCategoryImpl::SharedPointer category_sp,
    CXXSyntheticChildren::CreateFrontEndCallback generator,
    const char *description, ConstString type_name,
    ScriptedSyntheticChildren::Flags flags, bool regex) {
  lldb::SyntheticChildrenSP synth_sp(
      new CXXSyntheticChildren(flags, description, generator));
  if (regex)
    category_sp->GetRegexTypeSyntheticsContainer()->Add(
        RegularExpressionSP(new RegularExpression(type_name.GetStringRef())),
        synth_sp);
  else
    category_sp->GetTypeSyntheticsContainer()->Add(type_name, synth_sp);
}

void lldb_private::formatters::AddFilter(
    TypeCategoryImpl::SharedPointer category_sp,
    std::vector<std::string> children, const char *description,
    ConstString type_name, ScriptedSyntheticChildren::Flags flags, bool regex) {
  TypeFilterImplSP filter_sp(new TypeFilterImpl(flags));
  for (auto child : children)
    filter_sp->AddExpressionPath(child);
  if (regex)
    category_sp->GetRegexTypeFiltersContainer()->Add(
        RegularExpressionSP(new RegularExpression(type_name.GetStringRef())),
        filter_sp);
  else
    category_sp->GetTypeFiltersContainer()->Add(type_name, filter_sp);
}
#endif

size_t lldb_private::formatters::ExtractIndexFromString(const char *item_name) {
  if (!item_name || !*item_name)
    return UINT32_MAX;
  if (*item_name != '[')
    return UINT32_MAX;
  item_name++;
  char *endptr = NULL;
  unsigned long int idx = ::strtoul(item_name, &endptr, 0);
  if (idx == 0 && endptr == item_name)
    return UINT32_MAX;
  if (idx == ULONG_MAX)
    return UINT32_MAX;
  return idx;
}

lldb::addr_t
lldb_private::formatters::GetArrayAddressOrPointerValue(ValueObject &valobj) {
  lldb::addr_t data_addr = LLDB_INVALID_ADDRESS;

  if (valobj.IsPointerType())
    data_addr = valobj.GetValueAsUnsigned(0);
  else if (valobj.IsArrayType())
    data_addr = valobj.GetAddressOf();

  return data_addr;
}
