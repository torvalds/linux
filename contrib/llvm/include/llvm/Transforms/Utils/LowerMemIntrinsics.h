//===- llvm/Transforms/Utils/LowerMemintrinsics.h ---------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// Lower memset, memcpy, memmov intrinsics to loops (e.g. for targets without
// library support).
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_TRANSFORMS_UTILS_LOWERMEMINTRINSICS_H
#define LLVM_TRANSFORMS_UTILS_LOWERMEMINTRINSICS_H

namespace llvm {

class ConstantInt;
class Instruction;
class MemCpyInst;
class MemMoveInst;
class MemSetInst;
class TargetTransformInfo;
class Value;

/// Emit a loop implementing the semantics of llvm.memcpy where the size is not
/// a compile-time constant. Loop will be insterted at \p InsertBefore.
void createMemCpyLoopUnknownSize(Instruction *InsertBefore, Value *SrcAddr,
                                 Value *DstAddr, Value *CopyLen,
                                 unsigned SrcAlign, unsigned DestAlign,
                                 bool SrcIsVolatile, bool DstIsVolatile,
                                 const TargetTransformInfo &TTI);

/// Emit a loop implementing the semantics of an llvm.memcpy whose size is a
/// compile time constant. Loop is inserted at \p InsertBefore.
void createMemCpyLoopKnownSize(Instruction *InsertBefore, Value *SrcAddr,
                               Value *DstAddr, ConstantInt *CopyLen,
                               unsigned SrcAlign, unsigned DestAlign,
                               bool SrcIsVolatile, bool DstIsVolatile,
                               const TargetTransformInfo &TTI);


/// Expand \p MemCpy as a loop. \p MemCpy is not deleted.
void expandMemCpyAsLoop(MemCpyInst *MemCpy, const TargetTransformInfo &TTI);

/// Expand \p MemMove as a loop. \p MemMove is not deleted.
void expandMemMoveAsLoop(MemMoveInst *MemMove);

/// Expand \p MemSet as a loop. \p MemSet is not deleted.
void expandMemSetAsLoop(MemSetInst *MemSet);

} // End llvm namespace

#endif
