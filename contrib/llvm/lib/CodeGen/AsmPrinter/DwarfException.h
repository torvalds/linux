//===-- DwarfException.h - Dwarf Exception Framework -----------*- C++ -*--===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file contains support for writing dwarf exception info into asm files.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_CODEGEN_ASMPRINTER_DWARFEXCEPTION_H
#define LLVM_LIB_CODEGEN_ASMPRINTER_DWARFEXCEPTION_H

#include "EHStreamer.h"
#include "llvm/CodeGen/AsmPrinter.h"
#include "llvm/MC/MCDwarf.h"

namespace llvm {
class MachineFunction;
class ARMTargetStreamer;

class LLVM_LIBRARY_VISIBILITY DwarfCFIExceptionBase : public EHStreamer {
protected:
  DwarfCFIExceptionBase(AsmPrinter *A);

  /// Per-function flag to indicate if frame CFI info should be emitted.
  bool shouldEmitCFI;
  /// Per-module flag to indicate if .cfi_section has beeen emitted.
  bool hasEmittedCFISections;

  void markFunctionEnd() override;
  void endFragment() override;
};

class LLVM_LIBRARY_VISIBILITY DwarfCFIException : public DwarfCFIExceptionBase {
  /// Per-function flag to indicate if .cfi_personality should be emitted.
  bool shouldEmitPersonality;

  /// Per-function flag to indicate if .cfi_personality must be emitted.
  bool forceEmitPersonality;

  /// Per-function flag to indicate if .cfi_lsda should be emitted.
  bool shouldEmitLSDA;

  /// Per-function flag to indicate if frame moves info should be emitted.
  bool shouldEmitMoves;

public:
  //===--------------------------------------------------------------------===//
  // Main entry points.
  //
  DwarfCFIException(AsmPrinter *A);
  ~DwarfCFIException() override;

  /// Emit all exception information that should come after the content.
  void endModule() override;

  /// Gather pre-function exception information.  Assumes being emitted
  /// immediately after the function entry point.
  void beginFunction(const MachineFunction *MF) override;

  /// Gather and emit post-function exception information.
  void endFunction(const MachineFunction *) override;

  void beginFragment(const MachineBasicBlock *MBB,
                     ExceptionSymbolProvider ESP) override;
};

class LLVM_LIBRARY_VISIBILITY ARMException : public DwarfCFIExceptionBase {
  void emitTypeInfos(unsigned TTypeEncoding, MCSymbol *TTBaseLabel) override;
  ARMTargetStreamer &getTargetStreamer();

public:
  //===--------------------------------------------------------------------===//
  // Main entry points.
  //
  ARMException(AsmPrinter *A);
  ~ARMException() override;

  /// Emit all exception information that should come after the content.
  void endModule() override {}

  /// Gather pre-function exception information.  Assumes being emitted
  /// immediately after the function entry point.
  void beginFunction(const MachineFunction *MF) override;

  /// Gather and emit post-function exception information.
  void endFunction(const MachineFunction *) override;
};
} // End of namespace llvm

#endif
