//===- MachineBlockFrequencyInfo.h - MBB Frequency Analysis -----*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// Loops should be simplified before this analysis.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CODEGEN_MACHINEBLOCKFREQUENCYINFO_H
#define LLVM_CODEGEN_MACHINEBLOCKFREQUENCYINFO_H

#include "llvm/ADT/Optional.h"
#include "llvm/CodeGen/MachineFunctionPass.h"
#include "llvm/Support/BlockFrequency.h"
#include <cstdint>
#include <memory>

namespace llvm {

template <class BlockT> class BlockFrequencyInfoImpl;
class MachineBasicBlock;
class MachineBranchProbabilityInfo;
class MachineFunction;
class MachineLoopInfo;
class raw_ostream;

/// MachineBlockFrequencyInfo pass uses BlockFrequencyInfoImpl implementation
/// to estimate machine basic block frequencies.
class MachineBlockFrequencyInfo : public MachineFunctionPass {
  using ImplType = BlockFrequencyInfoImpl<MachineBasicBlock>;
  std::unique_ptr<ImplType> MBFI;

public:
  static char ID;

  MachineBlockFrequencyInfo();
  ~MachineBlockFrequencyInfo() override;

  void getAnalysisUsage(AnalysisUsage &AU) const override;

  bool runOnMachineFunction(MachineFunction &F) override;

  /// calculate - compute block frequency info for the given function.
  void calculate(const MachineFunction &F,
                 const MachineBranchProbabilityInfo &MBPI,
                 const MachineLoopInfo &MLI);

  void releaseMemory() override;

  /// getblockFreq - Return block frequency. Return 0 if we don't have the
  /// information. Please note that initial frequency is equal to 1024. It means
  /// that we should not rely on the value itself, but only on the comparison to
  /// the other block frequencies. We do this to avoid using of floating points.
  ///
  BlockFrequency getBlockFreq(const MachineBasicBlock *MBB) const;

  Optional<uint64_t> getBlockProfileCount(const MachineBasicBlock *MBB) const;
  Optional<uint64_t> getProfileCountFromFreq(uint64_t Freq) const;

  bool isIrrLoopHeader(const MachineBasicBlock *MBB);

  const MachineFunction *getFunction() const;
  const MachineBranchProbabilityInfo *getMBPI() const;
  void view(const Twine &Name, bool isSimple = true) const;

  // Print the block frequency Freq to OS using the current functions entry
  // frequency to convert freq into a relative decimal form.
  raw_ostream &printBlockFreq(raw_ostream &OS, const BlockFrequency Freq) const;

  // Convenience method that attempts to look up the frequency associated with
  // BB and print it to OS.
  raw_ostream &printBlockFreq(raw_ostream &OS,
                              const MachineBasicBlock *MBB) const;

  uint64_t getEntryFreq() const;
};

} // end namespace llvm

#endif // LLVM_CODEGEN_MACHINEBLOCKFREQUENCYINFO_H
