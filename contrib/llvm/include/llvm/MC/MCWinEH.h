//===- MCWinEH.h - Windows Unwinding Support --------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_MC_MCWINEH_H
#define LLVM_MC_MCWINEH_H

#include "llvm/ADT/MapVector.h"
#include <vector>

namespace llvm {
class MCSection;
class MCStreamer;
class MCSymbol;

namespace WinEH {
struct Instruction {
  const MCSymbol *Label;
  unsigned Offset;
  unsigned Register;
  unsigned Operation;

  Instruction(unsigned Op, MCSymbol *L, unsigned Reg, unsigned Off)
    : Label(L), Offset(Off), Register(Reg), Operation(Op) {}
};

struct FrameInfo {
  const MCSymbol *Begin = nullptr;
  const MCSymbol *End = nullptr;
  const MCSymbol *FuncletOrFuncEnd = nullptr;
  const MCSymbol *ExceptionHandler = nullptr;
  const MCSymbol *Function = nullptr;
  const MCSymbol *PrologEnd = nullptr;
  const MCSymbol *Symbol = nullptr;
  const MCSection *TextSection = nullptr;

  bool HandlesUnwind = false;
  bool HandlesExceptions = false;

  int LastFrameInst = -1;
  const FrameInfo *ChainedParent = nullptr;
  std::vector<Instruction> Instructions;
  MapVector<MCSymbol*, std::vector<Instruction>> EpilogMap;

  FrameInfo() = default;
  FrameInfo(const MCSymbol *Function, const MCSymbol *BeginFuncEHLabel)
      : Begin(BeginFuncEHLabel), Function(Function) {}
  FrameInfo(const MCSymbol *Function, const MCSymbol *BeginFuncEHLabel,
            const FrameInfo *ChainedParent)
      : Begin(BeginFuncEHLabel), Function(Function),
        ChainedParent(ChainedParent) {}
};

class UnwindEmitter {
public:
  virtual ~UnwindEmitter();

  /// This emits the unwind info sections (.pdata and .xdata in PE/COFF).
  virtual void Emit(MCStreamer &Streamer) const = 0;
  virtual void EmitUnwindInfo(MCStreamer &Streamer, FrameInfo *FI) const = 0;
};
}
}

#endif
