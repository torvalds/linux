///===- LazyMachineBlockFrequencyInfo.h - Lazy Block Frequency -*- C++ -*--===//
///
///                     The LLVM Compiler Infrastructure
///
/// This file is distributed under the University of Illinois Open Source
/// License. See LICENSE.TXT for details.
///
///===---------------------------------------------------------------------===//
/// \file
/// This is an alternative analysis pass to MachineBlockFrequencyInfo.  The
/// difference is that with this pass the block frequencies are not computed
/// when the analysis pass is executed but rather when the BFI result is
/// explicitly requested by the analysis client.
///
///===---------------------------------------------------------------------===//

#ifndef LLVM_ANALYSIS_LAZYMACHINEBLOCKFREQUENCYINFO_H
#define LLVM_ANALYSIS_LAZYMACHINEBLOCKFREQUENCYINFO_H

#include "llvm/CodeGen/MachineBlockFrequencyInfo.h"
#include "llvm/CodeGen/MachineBranchProbabilityInfo.h"
#include "llvm/CodeGen/MachineDominators.h"
#include "llvm/CodeGen/MachineLoopInfo.h"

namespace llvm {
/// This is an alternative analysis pass to MachineBlockFrequencyInfo.
/// The difference is that with this pass, the block frequencies are not
/// computed when the analysis pass is executed but rather when the BFI result
/// is explicitly requested by the analysis client.
///
/// This works by checking querying if MBFI is available and otherwise
/// generating MBFI on the fly.  In this case the passes required for (LI, DT)
/// are also queried before being computed on the fly.
///
/// Note that it is expected that we wouldn't need this functionality for the
/// new PM since with the new PM, analyses are executed on demand.

class LazyMachineBlockFrequencyInfoPass : public MachineFunctionPass {
private:
  /// If generated on the fly this own the instance.
  mutable std::unique_ptr<MachineBlockFrequencyInfo> OwnedMBFI;

  /// If generated on the fly this own the instance.
  mutable std::unique_ptr<MachineLoopInfo> OwnedMLI;

  /// If generated on the fly this own the instance.
  mutable std::unique_ptr<MachineDominatorTree> OwnedMDT;

  /// The function.
  MachineFunction *MF = nullptr;

  /// Calculate MBFI and all other analyses that's not available and
  /// required by BFI.
  MachineBlockFrequencyInfo &calculateIfNotAvailable() const;

public:
  static char ID;

  LazyMachineBlockFrequencyInfoPass();

  /// Compute and return the block frequencies.
  MachineBlockFrequencyInfo &getBFI() { return calculateIfNotAvailable(); }

  /// Compute and return the block frequencies.
  const MachineBlockFrequencyInfo &getBFI() const {
    return calculateIfNotAvailable();
  }

  void getAnalysisUsage(AnalysisUsage &AU) const override;

  bool runOnMachineFunction(MachineFunction &F) override;
  void releaseMemory() override;
  void print(raw_ostream &OS, const Module *M) const override;
};
}
#endif
