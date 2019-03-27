//===- X86TargetStreamer.h ------------------------------*- C++ -*---------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_X86_MCTARGETDESC_X86TARGETSTREAMER_H
#define LLVM_LIB_TARGET_X86_MCTARGETDESC_X86TARGETSTREAMER_H

#include "llvm/MC/MCStreamer.h"

namespace llvm {

/// X86 target streamer implementing x86-only assembly directives.
class X86TargetStreamer : public MCTargetStreamer {
public:
  X86TargetStreamer(MCStreamer &S) : MCTargetStreamer(S) {}

  virtual bool emitFPOProc(const MCSymbol *ProcSym, unsigned ParamsSize,
                           SMLoc L = {}) = 0;
  virtual bool emitFPOEndPrologue(SMLoc L = {}) = 0;
  virtual bool emitFPOEndProc(SMLoc L = {}) = 0;
  virtual bool emitFPOData(const MCSymbol *ProcSym, SMLoc L = {}) = 0;
  virtual bool emitFPOPushReg(unsigned Reg, SMLoc L = {}) = 0;
  virtual bool emitFPOStackAlloc(unsigned StackAlloc, SMLoc L = {}) = 0;
  virtual bool emitFPOStackAlign(unsigned Align, SMLoc L = {}) = 0;
  virtual bool emitFPOSetFrame(unsigned Reg, SMLoc L = {}) = 0;
};

} // end namespace llvm

#endif
