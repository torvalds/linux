//=====-- AArch64MCAsmInfo.h - AArch64 asm properties ---------*- C++ -*--====//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file contains the declaration of the AArch64MCAsmInfo class.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_AARCH64_MCTARGETDESC_AARCH64MCASMINFO_H
#define LLVM_LIB_TARGET_AARCH64_MCTARGETDESC_AARCH64MCASMINFO_H

#include "llvm/MC/MCAsmInfoCOFF.h"
#include "llvm/MC/MCAsmInfoDarwin.h"
#include "llvm/MC/MCAsmInfoELF.h"

namespace llvm {
class MCStreamer;
class Target;
class Triple;

struct AArch64MCAsmInfoDarwin : public MCAsmInfoDarwin {
  explicit AArch64MCAsmInfoDarwin();
  const MCExpr *
  getExprForPersonalitySymbol(const MCSymbol *Sym, unsigned Encoding,
                              MCStreamer &Streamer) const override;
};

struct AArch64MCAsmInfoELF : public MCAsmInfoELF {
  explicit AArch64MCAsmInfoELF(const Triple &T);
};

struct AArch64MCAsmInfoMicrosoftCOFF : public MCAsmInfoMicrosoft {
  explicit AArch64MCAsmInfoMicrosoftCOFF();
};

struct AArch64MCAsmInfoGNUCOFF : public MCAsmInfoGNUCOFF {
  explicit AArch64MCAsmInfoGNUCOFF();
};

} // namespace llvm

#endif
