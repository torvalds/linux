//===-- MLxExpansionPass.cpp - Expand MLx instrs to avoid hazards ---------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// Expand VFP / NEON floating point MLA / MLS instructions (each to a pair of
// multiple and add / sub instructions) when special VMLx hazards are detected.
//
//===----------------------------------------------------------------------===//

#include "ARM.h"
#include "ARMBaseInstrInfo.h"
#include "ARMSubtarget.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/CodeGen/MachineFunctionPass.h"
#include "llvm/CodeGen/MachineInstr.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/CodeGen/TargetRegisterInfo.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/raw_ostream.h"
using namespace llvm;

#define DEBUG_TYPE "mlx-expansion"

static cl::opt<bool>
ForceExapnd("expand-all-fp-mlx", cl::init(false), cl::Hidden);
static cl::opt<unsigned>
ExpandLimit("expand-limit", cl::init(~0U), cl::Hidden);

STATISTIC(NumExpand, "Number of fp MLA / MLS instructions expanded");

namespace {
  struct MLxExpansion : public MachineFunctionPass {
    static char ID;
    MLxExpansion() : MachineFunctionPass(ID) {}

    bool runOnMachineFunction(MachineFunction &Fn) override;

    StringRef getPassName() const override {
      return "ARM MLA / MLS expansion pass";
    }

  private:
    const ARMBaseInstrInfo *TII;
    const TargetRegisterInfo *TRI;
    MachineRegisterInfo *MRI;

    bool isLikeA9;
    bool isSwift;
    unsigned MIIdx;
    MachineInstr* LastMIs[4];
    SmallPtrSet<MachineInstr*, 4> IgnoreStall;

    void clearStack();
    void pushStack(MachineInstr *MI);
    MachineInstr *getAccDefMI(MachineInstr *MI) const;
    unsigned getDefReg(MachineInstr *MI) const;
    bool hasLoopHazard(MachineInstr *MI) const;
    bool hasRAWHazard(unsigned Reg, MachineInstr *MI) const;
    bool FindMLxHazard(MachineInstr *MI);
    void ExpandFPMLxInstruction(MachineBasicBlock &MBB, MachineInstr *MI,
                                unsigned MulOpc, unsigned AddSubOpc,
                                bool NegAcc, bool HasLane);
    bool ExpandFPMLxInstructions(MachineBasicBlock &MBB);
  };
  char MLxExpansion::ID = 0;
}

void MLxExpansion::clearStack() {
  std::fill(LastMIs, LastMIs + 4, nullptr);
  MIIdx = 0;
}

void MLxExpansion::pushStack(MachineInstr *MI) {
  LastMIs[MIIdx] = MI;
  if (++MIIdx == 4)
    MIIdx = 0;
}

MachineInstr *MLxExpansion::getAccDefMI(MachineInstr *MI) const {
  // Look past COPY and INSERT_SUBREG instructions to find the
  // real definition MI. This is important for _sfp instructions.
  unsigned Reg = MI->getOperand(1).getReg();
  if (TargetRegisterInfo::isPhysicalRegister(Reg))
    return nullptr;

  MachineBasicBlock *MBB = MI->getParent();
  MachineInstr *DefMI = MRI->getVRegDef(Reg);
  while (true) {
    if (DefMI->getParent() != MBB)
      break;
    if (DefMI->isCopyLike()) {
      Reg = DefMI->getOperand(1).getReg();
      if (TargetRegisterInfo::isVirtualRegister(Reg)) {
        DefMI = MRI->getVRegDef(Reg);
        continue;
      }
    } else if (DefMI->isInsertSubreg()) {
      Reg = DefMI->getOperand(2).getReg();
      if (TargetRegisterInfo::isVirtualRegister(Reg)) {
        DefMI = MRI->getVRegDef(Reg);
        continue;
      }
    }
    break;
  }
  return DefMI;
}

unsigned MLxExpansion::getDefReg(MachineInstr *MI) const {
  unsigned Reg = MI->getOperand(0).getReg();
  if (TargetRegisterInfo::isPhysicalRegister(Reg) ||
      !MRI->hasOneNonDBGUse(Reg))
    return Reg;

  MachineBasicBlock *MBB = MI->getParent();
  MachineInstr *UseMI = &*MRI->use_instr_nodbg_begin(Reg);
  if (UseMI->getParent() != MBB)
    return Reg;

  while (UseMI->isCopy() || UseMI->isInsertSubreg()) {
    Reg = UseMI->getOperand(0).getReg();
    if (TargetRegisterInfo::isPhysicalRegister(Reg) ||
        !MRI->hasOneNonDBGUse(Reg))
      return Reg;
    UseMI = &*MRI->use_instr_nodbg_begin(Reg);
    if (UseMI->getParent() != MBB)
      return Reg;
  }

  return Reg;
}

/// hasLoopHazard - Check whether an MLx instruction is chained to itself across
/// a single-MBB loop.
bool MLxExpansion::hasLoopHazard(MachineInstr *MI) const {
  unsigned Reg = MI->getOperand(1).getReg();
  if (TargetRegisterInfo::isPhysicalRegister(Reg))
    return false;

  MachineBasicBlock *MBB = MI->getParent();
  MachineInstr *DefMI = MRI->getVRegDef(Reg);
  while (true) {
outer_continue:
    if (DefMI->getParent() != MBB)
      break;

    if (DefMI->isPHI()) {
      for (unsigned i = 1, e = DefMI->getNumOperands(); i < e; i += 2) {
        if (DefMI->getOperand(i + 1).getMBB() == MBB) {
          unsigned SrcReg = DefMI->getOperand(i).getReg();
          if (TargetRegisterInfo::isVirtualRegister(SrcReg)) {
            DefMI = MRI->getVRegDef(SrcReg);
            goto outer_continue;
          }
        }
      }
    } else if (DefMI->isCopyLike()) {
      Reg = DefMI->getOperand(1).getReg();
      if (TargetRegisterInfo::isVirtualRegister(Reg)) {
        DefMI = MRI->getVRegDef(Reg);
        continue;
      }
    } else if (DefMI->isInsertSubreg()) {
      Reg = DefMI->getOperand(2).getReg();
      if (TargetRegisterInfo::isVirtualRegister(Reg)) {
        DefMI = MRI->getVRegDef(Reg);
        continue;
      }
    }

    break;
  }

  return DefMI == MI;
}

bool MLxExpansion::hasRAWHazard(unsigned Reg, MachineInstr *MI) const {
  // FIXME: Detect integer instructions properly.
  const MCInstrDesc &MCID = MI->getDesc();
  unsigned Domain = MCID.TSFlags & ARMII::DomainMask;
  if (MI->mayStore())
    return false;
  unsigned Opcode = MCID.getOpcode();
  if (Opcode == ARM::VMOVRS || Opcode == ARM::VMOVRRD)
    return false;
  if ((Domain & ARMII::DomainVFP) || (Domain & ARMII::DomainNEON))
    return MI->readsRegister(Reg, TRI);
  return false;
}

static bool isFpMulInstruction(unsigned Opcode) {
  switch (Opcode) {
  case ARM::VMULS:
  case ARM::VMULfd:
  case ARM::VMULfq:
  case ARM::VMULD:
  case ARM::VMULslfd:
  case ARM::VMULslfq:
    return true;
  default:
    return false;
  }
}

bool MLxExpansion::FindMLxHazard(MachineInstr *MI) {
  if (NumExpand >= ExpandLimit)
    return false;

  if (ForceExapnd)
    return true;

  MachineInstr *DefMI = getAccDefMI(MI);
  if (TII->isFpMLxInstruction(DefMI->getOpcode())) {
    // r0 = vmla
    // r3 = vmla r0, r1, r2
    // takes 16 - 17 cycles
    //
    // r0 = vmla
    // r4 = vmul r1, r2
    // r3 = vadd r0, r4
    // takes about 14 - 15 cycles even with vmul stalling for 4 cycles.
    IgnoreStall.insert(DefMI);
    return true;
  }

  // On Swift, we mostly care about hazards from multiplication instructions
  // writing the accumulator and the pipelining of loop iterations by out-of-
  // order execution.
  if (isSwift)
    return isFpMulInstruction(DefMI->getOpcode()) || hasLoopHazard(MI);

  if (IgnoreStall.count(MI))
    return false;

  // If a VMLA.F is followed by an VADD.F or VMUL.F with no RAW hazard, the
  // VADD.F or VMUL.F will stall 4 cycles before issue. The 4 cycle stall
  // preserves the in-order retirement of the instructions.
  // Look at the next few instructions, if *most* of them can cause hazards,
  // then the scheduler can't *fix* this, we'd better break up the VMLA.
  unsigned Limit1 = isLikeA9 ? 1 : 4;
  unsigned Limit2 = isLikeA9 ? 1 : 4;
  for (unsigned i = 1; i <= 4; ++i) {
    int Idx = ((int)MIIdx - i + 4) % 4;
    MachineInstr *NextMI = LastMIs[Idx];
    if (!NextMI)
      continue;

    if (TII->canCauseFpMLxStall(NextMI->getOpcode())) {
      if (i <= Limit1)
        return true;
    }

    // Look for VMLx RAW hazard.
    if (i <= Limit2 && hasRAWHazard(getDefReg(MI), NextMI))
      return true;
  }

  return false;
}

/// ExpandFPMLxInstructions - Expand a MLA / MLS instruction into a pair
/// of MUL + ADD / SUB instructions.
void
MLxExpansion::ExpandFPMLxInstruction(MachineBasicBlock &MBB, MachineInstr *MI,
                                     unsigned MulOpc, unsigned AddSubOpc,
                                     bool NegAcc, bool HasLane) {
  unsigned DstReg = MI->getOperand(0).getReg();
  bool DstDead = MI->getOperand(0).isDead();
  unsigned AccReg = MI->getOperand(1).getReg();
  unsigned Src1Reg = MI->getOperand(2).getReg();
  unsigned Src2Reg = MI->getOperand(3).getReg();
  bool Src1Kill = MI->getOperand(2).isKill();
  bool Src2Kill = MI->getOperand(3).isKill();
  unsigned LaneImm = HasLane ? MI->getOperand(4).getImm() : 0;
  unsigned NextOp = HasLane ? 5 : 4;
  ARMCC::CondCodes Pred = (ARMCC::CondCodes)MI->getOperand(NextOp).getImm();
  unsigned PredReg = MI->getOperand(++NextOp).getReg();

  const MCInstrDesc &MCID1 = TII->get(MulOpc);
  const MCInstrDesc &MCID2 = TII->get(AddSubOpc);
  const MachineFunction &MF = *MI->getParent()->getParent();
  unsigned TmpReg = MRI->createVirtualRegister(
                      TII->getRegClass(MCID1, 0, TRI, MF));

  MachineInstrBuilder MIB = BuildMI(MBB, MI, MI->getDebugLoc(), MCID1, TmpReg)
    .addReg(Src1Reg, getKillRegState(Src1Kill))
    .addReg(Src2Reg, getKillRegState(Src2Kill));
  if (HasLane)
    MIB.addImm(LaneImm);
  MIB.addImm(Pred).addReg(PredReg);

  MIB = BuildMI(MBB, MI, MI->getDebugLoc(), MCID2)
    .addReg(DstReg, getDefRegState(true) | getDeadRegState(DstDead));

  if (NegAcc) {
    bool AccKill = MRI->hasOneNonDBGUse(AccReg);
    MIB.addReg(TmpReg, getKillRegState(true))
       .addReg(AccReg, getKillRegState(AccKill));
  } else {
    MIB.addReg(AccReg).addReg(TmpReg, getKillRegState(true));
  }
  MIB.addImm(Pred).addReg(PredReg);

  LLVM_DEBUG({
    dbgs() << "Expanding: " << *MI;
    dbgs() << "  to:\n";
    MachineBasicBlock::iterator MII = MI;
    MII = std::prev(MII);
    MachineInstr &MI2 = *MII;
    MII = std::prev(MII);
    MachineInstr &MI1 = *MII;
    dbgs() << "    " << MI1;
    dbgs() << "    " << MI2;
  });

  MI->eraseFromParent();
  ++NumExpand;
}

bool MLxExpansion::ExpandFPMLxInstructions(MachineBasicBlock &MBB) {
  bool Changed = false;

  clearStack();
  IgnoreStall.clear();

  unsigned Skip = 0;
  MachineBasicBlock::reverse_iterator MII = MBB.rbegin(), E = MBB.rend();
  while (MII != E) {
    MachineInstr *MI = &*MII++;

    if (MI->isPosition() || MI->isImplicitDef() || MI->isCopy())
      continue;

    const MCInstrDesc &MCID = MI->getDesc();
    if (MI->isBarrier()) {
      clearStack();
      Skip = 0;
      continue;
    }

    unsigned Domain = MCID.TSFlags & ARMII::DomainMask;
    if (Domain == ARMII::DomainGeneral) {
      if (++Skip == 2)
        // Assume dual issues of non-VFP / NEON instructions.
        pushStack(nullptr);
    } else {
      Skip = 0;

      unsigned MulOpc, AddSubOpc;
      bool NegAcc, HasLane;
      if (!TII->isFpMLxInstruction(MCID.getOpcode(),
                                   MulOpc, AddSubOpc, NegAcc, HasLane) ||
          !FindMLxHazard(MI))
        pushStack(MI);
      else {
        ExpandFPMLxInstruction(MBB, MI, MulOpc, AddSubOpc, NegAcc, HasLane);
        Changed = true;
      }
    }
  }

  return Changed;
}

bool MLxExpansion::runOnMachineFunction(MachineFunction &Fn) {
  if (skipFunction(Fn.getFunction()))
    return false;

  TII = static_cast<const ARMBaseInstrInfo *>(Fn.getSubtarget().getInstrInfo());
  TRI = Fn.getSubtarget().getRegisterInfo();
  MRI = &Fn.getRegInfo();
  const ARMSubtarget *STI = &Fn.getSubtarget<ARMSubtarget>();
  if (!STI->expandMLx())
    return false;
  isLikeA9 = STI->isLikeA9() || STI->isSwift();
  isSwift = STI->isSwift();

  bool Modified = false;
  for (MachineBasicBlock &MBB : Fn)
    Modified |= ExpandFPMLxInstructions(MBB);

  return Modified;
}

FunctionPass *llvm::createMLxExpansionPass() {
  return new MLxExpansion();
}
