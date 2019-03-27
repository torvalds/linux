//===-- WasmException.h - Wasm Exception Framework -------------*- C++ -*--===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file contains support for writing WebAssembly exception info into asm
// files.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_CODEGEN_ASMPRINTER_WASMEXCEPTION_H
#define LLVM_LIB_CODEGEN_ASMPRINTER_WASMEXCEPTION_H

#include "EHStreamer.h"
#include "llvm/CodeGen/AsmPrinter.h"

namespace llvm {

class LLVM_LIBRARY_VISIBILITY WasmException : public EHStreamer {
public:
  WasmException(AsmPrinter *A) : EHStreamer(A) {}

  void endModule() override;
  void beginFunction(const MachineFunction *MF) override {}
  virtual void markFunctionEnd() override;
  void endFunction(const MachineFunction *MF) override;

protected:
  // Compute the call site table for wasm EH.
  void computeCallSiteTable(
      SmallVectorImpl<CallSiteEntry> &CallSites,
      const SmallVectorImpl<const LandingPadInfo *> &LandingPads,
      const SmallVectorImpl<unsigned> &FirstActions) override;
};

} // End of namespace llvm

#endif
