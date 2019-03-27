//===-- llvm/MC/MCFixupKindInfo.h - Fixup Descriptors -----------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_MC_MCFIXUPKINDINFO_H
#define LLVM_MC_MCFIXUPKINDINFO_H

namespace llvm {

/// Target independent information on a fixup kind.
struct MCFixupKindInfo {
  enum FixupKindFlags {
    /// Is this fixup kind PCrelative? This is used by the assembler backend to
    /// evaluate fixup values in a target independent manner when possible.
    FKF_IsPCRel = (1 << 0),

    /// Should this fixup kind force a 4-byte aligned effective PC value?
    FKF_IsAlignedDownTo32Bits = (1 << 1)
  };

  /// A target specific name for the fixup kind. The names will be unique for
  /// distinct kinds on any given target.
  const char *Name;

  /// The bit offset to write the relocation into.
  unsigned TargetOffset;

  /// The number of bits written by this fixup. The bits are assumed to be
  /// contiguous.
  unsigned TargetSize;

  /// Flags describing additional information on this fixup kind.
  unsigned Flags;
};

} // End llvm namespace

#endif
