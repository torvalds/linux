//===-- MSP430MCAsmInfo.h - MSP430 asm properties --------------*- C++ -*--===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file contains the declaration of the MSP430MCAsmInfo class.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_MSP430_MCTARGETDESC_MSP430MCASMINFO_H
#define LLVM_LIB_TARGET_MSP430_MCTARGETDESC_MSP430MCASMINFO_H

#include "llvm/MC/MCAsmInfoELF.h"

namespace llvm {
class Triple;

class MSP430MCAsmInfo : public MCAsmInfoELF {
  void anchor() override;

public:
  explicit MSP430MCAsmInfo(const Triple &TT);
};

} // namespace llvm

#endif
