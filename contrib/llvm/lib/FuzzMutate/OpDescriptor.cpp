//===-- OpDescriptor.cpp --------------------------------------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "llvm/FuzzMutate/OpDescriptor.h"
#include "llvm/IR/Constants.h"

using namespace llvm;
using namespace fuzzerop;

void fuzzerop::makeConstantsWithType(Type *T, std::vector<Constant *> &Cs) {
  if (auto *IntTy = dyn_cast<IntegerType>(T)) {
    uint64_t W = IntTy->getBitWidth();
    Cs.push_back(ConstantInt::get(IntTy, APInt::getMaxValue(W)));
    Cs.push_back(ConstantInt::get(IntTy, APInt::getMinValue(W)));
    Cs.push_back(ConstantInt::get(IntTy, APInt::getSignedMaxValue(W)));
    Cs.push_back(ConstantInt::get(IntTy, APInt::getSignedMinValue(W)));
    Cs.push_back(ConstantInt::get(IntTy, APInt::getOneBitSet(W, W / 2)));
  } else if (T->isFloatingPointTy()) {
    auto &Ctx = T->getContext();
    auto &Sem = T->getFltSemantics();
    Cs.push_back(ConstantFP::get(Ctx, APFloat::getZero(Sem)));
    Cs.push_back(ConstantFP::get(Ctx, APFloat::getLargest(Sem)));
    Cs.push_back(ConstantFP::get(Ctx, APFloat::getSmallest(Sem)));
  } else
    Cs.push_back(UndefValue::get(T));
}

std::vector<Constant *> fuzzerop::makeConstantsWithType(Type *T) {
  std::vector<Constant *> Result;
  makeConstantsWithType(T, Result);
  return Result;
}
