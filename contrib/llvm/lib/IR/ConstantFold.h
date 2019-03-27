//===-- ConstantFolding.h - Internal Constant Folding Interface -*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file defines the (internal) constant folding interfaces for LLVM.  These
// interfaces are used by the ConstantExpr::get* methods to automatically fold
// constants when possible.
//
// These operators may return a null object if they don't know how to perform
// the specified operation on the specified constant types.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_IR_CONSTANTFOLD_H
#define LLVM_LIB_IR_CONSTANTFOLD_H

#include "llvm/ADT/Optional.h"

namespace llvm {
template <typename T> class ArrayRef;
  class Value;
  class Constant;
  class Type;

  // Constant fold various types of instruction...
  Constant *ConstantFoldCastInstruction(
    unsigned opcode,     ///< The opcode of the cast
    Constant *V,         ///< The source constant
    Type *DestTy   ///< The destination type
  );
  Constant *ConstantFoldSelectInstruction(Constant *Cond,
                                          Constant *V1, Constant *V2);
  Constant *ConstantFoldExtractElementInstruction(Constant *Val, Constant *Idx);
  Constant *ConstantFoldInsertElementInstruction(Constant *Val, Constant *Elt,
                                                 Constant *Idx);
  Constant *ConstantFoldShuffleVectorInstruction(Constant *V1, Constant *V2,
                                                 Constant *Mask);
  Constant *ConstantFoldExtractValueInstruction(Constant *Agg,
                                                ArrayRef<unsigned> Idxs);
  Constant *ConstantFoldInsertValueInstruction(Constant *Agg, Constant *Val,
                                               ArrayRef<unsigned> Idxs);
  Constant *ConstantFoldBinaryInstruction(unsigned Opcode, Constant *V1,
                                          Constant *V2);
  Constant *ConstantFoldCompareInstruction(unsigned short predicate,
                                           Constant *C1, Constant *C2);
  Constant *ConstantFoldGetElementPtr(Type *Ty, Constant *C, bool InBounds,
                                      Optional<unsigned> InRangeIndex,
                                      ArrayRef<Value *> Idxs);
} // End llvm namespace

#endif
