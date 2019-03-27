//===-- X86AsmPrinter.h - X86 implementation of AsmPrinter ------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_X86_X86ASMPRINTER_H
#define LLVM_LIB_TARGET_X86_X86ASMPRINTER_H

#include "X86Subtarget.h"
#include "llvm/CodeGen/AsmPrinter.h"
#include "llvm/CodeGen/FaultMaps.h"
#include "llvm/CodeGen/StackMaps.h"
#include "llvm/MC/MCCodeEmitter.h"
#include "llvm/Target/TargetMachine.h"

// Implemented in X86MCInstLower.cpp
namespace {
  class X86MCInstLower;
}

namespace llvm {
class MCStreamer;
class MCSymbol;

class LLVM_LIBRARY_VISIBILITY X86AsmPrinter : public AsmPrinter {
  const X86Subtarget *Subtarget;
  StackMaps SM;
  FaultMaps FM;
  std::unique_ptr<MCCodeEmitter> CodeEmitter;
  bool EmitFPOData = false;
  bool NeedsRetpoline = false;

  // This utility class tracks the length of a stackmap instruction's 'shadow'.
  // It is used by the X86AsmPrinter to ensure that the stackmap shadow
  // invariants (i.e. no other stackmaps, patchpoints, or control flow within
  // the shadow) are met, while outputting a minimal number of NOPs for padding.
  //
  // To minimise the number of NOPs used, the shadow tracker counts the number
  // of instruction bytes output since the last stackmap. Only if there are too
  // few instruction bytes to cover the shadow are NOPs used for padding.
  class StackMapShadowTracker {
  public:
    void startFunction(MachineFunction &MF) {
      this->MF = &MF;
    }
    void count(MCInst &Inst, const MCSubtargetInfo &STI,
               MCCodeEmitter *CodeEmitter);

    // Called to signal the start of a shadow of RequiredSize bytes.
    void reset(unsigned RequiredSize) {
      RequiredShadowSize = RequiredSize;
      CurrentShadowSize = 0;
      InShadow = true;
    }

    // Called before every stackmap/patchpoint, and at the end of basic blocks,
    // to emit any necessary padding-NOPs.
    void emitShadowPadding(MCStreamer &OutStreamer, const MCSubtargetInfo &STI);
  private:
    const MachineFunction *MF;
    bool InShadow = false;

    // RequiredShadowSize holds the length of the shadow specified in the most
    // recently encountered STACKMAP instruction.
    // CurrentShadowSize counts the number of bytes encoded since the most
    // recently encountered STACKMAP, stopping when that number is greater than
    // or equal to RequiredShadowSize.
    unsigned RequiredShadowSize = 0, CurrentShadowSize = 0;
  };

  StackMapShadowTracker SMShadowTracker;

  // All instructions emitted by the X86AsmPrinter should use this helper
  // method.
  //
  // This helper function invokes the SMShadowTracker on each instruction before
  // outputting it to the OutStream. This allows the shadow tracker to minimise
  // the number of NOPs used for stackmap padding.
  void EmitAndCountInstruction(MCInst &Inst);
  void LowerSTACKMAP(const MachineInstr &MI);
  void LowerPATCHPOINT(const MachineInstr &MI, X86MCInstLower &MCIL);
  void LowerSTATEPOINT(const MachineInstr &MI, X86MCInstLower &MCIL);
  void LowerFAULTING_OP(const MachineInstr &MI, X86MCInstLower &MCIL);
  void LowerPATCHABLE_OP(const MachineInstr &MI, X86MCInstLower &MCIL);

  void LowerTlsAddr(X86MCInstLower &MCInstLowering, const MachineInstr &MI);

  // XRay-specific lowering for X86.
  void LowerPATCHABLE_FUNCTION_ENTER(const MachineInstr &MI,
                                     X86MCInstLower &MCIL);
  void LowerPATCHABLE_RET(const MachineInstr &MI, X86MCInstLower &MCIL);
  void LowerPATCHABLE_TAIL_CALL(const MachineInstr &MI, X86MCInstLower &MCIL);
  void LowerPATCHABLE_EVENT_CALL(const MachineInstr &MI, X86MCInstLower &MCIL);
  void LowerPATCHABLE_TYPED_EVENT_CALL(const MachineInstr &MI,
                                       X86MCInstLower &MCIL);

  void LowerFENTRY_CALL(const MachineInstr &MI, X86MCInstLower &MCIL);

  // Choose between emitting .seh_ directives and .cv_fpo_ directives.
  void EmitSEHInstruction(const MachineInstr *MI);

public:
  X86AsmPrinter(TargetMachine &TM, std::unique_ptr<MCStreamer> Streamer);

  StringRef getPassName() const override {
    return "X86 Assembly Printer";
  }

  const X86Subtarget &getSubtarget() const { return *Subtarget; }

  void EmitStartOfAsmFile(Module &M) override;

  void EmitEndOfAsmFile(Module &M) override;

  void EmitInstruction(const MachineInstr *MI) override;

  void EmitBasicBlockEnd(const MachineBasicBlock &MBB) override {
    AsmPrinter::EmitBasicBlockEnd(MBB);
    SMShadowTracker.emitShadowPadding(*OutStreamer, getSubtargetInfo());
  }

  bool PrintAsmOperand(const MachineInstr *MI, unsigned OpNo,
                       unsigned AsmVariant, const char *ExtraCode,
                       raw_ostream &OS) override;
  bool PrintAsmMemoryOperand(const MachineInstr *MI, unsigned OpNo,
                             unsigned AsmVariant, const char *ExtraCode,
                             raw_ostream &OS) override;

  bool doInitialization(Module &M) override {
    SMShadowTracker.reset(0);
    SM.reset();
    FM.reset();
    return AsmPrinter::doInitialization(M);
  }

  bool runOnMachineFunction(MachineFunction &F) override;
  void EmitFunctionBodyStart() override;
  void EmitFunctionBodyEnd() override;
};

} // end namespace llvm

#endif
