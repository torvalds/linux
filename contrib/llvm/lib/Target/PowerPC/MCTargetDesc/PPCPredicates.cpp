//===-- PPCPredicates.cpp - PPC Branch Predicate Information --------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file implements the PowerPC branch predicates.
//
//===----------------------------------------------------------------------===//

#include "PPCPredicates.h"
#include "llvm/Support/ErrorHandling.h"
#include <cassert>
using namespace llvm;

PPC::Predicate PPC::InvertPredicate(PPC::Predicate Opcode) {
  switch (Opcode) {
  case PPC::PRED_EQ: return PPC::PRED_NE;
  case PPC::PRED_NE: return PPC::PRED_EQ;
  case PPC::PRED_LT: return PPC::PRED_GE;
  case PPC::PRED_GE: return PPC::PRED_LT;
  case PPC::PRED_GT: return PPC::PRED_LE;
  case PPC::PRED_LE: return PPC::PRED_GT;
  case PPC::PRED_NU: return PPC::PRED_UN;
  case PPC::PRED_UN: return PPC::PRED_NU;
  case PPC::PRED_EQ_MINUS: return PPC::PRED_NE_PLUS;
  case PPC::PRED_NE_MINUS: return PPC::PRED_EQ_PLUS;
  case PPC::PRED_LT_MINUS: return PPC::PRED_GE_PLUS;
  case PPC::PRED_GE_MINUS: return PPC::PRED_LT_PLUS;
  case PPC::PRED_GT_MINUS: return PPC::PRED_LE_PLUS;
  case PPC::PRED_LE_MINUS: return PPC::PRED_GT_PLUS;
  case PPC::PRED_NU_MINUS: return PPC::PRED_UN_PLUS;
  case PPC::PRED_UN_MINUS: return PPC::PRED_NU_PLUS;
  case PPC::PRED_EQ_PLUS: return PPC::PRED_NE_MINUS;
  case PPC::PRED_NE_PLUS: return PPC::PRED_EQ_MINUS;
  case PPC::PRED_LT_PLUS: return PPC::PRED_GE_MINUS;
  case PPC::PRED_GE_PLUS: return PPC::PRED_LT_MINUS;
  case PPC::PRED_GT_PLUS: return PPC::PRED_LE_MINUS;
  case PPC::PRED_LE_PLUS: return PPC::PRED_GT_MINUS;
  case PPC::PRED_NU_PLUS: return PPC::PRED_UN_MINUS;
  case PPC::PRED_UN_PLUS: return PPC::PRED_NU_MINUS;

  // Simple predicates for single condition-register bits.
  case PPC::PRED_BIT_SET:   return PPC::PRED_BIT_UNSET;
  case PPC::PRED_BIT_UNSET: return PPC::PRED_BIT_SET;
  }
  llvm_unreachable("Unknown PPC branch opcode!");
}

PPC::Predicate PPC::getSwappedPredicate(PPC::Predicate Opcode) {
  switch (Opcode) {
  case PPC::PRED_EQ: return PPC::PRED_EQ;
  case PPC::PRED_NE: return PPC::PRED_NE;
  case PPC::PRED_LT: return PPC::PRED_GT;
  case PPC::PRED_GE: return PPC::PRED_LE;
  case PPC::PRED_GT: return PPC::PRED_LT;
  case PPC::PRED_LE: return PPC::PRED_GE;
  case PPC::PRED_NU: return PPC::PRED_NU;
  case PPC::PRED_UN: return PPC::PRED_UN;
  case PPC::PRED_EQ_MINUS: return PPC::PRED_EQ_MINUS;
  case PPC::PRED_NE_MINUS: return PPC::PRED_NE_MINUS;
  case PPC::PRED_LT_MINUS: return PPC::PRED_GT_MINUS;
  case PPC::PRED_GE_MINUS: return PPC::PRED_LE_MINUS;
  case PPC::PRED_GT_MINUS: return PPC::PRED_LT_MINUS;
  case PPC::PRED_LE_MINUS: return PPC::PRED_GE_MINUS;
  case PPC::PRED_NU_MINUS: return PPC::PRED_NU_MINUS;
  case PPC::PRED_UN_MINUS: return PPC::PRED_UN_MINUS;
  case PPC::PRED_EQ_PLUS: return PPC::PRED_EQ_PLUS;
  case PPC::PRED_NE_PLUS: return PPC::PRED_NE_PLUS;
  case PPC::PRED_LT_PLUS: return PPC::PRED_GT_PLUS;
  case PPC::PRED_GE_PLUS: return PPC::PRED_LE_PLUS;
  case PPC::PRED_GT_PLUS: return PPC::PRED_LT_PLUS;
  case PPC::PRED_LE_PLUS: return PPC::PRED_GE_PLUS;
  case PPC::PRED_NU_PLUS: return PPC::PRED_NU_PLUS;
  case PPC::PRED_UN_PLUS: return PPC::PRED_UN_PLUS;

  case PPC::PRED_BIT_SET:
  case PPC::PRED_BIT_UNSET:
    llvm_unreachable("Invalid use of bit predicate code");
  }
  llvm_unreachable("Unknown PPC branch opcode!");
}

