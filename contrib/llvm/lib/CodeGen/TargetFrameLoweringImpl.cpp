//===- TargetFrameLoweringImpl.cpp - Implement target frame interface ------==//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// Implements the layout of a stack frame on the target machine.
//
//===----------------------------------------------------------------------===//

#include "llvm/ADT/BitVector.h"
#include "llvm/CodeGen/MachineFrameInfo.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/CodeGen/TargetFrameLowering.h"
#include "llvm/CodeGen/TargetRegisterInfo.h"
#include "llvm/CodeGen/TargetSubtargetInfo.h"
#include "llvm/IR/Attributes.h"
#include "llvm/IR/CallingConv.h"
#include "llvm/IR/Function.h"
#include "llvm/MC/MCRegisterInfo.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Target/TargetMachine.h"
#include "llvm/Target/TargetOptions.h"

using namespace llvm;

TargetFrameLowering::~TargetFrameLowering() = default;

bool TargetFrameLowering::enableCalleeSaveSkip(const MachineFunction &MF) const {
  assert(MF.getFunction().hasFnAttribute(Attribute::NoReturn) &&
         MF.getFunction().hasFnAttribute(Attribute::NoUnwind) &&
         !MF.getFunction().hasFnAttribute(Attribute::UWTable));
  return false;
}

/// Returns the displacement from the frame register to the stack
/// frame of the specified index, along with the frame register used
/// (in output arg FrameReg). This is the default implementation which
/// is overridden for some targets.
int TargetFrameLowering::getFrameIndexReference(const MachineFunction &MF,
                                             int FI, unsigned &FrameReg) const {
  const MachineFrameInfo &MFI = MF.getFrameInfo();
  const TargetRegisterInfo *RI = MF.getSubtarget().getRegisterInfo();

  // By default, assume all frame indices are referenced via whatever
  // getFrameRegister() says. The target can override this if it's doing
  // something different.
  FrameReg = RI->getFrameRegister(MF);

  return MFI.getObjectOffset(FI) + MFI.getStackSize() -
         getOffsetOfLocalArea() + MFI.getOffsetAdjustment();
}

bool TargetFrameLowering::needsFrameIndexResolution(
    const MachineFunction &MF) const {
  return MF.getFrameInfo().hasStackObjects();
}

void TargetFrameLowering::determineCalleeSaves(MachineFunction &MF,
                                               BitVector &SavedRegs,
                                               RegScavenger *RS) const {
  const TargetRegisterInfo &TRI = *MF.getSubtarget().getRegisterInfo();

  // Resize before the early returns. Some backends expect that
  // SavedRegs.size() == TRI.getNumRegs() after this call even if there are no
  // saved registers.
  SavedRegs.resize(TRI.getNumRegs());

  // When interprocedural register allocation is enabled caller saved registers
  // are preferred over callee saved registers.
  if (MF.getTarget().Options.EnableIPRA && isSafeForNoCSROpt(MF.getFunction()))
    return;

  // Get the callee saved register list...
  const MCPhysReg *CSRegs = MF.getRegInfo().getCalleeSavedRegs();

  // Early exit if there are no callee saved registers.
  if (!CSRegs || CSRegs[0] == 0)
    return;

  // In Naked functions we aren't going to save any registers.
  if (MF.getFunction().hasFnAttribute(Attribute::Naked))
    return;

  // Noreturn+nounwind functions never restore CSR, so no saves are needed.
  // Purely noreturn functions may still return through throws, so those must
  // save CSR for caller exception handlers.
  //
  // If the function uses longjmp to break out of its current path of
  // execution we do not need the CSR spills either: setjmp stores all CSRs
  // it was called with into the jmp_buf, which longjmp then restores.
  if (MF.getFunction().hasFnAttribute(Attribute::NoReturn) &&
        MF.getFunction().hasFnAttribute(Attribute::NoUnwind) &&
        !MF.getFunction().hasFnAttribute(Attribute::UWTable) &&
        enableCalleeSaveSkip(MF))
    return;

  // Functions which call __builtin_unwind_init get all their registers saved.
  bool CallsUnwindInit = MF.callsUnwindInit();
  const MachineRegisterInfo &MRI = MF.getRegInfo();
  for (unsigned i = 0; CSRegs[i]; ++i) {
    unsigned Reg = CSRegs[i];
    if (CallsUnwindInit || MRI.isPhysRegModified(Reg))
      SavedRegs.set(Reg);
  }
}

unsigned TargetFrameLowering::getStackAlignmentSkew(
    const MachineFunction &MF) const {
  // When HHVM function is called, the stack is skewed as the return address
  // is removed from the stack before we enter the function.
  if (LLVM_UNLIKELY(MF.getFunction().getCallingConv() == CallingConv::HHVM))
    return MF.getTarget().getAllocaPointerSize();

  return 0;
}

int TargetFrameLowering::getInitialCFAOffset(const MachineFunction &MF) const {
  llvm_unreachable("getInitialCFAOffset() not implemented!");
}

unsigned TargetFrameLowering::getInitialCFARegister(const MachineFunction &MF)
    const {
  llvm_unreachable("getInitialCFARegister() not implemented!");
}