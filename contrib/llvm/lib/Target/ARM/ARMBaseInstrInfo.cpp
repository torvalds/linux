//===-- ARMBaseInstrInfo.cpp - ARM Instruction Information ----------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file contains the Base ARM implementation of the TargetInstrInfo class.
//
//===----------------------------------------------------------------------===//

#include "ARMBaseInstrInfo.h"
#include "ARMBaseRegisterInfo.h"
#include "ARMConstantPoolValue.h"
#include "ARMFeatures.h"
#include "ARMHazardRecognizer.h"
#include "ARMMachineFunctionInfo.h"
#include "ARMSubtarget.h"
#include "MCTargetDesc/ARMAddressingModes.h"
#include "MCTargetDesc/ARMBaseInfo.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallSet.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/Triple.h"
#include "llvm/CodeGen/LiveVariables.h"
#include "llvm/CodeGen/MachineBasicBlock.h"
#include "llvm/CodeGen/MachineConstantPool.h"
#include "llvm/CodeGen/MachineFrameInfo.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineInstr.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/CodeGen/MachineMemOperand.h"
#include "llvm/CodeGen/MachineOperand.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/CodeGen/ScoreboardHazardRecognizer.h"
#include "llvm/CodeGen/SelectionDAGNodes.h"
#include "llvm/CodeGen/TargetInstrInfo.h"
#include "llvm/CodeGen/TargetRegisterInfo.h"
#include "llvm/CodeGen/TargetSchedule.h"
#include "llvm/IR/Attributes.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DebugLoc.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/GlobalValue.h"
#include "llvm/MC/MCAsmInfo.h"
#include "llvm/MC/MCInstrDesc.h"
#include "llvm/MC/MCInstrItineraries.h"
#include "llvm/Support/BranchProbability.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Target/TargetMachine.h"
#include <algorithm>
#include <cassert>
#include <cstdint>
#include <iterator>
#include <new>
#include <utility>
#include <vector>

using namespace llvm;

#define DEBUG_TYPE "arm-instrinfo"

#define GET_INSTRINFO_CTOR_DTOR
#include "ARMGenInstrInfo.inc"

static cl::opt<bool>
EnableARM3Addr("enable-arm-3-addr-conv", cl::Hidden,
               cl::desc("Enable ARM 2-addr to 3-addr conv"));

/// ARM_MLxEntry - Record information about MLA / MLS instructions.
struct ARM_MLxEntry {
  uint16_t MLxOpc;     // MLA / MLS opcode
  uint16_t MulOpc;     // Expanded multiplication opcode
  uint16_t AddSubOpc;  // Expanded add / sub opcode
  bool NegAcc;         // True if the acc is negated before the add / sub.
  bool HasLane;        // True if instruction has an extra "lane" operand.
};

static const ARM_MLxEntry ARM_MLxTable[] = {
  // MLxOpc,          MulOpc,           AddSubOpc,       NegAcc, HasLane
  // fp scalar ops
  { ARM::VMLAS,       ARM::VMULS,       ARM::VADDS,      false,  false },
  { ARM::VMLSS,       ARM::VMULS,       ARM::VSUBS,      false,  false },
  { ARM::VMLAD,       ARM::VMULD,       ARM::VADDD,      false,  false },
  { ARM::VMLSD,       ARM::VMULD,       ARM::VSUBD,      false,  false },
  { ARM::VNMLAS,      ARM::VNMULS,      ARM::VSUBS,      true,   false },
  { ARM::VNMLSS,      ARM::VMULS,       ARM::VSUBS,      true,   false },
  { ARM::VNMLAD,      ARM::VNMULD,      ARM::VSUBD,      true,   false },
  { ARM::VNMLSD,      ARM::VMULD,       ARM::VSUBD,      true,   false },

  // fp SIMD ops
  { ARM::VMLAfd,      ARM::VMULfd,      ARM::VADDfd,     false,  false },
  { ARM::VMLSfd,      ARM::VMULfd,      ARM::VSUBfd,     false,  false },
  { ARM::VMLAfq,      ARM::VMULfq,      ARM::VADDfq,     false,  false },
  { ARM::VMLSfq,      ARM::VMULfq,      ARM::VSUBfq,     false,  false },
  { ARM::VMLAslfd,    ARM::VMULslfd,    ARM::VADDfd,     false,  true  },
  { ARM::VMLSslfd,    ARM::VMULslfd,    ARM::VSUBfd,     false,  true  },
  { ARM::VMLAslfq,    ARM::VMULslfq,    ARM::VADDfq,     false,  true  },
  { ARM::VMLSslfq,    ARM::VMULslfq,    ARM::VSUBfq,     false,  true  },
};

ARMBaseInstrInfo::ARMBaseInstrInfo(const ARMSubtarget& STI)
  : ARMGenInstrInfo(ARM::ADJCALLSTACKDOWN, ARM::ADJCALLSTACKUP),
    Subtarget(STI) {
  for (unsigned i = 0, e = array_lengthof(ARM_MLxTable); i != e; ++i) {
    if (!MLxEntryMap.insert(std::make_pair(ARM_MLxTable[i].MLxOpc, i)).second)
      llvm_unreachable("Duplicated entries?");
    MLxHazardOpcodes.insert(ARM_MLxTable[i].AddSubOpc);
    MLxHazardOpcodes.insert(ARM_MLxTable[i].MulOpc);
  }
}

// Use a ScoreboardHazardRecognizer for prepass ARM scheduling. TargetInstrImpl
// currently defaults to no prepass hazard recognizer.
ScheduleHazardRecognizer *
ARMBaseInstrInfo::CreateTargetHazardRecognizer(const TargetSubtargetInfo *STI,
                                               const ScheduleDAG *DAG) const {
  if (usePreRAHazardRecognizer()) {
    const InstrItineraryData *II =
        static_cast<const ARMSubtarget *>(STI)->getInstrItineraryData();
    return new ScoreboardHazardRecognizer(II, DAG, "pre-RA-sched");
  }
  return TargetInstrInfo::CreateTargetHazardRecognizer(STI, DAG);
}

ScheduleHazardRecognizer *ARMBaseInstrInfo::
CreateTargetPostRAHazardRecognizer(const InstrItineraryData *II,
                                   const ScheduleDAG *DAG) const {
  if (Subtarget.isThumb2() || Subtarget.hasVFP2())
    return (ScheduleHazardRecognizer *)new ARMHazardRecognizer(II, DAG);
  return TargetInstrInfo::CreateTargetPostRAHazardRecognizer(II, DAG);
}

MachineInstr *ARMBaseInstrInfo::convertToThreeAddress(
    MachineFunction::iterator &MFI, MachineInstr &MI, LiveVariables *LV) const {
  // FIXME: Thumb2 support.

  if (!EnableARM3Addr)
    return nullptr;

  MachineFunction &MF = *MI.getParent()->getParent();
  uint64_t TSFlags = MI.getDesc().TSFlags;
  bool isPre = false;
  switch ((TSFlags & ARMII::IndexModeMask) >> ARMII::IndexModeShift) {
  default: return nullptr;
  case ARMII::IndexModePre:
    isPre = true;
    break;
  case ARMII::IndexModePost:
    break;
  }

  // Try splitting an indexed load/store to an un-indexed one plus an add/sub
  // operation.
  unsigned MemOpc = getUnindexedOpcode(MI.getOpcode());
  if (MemOpc == 0)
    return nullptr;

  MachineInstr *UpdateMI = nullptr;
  MachineInstr *MemMI = nullptr;
  unsigned AddrMode = (TSFlags & ARMII::AddrModeMask);
  const MCInstrDesc &MCID = MI.getDesc();
  unsigned NumOps = MCID.getNumOperands();
  bool isLoad = !MI.mayStore();
  const MachineOperand &WB = isLoad ? MI.getOperand(1) : MI.getOperand(0);
  const MachineOperand &Base = MI.getOperand(2);
  const MachineOperand &Offset = MI.getOperand(NumOps - 3);
  unsigned WBReg = WB.getReg();
  unsigned BaseReg = Base.getReg();
  unsigned OffReg = Offset.getReg();
  unsigned OffImm = MI.getOperand(NumOps - 2).getImm();
  ARMCC::CondCodes Pred = (ARMCC::CondCodes)MI.getOperand(NumOps - 1).getImm();
  switch (AddrMode) {
  default: llvm_unreachable("Unknown indexed op!");
  case ARMII::AddrMode2: {
    bool isSub = ARM_AM::getAM2Op(OffImm) == ARM_AM::sub;
    unsigned Amt = ARM_AM::getAM2Offset(OffImm);
    if (OffReg == 0) {
      if (ARM_AM::getSOImmVal(Amt) == -1)
        // Can't encode it in a so_imm operand. This transformation will
        // add more than 1 instruction. Abandon!
        return nullptr;
      UpdateMI = BuildMI(MF, MI.getDebugLoc(),
                         get(isSub ? ARM::SUBri : ARM::ADDri), WBReg)
                     .addReg(BaseReg)
                     .addImm(Amt)
                     .add(predOps(Pred))
                     .add(condCodeOp());
    } else if (Amt != 0) {
      ARM_AM::ShiftOpc ShOpc = ARM_AM::getAM2ShiftOpc(OffImm);
      unsigned SOOpc = ARM_AM::getSORegOpc(ShOpc, Amt);
      UpdateMI = BuildMI(MF, MI.getDebugLoc(),
                         get(isSub ? ARM::SUBrsi : ARM::ADDrsi), WBReg)
                     .addReg(BaseReg)
                     .addReg(OffReg)
                     .addReg(0)
                     .addImm(SOOpc)
                     .add(predOps(Pred))
                     .add(condCodeOp());
    } else
      UpdateMI = BuildMI(MF, MI.getDebugLoc(),
                         get(isSub ? ARM::SUBrr : ARM::ADDrr), WBReg)
                     .addReg(BaseReg)
                     .addReg(OffReg)
                     .add(predOps(Pred))
                     .add(condCodeOp());
    break;
  }
  case ARMII::AddrMode3 : {
    bool isSub = ARM_AM::getAM3Op(OffImm) == ARM_AM::sub;
    unsigned Amt = ARM_AM::getAM3Offset(OffImm);
    if (OffReg == 0)
      // Immediate is 8-bits. It's guaranteed to fit in a so_imm operand.
      UpdateMI = BuildMI(MF, MI.getDebugLoc(),
                         get(isSub ? ARM::SUBri : ARM::ADDri), WBReg)
                     .addReg(BaseReg)
                     .addImm(Amt)
                     .add(predOps(Pred))
                     .add(condCodeOp());
    else
      UpdateMI = BuildMI(MF, MI.getDebugLoc(),
                         get(isSub ? ARM::SUBrr : ARM::ADDrr), WBReg)
                     .addReg(BaseReg)
                     .addReg(OffReg)
                     .add(predOps(Pred))
                     .add(condCodeOp());
    break;
  }
  }

  std::vector<MachineInstr*> NewMIs;
  if (isPre) {
    if (isLoad)
      MemMI =
          BuildMI(MF, MI.getDebugLoc(), get(MemOpc), MI.getOperand(0).getReg())
              .addReg(WBReg)
              .addImm(0)
              .addImm(Pred);
    else
      MemMI = BuildMI(MF, MI.getDebugLoc(), get(MemOpc))
                  .addReg(MI.getOperand(1).getReg())
                  .addReg(WBReg)
                  .addReg(0)
                  .addImm(0)
                  .addImm(Pred);
    NewMIs.push_back(MemMI);
    NewMIs.push_back(UpdateMI);
  } else {
    if (isLoad)
      MemMI =
          BuildMI(MF, MI.getDebugLoc(), get(MemOpc), MI.getOperand(0).getReg())
              .addReg(BaseReg)
              .addImm(0)
              .addImm(Pred);
    else
      MemMI = BuildMI(MF, MI.getDebugLoc(), get(MemOpc))
                  .addReg(MI.getOperand(1).getReg())
                  .addReg(BaseReg)
                  .addReg(0)
                  .addImm(0)
                  .addImm(Pred);
    if (WB.isDead())
      UpdateMI->getOperand(0).setIsDead();
    NewMIs.push_back(UpdateMI);
    NewMIs.push_back(MemMI);
  }

  // Transfer LiveVariables states, kill / dead info.
  if (LV) {
    for (unsigned i = 0, e = MI.getNumOperands(); i != e; ++i) {
      MachineOperand &MO = MI.getOperand(i);
      if (MO.isReg() && TargetRegisterInfo::isVirtualRegister(MO.getReg())) {
        unsigned Reg = MO.getReg();

        LiveVariables::VarInfo &VI = LV->getVarInfo(Reg);
        if (MO.isDef()) {
          MachineInstr *NewMI = (Reg == WBReg) ? UpdateMI : MemMI;
          if (MO.isDead())
            LV->addVirtualRegisterDead(Reg, *NewMI);
        }
        if (MO.isUse() && MO.isKill()) {
          for (unsigned j = 0; j < 2; ++j) {
            // Look at the two new MI's in reverse order.
            MachineInstr *NewMI = NewMIs[j];
            if (!NewMI->readsRegister(Reg))
              continue;
            LV->addVirtualRegisterKilled(Reg, *NewMI);
            if (VI.removeKill(MI))
              VI.Kills.push_back(NewMI);
            break;
          }
        }
      }
    }
  }

  MachineBasicBlock::iterator MBBI = MI.getIterator();
  MFI->insert(MBBI, NewMIs[1]);
  MFI->insert(MBBI, NewMIs[0]);
  return NewMIs[0];
}

// Branch analysis.
bool ARMBaseInstrInfo::analyzeBranch(MachineBasicBlock &MBB,
                                     MachineBasicBlock *&TBB,
                                     MachineBasicBlock *&FBB,
                                     SmallVectorImpl<MachineOperand> &Cond,
                                     bool AllowModify) const {
  TBB = nullptr;
  FBB = nullptr;

  MachineBasicBlock::iterator I = MBB.end();
  if (I == MBB.begin())
    return false; // Empty blocks are easy.
  --I;

  // Walk backwards from the end of the basic block until the branch is
  // analyzed or we give up.
  while (isPredicated(*I) || I->isTerminator() || I->isDebugValue()) {
    // Flag to be raised on unanalyzeable instructions. This is useful in cases
    // where we want to clean up on the end of the basic block before we bail
    // out.
    bool CantAnalyze = false;

    // Skip over DEBUG values and predicated nonterminators.
    while (I->isDebugInstr() || !I->isTerminator()) {
      if (I == MBB.begin())
        return false;
      --I;
    }

    if (isIndirectBranchOpcode(I->getOpcode()) ||
        isJumpTableBranchOpcode(I->getOpcode())) {
      // Indirect branches and jump tables can't be analyzed, but we still want
      // to clean up any instructions at the tail of the basic block.
      CantAnalyze = true;
    } else if (isUncondBranchOpcode(I->getOpcode())) {
      TBB = I->getOperand(0).getMBB();
    } else if (isCondBranchOpcode(I->getOpcode())) {
      // Bail out if we encounter multiple conditional branches.
      if (!Cond.empty())
        return true;

      assert(!FBB && "FBB should have been null.");
      FBB = TBB;
      TBB = I->getOperand(0).getMBB();
      Cond.push_back(I->getOperand(1));
      Cond.push_back(I->getOperand(2));
    } else if (I->isReturn()) {
      // Returns can't be analyzed, but we should run cleanup.
      CantAnalyze = !isPredicated(*I);
    } else {
      // We encountered other unrecognized terminator. Bail out immediately.
      return true;
    }

    // Cleanup code - to be run for unpredicated unconditional branches and
    //                returns.
    if (!isPredicated(*I) &&
          (isUncondBranchOpcode(I->getOpcode()) ||
           isIndirectBranchOpcode(I->getOpcode()) ||
           isJumpTableBranchOpcode(I->getOpcode()) ||
           I->isReturn())) {
      // Forget any previous condition branch information - it no longer applies.
      Cond.clear();
      FBB = nullptr;

      // If we can modify the function, delete everything below this
      // unconditional branch.
      if (AllowModify) {
        MachineBasicBlock::iterator DI = std::next(I);
        while (DI != MBB.end()) {
          MachineInstr &InstToDelete = *DI;
          ++DI;
          InstToDelete.eraseFromParent();
        }
      }
    }

    if (CantAnalyze)
      return true;

    if (I == MBB.begin())
      return false;

    --I;
  }

  // We made it past the terminators without bailing out - we must have
  // analyzed this branch successfully.
  return false;
}

unsigned ARMBaseInstrInfo::removeBranch(MachineBasicBlock &MBB,
                                        int *BytesRemoved) const {
  assert(!BytesRemoved && "code size not handled");

  MachineBasicBlock::iterator I = MBB.getLastNonDebugInstr();
  if (I == MBB.end())
    return 0;

  if (!isUncondBranchOpcode(I->getOpcode()) &&
      !isCondBranchOpcode(I->getOpcode()))
    return 0;

  // Remove the branch.
  I->eraseFromParent();

  I = MBB.end();

  if (I == MBB.begin()) return 1;
  --I;
  if (!isCondBranchOpcode(I->getOpcode()))
    return 1;

  // Remove the branch.
  I->eraseFromParent();
  return 2;
}

unsigned ARMBaseInstrInfo::insertBranch(MachineBasicBlock &MBB,
                                        MachineBasicBlock *TBB,
                                        MachineBasicBlock *FBB,
                                        ArrayRef<MachineOperand> Cond,
                                        const DebugLoc &DL,
                                        int *BytesAdded) const {
  assert(!BytesAdded && "code size not handled");
  ARMFunctionInfo *AFI = MBB.getParent()->getInfo<ARMFunctionInfo>();
  int BOpc   = !AFI->isThumbFunction()
    ? ARM::B : (AFI->isThumb2Function() ? ARM::t2B : ARM::tB);
  int BccOpc = !AFI->isThumbFunction()
    ? ARM::Bcc : (AFI->isThumb2Function() ? ARM::t2Bcc : ARM::tBcc);
  bool isThumb = AFI->isThumbFunction() || AFI->isThumb2Function();

  // Shouldn't be a fall through.
  assert(TBB && "insertBranch must not be told to insert a fallthrough");
  assert((Cond.size() == 2 || Cond.size() == 0) &&
         "ARM branch conditions have two components!");

  // For conditional branches, we use addOperand to preserve CPSR flags.

  if (!FBB) {
    if (Cond.empty()) { // Unconditional branch?
      if (isThumb)
        BuildMI(&MBB, DL, get(BOpc)).addMBB(TBB).add(predOps(ARMCC::AL));
      else
        BuildMI(&MBB, DL, get(BOpc)).addMBB(TBB);
    } else
      BuildMI(&MBB, DL, get(BccOpc))
          .addMBB(TBB)
          .addImm(Cond[0].getImm())
          .add(Cond[1]);
    return 1;
  }

  // Two-way conditional branch.
  BuildMI(&MBB, DL, get(BccOpc))
      .addMBB(TBB)
      .addImm(Cond[0].getImm())
      .add(Cond[1]);
  if (isThumb)
    BuildMI(&MBB, DL, get(BOpc)).addMBB(FBB).add(predOps(ARMCC::AL));
  else
    BuildMI(&MBB, DL, get(BOpc)).addMBB(FBB);
  return 2;
}

bool ARMBaseInstrInfo::
reverseBranchCondition(SmallVectorImpl<MachineOperand> &Cond) const {
  ARMCC::CondCodes CC = (ARMCC::CondCodes)(int)Cond[0].getImm();
  Cond[0].setImm(ARMCC::getOppositeCondition(CC));
  return false;
}

bool ARMBaseInstrInfo::isPredicated(const MachineInstr &MI) const {
  if (MI.isBundle()) {
    MachineBasicBlock::const_instr_iterator I = MI.getIterator();
    MachineBasicBlock::const_instr_iterator E = MI.getParent()->instr_end();
    while (++I != E && I->isInsideBundle()) {
      int PIdx = I->findFirstPredOperandIdx();
      if (PIdx != -1 && I->getOperand(PIdx).getImm() != ARMCC::AL)
        return true;
    }
    return false;
  }

  int PIdx = MI.findFirstPredOperandIdx();
  return PIdx != -1 && MI.getOperand(PIdx).getImm() != ARMCC::AL;
}

bool ARMBaseInstrInfo::PredicateInstruction(
    MachineInstr &MI, ArrayRef<MachineOperand> Pred) const {
  unsigned Opc = MI.getOpcode();
  if (isUncondBranchOpcode(Opc)) {
    MI.setDesc(get(getMatchingCondBranchOpcode(Opc)));
    MachineInstrBuilder(*MI.getParent()->getParent(), MI)
      .addImm(Pred[0].getImm())
      .addReg(Pred[1].getReg());
    return true;
  }

  int PIdx = MI.findFirstPredOperandIdx();
  if (PIdx != -1) {
    MachineOperand &PMO = MI.getOperand(PIdx);
    PMO.setImm(Pred[0].getImm());
    MI.getOperand(PIdx+1).setReg(Pred[1].getReg());
    return true;
  }
  return false;
}

bool ARMBaseInstrInfo::SubsumesPredicate(ArrayRef<MachineOperand> Pred1,
                                         ArrayRef<MachineOperand> Pred2) const {
  if (Pred1.size() > 2 || Pred2.size() > 2)
    return false;

  ARMCC::CondCodes CC1 = (ARMCC::CondCodes)Pred1[0].getImm();
  ARMCC::CondCodes CC2 = (ARMCC::CondCodes)Pred2[0].getImm();
  if (CC1 == CC2)
    return true;

  switch (CC1) {
  default:
    return false;
  case ARMCC::AL:
    return true;
  case ARMCC::HS:
    return CC2 == ARMCC::HI;
  case ARMCC::LS:
    return CC2 == ARMCC::LO || CC2 == ARMCC::EQ;
  case ARMCC::GE:
    return CC2 == ARMCC::GT;
  case ARMCC::LE:
    return CC2 == ARMCC::LT;
  }
}

bool ARMBaseInstrInfo::DefinesPredicate(
    MachineInstr &MI, std::vector<MachineOperand> &Pred) const {
  bool Found = false;
  for (unsigned i = 0, e = MI.getNumOperands(); i != e; ++i) {
    const MachineOperand &MO = MI.getOperand(i);
    if ((MO.isRegMask() && MO.clobbersPhysReg(ARM::CPSR)) ||
        (MO.isReg() && MO.isDef() && MO.getReg() == ARM::CPSR)) {
      Pred.push_back(MO);
      Found = true;
    }
  }

  return Found;
}

bool ARMBaseInstrInfo::isCPSRDefined(const MachineInstr &MI) {
  for (const auto &MO : MI.operands())
    if (MO.isReg() && MO.getReg() == ARM::CPSR && MO.isDef() && !MO.isDead())
      return true;
  return false;
}

bool ARMBaseInstrInfo::isAddrMode3OpImm(const MachineInstr &MI,
                                        unsigned Op) const {
  const MachineOperand &Offset = MI.getOperand(Op + 1);
  return Offset.getReg() != 0;
}

// Load with negative register offset requires additional 1cyc and +I unit
// for Cortex A57
bool ARMBaseInstrInfo::isAddrMode3OpMinusReg(const MachineInstr &MI,
                                             unsigned Op) const {
  const MachineOperand &Offset = MI.getOperand(Op + 1);
  const MachineOperand &Opc = MI.getOperand(Op + 2);
  assert(Opc.isImm());
  assert(Offset.isReg());
  int64_t OpcImm = Opc.getImm();

  bool isSub = ARM_AM::getAM3Op(OpcImm) == ARM_AM::sub;
  return (isSub && Offset.getReg() != 0);
}

bool ARMBaseInstrInfo::isLdstScaledReg(const MachineInstr &MI,
                                       unsigned Op) const {
  const MachineOperand &Opc = MI.getOperand(Op + 2);
  unsigned OffImm = Opc.getImm();
  return ARM_AM::getAM2ShiftOpc(OffImm) != ARM_AM::no_shift;
}

// Load, scaled register offset, not plus LSL2
bool ARMBaseInstrInfo::isLdstScaledRegNotPlusLsl2(const MachineInstr &MI,
                                                  unsigned Op) const {
  const MachineOperand &Opc = MI.getOperand(Op + 2);
  unsigned OffImm = Opc.getImm();

  bool isAdd = ARM_AM::getAM2Op(OffImm) == ARM_AM::add;
  unsigned Amt = ARM_AM::getAM2Offset(OffImm);
  ARM_AM::ShiftOpc ShiftOpc = ARM_AM::getAM2ShiftOpc(OffImm);
  if (ShiftOpc == ARM_AM::no_shift) return false; // not scaled
  bool SimpleScaled = (isAdd && ShiftOpc == ARM_AM::lsl && Amt == 2);
  return !SimpleScaled;
}

// Minus reg for ldstso addr mode
bool ARMBaseInstrInfo::isLdstSoMinusReg(const MachineInstr &MI,
                                        unsigned Op) const {
  unsigned OffImm = MI.getOperand(Op + 2).getImm();
  return ARM_AM::getAM2Op(OffImm) == ARM_AM::sub;
}

// Load, scaled register offset
bool ARMBaseInstrInfo::isAm2ScaledReg(const MachineInstr &MI,
                                      unsigned Op) const {
  unsigned OffImm = MI.getOperand(Op + 2).getImm();
  return ARM_AM::getAM2ShiftOpc(OffImm) != ARM_AM::no_shift;
}

static bool isEligibleForITBlock(const MachineInstr *MI) {
  switch (MI->getOpcode()) {
  default: return true;
  case ARM::tADC:   // ADC (register) T1
  case ARM::tADDi3: // ADD (immediate) T1
  case ARM::tADDi8: // ADD (immediate) T2
  case ARM::tADDrr: // ADD (register) T1
  case ARM::tAND:   // AND (register) T1
  case ARM::tASRri: // ASR (immediate) T1
  case ARM::tASRrr: // ASR (register) T1
  case ARM::tBIC:   // BIC (register) T1
  case ARM::tEOR:   // EOR (register) T1
  case ARM::tLSLri: // LSL (immediate) T1
  case ARM::tLSLrr: // LSL (register) T1
  case ARM::tLSRri: // LSR (immediate) T1
  case ARM::tLSRrr: // LSR (register) T1
  case ARM::tMUL:   // MUL T1
  case ARM::tMVN:   // MVN (register) T1
  case ARM::tORR:   // ORR (register) T1
  case ARM::tROR:   // ROR (register) T1
  case ARM::tRSB:   // RSB (immediate) T1
  case ARM::tSBC:   // SBC (register) T1
  case ARM::tSUBi3: // SUB (immediate) T1
  case ARM::tSUBi8: // SUB (immediate) T2
  case ARM::tSUBrr: // SUB (register) T1
    return !ARMBaseInstrInfo::isCPSRDefined(*MI);
  }
}

/// isPredicable - Return true if the specified instruction can be predicated.
/// By default, this returns true for every instruction with a
/// PredicateOperand.
bool ARMBaseInstrInfo::isPredicable(const MachineInstr &MI) const {
  if (!MI.isPredicable())
    return false;

  if (MI.isBundle())
    return false;

  if (!isEligibleForITBlock(&MI))
    return false;

  const ARMFunctionInfo *AFI =
      MI.getParent()->getParent()->getInfo<ARMFunctionInfo>();

  // Neon instructions in Thumb2 IT blocks are deprecated, see ARMARM.
  // In their ARM encoding, they can't be encoded in a conditional form.
  if ((MI.getDesc().TSFlags & ARMII::DomainMask) == ARMII::DomainNEON)
    return false;

  if (AFI->isThumb2Function()) {
    if (getSubtarget().restrictIT())
      return isV8EligibleForIT(&MI);
  }

  return true;
}

namespace llvm {

template <> bool IsCPSRDead<MachineInstr>(const MachineInstr *MI) {
  for (unsigned i = 0, e = MI->getNumOperands(); i != e; ++i) {
    const MachineOperand &MO = MI->getOperand(i);
    if (!MO.isReg() || MO.isUndef() || MO.isUse())
      continue;
    if (MO.getReg() != ARM::CPSR)
      continue;
    if (!MO.isDead())
      return false;
  }
  // all definitions of CPSR are dead
  return true;
}

} // end namespace llvm

/// GetInstSize - Return the size of the specified MachineInstr.
///
unsigned ARMBaseInstrInfo::getInstSizeInBytes(const MachineInstr &MI) const {
  const MachineBasicBlock &MBB = *MI.getParent();
  const MachineFunction *MF = MBB.getParent();
  const MCAsmInfo *MAI = MF->getTarget().getMCAsmInfo();

  const MCInstrDesc &MCID = MI.getDesc();
  if (MCID.getSize())
    return MCID.getSize();

  // If this machine instr is an inline asm, measure it.
  if (MI.getOpcode() == ARM::INLINEASM) {
    unsigned Size = getInlineAsmLength(MI.getOperand(0).getSymbolName(), *MAI);
    if (!MF->getInfo<ARMFunctionInfo>()->isThumbFunction())
      Size = alignTo(Size, 4);
    return Size;
  }
  unsigned Opc = MI.getOpcode();
  switch (Opc) {
  default:
    // pseudo-instruction sizes are zero.
    return 0;
  case TargetOpcode::BUNDLE:
    return getInstBundleLength(MI);
  case ARM::MOVi16_ga_pcrel:
  case ARM::MOVTi16_ga_pcrel:
  case ARM::t2MOVi16_ga_pcrel:
  case ARM::t2MOVTi16_ga_pcrel:
    return 4;
  case ARM::MOVi32imm:
  case ARM::t2MOVi32imm:
    return 8;
  case ARM::CONSTPOOL_ENTRY:
  case ARM::JUMPTABLE_INSTS:
  case ARM::JUMPTABLE_ADDRS:
  case ARM::JUMPTABLE_TBB:
  case ARM::JUMPTABLE_TBH:
    // If this machine instr is a constant pool entry, its size is recorded as
    // operand #2.
    return MI.getOperand(2).getImm();
  case ARM::Int_eh_sjlj_longjmp:
    return 16;
  case ARM::tInt_eh_sjlj_longjmp:
    return 10;
  case ARM::tInt_WIN_eh_sjlj_longjmp:
    return 12;
  case ARM::Int_eh_sjlj_setjmp:
  case ARM::Int_eh_sjlj_setjmp_nofp:
    return 20;
  case ARM::tInt_eh_sjlj_setjmp:
  case ARM::t2Int_eh_sjlj_setjmp:
  case ARM::t2Int_eh_sjlj_setjmp_nofp:
    return 12;
  case ARM::SPACE:
    return MI.getOperand(1).getImm();
  }
}

unsigned ARMBaseInstrInfo::getInstBundleLength(const MachineInstr &MI) const {
  unsigned Size = 0;
  MachineBasicBlock::const_instr_iterator I = MI.getIterator();
  MachineBasicBlock::const_instr_iterator E = MI.getParent()->instr_end();
  while (++I != E && I->isInsideBundle()) {
    assert(!I->isBundle() && "No nested bundle!");
    Size += getInstSizeInBytes(*I);
  }
  return Size;
}

void ARMBaseInstrInfo::copyFromCPSR(MachineBasicBlock &MBB,
                                    MachineBasicBlock::iterator I,
                                    unsigned DestReg, bool KillSrc,
                                    const ARMSubtarget &Subtarget) const {
  unsigned Opc = Subtarget.isThumb()
                     ? (Subtarget.isMClass() ? ARM::t2MRS_M : ARM::t2MRS_AR)
                     : ARM::MRS;

  MachineInstrBuilder MIB =
      BuildMI(MBB, I, I->getDebugLoc(), get(Opc), DestReg);

  // There is only 1 A/R class MRS instruction, and it always refers to
  // APSR. However, there are lots of other possibilities on M-class cores.
  if (Subtarget.isMClass())
    MIB.addImm(0x800);

  MIB.add(predOps(ARMCC::AL))
     .addReg(ARM::CPSR, RegState::Implicit | getKillRegState(KillSrc));
}

void ARMBaseInstrInfo::copyToCPSR(MachineBasicBlock &MBB,
                                  MachineBasicBlock::iterator I,
                                  unsigned SrcReg, bool KillSrc,
                                  const ARMSubtarget &Subtarget) const {
  unsigned Opc = Subtarget.isThumb()
                     ? (Subtarget.isMClass() ? ARM::t2MSR_M : ARM::t2MSR_AR)
                     : ARM::MSR;

  MachineInstrBuilder MIB = BuildMI(MBB, I, I->getDebugLoc(), get(Opc));

  if (Subtarget.isMClass())
    MIB.addImm(0x800);
  else
    MIB.addImm(8);

  MIB.addReg(SrcReg, getKillRegState(KillSrc))
     .add(predOps(ARMCC::AL))
     .addReg(ARM::CPSR, RegState::Implicit | RegState::Define);
}

void ARMBaseInstrInfo::copyPhysReg(MachineBasicBlock &MBB,
                                   MachineBasicBlock::iterator I,
                                   const DebugLoc &DL, unsigned DestReg,
                                   unsigned SrcReg, bool KillSrc) const {
  bool GPRDest = ARM::GPRRegClass.contains(DestReg);
  bool GPRSrc = ARM::GPRRegClass.contains(SrcReg);

  if (GPRDest && GPRSrc) {
    BuildMI(MBB, I, DL, get(ARM::MOVr), DestReg)
        .addReg(SrcReg, getKillRegState(KillSrc))
        .add(predOps(ARMCC::AL))
        .add(condCodeOp());
    return;
  }

  bool SPRDest = ARM::SPRRegClass.contains(DestReg);
  bool SPRSrc = ARM::SPRRegClass.contains(SrcReg);

  unsigned Opc = 0;
  if (SPRDest && SPRSrc)
    Opc = ARM::VMOVS;
  else if (GPRDest && SPRSrc)
    Opc = ARM::VMOVRS;
  else if (SPRDest && GPRSrc)
    Opc = ARM::VMOVSR;
  else if (ARM::DPRRegClass.contains(DestReg, SrcReg) && !Subtarget.isFPOnlySP())
    Opc = ARM::VMOVD;
  else if (ARM::QPRRegClass.contains(DestReg, SrcReg))
    Opc = ARM::VORRq;

  if (Opc) {
    MachineInstrBuilder MIB = BuildMI(MBB, I, DL, get(Opc), DestReg);
    MIB.addReg(SrcReg, getKillRegState(KillSrc));
    if (Opc == ARM::VORRq)
      MIB.addReg(SrcReg, getKillRegState(KillSrc));
    MIB.add(predOps(ARMCC::AL));
    return;
  }

  // Handle register classes that require multiple instructions.
  unsigned BeginIdx = 0;
  unsigned SubRegs = 0;
  int Spacing = 1;

  // Use VORRq when possible.
  if (ARM::QQPRRegClass.contains(DestReg, SrcReg)) {
    Opc = ARM::VORRq;
    BeginIdx = ARM::qsub_0;
    SubRegs = 2;
  } else if (ARM::QQQQPRRegClass.contains(DestReg, SrcReg)) {
    Opc = ARM::VORRq;
    BeginIdx = ARM::qsub_0;
    SubRegs = 4;
  // Fall back to VMOVD.
  } else if (ARM::DPairRegClass.contains(DestReg, SrcReg)) {
    Opc = ARM::VMOVD;
    BeginIdx = ARM::dsub_0;
    SubRegs = 2;
  } else if (ARM::DTripleRegClass.contains(DestReg, SrcReg)) {
    Opc = ARM::VMOVD;
    BeginIdx = ARM::dsub_0;
    SubRegs = 3;
  } else if (ARM::DQuadRegClass.contains(DestReg, SrcReg)) {
    Opc = ARM::VMOVD;
    BeginIdx = ARM::dsub_0;
    SubRegs = 4;
  } else if (ARM::GPRPairRegClass.contains(DestReg, SrcReg)) {
    Opc = Subtarget.isThumb2() ? ARM::tMOVr : ARM::MOVr;
    BeginIdx = ARM::gsub_0;
    SubRegs = 2;
  } else if (ARM::DPairSpcRegClass.contains(DestReg, SrcReg)) {
    Opc = ARM::VMOVD;
    BeginIdx = ARM::dsub_0;
    SubRegs = 2;
    Spacing = 2;
  } else if (ARM::DTripleSpcRegClass.contains(DestReg, SrcReg)) {
    Opc = ARM::VMOVD;
    BeginIdx = ARM::dsub_0;
    SubRegs = 3;
    Spacing = 2;
  } else if (ARM::DQuadSpcRegClass.contains(DestReg, SrcReg)) {
    Opc = ARM::VMOVD;
    BeginIdx = ARM::dsub_0;
    SubRegs = 4;
    Spacing = 2;
  } else if (ARM::DPRRegClass.contains(DestReg, SrcReg) && Subtarget.isFPOnlySP()) {
    Opc = ARM::VMOVS;
    BeginIdx = ARM::ssub_0;
    SubRegs = 2;
  } else if (SrcReg == ARM::CPSR) {
    copyFromCPSR(MBB, I, DestReg, KillSrc, Subtarget);
    return;
  } else if (DestReg == ARM::CPSR) {
    copyToCPSR(MBB, I, SrcReg, KillSrc, Subtarget);
    return;
  }

  assert(Opc && "Impossible reg-to-reg copy");

  const TargetRegisterInfo *TRI = &getRegisterInfo();
  MachineInstrBuilder Mov;

  // Copy register tuples backward when the first Dest reg overlaps with SrcReg.
  if (TRI->regsOverlap(SrcReg, TRI->getSubReg(DestReg, BeginIdx))) {
    BeginIdx = BeginIdx + ((SubRegs - 1) * Spacing);
    Spacing = -Spacing;
  }
#ifndef NDEBUG
  SmallSet<unsigned, 4> DstRegs;
#endif
  for (unsigned i = 0; i != SubRegs; ++i) {
    unsigned Dst = TRI->getSubReg(DestReg, BeginIdx + i * Spacing);
    unsigned Src = TRI->getSubReg(SrcReg, BeginIdx + i * Spacing);
    assert(Dst && Src && "Bad sub-register");
#ifndef NDEBUG
    assert(!DstRegs.count(Src) && "destructive vector copy");
    DstRegs.insert(Dst);
#endif
    Mov = BuildMI(MBB, I, I->getDebugLoc(), get(Opc), Dst).addReg(Src);
    // VORR takes two source operands.
    if (Opc == ARM::VORRq)
      Mov.addReg(Src);
    Mov = Mov.add(predOps(ARMCC::AL));
    // MOVr can set CC.
    if (Opc == ARM::MOVr)
      Mov = Mov.add(condCodeOp());
  }
  // Add implicit super-register defs and kills to the last instruction.
  Mov->addRegisterDefined(DestReg, TRI);
  if (KillSrc)
    Mov->addRegisterKilled(SrcReg, TRI);
}

bool ARMBaseInstrInfo::isCopyInstrImpl(const MachineInstr &MI,
                                       const MachineOperand *&Src,
                                       const MachineOperand *&Dest) const {
  // VMOVRRD is also a copy instruction but it requires
  // special way of handling. It is more complex copy version
  // and since that we are not considering it. For recognition
  // of such instruction isExtractSubregLike MI interface fuction
  // could be used.
  // VORRq is considered as a move only if two inputs are
  // the same register.
  if (!MI.isMoveReg() ||
      (MI.getOpcode() == ARM::VORRq &&
       MI.getOperand(1).getReg() != MI.getOperand(2).getReg()))
    return false;
  Dest = &MI.getOperand(0);
  Src = &MI.getOperand(1);
  return true;
}

const MachineInstrBuilder &
ARMBaseInstrInfo::AddDReg(MachineInstrBuilder &MIB, unsigned Reg,
                          unsigned SubIdx, unsigned State,
                          const TargetRegisterInfo *TRI) const {
  if (!SubIdx)
    return MIB.addReg(Reg, State);

  if (TargetRegisterInfo::isPhysicalRegister(Reg))
    return MIB.addReg(TRI->getSubReg(Reg, SubIdx), State);
  return MIB.addReg(Reg, State, SubIdx);
}

void ARMBaseInstrInfo::
storeRegToStackSlot(MachineBasicBlock &MBB, MachineBasicBlock::iterator I,
                    unsigned SrcReg, bool isKill, int FI,
                    const TargetRegisterClass *RC,
                    const TargetRegisterInfo *TRI) const {
  MachineFunction &MF = *MBB.getParent();
  MachineFrameInfo &MFI = MF.getFrameInfo();
  unsigned Align = MFI.getObjectAlignment(FI);

  MachineMemOperand *MMO = MF.getMachineMemOperand(
      MachinePointerInfo::getFixedStack(MF, FI), MachineMemOperand::MOStore,
      MFI.getObjectSize(FI), Align);

  switch (TRI->getSpillSize(*RC)) {
    case 2:
      if (ARM::HPRRegClass.hasSubClassEq(RC)) {
        BuildMI(MBB, I, DebugLoc(), get(ARM::VSTRH))
            .addReg(SrcReg, getKillRegState(isKill))
            .addFrameIndex(FI)
            .addImm(0)
            .addMemOperand(MMO)
            .add(predOps(ARMCC::AL));
      } else
        llvm_unreachable("Unknown reg class!");
      break;
    case 4:
      if (ARM::GPRRegClass.hasSubClassEq(RC)) {
        BuildMI(MBB, I, DebugLoc(), get(ARM::STRi12))
            .addReg(SrcReg, getKillRegState(isKill))
            .addFrameIndex(FI)
            .addImm(0)
            .addMemOperand(MMO)
            .add(predOps(ARMCC::AL));
      } else if (ARM::SPRRegClass.hasSubClassEq(RC)) {
        BuildMI(MBB, I, DebugLoc(), get(ARM::VSTRS))
            .addReg(SrcReg, getKillRegState(isKill))
            .addFrameIndex(FI)
            .addImm(0)
            .addMemOperand(MMO)
            .add(predOps(ARMCC::AL));
      } else
        llvm_unreachable("Unknown reg class!");
      break;
    case 8:
      if (ARM::DPRRegClass.hasSubClassEq(RC)) {
        BuildMI(MBB, I, DebugLoc(), get(ARM::VSTRD))
            .addReg(SrcReg, getKillRegState(isKill))
            .addFrameIndex(FI)
            .addImm(0)
            .addMemOperand(MMO)
            .add(predOps(ARMCC::AL));
      } else if (ARM::GPRPairRegClass.hasSubClassEq(RC)) {
        if (Subtarget.hasV5TEOps()) {
          MachineInstrBuilder MIB = BuildMI(MBB, I, DebugLoc(), get(ARM::STRD));
          AddDReg(MIB, SrcReg, ARM::gsub_0, getKillRegState(isKill), TRI);
          AddDReg(MIB, SrcReg, ARM::gsub_1, 0, TRI);
          MIB.addFrameIndex(FI).addReg(0).addImm(0).addMemOperand(MMO)
             .add(predOps(ARMCC::AL));
        } else {
          // Fallback to STM instruction, which has existed since the dawn of
          // time.
          MachineInstrBuilder MIB = BuildMI(MBB, I, DebugLoc(), get(ARM::STMIA))
                                        .addFrameIndex(FI)
                                        .addMemOperand(MMO)
                                        .add(predOps(ARMCC::AL));
          AddDReg(MIB, SrcReg, ARM::gsub_0, getKillRegState(isKill), TRI);
          AddDReg(MIB, SrcReg, ARM::gsub_1, 0, TRI);
        }
      } else
        llvm_unreachable("Unknown reg class!");
      break;
    case 16:
      if (ARM::DPairRegClass.hasSubClassEq(RC)) {
        // Use aligned spills if the stack can be realigned.
        if (Align >= 16 && getRegisterInfo().canRealignStack(MF)) {
          BuildMI(MBB, I, DebugLoc(), get(ARM::VST1q64))
              .addFrameIndex(FI)
              .addImm(16)
              .addReg(SrcReg, getKillRegState(isKill))
              .addMemOperand(MMO)
              .add(predOps(ARMCC::AL));
        } else {
          BuildMI(MBB, I, DebugLoc(), get(ARM::VSTMQIA))
              .addReg(SrcReg, getKillRegState(isKill))
              .addFrameIndex(FI)
              .addMemOperand(MMO)
              .add(predOps(ARMCC::AL));
        }
      } else
        llvm_unreachable("Unknown reg class!");
      break;
    case 24:
      if (ARM::DTripleRegClass.hasSubClassEq(RC)) {
        // Use aligned spills if the stack can be realigned.
        if (Align >= 16 && getRegisterInfo().canRealignStack(MF)) {
          BuildMI(MBB, I, DebugLoc(), get(ARM::VST1d64TPseudo))
              .addFrameIndex(FI)
              .addImm(16)
              .addReg(SrcReg, getKillRegState(isKill))
              .addMemOperand(MMO)
              .add(predOps(ARMCC::AL));
        } else {
          MachineInstrBuilder MIB = BuildMI(MBB, I, DebugLoc(),
                                            get(ARM::VSTMDIA))
                                        .addFrameIndex(FI)
                                        .add(predOps(ARMCC::AL))
                                        .addMemOperand(MMO);
          MIB = AddDReg(MIB, SrcReg, ARM::dsub_0, getKillRegState(isKill), TRI);
          MIB = AddDReg(MIB, SrcReg, ARM::dsub_1, 0, TRI);
          AddDReg(MIB, SrcReg, ARM::dsub_2, 0, TRI);
        }
      } else
        llvm_unreachable("Unknown reg class!");
      break;
    case 32:
      if (ARM::QQPRRegClass.hasSubClassEq(RC) || ARM::DQuadRegClass.hasSubClassEq(RC)) {
        if (Align >= 16 && getRegisterInfo().canRealignStack(MF)) {
          // FIXME: It's possible to only store part of the QQ register if the
          // spilled def has a sub-register index.
          BuildMI(MBB, I, DebugLoc(), get(ARM::VST1d64QPseudo))
              .addFrameIndex(FI)
              .addImm(16)
              .addReg(SrcReg, getKillRegState(isKill))
              .addMemOperand(MMO)
              .add(predOps(ARMCC::AL));
        } else {
          MachineInstrBuilder MIB = BuildMI(MBB, I, DebugLoc(),
                                            get(ARM::VSTMDIA))
                                        .addFrameIndex(FI)
                                        .add(predOps(ARMCC::AL))
                                        .addMemOperand(MMO);
          MIB = AddDReg(MIB, SrcReg, ARM::dsub_0, getKillRegState(isKill), TRI);
          MIB = AddDReg(MIB, SrcReg, ARM::dsub_1, 0, TRI);
          MIB = AddDReg(MIB, SrcReg, ARM::dsub_2, 0, TRI);
                AddDReg(MIB, SrcReg, ARM::dsub_3, 0, TRI);
        }
      } else
        llvm_unreachable("Unknown reg class!");
      break;
    case 64:
      if (ARM::QQQQPRRegClass.hasSubClassEq(RC)) {
        MachineInstrBuilder MIB = BuildMI(MBB, I, DebugLoc(), get(ARM::VSTMDIA))
                                      .addFrameIndex(FI)
                                      .add(predOps(ARMCC::AL))
                                      .addMemOperand(MMO);
        MIB = AddDReg(MIB, SrcReg, ARM::dsub_0, getKillRegState(isKill), TRI);
        MIB = AddDReg(MIB, SrcReg, ARM::dsub_1, 0, TRI);
        MIB = AddDReg(MIB, SrcReg, ARM::dsub_2, 0, TRI);
        MIB = AddDReg(MIB, SrcReg, ARM::dsub_3, 0, TRI);
        MIB = AddDReg(MIB, SrcReg, ARM::dsub_4, 0, TRI);
        MIB = AddDReg(MIB, SrcReg, ARM::dsub_5, 0, TRI);
        MIB = AddDReg(MIB, SrcReg, ARM::dsub_6, 0, TRI);
              AddDReg(MIB, SrcReg, ARM::dsub_7, 0, TRI);
      } else
        llvm_unreachable("Unknown reg class!");
      break;
    default:
      llvm_unreachable("Unknown reg class!");
  }
}

unsigned ARMBaseInstrInfo::isStoreToStackSlot(const MachineInstr &MI,
                                              int &FrameIndex) const {
  switch (MI.getOpcode()) {
  default: break;
  case ARM::STRrs:
  case ARM::t2STRs: // FIXME: don't use t2STRs to access frame.
    if (MI.getOperand(1).isFI() && MI.getOperand(2).isReg() &&
        MI.getOperand(3).isImm() && MI.getOperand(2).getReg() == 0 &&
        MI.getOperand(3).getImm() == 0) {
      FrameIndex = MI.getOperand(1).getIndex();
      return MI.getOperand(0).getReg();
    }
    break;
  case ARM::STRi12:
  case ARM::t2STRi12:
  case ARM::tSTRspi:
  case ARM::VSTRD:
  case ARM::VSTRS:
    if (MI.getOperand(1).isFI() && MI.getOperand(2).isImm() &&
        MI.getOperand(2).getImm() == 0) {
      FrameIndex = MI.getOperand(1).getIndex();
      return MI.getOperand(0).getReg();
    }
    break;
  case ARM::VST1q64:
  case ARM::VST1d64TPseudo:
  case ARM::VST1d64QPseudo:
    if (MI.getOperand(0).isFI() && MI.getOperand(2).getSubReg() == 0) {
      FrameIndex = MI.getOperand(0).getIndex();
      return MI.getOperand(2).getReg();
    }
    break;
  case ARM::VSTMQIA:
    if (MI.getOperand(1).isFI() && MI.getOperand(0).getSubReg() == 0) {
      FrameIndex = MI.getOperand(1).getIndex();
      return MI.getOperand(0).getReg();
    }
    break;
  }

  return 0;
}

unsigned ARMBaseInstrInfo::isStoreToStackSlotPostFE(const MachineInstr &MI,
                                                    int &FrameIndex) const {
  SmallVector<const MachineMemOperand *, 1> Accesses;
  if (MI.mayStore() && hasStoreToStackSlot(MI, Accesses)) {
    FrameIndex =
        cast<FixedStackPseudoSourceValue>(Accesses.front()->getPseudoValue())
            ->getFrameIndex();
    return true;
  }
  return false;
}

void ARMBaseInstrInfo::
loadRegFromStackSlot(MachineBasicBlock &MBB, MachineBasicBlock::iterator I,
                     unsigned DestReg, int FI,
                     const TargetRegisterClass *RC,
                     const TargetRegisterInfo *TRI) const {
  DebugLoc DL;
  if (I != MBB.end()) DL = I->getDebugLoc();
  MachineFunction &MF = *MBB.getParent();
  MachineFrameInfo &MFI = MF.getFrameInfo();
  unsigned Align = MFI.getObjectAlignment(FI);
  MachineMemOperand *MMO = MF.getMachineMemOperand(
      MachinePointerInfo::getFixedStack(MF, FI), MachineMemOperand::MOLoad,
      MFI.getObjectSize(FI), Align);

  switch (TRI->getSpillSize(*RC)) {
  case 2:
    if (ARM::HPRRegClass.hasSubClassEq(RC)) {
      BuildMI(MBB, I, DL, get(ARM::VLDRH), DestReg)
          .addFrameIndex(FI)
          .addImm(0)
          .addMemOperand(MMO)
          .add(predOps(ARMCC::AL));
    } else
      llvm_unreachable("Unknown reg class!");
    break;
  case 4:
    if (ARM::GPRRegClass.hasSubClassEq(RC)) {
      BuildMI(MBB, I, DL, get(ARM::LDRi12), DestReg)
          .addFrameIndex(FI)
          .addImm(0)
          .addMemOperand(MMO)
          .add(predOps(ARMCC::AL));
    } else if (ARM::SPRRegClass.hasSubClassEq(RC)) {
      BuildMI(MBB, I, DL, get(ARM::VLDRS), DestReg)
          .addFrameIndex(FI)
          .addImm(0)
          .addMemOperand(MMO)
          .add(predOps(ARMCC::AL));
    } else
      llvm_unreachable("Unknown reg class!");
    break;
  case 8:
    if (ARM::DPRRegClass.hasSubClassEq(RC)) {
      BuildMI(MBB, I, DL, get(ARM::VLDRD), DestReg)
          .addFrameIndex(FI)
          .addImm(0)
          .addMemOperand(MMO)
          .add(predOps(ARMCC::AL));
    } else if (ARM::GPRPairRegClass.hasSubClassEq(RC)) {
      MachineInstrBuilder MIB;

      if (Subtarget.hasV5TEOps()) {
        MIB = BuildMI(MBB, I, DL, get(ARM::LDRD));
        AddDReg(MIB, DestReg, ARM::gsub_0, RegState::DefineNoRead, TRI);
        AddDReg(MIB, DestReg, ARM::gsub_1, RegState::DefineNoRead, TRI);
        MIB.addFrameIndex(FI).addReg(0).addImm(0).addMemOperand(MMO)
           .add(predOps(ARMCC::AL));
      } else {
        // Fallback to LDM instruction, which has existed since the dawn of
        // time.
        MIB = BuildMI(MBB, I, DL, get(ARM::LDMIA))
                  .addFrameIndex(FI)
                  .addMemOperand(MMO)
                  .add(predOps(ARMCC::AL));
        MIB = AddDReg(MIB, DestReg, ARM::gsub_0, RegState::DefineNoRead, TRI);
        MIB = AddDReg(MIB, DestReg, ARM::gsub_1, RegState::DefineNoRead, TRI);
      }

      if (TargetRegisterInfo::isPhysicalRegister(DestReg))
        MIB.addReg(DestReg, RegState::ImplicitDefine);
    } else
      llvm_unreachable("Unknown reg class!");
    break;
  case 16:
    if (ARM::DPairRegClass.hasSubClassEq(RC)) {
      if (Align >= 16 && getRegisterInfo().canRealignStack(MF)) {
        BuildMI(MBB, I, DL, get(ARM::VLD1q64), DestReg)
            .addFrameIndex(FI)
            .addImm(16)
            .addMemOperand(MMO)
            .add(predOps(ARMCC::AL));
      } else {
        BuildMI(MBB, I, DL, get(ARM::VLDMQIA), DestReg)
            .addFrameIndex(FI)
            .addMemOperand(MMO)
            .add(predOps(ARMCC::AL));
      }
    } else
      llvm_unreachable("Unknown reg class!");
    break;
  case 24:
    if (ARM::DTripleRegClass.hasSubClassEq(RC)) {
      if (Align >= 16 && getRegisterInfo().canRealignStack(MF)) {
        BuildMI(MBB, I, DL, get(ARM::VLD1d64TPseudo), DestReg)
            .addFrameIndex(FI)
            .addImm(16)
            .addMemOperand(MMO)
            .add(predOps(ARMCC::AL));
      } else {
        MachineInstrBuilder MIB = BuildMI(MBB, I, DL, get(ARM::VLDMDIA))
                                      .addFrameIndex(FI)
                                      .addMemOperand(MMO)
                                      .add(predOps(ARMCC::AL));
        MIB = AddDReg(MIB, DestReg, ARM::dsub_0, RegState::DefineNoRead, TRI);
        MIB = AddDReg(MIB, DestReg, ARM::dsub_1, RegState::DefineNoRead, TRI);
        MIB = AddDReg(MIB, DestReg, ARM::dsub_2, RegState::DefineNoRead, TRI);
        if (TargetRegisterInfo::isPhysicalRegister(DestReg))
          MIB.addReg(DestReg, RegState::ImplicitDefine);
      }
    } else
      llvm_unreachable("Unknown reg class!");
    break;
   case 32:
    if (ARM::QQPRRegClass.hasSubClassEq(RC) || ARM::DQuadRegClass.hasSubClassEq(RC)) {
      if (Align >= 16 && getRegisterInfo().canRealignStack(MF)) {
        BuildMI(MBB, I, DL, get(ARM::VLD1d64QPseudo), DestReg)
            .addFrameIndex(FI)
            .addImm(16)
            .addMemOperand(MMO)
            .add(predOps(ARMCC::AL));
      } else {
        MachineInstrBuilder MIB = BuildMI(MBB, I, DL, get(ARM::VLDMDIA))
                                      .addFrameIndex(FI)
                                      .add(predOps(ARMCC::AL))
                                      .addMemOperand(MMO);
        MIB = AddDReg(MIB, DestReg, ARM::dsub_0, RegState::DefineNoRead, TRI);
        MIB = AddDReg(MIB, DestReg, ARM::dsub_1, RegState::DefineNoRead, TRI);
        MIB = AddDReg(MIB, DestReg, ARM::dsub_2, RegState::DefineNoRead, TRI);
        MIB = AddDReg(MIB, DestReg, ARM::dsub_3, RegState::DefineNoRead, TRI);
        if (TargetRegisterInfo::isPhysicalRegister(DestReg))
          MIB.addReg(DestReg, RegState::ImplicitDefine);
      }
    } else
      llvm_unreachable("Unknown reg class!");
    break;
  case 64:
    if (ARM::QQQQPRRegClass.hasSubClassEq(RC)) {
      MachineInstrBuilder MIB = BuildMI(MBB, I, DL, get(ARM::VLDMDIA))
                                    .addFrameIndex(FI)
                                    .add(predOps(ARMCC::AL))
                                    .addMemOperand(MMO);
      MIB = AddDReg(MIB, DestReg, ARM::dsub_0, RegState::DefineNoRead, TRI);
      MIB = AddDReg(MIB, DestReg, ARM::dsub_1, RegState::DefineNoRead, TRI);
      MIB = AddDReg(MIB, DestReg, ARM::dsub_2, RegState::DefineNoRead, TRI);
      MIB = AddDReg(MIB, DestReg, ARM::dsub_3, RegState::DefineNoRead, TRI);
      MIB = AddDReg(MIB, DestReg, ARM::dsub_4, RegState::DefineNoRead, TRI);
      MIB = AddDReg(MIB, DestReg, ARM::dsub_5, RegState::DefineNoRead, TRI);
      MIB = AddDReg(MIB, DestReg, ARM::dsub_6, RegState::DefineNoRead, TRI);
      MIB = AddDReg(MIB, DestReg, ARM::dsub_7, RegState::DefineNoRead, TRI);
      if (TargetRegisterInfo::isPhysicalRegister(DestReg))
        MIB.addReg(DestReg, RegState::ImplicitDefine);
    } else
      llvm_unreachable("Unknown reg class!");
    break;
  default:
    llvm_unreachable("Unknown regclass!");
  }
}

unsigned ARMBaseInstrInfo::isLoadFromStackSlot(const MachineInstr &MI,
                                               int &FrameIndex) const {
  switch (MI.getOpcode()) {
  default: break;
  case ARM::LDRrs:
  case ARM::t2LDRs:  // FIXME: don't use t2LDRs to access frame.
    if (MI.getOperand(1).isFI() && MI.getOperand(2).isReg() &&
        MI.getOperand(3).isImm() && MI.getOperand(2).getReg() == 0 &&
        MI.getOperand(3).getImm() == 0) {
      FrameIndex = MI.getOperand(1).getIndex();
      return MI.getOperand(0).getReg();
    }
    break;
  case ARM::LDRi12:
  case ARM::t2LDRi12:
  case ARM::tLDRspi:
  case ARM::VLDRD:
  case ARM::VLDRS:
    if (MI.getOperand(1).isFI() && MI.getOperand(2).isImm() &&
        MI.getOperand(2).getImm() == 0) {
      FrameIndex = MI.getOperand(1).getIndex();
      return MI.getOperand(0).getReg();
    }
    break;
  case ARM::VLD1q64:
  case ARM::VLD1d8TPseudo:
  case ARM::VLD1d16TPseudo:
  case ARM::VLD1d32TPseudo:
  case ARM::VLD1d64TPseudo:
  case ARM::VLD1d8QPseudo:
  case ARM::VLD1d16QPseudo:
  case ARM::VLD1d32QPseudo:
  case ARM::VLD1d64QPseudo:
    if (MI.getOperand(1).isFI() && MI.getOperand(0).getSubReg() == 0) {
      FrameIndex = MI.getOperand(1).getIndex();
      return MI.getOperand(0).getReg();
    }
    break;
  case ARM::VLDMQIA:
    if (MI.getOperand(1).isFI() && MI.getOperand(0).getSubReg() == 0) {
      FrameIndex = MI.getOperand(1).getIndex();
      return MI.getOperand(0).getReg();
    }
    break;
  }

  return 0;
}

unsigned ARMBaseInstrInfo::isLoadFromStackSlotPostFE(const MachineInstr &MI,
                                                     int &FrameIndex) const {
  SmallVector<const MachineMemOperand *, 1> Accesses;
  if (MI.mayLoad() && hasLoadFromStackSlot(MI, Accesses)) {
    FrameIndex =
        cast<FixedStackPseudoSourceValue>(Accesses.front()->getPseudoValue())
            ->getFrameIndex();
    return true;
  }
  return false;
}

/// Expands MEMCPY to either LDMIA/STMIA or LDMIA_UPD/STMID_UPD
/// depending on whether the result is used.
void ARMBaseInstrInfo::expandMEMCPY(MachineBasicBlock::iterator MI) const {
  bool isThumb1 = Subtarget.isThumb1Only();
  bool isThumb2 = Subtarget.isThumb2();
  const ARMBaseInstrInfo *TII = Subtarget.getInstrInfo();

  DebugLoc dl = MI->getDebugLoc();
  MachineBasicBlock *BB = MI->getParent();

  MachineInstrBuilder LDM, STM;
  if (isThumb1 || !MI->getOperand(1).isDead()) {
    MachineOperand LDWb(MI->getOperand(1));
    LDM = BuildMI(*BB, MI, dl, TII->get(isThumb2 ? ARM::t2LDMIA_UPD
                                                 : isThumb1 ? ARM::tLDMIA_UPD
                                                            : ARM::LDMIA_UPD))
              .add(LDWb);
  } else {
    LDM = BuildMI(*BB, MI, dl, TII->get(isThumb2 ? ARM::t2LDMIA : ARM::LDMIA));
  }

  if (isThumb1 || !MI->getOperand(0).isDead()) {
    MachineOperand STWb(MI->getOperand(0));
    STM = BuildMI(*BB, MI, dl, TII->get(isThumb2 ? ARM::t2STMIA_UPD
                                                 : isThumb1 ? ARM::tSTMIA_UPD
                                                            : ARM::STMIA_UPD))
              .add(STWb);
  } else {
    STM = BuildMI(*BB, MI, dl, TII->get(isThumb2 ? ARM::t2STMIA : ARM::STMIA));
  }

  MachineOperand LDBase(MI->getOperand(3));
  LDM.add(LDBase).add(predOps(ARMCC::AL));

  MachineOperand STBase(MI->getOperand(2));
  STM.add(STBase).add(predOps(ARMCC::AL));

  // Sort the scratch registers into ascending order.
  const TargetRegisterInfo &TRI = getRegisterInfo();
  SmallVector<unsigned, 6> ScratchRegs;
  for(unsigned I = 5; I < MI->getNumOperands(); ++I)
    ScratchRegs.push_back(MI->getOperand(I).getReg());
  llvm::sort(ScratchRegs,
             [&TRI](const unsigned &Reg1, const unsigned &Reg2) -> bool {
               return TRI.getEncodingValue(Reg1) <
                      TRI.getEncodingValue(Reg2);
             });

  for (const auto &Reg : ScratchRegs) {
    LDM.addReg(Reg, RegState::Define);
    STM.addReg(Reg, RegState::Kill);
  }

  BB->erase(MI);
}

bool ARMBaseInstrInfo::expandPostRAPseudo(MachineInstr &MI) const {
  if (MI.getOpcode() == TargetOpcode::LOAD_STACK_GUARD) {
    assert(getSubtarget().getTargetTriple().isOSBinFormatMachO() &&
           "LOAD_STACK_GUARD currently supported only for MachO.");
    expandLoadStackGuard(MI);
    MI.getParent()->erase(MI);
    return true;
  }

  if (MI.getOpcode() == ARM::MEMCPY) {
    expandMEMCPY(MI);
    return true;
  }

  // This hook gets to expand COPY instructions before they become
  // copyPhysReg() calls.  Look for VMOVS instructions that can legally be
  // widened to VMOVD.  We prefer the VMOVD when possible because it may be
  // changed into a VORR that can go down the NEON pipeline.
  if (!MI.isCopy() || Subtarget.dontWidenVMOVS() || Subtarget.isFPOnlySP())
    return false;

  // Look for a copy between even S-registers.  That is where we keep floats
  // when using NEON v2f32 instructions for f32 arithmetic.
  unsigned DstRegS = MI.getOperand(0).getReg();
  unsigned SrcRegS = MI.getOperand(1).getReg();
  if (!ARM::SPRRegClass.contains(DstRegS, SrcRegS))
    return false;

  const TargetRegisterInfo *TRI = &getRegisterInfo();
  unsigned DstRegD = TRI->getMatchingSuperReg(DstRegS, ARM::ssub_0,
                                              &ARM::DPRRegClass);
  unsigned SrcRegD = TRI->getMatchingSuperReg(SrcRegS, ARM::ssub_0,
                                              &ARM::DPRRegClass);
  if (!DstRegD || !SrcRegD)
    return false;

  // We want to widen this into a DstRegD = VMOVD SrcRegD copy.  This is only
  // legal if the COPY already defines the full DstRegD, and it isn't a
  // sub-register insertion.
  if (!MI.definesRegister(DstRegD, TRI) || MI.readsRegister(DstRegD, TRI))
    return false;

  // A dead copy shouldn't show up here, but reject it just in case.
  if (MI.getOperand(0).isDead())
    return false;

  // All clear, widen the COPY.
  LLVM_DEBUG(dbgs() << "widening:    " << MI);
  MachineInstrBuilder MIB(*MI.getParent()->getParent(), MI);

  // Get rid of the old implicit-def of DstRegD.  Leave it if it defines a Q-reg
  // or some other super-register.
  int ImpDefIdx = MI.findRegisterDefOperandIdx(DstRegD);
  if (ImpDefIdx != -1)
    MI.RemoveOperand(ImpDefIdx);

  // Change the opcode and operands.
  MI.setDesc(get(ARM::VMOVD));
  MI.getOperand(0).setReg(DstRegD);
  MI.getOperand(1).setReg(SrcRegD);
  MIB.add(predOps(ARMCC::AL));

  // We are now reading SrcRegD instead of SrcRegS.  This may upset the
  // register scavenger and machine verifier, so we need to indicate that we
  // are reading an undefined value from SrcRegD, but a proper value from
  // SrcRegS.
  MI.getOperand(1).setIsUndef();
  MIB.addReg(SrcRegS, RegState::Implicit);

  // SrcRegD may actually contain an unrelated value in the ssub_1
  // sub-register.  Don't kill it.  Only kill the ssub_0 sub-register.
  if (MI.getOperand(1).isKill()) {
    MI.getOperand(1).setIsKill(false);
    MI.addRegisterKilled(SrcRegS, TRI, true);
  }

  LLVM_DEBUG(dbgs() << "replaced by: " << MI);
  return true;
}

/// Create a copy of a const pool value. Update CPI to the new index and return
/// the label UID.
static unsigned duplicateCPV(MachineFunction &MF, unsigned &CPI) {
  MachineConstantPool *MCP = MF.getConstantPool();
  ARMFunctionInfo *AFI = MF.getInfo<ARMFunctionInfo>();

  const MachineConstantPoolEntry &MCPE = MCP->getConstants()[CPI];
  assert(MCPE.isMachineConstantPoolEntry() &&
         "Expecting a machine constantpool entry!");
  ARMConstantPoolValue *ACPV =
    static_cast<ARMConstantPoolValue*>(MCPE.Val.MachineCPVal);

  unsigned PCLabelId = AFI->createPICLabelUId();
  ARMConstantPoolValue *NewCPV = nullptr;

  // FIXME: The below assumes PIC relocation model and that the function
  // is Thumb mode (t1 or t2). PCAdjustment would be 8 for ARM mode PIC, and
  // zero for non-PIC in ARM or Thumb. The callers are all of thumb LDR
  // instructions, so that's probably OK, but is PIC always correct when
  // we get here?
  if (ACPV->isGlobalValue())
    NewCPV = ARMConstantPoolConstant::Create(
        cast<ARMConstantPoolConstant>(ACPV)->getGV(), PCLabelId, ARMCP::CPValue,
        4, ACPV->getModifier(), ACPV->mustAddCurrentAddress());
  else if (ACPV->isExtSymbol())
    NewCPV = ARMConstantPoolSymbol::
      Create(MF.getFunction().getContext(),
             cast<ARMConstantPoolSymbol>(ACPV)->getSymbol(), PCLabelId, 4);
  else if (ACPV->isBlockAddress())
    NewCPV = ARMConstantPoolConstant::
      Create(cast<ARMConstantPoolConstant>(ACPV)->getBlockAddress(), PCLabelId,
             ARMCP::CPBlockAddress, 4);
  else if (ACPV->isLSDA())
    NewCPV = ARMConstantPoolConstant::Create(&MF.getFunction(), PCLabelId,
                                             ARMCP::CPLSDA, 4);
  else if (ACPV->isMachineBasicBlock())
    NewCPV = ARMConstantPoolMBB::
      Create(MF.getFunction().getContext(),
             cast<ARMConstantPoolMBB>(ACPV)->getMBB(), PCLabelId, 4);
  else
    llvm_unreachable("Unexpected ARM constantpool value type!!");
  CPI = MCP->getConstantPoolIndex(NewCPV, MCPE.getAlignment());
  return PCLabelId;
}

void ARMBaseInstrInfo::reMaterialize(MachineBasicBlock &MBB,
                                     MachineBasicBlock::iterator I,
                                     unsigned DestReg, unsigned SubIdx,
                                     const MachineInstr &Orig,
                                     const TargetRegisterInfo &TRI) const {
  unsigned Opcode = Orig.getOpcode();
  switch (Opcode) {
  default: {
    MachineInstr *MI = MBB.getParent()->CloneMachineInstr(&Orig);
    MI->substituteRegister(Orig.getOperand(0).getReg(), DestReg, SubIdx, TRI);
    MBB.insert(I, MI);
    break;
  }
  case ARM::tLDRpci_pic:
  case ARM::t2LDRpci_pic: {
    MachineFunction &MF = *MBB.getParent();
    unsigned CPI = Orig.getOperand(1).getIndex();
    unsigned PCLabelId = duplicateCPV(MF, CPI);
    BuildMI(MBB, I, Orig.getDebugLoc(), get(Opcode), DestReg)
        .addConstantPoolIndex(CPI)
        .addImm(PCLabelId)
        .cloneMemRefs(Orig);
    break;
  }
  }
}

MachineInstr &
ARMBaseInstrInfo::duplicate(MachineBasicBlock &MBB,
    MachineBasicBlock::iterator InsertBefore,
    const MachineInstr &Orig) const {
  MachineInstr &Cloned = TargetInstrInfo::duplicate(MBB, InsertBefore, Orig);
  MachineBasicBlock::instr_iterator I = Cloned.getIterator();
  for (;;) {
    switch (I->getOpcode()) {
    case ARM::tLDRpci_pic:
    case ARM::t2LDRpci_pic: {
      MachineFunction &MF = *MBB.getParent();
      unsigned CPI = I->getOperand(1).getIndex();
      unsigned PCLabelId = duplicateCPV(MF, CPI);
      I->getOperand(1).setIndex(CPI);
      I->getOperand(2).setImm(PCLabelId);
      break;
    }
    }
    if (!I->isBundledWithSucc())
      break;
    ++I;
  }
  return Cloned;
}

bool ARMBaseInstrInfo::produceSameValue(const MachineInstr &MI0,
                                        const MachineInstr &MI1,
                                        const MachineRegisterInfo *MRI) const {
  unsigned Opcode = MI0.getOpcode();
  if (Opcode == ARM::t2LDRpci ||
      Opcode == ARM::t2LDRpci_pic ||
      Opcode == ARM::tLDRpci ||
      Opcode == ARM::tLDRpci_pic ||
      Opcode == ARM::LDRLIT_ga_pcrel ||
      Opcode == ARM::LDRLIT_ga_pcrel_ldr ||
      Opcode == ARM::tLDRLIT_ga_pcrel ||
      Opcode == ARM::MOV_ga_pcrel ||
      Opcode == ARM::MOV_ga_pcrel_ldr ||
      Opcode == ARM::t2MOV_ga_pcrel) {
    if (MI1.getOpcode() != Opcode)
      return false;
    if (MI0.getNumOperands() != MI1.getNumOperands())
      return false;

    const MachineOperand &MO0 = MI0.getOperand(1);
    const MachineOperand &MO1 = MI1.getOperand(1);
    if (MO0.getOffset() != MO1.getOffset())
      return false;

    if (Opcode == ARM::LDRLIT_ga_pcrel ||
        Opcode == ARM::LDRLIT_ga_pcrel_ldr ||
        Opcode == ARM::tLDRLIT_ga_pcrel ||
        Opcode == ARM::MOV_ga_pcrel ||
        Opcode == ARM::MOV_ga_pcrel_ldr ||
        Opcode == ARM::t2MOV_ga_pcrel)
      // Ignore the PC labels.
      return MO0.getGlobal() == MO1.getGlobal();

    const MachineFunction *MF = MI0.getParent()->getParent();
    const MachineConstantPool *MCP = MF->getConstantPool();
    int CPI0 = MO0.getIndex();
    int CPI1 = MO1.getIndex();
    const MachineConstantPoolEntry &MCPE0 = MCP->getConstants()[CPI0];
    const MachineConstantPoolEntry &MCPE1 = MCP->getConstants()[CPI1];
    bool isARMCP0 = MCPE0.isMachineConstantPoolEntry();
    bool isARMCP1 = MCPE1.isMachineConstantPoolEntry();
    if (isARMCP0 && isARMCP1) {
      ARMConstantPoolValue *ACPV0 =
        static_cast<ARMConstantPoolValue*>(MCPE0.Val.MachineCPVal);
      ARMConstantPoolValue *ACPV1 =
        static_cast<ARMConstantPoolValue*>(MCPE1.Val.MachineCPVal);
      return ACPV0->hasSameValue(ACPV1);
    } else if (!isARMCP0 && !isARMCP1) {
      return MCPE0.Val.ConstVal == MCPE1.Val.ConstVal;
    }
    return false;
  } else if (Opcode == ARM::PICLDR) {
    if (MI1.getOpcode() != Opcode)
      return false;
    if (MI0.getNumOperands() != MI1.getNumOperands())
      return false;

    unsigned Addr0 = MI0.getOperand(1).getReg();
    unsigned Addr1 = MI1.getOperand(1).getReg();
    if (Addr0 != Addr1) {
      if (!MRI ||
          !TargetRegisterInfo::isVirtualRegister(Addr0) ||
          !TargetRegisterInfo::isVirtualRegister(Addr1))
        return false;

      // This assumes SSA form.
      MachineInstr *Def0 = MRI->getVRegDef(Addr0);
      MachineInstr *Def1 = MRI->getVRegDef(Addr1);
      // Check if the loaded value, e.g. a constantpool of a global address, are
      // the same.
      if (!produceSameValue(*Def0, *Def1, MRI))
        return false;
    }

    for (unsigned i = 3, e = MI0.getNumOperands(); i != e; ++i) {
      // %12 = PICLDR %11, 0, 14, %noreg
      const MachineOperand &MO0 = MI0.getOperand(i);
      const MachineOperand &MO1 = MI1.getOperand(i);
      if (!MO0.isIdenticalTo(MO1))
        return false;
    }
    return true;
  }

  return MI0.isIdenticalTo(MI1, MachineInstr::IgnoreVRegDefs);
}

/// areLoadsFromSameBasePtr - This is used by the pre-regalloc scheduler to
/// determine if two loads are loading from the same base address. It should
/// only return true if the base pointers are the same and the only differences
/// between the two addresses is the offset. It also returns the offsets by
/// reference.
///
/// FIXME: remove this in favor of the MachineInstr interface once pre-RA-sched
/// is permanently disabled.
bool ARMBaseInstrInfo::areLoadsFromSameBasePtr(SDNode *Load1, SDNode *Load2,
                                               int64_t &Offset1,
                                               int64_t &Offset2) const {
  // Don't worry about Thumb: just ARM and Thumb2.
  if (Subtarget.isThumb1Only()) return false;

  if (!Load1->isMachineOpcode() || !Load2->isMachineOpcode())
    return false;

  switch (Load1->getMachineOpcode()) {
  default:
    return false;
  case ARM::LDRi12:
  case ARM::LDRBi12:
  case ARM::LDRD:
  case ARM::LDRH:
  case ARM::LDRSB:
  case ARM::LDRSH:
  case ARM::VLDRD:
  case ARM::VLDRS:
  case ARM::t2LDRi8:
  case ARM::t2LDRBi8:
  case ARM::t2LDRDi8:
  case ARM::t2LDRSHi8:
  case ARM::t2LDRi12:
  case ARM::t2LDRBi12:
  case ARM::t2LDRSHi12:
    break;
  }

  switch (Load2->getMachineOpcode()) {
  default:
    return false;
  case ARM::LDRi12:
  case ARM::LDRBi12:
  case ARM::LDRD:
  case ARM::LDRH:
  case ARM::LDRSB:
  case ARM::LDRSH:
  case ARM::VLDRD:
  case ARM::VLDRS:
  case ARM::t2LDRi8:
  case ARM::t2LDRBi8:
  case ARM::t2LDRSHi8:
  case ARM::t2LDRi12:
  case ARM::t2LDRBi12:
  case ARM::t2LDRSHi12:
    break;
  }

  // Check if base addresses and chain operands match.
  if (Load1->getOperand(0) != Load2->getOperand(0) ||
      Load1->getOperand(4) != Load2->getOperand(4))
    return false;

  // Index should be Reg0.
  if (Load1->getOperand(3) != Load2->getOperand(3))
    return false;

  // Determine the offsets.
  if (isa<ConstantSDNode>(Load1->getOperand(1)) &&
      isa<ConstantSDNode>(Load2->getOperand(1))) {
    Offset1 = cast<ConstantSDNode>(Load1->getOperand(1))->getSExtValue();
    Offset2 = cast<ConstantSDNode>(Load2->getOperand(1))->getSExtValue();
    return true;
  }

  return false;
}

/// shouldScheduleLoadsNear - This is a used by the pre-regalloc scheduler to
/// determine (in conjunction with areLoadsFromSameBasePtr) if two loads should
/// be scheduled togther. On some targets if two loads are loading from
/// addresses in the same cache line, it's better if they are scheduled
/// together. This function takes two integers that represent the load offsets
/// from the common base address. It returns true if it decides it's desirable
/// to schedule the two loads together. "NumLoads" is the number of loads that
/// have already been scheduled after Load1.
///
/// FIXME: remove this in favor of the MachineInstr interface once pre-RA-sched
/// is permanently disabled.
bool ARMBaseInstrInfo::shouldScheduleLoadsNear(SDNode *Load1, SDNode *Load2,
                                               int64_t Offset1, int64_t Offset2,
                                               unsigned NumLoads) const {
  // Don't worry about Thumb: just ARM and Thumb2.
  if (Subtarget.isThumb1Only()) return false;

  assert(Offset2 > Offset1);

  if ((Offset2 - Offset1) / 8 > 64)
    return false;

  // Check if the machine opcodes are different. If they are different
  // then we consider them to not be of the same base address,
  // EXCEPT in the case of Thumb2 byte loads where one is LDRBi8 and the other LDRBi12.
  // In this case, they are considered to be the same because they are different
  // encoding forms of the same basic instruction.
  if ((Load1->getMachineOpcode() != Load2->getMachineOpcode()) &&
      !((Load1->getMachineOpcode() == ARM::t2LDRBi8 &&
         Load2->getMachineOpcode() == ARM::t2LDRBi12) ||
        (Load1->getMachineOpcode() == ARM::t2LDRBi12 &&
         Load2->getMachineOpcode() == ARM::t2LDRBi8)))
    return false;  // FIXME: overly conservative?

  // Four loads in a row should be sufficient.
  if (NumLoads >= 3)
    return false;

  return true;
}

bool ARMBaseInstrInfo::isSchedulingBoundary(const MachineInstr &MI,
                                            const MachineBasicBlock *MBB,
                                            const MachineFunction &MF) const {
  // Debug info is never a scheduling boundary. It's necessary to be explicit
  // due to the special treatment of IT instructions below, otherwise a
  // dbg_value followed by an IT will result in the IT instruction being
  // considered a scheduling hazard, which is wrong. It should be the actual
  // instruction preceding the dbg_value instruction(s), just like it is
  // when debug info is not present.
  if (MI.isDebugInstr())
    return false;

  // Terminators and labels can't be scheduled around.
  if (MI.isTerminator() || MI.isPosition())
    return true;

  // Treat the start of the IT block as a scheduling boundary, but schedule
  // t2IT along with all instructions following it.
  // FIXME: This is a big hammer. But the alternative is to add all potential
  // true and anti dependencies to IT block instructions as implicit operands
  // to the t2IT instruction. The added compile time and complexity does not
  // seem worth it.
  MachineBasicBlock::const_iterator I = MI;
  // Make sure to skip any debug instructions
  while (++I != MBB->end() && I->isDebugInstr())
    ;
  if (I != MBB->end() && I->getOpcode() == ARM::t2IT)
    return true;

  // Don't attempt to schedule around any instruction that defines
  // a stack-oriented pointer, as it's unlikely to be profitable. This
  // saves compile time, because it doesn't require every single
  // stack slot reference to depend on the instruction that does the
  // modification.
  // Calls don't actually change the stack pointer, even if they have imp-defs.
  // No ARM calling conventions change the stack pointer. (X86 calling
  // conventions sometimes do).
  if (!MI.isCall() && MI.definesRegister(ARM::SP))
    return true;

  return false;
}

bool ARMBaseInstrInfo::
isProfitableToIfCvt(MachineBasicBlock &MBB,
                    unsigned NumCycles, unsigned ExtraPredCycles,
                    BranchProbability Probability) const {
  if (!NumCycles)
    return false;

  // If we are optimizing for size, see if the branch in the predecessor can be
  // lowered to cbn?z by the constant island lowering pass, and return false if
  // so. This results in a shorter instruction sequence.
  if (MBB.getParent()->getFunction().optForSize()) {
    MachineBasicBlock *Pred = *MBB.pred_begin();
    if (!Pred->empty()) {
      MachineInstr *LastMI = &*Pred->rbegin();
      if (LastMI->getOpcode() == ARM::t2Bcc) {
        MachineBasicBlock::iterator CmpMI = LastMI;
        if (CmpMI != Pred->begin()) {
          --CmpMI;
          if (CmpMI->getOpcode() == ARM::tCMPi8 ||
              CmpMI->getOpcode() == ARM::t2CMPri) {
            unsigned Reg = CmpMI->getOperand(0).getReg();
            unsigned PredReg = 0;
            ARMCC::CondCodes P = getInstrPredicate(*CmpMI, PredReg);
            if (P == ARMCC::AL && CmpMI->getOperand(1).getImm() == 0 &&
                isARMLowRegister(Reg))
              return false;
          }
        }
      }
    }
  }
  return isProfitableToIfCvt(MBB, NumCycles, ExtraPredCycles,
                             MBB, 0, 0, Probability);
}

bool ARMBaseInstrInfo::
isProfitableToIfCvt(MachineBasicBlock &TBB,
                    unsigned TCycles, unsigned TExtra,
                    MachineBasicBlock &FBB,
                    unsigned FCycles, unsigned FExtra,
                    BranchProbability Probability) const {
  if (!TCycles)
    return false;

  // Attempt to estimate the relative costs of predication versus branching.
  // Here we scale up each component of UnpredCost to avoid precision issue when
  // scaling TCycles/FCycles by Probability.
  const unsigned ScalingUpFactor = 1024;

  unsigned PredCost = (TCycles + FCycles + TExtra + FExtra) * ScalingUpFactor;
  unsigned UnpredCost;
  if (!Subtarget.hasBranchPredictor()) {
    // When we don't have a branch predictor it's always cheaper to not take a
    // branch than take it, so we have to take that into account.
    unsigned NotTakenBranchCost = 1;
    unsigned TakenBranchCost = Subtarget.getMispredictionPenalty();
    unsigned TUnpredCycles, FUnpredCycles;
    if (!FCycles) {
      // Triangle: TBB is the fallthrough
      TUnpredCycles = TCycles + NotTakenBranchCost;
      FUnpredCycles = TakenBranchCost;
    } else {
      // Diamond: TBB is the block that is branched to, FBB is the fallthrough
      TUnpredCycles = TCycles + TakenBranchCost;
      FUnpredCycles = FCycles + NotTakenBranchCost;
      // The branch at the end of FBB will disappear when it's predicated, so
      // discount it from PredCost.
      PredCost -= 1 * ScalingUpFactor;
    }
    // The total cost is the cost of each path scaled by their probabilites
    unsigned TUnpredCost = Probability.scale(TUnpredCycles * ScalingUpFactor);
    unsigned FUnpredCost = Probability.getCompl().scale(FUnpredCycles * ScalingUpFactor);
    UnpredCost = TUnpredCost + FUnpredCost;
    // When predicating assume that the first IT can be folded away but later
    // ones cost one cycle each
    if (Subtarget.isThumb2() && TCycles + FCycles > 4) {
      PredCost += ((TCycles + FCycles - 4) / 4) * ScalingUpFactor;
    }
  } else {
    unsigned TUnpredCost = Probability.scale(TCycles * ScalingUpFactor);
    unsigned FUnpredCost =
      Probability.getCompl().scale(FCycles * ScalingUpFactor);
    UnpredCost = TUnpredCost + FUnpredCost;
    UnpredCost += 1 * ScalingUpFactor; // The branch itself
    UnpredCost += Subtarget.getMispredictionPenalty() * ScalingUpFactor / 10;
  }

  return PredCost <= UnpredCost;
}

bool
ARMBaseInstrInfo::isProfitableToUnpredicate(MachineBasicBlock &TMBB,
                                            MachineBasicBlock &FMBB) const {
  // Reduce false anti-dependencies to let the target's out-of-order execution
  // engine do its thing.
  return Subtarget.isProfitableToUnpredicate();
}

/// getInstrPredicate - If instruction is predicated, returns its predicate
/// condition, otherwise returns AL. It also returns the condition code
/// register by reference.
ARMCC::CondCodes llvm::getInstrPredicate(const MachineInstr &MI,
                                         unsigned &PredReg) {
  int PIdx = MI.findFirstPredOperandIdx();
  if (PIdx == -1) {
    PredReg = 0;
    return ARMCC::AL;
  }

  PredReg = MI.getOperand(PIdx+1).getReg();
  return (ARMCC::CondCodes)MI.getOperand(PIdx).getImm();
}

unsigned llvm::getMatchingCondBranchOpcode(unsigned Opc) {
  if (Opc == ARM::B)
    return ARM::Bcc;
  if (Opc == ARM::tB)
    return ARM::tBcc;
  if (Opc == ARM::t2B)
    return ARM::t2Bcc;

  llvm_unreachable("Unknown unconditional branch opcode!");
}

MachineInstr *ARMBaseInstrInfo::commuteInstructionImpl(MachineInstr &MI,
                                                       bool NewMI,
                                                       unsigned OpIdx1,
                                                       unsigned OpIdx2) const {
  switch (MI.getOpcode()) {
  case ARM::MOVCCr:
  case ARM::t2MOVCCr: {
    // MOVCC can be commuted by inverting the condition.
    unsigned PredReg = 0;
    ARMCC::CondCodes CC = getInstrPredicate(MI, PredReg);
    // MOVCC AL can't be inverted. Shouldn't happen.
    if (CC == ARMCC::AL || PredReg != ARM::CPSR)
      return nullptr;
    MachineInstr *CommutedMI =
        TargetInstrInfo::commuteInstructionImpl(MI, NewMI, OpIdx1, OpIdx2);
    if (!CommutedMI)
      return nullptr;
    // After swapping the MOVCC operands, also invert the condition.
    CommutedMI->getOperand(CommutedMI->findFirstPredOperandIdx())
        .setImm(ARMCC::getOppositeCondition(CC));
    return CommutedMI;
  }
  }
  return TargetInstrInfo::commuteInstructionImpl(MI, NewMI, OpIdx1, OpIdx2);
}

/// Identify instructions that can be folded into a MOVCC instruction, and
/// return the defining instruction.
static MachineInstr *canFoldIntoMOVCC(unsigned Reg,
                                      const MachineRegisterInfo &MRI,
                                      const TargetInstrInfo *TII) {
  if (!TargetRegisterInfo::isVirtualRegister(Reg))
    return nullptr;
  if (!MRI.hasOneNonDBGUse(Reg))
    return nullptr;
  MachineInstr *MI = MRI.getVRegDef(Reg);
  if (!MI)
    return nullptr;
  // MI is folded into the MOVCC by predicating it.
  if (!MI->isPredicable())
    return nullptr;
  // Check if MI has any non-dead defs or physreg uses. This also detects
  // predicated instructions which will be reading CPSR.
  for (unsigned i = 1, e = MI->getNumOperands(); i != e; ++i) {
    const MachineOperand &MO = MI->getOperand(i);
    // Reject frame index operands, PEI can't handle the predicated pseudos.
    if (MO.isFI() || MO.isCPI() || MO.isJTI())
      return nullptr;
    if (!MO.isReg())
      continue;
    // MI can't have any tied operands, that would conflict with predication.
    if (MO.isTied())
      return nullptr;
    if (TargetRegisterInfo::isPhysicalRegister(MO.getReg()))
      return nullptr;
    if (MO.isDef() && !MO.isDead())
      return nullptr;
  }
  bool DontMoveAcrossStores = true;
  if (!MI->isSafeToMove(/* AliasAnalysis = */ nullptr, DontMoveAcrossStores))
    return nullptr;
  return MI;
}

bool ARMBaseInstrInfo::analyzeSelect(const MachineInstr &MI,
                                     SmallVectorImpl<MachineOperand> &Cond,
                                     unsigned &TrueOp, unsigned &FalseOp,
                                     bool &Optimizable) const {
  assert((MI.getOpcode() == ARM::MOVCCr || MI.getOpcode() == ARM::t2MOVCCr) &&
         "Unknown select instruction");
  // MOVCC operands:
  // 0: Def.
  // 1: True use.
  // 2: False use.
  // 3: Condition code.
  // 4: CPSR use.
  TrueOp = 1;
  FalseOp = 2;
  Cond.push_back(MI.getOperand(3));
  Cond.push_back(MI.getOperand(4));
  // We can always fold a def.
  Optimizable = true;
  return false;
}

MachineInstr *
ARMBaseInstrInfo::optimizeSelect(MachineInstr &MI,
                                 SmallPtrSetImpl<MachineInstr *> &SeenMIs,
                                 bool PreferFalse) const {
  assert((MI.getOpcode() == ARM::MOVCCr || MI.getOpcode() == ARM::t2MOVCCr) &&
         "Unknown select instruction");
  MachineRegisterInfo &MRI = MI.getParent()->getParent()->getRegInfo();
  MachineInstr *DefMI = canFoldIntoMOVCC(MI.getOperand(2).getReg(), MRI, this);
  bool Invert = !DefMI;
  if (!DefMI)
    DefMI = canFoldIntoMOVCC(MI.getOperand(1).getReg(), MRI, this);
  if (!DefMI)
    return nullptr;

  // Find new register class to use.
  MachineOperand FalseReg = MI.getOperand(Invert ? 2 : 1);
  unsigned DestReg = MI.getOperand(0).getReg();
  const TargetRegisterClass *PreviousClass = MRI.getRegClass(FalseReg.getReg());
  if (!MRI.constrainRegClass(DestReg, PreviousClass))
    return nullptr;

  // Create a new predicated version of DefMI.
  // Rfalse is the first use.
  MachineInstrBuilder NewMI =
      BuildMI(*MI.getParent(), MI, MI.getDebugLoc(), DefMI->getDesc(), DestReg);

  // Copy all the DefMI operands, excluding its (null) predicate.
  const MCInstrDesc &DefDesc = DefMI->getDesc();
  for (unsigned i = 1, e = DefDesc.getNumOperands();
       i != e && !DefDesc.OpInfo[i].isPredicate(); ++i)
    NewMI.add(DefMI->getOperand(i));

  unsigned CondCode = MI.getOperand(3).getImm();
  if (Invert)
    NewMI.addImm(ARMCC::getOppositeCondition(ARMCC::CondCodes(CondCode)));
  else
    NewMI.addImm(CondCode);
  NewMI.add(MI.getOperand(4));

  // DefMI is not the -S version that sets CPSR, so add an optional %noreg.
  if (NewMI->hasOptionalDef())
    NewMI.add(condCodeOp());

  // The output register value when the predicate is false is an implicit
  // register operand tied to the first def.
  // The tie makes the register allocator ensure the FalseReg is allocated the
  // same register as operand 0.
  FalseReg.setImplicit();
  NewMI.add(FalseReg);
  NewMI->tieOperands(0, NewMI->getNumOperands() - 1);

  // Update SeenMIs set: register newly created MI and erase removed DefMI.
  SeenMIs.insert(NewMI);
  SeenMIs.erase(DefMI);

  // If MI is inside a loop, and DefMI is outside the loop, then kill flags on
  // DefMI would be invalid when tranferred inside the loop.  Checking for a
  // loop is expensive, but at least remove kill flags if they are in different
  // BBs.
  if (DefMI->getParent() != MI.getParent())
    NewMI->clearKillInfo();

  // The caller will erase MI, but not DefMI.
  DefMI->eraseFromParent();
  return NewMI;
}

/// Map pseudo instructions that imply an 'S' bit onto real opcodes. Whether the
/// instruction is encoded with an 'S' bit is determined by the optional CPSR
/// def operand.
///
/// This will go away once we can teach tblgen how to set the optional CPSR def
/// operand itself.
struct AddSubFlagsOpcodePair {
  uint16_t PseudoOpc;
  uint16_t MachineOpc;
};

static const AddSubFlagsOpcodePair AddSubFlagsOpcodeMap[] = {
  {ARM::ADDSri, ARM::ADDri},
  {ARM::ADDSrr, ARM::ADDrr},
  {ARM::ADDSrsi, ARM::ADDrsi},
  {ARM::ADDSrsr, ARM::ADDrsr},

  {ARM::SUBSri, ARM::SUBri},
  {ARM::SUBSrr, ARM::SUBrr},
  {ARM::SUBSrsi, ARM::SUBrsi},
  {ARM::SUBSrsr, ARM::SUBrsr},

  {ARM::RSBSri, ARM::RSBri},
  {ARM::RSBSrsi, ARM::RSBrsi},
  {ARM::RSBSrsr, ARM::RSBrsr},

  {ARM::tADDSi3, ARM::tADDi3},
  {ARM::tADDSi8, ARM::tADDi8},
  {ARM::tADDSrr, ARM::tADDrr},
  {ARM::tADCS, ARM::tADC},

  {ARM::tSUBSi3, ARM::tSUBi3},
  {ARM::tSUBSi8, ARM::tSUBi8},
  {ARM::tSUBSrr, ARM::tSUBrr},
  {ARM::tSBCS, ARM::tSBC},
  {ARM::tRSBS, ARM::tRSB},

  {ARM::t2ADDSri, ARM::t2ADDri},
  {ARM::t2ADDSrr, ARM::t2ADDrr},
  {ARM::t2ADDSrs, ARM::t2ADDrs},

  {ARM::t2SUBSri, ARM::t2SUBri},
  {ARM::t2SUBSrr, ARM::t2SUBrr},
  {ARM::t2SUBSrs, ARM::t2SUBrs},

  {ARM::t2RSBSri, ARM::t2RSBri},
  {ARM::t2RSBSrs, ARM::t2RSBrs},
};

unsigned llvm::convertAddSubFlagsOpcode(unsigned OldOpc) {
  for (unsigned i = 0, e = array_lengthof(AddSubFlagsOpcodeMap); i != e; ++i)
    if (OldOpc == AddSubFlagsOpcodeMap[i].PseudoOpc)
      return AddSubFlagsOpcodeMap[i].MachineOpc;
  return 0;
}

void llvm::emitARMRegPlusImmediate(MachineBasicBlock &MBB,
                                   MachineBasicBlock::iterator &MBBI,
                                   const DebugLoc &dl, unsigned DestReg,
                                   unsigned BaseReg, int NumBytes,
                                   ARMCC::CondCodes Pred, unsigned PredReg,
                                   const ARMBaseInstrInfo &TII,
                                   unsigned MIFlags) {
  if (NumBytes == 0 && DestReg != BaseReg) {
    BuildMI(MBB, MBBI, dl, TII.get(ARM::MOVr), DestReg)
        .addReg(BaseReg, RegState::Kill)
        .add(predOps(Pred, PredReg))
        .add(condCodeOp())
        .setMIFlags(MIFlags);
    return;
  }

  bool isSub = NumBytes < 0;
  if (isSub) NumBytes = -NumBytes;

  while (NumBytes) {
    unsigned RotAmt = ARM_AM::getSOImmValRotate(NumBytes);
    unsigned ThisVal = NumBytes & ARM_AM::rotr32(0xFF, RotAmt);
    assert(ThisVal && "Didn't extract field correctly");

    // We will handle these bits from offset, clear them.
    NumBytes &= ~ThisVal;

    assert(ARM_AM::getSOImmVal(ThisVal) != -1 && "Bit extraction didn't work?");

    // Build the new ADD / SUB.
    unsigned Opc = isSub ? ARM::SUBri : ARM::ADDri;
    BuildMI(MBB, MBBI, dl, TII.get(Opc), DestReg)
        .addReg(BaseReg, RegState::Kill)
        .addImm(ThisVal)
        .add(predOps(Pred, PredReg))
        .add(condCodeOp())
        .setMIFlags(MIFlags);
    BaseReg = DestReg;
  }
}

bool llvm::tryFoldSPUpdateIntoPushPop(const ARMSubtarget &Subtarget,
                                      MachineFunction &MF, MachineInstr *MI,
                                      unsigned NumBytes) {
  // This optimisation potentially adds lots of load and store
  // micro-operations, it's only really a great benefit to code-size.
  if (!MF.getFunction().optForMinSize())
    return false;

  // If only one register is pushed/popped, LLVM can use an LDR/STR
  // instead. We can't modify those so make sure we're dealing with an
  // instruction we understand.
  bool IsPop = isPopOpcode(MI->getOpcode());
  bool IsPush = isPushOpcode(MI->getOpcode());
  if (!IsPush && !IsPop)
    return false;

  bool IsVFPPushPop = MI->getOpcode() == ARM::VSTMDDB_UPD ||
                      MI->getOpcode() == ARM::VLDMDIA_UPD;
  bool IsT1PushPop = MI->getOpcode() == ARM::tPUSH ||
                     MI->getOpcode() == ARM::tPOP ||
                     MI->getOpcode() == ARM::tPOP_RET;

  assert((IsT1PushPop || (MI->getOperand(0).getReg() == ARM::SP &&
                          MI->getOperand(1).getReg() == ARM::SP)) &&
         "trying to fold sp update into non-sp-updating push/pop");

  // The VFP push & pop act on D-registers, so we can only fold an adjustment
  // by a multiple of 8 bytes in correctly. Similarly rN is 4-bytes. Don't try
  // if this is violated.
  if (NumBytes % (IsVFPPushPop ? 8 : 4) != 0)
    return false;

  // ARM and Thumb2 push/pop insts have explicit "sp, sp" operands (+
  // pred) so the list starts at 4. Thumb1 starts after the predicate.
  int RegListIdx = IsT1PushPop ? 2 : 4;

  // Calculate the space we'll need in terms of registers.
  unsigned RegsNeeded;
  const TargetRegisterClass *RegClass;
  if (IsVFPPushPop) {
    RegsNeeded = NumBytes / 8;
    RegClass = &ARM::DPRRegClass;
  } else {
    RegsNeeded = NumBytes / 4;
    RegClass = &ARM::GPRRegClass;
  }

  // We're going to have to strip all list operands off before
  // re-adding them since the order matters, so save the existing ones
  // for later.
  SmallVector<MachineOperand, 4> RegList;

  // We're also going to need the first register transferred by this
  // instruction, which won't necessarily be the first register in the list.
  unsigned FirstRegEnc = -1;

  const TargetRegisterInfo *TRI = MF.getRegInfo().getTargetRegisterInfo();
  for (int i = MI->getNumOperands() - 1; i >= RegListIdx; --i) {
    MachineOperand &MO = MI->getOperand(i);
    RegList.push_back(MO);

    if (MO.isReg() && TRI->getEncodingValue(MO.getReg()) < FirstRegEnc)
      FirstRegEnc = TRI->getEncodingValue(MO.getReg());
  }

  const MCPhysReg *CSRegs = TRI->getCalleeSavedRegs(&MF);

  // Now try to find enough space in the reglist to allocate NumBytes.
  for (int CurRegEnc = FirstRegEnc - 1; CurRegEnc >= 0 && RegsNeeded;
       --CurRegEnc) {
    unsigned CurReg = RegClass->getRegister(CurRegEnc);
    if (!IsPop) {
      // Pushing any register is completely harmless, mark the register involved
      // as undef since we don't care about its value and must not restore it
      // during stack unwinding.
      RegList.push_back(MachineOperand::CreateReg(CurReg, false, false,
                                                  false, false, true));
      --RegsNeeded;
      continue;
    }

    // However, we can only pop an extra register if it's not live. For
    // registers live within the function we might clobber a return value
    // register; the other way a register can be live here is if it's
    // callee-saved.
    if (isCalleeSavedRegister(CurReg, CSRegs) ||
        MI->getParent()->computeRegisterLiveness(TRI, CurReg, MI) !=
        MachineBasicBlock::LQR_Dead) {
      // VFP pops don't allow holes in the register list, so any skip is fatal
      // for our transformation. GPR pops do, so we should just keep looking.
      if (IsVFPPushPop)
        return false;
      else
        continue;
    }

    // Mark the unimportant registers as <def,dead> in the POP.
    RegList.push_back(MachineOperand::CreateReg(CurReg, true, false, false,
                                                true));
    --RegsNeeded;
  }

  if (RegsNeeded > 0)
    return false;

  // Finally we know we can profitably perform the optimisation so go
  // ahead: strip all existing registers off and add them back again
  // in the right order.
  for (int i = MI->getNumOperands() - 1; i >= RegListIdx; --i)
    MI->RemoveOperand(i);

  // Add the complete list back in.
  MachineInstrBuilder MIB(MF, &*MI);
  for (int i = RegList.size() - 1; i >= 0; --i)
    MIB.add(RegList[i]);

  return true;
}

bool llvm::rewriteARMFrameIndex(MachineInstr &MI, unsigned FrameRegIdx,
                                unsigned FrameReg, int &Offset,
                                const ARMBaseInstrInfo &TII) {
  unsigned Opcode = MI.getOpcode();
  const MCInstrDesc &Desc = MI.getDesc();
  unsigned AddrMode = (Desc.TSFlags & ARMII::AddrModeMask);
  bool isSub = false;

  // Memory operands in inline assembly always use AddrMode2.
  if (Opcode == ARM::INLINEASM)
    AddrMode = ARMII::AddrMode2;

  if (Opcode == ARM::ADDri) {
    Offset += MI.getOperand(FrameRegIdx+1).getImm();
    if (Offset == 0) {
      // Turn it into a move.
      MI.setDesc(TII.get(ARM::MOVr));
      MI.getOperand(FrameRegIdx).ChangeToRegister(FrameReg, false);
      MI.RemoveOperand(FrameRegIdx+1);
      Offset = 0;
      return true;
    } else if (Offset < 0) {
      Offset = -Offset;
      isSub = true;
      MI.setDesc(TII.get(ARM::SUBri));
    }

    // Common case: small offset, fits into instruction.
    if (ARM_AM::getSOImmVal(Offset) != -1) {
      // Replace the FrameIndex with sp / fp
      MI.getOperand(FrameRegIdx).ChangeToRegister(FrameReg, false);
      MI.getOperand(FrameRegIdx+1).ChangeToImmediate(Offset);
      Offset = 0;
      return true;
    }

    // Otherwise, pull as much of the immedidate into this ADDri/SUBri
    // as possible.
    unsigned RotAmt = ARM_AM::getSOImmValRotate(Offset);
    unsigned ThisImmVal = Offset & ARM_AM::rotr32(0xFF, RotAmt);

    // We will handle these bits from offset, clear them.
    Offset &= ~ThisImmVal;

    // Get the properly encoded SOImmVal field.
    assert(ARM_AM::getSOImmVal(ThisImmVal) != -1 &&
           "Bit extraction didn't work?");
    MI.getOperand(FrameRegIdx+1).ChangeToImmediate(ThisImmVal);
 } else {
    unsigned ImmIdx = 0;
    int InstrOffs = 0;
    unsigned NumBits = 0;
    unsigned Scale = 1;
    switch (AddrMode) {
    case ARMII::AddrMode_i12:
      ImmIdx = FrameRegIdx + 1;
      InstrOffs = MI.getOperand(ImmIdx).getImm();
      NumBits = 12;
      break;
    case ARMII::AddrMode2:
      ImmIdx = FrameRegIdx+2;
      InstrOffs = ARM_AM::getAM2Offset(MI.getOperand(ImmIdx).getImm());
      if (ARM_AM::getAM2Op(MI.getOperand(ImmIdx).getImm()) == ARM_AM::sub)
        InstrOffs *= -1;
      NumBits = 12;
      break;
    case ARMII::AddrMode3:
      ImmIdx = FrameRegIdx+2;
      InstrOffs = ARM_AM::getAM3Offset(MI.getOperand(ImmIdx).getImm());
      if (ARM_AM::getAM3Op(MI.getOperand(ImmIdx).getImm()) == ARM_AM::sub)
        InstrOffs *= -1;
      NumBits = 8;
      break;
    case ARMII::AddrMode4:
    case ARMII::AddrMode6:
      // Can't fold any offset even if it's zero.
      return false;
    case ARMII::AddrMode5:
      ImmIdx = FrameRegIdx+1;
      InstrOffs = ARM_AM::getAM5Offset(MI.getOperand(ImmIdx).getImm());
      if (ARM_AM::getAM5Op(MI.getOperand(ImmIdx).getImm()) == ARM_AM::sub)
        InstrOffs *= -1;
      NumBits = 8;
      Scale = 4;
      break;
    case ARMII::AddrMode5FP16:
      ImmIdx = FrameRegIdx+1;
      InstrOffs = ARM_AM::getAM5Offset(MI.getOperand(ImmIdx).getImm());
      if (ARM_AM::getAM5Op(MI.getOperand(ImmIdx).getImm()) == ARM_AM::sub)
        InstrOffs *= -1;
      NumBits = 8;
      Scale = 2;
      break;
    default:
      llvm_unreachable("Unsupported addressing mode!");
    }

    Offset += InstrOffs * Scale;
    assert((Offset & (Scale-1)) == 0 && "Can't encode this offset!");
    if (Offset < 0) {
      Offset = -Offset;
      isSub = true;
    }

    // Attempt to fold address comp. if opcode has offset bits
    if (NumBits > 0) {
      // Common case: small offset, fits into instruction.
      MachineOperand &ImmOp = MI.getOperand(ImmIdx);
      int ImmedOffset = Offset / Scale;
      unsigned Mask = (1 << NumBits) - 1;
      if ((unsigned)Offset <= Mask * Scale) {
        // Replace the FrameIndex with sp
        MI.getOperand(FrameRegIdx).ChangeToRegister(FrameReg, false);
        // FIXME: When addrmode2 goes away, this will simplify (like the
        // T2 version), as the LDR.i12 versions don't need the encoding
        // tricks for the offset value.
        if (isSub) {
          if (AddrMode == ARMII::AddrMode_i12)
            ImmedOffset = -ImmedOffset;
          else
            ImmedOffset |= 1 << NumBits;
        }
        ImmOp.ChangeToImmediate(ImmedOffset);
        Offset = 0;
        return true;
      }

      // Otherwise, it didn't fit. Pull in what we can to simplify the immed.
      ImmedOffset = ImmedOffset & Mask;
      if (isSub) {
        if (AddrMode == ARMII::AddrMode_i12)
          ImmedOffset = -ImmedOffset;
        else
          ImmedOffset |= 1 << NumBits;
      }
      ImmOp.ChangeToImmediate(ImmedOffset);
      Offset &= ~(Mask*Scale);
    }
  }

  Offset = (isSub) ? -Offset : Offset;
  return Offset == 0;
}

/// analyzeCompare - For a comparison instruction, return the source registers
/// in SrcReg and SrcReg2 if having two register operands, and the value it
/// compares against in CmpValue. Return true if the comparison instruction
/// can be analyzed.
bool ARMBaseInstrInfo::analyzeCompare(const MachineInstr &MI, unsigned &SrcReg,
                                      unsigned &SrcReg2, int &CmpMask,
                                      int &CmpValue) const {
  switch (MI.getOpcode()) {
  default: break;
  case ARM::CMPri:
  case ARM::t2CMPri:
  case ARM::tCMPi8:
    SrcReg = MI.getOperand(0).getReg();
    SrcReg2 = 0;
    CmpMask = ~0;
    CmpValue = MI.getOperand(1).getImm();
    return true;
  case ARM::CMPrr:
  case ARM::t2CMPrr:
    SrcReg = MI.getOperand(0).getReg();
    SrcReg2 = MI.getOperand(1).getReg();
    CmpMask = ~0;
    CmpValue = 0;
    return true;
  case ARM::TSTri:
  case ARM::t2TSTri:
    SrcReg = MI.getOperand(0).getReg();
    SrcReg2 = 0;
    CmpMask = MI.getOperand(1).getImm();
    CmpValue = 0;
    return true;
  }

  return false;
}

/// isSuitableForMask - Identify a suitable 'and' instruction that
/// operates on the given source register and applies the same mask
/// as a 'tst' instruction. Provide a limited look-through for copies.
/// When successful, MI will hold the found instruction.
static bool isSuitableForMask(MachineInstr *&MI, unsigned SrcReg,
                              int CmpMask, bool CommonUse) {
  switch (MI->getOpcode()) {
    case ARM::ANDri:
    case ARM::t2ANDri:
      if (CmpMask != MI->getOperand(2).getImm())
        return false;
      if (SrcReg == MI->getOperand(CommonUse ? 1 : 0).getReg())
        return true;
      break;
  }

  return false;
}

/// getSwappedCondition - assume the flags are set by MI(a,b), return
/// the condition code if we modify the instructions such that flags are
/// set by MI(b,a).
inline static ARMCC::CondCodes getSwappedCondition(ARMCC::CondCodes CC) {
  switch (CC) {
  default: return ARMCC::AL;
  case ARMCC::EQ: return ARMCC::EQ;
  case ARMCC::NE: return ARMCC::NE;
  case ARMCC::HS: return ARMCC::LS;
  case ARMCC::LO: return ARMCC::HI;
  case ARMCC::HI: return ARMCC::LO;
  case ARMCC::LS: return ARMCC::HS;
  case ARMCC::GE: return ARMCC::LE;
  case ARMCC::LT: return ARMCC::GT;
  case ARMCC::GT: return ARMCC::LT;
  case ARMCC::LE: return ARMCC::GE;
  }
}

/// getCmpToAddCondition - assume the flags are set by CMP(a,b), return
/// the condition code if we modify the instructions such that flags are
/// set by ADD(a,b,X).
inline static ARMCC::CondCodes getCmpToAddCondition(ARMCC::CondCodes CC) {
  switch (CC) {
  default: return ARMCC::AL;
  case ARMCC::HS: return ARMCC::LO;
  case ARMCC::LO: return ARMCC::HS;
  case ARMCC::VS: return ARMCC::VS;
  case ARMCC::VC: return ARMCC::VC;
  }
}

/// isRedundantFlagInstr - check whether the first instruction, whose only
/// purpose is to update flags, can be made redundant.
/// CMPrr can be made redundant by SUBrr if the operands are the same.
/// CMPri can be made redundant by SUBri if the operands are the same.
/// CMPrr(r0, r1) can be made redundant by ADDr[ri](r0, r1, X).
/// This function can be extended later on.
inline static bool isRedundantFlagInstr(const MachineInstr *CmpI,
                                        unsigned SrcReg, unsigned SrcReg2,
                                        int ImmValue, const MachineInstr *OI) {
  if ((CmpI->getOpcode() == ARM::CMPrr ||
       CmpI->getOpcode() == ARM::t2CMPrr) &&
      (OI->getOpcode() == ARM::SUBrr ||
       OI->getOpcode() == ARM::t2SUBrr) &&
      ((OI->getOperand(1).getReg() == SrcReg &&
        OI->getOperand(2).getReg() == SrcReg2) ||
       (OI->getOperand(1).getReg() == SrcReg2 &&
        OI->getOperand(2).getReg() == SrcReg)))
    return true;

  if ((CmpI->getOpcode() == ARM::CMPri ||
       CmpI->getOpcode() == ARM::t2CMPri) &&
      (OI->getOpcode() == ARM::SUBri ||
       OI->getOpcode() == ARM::t2SUBri) &&
      OI->getOperand(1).getReg() == SrcReg &&
      OI->getOperand(2).getImm() == ImmValue)
    return true;

  if ((CmpI->getOpcode() == ARM::CMPrr || CmpI->getOpcode() == ARM::t2CMPrr) &&
      (OI->getOpcode() == ARM::ADDrr || OI->getOpcode() == ARM::t2ADDrr ||
       OI->getOpcode() == ARM::ADDri || OI->getOpcode() == ARM::t2ADDri) &&
      OI->getOperand(0).isReg() && OI->getOperand(1).isReg() &&
      OI->getOperand(0).getReg() == SrcReg &&
      OI->getOperand(1).getReg() == SrcReg2)
    return true;
  return false;
}

static bool isOptimizeCompareCandidate(MachineInstr *MI, bool &IsThumb1) {
  switch (MI->getOpcode()) {
  default: return false;
  case ARM::tLSLri:
  case ARM::tLSRri:
  case ARM::tLSLrr:
  case ARM::tLSRrr:
  case ARM::tSUBrr:
  case ARM::tADDrr:
  case ARM::tADDi3:
  case ARM::tADDi8:
  case ARM::tSUBi3:
  case ARM::tSUBi8:
  case ARM::tMUL:
    IsThumb1 = true;
    LLVM_FALLTHROUGH;
  case ARM::RSBrr:
  case ARM::RSBri:
  case ARM::RSCrr:
  case ARM::RSCri:
  case ARM::ADDrr:
  case ARM::ADDri:
  case ARM::ADCrr:
  case ARM::ADCri:
  case ARM::SUBrr:
  case ARM::SUBri:
  case ARM::SBCrr:
  case ARM::SBCri:
  case ARM::t2RSBri:
  case ARM::t2ADDrr:
  case ARM::t2ADDri:
  case ARM::t2ADCrr:
  case ARM::t2ADCri:
  case ARM::t2SUBrr:
  case ARM::t2SUBri:
  case ARM::t2SBCrr:
  case ARM::t2SBCri:
  case ARM::ANDrr:
  case ARM::ANDri:
  case ARM::t2ANDrr:
  case ARM::t2ANDri:
  case ARM::ORRrr:
  case ARM::ORRri:
  case ARM::t2ORRrr:
  case ARM::t2ORRri:
  case ARM::EORrr:
  case ARM::EORri:
  case ARM::t2EORrr:
  case ARM::t2EORri:
  case ARM::t2LSRri:
  case ARM::t2LSRrr:
  case ARM::t2LSLri:
  case ARM::t2LSLrr:
    return true;
  }
}

/// optimizeCompareInstr - Convert the instruction supplying the argument to the
/// comparison into one that sets the zero bit in the flags register;
/// Remove a redundant Compare instruction if an earlier instruction can set the
/// flags in the same way as Compare.
/// E.g. SUBrr(r1,r2) and CMPrr(r1,r2). We also handle the case where two
/// operands are swapped: SUBrr(r1,r2) and CMPrr(r2,r1), by updating the
/// condition code of instructions which use the flags.
bool ARMBaseInstrInfo::optimizeCompareInstr(
    MachineInstr &CmpInstr, unsigned SrcReg, unsigned SrcReg2, int CmpMask,
    int CmpValue, const MachineRegisterInfo *MRI) const {
  // Get the unique definition of SrcReg.
  MachineInstr *MI = MRI->getUniqueVRegDef(SrcReg);
  if (!MI) return false;

  // Masked compares sometimes use the same register as the corresponding 'and'.
  if (CmpMask != ~0) {
    if (!isSuitableForMask(MI, SrcReg, CmpMask, false) || isPredicated(*MI)) {
      MI = nullptr;
      for (MachineRegisterInfo::use_instr_iterator
           UI = MRI->use_instr_begin(SrcReg), UE = MRI->use_instr_end();
           UI != UE; ++UI) {
        if (UI->getParent() != CmpInstr.getParent())
          continue;
        MachineInstr *PotentialAND = &*UI;
        if (!isSuitableForMask(PotentialAND, SrcReg, CmpMask, true) ||
            isPredicated(*PotentialAND))
          continue;
        MI = PotentialAND;
        break;
      }
      if (!MI) return false;
    }
  }

  // Get ready to iterate backward from CmpInstr.
  MachineBasicBlock::iterator I = CmpInstr, E = MI,
                              B = CmpInstr.getParent()->begin();

  // Early exit if CmpInstr is at the beginning of the BB.
  if (I == B) return false;

  // There are two possible candidates which can be changed to set CPSR:
  // One is MI, the other is a SUB or ADD instruction.
  // For CMPrr(r1,r2), we are looking for SUB(r1,r2), SUB(r2,r1), or
  // ADDr[ri](r1, r2, X).
  // For CMPri(r1, CmpValue), we are looking for SUBri(r1, CmpValue).
  MachineInstr *SubAdd = nullptr;
  if (SrcReg2 != 0)
    // MI is not a candidate for CMPrr.
    MI = nullptr;
  else if (MI->getParent() != CmpInstr.getParent() || CmpValue != 0) {
    // Conservatively refuse to convert an instruction which isn't in the same
    // BB as the comparison.
    // For CMPri w/ CmpValue != 0, a SubAdd may still be a candidate.
    // Thus we cannot return here.
    if (CmpInstr.getOpcode() == ARM::CMPri ||
        CmpInstr.getOpcode() == ARM::t2CMPri)
      MI = nullptr;
    else
      return false;
  }

  bool IsThumb1 = false;
  if (MI && !isOptimizeCompareCandidate(MI, IsThumb1))
    return false;

  // We also want to do this peephole for cases like this: if (a*b == 0),
  // and optimise away the CMP instruction from the generated code sequence:
  // MULS, MOVS, MOVS, CMP. Here the MOVS instructions load the boolean values
  // resulting from the select instruction, but these MOVS instructions for
  // Thumb1 (V6M) are flag setting and are thus preventing this optimisation.
  // However, if we only have MOVS instructions in between the CMP and the
  // other instruction (the MULS in this example), then the CPSR is dead so we
  // can safely reorder the sequence into: MOVS, MOVS, MULS, CMP. We do this
  // reordering and then continue the analysis hoping we can eliminate the
  // CMP. This peephole works on the vregs, so is still in SSA form. As a
  // consequence, the movs won't redefine/kill the MUL operands which would
  // make this reordering illegal.
  if (MI && IsThumb1) {
    --I;
    bool CanReorder = true;
    const bool HasStmts = I != E;
    for (; I != E; --I) {
      if (I->getOpcode() != ARM::tMOVi8) {
        CanReorder = false;
        break;
      }
    }
    if (HasStmts && CanReorder) {
      MI = MI->removeFromParent();
      E = CmpInstr;
      CmpInstr.getParent()->insert(E, MI);
    }
    I = CmpInstr;
    E = MI;
  }

  // Check that CPSR isn't set between the comparison instruction and the one we
  // want to change. At the same time, search for SubAdd.
  const TargetRegisterInfo *TRI = &getRegisterInfo();
  do {
    const MachineInstr &Instr = *--I;

    // Check whether CmpInstr can be made redundant by the current instruction.
    if (isRedundantFlagInstr(&CmpInstr, SrcReg, SrcReg2, CmpValue, &Instr)) {
      SubAdd = &*I;
      break;
    }

    // Allow E (which was initially MI) to be SubAdd but do not search before E.
    if (I == E)
      break;

    if (Instr.modifiesRegister(ARM::CPSR, TRI) ||
        Instr.readsRegister(ARM::CPSR, TRI))
      // This instruction modifies or uses CPSR after the one we want to
      // change. We can't do this transformation.
      return false;

    if (I == B) {
      // In some cases, we scan the use-list of an instruction for an AND;
      // that AND is in the same BB, but may not be scheduled before the
      // corresponding TST.  In that case, bail out.
      //
      // FIXME: We could try to reschedule the AND.
      return false;
    }
  } while (true);

  // Return false if no candidates exist.
  if (!MI && !SubAdd)
    return false;

  // The single candidate is called MI.
  if (!MI) MI = SubAdd;

  // We can't use a predicated instruction - it doesn't always write the flags.
  if (isPredicated(*MI))
    return false;

  // Scan forward for the use of CPSR
  // When checking against MI: if it's a conditional code that requires
  // checking of the V bit or C bit, then this is not safe to do.
  // It is safe to remove CmpInstr if CPSR is redefined or killed.
  // If we are done with the basic block, we need to check whether CPSR is
  // live-out.
  SmallVector<std::pair<MachineOperand*, ARMCC::CondCodes>, 4>
      OperandsToUpdate;
  bool isSafe = false;
  I = CmpInstr;
  E = CmpInstr.getParent()->end();
  while (!isSafe && ++I != E) {
    const MachineInstr &Instr = *I;
    for (unsigned IO = 0, EO = Instr.getNumOperands();
         !isSafe && IO != EO; ++IO) {
      const MachineOperand &MO = Instr.getOperand(IO);
      if (MO.isRegMask() && MO.clobbersPhysReg(ARM::CPSR)) {
        isSafe = true;
        break;
      }
      if (!MO.isReg() || MO.getReg() != ARM::CPSR)
        continue;
      if (MO.isDef()) {
        isSafe = true;
        break;
      }
      // Condition code is after the operand before CPSR except for VSELs.
      ARMCC::CondCodes CC;
      bool IsInstrVSel = true;
      switch (Instr.getOpcode()) {
      default:
        IsInstrVSel = false;
        CC = (ARMCC::CondCodes)Instr.getOperand(IO - 1).getImm();
        break;
      case ARM::VSELEQD:
      case ARM::VSELEQS:
        CC = ARMCC::EQ;
        break;
      case ARM::VSELGTD:
      case ARM::VSELGTS:
        CC = ARMCC::GT;
        break;
      case ARM::VSELGED:
      case ARM::VSELGES:
        CC = ARMCC::GE;
        break;
      case ARM::VSELVSS:
      case ARM::VSELVSD:
        CC = ARMCC::VS;
        break;
      }

      if (SubAdd) {
        // If we have SUB(r1, r2) and CMP(r2, r1), the condition code based
        // on CMP needs to be updated to be based on SUB.
        // If we have ADD(r1, r2, X) and CMP(r1, r2), the condition code also
        // needs to be modified.
        // Push the condition code operands to OperandsToUpdate.
        // If it is safe to remove CmpInstr, the condition code of these
        // operands will be modified.
        unsigned Opc = SubAdd->getOpcode();
        bool IsSub = Opc == ARM::SUBrr || Opc == ARM::t2SUBrr ||
                     Opc == ARM::SUBri || Opc == ARM::t2SUBri;
        if (!IsSub || (SrcReg2 != 0 && SubAdd->getOperand(1).getReg() == SrcReg2 &&
                       SubAdd->getOperand(2).getReg() == SrcReg)) {
          // VSel doesn't support condition code update.
          if (IsInstrVSel)
            return false;
          // Ensure we can swap the condition.
          ARMCC::CondCodes NewCC = (IsSub ? getSwappedCondition(CC) : getCmpToAddCondition(CC));
          if (NewCC == ARMCC::AL)
            return false;
          OperandsToUpdate.push_back(
              std::make_pair(&((*I).getOperand(IO - 1)), NewCC));
        }
      } else {
        // No SubAdd, so this is x = <op> y, z; cmp x, 0.
        switch (CC) {
        case ARMCC::EQ: // Z
        case ARMCC::NE: // Z
        case ARMCC::MI: // N
        case ARMCC::PL: // N
        case ARMCC::AL: // none
          // CPSR can be used multiple times, we should continue.
          break;
        case ARMCC::HS: // C
        case ARMCC::LO: // C
        case ARMCC::VS: // V
        case ARMCC::VC: // V
        case ARMCC::HI: // C Z
        case ARMCC::LS: // C Z
        case ARMCC::GE: // N V
        case ARMCC::LT: // N V
        case ARMCC::GT: // Z N V
        case ARMCC::LE: // Z N V
          // The instruction uses the V bit or C bit which is not safe.
          return false;
        }
      }
    }
  }

  // If CPSR is not killed nor re-defined, we should check whether it is
  // live-out. If it is live-out, do not optimize.
  if (!isSafe) {
    MachineBasicBlock *MBB = CmpInstr.getParent();
    for (MachineBasicBlock::succ_iterator SI = MBB->succ_begin(),
             SE = MBB->succ_end(); SI != SE; ++SI)
      if ((*SI)->isLiveIn(ARM::CPSR))
        return false;
  }

  // Toggle the optional operand to CPSR (if it exists - in Thumb1 we always
  // set CPSR so this is represented as an explicit output)
  if (!IsThumb1) {
    MI->getOperand(5).setReg(ARM::CPSR);
    MI->getOperand(5).setIsDef(true);
  }
  assert(!isPredicated(*MI) && "Can't use flags from predicated instruction");
  CmpInstr.eraseFromParent();

  // Modify the condition code of operands in OperandsToUpdate.
  // Since we have SUB(r1, r2) and CMP(r2, r1), the condition code needs to
  // be changed from r2 > r1 to r1 < r2, from r2 < r1 to r1 > r2, etc.
  for (unsigned i = 0, e = OperandsToUpdate.size(); i < e; i++)
    OperandsToUpdate[i].first->setImm(OperandsToUpdate[i].second);

  MI->clearRegisterDeads(ARM::CPSR);

  return true;
}

bool ARMBaseInstrInfo::shouldSink(const MachineInstr &MI) const {
  // Do not sink MI if it might be used to optimize a redundant compare.
  // We heuristically only look at the instruction immediately following MI to
  // avoid potentially searching the entire basic block.
  if (isPredicated(MI))
    return true;
  MachineBasicBlock::const_iterator Next = &MI;
  ++Next;
  unsigned SrcReg, SrcReg2;
  int CmpMask, CmpValue;
  if (Next != MI.getParent()->end() &&
      analyzeCompare(*Next, SrcReg, SrcReg2, CmpMask, CmpValue) &&
      isRedundantFlagInstr(&*Next, SrcReg, SrcReg2, CmpValue, &MI))
    return false;
  return true;
}

bool ARMBaseInstrInfo::FoldImmediate(MachineInstr &UseMI, MachineInstr &DefMI,
                                     unsigned Reg,
                                     MachineRegisterInfo *MRI) const {
  // Fold large immediates into add, sub, or, xor.
  unsigned DefOpc = DefMI.getOpcode();
  if (DefOpc != ARM::t2MOVi32imm && DefOpc != ARM::MOVi32imm)
    return false;
  if (!DefMI.getOperand(1).isImm())
    // Could be t2MOVi32imm @xx
    return false;

  if (!MRI->hasOneNonDBGUse(Reg))
    return false;

  const MCInstrDesc &DefMCID = DefMI.getDesc();
  if (DefMCID.hasOptionalDef()) {
    unsigned NumOps = DefMCID.getNumOperands();
    const MachineOperand &MO = DefMI.getOperand(NumOps - 1);
    if (MO.getReg() == ARM::CPSR && !MO.isDead())
      // If DefMI defines CPSR and it is not dead, it's obviously not safe
      // to delete DefMI.
      return false;
  }

  const MCInstrDesc &UseMCID = UseMI.getDesc();
  if (UseMCID.hasOptionalDef()) {
    unsigned NumOps = UseMCID.getNumOperands();
    if (UseMI.getOperand(NumOps - 1).getReg() == ARM::CPSR)
      // If the instruction sets the flag, do not attempt this optimization
      // since it may change the semantics of the code.
      return false;
  }

  unsigned UseOpc = UseMI.getOpcode();
  unsigned NewUseOpc = 0;
  uint32_t ImmVal = (uint32_t)DefMI.getOperand(1).getImm();
  uint32_t SOImmValV1 = 0, SOImmValV2 = 0;
  bool Commute = false;
  switch (UseOpc) {
  default: return false;
  case ARM::SUBrr:
  case ARM::ADDrr:
  case ARM::ORRrr:
  case ARM::EORrr:
  case ARM::t2SUBrr:
  case ARM::t2ADDrr:
  case ARM::t2ORRrr:
  case ARM::t2EORrr: {
    Commute = UseMI.getOperand(2).getReg() != Reg;
    switch (UseOpc) {
    default: break;
    case ARM::ADDrr:
    case ARM::SUBrr:
      if (UseOpc == ARM::SUBrr && Commute)
        return false;

      // ADD/SUB are special because they're essentially the same operation, so
      // we can handle a larger range of immediates.
      if (ARM_AM::isSOImmTwoPartVal(ImmVal))
        NewUseOpc = UseOpc == ARM::ADDrr ? ARM::ADDri : ARM::SUBri;
      else if (ARM_AM::isSOImmTwoPartVal(-ImmVal)) {
        ImmVal = -ImmVal;
        NewUseOpc = UseOpc == ARM::ADDrr ? ARM::SUBri : ARM::ADDri;
      } else
        return false;
      SOImmValV1 = (uint32_t)ARM_AM::getSOImmTwoPartFirst(ImmVal);
      SOImmValV2 = (uint32_t)ARM_AM::getSOImmTwoPartSecond(ImmVal);
      break;
    case ARM::ORRrr:
    case ARM::EORrr:
      if (!ARM_AM::isSOImmTwoPartVal(ImmVal))
        return false;
      SOImmValV1 = (uint32_t)ARM_AM::getSOImmTwoPartFirst(ImmVal);
      SOImmValV2 = (uint32_t)ARM_AM::getSOImmTwoPartSecond(ImmVal);
      switch (UseOpc) {
      default: break;
      case ARM::ORRrr: NewUseOpc = ARM::ORRri; break;
      case ARM::EORrr: NewUseOpc = ARM::EORri; break;
      }
      break;
    case ARM::t2ADDrr:
    case ARM::t2SUBrr:
      if (UseOpc == ARM::t2SUBrr && Commute)
        return false;

      // ADD/SUB are special because they're essentially the same operation, so
      // we can handle a larger range of immediates.
      if (ARM_AM::isT2SOImmTwoPartVal(ImmVal))
        NewUseOpc = UseOpc == ARM::t2ADDrr ? ARM::t2ADDri : ARM::t2SUBri;
      else if (ARM_AM::isT2SOImmTwoPartVal(-ImmVal)) {
        ImmVal = -ImmVal;
        NewUseOpc = UseOpc == ARM::t2ADDrr ? ARM::t2SUBri : ARM::t2ADDri;
      } else
        return false;
      SOImmValV1 = (uint32_t)ARM_AM::getT2SOImmTwoPartFirst(ImmVal);
      SOImmValV2 = (uint32_t)ARM_AM::getT2SOImmTwoPartSecond(ImmVal);
      break;
    case ARM::t2ORRrr:
    case ARM::t2EORrr:
      if (!ARM_AM::isT2SOImmTwoPartVal(ImmVal))
        return false;
      SOImmValV1 = (uint32_t)ARM_AM::getT2SOImmTwoPartFirst(ImmVal);
      SOImmValV2 = (uint32_t)ARM_AM::getT2SOImmTwoPartSecond(ImmVal);
      switch (UseOpc) {
      default: break;
      case ARM::t2ORRrr: NewUseOpc = ARM::t2ORRri; break;
      case ARM::t2EORrr: NewUseOpc = ARM::t2EORri; break;
      }
      break;
    }
  }
  }

  unsigned OpIdx = Commute ? 2 : 1;
  unsigned Reg1 = UseMI.getOperand(OpIdx).getReg();
  bool isKill = UseMI.getOperand(OpIdx).isKill();
  unsigned NewReg = MRI->createVirtualRegister(MRI->getRegClass(Reg));
  BuildMI(*UseMI.getParent(), UseMI, UseMI.getDebugLoc(), get(NewUseOpc),
          NewReg)
      .addReg(Reg1, getKillRegState(isKill))
      .addImm(SOImmValV1)
      .add(predOps(ARMCC::AL))
      .add(condCodeOp());
  UseMI.setDesc(get(NewUseOpc));
  UseMI.getOperand(1).setReg(NewReg);
  UseMI.getOperand(1).setIsKill();
  UseMI.getOperand(2).ChangeToImmediate(SOImmValV2);
  DefMI.eraseFromParent();
  return true;
}

static unsigned getNumMicroOpsSwiftLdSt(const InstrItineraryData *ItinData,
                                        const MachineInstr &MI) {
  switch (MI.getOpcode()) {
  default: {
    const MCInstrDesc &Desc = MI.getDesc();
    int UOps = ItinData->getNumMicroOps(Desc.getSchedClass());
    assert(UOps >= 0 && "bad # UOps");
    return UOps;
  }

  case ARM::LDRrs:
  case ARM::LDRBrs:
  case ARM::STRrs:
  case ARM::STRBrs: {
    unsigned ShOpVal = MI.getOperand(3).getImm();
    bool isSub = ARM_AM::getAM2Op(ShOpVal) == ARM_AM::sub;
    unsigned ShImm = ARM_AM::getAM2Offset(ShOpVal);
    if (!isSub &&
        (ShImm == 0 ||
         ((ShImm == 1 || ShImm == 2 || ShImm == 3) &&
          ARM_AM::getAM2ShiftOpc(ShOpVal) == ARM_AM::lsl)))
      return 1;
    return 2;
  }

  case ARM::LDRH:
  case ARM::STRH: {
    if (!MI.getOperand(2).getReg())
      return 1;

    unsigned ShOpVal = MI.getOperand(3).getImm();
    bool isSub = ARM_AM::getAM2Op(ShOpVal) == ARM_AM::sub;
    unsigned ShImm = ARM_AM::getAM2Offset(ShOpVal);
    if (!isSub &&
        (ShImm == 0 ||
         ((ShImm == 1 || ShImm == 2 || ShImm == 3) &&
          ARM_AM::getAM2ShiftOpc(ShOpVal) == ARM_AM::lsl)))
      return 1;
    return 2;
  }

  case ARM::LDRSB:
  case ARM::LDRSH:
    return (ARM_AM::getAM3Op(MI.getOperand(3).getImm()) == ARM_AM::sub) ? 3 : 2;

  case ARM::LDRSB_POST:
  case ARM::LDRSH_POST: {
    unsigned Rt = MI.getOperand(0).getReg();
    unsigned Rm = MI.getOperand(3).getReg();
    return (Rt == Rm) ? 4 : 3;
  }

  case ARM::LDR_PRE_REG:
  case ARM::LDRB_PRE_REG: {
    unsigned Rt = MI.getOperand(0).getReg();
    unsigned Rm = MI.getOperand(3).getReg();
    if (Rt == Rm)
      return 3;
    unsigned ShOpVal = MI.getOperand(4).getImm();
    bool isSub = ARM_AM::getAM2Op(ShOpVal) == ARM_AM::sub;
    unsigned ShImm = ARM_AM::getAM2Offset(ShOpVal);
    if (!isSub &&
        (ShImm == 0 ||
         ((ShImm == 1 || ShImm == 2 || ShImm == 3) &&
          ARM_AM::getAM2ShiftOpc(ShOpVal) == ARM_AM::lsl)))
      return 2;
    return 3;
  }

  case ARM::STR_PRE_REG:
  case ARM::STRB_PRE_REG: {
    unsigned ShOpVal = MI.getOperand(4).getImm();
    bool isSub = ARM_AM::getAM2Op(ShOpVal) == ARM_AM::sub;
    unsigned ShImm = ARM_AM::getAM2Offset(ShOpVal);
    if (!isSub &&
        (ShImm == 0 ||
         ((ShImm == 1 || ShImm == 2 || ShImm == 3) &&
          ARM_AM::getAM2ShiftOpc(ShOpVal) == ARM_AM::lsl)))
      return 2;
    return 3;
  }

  case ARM::LDRH_PRE:
  case ARM::STRH_PRE: {
    unsigned Rt = MI.getOperand(0).getReg();
    unsigned Rm = MI.getOperand(3).getReg();
    if (!Rm)
      return 2;
    if (Rt == Rm)
      return 3;
    return (ARM_AM::getAM3Op(MI.getOperand(4).getImm()) == ARM_AM::sub) ? 3 : 2;
  }

  case ARM::LDR_POST_REG:
  case ARM::LDRB_POST_REG:
  case ARM::LDRH_POST: {
    unsigned Rt = MI.getOperand(0).getReg();
    unsigned Rm = MI.getOperand(3).getReg();
    return (Rt == Rm) ? 3 : 2;
  }

  case ARM::LDR_PRE_IMM:
  case ARM::LDRB_PRE_IMM:
  case ARM::LDR_POST_IMM:
  case ARM::LDRB_POST_IMM:
  case ARM::STRB_POST_IMM:
  case ARM::STRB_POST_REG:
  case ARM::STRB_PRE_IMM:
  case ARM::STRH_POST:
  case ARM::STR_POST_IMM:
  case ARM::STR_POST_REG:
  case ARM::STR_PRE_IMM:
    return 2;

  case ARM::LDRSB_PRE:
  case ARM::LDRSH_PRE: {
    unsigned Rm = MI.getOperand(3).getReg();
    if (Rm == 0)
      return 3;
    unsigned Rt = MI.getOperand(0).getReg();
    if (Rt == Rm)
      return 4;
    unsigned ShOpVal = MI.getOperand(4).getImm();
    bool isSub = ARM_AM::getAM2Op(ShOpVal) == ARM_AM::sub;
    unsigned ShImm = ARM_AM::getAM2Offset(ShOpVal);
    if (!isSub &&
        (ShImm == 0 ||
         ((ShImm == 1 || ShImm == 2 || ShImm == 3) &&
          ARM_AM::getAM2ShiftOpc(ShOpVal) == ARM_AM::lsl)))
      return 3;
    return 4;
  }

  case ARM::LDRD: {
    unsigned Rt = MI.getOperand(0).getReg();
    unsigned Rn = MI.getOperand(2).getReg();
    unsigned Rm = MI.getOperand(3).getReg();
    if (Rm)
      return (ARM_AM::getAM3Op(MI.getOperand(4).getImm()) == ARM_AM::sub) ? 4
                                                                          : 3;
    return (Rt == Rn) ? 3 : 2;
  }

  case ARM::STRD: {
    unsigned Rm = MI.getOperand(3).getReg();
    if (Rm)
      return (ARM_AM::getAM3Op(MI.getOperand(4).getImm()) == ARM_AM::sub) ? 4
                                                                          : 3;
    return 2;
  }

  case ARM::LDRD_POST:
  case ARM::t2LDRD_POST:
    return 3;

  case ARM::STRD_POST:
  case ARM::t2STRD_POST:
    return 4;

  case ARM::LDRD_PRE: {
    unsigned Rt = MI.getOperand(0).getReg();
    unsigned Rn = MI.getOperand(3).getReg();
    unsigned Rm = MI.getOperand(4).getReg();
    if (Rm)
      return (ARM_AM::getAM3Op(MI.getOperand(5).getImm()) == ARM_AM::sub) ? 5
                                                                          : 4;
    return (Rt == Rn) ? 4 : 3;
  }

  case ARM::t2LDRD_PRE: {
    unsigned Rt = MI.getOperand(0).getReg();
    unsigned Rn = MI.getOperand(3).getReg();
    return (Rt == Rn) ? 4 : 3;
  }

  case ARM::STRD_PRE: {
    unsigned Rm = MI.getOperand(4).getReg();
    if (Rm)
      return (ARM_AM::getAM3Op(MI.getOperand(5).getImm()) == ARM_AM::sub) ? 5
                                                                          : 4;
    return 3;
  }

  case ARM::t2STRD_PRE:
    return 3;

  case ARM::t2LDR_POST:
  case ARM::t2LDRB_POST:
  case ARM::t2LDRB_PRE:
  case ARM::t2LDRSBi12:
  case ARM::t2LDRSBi8:
  case ARM::t2LDRSBpci:
  case ARM::t2LDRSBs:
  case ARM::t2LDRH_POST:
  case ARM::t2LDRH_PRE:
  case ARM::t2LDRSBT:
  case ARM::t2LDRSB_POST:
  case ARM::t2LDRSB_PRE:
  case ARM::t2LDRSH_POST:
  case ARM::t2LDRSH_PRE:
  case ARM::t2LDRSHi12:
  case ARM::t2LDRSHi8:
  case ARM::t2LDRSHpci:
  case ARM::t2LDRSHs:
    return 2;

  case ARM::t2LDRDi8: {
    unsigned Rt = MI.getOperand(0).getReg();
    unsigned Rn = MI.getOperand(2).getReg();
    return (Rt == Rn) ? 3 : 2;
  }

  case ARM::t2STRB_POST:
  case ARM::t2STRB_PRE:
  case ARM::t2STRBs:
  case ARM::t2STRDi8:
  case ARM::t2STRH_POST:
  case ARM::t2STRH_PRE:
  case ARM::t2STRHs:
  case ARM::t2STR_POST:
  case ARM::t2STR_PRE:
  case ARM::t2STRs:
    return 2;
  }
}

// Return the number of 32-bit words loaded by LDM or stored by STM. If this
// can't be easily determined return 0 (missing MachineMemOperand).
//
// FIXME: The current MachineInstr design does not support relying on machine
// mem operands to determine the width of a memory access. Instead, we expect
// the target to provide this information based on the instruction opcode and
// operands. However, using MachineMemOperand is the best solution now for
// two reasons:
//
// 1) getNumMicroOps tries to infer LDM memory width from the total number of MI
// operands. This is much more dangerous than using the MachineMemOperand
// sizes because CodeGen passes can insert/remove optional machine operands. In
// fact, it's totally incorrect for preRA passes and appears to be wrong for
// postRA passes as well.
//
// 2) getNumLDMAddresses is only used by the scheduling machine model and any
// machine model that calls this should handle the unknown (zero size) case.
//
// Long term, we should require a target hook that verifies MachineMemOperand
// sizes during MC lowering. That target hook should be local to MC lowering
// because we can't ensure that it is aware of other MI forms. Doing this will
// ensure that MachineMemOperands are correctly propagated through all passes.
unsigned ARMBaseInstrInfo::getNumLDMAddresses(const MachineInstr &MI) const {
  unsigned Size = 0;
  for (MachineInstr::mmo_iterator I = MI.memoperands_begin(),
                                  E = MI.memoperands_end();
       I != E; ++I) {
    Size += (*I)->getSize();
  }
  return Size / 4;
}

static unsigned getNumMicroOpsSingleIssuePlusExtras(unsigned Opc,
                                                    unsigned NumRegs) {
  unsigned UOps = 1 + NumRegs; // 1 for address computation.
  switch (Opc) {
  default:
    break;
  case ARM::VLDMDIA_UPD:
  case ARM::VLDMDDB_UPD:
  case ARM::VLDMSIA_UPD:
  case ARM::VLDMSDB_UPD:
  case ARM::VSTMDIA_UPD:
  case ARM::VSTMDDB_UPD:
  case ARM::VSTMSIA_UPD:
  case ARM::VSTMSDB_UPD:
  case ARM::LDMIA_UPD:
  case ARM::LDMDA_UPD:
  case ARM::LDMDB_UPD:
  case ARM::LDMIB_UPD:
  case ARM::STMIA_UPD:
  case ARM::STMDA_UPD:
  case ARM::STMDB_UPD:
  case ARM::STMIB_UPD:
  case ARM::tLDMIA_UPD:
  case ARM::tSTMIA_UPD:
  case ARM::t2LDMIA_UPD:
  case ARM::t2LDMDB_UPD:
  case ARM::t2STMIA_UPD:
  case ARM::t2STMDB_UPD:
    ++UOps; // One for base register writeback.
    break;
  case ARM::LDMIA_RET:
  case ARM::tPOP_RET:
  case ARM::t2LDMIA_RET:
    UOps += 2; // One for base reg wb, one for write to pc.
    break;
  }
  return UOps;
}

unsigned ARMBaseInstrInfo::getNumMicroOps(const InstrItineraryData *ItinData,
                                          const MachineInstr &MI) const {
  if (!ItinData || ItinData->isEmpty())
    return 1;

  const MCInstrDesc &Desc = MI.getDesc();
  unsigned Class = Desc.getSchedClass();
  int ItinUOps = ItinData->getNumMicroOps(Class);
  if (ItinUOps >= 0) {
    if (Subtarget.isSwift() && (Desc.mayLoad() || Desc.mayStore()))
      return getNumMicroOpsSwiftLdSt(ItinData, MI);

    return ItinUOps;
  }

  unsigned Opc = MI.getOpcode();
  switch (Opc) {
  default:
    llvm_unreachable("Unexpected multi-uops instruction!");
  case ARM::VLDMQIA:
  case ARM::VSTMQIA:
    return 2;

  // The number of uOps for load / store multiple are determined by the number
  // registers.
  //
  // On Cortex-A8, each pair of register loads / stores can be scheduled on the
  // same cycle. The scheduling for the first load / store must be done
  // separately by assuming the address is not 64-bit aligned.
  //
  // On Cortex-A9, the formula is simply (#reg / 2) + (#reg % 2). If the address
  // is not 64-bit aligned, then AGU would take an extra cycle.  For VFP / NEON
  // load / store multiple, the formula is (#reg / 2) + (#reg % 2) + 1.
  case ARM::VLDMDIA:
  case ARM::VLDMDIA_UPD:
  case ARM::VLDMDDB_UPD:
  case ARM::VLDMSIA:
  case ARM::VLDMSIA_UPD:
  case ARM::VLDMSDB_UPD:
  case ARM::VSTMDIA:
  case ARM::VSTMDIA_UPD:
  case ARM::VSTMDDB_UPD:
  case ARM::VSTMSIA:
  case ARM::VSTMSIA_UPD:
  case ARM::VSTMSDB_UPD: {
    unsigned NumRegs = MI.getNumOperands() - Desc.getNumOperands();
    return (NumRegs / 2) + (NumRegs % 2) + 1;
  }

  case ARM::LDMIA_RET:
  case ARM::LDMIA:
  case ARM::LDMDA:
  case ARM::LDMDB:
  case ARM::LDMIB:
  case ARM::LDMIA_UPD:
  case ARM::LDMDA_UPD:
  case ARM::LDMDB_UPD:
  case ARM::LDMIB_UPD:
  case ARM::STMIA:
  case ARM::STMDA:
  case ARM::STMDB:
  case ARM::STMIB:
  case ARM::STMIA_UPD:
  case ARM::STMDA_UPD:
  case ARM::STMDB_UPD:
  case ARM::STMIB_UPD:
  case ARM::tLDMIA:
  case ARM::tLDMIA_UPD:
  case ARM::tSTMIA_UPD:
  case ARM::tPOP_RET:
  case ARM::tPOP:
  case ARM::tPUSH:
  case ARM::t2LDMIA_RET:
  case ARM::t2LDMIA:
  case ARM::t2LDMDB:
  case ARM::t2LDMIA_UPD:
  case ARM::t2LDMDB_UPD:
  case ARM::t2STMIA:
  case ARM::t2STMDB:
  case ARM::t2STMIA_UPD:
  case ARM::t2STMDB_UPD: {
    unsigned NumRegs = MI.getNumOperands() - Desc.getNumOperands() + 1;
    switch (Subtarget.getLdStMultipleTiming()) {
    case ARMSubtarget::SingleIssuePlusExtras:
      return getNumMicroOpsSingleIssuePlusExtras(Opc, NumRegs);
    case ARMSubtarget::SingleIssue:
      // Assume the worst.
      return NumRegs;
    case ARMSubtarget::DoubleIssue: {
      if (NumRegs < 4)
        return 2;
      // 4 registers would be issued: 2, 2.
      // 5 registers would be issued: 2, 2, 1.
      unsigned UOps = (NumRegs / 2);
      if (NumRegs % 2)
        ++UOps;
      return UOps;
    }
    case ARMSubtarget::DoubleIssueCheckUnalignedAccess: {
      unsigned UOps = (NumRegs / 2);
      // If there are odd number of registers or if it's not 64-bit aligned,
      // then it takes an extra AGU (Address Generation Unit) cycle.
      if ((NumRegs % 2) || !MI.hasOneMemOperand() ||
          (*MI.memoperands_begin())->getAlignment() < 8)
        ++UOps;
      return UOps;
      }
    }
  }
  }
  llvm_unreachable("Didn't find the number of microops");
}

int
ARMBaseInstrInfo::getVLDMDefCycle(const InstrItineraryData *ItinData,
                                  const MCInstrDesc &DefMCID,
                                  unsigned DefClass,
                                  unsigned DefIdx, unsigned DefAlign) const {
  int RegNo = (int)(DefIdx+1) - DefMCID.getNumOperands() + 1;
  if (RegNo <= 0)
    // Def is the address writeback.
    return ItinData->getOperandCycle(DefClass, DefIdx);

  int DefCycle;
  if (Subtarget.isCortexA8() || Subtarget.isCortexA7()) {
    // (regno / 2) + (regno % 2) + 1
    DefCycle = RegNo / 2 + 1;
    if (RegNo % 2)
      ++DefCycle;
  } else if (Subtarget.isLikeA9() || Subtarget.isSwift()) {
    DefCycle = RegNo;
    bool isSLoad = false;

    switch (DefMCID.getOpcode()) {
    default: break;
    case ARM::VLDMSIA:
    case ARM::VLDMSIA_UPD:
    case ARM::VLDMSDB_UPD:
      isSLoad = true;
      break;
    }

    // If there are odd number of 'S' registers or if it's not 64-bit aligned,
    // then it takes an extra cycle.
    if ((isSLoad && (RegNo % 2)) || DefAlign < 8)
      ++DefCycle;
  } else {
    // Assume the worst.
    DefCycle = RegNo + 2;
  }

  return DefCycle;
}

bool ARMBaseInstrInfo::isLDMBaseRegInList(const MachineInstr &MI) const {
  unsigned BaseReg = MI.getOperand(0).getReg();
  for (unsigned i = 1, sz = MI.getNumOperands(); i < sz; ++i) {
    const auto &Op = MI.getOperand(i);
    if (Op.isReg() && Op.getReg() == BaseReg)
      return true;
  }
  return false;
}
unsigned
ARMBaseInstrInfo::getLDMVariableDefsSize(const MachineInstr &MI) const {
  // ins GPR:$Rn, $p (2xOp), reglist:$regs, variable_ops
  // (outs GPR:$wb), (ins GPR:$Rn, $p (2xOp), reglist:$regs, variable_ops)
  return MI.getNumOperands() + 1 - MI.getDesc().getNumOperands();
}

int
ARMBaseInstrInfo::getLDMDefCycle(const InstrItineraryData *ItinData,
                                 const MCInstrDesc &DefMCID,
                                 unsigned DefClass,
                                 unsigned DefIdx, unsigned DefAlign) const {
  int RegNo = (int)(DefIdx+1) - DefMCID.getNumOperands() + 1;
  if (RegNo <= 0)
    // Def is the address writeback.
    return ItinData->getOperandCycle(DefClass, DefIdx);

  int DefCycle;
  if (Subtarget.isCortexA8() || Subtarget.isCortexA7()) {
    // 4 registers would be issued: 1, 2, 1.
    // 5 registers would be issued: 1, 2, 2.
    DefCycle = RegNo / 2;
    if (DefCycle < 1)
      DefCycle = 1;
    // Result latency is issue cycle + 2: E2.
    DefCycle += 2;
  } else if (Subtarget.isLikeA9() || Subtarget.isSwift()) {
    DefCycle = (RegNo / 2);
    // If there are odd number of registers or if it's not 64-bit aligned,
    // then it takes an extra AGU (Address Generation Unit) cycle.
    if ((RegNo % 2) || DefAlign < 8)
      ++DefCycle;
    // Result latency is AGU cycles + 2.
    DefCycle += 2;
  } else {
    // Assume the worst.
    DefCycle = RegNo + 2;
  }

  return DefCycle;
}

int
ARMBaseInstrInfo::getVSTMUseCycle(const InstrItineraryData *ItinData,
                                  const MCInstrDesc &UseMCID,
                                  unsigned UseClass,
                                  unsigned UseIdx, unsigned UseAlign) const {
  int RegNo = (int)(UseIdx+1) - UseMCID.getNumOperands() + 1;
  if (RegNo <= 0)
    return ItinData->getOperandCycle(UseClass, UseIdx);

  int UseCycle;
  if (Subtarget.isCortexA8() || Subtarget.isCortexA7()) {
    // (regno / 2) + (regno % 2) + 1
    UseCycle = RegNo / 2 + 1;
    if (RegNo % 2)
      ++UseCycle;
  } else if (Subtarget.isLikeA9() || Subtarget.isSwift()) {
    UseCycle = RegNo;
    bool isSStore = false;

    switch (UseMCID.getOpcode()) {
    default: break;
    case ARM::VSTMSIA:
    case ARM::VSTMSIA_UPD:
    case ARM::VSTMSDB_UPD:
      isSStore = true;
      break;
    }

    // If there are odd number of 'S' registers or if it's not 64-bit aligned,
    // then it takes an extra cycle.
    if ((isSStore && (RegNo % 2)) || UseAlign < 8)
      ++UseCycle;
  } else {
    // Assume the worst.
    UseCycle = RegNo + 2;
  }

  return UseCycle;
}

int
ARMBaseInstrInfo::getSTMUseCycle(const InstrItineraryData *ItinData,
                                 const MCInstrDesc &UseMCID,
                                 unsigned UseClass,
                                 unsigned UseIdx, unsigned UseAlign) const {
  int RegNo = (int)(UseIdx+1) - UseMCID.getNumOperands() + 1;
  if (RegNo <= 0)
    return ItinData->getOperandCycle(UseClass, UseIdx);

  int UseCycle;
  if (Subtarget.isCortexA8() || Subtarget.isCortexA7()) {
    UseCycle = RegNo / 2;
    if (UseCycle < 2)
      UseCycle = 2;
    // Read in E3.
    UseCycle += 2;
  } else if (Subtarget.isLikeA9() || Subtarget.isSwift()) {
    UseCycle = (RegNo / 2);
    // If there are odd number of registers or if it's not 64-bit aligned,
    // then it takes an extra AGU (Address Generation Unit) cycle.
    if ((RegNo % 2) || UseAlign < 8)
      ++UseCycle;
  } else {
    // Assume the worst.
    UseCycle = 1;
  }
  return UseCycle;
}

int
ARMBaseInstrInfo::getOperandLatency(const InstrItineraryData *ItinData,
                                    const MCInstrDesc &DefMCID,
                                    unsigned DefIdx, unsigned DefAlign,
                                    const MCInstrDesc &UseMCID,
                                    unsigned UseIdx, unsigned UseAlign) const {
  unsigned DefClass = DefMCID.getSchedClass();
  unsigned UseClass = UseMCID.getSchedClass();

  if (DefIdx < DefMCID.getNumDefs() && UseIdx < UseMCID.getNumOperands())
    return ItinData->getOperandLatency(DefClass, DefIdx, UseClass, UseIdx);

  // This may be a def / use of a variable_ops instruction, the operand
  // latency might be determinable dynamically. Let the target try to
  // figure it out.
  int DefCycle = -1;
  bool LdmBypass = false;
  switch (DefMCID.getOpcode()) {
  default:
    DefCycle = ItinData->getOperandCycle(DefClass, DefIdx);
    break;

  case ARM::VLDMDIA:
  case ARM::VLDMDIA_UPD:
  case ARM::VLDMDDB_UPD:
  case ARM::VLDMSIA:
  case ARM::VLDMSIA_UPD:
  case ARM::VLDMSDB_UPD:
    DefCycle = getVLDMDefCycle(ItinData, DefMCID, DefClass, DefIdx, DefAlign);
    break;

  case ARM::LDMIA_RET:
  case ARM::LDMIA:
  case ARM::LDMDA:
  case ARM::LDMDB:
  case ARM::LDMIB:
  case ARM::LDMIA_UPD:
  case ARM::LDMDA_UPD:
  case ARM::LDMDB_UPD:
  case ARM::LDMIB_UPD:
  case ARM::tLDMIA:
  case ARM::tLDMIA_UPD:
  case ARM::tPUSH:
  case ARM::t2LDMIA_RET:
  case ARM::t2LDMIA:
  case ARM::t2LDMDB:
  case ARM::t2LDMIA_UPD:
  case ARM::t2LDMDB_UPD:
    LdmBypass = true;
    DefCycle = getLDMDefCycle(ItinData, DefMCID, DefClass, DefIdx, DefAlign);
    break;
  }

  if (DefCycle == -1)
    // We can't seem to determine the result latency of the def, assume it's 2.
    DefCycle = 2;

  int UseCycle = -1;
  switch (UseMCID.getOpcode()) {
  default:
    UseCycle = ItinData->getOperandCycle(UseClass, UseIdx);
    break;

  case ARM::VSTMDIA:
  case ARM::VSTMDIA_UPD:
  case ARM::VSTMDDB_UPD:
  case ARM::VSTMSIA:
  case ARM::VSTMSIA_UPD:
  case ARM::VSTMSDB_UPD:
    UseCycle = getVSTMUseCycle(ItinData, UseMCID, UseClass, UseIdx, UseAlign);
    break;

  case ARM::STMIA:
  case ARM::STMDA:
  case ARM::STMDB:
  case ARM::STMIB:
  case ARM::STMIA_UPD:
  case ARM::STMDA_UPD:
  case ARM::STMDB_UPD:
  case ARM::STMIB_UPD:
  case ARM::tSTMIA_UPD:
  case ARM::tPOP_RET:
  case ARM::tPOP:
  case ARM::t2STMIA:
  case ARM::t2STMDB:
  case ARM::t2STMIA_UPD:
  case ARM::t2STMDB_UPD:
    UseCycle = getSTMUseCycle(ItinData, UseMCID, UseClass, UseIdx, UseAlign);
    break;
  }

  if (UseCycle == -1)
    // Assume it's read in the first stage.
    UseCycle = 1;

  UseCycle = DefCycle - UseCycle + 1;
  if (UseCycle > 0) {
    if (LdmBypass) {
      // It's a variable_ops instruction so we can't use DefIdx here. Just use
      // first def operand.
      if (ItinData->hasPipelineForwarding(DefClass, DefMCID.getNumOperands()-1,
                                          UseClass, UseIdx))
        --UseCycle;
    } else if (ItinData->hasPipelineForwarding(DefClass, DefIdx,
                                               UseClass, UseIdx)) {
      --UseCycle;
    }
  }

  return UseCycle;
}

static const MachineInstr *getBundledDefMI(const TargetRegisterInfo *TRI,
                                           const MachineInstr *MI, unsigned Reg,
                                           unsigned &DefIdx, unsigned &Dist) {
  Dist = 0;

  MachineBasicBlock::const_iterator I = MI; ++I;
  MachineBasicBlock::const_instr_iterator II = std::prev(I.getInstrIterator());
  assert(II->isInsideBundle() && "Empty bundle?");

  int Idx = -1;
  while (II->isInsideBundle()) {
    Idx = II->findRegisterDefOperandIdx(Reg, false, true, TRI);
    if (Idx != -1)
      break;
    --II;
    ++Dist;
  }

  assert(Idx != -1 && "Cannot find bundled definition!");
  DefIdx = Idx;
  return &*II;
}

static const MachineInstr *getBundledUseMI(const TargetRegisterInfo *TRI,
                                           const MachineInstr &MI, unsigned Reg,
                                           unsigned &UseIdx, unsigned &Dist) {
  Dist = 0;

  MachineBasicBlock::const_instr_iterator II = ++MI.getIterator();
  assert(II->isInsideBundle() && "Empty bundle?");
  MachineBasicBlock::const_instr_iterator E = MI.getParent()->instr_end();

  // FIXME: This doesn't properly handle multiple uses.
  int Idx = -1;
  while (II != E && II->isInsideBundle()) {
    Idx = II->findRegisterUseOperandIdx(Reg, false, TRI);
    if (Idx != -1)
      break;
    if (II->getOpcode() != ARM::t2IT)
      ++Dist;
    ++II;
  }

  if (Idx == -1) {
    Dist = 0;
    return nullptr;
  }

  UseIdx = Idx;
  return &*II;
}

/// Return the number of cycles to add to (or subtract from) the static
/// itinerary based on the def opcode and alignment. The caller will ensure that
/// adjusted latency is at least one cycle.
static int adjustDefLatency(const ARMSubtarget &Subtarget,
                            const MachineInstr &DefMI,
                            const MCInstrDesc &DefMCID, unsigned DefAlign) {
  int Adjust = 0;
  if (Subtarget.isCortexA8() || Subtarget.isLikeA9() || Subtarget.isCortexA7()) {
    // FIXME: Shifter op hack: no shift (i.e. [r +/- r]) or [r + r << 2]
    // variants are one cycle cheaper.
    switch (DefMCID.getOpcode()) {
    default: break;
    case ARM::LDRrs:
    case ARM::LDRBrs: {
      unsigned ShOpVal = DefMI.getOperand(3).getImm();
      unsigned ShImm = ARM_AM::getAM2Offset(ShOpVal);
      if (ShImm == 0 ||
          (ShImm == 2 && ARM_AM::getAM2ShiftOpc(ShOpVal) == ARM_AM::lsl))
        --Adjust;
      break;
    }
    case ARM::t2LDRs:
    case ARM::t2LDRBs:
    case ARM::t2LDRHs:
    case ARM::t2LDRSHs: {
      // Thumb2 mode: lsl only.
      unsigned ShAmt = DefMI.getOperand(3).getImm();
      if (ShAmt == 0 || ShAmt == 2)
        --Adjust;
      break;
    }
    }
  } else if (Subtarget.isSwift()) {
    // FIXME: Properly handle all of the latency adjustments for address
    // writeback.
    switch (DefMCID.getOpcode()) {
    default: break;
    case ARM::LDRrs:
    case ARM::LDRBrs: {
      unsigned ShOpVal = DefMI.getOperand(3).getImm();
      bool isSub = ARM_AM::getAM2Op(ShOpVal) == ARM_AM::sub;
      unsigned ShImm = ARM_AM::getAM2Offset(ShOpVal);
      if (!isSub &&
          (ShImm == 0 ||
           ((ShImm == 1 || ShImm == 2 || ShImm == 3) &&
            ARM_AM::getAM2ShiftOpc(ShOpVal) == ARM_AM::lsl)))
        Adjust -= 2;
      else if (!isSub &&
               ShImm == 1 && ARM_AM::getAM2ShiftOpc(ShOpVal) == ARM_AM::lsr)
        --Adjust;
      break;
    }
    case ARM::t2LDRs:
    case ARM::t2LDRBs:
    case ARM::t2LDRHs:
    case ARM::t2LDRSHs: {
      // Thumb2 mode: lsl only.
      unsigned ShAmt = DefMI.getOperand(3).getImm();
      if (ShAmt == 0 || ShAmt == 1 || ShAmt == 2 || ShAmt == 3)
        Adjust -= 2;
      break;
    }
    }
  }

  if (DefAlign < 8 && Subtarget.checkVLDnAccessAlignment()) {
    switch (DefMCID.getOpcode()) {
    default: break;
    case ARM::VLD1q8:
    case ARM::VLD1q16:
    case ARM::VLD1q32:
    case ARM::VLD1q64:
    case ARM::VLD1q8wb_fixed:
    case ARM::VLD1q16wb_fixed:
    case ARM::VLD1q32wb_fixed:
    case ARM::VLD1q64wb_fixed:
    case ARM::VLD1q8wb_register:
    case ARM::VLD1q16wb_register:
    case ARM::VLD1q32wb_register:
    case ARM::VLD1q64wb_register:
    case ARM::VLD2d8:
    case ARM::VLD2d16:
    case ARM::VLD2d32:
    case ARM::VLD2q8:
    case ARM::VLD2q16:
    case ARM::VLD2q32:
    case ARM::VLD2d8wb_fixed:
    case ARM::VLD2d16wb_fixed:
    case ARM::VLD2d32wb_fixed:
    case ARM::VLD2q8wb_fixed:
    case ARM::VLD2q16wb_fixed:
    case ARM::VLD2q32wb_fixed:
    case ARM::VLD2d8wb_register:
    case ARM::VLD2d16wb_register:
    case ARM::VLD2d32wb_register:
    case ARM::VLD2q8wb_register:
    case ARM::VLD2q16wb_register:
    case ARM::VLD2q32wb_register:
    case ARM::VLD3d8:
    case ARM::VLD3d16:
    case ARM::VLD3d32:
    case ARM::VLD1d64T:
    case ARM::VLD3d8_UPD:
    case ARM::VLD3d16_UPD:
    case ARM::VLD3d32_UPD:
    case ARM::VLD1d64Twb_fixed:
    case ARM::VLD1d64Twb_register:
    case ARM::VLD3q8_UPD:
    case ARM::VLD3q16_UPD:
    case ARM::VLD3q32_UPD:
    case ARM::VLD4d8:
    case ARM::VLD4d16:
    case ARM::VLD4d32:
    case ARM::VLD1d64Q:
    case ARM::VLD4d8_UPD:
    case ARM::VLD4d16_UPD:
    case ARM::VLD4d32_UPD:
    case ARM::VLD1d64Qwb_fixed:
    case ARM::VLD1d64Qwb_register:
    case ARM::VLD4q8_UPD:
    case ARM::VLD4q16_UPD:
    case ARM::VLD4q32_UPD:
    case ARM::VLD1DUPq8:
    case ARM::VLD1DUPq16:
    case ARM::VLD1DUPq32:
    case ARM::VLD1DUPq8wb_fixed:
    case ARM::VLD1DUPq16wb_fixed:
    case ARM::VLD1DUPq32wb_fixed:
    case ARM::VLD1DUPq8wb_register:
    case ARM::VLD1DUPq16wb_register:
    case ARM::VLD1DUPq32wb_register:
    case ARM::VLD2DUPd8:
    case ARM::VLD2DUPd16:
    case ARM::VLD2DUPd32:
    case ARM::VLD2DUPd8wb_fixed:
    case ARM::VLD2DUPd16wb_fixed:
    case ARM::VLD2DUPd32wb_fixed:
    case ARM::VLD2DUPd8wb_register:
    case ARM::VLD2DUPd16wb_register:
    case ARM::VLD2DUPd32wb_register:
    case ARM::VLD4DUPd8:
    case ARM::VLD4DUPd16:
    case ARM::VLD4DUPd32:
    case ARM::VLD4DUPd8_UPD:
    case ARM::VLD4DUPd16_UPD:
    case ARM::VLD4DUPd32_UPD:
    case ARM::VLD1LNd8:
    case ARM::VLD1LNd16:
    case ARM::VLD1LNd32:
    case ARM::VLD1LNd8_UPD:
    case ARM::VLD1LNd16_UPD:
    case ARM::VLD1LNd32_UPD:
    case ARM::VLD2LNd8:
    case ARM::VLD2LNd16:
    case ARM::VLD2LNd32:
    case ARM::VLD2LNq16:
    case ARM::VLD2LNq32:
    case ARM::VLD2LNd8_UPD:
    case ARM::VLD2LNd16_UPD:
    case ARM::VLD2LNd32_UPD:
    case ARM::VLD2LNq16_UPD:
    case ARM::VLD2LNq32_UPD:
    case ARM::VLD4LNd8:
    case ARM::VLD4LNd16:
    case ARM::VLD4LNd32:
    case ARM::VLD4LNq16:
    case ARM::VLD4LNq32:
    case ARM::VLD4LNd8_UPD:
    case ARM::VLD4LNd16_UPD:
    case ARM::VLD4LNd32_UPD:
    case ARM::VLD4LNq16_UPD:
    case ARM::VLD4LNq32_UPD:
      // If the address is not 64-bit aligned, the latencies of these
      // instructions increases by one.
      ++Adjust;
      break;
    }
  }
  return Adjust;
}

int ARMBaseInstrInfo::getOperandLatency(const InstrItineraryData *ItinData,
                                        const MachineInstr &DefMI,
                                        unsigned DefIdx,
                                        const MachineInstr &UseMI,
                                        unsigned UseIdx) const {
  // No operand latency. The caller may fall back to getInstrLatency.
  if (!ItinData || ItinData->isEmpty())
    return -1;

  const MachineOperand &DefMO = DefMI.getOperand(DefIdx);
  unsigned Reg = DefMO.getReg();

  const MachineInstr *ResolvedDefMI = &DefMI;
  unsigned DefAdj = 0;
  if (DefMI.isBundle())
    ResolvedDefMI =
        getBundledDefMI(&getRegisterInfo(), &DefMI, Reg, DefIdx, DefAdj);
  if (ResolvedDefMI->isCopyLike() || ResolvedDefMI->isInsertSubreg() ||
      ResolvedDefMI->isRegSequence() || ResolvedDefMI->isImplicitDef()) {
    return 1;
  }

  const MachineInstr *ResolvedUseMI = &UseMI;
  unsigned UseAdj = 0;
  if (UseMI.isBundle()) {
    ResolvedUseMI =
        getBundledUseMI(&getRegisterInfo(), UseMI, Reg, UseIdx, UseAdj);
    if (!ResolvedUseMI)
      return -1;
  }

  return getOperandLatencyImpl(
      ItinData, *ResolvedDefMI, DefIdx, ResolvedDefMI->getDesc(), DefAdj, DefMO,
      Reg, *ResolvedUseMI, UseIdx, ResolvedUseMI->getDesc(), UseAdj);
}

int ARMBaseInstrInfo::getOperandLatencyImpl(
    const InstrItineraryData *ItinData, const MachineInstr &DefMI,
    unsigned DefIdx, const MCInstrDesc &DefMCID, unsigned DefAdj,
    const MachineOperand &DefMO, unsigned Reg, const MachineInstr &UseMI,
    unsigned UseIdx, const MCInstrDesc &UseMCID, unsigned UseAdj) const {
  if (Reg == ARM::CPSR) {
    if (DefMI.getOpcode() == ARM::FMSTAT) {
      // fpscr -> cpsr stalls over 20 cycles on A8 (and earlier?)
      return Subtarget.isLikeA9() ? 1 : 20;
    }

    // CPSR set and branch can be paired in the same cycle.
    if (UseMI.isBranch())
      return 0;

    // Otherwise it takes the instruction latency (generally one).
    unsigned Latency = getInstrLatency(ItinData, DefMI);

    // For Thumb2 and -Os, prefer scheduling CPSR setting instruction close to
    // its uses. Instructions which are otherwise scheduled between them may
    // incur a code size penalty (not able to use the CPSR setting 16-bit
    // instructions).
    if (Latency > 0 && Subtarget.isThumb2()) {
      const MachineFunction *MF = DefMI.getParent()->getParent();
      // FIXME: Use Function::optForSize().
      if (MF->getFunction().hasFnAttribute(Attribute::OptimizeForSize))
        --Latency;
    }
    return Latency;
  }

  if (DefMO.isImplicit() || UseMI.getOperand(UseIdx).isImplicit())
    return -1;

  unsigned DefAlign = DefMI.hasOneMemOperand()
                          ? (*DefMI.memoperands_begin())->getAlignment()
                          : 0;
  unsigned UseAlign = UseMI.hasOneMemOperand()
                          ? (*UseMI.memoperands_begin())->getAlignment()
                          : 0;

  // Get the itinerary's latency if possible, and handle variable_ops.
  int Latency = getOperandLatency(ItinData, DefMCID, DefIdx, DefAlign, UseMCID,
                                  UseIdx, UseAlign);
  // Unable to find operand latency. The caller may resort to getInstrLatency.
  if (Latency < 0)
    return Latency;

  // Adjust for IT block position.
  int Adj = DefAdj + UseAdj;

  // Adjust for dynamic def-side opcode variants not captured by the itinerary.
  Adj += adjustDefLatency(Subtarget, DefMI, DefMCID, DefAlign);
  if (Adj >= 0 || (int)Latency > -Adj) {
    return Latency + Adj;
  }
  // Return the itinerary latency, which may be zero but not less than zero.
  return Latency;
}

int
ARMBaseInstrInfo::getOperandLatency(const InstrItineraryData *ItinData,
                                    SDNode *DefNode, unsigned DefIdx,
                                    SDNode *UseNode, unsigned UseIdx) const {
  if (!DefNode->isMachineOpcode())
    return 1;

  const MCInstrDesc &DefMCID = get(DefNode->getMachineOpcode());

  if (isZeroCost(DefMCID.Opcode))
    return 0;

  if (!ItinData || ItinData->isEmpty())
    return DefMCID.mayLoad() ? 3 : 1;

  if (!UseNode->isMachineOpcode()) {
    int Latency = ItinData->getOperandCycle(DefMCID.getSchedClass(), DefIdx);
    int Adj = Subtarget.getPreISelOperandLatencyAdjustment();
    int Threshold = 1 + Adj;
    return Latency <= Threshold ? 1 : Latency - Adj;
  }

  const MCInstrDesc &UseMCID = get(UseNode->getMachineOpcode());
  const MachineSDNode *DefMN = dyn_cast<MachineSDNode>(DefNode);
  unsigned DefAlign = !DefMN->memoperands_empty()
    ? (*DefMN->memoperands_begin())->getAlignment() : 0;
  const MachineSDNode *UseMN = dyn_cast<MachineSDNode>(UseNode);
  unsigned UseAlign = !UseMN->memoperands_empty()
    ? (*UseMN->memoperands_begin())->getAlignment() : 0;
  int Latency = getOperandLatency(ItinData, DefMCID, DefIdx, DefAlign,
                                  UseMCID, UseIdx, UseAlign);

  if (Latency > 1 &&
      (Subtarget.isCortexA8() || Subtarget.isLikeA9() ||
       Subtarget.isCortexA7())) {
    // FIXME: Shifter op hack: no shift (i.e. [r +/- r]) or [r + r << 2]
    // variants are one cycle cheaper.
    switch (DefMCID.getOpcode()) {
    default: break;
    case ARM::LDRrs:
    case ARM::LDRBrs: {
      unsigned ShOpVal =
        cast<ConstantSDNode>(DefNode->getOperand(2))->getZExtValue();
      unsigned ShImm = ARM_AM::getAM2Offset(ShOpVal);
      if (ShImm == 0 ||
          (ShImm == 2 && ARM_AM::getAM2ShiftOpc(ShOpVal) == ARM_AM::lsl))
        --Latency;
      break;
    }
    case ARM::t2LDRs:
    case ARM::t2LDRBs:
    case ARM::t2LDRHs:
    case ARM::t2LDRSHs: {
      // Thumb2 mode: lsl only.
      unsigned ShAmt =
        cast<ConstantSDNode>(DefNode->getOperand(2))->getZExtValue();
      if (ShAmt == 0 || ShAmt == 2)
        --Latency;
      break;
    }
    }
  } else if (DefIdx == 0 && Latency > 2 && Subtarget.isSwift()) {
    // FIXME: Properly handle all of the latency adjustments for address
    // writeback.
    switch (DefMCID.getOpcode()) {
    default: break;
    case ARM::LDRrs:
    case ARM::LDRBrs: {
      unsigned ShOpVal =
        cast<ConstantSDNode>(DefNode->getOperand(2))->getZExtValue();
      unsigned ShImm = ARM_AM::getAM2Offset(ShOpVal);
      if (ShImm == 0 ||
          ((ShImm == 1 || ShImm == 2 || ShImm == 3) &&
           ARM_AM::getAM2ShiftOpc(ShOpVal) == ARM_AM::lsl))
        Latency -= 2;
      else if (ShImm == 1 && ARM_AM::getAM2ShiftOpc(ShOpVal) == ARM_AM::lsr)
        --Latency;
      break;
    }
    case ARM::t2LDRs:
    case ARM::t2LDRBs:
    case ARM::t2LDRHs:
    case ARM::t2LDRSHs:
      // Thumb2 mode: lsl 0-3 only.
      Latency -= 2;
      break;
    }
  }

  if (DefAlign < 8 && Subtarget.checkVLDnAccessAlignment())
    switch (DefMCID.getOpcode()) {
    default: break;
    case ARM::VLD1q8:
    case ARM::VLD1q16:
    case ARM::VLD1q32:
    case ARM::VLD1q64:
    case ARM::VLD1q8wb_register:
    case ARM::VLD1q16wb_register:
    case ARM::VLD1q32wb_register:
    case ARM::VLD1q64wb_register:
    case ARM::VLD1q8wb_fixed:
    case ARM::VLD1q16wb_fixed:
    case ARM::VLD1q32wb_fixed:
    case ARM::VLD1q64wb_fixed:
    case ARM::VLD2d8:
    case ARM::VLD2d16:
    case ARM::VLD2d32:
    case ARM::VLD2q8Pseudo:
    case ARM::VLD2q16Pseudo:
    case ARM::VLD2q32Pseudo:
    case ARM::VLD2d8wb_fixed:
    case ARM::VLD2d16wb_fixed:
    case ARM::VLD2d32wb_fixed:
    case ARM::VLD2q8PseudoWB_fixed:
    case ARM::VLD2q16PseudoWB_fixed:
    case ARM::VLD2q32PseudoWB_fixed:
    case ARM::VLD2d8wb_register:
    case ARM::VLD2d16wb_register:
    case ARM::VLD2d32wb_register:
    case ARM::VLD2q8PseudoWB_register:
    case ARM::VLD2q16PseudoWB_register:
    case ARM::VLD2q32PseudoWB_register:
    case ARM::VLD3d8Pseudo:
    case ARM::VLD3d16Pseudo:
    case ARM::VLD3d32Pseudo:
    case ARM::VLD1d8TPseudo:
    case ARM::VLD1d16TPseudo:
    case ARM::VLD1d32TPseudo:
    case ARM::VLD1d64TPseudo:
    case ARM::VLD1d64TPseudoWB_fixed:
    case ARM::VLD1d64TPseudoWB_register:
    case ARM::VLD3d8Pseudo_UPD:
    case ARM::VLD3d16Pseudo_UPD:
    case ARM::VLD3d32Pseudo_UPD:
    case ARM::VLD3q8Pseudo_UPD:
    case ARM::VLD3q16Pseudo_UPD:
    case ARM::VLD3q32Pseudo_UPD:
    case ARM::VLD3q8oddPseudo:
    case ARM::VLD3q16oddPseudo:
    case ARM::VLD3q32oddPseudo:
    case ARM::VLD3q8oddPseudo_UPD:
    case ARM::VLD3q16oddPseudo_UPD:
    case ARM::VLD3q32oddPseudo_UPD:
    case ARM::VLD4d8Pseudo:
    case ARM::VLD4d16Pseudo:
    case ARM::VLD4d32Pseudo:
    case ARM::VLD1d8QPseudo:
    case ARM::VLD1d16QPseudo:
    case ARM::VLD1d32QPseudo:
    case ARM::VLD1d64QPseudo:
    case ARM::VLD1d64QPseudoWB_fixed:
    case ARM::VLD1d64QPseudoWB_register:
    case ARM::VLD1q8HighQPseudo:
    case ARM::VLD1q8LowQPseudo_UPD:
    case ARM::VLD1q8HighTPseudo:
    case ARM::VLD1q8LowTPseudo_UPD:
    case ARM::VLD1q16HighQPseudo:
    case ARM::VLD1q16LowQPseudo_UPD:
    case ARM::VLD1q16HighTPseudo:
    case ARM::VLD1q16LowTPseudo_UPD:
    case ARM::VLD1q32HighQPseudo:
    case ARM::VLD1q32LowQPseudo_UPD:
    case ARM::VLD1q32HighTPseudo:
    case ARM::VLD1q32LowTPseudo_UPD:
    case ARM::VLD1q64HighQPseudo:
    case ARM::VLD1q64LowQPseudo_UPD:
    case ARM::VLD1q64HighTPseudo:
    case ARM::VLD1q64LowTPseudo_UPD:
    case ARM::VLD4d8Pseudo_UPD:
    case ARM::VLD4d16Pseudo_UPD:
    case ARM::VLD4d32Pseudo_UPD:
    case ARM::VLD4q8Pseudo_UPD:
    case ARM::VLD4q16Pseudo_UPD:
    case ARM::VLD4q32Pseudo_UPD:
    case ARM::VLD4q8oddPseudo:
    case ARM::VLD4q16oddPseudo:
    case ARM::VLD4q32oddPseudo:
    case ARM::VLD4q8oddPseudo_UPD:
    case ARM::VLD4q16oddPseudo_UPD:
    case ARM::VLD4q32oddPseudo_UPD:
    case ARM::VLD1DUPq8:
    case ARM::VLD1DUPq16:
    case ARM::VLD1DUPq32:
    case ARM::VLD1DUPq8wb_fixed:
    case ARM::VLD1DUPq16wb_fixed:
    case ARM::VLD1DUPq32wb_fixed:
    case ARM::VLD1DUPq8wb_register:
    case ARM::VLD1DUPq16wb_register:
    case ARM::VLD1DUPq32wb_register:
    case ARM::VLD2DUPd8:
    case ARM::VLD2DUPd16:
    case ARM::VLD2DUPd32:
    case ARM::VLD2DUPd8wb_fixed:
    case ARM::VLD2DUPd16wb_fixed:
    case ARM::VLD2DUPd32wb_fixed:
    case ARM::VLD2DUPd8wb_register:
    case ARM::VLD2DUPd16wb_register:
    case ARM::VLD2DUPd32wb_register:
    case ARM::VLD2DUPq8EvenPseudo:
    case ARM::VLD2DUPq8OddPseudo:
    case ARM::VLD2DUPq16EvenPseudo:
    case ARM::VLD2DUPq16OddPseudo:
    case ARM::VLD2DUPq32EvenPseudo:
    case ARM::VLD2DUPq32OddPseudo:
    case ARM::VLD3DUPq8EvenPseudo:
    case ARM::VLD3DUPq8OddPseudo:
    case ARM::VLD3DUPq16EvenPseudo:
    case ARM::VLD3DUPq16OddPseudo:
    case ARM::VLD3DUPq32EvenPseudo:
    case ARM::VLD3DUPq32OddPseudo:
    case ARM::VLD4DUPd8Pseudo:
    case ARM::VLD4DUPd16Pseudo:
    case ARM::VLD4DUPd32Pseudo:
    case ARM::VLD4DUPd8Pseudo_UPD:
    case ARM::VLD4DUPd16Pseudo_UPD:
    case ARM::VLD4DUPd32Pseudo_UPD:
    case ARM::VLD4DUPq8EvenPseudo:
    case ARM::VLD4DUPq8OddPseudo:
    case ARM::VLD4DUPq16EvenPseudo:
    case ARM::VLD4DUPq16OddPseudo:
    case ARM::VLD4DUPq32EvenPseudo:
    case ARM::VLD4DUPq32OddPseudo:
    case ARM::VLD1LNq8Pseudo:
    case ARM::VLD1LNq16Pseudo:
    case ARM::VLD1LNq32Pseudo:
    case ARM::VLD1LNq8Pseudo_UPD:
    case ARM::VLD1LNq16Pseudo_UPD:
    case ARM::VLD1LNq32Pseudo_UPD:
    case ARM::VLD2LNd8Pseudo:
    case ARM::VLD2LNd16Pseudo:
    case ARM::VLD2LNd32Pseudo:
    case ARM::VLD2LNq16Pseudo:
    case ARM::VLD2LNq32Pseudo:
    case ARM::VLD2LNd8Pseudo_UPD:
    case ARM::VLD2LNd16Pseudo_UPD:
    case ARM::VLD2LNd32Pseudo_UPD:
    case ARM::VLD2LNq16Pseudo_UPD:
    case ARM::VLD2LNq32Pseudo_UPD:
    case ARM::VLD4LNd8Pseudo:
    case ARM::VLD4LNd16Pseudo:
    case ARM::VLD4LNd32Pseudo:
    case ARM::VLD4LNq16Pseudo:
    case ARM::VLD4LNq32Pseudo:
    case ARM::VLD4LNd8Pseudo_UPD:
    case ARM::VLD4LNd16Pseudo_UPD:
    case ARM::VLD4LNd32Pseudo_UPD:
    case ARM::VLD4LNq16Pseudo_UPD:
    case ARM::VLD4LNq32Pseudo_UPD:
      // If the address is not 64-bit aligned, the latencies of these
      // instructions increases by one.
      ++Latency;
      break;
    }

  return Latency;
}

unsigned ARMBaseInstrInfo::getPredicationCost(const MachineInstr &MI) const {
  if (MI.isCopyLike() || MI.isInsertSubreg() || MI.isRegSequence() ||
      MI.isImplicitDef())
    return 0;

  if (MI.isBundle())
    return 0;

  const MCInstrDesc &MCID = MI.getDesc();

  if (MCID.isCall() || (MCID.hasImplicitDefOfPhysReg(ARM::CPSR) &&
                        !Subtarget.cheapPredicableCPSRDef())) {
    // When predicated, CPSR is an additional source operand for CPSR updating
    // instructions, this apparently increases their latencies.
    return 1;
  }
  return 0;
}

unsigned ARMBaseInstrInfo::getInstrLatency(const InstrItineraryData *ItinData,
                                           const MachineInstr &MI,
                                           unsigned *PredCost) const {
  if (MI.isCopyLike() || MI.isInsertSubreg() || MI.isRegSequence() ||
      MI.isImplicitDef())
    return 1;

  // An instruction scheduler typically runs on unbundled instructions, however
  // other passes may query the latency of a bundled instruction.
  if (MI.isBundle()) {
    unsigned Latency = 0;
    MachineBasicBlock::const_instr_iterator I = MI.getIterator();
    MachineBasicBlock::const_instr_iterator E = MI.getParent()->instr_end();
    while (++I != E && I->isInsideBundle()) {
      if (I->getOpcode() != ARM::t2IT)
        Latency += getInstrLatency(ItinData, *I, PredCost);
    }
    return Latency;
  }

  const MCInstrDesc &MCID = MI.getDesc();
  if (PredCost && (MCID.isCall() || (MCID.hasImplicitDefOfPhysReg(ARM::CPSR) &&
                                     !Subtarget.cheapPredicableCPSRDef()))) {
    // When predicated, CPSR is an additional source operand for CPSR updating
    // instructions, this apparently increases their latencies.
    *PredCost = 1;
  }
  // Be sure to call getStageLatency for an empty itinerary in case it has a
  // valid MinLatency property.
  if (!ItinData)
    return MI.mayLoad() ? 3 : 1;

  unsigned Class = MCID.getSchedClass();

  // For instructions with variable uops, use uops as latency.
  if (!ItinData->isEmpty() && ItinData->getNumMicroOps(Class) < 0)
    return getNumMicroOps(ItinData, MI);

  // For the common case, fall back on the itinerary's latency.
  unsigned Latency = ItinData->getStageLatency(Class);

  // Adjust for dynamic def-side opcode variants not captured by the itinerary.
  unsigned DefAlign =
      MI.hasOneMemOperand() ? (*MI.memoperands_begin())->getAlignment() : 0;
  int Adj = adjustDefLatency(Subtarget, MI, MCID, DefAlign);
  if (Adj >= 0 || (int)Latency > -Adj) {
    return Latency + Adj;
  }
  return Latency;
}

int ARMBaseInstrInfo::getInstrLatency(const InstrItineraryData *ItinData,
                                      SDNode *Node) const {
  if (!Node->isMachineOpcode())
    return 1;

  if (!ItinData || ItinData->isEmpty())
    return 1;

  unsigned Opcode = Node->getMachineOpcode();
  switch (Opcode) {
  default:
    return ItinData->getStageLatency(get(Opcode).getSchedClass());
  case ARM::VLDMQIA:
  case ARM::VSTMQIA:
    return 2;
  }
}

bool ARMBaseInstrInfo::hasHighOperandLatency(const TargetSchedModel &SchedModel,
                                             const MachineRegisterInfo *MRI,
                                             const MachineInstr &DefMI,
                                             unsigned DefIdx,
                                             const MachineInstr &UseMI,
                                             unsigned UseIdx) const {
  unsigned DDomain = DefMI.getDesc().TSFlags & ARMII::DomainMask;
  unsigned UDomain = UseMI.getDesc().TSFlags & ARMII::DomainMask;
  if (Subtarget.nonpipelinedVFP() &&
      (DDomain == ARMII::DomainVFP || UDomain == ARMII::DomainVFP))
    return true;

  // Hoist VFP / NEON instructions with 4 or higher latency.
  unsigned Latency =
      SchedModel.computeOperandLatency(&DefMI, DefIdx, &UseMI, UseIdx);
  if (Latency <= 3)
    return false;
  return DDomain == ARMII::DomainVFP || DDomain == ARMII::DomainNEON ||
         UDomain == ARMII::DomainVFP || UDomain == ARMII::DomainNEON;
}

bool ARMBaseInstrInfo::hasLowDefLatency(const TargetSchedModel &SchedModel,
                                        const MachineInstr &DefMI,
                                        unsigned DefIdx) const {
  const InstrItineraryData *ItinData = SchedModel.getInstrItineraries();
  if (!ItinData || ItinData->isEmpty())
    return false;

  unsigned DDomain = DefMI.getDesc().TSFlags & ARMII::DomainMask;
  if (DDomain == ARMII::DomainGeneral) {
    unsigned DefClass = DefMI.getDesc().getSchedClass();
    int DefCycle = ItinData->getOperandCycle(DefClass, DefIdx);
    return (DefCycle != -1 && DefCycle <= 2);
  }
  return false;
}

bool ARMBaseInstrInfo::verifyInstruction(const MachineInstr &MI,
                                         StringRef &ErrInfo) const {
  if (convertAddSubFlagsOpcode(MI.getOpcode())) {
    ErrInfo = "Pseudo flag setting opcodes only exist in Selection DAG";
    return false;
  }
  return true;
}

// LoadStackGuard has so far only been implemented for MachO. Different code
// sequence is needed for other targets.
void ARMBaseInstrInfo::expandLoadStackGuardBase(MachineBasicBlock::iterator MI,
                                                unsigned LoadImmOpc,
                                                unsigned LoadOpc) const {
  assert(!Subtarget.isROPI() && !Subtarget.isRWPI() &&
         "ROPI/RWPI not currently supported with stack guard");

  MachineBasicBlock &MBB = *MI->getParent();
  DebugLoc DL = MI->getDebugLoc();
  unsigned Reg = MI->getOperand(0).getReg();
  const GlobalValue *GV =
      cast<GlobalValue>((*MI->memoperands_begin())->getValue());
  MachineInstrBuilder MIB;

  BuildMI(MBB, MI, DL, get(LoadImmOpc), Reg)
      .addGlobalAddress(GV, 0, ARMII::MO_NONLAZY);

  if (Subtarget.isGVIndirectSymbol(GV)) {
    MIB = BuildMI(MBB, MI, DL, get(LoadOpc), Reg);
    MIB.addReg(Reg, RegState::Kill).addImm(0);
    auto Flags = MachineMemOperand::MOLoad |
                 MachineMemOperand::MODereferenceable |
                 MachineMemOperand::MOInvariant;
    MachineMemOperand *MMO = MBB.getParent()->getMachineMemOperand(
        MachinePointerInfo::getGOT(*MBB.getParent()), Flags, 4, 4);
    MIB.addMemOperand(MMO).add(predOps(ARMCC::AL));
  }

  MIB = BuildMI(MBB, MI, DL, get(LoadOpc), Reg);
  MIB.addReg(Reg, RegState::Kill)
      .addImm(0)
      .cloneMemRefs(*MI)
      .add(predOps(ARMCC::AL));
}

bool
ARMBaseInstrInfo::isFpMLxInstruction(unsigned Opcode, unsigned &MulOpc,
                                     unsigned &AddSubOpc,
                                     bool &NegAcc, bool &HasLane) const {
  DenseMap<unsigned, unsigned>::const_iterator I = MLxEntryMap.find(Opcode);
  if (I == MLxEntryMap.end())
    return false;

  const ARM_MLxEntry &Entry = ARM_MLxTable[I->second];
  MulOpc = Entry.MulOpc;
  AddSubOpc = Entry.AddSubOpc;
  NegAcc = Entry.NegAcc;
  HasLane = Entry.HasLane;
  return true;
}

//===----------------------------------------------------------------------===//
// Execution domains.
//===----------------------------------------------------------------------===//
//
// Some instructions go down the NEON pipeline, some go down the VFP pipeline,
// and some can go down both.  The vmov instructions go down the VFP pipeline,
// but they can be changed to vorr equivalents that are executed by the NEON
// pipeline.
//
// We use the following execution domain numbering:
//
enum ARMExeDomain {
  ExeGeneric = 0,
  ExeVFP = 1,
  ExeNEON = 2
};

//
// Also see ARMInstrFormats.td and Domain* enums in ARMBaseInfo.h
//
std::pair<uint16_t, uint16_t>
ARMBaseInstrInfo::getExecutionDomain(const MachineInstr &MI) const {
  // If we don't have access to NEON instructions then we won't be able
  // to swizzle anything to the NEON domain. Check to make sure.
  if (Subtarget.hasNEON()) {
    // VMOVD, VMOVRS and VMOVSR are VFP instructions, but can be changed to NEON
    // if they are not predicated.
    if (MI.getOpcode() == ARM::VMOVD && !isPredicated(MI))
      return std::make_pair(ExeVFP, (1 << ExeVFP) | (1 << ExeNEON));

    // CortexA9 is particularly picky about mixing the two and wants these
    // converted.
    if (Subtarget.useNEONForFPMovs() && !isPredicated(MI) &&
        (MI.getOpcode() == ARM::VMOVRS || MI.getOpcode() == ARM::VMOVSR ||
         MI.getOpcode() == ARM::VMOVS))
      return std::make_pair(ExeVFP, (1 << ExeVFP) | (1 << ExeNEON));
  }
  // No other instructions can be swizzled, so just determine their domain.
  unsigned Domain = MI.getDesc().TSFlags & ARMII::DomainMask;

  if (Domain & ARMII::DomainNEON)
    return std::make_pair(ExeNEON, 0);

  // Certain instructions can go either way on Cortex-A8.
  // Treat them as NEON instructions.
  if ((Domain & ARMII::DomainNEONA8) && Subtarget.isCortexA8())
    return std::make_pair(ExeNEON, 0);

  if (Domain & ARMII::DomainVFP)
    return std::make_pair(ExeVFP, 0);

  return std::make_pair(ExeGeneric, 0);
}

static unsigned getCorrespondingDRegAndLane(const TargetRegisterInfo *TRI,
                                            unsigned SReg, unsigned &Lane) {
  unsigned DReg = TRI->getMatchingSuperReg(SReg, ARM::ssub_0, &ARM::DPRRegClass);
  Lane = 0;

  if (DReg != ARM::NoRegister)
   return DReg;

  Lane = 1;
  DReg = TRI->getMatchingSuperReg(SReg, ARM::ssub_1, &ARM::DPRRegClass);

  assert(DReg && "S-register with no D super-register?");
  return DReg;
}

/// getImplicitSPRUseForDPRUse - Given a use of a DPR register and lane,
/// set ImplicitSReg to a register number that must be marked as implicit-use or
/// zero if no register needs to be defined as implicit-use.
///
/// If the function cannot determine if an SPR should be marked implicit use or
/// not, it returns false.
///
/// This function handles cases where an instruction is being modified from taking
/// an SPR to a DPR[Lane]. A use of the DPR is being added, which may conflict
/// with an earlier def of an SPR corresponding to DPR[Lane^1] (i.e. the other
/// lane of the DPR).
///
/// If the other SPR is defined, an implicit-use of it should be added. Else,
/// (including the case where the DPR itself is defined), it should not.
///
static bool getImplicitSPRUseForDPRUse(const TargetRegisterInfo *TRI,
                                       MachineInstr &MI, unsigned DReg,
                                       unsigned Lane, unsigned &ImplicitSReg) {
  // If the DPR is defined or used already, the other SPR lane will be chained
  // correctly, so there is nothing to be done.
  if (MI.definesRegister(DReg, TRI) || MI.readsRegister(DReg, TRI)) {
    ImplicitSReg = 0;
    return true;
  }

  // Otherwise we need to go searching to see if the SPR is set explicitly.
  ImplicitSReg = TRI->getSubReg(DReg,
                                (Lane & 1) ? ARM::ssub_0 : ARM::ssub_1);
  MachineBasicBlock::LivenessQueryResult LQR =
      MI.getParent()->computeRegisterLiveness(TRI, ImplicitSReg, MI);

  if (LQR == MachineBasicBlock::LQR_Live)
    return true;
  else if (LQR == MachineBasicBlock::LQR_Unknown)
    return false;

  // If the register is known not to be live, there is no need to add an
  // implicit-use.
  ImplicitSReg = 0;
  return true;
}

void ARMBaseInstrInfo::setExecutionDomain(MachineInstr &MI,
                                          unsigned Domain) const {
  unsigned DstReg, SrcReg, DReg;
  unsigned Lane;
  MachineInstrBuilder MIB(*MI.getParent()->getParent(), MI);
  const TargetRegisterInfo *TRI = &getRegisterInfo();
  switch (MI.getOpcode()) {
  default:
    llvm_unreachable("cannot handle opcode!");
    break;
  case ARM::VMOVD:
    if (Domain != ExeNEON)
      break;

    // Zap the predicate operands.
    assert(!isPredicated(MI) && "Cannot predicate a VORRd");

    // Make sure we've got NEON instructions.
    assert(Subtarget.hasNEON() && "VORRd requires NEON");

    // Source instruction is %DDst = VMOVD %DSrc, 14, %noreg (; implicits)
    DstReg = MI.getOperand(0).getReg();
    SrcReg = MI.getOperand(1).getReg();

    for (unsigned i = MI.getDesc().getNumOperands(); i; --i)
      MI.RemoveOperand(i - 1);

    // Change to a %DDst = VORRd %DSrc, %DSrc, 14, %noreg (; implicits)
    MI.setDesc(get(ARM::VORRd));
    MIB.addReg(DstReg, RegState::Define)
        .addReg(SrcReg)
        .addReg(SrcReg)
        .add(predOps(ARMCC::AL));
    break;
  case ARM::VMOVRS:
    if (Domain != ExeNEON)
      break;
    assert(!isPredicated(MI) && "Cannot predicate a VGETLN");

    // Source instruction is %RDst = VMOVRS %SSrc, 14, %noreg (; implicits)
    DstReg = MI.getOperand(0).getReg();
    SrcReg = MI.getOperand(1).getReg();

    for (unsigned i = MI.getDesc().getNumOperands(); i; --i)
      MI.RemoveOperand(i - 1);

    DReg = getCorrespondingDRegAndLane(TRI, SrcReg, Lane);

    // Convert to %RDst = VGETLNi32 %DSrc, Lane, 14, %noreg (; imps)
    // Note that DSrc has been widened and the other lane may be undef, which
    // contaminates the entire register.
    MI.setDesc(get(ARM::VGETLNi32));
    MIB.addReg(DstReg, RegState::Define)
        .addReg(DReg, RegState::Undef)
        .addImm(Lane)
        .add(predOps(ARMCC::AL));

    // The old source should be an implicit use, otherwise we might think it
    // was dead before here.
    MIB.addReg(SrcReg, RegState::Implicit);
    break;
  case ARM::VMOVSR: {
    if (Domain != ExeNEON)
      break;
    assert(!isPredicated(MI) && "Cannot predicate a VSETLN");

    // Source instruction is %SDst = VMOVSR %RSrc, 14, %noreg (; implicits)
    DstReg = MI.getOperand(0).getReg();
    SrcReg = MI.getOperand(1).getReg();

    DReg = getCorrespondingDRegAndLane(TRI, DstReg, Lane);

    unsigned ImplicitSReg;
    if (!getImplicitSPRUseForDPRUse(TRI, MI, DReg, Lane, ImplicitSReg))
      break;

    for (unsigned i = MI.getDesc().getNumOperands(); i; --i)
      MI.RemoveOperand(i - 1);

    // Convert to %DDst = VSETLNi32 %DDst, %RSrc, Lane, 14, %noreg (; imps)
    // Again DDst may be undefined at the beginning of this instruction.
    MI.setDesc(get(ARM::VSETLNi32));
    MIB.addReg(DReg, RegState::Define)
        .addReg(DReg, getUndefRegState(!MI.readsRegister(DReg, TRI)))
        .addReg(SrcReg)
        .addImm(Lane)
        .add(predOps(ARMCC::AL));

    // The narrower destination must be marked as set to keep previous chains
    // in place.
    MIB.addReg(DstReg, RegState::Define | RegState::Implicit);
    if (ImplicitSReg != 0)
      MIB.addReg(ImplicitSReg, RegState::Implicit);
    break;
    }
    case ARM::VMOVS: {
      if (Domain != ExeNEON)
        break;

      // Source instruction is %SDst = VMOVS %SSrc, 14, %noreg (; implicits)
      DstReg = MI.getOperand(0).getReg();
      SrcReg = MI.getOperand(1).getReg();

      unsigned DstLane = 0, SrcLane = 0, DDst, DSrc;
      DDst = getCorrespondingDRegAndLane(TRI, DstReg, DstLane);
      DSrc = getCorrespondingDRegAndLane(TRI, SrcReg, SrcLane);

      unsigned ImplicitSReg;
      if (!getImplicitSPRUseForDPRUse(TRI, MI, DSrc, SrcLane, ImplicitSReg))
        break;

      for (unsigned i = MI.getDesc().getNumOperands(); i; --i)
        MI.RemoveOperand(i - 1);

      if (DSrc == DDst) {
        // Destination can be:
        //     %DDst = VDUPLN32d %DDst, Lane, 14, %noreg (; implicits)
        MI.setDesc(get(ARM::VDUPLN32d));
        MIB.addReg(DDst, RegState::Define)
            .addReg(DDst, getUndefRegState(!MI.readsRegister(DDst, TRI)))
            .addImm(SrcLane)
            .add(predOps(ARMCC::AL));

        // Neither the source or the destination are naturally represented any
        // more, so add them in manually.
        MIB.addReg(DstReg, RegState::Implicit | RegState::Define);
        MIB.addReg(SrcReg, RegState::Implicit);
        if (ImplicitSReg != 0)
          MIB.addReg(ImplicitSReg, RegState::Implicit);
        break;
      }

      // In general there's no single instruction that can perform an S <-> S
      // move in NEON space, but a pair of VEXT instructions *can* do the
      // job. It turns out that the VEXTs needed will only use DSrc once, with
      // the position based purely on the combination of lane-0 and lane-1
      // involved. For example
      //     vmov s0, s2 -> vext.32 d0, d0, d1, #1  vext.32 d0, d0, d0, #1
      //     vmov s1, s3 -> vext.32 d0, d1, d0, #1  vext.32 d0, d0, d0, #1
      //     vmov s0, s3 -> vext.32 d0, d0, d0, #1  vext.32 d0, d1, d0, #1
      //     vmov s1, s2 -> vext.32 d0, d0, d0, #1  vext.32 d0, d0, d1, #1
      //
      // Pattern of the MachineInstrs is:
      //     %DDst = VEXTd32 %DSrc1, %DSrc2, Lane, 14, %noreg (;implicits)
      MachineInstrBuilder NewMIB;
      NewMIB = BuildMI(*MI.getParent(), MI, MI.getDebugLoc(), get(ARM::VEXTd32),
                       DDst);

      // On the first instruction, both DSrc and DDst may be undef if present.
      // Specifically when the original instruction didn't have them as an
      // <imp-use>.
      unsigned CurReg = SrcLane == 1 && DstLane == 1 ? DSrc : DDst;
      bool CurUndef = !MI.readsRegister(CurReg, TRI);
      NewMIB.addReg(CurReg, getUndefRegState(CurUndef));

      CurReg = SrcLane == 0 && DstLane == 0 ? DSrc : DDst;
      CurUndef = !MI.readsRegister(CurReg, TRI);
      NewMIB.addReg(CurReg, getUndefRegState(CurUndef))
            .addImm(1)
            .add(predOps(ARMCC::AL));

      if (SrcLane == DstLane)
        NewMIB.addReg(SrcReg, RegState::Implicit);

      MI.setDesc(get(ARM::VEXTd32));
      MIB.addReg(DDst, RegState::Define);

      // On the second instruction, DDst has definitely been defined above, so
      // it is not undef. DSrc, if present, can be undef as above.
      CurReg = SrcLane == 1 && DstLane == 0 ? DSrc : DDst;
      CurUndef = CurReg == DSrc && !MI.readsRegister(CurReg, TRI);
      MIB.addReg(CurReg, getUndefRegState(CurUndef));

      CurReg = SrcLane == 0 && DstLane == 1 ? DSrc : DDst;
      CurUndef = CurReg == DSrc && !MI.readsRegister(CurReg, TRI);
      MIB.addReg(CurReg, getUndefRegState(CurUndef))
         .addImm(1)
         .add(predOps(ARMCC::AL));

      if (SrcLane != DstLane)
        MIB.addReg(SrcReg, RegState::Implicit);

      // As before, the original destination is no longer represented, add it
      // implicitly.
      MIB.addReg(DstReg, RegState::Define | RegState::Implicit);
      if (ImplicitSReg != 0)
        MIB.addReg(ImplicitSReg, RegState::Implicit);
      break;
    }
  }
}

//===----------------------------------------------------------------------===//
// Partial register updates
//===----------------------------------------------------------------------===//
//
// Swift renames NEON registers with 64-bit granularity.  That means any
// instruction writing an S-reg implicitly reads the containing D-reg.  The
// problem is mostly avoided by translating f32 operations to v2f32 operations
// on D-registers, but f32 loads are still a problem.
//
// These instructions can load an f32 into a NEON register:
//
// VLDRS - Only writes S, partial D update.
// VLD1LNd32 - Writes all D-regs, explicit partial D update, 2 uops.
// VLD1DUPd32 - Writes all D-regs, no partial reg update, 2 uops.
//
// FCONSTD can be used as a dependency-breaking instruction.
unsigned ARMBaseInstrInfo::getPartialRegUpdateClearance(
    const MachineInstr &MI, unsigned OpNum,
    const TargetRegisterInfo *TRI) const {
  auto PartialUpdateClearance = Subtarget.getPartialUpdateClearance();
  if (!PartialUpdateClearance)
    return 0;

  assert(TRI && "Need TRI instance");

  const MachineOperand &MO = MI.getOperand(OpNum);
  if (MO.readsReg())
    return 0;
  unsigned Reg = MO.getReg();
  int UseOp = -1;

  switch (MI.getOpcode()) {
  // Normal instructions writing only an S-register.
  case ARM::VLDRS:
  case ARM::FCONSTS:
  case ARM::VMOVSR:
  case ARM::VMOVv8i8:
  case ARM::VMOVv4i16:
  case ARM::VMOVv2i32:
  case ARM::VMOVv2f32:
  case ARM::VMOVv1i64:
    UseOp = MI.findRegisterUseOperandIdx(Reg, false, TRI);
    break;

    // Explicitly reads the dependency.
  case ARM::VLD1LNd32:
    UseOp = 3;
    break;
  default:
    return 0;
  }

  // If this instruction actually reads a value from Reg, there is no unwanted
  // dependency.
  if (UseOp != -1 && MI.getOperand(UseOp).readsReg())
    return 0;

  // We must be able to clobber the whole D-reg.
  if (TargetRegisterInfo::isVirtualRegister(Reg)) {
    // Virtual register must be a def undef foo:ssub_0 operand.
    if (!MO.getSubReg() || MI.readsVirtualRegister(Reg))
      return 0;
  } else if (ARM::SPRRegClass.contains(Reg)) {
    // Physical register: MI must define the full D-reg.
    unsigned DReg = TRI->getMatchingSuperReg(Reg, ARM::ssub_0,
                                             &ARM::DPRRegClass);
    if (!DReg || !MI.definesRegister(DReg, TRI))
      return 0;
  }

  // MI has an unwanted D-register dependency.
  // Avoid defs in the previous N instructrions.
  return PartialUpdateClearance;
}

// Break a partial register dependency after getPartialRegUpdateClearance
// returned non-zero.
void ARMBaseInstrInfo::breakPartialRegDependency(
    MachineInstr &MI, unsigned OpNum, const TargetRegisterInfo *TRI) const {
  assert(OpNum < MI.getDesc().getNumDefs() && "OpNum is not a def");
  assert(TRI && "Need TRI instance");

  const MachineOperand &MO = MI.getOperand(OpNum);
  unsigned Reg = MO.getReg();
  assert(TargetRegisterInfo::isPhysicalRegister(Reg) &&
         "Can't break virtual register dependencies.");
  unsigned DReg = Reg;

  // If MI defines an S-reg, find the corresponding D super-register.
  if (ARM::SPRRegClass.contains(Reg)) {
    DReg = ARM::D0 + (Reg - ARM::S0) / 2;
    assert(TRI->isSuperRegister(Reg, DReg) && "Register enums broken");
  }

  assert(ARM::DPRRegClass.contains(DReg) && "Can only break D-reg deps");
  assert(MI.definesRegister(DReg, TRI) && "MI doesn't clobber full D-reg");

  // FIXME: In some cases, VLDRS can be changed to a VLD1DUPd32 which defines
  // the full D-register by loading the same value to both lanes.  The
  // instruction is micro-coded with 2 uops, so don't do this until we can
  // properly schedule micro-coded instructions.  The dispatcher stalls cause
  // too big regressions.

  // Insert the dependency-breaking FCONSTD before MI.
  // 96 is the encoding of 0.5, but the actual value doesn't matter here.
  BuildMI(*MI.getParent(), MI, MI.getDebugLoc(), get(ARM::FCONSTD), DReg)
      .addImm(96)
      .add(predOps(ARMCC::AL));
  MI.addRegisterKilled(DReg, TRI, true);
}

bool ARMBaseInstrInfo::hasNOP() const {
  return Subtarget.getFeatureBits()[ARM::HasV6KOps];
}

bool ARMBaseInstrInfo::isSwiftFastImmShift(const MachineInstr *MI) const {
  if (MI->getNumOperands() < 4)
    return true;
  unsigned ShOpVal = MI->getOperand(3).getImm();
  unsigned ShImm = ARM_AM::getSORegOffset(ShOpVal);
  // Swift supports faster shifts for: lsl 2, lsl 1, and lsr 1.
  if ((ShImm == 1 && ARM_AM::getSORegShOp(ShOpVal) == ARM_AM::lsr) ||
      ((ShImm == 1 || ShImm == 2) &&
       ARM_AM::getSORegShOp(ShOpVal) == ARM_AM::lsl))
    return true;

  return false;
}

bool ARMBaseInstrInfo::getRegSequenceLikeInputs(
    const MachineInstr &MI, unsigned DefIdx,
    SmallVectorImpl<RegSubRegPairAndIdx> &InputRegs) const {
  assert(DefIdx < MI.getDesc().getNumDefs() && "Invalid definition index");
  assert(MI.isRegSequenceLike() && "Invalid kind of instruction");

  switch (MI.getOpcode()) {
  case ARM::VMOVDRR:
    // dX = VMOVDRR rY, rZ
    // is the same as:
    // dX = REG_SEQUENCE rY, ssub_0, rZ, ssub_1
    // Populate the InputRegs accordingly.
    // rY
    const MachineOperand *MOReg = &MI.getOperand(1);
    if (!MOReg->isUndef())
      InputRegs.push_back(RegSubRegPairAndIdx(MOReg->getReg(),
                                              MOReg->getSubReg(), ARM::ssub_0));
    // rZ
    MOReg = &MI.getOperand(2);
    if (!MOReg->isUndef())
      InputRegs.push_back(RegSubRegPairAndIdx(MOReg->getReg(),
                                              MOReg->getSubReg(), ARM::ssub_1));
    return true;
  }
  llvm_unreachable("Target dependent opcode missing");
}

bool ARMBaseInstrInfo::getExtractSubregLikeInputs(
    const MachineInstr &MI, unsigned DefIdx,
    RegSubRegPairAndIdx &InputReg) const {
  assert(DefIdx < MI.getDesc().getNumDefs() && "Invalid definition index");
  assert(MI.isExtractSubregLike() && "Invalid kind of instruction");

  switch (MI.getOpcode()) {
  case ARM::VMOVRRD:
    // rX, rY = VMOVRRD dZ
    // is the same as:
    // rX = EXTRACT_SUBREG dZ, ssub_0
    // rY = EXTRACT_SUBREG dZ, ssub_1
    const MachineOperand &MOReg = MI.getOperand(2);
    if (MOReg.isUndef())
      return false;
    InputReg.Reg = MOReg.getReg();
    InputReg.SubReg = MOReg.getSubReg();
    InputReg.SubIdx = DefIdx == 0 ? ARM::ssub_0 : ARM::ssub_1;
    return true;
  }
  llvm_unreachable("Target dependent opcode missing");
}

bool ARMBaseInstrInfo::getInsertSubregLikeInputs(
    const MachineInstr &MI, unsigned DefIdx, RegSubRegPair &BaseReg,
    RegSubRegPairAndIdx &InsertedReg) const {
  assert(DefIdx < MI.getDesc().getNumDefs() && "Invalid definition index");
  assert(MI.isInsertSubregLike() && "Invalid kind of instruction");

  switch (MI.getOpcode()) {
  case ARM::VSETLNi32:
    // dX = VSETLNi32 dY, rZ, imm
    const MachineOperand &MOBaseReg = MI.getOperand(1);
    const MachineOperand &MOInsertedReg = MI.getOperand(2);
    if (MOInsertedReg.isUndef())
      return false;
    const MachineOperand &MOIndex = MI.getOperand(3);
    BaseReg.Reg = MOBaseReg.getReg();
    BaseReg.SubReg = MOBaseReg.getSubReg();

    InsertedReg.Reg = MOInsertedReg.getReg();
    InsertedReg.SubReg = MOInsertedReg.getSubReg();
    InsertedReg.SubIdx = MOIndex.getImm() == 0 ? ARM::ssub_0 : ARM::ssub_1;
    return true;
  }
  llvm_unreachable("Target dependent opcode missing");
}

std::pair<unsigned, unsigned>
ARMBaseInstrInfo::decomposeMachineOperandsTargetFlags(unsigned TF) const {
  const unsigned Mask = ARMII::MO_OPTION_MASK;
  return std::make_pair(TF & Mask, TF & ~Mask);
}

ArrayRef<std::pair<unsigned, const char *>>
ARMBaseInstrInfo::getSerializableDirectMachineOperandTargetFlags() const {
  using namespace ARMII;

  static const std::pair<unsigned, const char *> TargetFlags[] = {
      {MO_LO16, "arm-lo16"}, {MO_HI16, "arm-hi16"}};
  return makeArrayRef(TargetFlags);
}

ArrayRef<std::pair<unsigned, const char *>>
ARMBaseInstrInfo::getSerializableBitmaskMachineOperandTargetFlags() const {
  using namespace ARMII;

  static const std::pair<unsigned, const char *> TargetFlags[] = {
      {MO_COFFSTUB, "arm-coffstub"},
      {MO_GOT, "arm-got"},
      {MO_SBREL, "arm-sbrel"},
      {MO_DLLIMPORT, "arm-dllimport"},
      {MO_SECREL, "arm-secrel"},
      {MO_NONLAZY, "arm-nonlazy"}};
  return makeArrayRef(TargetFlags);
}
