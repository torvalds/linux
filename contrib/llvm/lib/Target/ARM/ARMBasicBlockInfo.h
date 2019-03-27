//===-- ARMBasicBlockInfo.h - Basic Block Information -----------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// Utility functions and data structure for computing block size.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_ARM_ARMBASICBLOCKINFO_H
#define LLVM_LIB_TARGET_ARM_ARMBASICBLOCKINFO_H

#include "llvm/Support/MathExtras.h"
#include <algorithm>
#include <cstdint>

namespace llvm {

/// UnknownPadding - Return the worst case padding that could result from
/// unknown offset bits.  This does not include alignment padding caused by
/// known offset bits.
///
/// @param LogAlign log2(alignment)
/// @param KnownBits Number of known low offset bits.
inline unsigned UnknownPadding(unsigned LogAlign, unsigned KnownBits) {
  if (KnownBits < LogAlign)
    return (1u << LogAlign) - (1u << KnownBits);
  return 0;
}

/// BasicBlockInfo - Information about the offset and size of a single
/// basic block.
struct BasicBlockInfo {
  /// Offset - Distance from the beginning of the function to the beginning
  /// of this basic block.
  ///
  /// Offsets are computed assuming worst case padding before an aligned
  /// block. This means that subtracting basic block offsets always gives a
  /// conservative estimate of the real distance which may be smaller.
  ///
  /// Because worst case padding is used, the computed offset of an aligned
  /// block may not actually be aligned.
  unsigned Offset = 0;

  /// Size - Size of the basic block in bytes.  If the block contains
  /// inline assembly, this is a worst case estimate.
  ///
  /// The size does not include any alignment padding whether from the
  /// beginning of the block, or from an aligned jump table at the end.
  unsigned Size = 0;

  /// KnownBits - The number of low bits in Offset that are known to be
  /// exact.  The remaining bits of Offset are an upper bound.
  uint8_t KnownBits = 0;

  /// Unalign - When non-zero, the block contains instructions (inline asm)
  /// of unknown size.  The real size may be smaller than Size bytes by a
  /// multiple of 1 << Unalign.
  uint8_t Unalign = 0;

  /// PostAlign - When non-zero, the block terminator contains a .align
  /// directive, so the end of the block is aligned to 1 << PostAlign
  /// bytes.
  uint8_t PostAlign = 0;

  BasicBlockInfo() = default;

  /// Compute the number of known offset bits internally to this block.
  /// This number should be used to predict worst case padding when
  /// splitting the block.
  unsigned internalKnownBits() const {
    unsigned Bits = Unalign ? Unalign : KnownBits;
    // If the block size isn't a multiple of the known bits, assume the
    // worst case padding.
    if (Size & ((1u << Bits) - 1))
      Bits = countTrailingZeros(Size);
    return Bits;
  }

  /// Compute the offset immediately following this block.  If LogAlign is
  /// specified, return the offset the successor block will get if it has
  /// this alignment.
  unsigned postOffset(unsigned LogAlign = 0) const {
    unsigned PO = Offset + Size;
    unsigned LA = std::max(unsigned(PostAlign), LogAlign);
    if (!LA)
      return PO;
    // Add alignment padding from the terminator.
    return PO + UnknownPadding(LA, internalKnownBits());
  }

  /// Compute the number of known low bits of postOffset.  If this block
  /// contains inline asm, the number of known bits drops to the
  /// instruction alignment.  An aligned terminator may increase the number
  /// of know bits.
  /// If LogAlign is given, also consider the alignment of the next block.
  unsigned postKnownBits(unsigned LogAlign = 0) const {
    return std::max(std::max(unsigned(PostAlign), LogAlign),
                    internalKnownBits());
  }
};

} // end namespace llvm

#endif // LLVM_LIB_TARGET_ARM_ARMBASICBLOCKINFO_H
