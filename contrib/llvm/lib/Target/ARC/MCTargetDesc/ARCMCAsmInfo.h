//===- ARCMCAsmInfo.h - ARC asm properties ----------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file contains the declaration of the ARCMCAsmInfo class.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_ARC_MCTARGETDESC_ARCMCASMINFO_H
#define LLVM_LIB_TARGET_ARC_MCTARGETDESC_ARCMCASMINFO_H

#include "llvm/MC/MCAsmInfoELF.h"

namespace llvm {

class Triple;

class ARCMCAsmInfo : public MCAsmInfoELF {
  void anchor() override;

public:
  explicit ARCMCAsmInfo(const Triple &TT);
};

} // end namespace llvm

#endif // LLVM_LIB_TARGET_ARC_MCTARGETDESC_ARCMCASMINFO_H
