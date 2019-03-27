//===-- GuardUtils.cpp - Utils for work with guards -------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
// Utils that are used to perform analyzes related to guards and their
// conditions.
//===----------------------------------------------------------------------===//

#include "llvm/Analysis/GuardUtils.h"
#include "llvm/IR/PatternMatch.h"

using namespace llvm;

bool llvm::isGuard(const User *U) {
  using namespace llvm::PatternMatch;
  return match(U, m_Intrinsic<Intrinsic::experimental_guard>());
}
