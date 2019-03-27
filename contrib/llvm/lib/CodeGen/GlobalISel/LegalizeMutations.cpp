//===- lib/CodeGen/GlobalISel/LegalizerMutations.cpp - Mutations ----------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// A library of mutation factories to use for LegalityMutation.
//
//===----------------------------------------------------------------------===//

#include "llvm/CodeGen/GlobalISel/LegalizerInfo.h"

using namespace llvm;

LegalizeMutation LegalizeMutations::changeTo(unsigned TypeIdx, LLT Ty) {
  return
      [=](const LegalityQuery &Query) { return std::make_pair(TypeIdx, Ty); };
}

LegalizeMutation LegalizeMutations::changeTo(unsigned TypeIdx,
                                             unsigned FromTypeIdx) {
  return [=](const LegalityQuery &Query) {
    return std::make_pair(TypeIdx, Query.Types[FromTypeIdx]);
  };
}

LegalizeMutation LegalizeMutations::widenScalarToNextPow2(unsigned TypeIdx,
                                                          unsigned Min) {
  return [=](const LegalityQuery &Query) {
    unsigned NewSizeInBits =
        1 << Log2_32_Ceil(Query.Types[TypeIdx].getSizeInBits());
    if (NewSizeInBits < Min)
      NewSizeInBits = Min;
    return std::make_pair(TypeIdx, LLT::scalar(NewSizeInBits));
  };
}

LegalizeMutation LegalizeMutations::moreElementsToNextPow2(unsigned TypeIdx,
                                                           unsigned Min) {
  return [=](const LegalityQuery &Query) {
    const LLT &VecTy = Query.Types[TypeIdx];
    unsigned NewNumElements = 1 << Log2_32_Ceil(VecTy.getNumElements());
    if (NewNumElements < Min)
      NewNumElements = Min;
    return std::make_pair(
        TypeIdx, LLT::vector(NewNumElements, VecTy.getScalarSizeInBits()));
  };
}
