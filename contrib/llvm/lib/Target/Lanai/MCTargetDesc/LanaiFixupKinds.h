//===-- LanaiFixupKinds.h - Lanai Specific Fixup Entries --------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_LANAI_MCTARGETDESC_LANAIFIXUPKINDS_H
#define LLVM_LIB_TARGET_LANAI_MCTARGETDESC_LANAIFIXUPKINDS_H

#include "llvm/MC/MCFixup.h"

namespace llvm {
namespace Lanai {
// Although most of the current fixup types reflect a unique relocation
// one can have multiple fixup types for a given relocation and thus need
// to be uniquely named.
//
// This table *must* be in the save order of
// MCFixupKindInfo Infos[Lanai::NumTargetFixupKinds]
// in LanaiAsmBackend.cpp.
//
enum Fixups {
  // Results in R_Lanai_NONE
  FIXUP_LANAI_NONE = FirstTargetFixupKind,

  FIXUP_LANAI_21,   // 21-bit symbol relocation
  FIXUP_LANAI_21_F, // 21-bit symbol relocation, last two bits masked to 0
  FIXUP_LANAI_25,   // 25-bit branch targets
  FIXUP_LANAI_32,   // general 32-bit relocation
  FIXUP_LANAI_HI16, // upper 16-bits of a symbolic relocation
  FIXUP_LANAI_LO16, // lower 16-bits of a symbolic relocation

  // Marker
  LastTargetFixupKind,
  NumTargetFixupKinds = LastTargetFixupKind - FirstTargetFixupKind
};
} // namespace Lanai
} // namespace llvm

#endif // LLVM_LIB_TARGET_LANAI_MCTARGETDESC_LANAIFIXUPKINDS_H
