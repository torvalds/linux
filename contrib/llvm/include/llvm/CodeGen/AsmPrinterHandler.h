//===-- llvm/CodeGen/AsmPrinterHandler.h -----------------------*- C++ -*--===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file contains a generic interface for AsmPrinter handlers,
// like debug and EH info emitters.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CODEGEN_ASMPRINTERHANDLER_H
#define LLVM_CODEGEN_ASMPRINTERHANDLER_H

#include "llvm/Support/DataTypes.h"

namespace llvm {

class AsmPrinter;
class MachineBasicBlock;
class MachineFunction;
class MachineInstr;
class MCSymbol;

typedef MCSymbol *ExceptionSymbolProvider(AsmPrinter *Asm);

/// Collects and handles AsmPrinter objects required to build debug
/// or EH information.
class AsmPrinterHandler {
public:
  virtual ~AsmPrinterHandler();

  /// For symbols that have a size designated (e.g. common symbols),
  /// this tracks that size.
  virtual void setSymbolSize(const MCSymbol *Sym, uint64_t Size) = 0;

  /// Emit all sections that should come after the content.
  virtual void endModule() = 0;

  /// Gather pre-function debug information.
  /// Every beginFunction(MF) call should be followed by an endFunction(MF)
  /// call.
  virtual void beginFunction(const MachineFunction *MF) = 0;

  // Emit any of function marker (like .cfi_endproc). This is called
  // before endFunction and cannot switch sections.
  virtual void markFunctionEnd();

  /// Gather post-function debug information.
  /// Please note that some AsmPrinter implementations may not call
  /// beginFunction at all.
  virtual void endFunction(const MachineFunction *MF) = 0;

  virtual void beginFragment(const MachineBasicBlock *MBB,
                             ExceptionSymbolProvider ESP) {}
  virtual void endFragment() {}

  /// Emit target-specific EH funclet machinery.
  virtual void beginFunclet(const MachineBasicBlock &MBB,
                            MCSymbol *Sym = nullptr) {}
  virtual void endFunclet() {}

  /// Process beginning of an instruction.
  virtual void beginInstruction(const MachineInstr *MI) = 0;

  /// Process end of an instruction.
  virtual void endInstruction() = 0;
};
} // End of namespace llvm

#endif
