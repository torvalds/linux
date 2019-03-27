//===-- ARMFixupKinds.h - ARM Specific Fixup Entries ------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_ARM_MCTARGETDESC_ARMFIXUPKINDS_H
#define LLVM_LIB_TARGET_ARM_MCTARGETDESC_ARMFIXUPKINDS_H

#include "llvm/MC/MCFixup.h"

namespace llvm {
namespace ARM {
enum Fixups {
  // 12-bit PC relative relocation for symbol addresses
  fixup_arm_ldst_pcrel_12 = FirstTargetFixupKind,

  // Equivalent to fixup_arm_ldst_pcrel_12, with the 16-bit halfwords reordered.
  fixup_t2_ldst_pcrel_12,

  // 10-bit PC relative relocation for symbol addresses used in
  // LDRD/LDRH/LDRB/etc. instructions. All bits are encoded.
  fixup_arm_pcrel_10_unscaled,
  // 10-bit PC relative relocation for symbol addresses used in VFP instructions
  // where the lower 2 bits are not encoded (so it's encoded as an 8-bit
  // immediate).
  fixup_arm_pcrel_10,
  // Equivalent to fixup_arm_pcrel_10, accounting for the short-swapped encoding
  // of Thumb2 instructions.
  fixup_t2_pcrel_10,
  // 9-bit PC relative relocation for symbol addresses used in VFP instructions
  // where bit 0 not encoded (so it's encoded as an 8-bit immediate).
  fixup_arm_pcrel_9,
  // Equivalent to fixup_arm_pcrel_9, accounting for the short-swapped encoding
  // of Thumb2 instructions.
  fixup_t2_pcrel_9,
  // 10-bit PC relative relocation for symbol addresses where the lower 2 bits
  // are not encoded (so it's encoded as an 8-bit immediate).
  fixup_thumb_adr_pcrel_10,
  // 12-bit PC relative relocation for the ADR instruction.
  fixup_arm_adr_pcrel_12,
  // 12-bit PC relative relocation for the ADR instruction.
  fixup_t2_adr_pcrel_12,
  // 24-bit PC relative relocation for conditional branch instructions.
  fixup_arm_condbranch,
  // 24-bit PC relative relocation for branch instructions. (unconditional)
  fixup_arm_uncondbranch,
  // 20-bit PC relative relocation for Thumb2 direct uconditional branch
  // instructions.
  fixup_t2_condbranch,
  // 20-bit PC relative relocation for Thumb2 direct branch unconditional branch
  // instructions.
  fixup_t2_uncondbranch,

  // 12-bit fixup for Thumb B instructions.
  fixup_arm_thumb_br,

  // The following fixups handle the ARM BL instructions. These can be
  // conditionalised; however, the ARM ELF ABI requires a different relocation
  // in that case: R_ARM_JUMP24 instead of R_ARM_CALL. The difference is that
  // R_ARM_CALL is allowed to change the instruction to a BLX inline, which has
  // no conditional version; R_ARM_JUMP24 would have to insert a veneer.
  //
  // MachO does not draw a distinction between the two cases, so it will treat
  // fixup_arm_uncondbl and fixup_arm_condbl as identical fixups.

  // Fixup for unconditional ARM BL instructions.
  fixup_arm_uncondbl,

  // Fixup for ARM BL instructions with nontrivial conditionalisation.
  fixup_arm_condbl,

  // Fixup for ARM BLX instructions.
  fixup_arm_blx,

  // Fixup for Thumb BL instructions.
  fixup_arm_thumb_bl,

  // Fixup for Thumb BLX instructions.
  fixup_arm_thumb_blx,

  // Fixup for Thumb branch instructions.
  fixup_arm_thumb_cb,

  // Fixup for Thumb load/store from constant pool instrs.
  fixup_arm_thumb_cp,

  // Fixup for Thumb conditional branching instructions.
  fixup_arm_thumb_bcc,

  // The next two are for the movt/movw pair
  // the 16bit imm field are split into imm{15-12} and imm{11-0}
  fixup_arm_movt_hi16, // :upper16:
  fixup_arm_movw_lo16, // :lower16:
  fixup_t2_movt_hi16,  // :upper16:
  fixup_t2_movw_lo16,  // :lower16:

  // Fixup for mod_imm
  fixup_arm_mod_imm,

  // Fixup for Thumb2 8-bit rotated operand
  fixup_t2_so_imm,

  // Marker
  LastTargetFixupKind,
  NumTargetFixupKinds = LastTargetFixupKind - FirstTargetFixupKind
};
}
} // namespace llvm

#endif
