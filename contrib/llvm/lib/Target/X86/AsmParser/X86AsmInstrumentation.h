//===- X86AsmInstrumentation.h - Instrument X86 inline assembly -*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_X86_ASMPARSER_X86ASMINSTRUMENTATION_H
#define LLVM_LIB_TARGET_X86_ASMPARSER_X86ASMINSTRUMENTATION_H

#include "llvm/ADT/SmallVector.h"
#include <memory>

namespace llvm {

class MCContext;
class MCInst;
class MCInstrInfo;
class MCParsedAsmOperand;
class MCStreamer;
class MCSubtargetInfo;
class MCTargetOptions;
class X86AsmInstrumentation;

X86AsmInstrumentation *
CreateX86AsmInstrumentation(const MCTargetOptions &MCOptions,
                            const MCContext &Ctx,
                            const MCSubtargetInfo *&STI);

class X86AsmInstrumentation {
public:
  virtual ~X86AsmInstrumentation();

  // Sets frame register corresponding to a current frame.
  void SetInitialFrameRegister(unsigned RegNo) {
    InitialFrameReg = RegNo;
  }

  // Tries to instrument and emit instruction.
  virtual void InstrumentAndEmitInstruction(
      const MCInst &Inst,
      SmallVectorImpl<std::unique_ptr<MCParsedAsmOperand>> &Operands,
      MCContext &Ctx, const MCInstrInfo &MII, MCStreamer &Out,
      bool PrintSchedInfoEnabled);

protected:
  friend X86AsmInstrumentation *
  CreateX86AsmInstrumentation(const MCTargetOptions &MCOptions,
                              const MCContext &Ctx,
                              const MCSubtargetInfo *&STI);

  X86AsmInstrumentation(const MCSubtargetInfo *&STI);

  unsigned GetFrameRegGeneric(const MCContext &Ctx, MCStreamer &Out);

  void EmitInstruction(MCStreamer &Out, const MCInst &Inst,
                       bool PrintSchedInfoEnabled = false);

  const MCSubtargetInfo *&STI;

  unsigned InitialFrameReg = 0;
};

} // end namespace llvm

#endif // LLVM_LIB_TARGET_X86_ASMPARSER_X86ASMINSTRUMENTATION_H
