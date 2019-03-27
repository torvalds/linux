//===-- MachineFrameInfo.cpp ---------------------------------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
/// \file Implements MachineFrameInfo that manages the stack frame.
//
//===----------------------------------------------------------------------===//

#include "llvm/CodeGen/MachineFrameInfo.h"

#include "llvm/ADT/BitVector.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/CodeGen/TargetFrameLowering.h"
#include "llvm/CodeGen/TargetInstrInfo.h"
#include "llvm/CodeGen/TargetRegisterInfo.h"
#include "llvm/CodeGen/TargetSubtargetInfo.h"
#include "llvm/Config/llvm-config.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/raw_ostream.h"
#include <cassert>

#define DEBUG_TYPE "codegen"

using namespace llvm;

void MachineFrameInfo::ensureMaxAlignment(unsigned Align) {
  if (!StackRealignable)
    assert(Align <= StackAlignment &&
           "For targets without stack realignment, Align is out of limit!");
  if (MaxAlignment < Align) MaxAlignment = Align;
}

/// Clamp the alignment if requested and emit a warning.
static inline unsigned clampStackAlignment(bool ShouldClamp, unsigned Align,
                                           unsigned StackAlign) {
  if (!ShouldClamp || Align <= StackAlign)
    return Align;
  LLVM_DEBUG(dbgs() << "Warning: requested alignment " << Align
                    << " exceeds the stack alignment " << StackAlign
                    << " when stack realignment is off" << '\n');
  return StackAlign;
}

int MachineFrameInfo::CreateStackObject(uint64_t Size, unsigned Alignment,
                                        bool IsSpillSlot,
                                        const AllocaInst *Alloca,
                                        uint8_t StackID) {
  assert(Size != 0 && "Cannot allocate zero size stack objects!");
  Alignment = clampStackAlignment(!StackRealignable, Alignment, StackAlignment);
  Objects.push_back(StackObject(Size, Alignment, 0, false, IsSpillSlot, Alloca,
                                !IsSpillSlot, StackID));
  int Index = (int)Objects.size() - NumFixedObjects - 1;
  assert(Index >= 0 && "Bad frame index!");
  ensureMaxAlignment(Alignment);
  return Index;
}

int MachineFrameInfo::CreateSpillStackObject(uint64_t Size,
                                             unsigned Alignment) {
  Alignment = clampStackAlignment(!StackRealignable, Alignment, StackAlignment);
  CreateStackObject(Size, Alignment, true);
  int Index = (int)Objects.size() - NumFixedObjects - 1;
  ensureMaxAlignment(Alignment);
  return Index;
}

int MachineFrameInfo::CreateVariableSizedObject(unsigned Alignment,
                                                const AllocaInst *Alloca) {
  HasVarSizedObjects = true;
  Alignment = clampStackAlignment(!StackRealignable, Alignment, StackAlignment);
  Objects.push_back(StackObject(0, Alignment, 0, false, false, Alloca, true));
  ensureMaxAlignment(Alignment);
  return (int)Objects.size()-NumFixedObjects-1;
}

int MachineFrameInfo::CreateFixedObject(uint64_t Size, int64_t SPOffset,
                                        bool IsImmutable, bool IsAliased) {
  assert(Size != 0 && "Cannot allocate zero size fixed stack objects!");
  // The alignment of the frame index can be determined from its offset from
  // the incoming frame position.  If the frame object is at offset 32 and
  // the stack is guaranteed to be 16-byte aligned, then we know that the
  // object is 16-byte aligned. Note that unlike the non-fixed case, if the
  // stack needs realignment, we can't assume that the stack will in fact be
  // aligned.
  unsigned Alignment = MinAlign(SPOffset, ForcedRealign ? 1 : StackAlignment);
  Alignment = clampStackAlignment(!StackRealignable, Alignment, StackAlignment);
  Objects.insert(Objects.begin(),
                 StackObject(Size, Alignment, SPOffset, IsImmutable,
                             /*isSpillSlot=*/false, /*Alloca=*/nullptr,
                             IsAliased));
  return -++NumFixedObjects;
}

int MachineFrameInfo::CreateFixedSpillStackObject(uint64_t Size,
                                                  int64_t SPOffset,
                                                  bool IsImmutable) {
  unsigned Alignment = MinAlign(SPOffset, ForcedRealign ? 1 : StackAlignment);
  Alignment = clampStackAlignment(!StackRealignable, Alignment, StackAlignment);
  Objects.insert(Objects.begin(),
                 StackObject(Size, Alignment, SPOffset, IsImmutable,
                             /*IsSpillSlot=*/true, /*Alloca=*/nullptr,
                             /*IsAliased=*/false));
  return -++NumFixedObjects;
}

BitVector MachineFrameInfo::getPristineRegs(const MachineFunction &MF) const {
  const TargetRegisterInfo *TRI = MF.getSubtarget().getRegisterInfo();
  BitVector BV(TRI->getNumRegs());

  // Before CSI is calculated, no registers are considered pristine. They can be
  // freely used and PEI will make sure they are saved.
  if (!isCalleeSavedInfoValid())
    return BV;

  const MachineRegisterInfo &MRI = MF.getRegInfo();
  for (const MCPhysReg *CSR = MRI.getCalleeSavedRegs(); CSR && *CSR;
       ++CSR)
    BV.set(*CSR);

  // Saved CSRs are not pristine.
  for (auto &I : getCalleeSavedInfo())
    for (MCSubRegIterator S(I.getReg(), TRI, true); S.isValid(); ++S)
      BV.reset(*S);

  return BV;
}

unsigned MachineFrameInfo::estimateStackSize(const MachineFunction &MF) const {
  const TargetFrameLowering *TFI = MF.getSubtarget().getFrameLowering();
  const TargetRegisterInfo *RegInfo = MF.getSubtarget().getRegisterInfo();
  unsigned MaxAlign = getMaxAlignment();
  int Offset = 0;

  // This code is very, very similar to PEI::calculateFrameObjectOffsets().
  // It really should be refactored to share code. Until then, changes
  // should keep in mind that there's tight coupling between the two.

  for (int i = getObjectIndexBegin(); i != 0; ++i) {
    int FixedOff = -getObjectOffset(i);
    if (FixedOff > Offset) Offset = FixedOff;
  }
  for (unsigned i = 0, e = getObjectIndexEnd(); i != e; ++i) {
    if (isDeadObjectIndex(i))
      continue;
    Offset += getObjectSize(i);
    unsigned Align = getObjectAlignment(i);
    // Adjust to alignment boundary
    Offset = (Offset+Align-1)/Align*Align;

    MaxAlign = std::max(Align, MaxAlign);
  }

  if (adjustsStack() && TFI->hasReservedCallFrame(MF))
    Offset += getMaxCallFrameSize();

  // Round up the size to a multiple of the alignment.  If the function has
  // any calls or alloca's, align to the target's StackAlignment value to
  // ensure that the callee's frame or the alloca data is suitably aligned;
  // otherwise, for leaf functions, align to the TransientStackAlignment
  // value.
  unsigned StackAlign;
  if (adjustsStack() || hasVarSizedObjects() ||
      (RegInfo->needsStackRealignment(MF) && getObjectIndexEnd() != 0))
    StackAlign = TFI->getStackAlignment();
  else
    StackAlign = TFI->getTransientStackAlignment();

  // If the frame pointer is eliminated, all frame offsets will be relative to
  // SP not FP. Align to MaxAlign so this works.
  StackAlign = std::max(StackAlign, MaxAlign);
  unsigned AlignMask = StackAlign - 1;
  Offset = (Offset + AlignMask) & ~uint64_t(AlignMask);

  return (unsigned)Offset;
}

void MachineFrameInfo::computeMaxCallFrameSize(const MachineFunction &MF) {
  const TargetInstrInfo &TII = *MF.getSubtarget().getInstrInfo();
  unsigned FrameSetupOpcode = TII.getCallFrameSetupOpcode();
  unsigned FrameDestroyOpcode = TII.getCallFrameDestroyOpcode();
  assert(FrameSetupOpcode != ~0u && FrameDestroyOpcode != ~0u &&
         "Can only compute MaxCallFrameSize if Setup/Destroy opcode are known");

  MaxCallFrameSize = 0;
  for (const MachineBasicBlock &MBB : MF) {
    for (const MachineInstr &MI : MBB) {
      unsigned Opcode = MI.getOpcode();
      if (Opcode == FrameSetupOpcode || Opcode == FrameDestroyOpcode) {
        unsigned Size = TII.getFrameSize(MI);
        MaxCallFrameSize = std::max(MaxCallFrameSize, Size);
        AdjustsStack = true;
      } else if (MI.isInlineAsm()) {
        // Some inline asm's need a stack frame, as indicated by operand 1.
        unsigned ExtraInfo = MI.getOperand(InlineAsm::MIOp_ExtraInfo).getImm();
        if (ExtraInfo & InlineAsm::Extra_IsAlignStack)
          AdjustsStack = true;
      }
    }
  }
}

void MachineFrameInfo::print(const MachineFunction &MF, raw_ostream &OS) const{
  if (Objects.empty()) return;

  const TargetFrameLowering *FI = MF.getSubtarget().getFrameLowering();
  int ValOffset = (FI ? FI->getOffsetOfLocalArea() : 0);

  OS << "Frame Objects:\n";

  for (unsigned i = 0, e = Objects.size(); i != e; ++i) {
    const StackObject &SO = Objects[i];
    OS << "  fi#" << (int)(i-NumFixedObjects) << ": ";

    if (SO.StackID != 0)
      OS << "id=" << static_cast<unsigned>(SO.StackID) << ' ';

    if (SO.Size == ~0ULL) {
      OS << "dead\n";
      continue;
    }
    if (SO.Size == 0)
      OS << "variable sized";
    else
      OS << "size=" << SO.Size;
    OS << ", align=" << SO.Alignment;

    if (i < NumFixedObjects)
      OS << ", fixed";
    if (i < NumFixedObjects || SO.SPOffset != -1) {
      int64_t Off = SO.SPOffset - ValOffset;
      OS << ", at location [SP";
      if (Off > 0)
        OS << "+" << Off;
      else if (Off < 0)
        OS << Off;
      OS << "]";
    }
    OS << "\n";
  }
}

#if !defined(NDEBUG) || defined(LLVM_ENABLE_DUMP)
LLVM_DUMP_METHOD void MachineFrameInfo::dump(const MachineFunction &MF) const {
  print(MF, dbgs());
}
#endif
