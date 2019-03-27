//=-- ProfilesummaryBuilder.cpp - Profile summary computation ---------------=//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file contains support for computing profile summary data.
//
//===----------------------------------------------------------------------===//

#include "llvm/IR/Attributes.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/Metadata.h"
#include "llvm/IR/Type.h"
#include "llvm/ProfileData/InstrProf.h"
#include "llvm/ProfileData/ProfileCommon.h"
#include "llvm/ProfileData/SampleProf.h"
#include "llvm/Support/Casting.h"

using namespace llvm;

// A set of cutoff values. Each value, when divided by ProfileSummary::Scale
// (which is 1000000) is a desired percentile of total counts.
static const uint32_t DefaultCutoffsData[] = {
    10000,  /*  1% */
    100000, /* 10% */
    200000, 300000, 400000, 500000, 600000, 700000, 800000,
    900000, 950000, 990000, 999000, 999900, 999990, 999999};
const ArrayRef<uint32_t> ProfileSummaryBuilder::DefaultCutoffs =
    DefaultCutoffsData;

void InstrProfSummaryBuilder::addRecord(const InstrProfRecord &R) {
  // The first counter is not necessarily an entry count for IR
  // instrumentation profiles.
  // Eventually MaxFunctionCount will become obsolete and this can be
  // removed.
  addEntryCount(R.Counts[0]);
  for (size_t I = 1, E = R.Counts.size(); I < E; ++I)
    addInternalCount(R.Counts[I]);
}

// To compute the detailed summary, we consider each line containing samples as
// equivalent to a block with a count in the instrumented profile.
void SampleProfileSummaryBuilder::addRecord(
    const sampleprof::FunctionSamples &FS) {
  NumFunctions++;
  if (FS.getHeadSamples() > MaxFunctionCount)
    MaxFunctionCount = FS.getHeadSamples();
  for (const auto &I : FS.getBodySamples())
    addCount(I.second.getSamples());
}

// The argument to this method is a vector of cutoff percentages and the return
// value is a vector of (Cutoff, MinCount, NumCounts) triplets.
void ProfileSummaryBuilder::computeDetailedSummary() {
  if (DetailedSummaryCutoffs.empty())
    return;
  llvm::sort(DetailedSummaryCutoffs);
  auto Iter = CountFrequencies.begin();
  const auto End = CountFrequencies.end();

  uint32_t CountsSeen = 0;
  uint64_t CurrSum = 0, Count = 0;

  for (const uint32_t Cutoff : DetailedSummaryCutoffs) {
    assert(Cutoff <= 999999);
    APInt Temp(128, TotalCount);
    APInt N(128, Cutoff);
    APInt D(128, ProfileSummary::Scale);
    Temp *= N;
    Temp = Temp.sdiv(D);
    uint64_t DesiredCount = Temp.getZExtValue();
    assert(DesiredCount <= TotalCount);
    while (CurrSum < DesiredCount && Iter != End) {
      Count = Iter->first;
      uint32_t Freq = Iter->second;
      CurrSum += (Count * Freq);
      CountsSeen += Freq;
      Iter++;
    }
    assert(CurrSum >= DesiredCount);
    ProfileSummaryEntry PSE = {Cutoff, Count, CountsSeen};
    DetailedSummary.push_back(PSE);
  }
}

std::unique_ptr<ProfileSummary> SampleProfileSummaryBuilder::getSummary() {
  computeDetailedSummary();
  return llvm::make_unique<ProfileSummary>(
      ProfileSummary::PSK_Sample, DetailedSummary, TotalCount, MaxCount, 0,
      MaxFunctionCount, NumCounts, NumFunctions);
}

std::unique_ptr<ProfileSummary> InstrProfSummaryBuilder::getSummary() {
  computeDetailedSummary();
  return llvm::make_unique<ProfileSummary>(
      ProfileSummary::PSK_Instr, DetailedSummary, TotalCount, MaxCount,
      MaxInternalBlockCount, MaxFunctionCount, NumCounts, NumFunctions);
}

void InstrProfSummaryBuilder::addEntryCount(uint64_t Count) {
  addCount(Count);
  NumFunctions++;
  if (Count > MaxFunctionCount)
    MaxFunctionCount = Count;
}

void InstrProfSummaryBuilder::addInternalCount(uint64_t Count) {
  addCount(Count);
  if (Count > MaxInternalBlockCount)
    MaxInternalBlockCount = Count;
}
