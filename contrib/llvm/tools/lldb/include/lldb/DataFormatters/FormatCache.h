//===-- FormatCache.h ---------------------------------------------*- C++
//-*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef lldb_FormatCache_h_
#define lldb_FormatCache_h_

#include <map>
#include <mutex>

#include "lldb/Utility/ConstString.h"
#include "lldb/lldb-public.h"

namespace lldb_private {
class FormatCache {
private:
  struct Entry {
  private:
    bool m_format_cached : 1;
    bool m_summary_cached : 1;
    bool m_synthetic_cached : 1;
    bool m_validator_cached : 1;

    lldb::TypeFormatImplSP m_format_sp;
    lldb::TypeSummaryImplSP m_summary_sp;
    lldb::SyntheticChildrenSP m_synthetic_sp;
    lldb::TypeValidatorImplSP m_validator_sp;

  public:
    Entry();
    Entry(lldb::TypeFormatImplSP);
    Entry(lldb::TypeSummaryImplSP);
    Entry(lldb::SyntheticChildrenSP);
    Entry(lldb::TypeValidatorImplSP);
    Entry(lldb::TypeFormatImplSP, lldb::TypeSummaryImplSP,
          lldb::SyntheticChildrenSP, lldb::TypeValidatorImplSP);

    bool IsFormatCached();

    bool IsSummaryCached();

    bool IsSyntheticCached();

    bool IsValidatorCached();

    lldb::TypeFormatImplSP GetFormat();

    lldb::TypeSummaryImplSP GetSummary();

    lldb::SyntheticChildrenSP GetSynthetic();

    lldb::TypeValidatorImplSP GetValidator();

    void SetFormat(lldb::TypeFormatImplSP);

    void SetSummary(lldb::TypeSummaryImplSP);

    void SetSynthetic(lldb::SyntheticChildrenSP);

    void SetValidator(lldb::TypeValidatorImplSP);
  };
  typedef std::map<ConstString, Entry> CacheMap;
  CacheMap m_map;
  std::recursive_mutex m_mutex;

  uint64_t m_cache_hits;
  uint64_t m_cache_misses;

  Entry &GetEntry(const ConstString &type);

public:
  FormatCache();

  bool GetFormat(const ConstString &type, lldb::TypeFormatImplSP &format_sp);

  bool GetSummary(const ConstString &type, lldb::TypeSummaryImplSP &summary_sp);

  bool GetSynthetic(const ConstString &type,
                    lldb::SyntheticChildrenSP &synthetic_sp);

  bool GetValidator(const ConstString &type,
                    lldb::TypeValidatorImplSP &summary_sp);

  void SetFormat(const ConstString &type, lldb::TypeFormatImplSP &format_sp);

  void SetSummary(const ConstString &type, lldb::TypeSummaryImplSP &summary_sp);

  void SetSynthetic(const ConstString &type,
                    lldb::SyntheticChildrenSP &synthetic_sp);

  void SetValidator(const ConstString &type,
                    lldb::TypeValidatorImplSP &synthetic_sp);

  void Clear();

  uint64_t GetCacheHits() { return m_cache_hits; }

  uint64_t GetCacheMisses() { return m_cache_misses; }
};
} // namespace lldb_private

#endif // lldb_FormatCache_h_
