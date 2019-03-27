//===-- AVRFixupKinds.h - AVR Specific Fixup Entries ------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_AVR_FIXUP_KINDS_H
#define LLVM_AVR_FIXUP_KINDS_H

#include "llvm/MC/MCFixup.h"

namespace llvm {
namespace AVR {

/// The set of supported fixups.
///
/// Although most of the current fixup types reflect a unique relocation
/// one can have multiple fixup types for a given relocation and thus need
/// to be uniquely named.
///
/// \note This table *must* be in the same order of
///       MCFixupKindInfo Infos[AVR::NumTargetFixupKinds]
///       in `AVRAsmBackend.cpp`.
enum Fixups {
  /// A 32-bit AVR fixup.
  fixup_32 = FirstTargetFixupKind,

  /// A 7-bit PC-relative fixup for the family of conditional
  /// branches which take 7-bit targets (BRNE,BRGT,etc).
  fixup_7_pcrel,
  /// A 12-bit PC-relative fixup for the family of branches
  /// which take 12-bit targets (RJMP,RCALL,etc).
  /// \note Although the fixup is labelled as 13 bits, it
  ///       is actually only encoded in 12. The reason for
  ///       The nonmenclature is that AVR branch targets are
  ///       rightshifted by 1, because instructions are always
  ///       aligned to 2 bytes, so the 0'th bit is always 0.
  ///       This way there is 13-bits of precision.
  fixup_13_pcrel,

  /// A 16-bit address.
  fixup_16,
  /// A 16-bit program memory address.
  fixup_16_pm,

  /// Replaces the 8-bit immediate with another value.
  fixup_ldi,

  /// Replaces the immediate operand of a 16-bit `Rd, K` instruction
  /// with the lower 8 bits of a 16-bit value (bits 0-7).
  fixup_lo8_ldi,
  /// Replaces the immediate operand of a 16-bit `Rd, K` instruction
  /// with the upper 8 bits of a 16-bit value (bits 8-15).
  fixup_hi8_ldi,
  /// Replaces the immediate operand of a 16-bit `Rd, K` instruction
  /// with the upper 8 bits of a 24-bit value (bits 16-23).
  fixup_hh8_ldi,
  /// Replaces the immediate operand of a 16-bit `Rd, K` instruction
  /// with the upper 8 bits of a 32-bit value (bits 24-31).
  fixup_ms8_ldi,

  /// Replaces the immediate operand of a 16-bit `Rd, K` instruction
  /// with the lower 8 bits of a negated 16-bit value (bits 0-7).
  fixup_lo8_ldi_neg,
  /// Replaces the immediate operand of a 16-bit `Rd, K` instruction
  /// with the upper 8 bits of a negated 16-bit value (bits 8-15).
  fixup_hi8_ldi_neg,
  /// Replaces the immediate operand of a 16-bit `Rd, K` instruction
  /// with the upper 8 bits of a negated negated 24-bit value (bits 16-23).
  fixup_hh8_ldi_neg,
  /// Replaces the immediate operand of a 16-bit `Rd, K` instruction
  /// with the upper 8 bits of a negated negated 32-bit value (bits 24-31).
  fixup_ms8_ldi_neg,

  /// Replaces the immediate operand of a 16-bit `Rd, K` instruction
  /// with the lower 8 bits of a 16-bit program memory address value (bits 0-7).
  fixup_lo8_ldi_pm,
  /// Replaces the immediate operand of a 16-bit `Rd, K` instruction
  /// with the upper 8 bits of a 16-bit program memory address value (bits
  /// 8-15).
  fixup_hi8_ldi_pm,
  /// Replaces the immediate operand of a 16-bit `Rd, K` instruction
  /// with the upper 8 bits of a 24-bit program memory address value (bits
  /// 16-23).
  fixup_hh8_ldi_pm,

  /// Replaces the immediate operand of a 16-bit `Rd, K` instruction
  /// with the lower 8 bits of a negated 16-bit program memory address value
  /// (bits 0-7).
  fixup_lo8_ldi_pm_neg,
  /// Replaces the immediate operand of a 16-bit `Rd, K` instruction
  /// with the upper 8 bits of a negated 16-bit program memory address value
  /// (bits 8-15).
  fixup_hi8_ldi_pm_neg,
  /// Replaces the immediate operand of a 16-bit `Rd, K` instruction
  /// with the upper 8 bits of a negated 24-bit program memory address value
  /// (bits 16-23).
  fixup_hh8_ldi_pm_neg,

  /// A 22-bit fixup for the target of a `CALL k` or `JMP k` instruction.
  fixup_call,

  fixup_6,
  /// A symbol+addr fixup for the `LDD <x>+<n>, <r>" family of instructions.
  fixup_6_adiw,

  fixup_lo8_ldi_gs,
  fixup_hi8_ldi_gs,

  fixup_8,
  fixup_8_lo8,
  fixup_8_hi8,
  fixup_8_hlo8,

  fixup_diff8,
  fixup_diff16,
  fixup_diff32,

  fixup_lds_sts_16,

  /// A 6-bit port address.
  fixup_port6,
  /// A 5-bit port address.
  fixup_port5,

  // Marker
  LastTargetFixupKind,
  NumTargetFixupKinds = LastTargetFixupKind - FirstTargetFixupKind
};

namespace fixups {

/// Adjusts the value of a branch target.
/// All branch targets in AVR are rightshifted by 1 to take advantage
/// of the fact that all instructions are aligned to addresses of size
/// 2, so bit 0 of an address is always 0. This gives us another bit
/// of precision.
/// \param[in,out] The target to adjust.
template <typename T> inline void adjustBranchTarget(T &val) { val >>= 1; }

} // end of namespace fixups
}
} // end of namespace llvm::AVR

#endif // LLVM_AVR_FIXUP_KINDS_H
