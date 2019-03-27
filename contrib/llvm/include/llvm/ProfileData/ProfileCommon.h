//===- ProfileCommon.h - Common profiling APIs. -----------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file contains data structures and functions common to both instrumented
// and sample profiling.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_PROFILEDATA_PROFILECOMMON_H
#define LLVM_PROFILEDATA_PROFILECOMMON_H

#include "llvm/ADT/ArrayRef.h"
#include "llvm/IR/ProfileSummary.h"
#include "llvm/ProfileData/InstrProf.h"
#include "llvm/Support/Error.h"
#include <algorithm>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <vector>

namespace llvm {

namespace sampleprof {

class FunctionSamples;

} // end namespace sampleprof

inline const char *getHotSectionPrefix() { return ".hot"; }
inline const char *getUnlikelySectionPrefix() { return ".unlikely"; }

class ProfileSummaryBuilder {
private:
  /// We keep track of the number of times a count (block count or samples)
  /// appears in the profile. The map is kept sorted in the descending order of
  /// counts.
  std::map<uint64_t, uint32_t, std::greater<uint64_t>> CountFrequencies;
  std::vector<uint32_t> DetailedSummaryCutoffs;

protected:
  SummaryEntryVector DetailedSummary;
  uint64_t TotalCount = 0;
  uint64_t MaxCount = 0;
  uint64_t MaxFunctionCount = 0;
  uint32_t NumCounts = 0;
  uint32_t NumFunctions = 0;

  ProfileSummaryBuilder(std::vector<uint32_t> Cutoffs)
      : DetailedSummaryCutoffs(std::move(Cutoffs)) {}
  ~ProfileSummaryBuilder() = default;

  inline void addCount(uint64_t Count);
  void computeDetailedSummary();

public:
  /// A vector of useful cutoff values for detailed summary.
  static const ArrayRef<uint32_t> DefaultCutoffs;
};

class InstrProfSummaryBuilder final : public ProfileSummaryBuilder {
  uint64_t MaxInternalBlockCount = 0;

  inline void addEntryCount(uint64_t Count);
  inline void addInternalCount(uint64_t Count);

public:
  InstrProfSummaryBuilder(std::vector<uint32_t> Cutoffs)
      : ProfileSummaryBuilder(std::move(Cutoffs)) {}

  void addRecord(const InstrProfRecord &);
  std::unique_ptr<ProfileSummary> getSummary();
};

class SampleProfileSummaryBuilder final : public ProfileSummaryBuilder {
public:
  SampleProfileSummaryBuilder(std::vector<uint32_t> Cutoffs)
      : ProfileSummaryBuilder(std::move(Cutoffs)) {}

  void addRecord(const sampleprof::FunctionSamples &FS);
  std::unique_ptr<ProfileSummary> getSummary();
};

/// This is called when a count is seen in the profile.
void ProfileSummaryBuilder::addCount(uint64_t Count) {
  TotalCount += Count;
  if (Count > MaxCount)
    MaxCount = Count;
  NumCounts++;
  CountFrequencies[Count]++;
}

} // end namespace llvm

#endif // LLVM_PROFILEDATA_PROFILECOMMON_H
