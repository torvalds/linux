//=== lib/CodeGen/GlobalISel/MipsPreLegalizerCombiner.cpp --------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This pass does combining of machine instructions at the generic MI level,
// before the legalizer.
//
//===----------------------------------------------------------------------===//

#include "MipsTargetMachine.h"
#include "llvm/CodeGen/GlobalISel/Combiner.h"
#include "llvm/CodeGen/GlobalISel/CombinerInfo.h"
#include "llvm/CodeGen/GlobalISel/MIPatternMatch.h"
#include "llvm/CodeGen/TargetPassConfig.h"

#define DEBUG_TYPE "mips-prelegalizer-combiner"

using namespace llvm;

namespace {
class MipsPreLegalizerCombinerInfo : public CombinerInfo {
public:
  MipsPreLegalizerCombinerInfo()
      : CombinerInfo(/*AllowIllegalOps*/ true, /*ShouldLegalizeIllegal*/ false,
                     /*LegalizerInfo*/ nullptr) {}
  virtual bool combine(GISelChangeObserver &Observer, MachineInstr &MI,
                       MachineIRBuilder &B) const override;
};

bool MipsPreLegalizerCombinerInfo::combine(GISelChangeObserver &Observer,
                                           MachineInstr &MI,
                                           MachineIRBuilder &B) const {
  return false;
}

// Pass boilerplate
// ================

class MipsPreLegalizerCombiner : public MachineFunctionPass {
public:
  static char ID;

  MipsPreLegalizerCombiner();

  StringRef getPassName() const override { return "MipsPreLegalizerCombiner"; }

  bool runOnMachineFunction(MachineFunction &MF) override;

  void getAnalysisUsage(AnalysisUsage &AU) const override;
};
} // end anonymous namespace

void MipsPreLegalizerCombiner::getAnalysisUsage(AnalysisUsage &AU) const {
  AU.addRequired<TargetPassConfig>();
  AU.setPreservesCFG();
  getSelectionDAGFallbackAnalysisUsage(AU);
  MachineFunctionPass::getAnalysisUsage(AU);
}

MipsPreLegalizerCombiner::MipsPreLegalizerCombiner() : MachineFunctionPass(ID) {
  initializeMipsPreLegalizerCombinerPass(*PassRegistry::getPassRegistry());
}

bool MipsPreLegalizerCombiner::runOnMachineFunction(MachineFunction &MF) {
  if (MF.getProperties().hasProperty(
          MachineFunctionProperties::Property::FailedISel))
    return false;
  auto *TPC = &getAnalysis<TargetPassConfig>();
  MipsPreLegalizerCombinerInfo PCInfo;
  Combiner C(PCInfo, TPC);
  return C.combineMachineInstrs(MF, nullptr);
}

char MipsPreLegalizerCombiner::ID = 0;
INITIALIZE_PASS_BEGIN(MipsPreLegalizerCombiner, DEBUG_TYPE,
                      "Combine Mips machine instrs before legalization", false,
                      false)
INITIALIZE_PASS_DEPENDENCY(TargetPassConfig)
INITIALIZE_PASS_END(MipsPreLegalizerCombiner, DEBUG_TYPE,
                    "Combine Mips machine instrs before legalization", false,
                    false)

namespace llvm {
FunctionPass *createMipsPreLegalizeCombiner() {
  return new MipsPreLegalizerCombiner();
}
} // end namespace llvm
