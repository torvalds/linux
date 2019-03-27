//===- llvm/Transforms/Utils/BypassSlowDivision.h ---------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file contains an optimization for div and rem on architectures that
// execute short instructions significantly faster than longer instructions.
// For example, on Intel Atom 32-bit divides are slow enough that during
// runtime it is profitable to check the value of the operands, and if they are
// positive and less than 256 use an unsigned 8-bit divide.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_TRANSFORMS_UTILS_BYPASSSLOWDIVISION_H
#define LLVM_TRANSFORMS_UTILS_BYPASSSLOWDIVISION_H

#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/DenseMapInfo.h"
#include <cstdint>

namespace llvm {

class BasicBlock;
class Value;

struct DivRemMapKey {
  bool SignedOp;
  Value *Dividend;
  Value *Divisor;

  DivRemMapKey(bool InSignedOp, Value *InDividend, Value *InDivisor)
      : SignedOp(InSignedOp), Dividend(InDividend), Divisor(InDivisor) {}
};

template <> struct DenseMapInfo<DivRemMapKey> {
  static bool isEqual(const DivRemMapKey &Val1, const DivRemMapKey &Val2) {
    return Val1.SignedOp == Val2.SignedOp && Val1.Dividend == Val2.Dividend &&
           Val1.Divisor == Val2.Divisor;
  }

  static DivRemMapKey getEmptyKey() {
    return DivRemMapKey(false, nullptr, nullptr);
  }

  static DivRemMapKey getTombstoneKey() {
    return DivRemMapKey(true, nullptr, nullptr);
  }

  static unsigned getHashValue(const DivRemMapKey &Val) {
    return (unsigned)(reinterpret_cast<uintptr_t>(Val.Dividend) ^
                      reinterpret_cast<uintptr_t>(Val.Divisor)) ^
           (unsigned)Val.SignedOp;
  }
};

/// This optimization identifies DIV instructions in a BB that can be
/// profitably bypassed and carried out with a shorter, faster divide.
///
/// This optimization may add basic blocks immediately after BB; for obvious
/// reasons, you shouldn't pass those blocks to bypassSlowDivision.
bool bypassSlowDivision(
    BasicBlock *BB, const DenseMap<unsigned int, unsigned int> &BypassWidth);

} // end namespace llvm

#endif // LLVM_TRANSFORMS_UTILS_BYPASSSLOWDIVISION_H
