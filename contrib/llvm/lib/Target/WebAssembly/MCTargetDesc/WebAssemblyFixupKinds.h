//=- WebAssemblyFixupKinds.h - WebAssembly Specific Fixup Entries -*- C++ -*-=//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_WEBASSEMBLY_MCTARGETDESC_WEBASSEMBLYFIXUPKINDS_H
#define LLVM_LIB_TARGET_WEBASSEMBLY_MCTARGETDESC_WEBASSEMBLYFIXUPKINDS_H

#include "llvm/MC/MCFixup.h"

namespace llvm {
namespace WebAssembly {
enum Fixups {
  fixup_code_sleb128_i32 = FirstTargetFixupKind, // 32-bit signed
  fixup_code_sleb128_i64,                        // 64-bit signed
  fixup_code_uleb128_i32,                        // 32-bit unsigned

  // Marker
  LastTargetFixupKind,
  NumTargetFixupKinds = LastTargetFixupKind - FirstTargetFixupKind
};
} // end namespace WebAssembly
} // end namespace llvm

#endif
