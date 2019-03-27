//===-- XCoreMCAsmInfo.h - XCore asm properties ----------------*- C++ -*--===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file contains the declaration of the XCoreMCAsmInfo class.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_XCORE_MCTARGETDESC_XCOREMCASMINFO_H
#define LLVM_LIB_TARGET_XCORE_MCTARGETDESC_XCOREMCASMINFO_H

#include "llvm/MC/MCAsmInfoELF.h"

namespace llvm {
class Triple;

class XCoreMCAsmInfo : public MCAsmInfoELF {
  void anchor() override;

public:
  explicit XCoreMCAsmInfo(const Triple &TT);
};

} // namespace llvm

#endif
