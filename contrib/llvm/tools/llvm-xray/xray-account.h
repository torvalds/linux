//===- xray-account.h - XRay Function Call Accounting ---------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file defines the interface for performing some basic function call
// accounting from an XRay trace.
//
//===----------------------------------------------------------------------===//
#ifndef LLVM_TOOLS_LLVM_XRAY_XRAY_ACCOUNT_H
#define LLVM_TOOLS_LLVM_XRAY_XRAY_ACCOUNT_H

#include <map>
#include <utility>
#include <vector>

#include "func-id-helper.h"
#include "llvm/Support/Program.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/XRay/XRayRecord.h"

namespace llvm {
namespace xray {

class LatencyAccountant {
public:
  typedef std::map<int32_t, std::vector<uint64_t>> FunctionLatencyMap;
  typedef std::map<llvm::sys::procid_t, std::pair<uint64_t, uint64_t>>
      PerThreadMinMaxTSCMap;
  typedef std::map<uint8_t, std::pair<uint64_t, uint64_t>> PerCPUMinMaxTSCMap;
  typedef std::vector<std::pair<int32_t, uint64_t>> FunctionStack;
  typedef std::map<llvm::sys::procid_t, FunctionStack>
      PerThreadFunctionStackMap;

private:
  PerThreadFunctionStackMap PerThreadFunctionStack;
  FunctionLatencyMap FunctionLatencies;
  PerThreadMinMaxTSCMap PerThreadMinMaxTSC;
  PerCPUMinMaxTSCMap PerCPUMinMaxTSC;
  FuncIdConversionHelper &FuncIdHelper;

  bool DeduceSiblingCalls = false;
  uint64_t CurrentMaxTSC = 0;

  void recordLatency(int32_t FuncId, uint64_t Latency) {
    FunctionLatencies[FuncId].push_back(Latency);
  }

public:
  explicit LatencyAccountant(FuncIdConversionHelper &FuncIdHelper,
                             bool DeduceSiblingCalls)
      : FuncIdHelper(FuncIdHelper), DeduceSiblingCalls(DeduceSiblingCalls) {}

  const FunctionLatencyMap &getFunctionLatencies() const {
    return FunctionLatencies;
  }

  const PerThreadMinMaxTSCMap &getPerThreadMinMaxTSC() const {
    return PerThreadMinMaxTSC;
  }

  const PerCPUMinMaxTSCMap &getPerCPUMinMaxTSC() const {
    return PerCPUMinMaxTSC;
  }

  /// Returns false in case we fail to account the provided record. This happens
  /// in the following cases:
  ///
  ///   - An exit record does not match any entry records for the same function.
  ///     If we've been set to deduce sibling calls, we try walking up the stack
  ///     and recording times for the higher level functions.
  ///   - A record has a TSC that's before the latest TSC that has been
  ///     recorded. We still record the TSC for the min-max.
  ///
  bool accountRecord(const XRayRecord &Record);

  const FunctionStack *getThreadFunctionStack(llvm::sys::procid_t TId) const {
    auto I = PerThreadFunctionStack.find(TId);
    if (I == PerThreadFunctionStack.end())
      return nullptr;
    return &I->second;
  }

  const PerThreadFunctionStackMap &getPerThreadFunctionStack() const {
    return PerThreadFunctionStack;
  }

  // Output Functions
  // ================

  void exportStatsAsText(raw_ostream &OS, const XRayFileHeader &Header) const;
  void exportStatsAsCSV(raw_ostream &OS, const XRayFileHeader &Header) const;

private:
  // Internal helper to implement common parts of the exportStatsAs...
  // functions.
  template <class F> void exportStats(const XRayFileHeader &Header, F fn) const;
};

} // namespace xray
} // namespace llvm

#endif // LLVM_TOOLS_LLVM_XRAY_XRAY_ACCOUNT_H
