//=====-- LanaiMCAsmInfo.h - Lanai asm properties -----------*- C++ -*--====//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file contains the declaration of the LanaiMCAsmInfo class.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_LANAI_MCTARGETDESC_LANAIMCASMINFO_H
#define LLVM_LIB_TARGET_LANAI_MCTARGETDESC_LANAIMCASMINFO_H

#include "llvm/MC/MCAsmInfoELF.h"

namespace llvm {
class Triple;

class LanaiMCAsmInfo : public MCAsmInfoELF {
  void anchor() override;

public:
  explicit LanaiMCAsmInfo(const Triple &TheTriple);
};

} // namespace llvm

#endif // LLVM_LIB_TARGET_LANAI_MCTARGETDESC_LANAIMCASMINFO_H
