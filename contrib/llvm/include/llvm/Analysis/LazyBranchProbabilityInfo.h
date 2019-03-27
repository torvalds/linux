//===- LazyBranchProbabilityInfo.h - Lazy Branch Probability ----*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This is an alternative analysis pass to BranchProbabilityInfoWrapperPass.
// The difference is that with this pass the branch probabilities are not
// computed when the analysis pass is executed but rather when the BPI results
// is explicitly requested by the analysis client.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_ANALYSIS_LAZYBRANCHPROBABILITYINFO_H
#define LLVM_ANALYSIS_LAZYBRANCHPROBABILITYINFO_H

#include "llvm/Analysis/BranchProbabilityInfo.h"
#include "llvm/Pass.h"

namespace llvm {
class AnalysisUsage;
class Function;
class LoopInfo;
class TargetLibraryInfo;

/// This is an alternative analysis pass to
/// BranchProbabilityInfoWrapperPass.  The difference is that with this pass the
/// branch probabilities are not computed when the analysis pass is executed but
/// rather when the BPI results is explicitly requested by the analysis client.
///
/// There are some additional requirements for any client pass that wants to use
/// the analysis:
///
/// 1. The pass needs to initialize dependent passes with:
///
///   INITIALIZE_PASS_DEPENDENCY(LazyBPIPass)
///
/// 2. Similarly, getAnalysisUsage should call:
///
///   LazyBranchProbabilityInfoPass::getLazyBPIAnalysisUsage(AU)
///
/// 3. The computed BPI should be requested with
///    getAnalysis<LazyBranchProbabilityInfoPass>().getBPI() before LoopInfo
///    could be invalidated for example by changing the CFG.
///
/// Note that it is expected that we wouldn't need this functionality for the
/// new PM since with the new PM, analyses are executed on demand.
class LazyBranchProbabilityInfoPass : public FunctionPass {

  /// Wraps a BPI to allow lazy computation of the branch probabilities.
  ///
  /// A pass that only conditionally uses BPI can uncondtionally require the
  /// analysis without paying for the overhead if BPI doesn't end up being used.
  class LazyBranchProbabilityInfo {
  public:
    LazyBranchProbabilityInfo(const Function *F, const LoopInfo *LI,
                              const TargetLibraryInfo *TLI)
        : Calculated(false), F(F), LI(LI), TLI(TLI) {}

    /// Retrieve the BPI with the branch probabilities computed.
    BranchProbabilityInfo &getCalculated() {
      if (!Calculated) {
        assert(F && LI && "call setAnalysis");
        BPI.calculate(*F, *LI, TLI);
        Calculated = true;
      }
      return BPI;
    }

    const BranchProbabilityInfo &getCalculated() const {
      return const_cast<LazyBranchProbabilityInfo *>(this)->getCalculated();
    }

  private:
    BranchProbabilityInfo BPI;
    bool Calculated;
    const Function *F;
    const LoopInfo *LI;
    const TargetLibraryInfo *TLI;
  };

  std::unique_ptr<LazyBranchProbabilityInfo> LBPI;

public:
  static char ID;

  LazyBranchProbabilityInfoPass();

  /// Compute and return the branch probabilities.
  BranchProbabilityInfo &getBPI() { return LBPI->getCalculated(); }

  /// Compute and return the branch probabilities.
  const BranchProbabilityInfo &getBPI() const { return LBPI->getCalculated(); }

  void getAnalysisUsage(AnalysisUsage &AU) const override;

  /// Helper for client passes to set up the analysis usage on behalf of this
  /// pass.
  static void getLazyBPIAnalysisUsage(AnalysisUsage &AU);

  bool runOnFunction(Function &F) override;
  void releaseMemory() override;
  void print(raw_ostream &OS, const Module *M) const override;
};

/// Helper for client passes to initialize dependent passes for LBPI.
void initializeLazyBPIPassPass(PassRegistry &Registry);

/// Simple trait class that provides a mapping between BPI passes and the
/// corresponding BPInfo.
template <typename PassT> struct BPIPassTrait {
  static PassT &getBPI(PassT *P) { return *P; }
};

template <> struct BPIPassTrait<LazyBranchProbabilityInfoPass> {
  static BranchProbabilityInfo &getBPI(LazyBranchProbabilityInfoPass *P) {
    return P->getBPI();
  }
};
}
#endif
