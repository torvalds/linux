//===- lib/CodeGen/GlobalISel/LegalizerPredicates.cpp - Predicates --------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// A library of predicate factories to use for LegalityPredicate.
//
//===----------------------------------------------------------------------===//

#include "llvm/CodeGen/GlobalISel/LegalizerInfo.h"

using namespace llvm;

LegalityPredicate LegalityPredicates::typeIs(unsigned TypeIdx, LLT Type) {
  return
      [=](const LegalityQuery &Query) { return Query.Types[TypeIdx] == Type; };
}

LegalityPredicate
LegalityPredicates::typeInSet(unsigned TypeIdx,
                              std::initializer_list<LLT> TypesInit) {
  SmallVector<LLT, 4> Types = TypesInit;
  return [=](const LegalityQuery &Query) {
    return std::find(Types.begin(), Types.end(), Query.Types[TypeIdx]) != Types.end();
  };
}

LegalityPredicate LegalityPredicates::typePairInSet(
    unsigned TypeIdx0, unsigned TypeIdx1,
    std::initializer_list<std::pair<LLT, LLT>> TypesInit) {
  SmallVector<std::pair<LLT, LLT>, 4> Types = TypesInit;
  return [=](const LegalityQuery &Query) {
    std::pair<LLT, LLT> Match = {Query.Types[TypeIdx0], Query.Types[TypeIdx1]};
    return std::find(Types.begin(), Types.end(), Match) != Types.end();
  };
}

LegalityPredicate LegalityPredicates::typePairAndMemSizeInSet(
    unsigned TypeIdx0, unsigned TypeIdx1, unsigned MMOIdx,
    std::initializer_list<TypePairAndMemSize> TypesAndMemSizeInit) {
  SmallVector<TypePairAndMemSize, 4> TypesAndMemSize = TypesAndMemSizeInit;
  return [=](const LegalityQuery &Query) {
    TypePairAndMemSize Match = {Query.Types[TypeIdx0], Query.Types[TypeIdx1],
                                Query.MMODescrs[MMOIdx].SizeInBits};
    return std::find(TypesAndMemSize.begin(), TypesAndMemSize.end(), Match) !=
           TypesAndMemSize.end();
  };
}

LegalityPredicate LegalityPredicates::isScalar(unsigned TypeIdx) {
  return [=](const LegalityQuery &Query) {
    return Query.Types[TypeIdx].isScalar();
  };
}

LegalityPredicate LegalityPredicates::narrowerThan(unsigned TypeIdx,
                                                   unsigned Size) {
  return [=](const LegalityQuery &Query) {
    const LLT &QueryTy = Query.Types[TypeIdx];
    return QueryTy.isScalar() && QueryTy.getSizeInBits() < Size;
  };
}

LegalityPredicate LegalityPredicates::widerThan(unsigned TypeIdx,
                                                unsigned Size) {
  return [=](const LegalityQuery &Query) {
    const LLT &QueryTy = Query.Types[TypeIdx];
    return QueryTy.isScalar() && QueryTy.getSizeInBits() > Size;
  };
}

LegalityPredicate LegalityPredicates::sizeNotPow2(unsigned TypeIdx) {
  return [=](const LegalityQuery &Query) {
    const LLT &QueryTy = Query.Types[TypeIdx];
    return QueryTy.isScalar() && !isPowerOf2_32(QueryTy.getSizeInBits());
  };
}

LegalityPredicate LegalityPredicates::memSizeInBytesNotPow2(unsigned MMOIdx) {
  return [=](const LegalityQuery &Query) {
    return !isPowerOf2_32(Query.MMODescrs[MMOIdx].SizeInBits / 8);
  };
}

LegalityPredicate LegalityPredicates::numElementsNotPow2(unsigned TypeIdx) {
  return [=](const LegalityQuery &Query) {
    const LLT &QueryTy = Query.Types[TypeIdx];
    return QueryTy.isVector() && isPowerOf2_32(QueryTy.getNumElements());
  };
}

LegalityPredicate LegalityPredicates::atomicOrderingAtLeastOrStrongerThan(
    unsigned MMOIdx, AtomicOrdering Ordering) {
  return [=](const LegalityQuery &Query) {
    return isAtLeastOrStrongerThan(Query.MMODescrs[MMOIdx].Ordering, Ordering);
  };
}
