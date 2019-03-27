//===---- X86FixupSetCC.cpp - optimize usage of LEA instructions ----------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file defines a pass that fixes zero-extension of setcc patterns.
// X86 setcc instructions are modeled to have no input arguments, and a single
// GR8 output argument. This is consistent with other similar instructions
// (e.g. movb), but means it is impossible to directly generate a setcc into
// the lower GR8 of a specified GR32.
// This means that ISel must select (zext (setcc)) into something like
// seta %al; movzbl %al, %eax.
// Unfortunately, this can cause a stall due to the partial register write
// performed by the setcc. Instead, we can use:
// xor %eax, %eax; seta %al
// This both avoids the stall, and encodes shorter.
//===----------------------------------------------------------------------===//

#include "X86.h"
#include "X86InstrInfo.h"
#include "X86Subtarget.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/CodeGen/MachineFunctionPass.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"

using namespace llvm;

#define DEBUG_TYPE "x86-fixup-setcc"

STATISTIC(NumSubstZexts, "Number of setcc + zext pairs substituted");

namespace {
class X86FixupSetCCPass : public MachineFunctionPass {
public:
  X86FixupSetCCPass() : MachineFunctionPass(ID) {}

  StringRef getPassName() const override { return "X86 Fixup SetCC"; }

  bool runOnMachineFunction(MachineFunction &MF) override;

private:
  // Find the preceding instruction that imp-defs eflags.
  MachineInstr *findFlagsImpDef(MachineBasicBlock *MBB,
                                MachineBasicBlock::reverse_iterator MI);

  // Return true if MI imp-uses eflags.
  bool impUsesFlags(MachineInstr *MI);

  // Return true if this is the opcode of a SetCC instruction with a register
  // output.
  bool isSetCCr(unsigned Opode);

  MachineRegisterInfo *MRI;
  const X86InstrInfo *TII;

  enum { SearchBound = 16 };

  static char ID;
};

char X86FixupSetCCPass::ID = 0;
}

FunctionPass *llvm::createX86FixupSetCC() { return new X86FixupSetCCPass(); }

bool X86FixupSetCCPass::isSetCCr(unsigned Opcode) {
  switch (Opcode) {
  default:
    return false;
  case X86::SETOr:
  case X86::SETNOr:
  case X86::SETBr:
  case X86::SETAEr:
  case X86::SETEr:
  case X86::SETNEr:
  case X86::SETBEr:
  case X86::SETAr:
  case X86::SETSr:
  case X86::SETNSr:
  case X86::SETPr:
  case X86::SETNPr:
  case X86::SETLr:
  case X86::SETGEr:
  case X86::SETLEr:
  case X86::SETGr:
    return true;
  }
}

// We expect the instruction *immediately* before the setcc to imp-def
// EFLAGS (because of scheduling glue). To make this less brittle w.r.t
// scheduling, look backwards until we hit the beginning of the
// basic-block, or a small bound (to avoid quadratic behavior).
MachineInstr *
X86FixupSetCCPass::findFlagsImpDef(MachineBasicBlock *MBB,
                                   MachineBasicBlock::reverse_iterator MI) {
  // FIXME: Should this be instr_rend(), and MI be reverse_instr_iterator?
  auto MBBStart = MBB->rend();
  for (int i = 0; (i < SearchBound) && (MI != MBBStart); ++i, ++MI)
    for (auto &Op : MI->implicit_operands())
      if ((Op.getReg() == X86::EFLAGS) && (Op.isDef()))
        return &*MI;

  return nullptr;
}

bool X86FixupSetCCPass::impUsesFlags(MachineInstr *MI) {
  for (auto &Op : MI->implicit_operands())
    if ((Op.getReg() == X86::EFLAGS) && (Op.isUse()))
      return true;

  return false;
}

bool X86FixupSetCCPass::runOnMachineFunction(MachineFunction &MF) {
  bool Changed = false;
  MRI = &MF.getRegInfo();
  TII = MF.getSubtarget<X86Subtarget>().getInstrInfo();

  SmallVector<MachineInstr*, 4> ToErase;

  for (auto &MBB : MF) {
    for (auto &MI : MBB) {
      // Find a setcc that is used by a zext.
      // This doesn't have to be the only use, the transformation is safe
      // regardless.
      if (!isSetCCr(MI.getOpcode()))
        continue;

      MachineInstr *ZExt = nullptr;
      for (auto &Use : MRI->use_instructions(MI.getOperand(0).getReg()))
        if (Use.getOpcode() == X86::MOVZX32rr8)
          ZExt = &Use;

      if (!ZExt)
        continue;

      // Find the preceding instruction that imp-defs eflags.
      MachineInstr *FlagsDefMI = findFlagsImpDef(
          MI.getParent(), MachineBasicBlock::reverse_iterator(&MI));
      if (!FlagsDefMI)
        continue;

      // We'd like to put something that clobbers eflags directly before
      // FlagsDefMI. This can't hurt anything after FlagsDefMI, because
      // it, itself, by definition, clobbers eflags. But it may happen that
      // FlagsDefMI also *uses* eflags, in which case the transformation is
      // invalid.
      if (impUsesFlags(FlagsDefMI))
        continue;

      ++NumSubstZexts;
      Changed = true;

      // On 32-bit, we need to be careful to force an ABCD register.
      const TargetRegisterClass *RC = MF.getSubtarget<X86Subtarget>().is64Bit()
                                          ? &X86::GR32RegClass
                                          : &X86::GR32_ABCDRegClass;
      unsigned ZeroReg = MRI->createVirtualRegister(RC);
      unsigned InsertReg = MRI->createVirtualRegister(RC);

      // Initialize a register with 0. This must go before the eflags def
      BuildMI(MBB, FlagsDefMI, MI.getDebugLoc(), TII->get(X86::MOV32r0),
              ZeroReg);

      // X86 setcc only takes an output GR8, so fake a GR32 input by inserting
      // the setcc result into the low byte of the zeroed register.
      BuildMI(*ZExt->getParent(), ZExt, ZExt->getDebugLoc(),
              TII->get(X86::INSERT_SUBREG), InsertReg)
          .addReg(ZeroReg)
          .addReg(MI.getOperand(0).getReg())
          .addImm(X86::sub_8bit);
      MRI->replaceRegWith(ZExt->getOperand(0).getReg(), InsertReg);
      ToErase.push_back(ZExt);
    }
  }

  for (auto &I : ToErase)
    I->eraseFromParent();

  return Changed;
}
