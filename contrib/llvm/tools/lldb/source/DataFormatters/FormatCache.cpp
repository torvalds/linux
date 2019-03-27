//===-- FormatCache.cpp ------------------------------------------*- C++
//-*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//




#include "lldb/DataFormatters/FormatCache.h"

using namespace lldb;
using namespace lldb_private;

FormatCache::Entry::Entry()
    : m_format_cached(false), m_summary_cached(false),
      m_synthetic_cached(false), m_validator_cached(false), m_format_sp(),
      m_summary_sp(), m_synthetic_sp(), m_validator_sp() {}

FormatCache::Entry::Entry(lldb::TypeFormatImplSP format_sp)
    : m_summary_cached(false), m_synthetic_cached(false),
      m_validator_cached(false), m_summary_sp(), m_synthetic_sp(),
      m_validator_sp() {
  SetFormat(format_sp);
}

FormatCache::Entry::Entry(lldb::TypeSummaryImplSP summary_sp)
    : m_format_cached(false), m_synthetic_cached(false),
      m_validator_cached(false), m_format_sp(), m_synthetic_sp(),
      m_validator_sp() {
  SetSummary(summary_sp);
}

FormatCache::Entry::Entry(lldb::SyntheticChildrenSP synthetic_sp)
    : m_format_cached(false), m_summary_cached(false),
      m_validator_cached(false), m_format_sp(), m_summary_sp(),
      m_validator_sp() {
  SetSynthetic(synthetic_sp);
}

FormatCache::Entry::Entry(lldb::TypeValidatorImplSP validator_sp)
    : m_format_cached(false), m_summary_cached(false),
      m_synthetic_cached(false), m_format_sp(), m_summary_sp(),
      m_synthetic_sp() {
  SetValidator(validator_sp);
}

FormatCache::Entry::Entry(lldb::TypeFormatImplSP format_sp,
                          lldb::TypeSummaryImplSP summary_sp,
                          lldb::SyntheticChildrenSP synthetic_sp,
                          lldb::TypeValidatorImplSP validator_sp) {
  SetFormat(format_sp);
  SetSummary(summary_sp);
  SetSynthetic(synthetic_sp);
  SetValidator(validator_sp);
}

bool FormatCache::Entry::IsFormatCached() { return m_format_cached; }

bool FormatCache::Entry::IsSummaryCached() { return m_summary_cached; }

bool FormatCache::Entry::IsSyntheticCached() { return m_synthetic_cached; }

bool FormatCache::Entry::IsValidatorCached() { return m_validator_cached; }

lldb::TypeFormatImplSP FormatCache::Entry::GetFormat() { return m_format_sp; }

lldb::TypeSummaryImplSP FormatCache::Entry::GetSummary() {
  return m_summary_sp;
}

lldb::SyntheticChildrenSP FormatCache::Entry::GetSynthetic() {
  return m_synthetic_sp;
}

lldb::TypeValidatorImplSP FormatCache::Entry::GetValidator() {
  return m_validator_sp;
}

void FormatCache::Entry::SetFormat(lldb::TypeFormatImplSP format_sp) {
  m_format_cached = true;
  m_format_sp = format_sp;
}

void FormatCache::Entry::SetSummary(lldb::TypeSummaryImplSP summary_sp) {
  m_summary_cached = true;
  m_summary_sp = summary_sp;
}

void FormatCache::Entry::SetSynthetic(lldb::SyntheticChildrenSP synthetic_sp) {
  m_synthetic_cached = true;
  m_synthetic_sp = synthetic_sp;
}

void FormatCache::Entry::SetValidator(lldb::TypeValidatorImplSP validator_sp) {
  m_validator_cached = true;
  m_validator_sp = validator_sp;
}

FormatCache::FormatCache()
    : m_map(), m_mutex()
#ifdef LLDB_CONFIGURATION_DEBUG
      ,
      m_cache_hits(0), m_cache_misses(0)
#endif
{
}

FormatCache::Entry &FormatCache::GetEntry(const ConstString &type) {
  auto i = m_map.find(type), e = m_map.end();
  if (i != e)
    return i->second;
  m_map[type] = FormatCache::Entry();
  return m_map[type];
}

bool FormatCache::GetFormat(const ConstString &type,
                            lldb::TypeFormatImplSP &format_sp) {
  std::lock_guard<std::recursive_mutex> guard(m_mutex);
  auto entry = GetEntry(type);
  if (entry.IsFormatCached()) {
#ifdef LLDB_CONFIGURATION_DEBUG
    m_cache_hits++;
#endif
    format_sp = entry.GetFormat();
    return true;
  }
#ifdef LLDB_CONFIGURATION_DEBUG
  m_cache_misses++;
#endif
  format_sp.reset();
  return false;
}

bool FormatCache::GetSummary(const ConstString &type,
                             lldb::TypeSummaryImplSP &summary_sp) {
  std::lock_guard<std::recursive_mutex> guard(m_mutex);
  auto entry = GetEntry(type);
  if (entry.IsSummaryCached()) {
#ifdef LLDB_CONFIGURATION_DEBUG
    m_cache_hits++;
#endif
    summary_sp = entry.GetSummary();
    return true;
  }
#ifdef LLDB_CONFIGURATION_DEBUG
  m_cache_misses++;
#endif
  summary_sp.reset();
  return false;
}

bool FormatCache::GetSynthetic(const ConstString &type,
                               lldb::SyntheticChildrenSP &synthetic_sp) {
  std::lock_guard<std::recursive_mutex> guard(m_mutex);
  auto entry = GetEntry(type);
  if (entry.IsSyntheticCached()) {
#ifdef LLDB_CONFIGURATION_DEBUG
    m_cache_hits++;
#endif
    synthetic_sp = entry.GetSynthetic();
    return true;
  }
#ifdef LLDB_CONFIGURATION_DEBUG
  m_cache_misses++;
#endif
  synthetic_sp.reset();
  return false;
}

bool FormatCache::GetValidator(const ConstString &type,
                               lldb::TypeValidatorImplSP &validator_sp) {
  std::lock_guard<std::recursive_mutex> guard(m_mutex);
  auto entry = GetEntry(type);
  if (entry.IsValidatorCached()) {
#ifdef LLDB_CONFIGURATION_DEBUG
    m_cache_hits++;
#endif
    validator_sp = entry.GetValidator();
    return true;
  }
#ifdef LLDB_CONFIGURATION_DEBUG
  m_cache_misses++;
#endif
  validator_sp.reset();
  return false;
}

void FormatCache::SetFormat(const ConstString &type,
                            lldb::TypeFormatImplSP &format_sp) {
  std::lock_guard<std::recursive_mutex> guard(m_mutex);
  GetEntry(type).SetFormat(format_sp);
}

void FormatCache::SetSummary(const ConstString &type,
                             lldb::TypeSummaryImplSP &summary_sp) {
  std::lock_guard<std::recursive_mutex> guard(m_mutex);
  GetEntry(type).SetSummary(summary_sp);
}

void FormatCache::SetSynthetic(const ConstString &type,
                               lldb::SyntheticChildrenSP &synthetic_sp) {
  std::lock_guard<std::recursive_mutex> guard(m_mutex);
  GetEntry(type).SetSynthetic(synthetic_sp);
}

void FormatCache::SetValidator(const ConstString &type,
                               lldb::TypeValidatorImplSP &validator_sp) {
  std::lock_guard<std::recursive_mutex> guard(m_mutex);
  GetEntry(type).SetValidator(validator_sp);
}

void FormatCache::Clear() {
  std::lock_guard<std::recursive_mutex> guard(m_mutex);
  m_map.clear();
}
