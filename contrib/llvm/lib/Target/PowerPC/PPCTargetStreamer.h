//===- PPCTargetStreamer.h - PPC Target Streamer ----------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_POWERPC_PPCTARGETSTREAMER_H
#define LLVM_LIB_TARGET_POWERPC_PPCTARGETSTREAMER_H

#include "llvm/ADT/StringRef.h"
#include "llvm/MC/MCStreamer.h"

namespace llvm {

class MCExpr;
class MCSymbol;
class MCSymbolELF;

class PPCTargetStreamer : public MCTargetStreamer {
public:
  PPCTargetStreamer(MCStreamer &S);
  ~PPCTargetStreamer() override;

  virtual void emitTCEntry(const MCSymbol &S) = 0;
  virtual void emitMachine(StringRef CPU) = 0;
  virtual void emitAbiVersion(int AbiVersion) = 0;
  virtual void emitLocalEntry(MCSymbolELF *S, const MCExpr *LocalOffset) = 0;
};

} // end namespace llvm

#endif // LLVM_LIB_TARGET_POWERPC_PPCTARGETSTREAMER_H
