//===-- llvm/MC/MCExternalSymbolizer.h - ------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file contains the declaration of the MCExternalSymbolizer class, which
// enables library users to provide callbacks (through the C API) to do the
// symbolization externally.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_MC_MCDISASSEMBLER_MCEXTERNALSYMBOLIZER_H
#define LLVM_MC_MCDISASSEMBLER_MCEXTERNALSYMBOLIZER_H

#include "llvm-c/Disassembler.h"
#include "llvm/MC/MCDisassembler/MCSymbolizer.h"
#include <memory>

namespace llvm {

/// Symbolize using user-provided, C API, callbacks.
///
/// See llvm-c/Disassembler.h.
class MCExternalSymbolizer : public MCSymbolizer {
protected:
  /// \name Hooks for symbolic disassembly via the public 'C' interface.
  /// @{
  /// The function to get the symbolic information for operands.
  LLVMOpInfoCallback GetOpInfo;
  /// The function to lookup a symbol name.
  LLVMSymbolLookupCallback SymbolLookUp;
  /// The pointer to the block of symbolic information for above call back.
  void *DisInfo;
  /// @}

public:
  MCExternalSymbolizer(MCContext &Ctx,
                       std::unique_ptr<MCRelocationInfo> RelInfo,
                       LLVMOpInfoCallback getOpInfo,
                       LLVMSymbolLookupCallback symbolLookUp, void *disInfo)
    : MCSymbolizer(Ctx, std::move(RelInfo)), GetOpInfo(getOpInfo),
      SymbolLookUp(symbolLookUp), DisInfo(disInfo) {}

  bool tryAddingSymbolicOperand(MCInst &MI, raw_ostream &CommentStream,
                                int64_t Value, uint64_t Address, bool IsBranch,
                                uint64_t Offset, uint64_t InstSize) override;
  void tryAddingPcLoadReferenceComment(raw_ostream &CommentStream,
                                       int64_t Value,
                                       uint64_t Address) override;
};

}

#endif
