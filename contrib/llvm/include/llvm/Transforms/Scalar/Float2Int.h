//===-- Float2Int.h - Demote floating point ops to work on integers -------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file provides the Float2Int pass, which aims to demote floating
// point operations to work on integers, where that is losslessly possible.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_TRANSFORMS_SCALAR_FLOAT2INT_H
#define LLVM_TRANSFORMS_SCALAR_FLOAT2INT_H

#include "llvm/ADT/EquivalenceClasses.h"
#include "llvm/ADT/MapVector.h"
#include "llvm/IR/ConstantRange.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/PassManager.h"

namespace llvm {
class Float2IntPass : public PassInfoMixin<Float2IntPass> {
public:
  PreservedAnalyses run(Function &F, FunctionAnalysisManager &AM);

  // Glue for old PM.
  bool runImpl(Function &F);

private:
  void findRoots(Function &F, SmallPtrSet<Instruction *, 8> &Roots);
  void seen(Instruction *I, ConstantRange R);
  ConstantRange badRange();
  ConstantRange unknownRange();
  ConstantRange validateRange(ConstantRange R);
  void walkBackwards(const SmallPtrSetImpl<Instruction *> &Roots);
  void walkForwards();
  bool validateAndTransform();
  Value *convert(Instruction *I, Type *ToTy);
  void cleanup();

  MapVector<Instruction *, ConstantRange> SeenInsts;
  SmallPtrSet<Instruction *, 8> Roots;
  EquivalenceClasses<Instruction *> ECs;
  MapVector<Instruction *, Value *> ConvertedInsts;
  LLVMContext *Ctx;
};
}
#endif // LLVM_TRANSFORMS_SCALAR_FLOAT2INT_H
