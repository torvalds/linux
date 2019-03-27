//===-- LanguageCategory.cpp ---------------------------------------*- C++
//-*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "lldb/DataFormatters/LanguageCategory.h"

#include "lldb/DataFormatters/FormatManager.h"
#include "lldb/DataFormatters/TypeCategory.h"
#include "lldb/DataFormatters/TypeFormat.h"
#include "lldb/DataFormatters/TypeSummary.h"
#include "lldb/DataFormatters/TypeSynthetic.h"
#include "lldb/DataFormatters/TypeValidator.h"
#include "lldb/Target/Language.h"

using namespace lldb;
using namespace lldb_private;

LanguageCategory::LanguageCategory(lldb::LanguageType lang_type)
    : m_category_sp(), m_hardcoded_formats(), m_hardcoded_summaries(),
      m_hardcoded_synthetics(), m_hardcoded_validators(), m_format_cache(),
      m_enabled(false) {
  if (Language *language_plugin = Language::FindPlugin(lang_type)) {
    m_category_sp = language_plugin->GetFormatters();
    m_hardcoded_formats = language_plugin->GetHardcodedFormats();
    m_hardcoded_summaries = language_plugin->GetHardcodedSummaries();
    m_hardcoded_synthetics = language_plugin->GetHardcodedSynthetics();
    m_hardcoded_validators = language_plugin->GetHardcodedValidators();
  }
  Enable();
}

bool LanguageCategory::Get(FormattersMatchData &match_data,
                           lldb::TypeFormatImplSP &format_sp) {
  if (!m_category_sp)
    return false;

  if (!IsEnabled())
    return false;

  if (match_data.GetTypeForCache()) {
    if (m_format_cache.GetFormat(match_data.GetTypeForCache(), format_sp))
      return format_sp.get() != nullptr;
  }

  ValueObject &valobj(match_data.GetValueObject());
  bool result =
      m_category_sp->Get(valobj, match_data.GetMatchesVector(), format_sp);
  if (match_data.GetTypeForCache() &&
      (!format_sp || !format_sp->NonCacheable())) {
    m_format_cache.SetFormat(match_data.GetTypeForCache(), format_sp);
  }
  return result;
}

bool LanguageCategory::Get(FormattersMatchData &match_data,
                           lldb::TypeSummaryImplSP &format_sp) {
  if (!m_category_sp)
    return false;

  if (!IsEnabled())
    return false;

  if (match_data.GetTypeForCache()) {
    if (m_format_cache.GetSummary(match_data.GetTypeForCache(), format_sp))
      return format_sp.get() != nullptr;
  }

  ValueObject &valobj(match_data.GetValueObject());
  bool result =
      m_category_sp->Get(valobj, match_data.GetMatchesVector(), format_sp);
  if (match_data.GetTypeForCache() &&
      (!format_sp || !format_sp->NonCacheable())) {
    m_format_cache.SetSummary(match_data.GetTypeForCache(), format_sp);
  }
  return result;
}

bool LanguageCategory::Get(FormattersMatchData &match_data,
                           lldb::SyntheticChildrenSP &format_sp) {
  if (!m_category_sp)
    return false;

  if (!IsEnabled())
    return false;

  if (match_data.GetTypeForCache()) {
    if (m_format_cache.GetSynthetic(match_data.GetTypeForCache(), format_sp))
      return format_sp.get() != nullptr;
  }

  ValueObject &valobj(match_data.GetValueObject());
  bool result =
      m_category_sp->Get(valobj, match_data.GetMatchesVector(), format_sp);
  if (match_data.GetTypeForCache() &&
      (!format_sp || !format_sp->NonCacheable())) {
    m_format_cache.SetSynthetic(match_data.GetTypeForCache(), format_sp);
  }
  return result;
}

bool LanguageCategory::Get(FormattersMatchData &match_data,
                           lldb::TypeValidatorImplSP &format_sp) {
  if (!m_category_sp)
    return false;

  if (!IsEnabled())
    return false;

  if (match_data.GetTypeForCache()) {
    if (m_format_cache.GetValidator(match_data.GetTypeForCache(), format_sp))
      return format_sp.get() != nullptr;
  }

  ValueObject &valobj(match_data.GetValueObject());
  bool result =
      m_category_sp->Get(valobj, match_data.GetMatchesVector(), format_sp);
  if (match_data.GetTypeForCache() &&
      (!format_sp || !format_sp->NonCacheable())) {
    m_format_cache.SetValidator(match_data.GetTypeForCache(), format_sp);
  }
  return result;
}

bool LanguageCategory::GetHardcoded(FormatManager &fmt_mgr,
                                    FormattersMatchData &match_data,
                                    lldb::TypeFormatImplSP &format_sp) {
  if (!IsEnabled())
    return false;

  ValueObject &valobj(match_data.GetValueObject());
  lldb::DynamicValueType use_dynamic(match_data.GetDynamicValueType());

  for (auto &candidate : m_hardcoded_formats) {
    if ((format_sp = candidate(valobj, use_dynamic, fmt_mgr)))
      break;
  }
  if (match_data.GetTypeForCache() &&
      (!format_sp || !format_sp->NonCacheable())) {
    m_format_cache.SetFormat(match_data.GetTypeForCache(), format_sp);
  }
  return format_sp.get() != nullptr;
}

bool LanguageCategory::GetHardcoded(FormatManager &fmt_mgr,
                                    FormattersMatchData &match_data,
                                    lldb::TypeSummaryImplSP &format_sp) {
  if (!IsEnabled())
    return false;

  ValueObject &valobj(match_data.GetValueObject());
  lldb::DynamicValueType use_dynamic(match_data.GetDynamicValueType());

  for (auto &candidate : m_hardcoded_summaries) {
    if ((format_sp = candidate(valobj, use_dynamic, fmt_mgr)))
      break;
  }
  if (match_data.GetTypeForCache() &&
      (!format_sp || !format_sp->NonCacheable())) {
    m_format_cache.SetSummary(match_data.GetTypeForCache(), format_sp);
  }
  return format_sp.get() != nullptr;
}

bool LanguageCategory::GetHardcoded(FormatManager &fmt_mgr,
                                    FormattersMatchData &match_data,
                                    lldb::SyntheticChildrenSP &format_sp) {
  if (!IsEnabled())
    return false;

  ValueObject &valobj(match_data.GetValueObject());
  lldb::DynamicValueType use_dynamic(match_data.GetDynamicValueType());

  for (auto &candidate : m_hardcoded_synthetics) {
    if ((format_sp = candidate(valobj, use_dynamic, fmt_mgr)))
      break;
  }
  if (match_data.GetTypeForCache() &&
      (!format_sp || !format_sp->NonCacheable())) {
    m_format_cache.SetSynthetic(match_data.GetTypeForCache(), format_sp);
  }
  return format_sp.get() != nullptr;
}

bool LanguageCategory::GetHardcoded(FormatManager &fmt_mgr,
                                    FormattersMatchData &match_data,
                                    lldb::TypeValidatorImplSP &format_sp) {
  if (!IsEnabled())
    return false;

  ValueObject &valobj(match_data.GetValueObject());
  lldb::DynamicValueType use_dynamic(match_data.GetDynamicValueType());

  for (auto &candidate : m_hardcoded_validators) {
    if ((format_sp = candidate(valobj, use_dynamic, fmt_mgr)))
      break;
  }
  if (match_data.GetTypeForCache() &&
      (!format_sp || !format_sp->NonCacheable())) {
    m_format_cache.SetValidator(match_data.GetTypeForCache(), format_sp);
  }
  return format_sp.get() != nullptr;
}

lldb::TypeCategoryImplSP LanguageCategory::GetCategory() const {
  return m_category_sp;
}

FormatCache &LanguageCategory::GetFormatCache() { return m_format_cache; }

void LanguageCategory::Enable() {
  if (m_category_sp)
    m_category_sp->Enable(true, TypeCategoryMap::Default);
  m_enabled = true;
}

void LanguageCategory::Disable() {
  if (m_category_sp)
    m_category_sp->Disable();
  m_enabled = false;
}

bool LanguageCategory::IsEnabled() { return m_enabled; }
