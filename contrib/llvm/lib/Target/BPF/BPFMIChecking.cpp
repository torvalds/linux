//===-------------- BPFMIChecking.cpp - MI Checking Legality -------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This pass performs checking to signal errors for certain illegal usages at
// MachineInstruction layer. Specially, the result of XADD{32,64} insn should
// not be used. The pass is done at the PreEmit pass right before the
// machine code is emitted at which point the register liveness information
// is still available.
//
//===----------------------------------------------------------------------===//

#include "BPF.h"
#include "BPFInstrInfo.h"
#include "BPFTargetMachine.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"

using namespace llvm;

#define DEBUG_TYPE "bpf-mi-checking"

namespace {

struct BPFMIPreEmitChecking : public MachineFunctionPass {

  static char ID;
  MachineFunction *MF;
  const TargetRegisterInfo *TRI;

  BPFMIPreEmitChecking() : MachineFunctionPass(ID) {
    initializeBPFMIPreEmitCheckingPass(*PassRegistry::getPassRegistry());
  }

private:
  // Initialize class variables.
  void initialize(MachineFunction &MFParm);

  void checkingIllegalXADD(void);

public:

  // Main entry point for this pass.
  bool runOnMachineFunction(MachineFunction &MF) override {
    if (!skipFunction(MF.getFunction())) {
      initialize(MF);
      checkingIllegalXADD();
    }
    return false;
  }
};

// Initialize class variables.
void BPFMIPreEmitChecking::initialize(MachineFunction &MFParm) {
  MF = &MFParm;
  TRI = MF->getSubtarget<BPFSubtarget>().getRegisterInfo();
  LLVM_DEBUG(dbgs() << "*** BPF PreEmit checking pass ***\n\n");
}

void BPFMIPreEmitChecking::checkingIllegalXADD(void) {
  for (MachineBasicBlock &MBB : *MF) {
    for (MachineInstr &MI : MBB) {
      if (MI.getOpcode() != BPF::XADD32 && MI.getOpcode() != BPF::XADD64)
        continue;

      LLVM_DEBUG(MI.dump());
      if (!MI.allDefsAreDead()) {
        DebugLoc Empty;
        const DebugLoc &DL = MI.getDebugLoc();
        if (DL != Empty)
          report_fatal_error("line " + std::to_string(DL.getLine()) +
                             ": Invalid usage of the XADD return value", false);
        else
          report_fatal_error("Invalid usage of the XADD return value", false);
      }
    }
  }

  return;
}

} // end default namespace

INITIALIZE_PASS(BPFMIPreEmitChecking, "bpf-mi-pemit-checking",
                "BPF PreEmit Checking", false, false)

char BPFMIPreEmitChecking::ID = 0;
FunctionPass* llvm::createBPFMIPreEmitCheckingPass()
{
  return new BPFMIPreEmitChecking();
}
