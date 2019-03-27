//===-- WinException.h - Windows Exception Handling ----------*- C++ -*--===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file contains support for writing windows exception info into asm files.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_CODEGEN_ASMPRINTER_WIN64EXCEPTION_H
#define LLVM_LIB_CODEGEN_ASMPRINTER_WIN64EXCEPTION_H

#include "EHStreamer.h"

namespace llvm {
class Function;
class GlobalValue;
class MachineFunction;
class MCExpr;
class MCSection;
class Value;
struct WinEHFuncInfo;

class LLVM_LIBRARY_VISIBILITY WinException : public EHStreamer {
  /// Per-function flag to indicate if personality info should be emitted.
  bool shouldEmitPersonality = false;

  /// Per-function flag to indicate if the LSDA should be emitted.
  bool shouldEmitLSDA = false;

  /// Per-function flag to indicate if frame moves info should be emitted.
  bool shouldEmitMoves = false;

  /// True if this is a 64-bit target and we should use image relative offsets.
  bool useImageRel32 = false;

  /// True if we are generating exception handling on Windows for ARM64.
  bool isAArch64 = false;

  /// Pointer to the current funclet entry BB.
  const MachineBasicBlock *CurrentFuncletEntry = nullptr;

  /// The section of the last funclet start.
  MCSection *CurrentFuncletTextSection = nullptr;

  void emitCSpecificHandlerTable(const MachineFunction *MF);

  void emitSEHActionsForRange(const WinEHFuncInfo &FuncInfo,
                              const MCSymbol *BeginLabel,
                              const MCSymbol *EndLabel, int State);

  /// Emit the EH table data for 32-bit and 64-bit functions using
  /// the __CxxFrameHandler3 personality.
  void emitCXXFrameHandler3Table(const MachineFunction *MF);

  /// Emit the EH table data for _except_handler3 and _except_handler4
  /// personality functions. These are only used on 32-bit and do not use CFI
  /// tables.
  void emitExceptHandlerTable(const MachineFunction *MF);

  void emitCLRExceptionTable(const MachineFunction *MF);

  void computeIP2StateTable(
      const MachineFunction *MF, const WinEHFuncInfo &FuncInfo,
      SmallVectorImpl<std::pair<const MCExpr *, int>> &IPToStateTable);

  /// Emits the label used with llvm.eh.recoverfp, which is used by
  /// outlined funclets.
  void emitEHRegistrationOffsetLabel(const WinEHFuncInfo &FuncInfo,
                                     StringRef FLinkageName);

  const MCExpr *create32bitRef(const MCSymbol *Value);
  const MCExpr *create32bitRef(const GlobalValue *GV);
  const MCExpr *getLabel(const MCSymbol *Label);
  const MCExpr *getOffset(const MCSymbol *OffsetOf, const MCSymbol *OffsetFrom);
  const MCExpr *getOffsetPlusOne(const MCSymbol *OffsetOf,
                                 const MCSymbol *OffsetFrom);

  /// Gets the offset that we should use in a table for a stack object with the
  /// given index. For targets using CFI (Win64, etc), this is relative to the
  /// established SP at the end of the prologue. For targets without CFI (Win32
  /// only), it is relative to the frame pointer.
  int getFrameIndexOffset(int FrameIndex, const WinEHFuncInfo &FuncInfo);

public:
  //===--------------------------------------------------------------------===//
  // Main entry points.
  //
  WinException(AsmPrinter *A);
  ~WinException() override;

  /// Emit all exception information that should come after the content.
  void endModule() override;

  /// Gather pre-function exception information.  Assumes being emitted
  /// immediately after the function entry point.
  void beginFunction(const MachineFunction *MF) override;

  /// Gather and emit post-function exception information.
  void endFunction(const MachineFunction *) override;

  /// Emit target-specific EH funclet machinery.
  void beginFunclet(const MachineBasicBlock &MBB, MCSymbol *Sym) override;
  void endFunclet() override;
};
}

#endif

