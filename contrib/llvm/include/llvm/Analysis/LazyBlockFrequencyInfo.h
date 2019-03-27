//===- LazyBlockFrequencyInfo.h - Lazy Block Frequency Analysis -*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This is an alternative analysis pass to BlockFrequencyInfoWrapperPass.  The
// difference is that with this pass the block frequencies are not computed when
// the analysis pass is executed but rather when the BFI result is explicitly
// requested by the analysis client.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_ANALYSIS_LAZYBLOCKFREQUENCYINFO_H
#define LLVM_ANALYSIS_LAZYBLOCKFREQUENCYINFO_H

#include "llvm/Analysis/BlockFrequencyInfo.h"
#include "llvm/Analysis/LazyBranchProbabilityInfo.h"
#include "llvm/Pass.h"

namespace llvm {
class AnalysisUsage;
class BranchProbabilityInfo;
class Function;
class LoopInfo;

/// Wraps a BFI to allow lazy computation of the block frequencies.
///
/// A pass that only conditionally uses BFI can uncondtionally require the
/// analysis without paying for the overhead if BFI doesn't end up being used.
template <typename FunctionT, typename BranchProbabilityInfoPassT,
          typename LoopInfoT, typename BlockFrequencyInfoT>
class LazyBlockFrequencyInfo {
public:
  LazyBlockFrequencyInfo()
      : Calculated(false), F(nullptr), BPIPass(nullptr), LI(nullptr) {}

  /// Set up the per-function input.
  void setAnalysis(const FunctionT *F, BranchProbabilityInfoPassT *BPIPass,
                   const LoopInfoT *LI) {
    this->F = F;
    this->BPIPass = BPIPass;
    this->LI = LI;
  }

  /// Retrieve the BFI with the block frequencies computed.
  BlockFrequencyInfoT &getCalculated() {
    if (!Calculated) {
      assert(F && BPIPass && LI && "call setAnalysis");
      BFI.calculate(
          *F, BPIPassTrait<BranchProbabilityInfoPassT>::getBPI(BPIPass), *LI);
      Calculated = true;
    }
    return BFI;
  }

  const BlockFrequencyInfoT &getCalculated() const {
    return const_cast<LazyBlockFrequencyInfo *>(this)->getCalculated();
  }

  void releaseMemory() {
    BFI.releaseMemory();
    Calculated = false;
    setAnalysis(nullptr, nullptr, nullptr);
  }

private:
  BlockFrequencyInfoT BFI;
  bool Calculated;
  const FunctionT *F;
  BranchProbabilityInfoPassT *BPIPass;
  const LoopInfoT *LI;
};

/// This is an alternative analysis pass to
/// BlockFrequencyInfoWrapperPass.  The difference is that with this pass the
/// block frequencies are not computed when the analysis pass is executed but
/// rather when the BFI result is explicitly requested by the analysis client.
///
/// There are some additional requirements for any client pass that wants to use
/// the analysis:
///
/// 1. The pass needs to initialize dependent passes with:
///
///   INITIALIZE_PASS_DEPENDENCY(LazyBFIPass)
///
/// 2. Similarly, getAnalysisUsage should call:
///
///   LazyBlockFrequencyInfoPass::getLazyBFIAnalysisUsage(AU)
///
/// 3. The computed BFI should be requested with
///    getAnalysis<LazyBlockFrequencyInfoPass>().getBFI() before either LoopInfo
///    or BPI could be invalidated for example by changing the CFG.
///
/// Note that it is expected that we wouldn't need this functionality for the
/// new PM since with the new PM, analyses are executed on demand.

class LazyBlockFrequencyInfoPass : public FunctionPass {
private:
  LazyBlockFrequencyInfo<Function, LazyBranchProbabilityInfoPass, LoopInfo,
                         BlockFrequencyInfo>
      LBFI;

public:
  static char ID;

  LazyBlockFrequencyInfoPass();

  /// Compute and return the block frequencies.
  BlockFrequencyInfo &getBFI() { return LBFI.getCalculated(); }

  /// Compute and return the block frequencies.
  const BlockFrequencyInfo &getBFI() const { return LBFI.getCalculated(); }

  void getAnalysisUsage(AnalysisUsage &AU) const override;

  /// Helper for client passes to set up the analysis usage on behalf of this
  /// pass.
  static void getLazyBFIAnalysisUsage(AnalysisUsage &AU);

  bool runOnFunction(Function &F) override;
  void releaseMemory() override;
  void print(raw_ostream &OS, const Module *M) const override;
};

/// Helper for client passes to initialize dependent passes for LBFI.
void initializeLazyBFIPassPass(PassRegistry &Registry);
}
#endif
