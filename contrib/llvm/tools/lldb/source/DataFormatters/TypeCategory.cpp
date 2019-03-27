//===-- TypeCategory.cpp -----------------------------------------*- C++-*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "lldb/DataFormatters/TypeCategory.h"
#include "lldb/Target/Language.h"


using namespace lldb;
using namespace lldb_private;

TypeCategoryImpl::TypeCategoryImpl(
    IFormatChangeListener *clist, ConstString name,
    std::initializer_list<lldb::LanguageType> langs)
    : m_format_cont("format", "regex-format", clist),
      m_summary_cont("summary", "regex-summary", clist),
      m_filter_cont("filter", "regex-filter", clist),
#ifndef LLDB_DISABLE_PYTHON
      m_synth_cont("synth", "regex-synth", clist),
#endif
      m_validator_cont("validator", "regex-validator", clist), m_enabled(false),
      m_change_listener(clist), m_mutex(), m_name(name), m_languages() {
  for (const lldb::LanguageType lang : langs)
    AddLanguage(lang);
}

static bool IsApplicable(lldb::LanguageType category_lang,
                         lldb::LanguageType valobj_lang) {
  switch (category_lang) {
  // Unless we know better, allow only exact equality.
  default:
    return category_lang == valobj_lang;

  // the C family, we consider it as one
  case eLanguageTypeC89:
  case eLanguageTypeC:
  case eLanguageTypeC99:
    return valobj_lang == eLanguageTypeC89 || valobj_lang == eLanguageTypeC ||
           valobj_lang == eLanguageTypeC99;

  // ObjC knows about C and itself
  case eLanguageTypeObjC:
    return valobj_lang == eLanguageTypeC89 || valobj_lang == eLanguageTypeC ||
           valobj_lang == eLanguageTypeC99 || valobj_lang == eLanguageTypeObjC;

  // C++ knows about C and C++
  case eLanguageTypeC_plus_plus:
    return valobj_lang == eLanguageTypeC89 || valobj_lang == eLanguageTypeC ||
           valobj_lang == eLanguageTypeC99 ||
           valobj_lang == eLanguageTypeC_plus_plus;

  // ObjC++ knows about C,C++,ObjC and ObjC++
  case eLanguageTypeObjC_plus_plus:
    return valobj_lang == eLanguageTypeC89 || valobj_lang == eLanguageTypeC ||
           valobj_lang == eLanguageTypeC99 ||
           valobj_lang == eLanguageTypeC_plus_plus ||
           valobj_lang == eLanguageTypeObjC;

  // Categories with unspecified language match everything.
  case eLanguageTypeUnknown:
    return true;
  }
}

bool TypeCategoryImpl::IsApplicable(ValueObject &valobj) {
  lldb::LanguageType valobj_lang = valobj.GetObjectRuntimeLanguage();
  for (size_t idx = 0; idx < GetNumLanguages(); idx++) {
    const lldb::LanguageType category_lang = GetLanguageAtIndex(idx);
    if (::IsApplicable(category_lang, valobj_lang))
      return true;
  }
  return false;
}

size_t TypeCategoryImpl::GetNumLanguages() {
  if (m_languages.empty())
    return 1;
  return m_languages.size();
}

lldb::LanguageType TypeCategoryImpl::GetLanguageAtIndex(size_t idx) {
  if (m_languages.empty())
    return lldb::eLanguageTypeUnknown;
  return m_languages[idx];
}

void TypeCategoryImpl::AddLanguage(lldb::LanguageType lang) {
  m_languages.push_back(lang);
}

bool TypeCategoryImpl::HasLanguage(lldb::LanguageType lang) {
  const auto iter = std::find(m_languages.begin(), m_languages.end(), lang),
             end = m_languages.end();
  return (iter != end);
}

bool TypeCategoryImpl::Get(ValueObject &valobj,
                           const FormattersMatchVector &candidates,
                           lldb::TypeFormatImplSP &entry, uint32_t *reason) {
  if (!IsEnabled() || !IsApplicable(valobj))
    return false;
  if (GetTypeFormatsContainer()->Get(candidates, entry, reason))
    return true;
  bool regex = GetRegexTypeFormatsContainer()->Get(candidates, entry, reason);
  if (regex && reason)
    *reason |= lldb_private::eFormatterChoiceCriterionRegularExpressionSummary;
  return regex;
}

bool TypeCategoryImpl::Get(ValueObject &valobj,
                           const FormattersMatchVector &candidates,
                           lldb::TypeSummaryImplSP &entry, uint32_t *reason) {
  if (!IsEnabled() || !IsApplicable(valobj))
    return false;
  if (GetTypeSummariesContainer()->Get(candidates, entry, reason))
    return true;
  bool regex = GetRegexTypeSummariesContainer()->Get(candidates, entry, reason);
  if (regex && reason)
    *reason |= lldb_private::eFormatterChoiceCriterionRegularExpressionSummary;
  return regex;
}

bool TypeCategoryImpl::Get(ValueObject &valobj,
                           const FormattersMatchVector &candidates,
                           lldb::SyntheticChildrenSP &entry, uint32_t *reason) {
  if (!IsEnabled() || !IsApplicable(valobj))
    return false;
  TypeFilterImpl::SharedPointer filter_sp;
  uint32_t reason_filter = 0;
  bool regex_filter = false;
  // first find both Filter and Synth, and then check which is most recent

  if (!GetTypeFiltersContainer()->Get(candidates, filter_sp, &reason_filter))
    regex_filter = GetRegexTypeFiltersContainer()->Get(candidates, filter_sp,
                                                       &reason_filter);

#ifndef LLDB_DISABLE_PYTHON
  bool regex_synth = false;
  uint32_t reason_synth = 0;
  bool pick_synth = false;
  ScriptedSyntheticChildren::SharedPointer synth;
  if (!GetTypeSyntheticsContainer()->Get(candidates, synth, &reason_synth))
    regex_synth = GetRegexTypeSyntheticsContainer()->Get(candidates, synth,
                                                         &reason_synth);
  if (!filter_sp.get() && !synth.get())
    return false;
  else if (!filter_sp.get() && synth.get())
    pick_synth = true;

  else if (filter_sp.get() && !synth.get())
    pick_synth = false;

  else /*if (filter_sp.get() && synth.get())*/
  {
    pick_synth = filter_sp->GetRevision() <= synth->GetRevision();
  }
  if (pick_synth) {
    if (regex_synth && reason)
      *reason |= lldb_private::eFormatterChoiceCriterionRegularExpressionFilter;
    entry = synth;
    return true;
  } else {
    if (regex_filter && reason)
      *reason |= lldb_private::eFormatterChoiceCriterionRegularExpressionFilter;
    entry = filter_sp;
    return true;
  }

#else
  if (filter_sp) {
    entry = filter_sp;
    return true;
  }
#endif

  return false;
}

bool TypeCategoryImpl::Get(ValueObject &valobj,
                           const FormattersMatchVector &candidates,
                           lldb::TypeValidatorImplSP &entry, uint32_t *reason) {
  if (!IsEnabled())
    return false;
  if (GetTypeValidatorsContainer()->Get(candidates, entry, reason))
    return true;
  bool regex =
      GetRegexTypeValidatorsContainer()->Get(candidates, entry, reason);
  if (regex && reason)
    *reason |= lldb_private::eFormatterChoiceCriterionRegularExpressionSummary;
  return regex;
}

void TypeCategoryImpl::Clear(FormatCategoryItems items) {
  if ((items & eFormatCategoryItemValue) == eFormatCategoryItemValue)
    GetTypeFormatsContainer()->Clear();
  if ((items & eFormatCategoryItemRegexValue) == eFormatCategoryItemRegexValue)
    GetRegexTypeFormatsContainer()->Clear();

  if ((items & eFormatCategoryItemSummary) == eFormatCategoryItemSummary)
    GetTypeSummariesContainer()->Clear();
  if ((items & eFormatCategoryItemRegexSummary) ==
      eFormatCategoryItemRegexSummary)
    GetRegexTypeSummariesContainer()->Clear();

  if ((items & eFormatCategoryItemFilter) == eFormatCategoryItemFilter)
    GetTypeFiltersContainer()->Clear();
  if ((items & eFormatCategoryItemRegexFilter) ==
      eFormatCategoryItemRegexFilter)
    GetRegexTypeFiltersContainer()->Clear();

#ifndef LLDB_DISABLE_PYTHON
  if ((items & eFormatCategoryItemSynth) == eFormatCategoryItemSynth)
    GetTypeSyntheticsContainer()->Clear();
  if ((items & eFormatCategoryItemRegexSynth) == eFormatCategoryItemRegexSynth)
    GetRegexTypeSyntheticsContainer()->Clear();
#endif

  if ((items & eFormatCategoryItemValidator) == eFormatCategoryItemValidator)
    GetTypeValidatorsContainer()->Clear();
  if ((items & eFormatCategoryItemRegexValidator) ==
      eFormatCategoryItemRegexValidator)
    GetRegexTypeValidatorsContainer()->Clear();
}

bool TypeCategoryImpl::Delete(ConstString name, FormatCategoryItems items) {
  bool success = false;

  if ((items & eFormatCategoryItemValue) == eFormatCategoryItemValue)
    success = GetTypeFormatsContainer()->Delete(name) || success;
  if ((items & eFormatCategoryItemRegexValue) == eFormatCategoryItemRegexValue)
    success = GetRegexTypeFormatsContainer()->Delete(name) || success;

  if ((items & eFormatCategoryItemSummary) == eFormatCategoryItemSummary)
    success = GetTypeSummariesContainer()->Delete(name) || success;
  if ((items & eFormatCategoryItemRegexSummary) ==
      eFormatCategoryItemRegexSummary)
    success = GetRegexTypeSummariesContainer()->Delete(name) || success;

  if ((items & eFormatCategoryItemFilter) == eFormatCategoryItemFilter)
    success = GetTypeFiltersContainer()->Delete(name) || success;
  if ((items & eFormatCategoryItemRegexFilter) ==
      eFormatCategoryItemRegexFilter)
    success = GetRegexTypeFiltersContainer()->Delete(name) || success;

#ifndef LLDB_DISABLE_PYTHON
  if ((items & eFormatCategoryItemSynth) == eFormatCategoryItemSynth)
    success = GetTypeSyntheticsContainer()->Delete(name) || success;
  if ((items & eFormatCategoryItemRegexSynth) == eFormatCategoryItemRegexSynth)
    success = GetRegexTypeSyntheticsContainer()->Delete(name) || success;
#endif

  if ((items & eFormatCategoryItemValidator) == eFormatCategoryItemValidator)
    success = GetTypeValidatorsContainer()->Delete(name) || success;
  if ((items & eFormatCategoryItemRegexValidator) ==
      eFormatCategoryItemRegexValidator)
    success = GetRegexTypeValidatorsContainer()->Delete(name) || success;

  return success;
}

uint32_t TypeCategoryImpl::GetCount(FormatCategoryItems items) {
  uint32_t count = 0;

  if ((items & eFormatCategoryItemValue) == eFormatCategoryItemValue)
    count += GetTypeFormatsContainer()->GetCount();
  if ((items & eFormatCategoryItemRegexValue) == eFormatCategoryItemRegexValue)
    count += GetRegexTypeFormatsContainer()->GetCount();

  if ((items & eFormatCategoryItemSummary) == eFormatCategoryItemSummary)
    count += GetTypeSummariesContainer()->GetCount();
  if ((items & eFormatCategoryItemRegexSummary) ==
      eFormatCategoryItemRegexSummary)
    count += GetRegexTypeSummariesContainer()->GetCount();

  if ((items & eFormatCategoryItemFilter) == eFormatCategoryItemFilter)
    count += GetTypeFiltersContainer()->GetCount();
  if ((items & eFormatCategoryItemRegexFilter) ==
      eFormatCategoryItemRegexFilter)
    count += GetRegexTypeFiltersContainer()->GetCount();

#ifndef LLDB_DISABLE_PYTHON
  if ((items & eFormatCategoryItemSynth) == eFormatCategoryItemSynth)
    count += GetTypeSyntheticsContainer()->GetCount();
  if ((items & eFormatCategoryItemRegexSynth) == eFormatCategoryItemRegexSynth)
    count += GetRegexTypeSyntheticsContainer()->GetCount();
#endif

  if ((items & eFormatCategoryItemValidator) == eFormatCategoryItemValidator)
    count += GetTypeValidatorsContainer()->GetCount();
  if ((items & eFormatCategoryItemRegexValidator) ==
      eFormatCategoryItemRegexValidator)
    count += GetRegexTypeValidatorsContainer()->GetCount();

  return count;
}

bool TypeCategoryImpl::AnyMatches(ConstString type_name,
                                  FormatCategoryItems items, bool only_enabled,
                                  const char **matching_category,
                                  FormatCategoryItems *matching_type) {
  if (!IsEnabled() && only_enabled)
    return false;

  lldb::TypeFormatImplSP format_sp;
  lldb::TypeSummaryImplSP summary_sp;
  TypeFilterImpl::SharedPointer filter_sp;
#ifndef LLDB_DISABLE_PYTHON
  ScriptedSyntheticChildren::SharedPointer synth_sp;
#endif
  TypeValidatorImpl::SharedPointer validator_sp;

  if ((items & eFormatCategoryItemValue) == eFormatCategoryItemValue) {
    if (GetTypeFormatsContainer()->Get(type_name, format_sp)) {
      if (matching_category)
        *matching_category = m_name.GetCString();
      if (matching_type)
        *matching_type = eFormatCategoryItemValue;
      return true;
    }
  }
  if ((items & eFormatCategoryItemRegexValue) ==
      eFormatCategoryItemRegexValue) {
    if (GetRegexTypeFormatsContainer()->Get(type_name, format_sp)) {
      if (matching_category)
        *matching_category = m_name.GetCString();
      if (matching_type)
        *matching_type = eFormatCategoryItemRegexValue;
      return true;
    }
  }

  if ((items & eFormatCategoryItemSummary) == eFormatCategoryItemSummary) {
    if (GetTypeSummariesContainer()->Get(type_name, summary_sp)) {
      if (matching_category)
        *matching_category = m_name.GetCString();
      if (matching_type)
        *matching_type = eFormatCategoryItemSummary;
      return true;
    }
  }
  if ((items & eFormatCategoryItemRegexSummary) ==
      eFormatCategoryItemRegexSummary) {
    if (GetRegexTypeSummariesContainer()->Get(type_name, summary_sp)) {
      if (matching_category)
        *matching_category = m_name.GetCString();
      if (matching_type)
        *matching_type = eFormatCategoryItemRegexSummary;
      return true;
    }
  }

  if ((items & eFormatCategoryItemFilter) == eFormatCategoryItemFilter) {
    if (GetTypeFiltersContainer()->Get(type_name, filter_sp)) {
      if (matching_category)
        *matching_category = m_name.GetCString();
      if (matching_type)
        *matching_type = eFormatCategoryItemFilter;
      return true;
    }
  }
  if ((items & eFormatCategoryItemRegexFilter) ==
      eFormatCategoryItemRegexFilter) {
    if (GetRegexTypeFiltersContainer()->Get(type_name, filter_sp)) {
      if (matching_category)
        *matching_category = m_name.GetCString();
      if (matching_type)
        *matching_type = eFormatCategoryItemRegexFilter;
      return true;
    }
  }

#ifndef LLDB_DISABLE_PYTHON
  if ((items & eFormatCategoryItemSynth) == eFormatCategoryItemSynth) {
    if (GetTypeSyntheticsContainer()->Get(type_name, synth_sp)) {
      if (matching_category)
        *matching_category = m_name.GetCString();
      if (matching_type)
        *matching_type = eFormatCategoryItemSynth;
      return true;
    }
  }
  if ((items & eFormatCategoryItemRegexSynth) ==
      eFormatCategoryItemRegexSynth) {
    if (GetRegexTypeSyntheticsContainer()->Get(type_name, synth_sp)) {
      if (matching_category)
        *matching_category = m_name.GetCString();
      if (matching_type)
        *matching_type = eFormatCategoryItemRegexSynth;
      return true;
    }
  }
#endif

  if ((items & eFormatCategoryItemValidator) == eFormatCategoryItemValidator) {
    if (GetTypeValidatorsContainer()->Get(type_name, validator_sp)) {
      if (matching_category)
        *matching_category = m_name.GetCString();
      if (matching_type)
        *matching_type = eFormatCategoryItemValidator;
      return true;
    }
  }
  if ((items & eFormatCategoryItemRegexValidator) ==
      eFormatCategoryItemRegexValidator) {
    if (GetRegexTypeValidatorsContainer()->Get(type_name, validator_sp)) {
      if (matching_category)
        *matching_category = m_name.GetCString();
      if (matching_type)
        *matching_type = eFormatCategoryItemRegexValidator;
      return true;
    }
  }

  return false;
}

TypeCategoryImpl::FormatContainer::MapValueType
TypeCategoryImpl::GetFormatForType(lldb::TypeNameSpecifierImplSP type_sp) {
  FormatContainer::MapValueType retval;

  if (type_sp) {
    if (type_sp->IsRegex())
      GetRegexTypeFormatsContainer()->GetExact(ConstString(type_sp->GetName()),
                                               retval);
    else
      GetTypeFormatsContainer()->GetExact(ConstString(type_sp->GetName()),
                                          retval);
  }

  return retval;
}

TypeCategoryImpl::SummaryContainer::MapValueType
TypeCategoryImpl::GetSummaryForType(lldb::TypeNameSpecifierImplSP type_sp) {
  SummaryContainer::MapValueType retval;

  if (type_sp) {
    if (type_sp->IsRegex())
      GetRegexTypeSummariesContainer()->GetExact(
          ConstString(type_sp->GetName()), retval);
    else
      GetTypeSummariesContainer()->GetExact(ConstString(type_sp->GetName()),
                                            retval);
  }

  return retval;
}

TypeCategoryImpl::FilterContainer::MapValueType
TypeCategoryImpl::GetFilterForType(lldb::TypeNameSpecifierImplSP type_sp) {
  FilterContainer::MapValueType retval;

  if (type_sp) {
    if (type_sp->IsRegex())
      GetRegexTypeFiltersContainer()->GetExact(ConstString(type_sp->GetName()),
                                               retval);
    else
      GetTypeFiltersContainer()->GetExact(ConstString(type_sp->GetName()),
                                          retval);
  }

  return retval;
}

#ifndef LLDB_DISABLE_PYTHON
TypeCategoryImpl::SynthContainer::MapValueType
TypeCategoryImpl::GetSyntheticForType(lldb::TypeNameSpecifierImplSP type_sp) {
  SynthContainer::MapValueType retval;

  if (type_sp) {
    if (type_sp->IsRegex())
      GetRegexTypeSyntheticsContainer()->GetExact(
          ConstString(type_sp->GetName()), retval);
    else
      GetTypeSyntheticsContainer()->GetExact(ConstString(type_sp->GetName()),
                                             retval);
  }

  return retval;
}
#endif

TypeCategoryImpl::ValidatorContainer::MapValueType
TypeCategoryImpl::GetValidatorForType(lldb::TypeNameSpecifierImplSP type_sp) {
  ValidatorContainer::MapValueType retval;

  if (type_sp) {
    if (type_sp->IsRegex())
      GetRegexTypeValidatorsContainer()->GetExact(
          ConstString(type_sp->GetName()), retval);
    else
      GetTypeValidatorsContainer()->GetExact(ConstString(type_sp->GetName()),
                                             retval);
  }

  return retval;
}

lldb::TypeNameSpecifierImplSP
TypeCategoryImpl::GetTypeNameSpecifierForSummaryAtIndex(size_t index) {
  if (index < GetTypeSummariesContainer()->GetCount())
    return GetTypeSummariesContainer()->GetTypeNameSpecifierAtIndex(index);
  else
    return GetRegexTypeSummariesContainer()->GetTypeNameSpecifierAtIndex(
        index - GetTypeSummariesContainer()->GetCount());
}

TypeCategoryImpl::FormatContainer::MapValueType
TypeCategoryImpl::GetFormatAtIndex(size_t index) {
  if (index < GetTypeFormatsContainer()->GetCount())
    return GetTypeFormatsContainer()->GetAtIndex(index);
  else
    return GetRegexTypeFormatsContainer()->GetAtIndex(
        index - GetTypeFormatsContainer()->GetCount());
}

TypeCategoryImpl::SummaryContainer::MapValueType
TypeCategoryImpl::GetSummaryAtIndex(size_t index) {
  if (index < GetTypeSummariesContainer()->GetCount())
    return GetTypeSummariesContainer()->GetAtIndex(index);
  else
    return GetRegexTypeSummariesContainer()->GetAtIndex(
        index - GetTypeSummariesContainer()->GetCount());
}

TypeCategoryImpl::FilterContainer::MapValueType
TypeCategoryImpl::GetFilterAtIndex(size_t index) {
  if (index < GetTypeFiltersContainer()->GetCount())
    return GetTypeFiltersContainer()->GetAtIndex(index);
  else
    return GetRegexTypeFiltersContainer()->GetAtIndex(
        index - GetTypeFiltersContainer()->GetCount());
}

lldb::TypeNameSpecifierImplSP
TypeCategoryImpl::GetTypeNameSpecifierForFormatAtIndex(size_t index) {
  if (index < GetTypeFormatsContainer()->GetCount())
    return GetTypeFormatsContainer()->GetTypeNameSpecifierAtIndex(index);
  else
    return GetRegexTypeFormatsContainer()->GetTypeNameSpecifierAtIndex(
        index - GetTypeFormatsContainer()->GetCount());
}

lldb::TypeNameSpecifierImplSP
TypeCategoryImpl::GetTypeNameSpecifierForFilterAtIndex(size_t index) {
  if (index < GetTypeFiltersContainer()->GetCount())
    return GetTypeFiltersContainer()->GetTypeNameSpecifierAtIndex(index);
  else
    return GetRegexTypeFiltersContainer()->GetTypeNameSpecifierAtIndex(
        index - GetTypeFiltersContainer()->GetCount());
}

#ifndef LLDB_DISABLE_PYTHON
TypeCategoryImpl::SynthContainer::MapValueType
TypeCategoryImpl::GetSyntheticAtIndex(size_t index) {
  if (index < GetTypeSyntheticsContainer()->GetCount())
    return GetTypeSyntheticsContainer()->GetAtIndex(index);
  else
    return GetRegexTypeSyntheticsContainer()->GetAtIndex(
        index - GetTypeSyntheticsContainer()->GetCount());
}

lldb::TypeNameSpecifierImplSP
TypeCategoryImpl::GetTypeNameSpecifierForSyntheticAtIndex(size_t index) {
  if (index < GetTypeSyntheticsContainer()->GetCount())
    return GetTypeSyntheticsContainer()->GetTypeNameSpecifierAtIndex(index);
  else
    return GetRegexTypeSyntheticsContainer()->GetTypeNameSpecifierAtIndex(
        index - GetTypeSyntheticsContainer()->GetCount());
}
#endif

TypeCategoryImpl::ValidatorContainer::MapValueType
TypeCategoryImpl::GetValidatorAtIndex(size_t index) {
  if (index < GetTypeValidatorsContainer()->GetCount())
    return GetTypeValidatorsContainer()->GetAtIndex(index);
  else
    return GetRegexTypeValidatorsContainer()->GetAtIndex(
        index - GetTypeValidatorsContainer()->GetCount());
}

lldb::TypeNameSpecifierImplSP
TypeCategoryImpl::GetTypeNameSpecifierForValidatorAtIndex(size_t index) {
  if (index < GetTypeValidatorsContainer()->GetCount())
    return GetTypeValidatorsContainer()->GetTypeNameSpecifierAtIndex(index);
  else
    return GetRegexTypeValidatorsContainer()->GetTypeNameSpecifierAtIndex(
        index - GetTypeValidatorsContainer()->GetCount());
}

void TypeCategoryImpl::Enable(bool value, uint32_t position) {
  std::lock_guard<std::recursive_mutex> guard(m_mutex);
  if ((m_enabled = value))
    m_enabled_position = position;
  if (m_change_listener)
    m_change_listener->Changed();
}

std::string TypeCategoryImpl::GetDescription() {
  StreamString stream;
  stream.Printf("%s (%s", GetName(), (IsEnabled() ? "enabled" : "disabled"));
  StreamString lang_stream;
  lang_stream.Printf(", applicable for language(s): ");
  bool print_lang = false;
  for (size_t idx = 0; idx < GetNumLanguages(); idx++) {
    const lldb::LanguageType lang = GetLanguageAtIndex(idx);
    if (lang != lldb::eLanguageTypeUnknown)
      print_lang = true;
    lang_stream.Printf("%s%s", Language::GetNameForLanguageType(lang),
                       idx + 1 < GetNumLanguages() ? ", " : "");
  }
  if (print_lang)
    stream.PutCString(lang_stream.GetString());
  stream.PutChar(')');
  return stream.GetString();
}
