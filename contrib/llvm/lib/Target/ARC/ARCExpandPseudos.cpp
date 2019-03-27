//===- ARCExpandPseudosPass - ARC expand pseudo loads -----------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This pass expands stores with large offsets into an appropriate sequence.
//===----------------------------------------------------------------------===//

#include "ARC.h"
#include "ARCInstrInfo.h"
#include "ARCRegisterInfo.h"
#include "ARCSubtarget.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/CodeGen/MachineFunctionPass.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"

using namespace llvm;

#define DEBUG_TYPE "arc-expand-pseudos"

namespace {

class ARCExpandPseudos : public MachineFunctionPass {
public:
  static char ID;
  ARCExpandPseudos() : MachineFunctionPass(ID) {}

  bool runOnMachineFunction(MachineFunction &Fn) override;

  StringRef getPassName() const override { return "ARC Expand Pseudos"; }

private:
  void ExpandStore(MachineFunction &, MachineBasicBlock::iterator);

  const ARCInstrInfo *TII;
};

char ARCExpandPseudos::ID = 0;

} // end anonymous namespace

static unsigned getMappedOp(unsigned PseudoOp) {
  switch (PseudoOp) {
  case ARC::ST_FAR:
    return ARC::ST_rs9;
  case ARC::STH_FAR:
    return ARC::STH_rs9;
  case ARC::STB_FAR:
    return ARC::STB_rs9;
  default:
    llvm_unreachable("Unhandled pseudo op.");
  }
}

void ARCExpandPseudos::ExpandStore(MachineFunction &MF,
                                   MachineBasicBlock::iterator SII) {
  MachineInstr &SI = *SII;
  unsigned AddrReg = MF.getRegInfo().createVirtualRegister(&ARC::GPR32RegClass);
  unsigned AddOpc =
      isUInt<6>(SI.getOperand(2).getImm()) ? ARC::ADD_rru6 : ARC::ADD_rrlimm;
  BuildMI(*SI.getParent(), SI, SI.getDebugLoc(), TII->get(AddOpc), AddrReg)
      .addReg(SI.getOperand(1).getReg())
      .addImm(SI.getOperand(2).getImm());
  BuildMI(*SI.getParent(), SI, SI.getDebugLoc(),
          TII->get(getMappedOp(SI.getOpcode())))
      .addReg(SI.getOperand(0).getReg())
      .addReg(AddrReg)
      .addImm(0);
  SI.eraseFromParent();
}

bool ARCExpandPseudos::runOnMachineFunction(MachineFunction &MF) {
  const ARCSubtarget *STI = &MF.getSubtarget<ARCSubtarget>();
  TII = STI->getInstrInfo();
  bool ExpandedStore = false;
  for (auto &MBB : MF) {
    MachineBasicBlock::iterator MBBI = MBB.begin(), E = MBB.end();
    while (MBBI != E) {
      MachineBasicBlock::iterator NMBBI = std::next(MBBI);
      switch (MBBI->getOpcode()) {
      case ARC::ST_FAR:
      case ARC::STH_FAR:
      case ARC::STB_FAR:
        ExpandStore(MF, MBBI);
        ExpandedStore = true;
        break;
      default:
        break;
      }
      MBBI = NMBBI;
    }
  }
  return ExpandedStore;
}

FunctionPass *llvm::createARCExpandPseudosPass() {
  return new ARCExpandPseudos();
}
