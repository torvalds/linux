//===-- SparcFixupKinds.h - Sparc Specific Fixup Entries --------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_SPARC_MCTARGETDESC_SPARCFIXUPKINDS_H
#define LLVM_LIB_TARGET_SPARC_MCTARGETDESC_SPARCFIXUPKINDS_H

#include "llvm/MC/MCFixup.h"

namespace llvm {
  namespace Sparc {
    enum Fixups {
      // fixup_sparc_call30 - 30-bit PC relative relocation for call
      fixup_sparc_call30 = FirstTargetFixupKind,

      /// fixup_sparc_br22 - 22-bit PC relative relocation for
      /// branches
      fixup_sparc_br22,

      /// fixup_sparc_br19 - 19-bit PC relative relocation for
      /// branches on icc/xcc
      fixup_sparc_br19,

      /// fixup_sparc_bpr  - 16-bit fixup for bpr
      fixup_sparc_br16_2,
      fixup_sparc_br16_14,

      /// fixup_sparc_13 - 13-bit fixup
      fixup_sparc_13,

      /// fixup_sparc_hi22  - 22-bit fixup corresponding to %hi(foo)
      /// for sethi
      fixup_sparc_hi22,

      /// fixup_sparc_lo10  - 10-bit fixup corresponding to %lo(foo)
      fixup_sparc_lo10,

      /// fixup_sparc_h44  - 22-bit fixup corresponding to %h44(foo)
      fixup_sparc_h44,

      /// fixup_sparc_m44  - 10-bit fixup corresponding to %m44(foo)
      fixup_sparc_m44,

      /// fixup_sparc_l44  - 12-bit fixup corresponding to %l44(foo)
      fixup_sparc_l44,

      /// fixup_sparc_hh  -  22-bit fixup corresponding to %hh(foo)
      fixup_sparc_hh,

      /// fixup_sparc_hm  -  10-bit fixup corresponding to %hm(foo)
      fixup_sparc_hm,

      /// fixup_sparc_pc22 - 22-bit fixup corresponding to %pc22(foo)
      fixup_sparc_pc22,

      /// fixup_sparc_pc10 - 10-bit fixup corresponding to %pc10(foo)
      fixup_sparc_pc10,

      /// fixup_sparc_got22 - 22-bit fixup corresponding to %got22(foo)
      fixup_sparc_got22,

      /// fixup_sparc_got10 - 10-bit fixup corresponding to %got10(foo)
      fixup_sparc_got10,

      /// fixup_sparc_got13 - 13-bit fixup corresponding to %got13(foo)
      fixup_sparc_got13,

      /// fixup_sparc_wplt30
      fixup_sparc_wplt30,

      /// fixups for Thread Local Storage
      fixup_sparc_tls_gd_hi22,
      fixup_sparc_tls_gd_lo10,
      fixup_sparc_tls_gd_add,
      fixup_sparc_tls_gd_call,
      fixup_sparc_tls_ldm_hi22,
      fixup_sparc_tls_ldm_lo10,
      fixup_sparc_tls_ldm_add,
      fixup_sparc_tls_ldm_call,
      fixup_sparc_tls_ldo_hix22,
      fixup_sparc_tls_ldo_lox10,
      fixup_sparc_tls_ldo_add,
      fixup_sparc_tls_ie_hi22,
      fixup_sparc_tls_ie_lo10,
      fixup_sparc_tls_ie_ld,
      fixup_sparc_tls_ie_ldx,
      fixup_sparc_tls_ie_add,
      fixup_sparc_tls_le_hix22,
      fixup_sparc_tls_le_lox10,

      // Marker
      LastTargetFixupKind,
      NumTargetFixupKinds = LastTargetFixupKind - FirstTargetFixupKind
    };
  }
}

#endif
