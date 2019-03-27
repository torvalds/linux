//===-- NVPTXMCAsmInfo.h - NVPTX asm properties ----------------*- C++ -*--===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file contains the declaration of the NVPTXMCAsmInfo class.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_NVPTX_MCTARGETDESC_NVPTXMCASMINFO_H
#define LLVM_LIB_TARGET_NVPTX_MCTARGETDESC_NVPTXMCASMINFO_H

#include "llvm/MC/MCAsmInfo.h"

namespace llvm {
class Target;
class Triple;

class NVPTXMCAsmInfo : public MCAsmInfo {
  virtual void anchor();

public:
  explicit NVPTXMCAsmInfo(const Triple &TheTriple);

  /// Return true if the .section directive should be omitted when
  /// emitting \p SectionName.  For example:
  ///
  /// shouldOmitSectionDirective(".text")
  ///
  /// returns false => .section .text,#alloc,#execinstr
  /// returns true  => .text
  bool shouldOmitSectionDirective(StringRef SectionName) const override {
    return true;
  }
};
} // namespace llvm

#endif
