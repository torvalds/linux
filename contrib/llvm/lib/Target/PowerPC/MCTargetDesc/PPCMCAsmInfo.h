//===-- PPCMCAsmInfo.h - PPC asm properties --------------------*- C++ -*--===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file contains the declaration of the MCAsmInfoDarwin class.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_POWERPC_MCTARGETDESC_PPCMCASMINFO_H
#define LLVM_LIB_TARGET_POWERPC_MCTARGETDESC_PPCMCASMINFO_H

#include "llvm/MC/MCAsmInfoDarwin.h"
#include "llvm/MC/MCAsmInfoELF.h"

namespace llvm {
class Triple;

class PPCMCAsmInfoDarwin : public MCAsmInfoDarwin {
  virtual void anchor();

public:
  explicit PPCMCAsmInfoDarwin(bool is64Bit, const Triple &);
};

class PPCELFMCAsmInfo : public MCAsmInfoELF {
  void anchor() override;

public:
  explicit PPCELFMCAsmInfo(bool is64Bit, const Triple &);
};

} // namespace llvm

#endif
