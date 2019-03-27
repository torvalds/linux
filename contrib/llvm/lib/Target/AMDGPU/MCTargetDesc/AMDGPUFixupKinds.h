//===-- AMDGPUFixupKinds.h - AMDGPU Specific Fixup Entries ------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_AMDGPU_MCTARGETDESC_AMDGPUFIXUPKINDS_H
#define LLVM_LIB_TARGET_AMDGPU_MCTARGETDESC_AMDGPUFIXUPKINDS_H

#include "llvm/MC/MCFixup.h"

namespace llvm {
namespace AMDGPU {
enum Fixups {
  /// 16-bit PC relative fixup for SOPP branch instructions.
  fixup_si_sopp_br = FirstTargetFixupKind,

  // Marker
  LastTargetFixupKind,
  NumTargetFixupKinds = LastTargetFixupKind - FirstTargetFixupKind
};
}
}

#endif
