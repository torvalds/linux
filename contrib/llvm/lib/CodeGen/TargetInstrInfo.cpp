//===-- TargetInstrInfo.cpp - Target Instruction Information --------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file implements the TargetInstrInfo class.
//
//===----------------------------------------------------------------------===//

#include "llvm/CodeGen/TargetInstrInfo.h"
#include "llvm/CodeGen/MachineFrameInfo.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/CodeGen/MachineMemOperand.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/CodeGen/PseudoSourceValue.h"
#include "llvm/CodeGen/ScoreboardHazardRecognizer.h"
#include "llvm/CodeGen/StackMaps.h"
#include "llvm/CodeGen/TargetFrameLowering.h"
#include "llvm/CodeGen/TargetLowering.h"
#include "llvm/CodeGen/TargetRegisterInfo.h"
#include "llvm/CodeGen/TargetSchedule.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/MC/MCAsmInfo.h"
#include "llvm/MC/MCInstrItineraries.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Target/TargetMachine.h"
#include <cctype>

using namespace llvm;

static cl::opt<bool> DisableHazardRecognizer(
  "disable-sched-hazard", cl::Hidden, cl::init(false),
  cl::desc("Disable hazard detection during preRA scheduling"));

TargetInstrInfo::~TargetInstrInfo() {
}

const TargetRegisterClass*
TargetInstrInfo::getRegClass(const MCInstrDesc &MCID, unsigned OpNum,
                             const TargetRegisterInfo *TRI,
                             const MachineFunction &MF) const {
  if (OpNum >= MCID.getNumOperands())
    return nullptr;

  short RegClass = MCID.OpInfo[OpNum].RegClass;
  if (MCID.OpInfo[OpNum].isLookupPtrRegClass())
    return TRI->getPointerRegClass(MF, RegClass);

  // Instructions like INSERT_SUBREG do not have fixed register classes.
  if (RegClass < 0)
    return nullptr;

  // Otherwise just look it up normally.
  return TRI->getRegClass(RegClass);
}

/// insertNoop - Insert a noop into the instruction stream at the specified
/// point.
void TargetInstrInfo::insertNoop(MachineBasicBlock &MBB,
                                 MachineBasicBlock::iterator MI) const {
  llvm_unreachable("Target didn't implement insertNoop!");
}

static bool isAsmComment(const char *Str, const MCAsmInfo &MAI) {
  return strncmp(Str, MAI.getCommentString().data(),
                 MAI.getCommentString().size()) == 0;
}

/// Measure the specified inline asm to determine an approximation of its
/// length.
/// Comments (which run till the next SeparatorString or newline) do not
/// count as an instruction.
/// Any other non-whitespace text is considered an instruction, with
/// multiple instructions separated by SeparatorString or newlines.
/// Variable-length instructions are not handled here; this function
/// may be overloaded in the target code to do that.
/// We implement a special case of the .space directive which takes only a
/// single integer argument in base 10 that is the size in bytes. This is a
/// restricted form of the GAS directive in that we only interpret
/// simple--i.e. not a logical or arithmetic expression--size values without
/// the optional fill value. This is primarily used for creating arbitrary
/// sized inline asm blocks for testing purposes.
unsigned TargetInstrInfo::getInlineAsmLength(const char *Str,
                                             const MCAsmInfo &MAI) const {
  // Count the number of instructions in the asm.
  bool AtInsnStart = true;
  unsigned Length = 0;
  for (; *Str; ++Str) {
    if (*Str == '\n' || strncmp(Str, MAI.getSeparatorString(),
                                strlen(MAI.getSeparatorString())) == 0) {
      AtInsnStart = true;
    } else if (isAsmComment(Str, MAI)) {
      // Stop counting as an instruction after a comment until the next
      // separator.
      AtInsnStart = false;
    }

    if (AtInsnStart && !std::isspace(static_cast<unsigned char>(*Str))) {
      unsigned AddLength = MAI.getMaxInstLength();
      if (strncmp(Str, ".space", 6) == 0) {
        char *EStr;
        int SpaceSize;
        SpaceSize = strtol(Str + 6, &EStr, 10);
        SpaceSize = SpaceSize < 0 ? 0 : SpaceSize;
        while (*EStr != '\n' && std::isspace(static_cast<unsigned char>(*EStr)))
          ++EStr;
        if (*EStr == '\0' || *EStr == '\n' ||
            isAsmComment(EStr, MAI)) // Successfully parsed .space argument
          AddLength = SpaceSize;
      }
      Length += AddLength;
      AtInsnStart = false;
    }
  }

  return Length;
}

/// ReplaceTailWithBranchTo - Delete the instruction OldInst and everything
/// after it, replacing it with an unconditional branch to NewDest.
void
TargetInstrInfo::ReplaceTailWithBranchTo(MachineBasicBlock::iterator Tail,
                                         MachineBasicBlock *NewDest) const {
  MachineBasicBlock *MBB = Tail->getParent();

  // Remove all the old successors of MBB from the CFG.
  while (!MBB->succ_empty())
    MBB->removeSuccessor(MBB->succ_begin());

  // Save off the debug loc before erasing the instruction.
  DebugLoc DL = Tail->getDebugLoc();

  // Remove all the dead instructions from the end of MBB.
  MBB->erase(Tail, MBB->end());

  // If MBB isn't immediately before MBB, insert a branch to it.
  if (++MachineFunction::iterator(MBB) != MachineFunction::iterator(NewDest))
    insertBranch(*MBB, NewDest, nullptr, SmallVector<MachineOperand, 0>(), DL);
  MBB->addSuccessor(NewDest);
}

MachineInstr *TargetInstrInfo::commuteInstructionImpl(MachineInstr &MI,
                                                      bool NewMI, unsigned Idx1,
                                                      unsigned Idx2) const {
  const MCInstrDesc &MCID = MI.getDesc();
  bool HasDef = MCID.getNumDefs();
  if (HasDef && !MI.getOperand(0).isReg())
    // No idea how to commute this instruction. Target should implement its own.
    return nullptr;

  unsigned CommutableOpIdx1 = Idx1; (void)CommutableOpIdx1;
  unsigned CommutableOpIdx2 = Idx2; (void)CommutableOpIdx2;
  assert(findCommutedOpIndices(MI, CommutableOpIdx1, CommutableOpIdx2) &&
         CommutableOpIdx1 == Idx1 && CommutableOpIdx2 == Idx2 &&
         "TargetInstrInfo::CommuteInstructionImpl(): not commutable operands.");
  assert(MI.getOperand(Idx1).isReg() && MI.getOperand(Idx2).isReg() &&
         "This only knows how to commute register operands so far");

  unsigned Reg0 = HasDef ? MI.getOperand(0).getReg() : 0;
  unsigned Reg1 = MI.getOperand(Idx1).getReg();
  unsigned Reg2 = MI.getOperand(Idx2).getReg();
  unsigned SubReg0 = HasDef ? MI.getOperand(0).getSubReg() : 0;
  unsigned SubReg1 = MI.getOperand(Idx1).getSubReg();
  unsigned SubReg2 = MI.getOperand(Idx2).getSubReg();
  bool Reg1IsKill = MI.getOperand(Idx1).isKill();
  bool Reg2IsKill = MI.getOperand(Idx2).isKill();
  bool Reg1IsUndef = MI.getOperand(Idx1).isUndef();
  bool Reg2IsUndef = MI.getOperand(Idx2).isUndef();
  bool Reg1IsInternal = MI.getOperand(Idx1).isInternalRead();
  bool Reg2IsInternal = MI.getOperand(Idx2).isInternalRead();
  // Avoid calling isRenamable for virtual registers since we assert that
  // renamable property is only queried/set for physical registers.
  bool Reg1IsRenamable = TargetRegisterInfo::isPhysicalRegister(Reg1)
                             ? MI.getOperand(Idx1).isRenamable()
                             : false;
  bool Reg2IsRenamable = TargetRegisterInfo::isPhysicalRegister(Reg2)
                             ? MI.getOperand(Idx2).isRenamable()
                             : false;
  // If destination is tied to either of the commuted source register, then
  // it must be updated.
  if (HasDef && Reg0 == Reg1 &&
      MI.getDesc().getOperandConstraint(Idx1, MCOI::TIED_TO) == 0) {
    Reg2IsKill = false;
    Reg0 = Reg2;
    SubReg0 = SubReg2;
  } else if (HasDef && Reg0 == Reg2 &&
             MI.getDesc().getOperandConstraint(Idx2, MCOI::TIED_TO) == 0) {
    Reg1IsKill = false;
    Reg0 = Reg1;
    SubReg0 = SubReg1;
  }

  MachineInstr *CommutedMI = nullptr;
  if (NewMI) {
    // Create a new instruction.
    MachineFunction &MF = *MI.getMF();
    CommutedMI = MF.CloneMachineInstr(&MI);
  } else {
    CommutedMI = &MI;
  }

  if (HasDef) {
    CommutedMI->getOperand(0).setReg(Reg0);
    CommutedMI->getOperand(0).setSubReg(SubReg0);
  }
  CommutedMI->getOperand(Idx2).setReg(Reg1);
  CommutedMI->getOperand(Idx1).setReg(Reg2);
  CommutedMI->getOperand(Idx2).setSubReg(SubReg1);
  CommutedMI->getOperand(Idx1).setSubReg(SubReg2);
  CommutedMI->getOperand(Idx2).setIsKill(Reg1IsKill);
  CommutedMI->getOperand(Idx1).setIsKill(Reg2IsKill);
  CommutedMI->getOperand(Idx2).setIsUndef(Reg1IsUndef);
  CommutedMI->getOperand(Idx1).setIsUndef(Reg2IsUndef);
  CommutedMI->getOperand(Idx2).setIsInternalRead(Reg1IsInternal);
  CommutedMI->getOperand(Idx1).setIsInternalRead(Reg2IsInternal);
  // Avoid calling setIsRenamable for virtual registers since we assert that
  // renamable property is only queried/set for physical registers.
  if (TargetRegisterInfo::isPhysicalRegister(Reg1))
    CommutedMI->getOperand(Idx2).setIsRenamable(Reg1IsRenamable);
  if (TargetRegisterInfo::isPhysicalRegister(Reg2))
    CommutedMI->getOperand(Idx1).setIsRenamable(Reg2IsRenamable);
  return CommutedMI;
}

MachineInstr *TargetInstrInfo::commuteInstruction(MachineInstr &MI, bool NewMI,
                                                  unsigned OpIdx1,
                                                  unsigned OpIdx2) const {
  // If OpIdx1 or OpIdx2 is not specified, then this method is free to choose
  // any commutable operand, which is done in findCommutedOpIndices() method
  // called below.
  if ((OpIdx1 == CommuteAnyOperandIndex || OpIdx2 == CommuteAnyOperandIndex) &&
      !findCommutedOpIndices(MI, OpIdx1, OpIdx2)) {
    assert(MI.isCommutable() &&
           "Precondition violation: MI must be commutable.");
    return nullptr;
  }
  return commuteInstructionImpl(MI, NewMI, OpIdx1, OpIdx2);
}

bool TargetInstrInfo::fixCommutedOpIndices(unsigned &ResultIdx1,
                                           unsigned &ResultIdx2,
                                           unsigned CommutableOpIdx1,
                                           unsigned CommutableOpIdx2) {
  if (ResultIdx1 == CommuteAnyOperandIndex &&
      ResultIdx2 == CommuteAnyOperandIndex) {
    ResultIdx1 = CommutableOpIdx1;
    ResultIdx2 = CommutableOpIdx2;
  } else if (ResultIdx1 == CommuteAnyOperandIndex) {
    if (ResultIdx2 == CommutableOpIdx1)
      ResultIdx1 = CommutableOpIdx2;
    else if (ResultIdx2 == CommutableOpIdx2)
      ResultIdx1 = CommutableOpIdx1;
    else
      return false;
  } else if (ResultIdx2 == CommuteAnyOperandIndex) {
    if (ResultIdx1 == CommutableOpIdx1)
      ResultIdx2 = CommutableOpIdx2;
    else if (ResultIdx1 == CommutableOpIdx2)
      ResultIdx2 = CommutableOpIdx1;
    else
      return false;
  } else
    // Check that the result operand indices match the given commutable
    // operand indices.
    return (ResultIdx1 == CommutableOpIdx1 && ResultIdx2 == CommutableOpIdx2) ||
           (ResultIdx1 == CommutableOpIdx2 && ResultIdx2 == CommutableOpIdx1);

  return true;
}

bool TargetInstrInfo::findCommutedOpIndices(MachineInstr &MI,
                                            unsigned &SrcOpIdx1,
                                            unsigned &SrcOpIdx2) const {
  assert(!MI.isBundle() &&
         "TargetInstrInfo::findCommutedOpIndices() can't handle bundles");

  const MCInstrDesc &MCID = MI.getDesc();
  if (!MCID.isCommutable())
    return false;

  // This assumes v0 = op v1, v2 and commuting would swap v1 and v2. If this
  // is not true, then the target must implement this.
  unsigned CommutableOpIdx1 = MCID.getNumDefs();
  unsigned CommutableOpIdx2 = CommutableOpIdx1 + 1;
  if (!fixCommutedOpIndices(SrcOpIdx1, SrcOpIdx2,
                            CommutableOpIdx1, CommutableOpIdx2))
    return false;

  if (!MI.getOperand(SrcOpIdx1).isReg() || !MI.getOperand(SrcOpIdx2).isReg())
    // No idea.
    return false;
  return true;
}

bool TargetInstrInfo::isUnpredicatedTerminator(const MachineInstr &MI) const {
  if (!MI.isTerminator()) return false;

  // Conditional branch is a special case.
  if (MI.isBranch() && !MI.isBarrier())
    return true;
  if (!MI.isPredicable())
    return true;
  return !isPredicated(MI);
}

bool TargetInstrInfo::PredicateInstruction(
    MachineInstr &MI, ArrayRef<MachineOperand> Pred) const {
  bool MadeChange = false;

  assert(!MI.isBundle() &&
         "TargetInstrInfo::PredicateInstruction() can't handle bundles");

  const MCInstrDesc &MCID = MI.getDesc();
  if (!MI.isPredicable())
    return false;

  for (unsigned j = 0, i = 0, e = MI.getNumOperands(); i != e; ++i) {
    if (MCID.OpInfo[i].isPredicate()) {
      MachineOperand &MO = MI.getOperand(i);
      if (MO.isReg()) {
        MO.setReg(Pred[j].getReg());
        MadeChange = true;
      } else if (MO.isImm()) {
        MO.setImm(Pred[j].getImm());
        MadeChange = true;
      } else if (MO.isMBB()) {
        MO.setMBB(Pred[j].getMBB());
        MadeChange = true;
      }
      ++j;
    }
  }
  return MadeChange;
}

bool TargetInstrInfo::hasLoadFromStackSlot(
    const MachineInstr &MI,
    SmallVectorImpl<const MachineMemOperand *> &Accesses) const {
  size_t StartSize = Accesses.size();
  for (MachineInstr::mmo_iterator o = MI.memoperands_begin(),
                                  oe = MI.memoperands_end();
       o != oe; ++o) {
    if ((*o)->isLoad() &&
        dyn_cast_or_null<FixedStackPseudoSourceValue>((*o)->getPseudoValue()))
      Accesses.push_back(*o);
  }
  return Accesses.size() != StartSize;
}

bool TargetInstrInfo::hasStoreToStackSlot(
    const MachineInstr &MI,
    SmallVectorImpl<const MachineMemOperand *> &Accesses) const {
  size_t StartSize = Accesses.size();
  for (MachineInstr::mmo_iterator o = MI.memoperands_begin(),
                                  oe = MI.memoperands_end();
       o != oe; ++o) {
    if ((*o)->isStore() &&
        dyn_cast_or_null<FixedStackPseudoSourceValue>((*o)->getPseudoValue()))
      Accesses.push_back(*o);
  }
  return Accesses.size() != StartSize;
}

bool TargetInstrInfo::getStackSlotRange(const TargetRegisterClass *RC,
                                        unsigned SubIdx, unsigned &Size,
                                        unsigned &Offset,
                                        const MachineFunction &MF) const {
  const TargetRegisterInfo *TRI = MF.getSubtarget().getRegisterInfo();
  if (!SubIdx) {
    Size = TRI->getSpillSize(*RC);
    Offset = 0;
    return true;
  }
  unsigned BitSize = TRI->getSubRegIdxSize(SubIdx);
  // Convert bit size to byte size.
  if (BitSize % 8)
    return false;

  int BitOffset = TRI->getSubRegIdxOffset(SubIdx);
  if (BitOffset < 0 || BitOffset % 8)
    return false;

  Size = BitSize /= 8;
  Offset = (unsigned)BitOffset / 8;

  assert(TRI->getSpillSize(*RC) >= (Offset + Size) && "bad subregister range");

  if (!MF.getDataLayout().isLittleEndian()) {
    Offset = TRI->getSpillSize(*RC) - (Offset + Size);
  }
  return true;
}

void TargetInstrInfo::reMaterialize(MachineBasicBlock &MBB,
                                    MachineBasicBlock::iterator I,
                                    unsigned DestReg, unsigned SubIdx,
                                    const MachineInstr &Orig,
                                    const TargetRegisterInfo &TRI) const {
  MachineInstr *MI = MBB.getParent()->CloneMachineInstr(&Orig);
  MI->substituteRegister(MI->getOperand(0).getReg(), DestReg, SubIdx, TRI);
  MBB.insert(I, MI);
}

bool TargetInstrInfo::produceSameValue(const MachineInstr &MI0,
                                       const MachineInstr &MI1,
                                       const MachineRegisterInfo *MRI) const {
  return MI0.isIdenticalTo(MI1, MachineInstr::IgnoreVRegDefs);
}

MachineInstr &TargetInstrInfo::duplicate(MachineBasicBlock &MBB,
    MachineBasicBlock::iterator InsertBefore, const MachineInstr &Orig) const {
  assert(!Orig.isNotDuplicable() && "Instruction cannot be duplicated");
  MachineFunction &MF = *MBB.getParent();
  return MF.CloneMachineInstrBundle(MBB, InsertBefore, Orig);
}

// If the COPY instruction in MI can be folded to a stack operation, return
// the register class to use.
static const TargetRegisterClass *canFoldCopy(const MachineInstr &MI,
                                              unsigned FoldIdx) {
  assert(MI.isCopy() && "MI must be a COPY instruction");
  if (MI.getNumOperands() != 2)
    return nullptr;
  assert(FoldIdx<2 && "FoldIdx refers no nonexistent operand");

  const MachineOperand &FoldOp = MI.getOperand(FoldIdx);
  const MachineOperand &LiveOp = MI.getOperand(1 - FoldIdx);

  if (FoldOp.getSubReg() || LiveOp.getSubReg())
    return nullptr;

  unsigned FoldReg = FoldOp.getReg();
  unsigned LiveReg = LiveOp.getReg();

  assert(TargetRegisterInfo::isVirtualRegister(FoldReg) &&
         "Cannot fold physregs");

  const MachineRegisterInfo &MRI = MI.getMF()->getRegInfo();
  const TargetRegisterClass *RC = MRI.getRegClass(FoldReg);

  if (TargetRegisterInfo::isPhysicalRegister(LiveOp.getReg()))
    return RC->contains(LiveOp.getReg()) ? RC : nullptr;

  if (RC->hasSubClassEq(MRI.getRegClass(LiveReg)))
    return RC;

  // FIXME: Allow folding when register classes are memory compatible.
  return nullptr;
}

void TargetInstrInfo::getNoop(MCInst &NopInst) const {
  llvm_unreachable("Not implemented");
}

static MachineInstr *foldPatchpoint(MachineFunction &MF, MachineInstr &MI,
                                    ArrayRef<unsigned> Ops, int FrameIndex,
                                    const TargetInstrInfo &TII) {
  unsigned StartIdx = 0;
  switch (MI.getOpcode()) {
  case TargetOpcode::STACKMAP: {
    // StackMapLiveValues are foldable
    StartIdx = StackMapOpers(&MI).getVarIdx();
    break;
  }
  case TargetOpcode::PATCHPOINT: {
    // For PatchPoint, the call args are not foldable (even if reported in the
    // stackmap e.g. via anyregcc).
    StartIdx = PatchPointOpers(&MI).getVarIdx();
    break;
  }
  case TargetOpcode::STATEPOINT: {
    // For statepoints, fold deopt and gc arguments, but not call arguments.
    StartIdx = StatepointOpers(&MI).getVarIdx();
    break;
  }
  default:
    llvm_unreachable("unexpected stackmap opcode");
  }

  // Return false if any operands requested for folding are not foldable (not
  // part of the stackmap's live values).
  for (unsigned Op : Ops) {
    if (Op < StartIdx)
      return nullptr;
  }

  MachineInstr *NewMI =
      MF.CreateMachineInstr(TII.get(MI.getOpcode()), MI.getDebugLoc(), true);
  MachineInstrBuilder MIB(MF, NewMI);

  // No need to fold return, the meta data, and function arguments
  for (unsigned i = 0; i < StartIdx; ++i)
    MIB.add(MI.getOperand(i));

  for (unsigned i = StartIdx; i < MI.getNumOperands(); ++i) {
    MachineOperand &MO = MI.getOperand(i);
    if (is_contained(Ops, i)) {
      unsigned SpillSize;
      unsigned SpillOffset;
      // Compute the spill slot size and offset.
      const TargetRegisterClass *RC =
        MF.getRegInfo().getRegClass(MO.getReg());
      bool Valid =
          TII.getStackSlotRange(RC, MO.getSubReg(), SpillSize, SpillOffset, MF);
      if (!Valid)
        report_fatal_error("cannot spill patchpoint subregister operand");
      MIB.addImm(StackMaps::IndirectMemRefOp);
      MIB.addImm(SpillSize);
      MIB.addFrameIndex(FrameIndex);
      MIB.addImm(SpillOffset);
    }
    else
      MIB.add(MO);
  }
  return NewMI;
}

MachineInstr *TargetInstrInfo::foldMemoryOperand(MachineInstr &MI,
                                                 ArrayRef<unsigned> Ops, int FI,
                                                 LiveIntervals *LIS) const {
  auto Flags = MachineMemOperand::MONone;
  for (unsigned OpIdx : Ops)
    Flags |= MI.getOperand(OpIdx).isDef() ? MachineMemOperand::MOStore
                                          : MachineMemOperand::MOLoad;

  MachineBasicBlock *MBB = MI.getParent();
  assert(MBB && "foldMemoryOperand needs an inserted instruction");
  MachineFunction &MF = *MBB->getParent();

  // If we're not folding a load into a subreg, the size of the load is the
  // size of the spill slot. But if we are, we need to figure out what the
  // actual load size is.
  int64_t MemSize = 0;
  const MachineFrameInfo &MFI = MF.getFrameInfo();
  const TargetRegisterInfo *TRI = MF.getSubtarget().getRegisterInfo();

  if (Flags & MachineMemOperand::MOStore) {
    MemSize = MFI.getObjectSize(FI);
  } else {
    for (unsigned OpIdx : Ops) {
      int64_t OpSize = MFI.getObjectSize(FI);

      if (auto SubReg = MI.getOperand(OpIdx).getSubReg()) {
        unsigned SubRegSize = TRI->getSubRegIdxSize(SubReg);
        if (SubRegSize > 0 && !(SubRegSize % 8))
          OpSize = SubRegSize / 8;
      }

      MemSize = std::max(MemSize, OpSize);
    }
  }

  assert(MemSize && "Did not expect a zero-sized stack slot");

  MachineInstr *NewMI = nullptr;

  if (MI.getOpcode() == TargetOpcode::STACKMAP ||
      MI.getOpcode() == TargetOpcode::PATCHPOINT ||
      MI.getOpcode() == TargetOpcode::STATEPOINT) {
    // Fold stackmap/patchpoint.
    NewMI = foldPatchpoint(MF, MI, Ops, FI, *this);
    if (NewMI)
      MBB->insert(MI, NewMI);
  } else {
    // Ask the target to do the actual folding.
    NewMI = foldMemoryOperandImpl(MF, MI, Ops, MI, FI, LIS);
  }

  if (NewMI) {
    NewMI->setMemRefs(MF, MI.memoperands());
    // Add a memory operand, foldMemoryOperandImpl doesn't do that.
    assert((!(Flags & MachineMemOperand::MOStore) ||
            NewMI->mayStore()) &&
           "Folded a def to a non-store!");
    assert((!(Flags & MachineMemOperand::MOLoad) ||
            NewMI->mayLoad()) &&
           "Folded a use to a non-load!");
    assert(MFI.getObjectOffset(FI) != -1);
    MachineMemOperand *MMO = MF.getMachineMemOperand(
        MachinePointerInfo::getFixedStack(MF, FI), Flags, MemSize,
        MFI.getObjectAlignment(FI));
    NewMI->addMemOperand(MF, MMO);

    return NewMI;
  }

  // Straight COPY may fold as load/store.
  if (!MI.isCopy() || Ops.size() != 1)
    return nullptr;

  const TargetRegisterClass *RC = canFoldCopy(MI, Ops[0]);
  if (!RC)
    return nullptr;

  const MachineOperand &MO = MI.getOperand(1 - Ops[0]);
  MachineBasicBlock::iterator Pos = MI;

  if (Flags == MachineMemOperand::MOStore)
    storeRegToStackSlot(*MBB, Pos, MO.getReg(), MO.isKill(), FI, RC, TRI);
  else
    loadRegFromStackSlot(*MBB, Pos, MO.getReg(), FI, RC, TRI);
  return &*--Pos;
}

MachineInstr *TargetInstrInfo::foldMemoryOperand(MachineInstr &MI,
                                                 ArrayRef<unsigned> Ops,
                                                 MachineInstr &LoadMI,
                                                 LiveIntervals *LIS) const {
  assert(LoadMI.canFoldAsLoad() && "LoadMI isn't foldable!");
#ifndef NDEBUG
  for (unsigned OpIdx : Ops)
    assert(MI.getOperand(OpIdx).isUse() && "Folding load into def!");
#endif

  MachineBasicBlock &MBB = *MI.getParent();
  MachineFunction &MF = *MBB.getParent();

  // Ask the target to do the actual folding.
  MachineInstr *NewMI = nullptr;
  int FrameIndex = 0;

  if ((MI.getOpcode() == TargetOpcode::STACKMAP ||
       MI.getOpcode() == TargetOpcode::PATCHPOINT ||
       MI.getOpcode() == TargetOpcode::STATEPOINT) &&
      isLoadFromStackSlot(LoadMI, FrameIndex)) {
    // Fold stackmap/patchpoint.
    NewMI = foldPatchpoint(MF, MI, Ops, FrameIndex, *this);
    if (NewMI)
      NewMI = &*MBB.insert(MI, NewMI);
  } else {
    // Ask the target to do the actual folding.
    NewMI = foldMemoryOperandImpl(MF, MI, Ops, MI, LoadMI, LIS);
  }

  if (!NewMI)
    return nullptr;

  // Copy the memoperands from the load to the folded instruction.
  if (MI.memoperands_empty()) {
    NewMI->setMemRefs(MF, LoadMI.memoperands());
  } else {
    // Handle the rare case of folding multiple loads.
    NewMI->setMemRefs(MF, MI.memoperands());
    for (MachineInstr::mmo_iterator I = LoadMI.memoperands_begin(),
                                    E = LoadMI.memoperands_end();
         I != E; ++I) {
      NewMI->addMemOperand(MF, *I);
    }
  }
  return NewMI;
}

bool TargetInstrInfo::hasReassociableOperands(
    const MachineInstr &Inst, const MachineBasicBlock *MBB) const {
  const MachineOperand &Op1 = Inst.getOperand(1);
  const MachineOperand &Op2 = Inst.getOperand(2);
  const MachineRegisterInfo &MRI = MBB->getParent()->getRegInfo();

  // We need virtual register definitions for the operands that we will
  // reassociate.
  MachineInstr *MI1 = nullptr;
  MachineInstr *MI2 = nullptr;
  if (Op1.isReg() && TargetRegisterInfo::isVirtualRegister(Op1.getReg()))
    MI1 = MRI.getUniqueVRegDef(Op1.getReg());
  if (Op2.isReg() && TargetRegisterInfo::isVirtualRegister(Op2.getReg()))
    MI2 = MRI.getUniqueVRegDef(Op2.getReg());

  // And they need to be in the trace (otherwise, they won't have a depth).
  return MI1 && MI2 && MI1->getParent() == MBB && MI2->getParent() == MBB;
}

bool TargetInstrInfo::hasReassociableSibling(const MachineInstr &Inst,
                                             bool &Commuted) const {
  const MachineBasicBlock *MBB = Inst.getParent();
  const MachineRegisterInfo &MRI = MBB->getParent()->getRegInfo();
  MachineInstr *MI1 = MRI.getUniqueVRegDef(Inst.getOperand(1).getReg());
  MachineInstr *MI2 = MRI.getUniqueVRegDef(Inst.getOperand(2).getReg());
  unsigned AssocOpcode = Inst.getOpcode();

  // If only one operand has the same opcode and it's the second source operand,
  // the operands must be commuted.
  Commuted = MI1->getOpcode() != AssocOpcode && MI2->getOpcode() == AssocOpcode;
  if (Commuted)
    std::swap(MI1, MI2);

  // 1. The previous instruction must be the same type as Inst.
  // 2. The previous instruction must have virtual register definitions for its
  //    operands in the same basic block as Inst.
  // 3. The previous instruction's result must only be used by Inst.
  return MI1->getOpcode() == AssocOpcode &&
         hasReassociableOperands(*MI1, MBB) &&
         MRI.hasOneNonDBGUse(MI1->getOperand(0).getReg());
}

// 1. The operation must be associative and commutative.
// 2. The instruction must have virtual register definitions for its
//    operands in the same basic block.
// 3. The instruction must have a reassociable sibling.
bool TargetInstrInfo::isReassociationCandidate(const MachineInstr &Inst,
                                               bool &Commuted) const {
  return isAssociativeAndCommutative(Inst) &&
         hasReassociableOperands(Inst, Inst.getParent()) &&
         hasReassociableSibling(Inst, Commuted);
}

// The concept of the reassociation pass is that these operations can benefit
// from this kind of transformation:
//
// A = ? op ?
// B = A op X (Prev)
// C = B op Y (Root)
// -->
// A = ? op ?
// B = X op Y
// C = A op B
//
// breaking the dependency between A and B, allowing them to be executed in
// parallel (or back-to-back in a pipeline) instead of depending on each other.

// FIXME: This has the potential to be expensive (compile time) while not
// improving the code at all. Some ways to limit the overhead:
// 1. Track successful transforms; bail out if hit rate gets too low.
// 2. Only enable at -O3 or some other non-default optimization level.
// 3. Pre-screen pattern candidates here: if an operand of the previous
//    instruction is known to not increase the critical path, then don't match
//    that pattern.
bool TargetInstrInfo::getMachineCombinerPatterns(
    MachineInstr &Root,
    SmallVectorImpl<MachineCombinerPattern> &Patterns) const {
  bool Commute;
  if (isReassociationCandidate(Root, Commute)) {
    // We found a sequence of instructions that may be suitable for a
    // reassociation of operands to increase ILP. Specify each commutation
    // possibility for the Prev instruction in the sequence and let the
    // machine combiner decide if changing the operands is worthwhile.
    if (Commute) {
      Patterns.push_back(MachineCombinerPattern::REASSOC_AX_YB);
      Patterns.push_back(MachineCombinerPattern::REASSOC_XA_YB);
    } else {
      Patterns.push_back(MachineCombinerPattern::REASSOC_AX_BY);
      Patterns.push_back(MachineCombinerPattern::REASSOC_XA_BY);
    }
    return true;
  }

  return false;
}

/// Return true when a code sequence can improve loop throughput.
bool
TargetInstrInfo::isThroughputPattern(MachineCombinerPattern Pattern) const {
  return false;
}

/// Attempt the reassociation transformation to reduce critical path length.
/// See the above comments before getMachineCombinerPatterns().
void TargetInstrInfo::reassociateOps(
    MachineInstr &Root, MachineInstr &Prev,
    MachineCombinerPattern Pattern,
    SmallVectorImpl<MachineInstr *> &InsInstrs,
    SmallVectorImpl<MachineInstr *> &DelInstrs,
    DenseMap<unsigned, unsigned> &InstrIdxForVirtReg) const {
  MachineFunction *MF = Root.getMF();
  MachineRegisterInfo &MRI = MF->getRegInfo();
  const TargetInstrInfo *TII = MF->getSubtarget().getInstrInfo();
  const TargetRegisterInfo *TRI = MF->getSubtarget().getRegisterInfo();
  const TargetRegisterClass *RC = Root.getRegClassConstraint(0, TII, TRI);

  // This array encodes the operand index for each parameter because the
  // operands may be commuted. Each row corresponds to a pattern value,
  // and each column specifies the index of A, B, X, Y.
  unsigned OpIdx[4][4] = {
    { 1, 1, 2, 2 },
    { 1, 2, 2, 1 },
    { 2, 1, 1, 2 },
    { 2, 2, 1, 1 }
  };

  int Row;
  switch (Pattern) {
  case MachineCombinerPattern::REASSOC_AX_BY: Row = 0; break;
  case MachineCombinerPattern::REASSOC_AX_YB: Row = 1; break;
  case MachineCombinerPattern::REASSOC_XA_BY: Row = 2; break;
  case MachineCombinerPattern::REASSOC_XA_YB: Row = 3; break;
  default: llvm_unreachable("unexpected MachineCombinerPattern");
  }

  MachineOperand &OpA = Prev.getOperand(OpIdx[Row][0]);
  MachineOperand &OpB = Root.getOperand(OpIdx[Row][1]);
  MachineOperand &OpX = Prev.getOperand(OpIdx[Row][2]);
  MachineOperand &OpY = Root.getOperand(OpIdx[Row][3]);
  MachineOperand &OpC = Root.getOperand(0);

  unsigned RegA = OpA.getReg();
  unsigned RegB = OpB.getReg();
  unsigned RegX = OpX.getReg();
  unsigned RegY = OpY.getReg();
  unsigned RegC = OpC.getReg();

  if (TargetRegisterInfo::isVirtualRegister(RegA))
    MRI.constrainRegClass(RegA, RC);
  if (TargetRegisterInfo::isVirtualRegister(RegB))
    MRI.constrainRegClass(RegB, RC);
  if (TargetRegisterInfo::isVirtualRegister(RegX))
    MRI.constrainRegClass(RegX, RC);
  if (TargetRegisterInfo::isVirtualRegister(RegY))
    MRI.constrainRegClass(RegY, RC);
  if (TargetRegisterInfo::isVirtualRegister(RegC))
    MRI.constrainRegClass(RegC, RC);

  // Create a new virtual register for the result of (X op Y) instead of
  // recycling RegB because the MachineCombiner's computation of the critical
  // path requires a new register definition rather than an existing one.
  unsigned NewVR = MRI.createVirtualRegister(RC);
  InstrIdxForVirtReg.insert(std::make_pair(NewVR, 0));

  unsigned Opcode = Root.getOpcode();
  bool KillA = OpA.isKill();
  bool KillX = OpX.isKill();
  bool KillY = OpY.isKill();

  // Create new instructions for insertion.
  MachineInstrBuilder MIB1 =
      BuildMI(*MF, Prev.getDebugLoc(), TII->get(Opcode), NewVR)
          .addReg(RegX, getKillRegState(KillX))
          .addReg(RegY, getKillRegState(KillY));
  MachineInstrBuilder MIB2 =
      BuildMI(*MF, Root.getDebugLoc(), TII->get(Opcode), RegC)
          .addReg(RegA, getKillRegState(KillA))
          .addReg(NewVR, getKillRegState(true));

  setSpecialOperandAttr(Root, Prev, *MIB1, *MIB2);

  // Record new instructions for insertion and old instructions for deletion.
  InsInstrs.push_back(MIB1);
  InsInstrs.push_back(MIB2);
  DelInstrs.push_back(&Prev);
  DelInstrs.push_back(&Root);
}

void TargetInstrInfo::genAlternativeCodeSequence(
    MachineInstr &Root, MachineCombinerPattern Pattern,
    SmallVectorImpl<MachineInstr *> &InsInstrs,
    SmallVectorImpl<MachineInstr *> &DelInstrs,
    DenseMap<unsigned, unsigned> &InstIdxForVirtReg) const {
  MachineRegisterInfo &MRI = Root.getMF()->getRegInfo();

  // Select the previous instruction in the sequence based on the input pattern.
  MachineInstr *Prev = nullptr;
  switch (Pattern) {
  case MachineCombinerPattern::REASSOC_AX_BY:
  case MachineCombinerPattern::REASSOC_XA_BY:
    Prev = MRI.getUniqueVRegDef(Root.getOperand(1).getReg());
    break;
  case MachineCombinerPattern::REASSOC_AX_YB:
  case MachineCombinerPattern::REASSOC_XA_YB:
    Prev = MRI.getUniqueVRegDef(Root.getOperand(2).getReg());
    break;
  default:
    break;
  }

  assert(Prev && "Unknown pattern for machine combiner");

  reassociateOps(Root, *Prev, Pattern, InsInstrs, DelInstrs, InstIdxForVirtReg);
}

bool TargetInstrInfo::isReallyTriviallyReMaterializableGeneric(
    const MachineInstr &MI, AliasAnalysis *AA) const {
  const MachineFunction &MF = *MI.getMF();
  const MachineRegisterInfo &MRI = MF.getRegInfo();

  // Remat clients assume operand 0 is the defined register.
  if (!MI.getNumOperands() || !MI.getOperand(0).isReg())
    return false;
  unsigned DefReg = MI.getOperand(0).getReg();

  // A sub-register definition can only be rematerialized if the instruction
  // doesn't read the other parts of the register.  Otherwise it is really a
  // read-modify-write operation on the full virtual register which cannot be
  // moved safely.
  if (TargetRegisterInfo::isVirtualRegister(DefReg) &&
      MI.getOperand(0).getSubReg() && MI.readsVirtualRegister(DefReg))
    return false;

  // A load from a fixed stack slot can be rematerialized. This may be
  // redundant with subsequent checks, but it's target-independent,
  // simple, and a common case.
  int FrameIdx = 0;
  if (isLoadFromStackSlot(MI, FrameIdx) &&
      MF.getFrameInfo().isImmutableObjectIndex(FrameIdx))
    return true;

  // Avoid instructions obviously unsafe for remat.
  if (MI.isNotDuplicable() || MI.mayStore() || MI.hasUnmodeledSideEffects())
    return false;

  // Don't remat inline asm. We have no idea how expensive it is
  // even if it's side effect free.
  if (MI.isInlineAsm())
    return false;

  // Avoid instructions which load from potentially varying memory.
  if (MI.mayLoad() && !MI.isDereferenceableInvariantLoad(AA))
    return false;

  // If any of the registers accessed are non-constant, conservatively assume
  // the instruction is not rematerializable.
  for (unsigned i = 0, e = MI.getNumOperands(); i != e; ++i) {
    const MachineOperand &MO = MI.getOperand(i);
    if (!MO.isReg()) continue;
    unsigned Reg = MO.getReg();
    if (Reg == 0)
      continue;

    // Check for a well-behaved physical register.
    if (TargetRegisterInfo::isPhysicalRegister(Reg)) {
      if (MO.isUse()) {
        // If the physreg has no defs anywhere, it's just an ambient register
        // and we can freely move its uses. Alternatively, if it's allocatable,
        // it could get allocated to something with a def during allocation.
        if (!MRI.isConstantPhysReg(Reg))
          return false;
      } else {
        // A physreg def. We can't remat it.
        return false;
      }
      continue;
    }

    // Only allow one virtual-register def.  There may be multiple defs of the
    // same virtual register, though.
    if (MO.isDef() && Reg != DefReg)
      return false;

    // Don't allow any virtual-register uses. Rematting an instruction with
    // virtual register uses would length the live ranges of the uses, which
    // is not necessarily a good idea, certainly not "trivial".
    if (MO.isUse())
      return false;
  }

  // Everything checked out.
  return true;
}

int TargetInstrInfo::getSPAdjust(const MachineInstr &MI) const {
  const MachineFunction *MF = MI.getMF();
  const TargetFrameLowering *TFI = MF->getSubtarget().getFrameLowering();
  bool StackGrowsDown =
    TFI->getStackGrowthDirection() == TargetFrameLowering::StackGrowsDown;

  unsigned FrameSetupOpcode = getCallFrameSetupOpcode();
  unsigned FrameDestroyOpcode = getCallFrameDestroyOpcode();

  if (!isFrameInstr(MI))
    return 0;

  int SPAdj = TFI->alignSPAdjust(getFrameSize(MI));

  if ((!StackGrowsDown && MI.getOpcode() == FrameSetupOpcode) ||
      (StackGrowsDown && MI.getOpcode() == FrameDestroyOpcode))
    SPAdj = -SPAdj;

  return SPAdj;
}

/// isSchedulingBoundary - Test if the given instruction should be
/// considered a scheduling boundary. This primarily includes labels
/// and terminators.
bool TargetInstrInfo::isSchedulingBoundary(const MachineInstr &MI,
                                           const MachineBasicBlock *MBB,
                                           const MachineFunction &MF) const {
  // Terminators and labels can't be scheduled around.
  if (MI.isTerminator() || MI.isPosition())
    return true;

  // Don't attempt to schedule around any instruction that defines
  // a stack-oriented pointer, as it's unlikely to be profitable. This
  // saves compile time, because it doesn't require every single
  // stack slot reference to depend on the instruction that does the
  // modification.
  const TargetLowering &TLI = *MF.getSubtarget().getTargetLowering();
  const TargetRegisterInfo *TRI = MF.getSubtarget().getRegisterInfo();
  return MI.modifiesRegister(TLI.getStackPointerRegisterToSaveRestore(), TRI);
}

// Provide a global flag for disabling the PreRA hazard recognizer that targets
// may choose to honor.
bool TargetInstrInfo::usePreRAHazardRecognizer() const {
  return !DisableHazardRecognizer;
}

// Default implementation of CreateTargetRAHazardRecognizer.
ScheduleHazardRecognizer *TargetInstrInfo::
CreateTargetHazardRecognizer(const TargetSubtargetInfo *STI,
                             const ScheduleDAG *DAG) const {
  // Dummy hazard recognizer allows all instructions to issue.
  return new ScheduleHazardRecognizer();
}

// Default implementation of CreateTargetMIHazardRecognizer.
ScheduleHazardRecognizer *TargetInstrInfo::
CreateTargetMIHazardRecognizer(const InstrItineraryData *II,
                               const ScheduleDAG *DAG) const {
  return (ScheduleHazardRecognizer *)
    new ScoreboardHazardRecognizer(II, DAG, "misched");
}

// Default implementation of CreateTargetPostRAHazardRecognizer.
ScheduleHazardRecognizer *TargetInstrInfo::
CreateTargetPostRAHazardRecognizer(const InstrItineraryData *II,
                                   const ScheduleDAG *DAG) const {
  return (ScheduleHazardRecognizer *)
    new ScoreboardHazardRecognizer(II, DAG, "post-RA-sched");
}

//===----------------------------------------------------------------------===//
//  SelectionDAG latency interface.
//===----------------------------------------------------------------------===//

int
TargetInstrInfo::getOperandLatency(const InstrItineraryData *ItinData,
                                   SDNode *DefNode, unsigned DefIdx,
                                   SDNode *UseNode, unsigned UseIdx) const {
  if (!ItinData || ItinData->isEmpty())
    return -1;

  if (!DefNode->isMachineOpcode())
    return -1;

  unsigned DefClass = get(DefNode->getMachineOpcode()).getSchedClass();
  if (!UseNode->isMachineOpcode())
    return ItinData->getOperandCycle(DefClass, DefIdx);
  unsigned UseClass = get(UseNode->getMachineOpcode()).getSchedClass();
  return ItinData->getOperandLatency(DefClass, DefIdx, UseClass, UseIdx);
}

int TargetInstrInfo::getInstrLatency(const InstrItineraryData *ItinData,
                                     SDNode *N) const {
  if (!ItinData || ItinData->isEmpty())
    return 1;

  if (!N->isMachineOpcode())
    return 1;

  return ItinData->getStageLatency(get(N->getMachineOpcode()).getSchedClass());
}

//===----------------------------------------------------------------------===//
//  MachineInstr latency interface.
//===----------------------------------------------------------------------===//

unsigned TargetInstrInfo::getNumMicroOps(const InstrItineraryData *ItinData,
                                         const MachineInstr &MI) const {
  if (!ItinData || ItinData->isEmpty())
    return 1;

  unsigned Class = MI.getDesc().getSchedClass();
  int UOps = ItinData->Itineraries[Class].NumMicroOps;
  if (UOps >= 0)
    return UOps;

  // The # of u-ops is dynamically determined. The specific target should
  // override this function to return the right number.
  return 1;
}

/// Return the default expected latency for a def based on it's opcode.
unsigned TargetInstrInfo::defaultDefLatency(const MCSchedModel &SchedModel,
                                            const MachineInstr &DefMI) const {
  if (DefMI.isTransient())
    return 0;
  if (DefMI.mayLoad())
    return SchedModel.LoadLatency;
  if (isHighLatencyDef(DefMI.getOpcode()))
    return SchedModel.HighLatency;
  return 1;
}

unsigned TargetInstrInfo::getPredicationCost(const MachineInstr &) const {
  return 0;
}

unsigned TargetInstrInfo::getInstrLatency(const InstrItineraryData *ItinData,
                                          const MachineInstr &MI,
                                          unsigned *PredCost) const {
  // Default to one cycle for no itinerary. However, an "empty" itinerary may
  // still have a MinLatency property, which getStageLatency checks.
  if (!ItinData)
    return MI.mayLoad() ? 2 : 1;

  return ItinData->getStageLatency(MI.getDesc().getSchedClass());
}

bool TargetInstrInfo::hasLowDefLatency(const TargetSchedModel &SchedModel,
                                       const MachineInstr &DefMI,
                                       unsigned DefIdx) const {
  const InstrItineraryData *ItinData = SchedModel.getInstrItineraries();
  if (!ItinData || ItinData->isEmpty())
    return false;

  unsigned DefClass = DefMI.getDesc().getSchedClass();
  int DefCycle = ItinData->getOperandCycle(DefClass, DefIdx);
  return (DefCycle != -1 && DefCycle <= 1);
}

/// Both DefMI and UseMI must be valid.  By default, call directly to the
/// itinerary. This may be overriden by the target.
int TargetInstrInfo::getOperandLatency(const InstrItineraryData *ItinData,
                                       const MachineInstr &DefMI,
                                       unsigned DefIdx,
                                       const MachineInstr &UseMI,
                                       unsigned UseIdx) const {
  unsigned DefClass = DefMI.getDesc().getSchedClass();
  unsigned UseClass = UseMI.getDesc().getSchedClass();
  return ItinData->getOperandLatency(DefClass, DefIdx, UseClass, UseIdx);
}

/// If we can determine the operand latency from the def only, without itinerary
/// lookup, do so. Otherwise return -1.
int TargetInstrInfo::computeDefOperandLatency(
    const InstrItineraryData *ItinData, const MachineInstr &DefMI) const {

  // Let the target hook getInstrLatency handle missing itineraries.
  if (!ItinData)
    return getInstrLatency(ItinData, DefMI);

  if(ItinData->isEmpty())
    return defaultDefLatency(ItinData->SchedModel, DefMI);

  // ...operand lookup required
  return -1;
}

bool TargetInstrInfo::getRegSequenceInputs(
    const MachineInstr &MI, unsigned DefIdx,
    SmallVectorImpl<RegSubRegPairAndIdx> &InputRegs) const {
  assert((MI.isRegSequence() ||
          MI.isRegSequenceLike()) && "Instruction do not have the proper type");

  if (!MI.isRegSequence())
    return getRegSequenceLikeInputs(MI, DefIdx, InputRegs);

  // We are looking at:
  // Def = REG_SEQUENCE v0, sub0, v1, sub1, ...
  assert(DefIdx == 0 && "REG_SEQUENCE only has one def");
  for (unsigned OpIdx = 1, EndOpIdx = MI.getNumOperands(); OpIdx != EndOpIdx;
       OpIdx += 2) {
    const MachineOperand &MOReg = MI.getOperand(OpIdx);
    if (MOReg.isUndef())
      continue;
    const MachineOperand &MOSubIdx = MI.getOperand(OpIdx + 1);
    assert(MOSubIdx.isImm() &&
           "One of the subindex of the reg_sequence is not an immediate");
    // Record Reg:SubReg, SubIdx.
    InputRegs.push_back(RegSubRegPairAndIdx(MOReg.getReg(), MOReg.getSubReg(),
                                            (unsigned)MOSubIdx.getImm()));
  }
  return true;
}

bool TargetInstrInfo::getExtractSubregInputs(
    const MachineInstr &MI, unsigned DefIdx,
    RegSubRegPairAndIdx &InputReg) const {
  assert((MI.isExtractSubreg() ||
      MI.isExtractSubregLike()) && "Instruction do not have the proper type");

  if (!MI.isExtractSubreg())
    return getExtractSubregLikeInputs(MI, DefIdx, InputReg);

  // We are looking at:
  // Def = EXTRACT_SUBREG v0.sub1, sub0.
  assert(DefIdx == 0 && "EXTRACT_SUBREG only has one def");
  const MachineOperand &MOReg = MI.getOperand(1);
  if (MOReg.isUndef())
    return false;
  const MachineOperand &MOSubIdx = MI.getOperand(2);
  assert(MOSubIdx.isImm() &&
         "The subindex of the extract_subreg is not an immediate");

  InputReg.Reg = MOReg.getReg();
  InputReg.SubReg = MOReg.getSubReg();
  InputReg.SubIdx = (unsigned)MOSubIdx.getImm();
  return true;
}

bool TargetInstrInfo::getInsertSubregInputs(
    const MachineInstr &MI, unsigned DefIdx,
    RegSubRegPair &BaseReg, RegSubRegPairAndIdx &InsertedReg) const {
  assert((MI.isInsertSubreg() ||
      MI.isInsertSubregLike()) && "Instruction do not have the proper type");

  if (!MI.isInsertSubreg())
    return getInsertSubregLikeInputs(MI, DefIdx, BaseReg, InsertedReg);

  // We are looking at:
  // Def = INSERT_SEQUENCE v0, v1, sub0.
  assert(DefIdx == 0 && "INSERT_SUBREG only has one def");
  const MachineOperand &MOBaseReg = MI.getOperand(1);
  const MachineOperand &MOInsertedReg = MI.getOperand(2);
  if (MOInsertedReg.isUndef())
    return false;
  const MachineOperand &MOSubIdx = MI.getOperand(3);
  assert(MOSubIdx.isImm() &&
         "One of the subindex of the reg_sequence is not an immediate");
  BaseReg.Reg = MOBaseReg.getReg();
  BaseReg.SubReg = MOBaseReg.getSubReg();

  InsertedReg.Reg = MOInsertedReg.getReg();
  InsertedReg.SubReg = MOInsertedReg.getSubReg();
  InsertedReg.SubIdx = (unsigned)MOSubIdx.getImm();
  return true;
}
