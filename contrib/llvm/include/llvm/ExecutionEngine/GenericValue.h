//===- GenericValue.h - Represent any type of LLVM value --------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// The GenericValue class is used to represent an LLVM value of arbitrary type.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_EXECUTIONENGINE_GENERICVALUE_H
#define LLVM_EXECUTIONENGINE_GENERICVALUE_H

#include "llvm/ADT/APInt.h"
#include <vector>

namespace llvm {

using PointerTy = void *;

struct GenericValue {
  struct IntPair {
    unsigned int first;
    unsigned int second;
  };
  union {
    double DoubleVal;
    float FloatVal;
    PointerTy PointerVal;
    struct IntPair UIntPairVal;
    unsigned char Untyped[8];
  };
  APInt IntVal; // also used for long doubles.
  // For aggregate data types.
  std::vector<GenericValue> AggregateVal;

  // to make code faster, set GenericValue to zero could be omitted, but it is
  // potentially can cause problems, since GenericValue to store garbage
  // instead of zero.
  GenericValue() : IntVal(1, 0) {
    UIntPairVal.first = 0;
    UIntPairVal.second = 0;
  }
  explicit GenericValue(void *V) : PointerVal(V), IntVal(1, 0) {}
};

inline GenericValue PTOGV(void *P) { return GenericValue(P); }
inline void *GVTOP(const GenericValue &GV) { return GV.PointerVal; }

} // end namespace llvm

#endif // LLVM_EXECUTIONENGINE_GENERICVALUE_H
