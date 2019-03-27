//===-- NVPTXPeephole.cpp - NVPTX Peephole Optimiztions -------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// In NVPTX, NVPTXFrameLowering will emit following instruction at the beginning
// of a MachineFunction.
//
//   mov %SPL, %depot
//   cvta.local %SP, %SPL
//
// Because Frame Index is a generic address and alloca can only return generic
// pointer, without this pass the instructions producing alloca'ed address will
// be based on %SP. NVPTXLowerAlloca tends to help replace store and load on
// this address with their .local versions, but this may introduce a lot of
// cvta.to.local instructions. Performance can be improved if we avoid casting
// address back and forth and directly calculate local address based on %SPL.
// This peephole pass optimizes these cases, for example
//
// It will transform the following pattern
//    %0 = LEA_ADDRi64 %VRFrame, 4
//    %1 = cvta_to_local_yes_64 %0
//
// into
//    %1 = LEA_ADDRi64 %VRFrameLocal, 4
//
// %VRFrameLocal is the virtual register name of %SPL
//
//===----------------------------------------------------------------------===//

#include "NVPTX.h"
#include "llvm/CodeGen/MachineFunctionPass.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/CodeGen/TargetInstrInfo.h"
#include "llvm/CodeGen/TargetRegisterInfo.h"

using namespace llvm;

#define DEBUG_TYPE "nvptx-peephole"

namespace llvm {
void initializeNVPTXPeepholePass(PassRegistry &);
}

namespace {
struct NVPTXPeephole : public MachineFunctionPass {
 public:
  static char ID;
  NVPTXPeephole() : MachineFunctionPass(ID) {
    initializeNVPTXPeepholePass(*PassRegistry::getPassRegistry());
  }

  bool runOnMachineFunction(MachineFunction &MF) override;

  StringRef getPassName() const override {
    return "NVPTX optimize redundant cvta.to.local instruction";
  }

  void getAnalysisUsage(AnalysisUsage &AU) const override {
    MachineFunctionPass::getAnalysisUsage(AU);
  }
};
}

char NVPTXPeephole::ID = 0;

INITIALIZE_PASS(NVPTXPeephole, "nvptx-peephole", "NVPTX Peephole", false, false)

static bool isCVTAToLocalCombinationCandidate(MachineInstr &Root) {
  auto &MBB = *Root.getParent();
  auto &MF = *MBB.getParent();
  // Check current instruction is cvta.to.local
  if (Root.getOpcode() != NVPTX::cvta_to_local_yes_64 &&
      Root.getOpcode() != NVPTX::cvta_to_local_yes)
    return false;

  auto &Op = Root.getOperand(1);
  const auto &MRI = MF.getRegInfo();
  MachineInstr *GenericAddrDef = nullptr;
  if (Op.isReg() && TargetRegisterInfo::isVirtualRegister(Op.getReg())) {
    GenericAddrDef = MRI.getUniqueVRegDef(Op.getReg());
  }

  // Check the register operand is uniquely defined by LEA_ADDRi instruction
  if (!GenericAddrDef || GenericAddrDef->getParent() != &MBB ||
      (GenericAddrDef->getOpcode() != NVPTX::LEA_ADDRi64 &&
       GenericAddrDef->getOpcode() != NVPTX::LEA_ADDRi)) {
    return false;
  }

  // Check the LEA_ADDRi operand is Frame index
  auto &BaseAddrOp = GenericAddrDef->getOperand(1);
  if (BaseAddrOp.isReg() && BaseAddrOp.getReg() == NVPTX::VRFrame) {
    return true;
  }

  return false;
}

static void CombineCVTAToLocal(MachineInstr &Root) {
  auto &MBB = *Root.getParent();
  auto &MF = *MBB.getParent();
  const auto &MRI = MF.getRegInfo();
  const TargetInstrInfo *TII = MF.getSubtarget().getInstrInfo();
  auto &Prev = *MRI.getUniqueVRegDef(Root.getOperand(1).getReg());

  MachineInstrBuilder MIB =
      BuildMI(MF, Root.getDebugLoc(), TII->get(Prev.getOpcode()),
              Root.getOperand(0).getReg())
          .addReg(NVPTX::VRFrameLocal)
          .add(Prev.getOperand(2));

  MBB.insert((MachineBasicBlock::iterator)&Root, MIB);

  // Check if MRI has only one non dbg use, which is Root
  if (MRI.hasOneNonDBGUse(Prev.getOperand(0).getReg())) {
    Prev.eraseFromParentAndMarkDBGValuesForRemoval();
  }
  Root.eraseFromParentAndMarkDBGValuesForRemoval();
}

bool NVPTXPeephole::runOnMachineFunction(MachineFunction &MF) {
  if (skipFunction(MF.getFunction()))
    return false;

  bool Changed = false;
  // Loop over all of the basic blocks.
  for (auto &MBB : MF) {
    // Traverse the basic block.
    auto BlockIter = MBB.begin();

    while (BlockIter != MBB.end()) {
      auto &MI = *BlockIter++;
      if (isCVTAToLocalCombinationCandidate(MI)) {
        CombineCVTAToLocal(MI);
        Changed = true;
      }
    }  // Instruction
  }    // Basic Block

  // Remove unnecessary %VRFrame = cvta.local %VRFrameLocal
  const auto &MRI = MF.getRegInfo();
  if (MRI.use_empty(NVPTX::VRFrame)) {
    if (auto MI = MRI.getUniqueVRegDef(NVPTX::VRFrame)) {
      MI->eraseFromParentAndMarkDBGValuesForRemoval();
    }
  }

  return Changed;
}

MachineFunctionPass *llvm::createNVPTXPeephole() { return new NVPTXPeephole(); }
