//=======- GCNDPPCombine.cpp - optimization for DPP instructions ---==========//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
// The pass combines V_MOV_B32_dpp instruction with its VALU uses as a DPP src0
// operand.If any of the use instruction cannot be combined with the mov the
// whole sequence is reverted.
//
// $old = ...
// $dpp_value = V_MOV_B32_dpp $old, $vgpr_to_be_read_from_other_lane,
//                            dpp_controls..., $bound_ctrl
// $res = VALU $dpp_value, ...
//
// to
//
// $res = VALU_DPP $folded_old, $vgpr_to_be_read_from_other_lane, ...,
//                 dpp_controls..., $folded_bound_ctrl
//
// Combining rules :
//
// $bound_ctrl is DPP_BOUND_ZERO, $old is any
// $bound_ctrl is DPP_BOUND_OFF, $old is 0
//
// ->$folded_old = undef, $folded_bound_ctrl = DPP_BOUND_ZERO
// $bound_ctrl is DPP_BOUND_OFF, $old is undef
//
// ->$folded_old = undef, $folded_bound_ctrl = DPP_BOUND_OFF
// $bound_ctrl is DPP_BOUND_OFF, $old is foldable
//
// ->$folded_old = folded value, $folded_bound_ctrl = DPP_BOUND_OFF
//===----------------------------------------------------------------------===//

#include "AMDGPU.h"
#include "AMDGPUSubtarget.h"
#include "SIInstrInfo.h"
#include "MCTargetDesc/AMDGPUMCTargetDesc.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/CodeGen/MachineBasicBlock.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineFunctionPass.h"
#include "llvm/CodeGen/MachineInstr.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/CodeGen/MachineOperand.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/CodeGen/TargetRegisterInfo.h"
#include "llvm/Pass.h"
#include <cassert>

using namespace llvm;

#define DEBUG_TYPE "gcn-dpp-combine"

STATISTIC(NumDPPMovsCombined, "Number of DPP moves combined.");

namespace {

class GCNDPPCombine : public MachineFunctionPass {
  MachineRegisterInfo *MRI;
  const SIInstrInfo *TII;

  using RegSubRegPair = TargetInstrInfo::RegSubRegPair;

  MachineOperand *getOldOpndValue(MachineOperand &OldOpnd) const;

  RegSubRegPair foldOldOpnd(MachineInstr &OrigMI,
                            RegSubRegPair OldOpndVGPR,
                            MachineOperand &OldOpndValue) const;

  MachineInstr *createDPPInst(MachineInstr &OrigMI,
                              MachineInstr &MovMI,
                              RegSubRegPair OldOpndVGPR,
                              MachineOperand *OldOpnd,
                              bool BoundCtrlZero) const;

  MachineInstr *createDPPInst(MachineInstr &OrigMI,
                              MachineInstr &MovMI,
                              RegSubRegPair OldOpndVGPR,
                              bool BoundCtrlZero) const;

  bool hasNoImmOrEqual(MachineInstr &MI,
                       unsigned OpndName,
                       int64_t Value,
                       int64_t Mask = -1) const;

  bool combineDPPMov(MachineInstr &MI) const;

public:
  static char ID;

  GCNDPPCombine() : MachineFunctionPass(ID) {
    initializeGCNDPPCombinePass(*PassRegistry::getPassRegistry());
  }

  bool runOnMachineFunction(MachineFunction &MF) override;

  StringRef getPassName() const override { return "GCN DPP Combine"; }

  void getAnalysisUsage(AnalysisUsage &AU) const override {
    AU.setPreservesCFG();
    MachineFunctionPass::getAnalysisUsage(AU);
  }
};

} // end anonymous namespace

INITIALIZE_PASS(GCNDPPCombine, DEBUG_TYPE, "GCN DPP Combine", false, false)

char GCNDPPCombine::ID = 0;

char &llvm::GCNDPPCombineID = GCNDPPCombine::ID;

FunctionPass *llvm::createGCNDPPCombinePass() {
  return new GCNDPPCombine();
}

static int getDPPOp(unsigned Op) {
  auto DPP32 = AMDGPU::getDPPOp32(Op);
  if (DPP32 != -1)
    return DPP32;

  auto E32 = AMDGPU::getVOPe32(Op);
  return E32 != -1 ? AMDGPU::getDPPOp32(E32) : -1;
}

// tracks the register operand definition and returns:
//   1. immediate operand used to initialize the register if found
//   2. nullptr if the register operand is undef
//   3. the operand itself otherwise
MachineOperand *GCNDPPCombine::getOldOpndValue(MachineOperand &OldOpnd) const {
  auto *Def = getVRegSubRegDef(getRegSubRegPair(OldOpnd), *MRI);
  if (!Def)
    return nullptr;

  switch(Def->getOpcode()) {
  default: break;
  case AMDGPU::IMPLICIT_DEF:
    return nullptr;
  case AMDGPU::COPY:
  case AMDGPU::V_MOV_B32_e32: {
    auto &Op1 = Def->getOperand(1);
    if (Op1.isImm())
      return &Op1;
    break;
  }
  }
  return &OldOpnd;
}

MachineInstr *GCNDPPCombine::createDPPInst(MachineInstr &OrigMI,
                                           MachineInstr &MovMI,
                                           RegSubRegPair OldOpndVGPR,
                                           bool BoundCtrlZero) const {
  assert(MovMI.getOpcode() == AMDGPU::V_MOV_B32_dpp);
  assert(TII->getNamedOperand(MovMI, AMDGPU::OpName::vdst)->getReg() ==
         TII->getNamedOperand(OrigMI, AMDGPU::OpName::src0)->getReg());

  auto OrigOp = OrigMI.getOpcode();
  auto DPPOp = getDPPOp(OrigOp);
  if (DPPOp == -1) {
    LLVM_DEBUG(dbgs() << "  failed: no DPP opcode\n");
    return nullptr;
  }

  auto DPPInst = BuildMI(*OrigMI.getParent(), OrigMI,
                         OrigMI.getDebugLoc(), TII->get(DPPOp));
  bool Fail = false;
  do {
    auto *Dst = TII->getNamedOperand(OrigMI, AMDGPU::OpName::vdst);
    assert(Dst);
    DPPInst.add(*Dst);
    int NumOperands = 1;

    const int OldIdx = AMDGPU::getNamedOperandIdx(DPPOp, AMDGPU::OpName::old);
    if (OldIdx != -1) {
      assert(OldIdx == NumOperands);
      assert(isOfRegClass(OldOpndVGPR, AMDGPU::VGPR_32RegClass, *MRI));
      DPPInst.addReg(OldOpndVGPR.Reg, 0, OldOpndVGPR.SubReg);
      ++NumOperands;
    }

    if (auto *Mod0 = TII->getNamedOperand(OrigMI,
                                          AMDGPU::OpName::src0_modifiers)) {
      assert(NumOperands == AMDGPU::getNamedOperandIdx(DPPOp,
                                          AMDGPU::OpName::src0_modifiers));
      assert(0LL == (Mod0->getImm() & ~(SISrcMods::ABS | SISrcMods::NEG)));
      DPPInst.addImm(Mod0->getImm());
      ++NumOperands;
    }
    auto *Src0 = TII->getNamedOperand(MovMI, AMDGPU::OpName::src0);
    assert(Src0);
    if (!TII->isOperandLegal(*DPPInst.getInstr(), NumOperands, Src0)) {
      LLVM_DEBUG(dbgs() << "  failed: src0 is illegal\n");
      Fail = true;
      break;
    }
    DPPInst.add(*Src0);
    ++NumOperands;

    if (auto *Mod1 = TII->getNamedOperand(OrigMI,
                                          AMDGPU::OpName::src1_modifiers)) {
      assert(NumOperands == AMDGPU::getNamedOperandIdx(DPPOp,
                                          AMDGPU::OpName::src1_modifiers));
      assert(0LL == (Mod1->getImm() & ~(SISrcMods::ABS | SISrcMods::NEG)));
      DPPInst.addImm(Mod1->getImm());
      ++NumOperands;
    }
    if (auto *Src1 = TII->getNamedOperand(OrigMI, AMDGPU::OpName::src1)) {
      if (!TII->isOperandLegal(*DPPInst.getInstr(), NumOperands, Src1)) {
        LLVM_DEBUG(dbgs() << "  failed: src1 is illegal\n");
        Fail = true;
        break;
      }
      DPPInst.add(*Src1);
      ++NumOperands;
    }

    if (auto *Src2 = TII->getNamedOperand(OrigMI, AMDGPU::OpName::src2)) {
      if (!TII->isOperandLegal(*DPPInst.getInstr(), NumOperands, Src2)) {
        LLVM_DEBUG(dbgs() << "  failed: src2 is illegal\n");
        Fail = true;
        break;
      }
      DPPInst.add(*Src2);
    }

    DPPInst.add(*TII->getNamedOperand(MovMI, AMDGPU::OpName::dpp_ctrl));
    DPPInst.add(*TII->getNamedOperand(MovMI, AMDGPU::OpName::row_mask));
    DPPInst.add(*TII->getNamedOperand(MovMI, AMDGPU::OpName::bank_mask));
    DPPInst.addImm(BoundCtrlZero ? 1 : 0);
  } while (false);

  if (Fail) {
    DPPInst.getInstr()->eraseFromParent();
    return nullptr;
  }
  LLVM_DEBUG(dbgs() << "  combined:  " << *DPPInst.getInstr());
  return DPPInst.getInstr();
}

GCNDPPCombine::RegSubRegPair
GCNDPPCombine::foldOldOpnd(MachineInstr &OrigMI,
                           RegSubRegPair OldOpndVGPR,
                           MachineOperand &OldOpndValue) const {
  assert(OldOpndValue.isImm());
  switch (OrigMI.getOpcode()) {
  default: break;
  case AMDGPU::V_MAX_U32_e32:
    if (OldOpndValue.getImm() == std::numeric_limits<uint32_t>::max())
      return OldOpndVGPR;
    break;
  case AMDGPU::V_MAX_I32_e32:
    if (OldOpndValue.getImm() == std::numeric_limits<int32_t>::max())
      return OldOpndVGPR;
    break;
  case AMDGPU::V_MIN_I32_e32:
    if (OldOpndValue.getImm() == std::numeric_limits<int32_t>::min())
      return OldOpndVGPR;
    break;

  case AMDGPU::V_MUL_I32_I24_e32:
  case AMDGPU::V_MUL_U32_U24_e32:
    if (OldOpndValue.getImm() == 1) {
      auto *Src1 = TII->getNamedOperand(OrigMI, AMDGPU::OpName::src1);
      assert(Src1 && Src1->isReg());
      return getRegSubRegPair(*Src1);
    }
    break;
  }
  return RegSubRegPair();
}

// Cases to combine:
//  $bound_ctrl is DPP_BOUND_ZERO, $old is any
//  $bound_ctrl is DPP_BOUND_OFF, $old is 0
//  -> $old = undef, $bound_ctrl = DPP_BOUND_ZERO

//  $bound_ctrl is DPP_BOUND_OFF, $old is undef
//  -> $old = undef, $bound_ctrl = DPP_BOUND_OFF

//  $bound_ctrl is DPP_BOUND_OFF, $old is foldable
//  -> $old = folded value, $bound_ctrl = DPP_BOUND_OFF

MachineInstr *GCNDPPCombine::createDPPInst(MachineInstr &OrigMI,
                                           MachineInstr &MovMI,
                                           RegSubRegPair OldOpndVGPR,
                                           MachineOperand *OldOpndValue,
                                           bool BoundCtrlZero) const {
  assert(OldOpndVGPR.Reg);
  if (!BoundCtrlZero && OldOpndValue) {
    assert(OldOpndValue->isImm());
    OldOpndVGPR = foldOldOpnd(OrigMI, OldOpndVGPR, *OldOpndValue);
    if (!OldOpndVGPR.Reg) {
      LLVM_DEBUG(dbgs() << "  failed: old immediate cannot be folded\n");
      return nullptr;
    }
  }
  return createDPPInst(OrigMI, MovMI, OldOpndVGPR, BoundCtrlZero);
}

// returns true if MI doesn't have OpndName immediate operand or the
// operand has Value
bool GCNDPPCombine::hasNoImmOrEqual(MachineInstr &MI, unsigned OpndName,
                                    int64_t Value, int64_t Mask) const {
  auto *Imm = TII->getNamedOperand(MI, OpndName);
  if (!Imm)
    return true;

  assert(Imm->isImm());
  return (Imm->getImm() & Mask) == Value;
}

bool GCNDPPCombine::combineDPPMov(MachineInstr &MovMI) const {
  assert(MovMI.getOpcode() == AMDGPU::V_MOV_B32_dpp);
  auto *BCZOpnd = TII->getNamedOperand(MovMI, AMDGPU::OpName::bound_ctrl);
  assert(BCZOpnd && BCZOpnd->isImm());
  bool BoundCtrlZero = 0 != BCZOpnd->getImm();

  LLVM_DEBUG(dbgs() << "\nDPP combine: " << MovMI);

  auto *OldOpnd = TII->getNamedOperand(MovMI, AMDGPU::OpName::old);
  assert(OldOpnd && OldOpnd->isReg());
  auto OldOpndVGPR = getRegSubRegPair(*OldOpnd);
  auto *OldOpndValue = getOldOpndValue(*OldOpnd);
  assert(!OldOpndValue || OldOpndValue->isImm() || OldOpndValue == OldOpnd);
  if (OldOpndValue) {
    if (BoundCtrlZero) {
      OldOpndVGPR.Reg = AMDGPU::NoRegister; // should be undef, ignore old opnd
      OldOpndValue = nullptr;
    } else {
      if (!OldOpndValue->isImm()) {
        LLVM_DEBUG(dbgs() << "  failed: old operand isn't an imm or undef\n");
        return false;
      }
      if (OldOpndValue->getImm() == 0) {
        OldOpndVGPR.Reg = AMDGPU::NoRegister; // should be undef
        OldOpndValue = nullptr;
        BoundCtrlZero = true;
      }
    }
  }

  LLVM_DEBUG(dbgs() << "  old=";
    if (!OldOpndValue)
      dbgs() << "undef";
    else
      dbgs() << OldOpndValue->getImm();
    dbgs() << ", bound_ctrl=" << BoundCtrlZero << '\n');

  std::vector<MachineInstr*> OrigMIs, DPPMIs;
  if (!OldOpndVGPR.Reg) { // OldOpndVGPR = undef
    OldOpndVGPR = RegSubRegPair(
      MRI->createVirtualRegister(&AMDGPU::VGPR_32RegClass));
    auto UndefInst = BuildMI(*MovMI.getParent(), MovMI, MovMI.getDebugLoc(),
                             TII->get(AMDGPU::IMPLICIT_DEF), OldOpndVGPR.Reg);
    DPPMIs.push_back(UndefInst.getInstr());
  }

  OrigMIs.push_back(&MovMI);
  bool Rollback = true;
  for (auto &Use : MRI->use_nodbg_operands(
       TII->getNamedOperand(MovMI, AMDGPU::OpName::vdst)->getReg())) {
    Rollback = true;

    auto &OrigMI = *Use.getParent();
    auto OrigOp = OrigMI.getOpcode();
    if (TII->isVOP3(OrigOp)) {
      if (!TII->hasVALU32BitEncoding(OrigOp)) {
        LLVM_DEBUG(dbgs() << "  failed: VOP3 hasn't e32 equivalent\n");
        break;
      }
      // check if other than abs|neg modifiers are set (opsel for example)
      const int64_t Mask = ~(SISrcMods::ABS | SISrcMods::NEG);
      if (!hasNoImmOrEqual(OrigMI, AMDGPU::OpName::src0_modifiers, 0, Mask) ||
          !hasNoImmOrEqual(OrigMI, AMDGPU::OpName::src1_modifiers, 0, Mask) ||
          !hasNoImmOrEqual(OrigMI, AMDGPU::OpName::clamp, 0) ||
          !hasNoImmOrEqual(OrigMI, AMDGPU::OpName::omod, 0)) {
        LLVM_DEBUG(dbgs() << "  failed: VOP3 has non-default modifiers\n");
        break;
      }
    } else if (!TII->isVOP1(OrigOp) && !TII->isVOP2(OrigOp)) {
      LLVM_DEBUG(dbgs() << "  failed: not VOP1/2/3\n");
      break;
    }

    LLVM_DEBUG(dbgs() << "  combining: " << OrigMI);
    if (&Use == TII->getNamedOperand(OrigMI, AMDGPU::OpName::src0)) {
      if (auto *DPPInst = createDPPInst(OrigMI, MovMI, OldOpndVGPR,
                                        OldOpndValue, BoundCtrlZero)) {
        DPPMIs.push_back(DPPInst);
        Rollback = false;
      }
    } else if (OrigMI.isCommutable() &&
               &Use == TII->getNamedOperand(OrigMI, AMDGPU::OpName::src1)) {
      auto *BB = OrigMI.getParent();
      auto *NewMI = BB->getParent()->CloneMachineInstr(&OrigMI);
      BB->insert(OrigMI, NewMI);
      if (TII->commuteInstruction(*NewMI)) {
        LLVM_DEBUG(dbgs() << "  commuted:  " << *NewMI);
        if (auto *DPPInst = createDPPInst(*NewMI, MovMI, OldOpndVGPR,
                                          OldOpndValue, BoundCtrlZero)) {
          DPPMIs.push_back(DPPInst);
          Rollback = false;
        }
      } else
        LLVM_DEBUG(dbgs() << "  failed: cannot be commuted\n");
      NewMI->eraseFromParent();
    } else
      LLVM_DEBUG(dbgs() << "  failed: no suitable operands\n");
    if (Rollback)
      break;
    OrigMIs.push_back(&OrigMI);
  }

  for (auto *MI : *(Rollback? &DPPMIs : &OrigMIs))
    MI->eraseFromParent();

  return !Rollback;
}

bool GCNDPPCombine::runOnMachineFunction(MachineFunction &MF) {
  auto &ST = MF.getSubtarget<GCNSubtarget>();
  if (!ST.hasDPP() || skipFunction(MF.getFunction()))
    return false;

  MRI = &MF.getRegInfo();
  TII = ST.getInstrInfo();

  assert(MRI->isSSA() && "Must be run on SSA");

  bool Changed = false;
  for (auto &MBB : MF) {
    for (auto I = MBB.rbegin(), E = MBB.rend(); I != E;) {
      auto &MI = *I++;
      if (MI.getOpcode() == AMDGPU::V_MOV_B32_dpp && combineDPPMov(MI)) {
        Changed = true;
        ++NumDPPMovsCombined;
      }
    }
  }
  return Changed;
}
