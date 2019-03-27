//===- MCAsmInfoDarwin.h - Darwin asm properties ----------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file defines target asm properties related what form asm statements
// should take in general on Darwin-based targets
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_MC_MCASMINFODARWIN_H
#define LLVM_MC_MCASMINFODARWIN_H

#include "llvm/MC/MCAsmInfo.h"

namespace llvm {

class MCAsmInfoDarwin : public MCAsmInfo {
public:
  explicit MCAsmInfoDarwin();

  bool isSectionAtomizableBySymbols(const MCSection &Section) const override;
};

} // end namespace llvm

#endif // LLVM_MC_MCASMINFODARWIN_H
