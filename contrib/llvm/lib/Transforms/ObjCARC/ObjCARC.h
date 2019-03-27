//===- ObjCARC.h - ObjC ARC Optimization --------------*- C++ -*-----------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
/// \file
/// This file defines common definitions/declarations used by the ObjC ARC
/// Optimizer. ARC stands for Automatic Reference Counting and is a system for
/// managing reference counts for objects in Objective C.
///
/// WARNING: This file knows about certain library functions. It recognizes them
/// by name, and hardwires knowledge of their semantics.
///
/// WARNING: This file knows about how certain Objective-C library functions are
/// used. Naive LLVM IR transformations which would otherwise be
/// behavior-preserving may break these assumptions.
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TRANSFORMS_OBJCARC_OBJCARC_H
#define LLVM_LIB_TRANSFORMS_OBJCARC_OBJCARC_H

#include "llvm/ADT/StringSwitch.h"
#include "llvm/Analysis/AliasAnalysis.h"
#include "llvm/Analysis/ObjCARCAnalysisUtils.h"
#include "llvm/Analysis/ObjCARCInstKind.h"
#include "llvm/Analysis/Passes.h"
#include "llvm/Transforms/Utils/Local.h"
#include "llvm/Analysis/ValueTracking.h"
#include "llvm/IR/CallSite.h"
#include "llvm/IR/InstIterator.h"
#include "llvm/IR/Module.h"
#include "llvm/Pass.h"
#include "llvm/Transforms/ObjCARC.h"

namespace llvm {
class raw_ostream;
}

namespace llvm {
namespace objcarc {

/// Erase the given instruction.
///
/// Many ObjC calls return their argument verbatim,
/// so if it's such a call and the return value has users, replace them with the
/// argument value.
///
static inline void EraseInstruction(Instruction *CI) {
  Value *OldArg = cast<CallInst>(CI)->getArgOperand(0);

  bool Unused = CI->use_empty();

  if (!Unused) {
    // Replace the return value with the argument.
    assert((IsForwarding(GetBasicARCInstKind(CI)) ||
            (IsNoopOnNull(GetBasicARCInstKind(CI)) &&
             IsNullOrUndef(OldArg->stripPointerCasts()))) &&
           "Can't delete non-forwarding instruction with users!");
    CI->replaceAllUsesWith(OldArg);
  }

  CI->eraseFromParent();

  if (Unused)
    RecursivelyDeleteTriviallyDeadInstructions(OldArg);
}

/// If Inst is a ReturnRV and its operand is a call or invoke, return the
/// operand. Otherwise return null.
static inline const Instruction *getreturnRVOperand(const Instruction &Inst,
                                                    ARCInstKind Class) {
  if (Class != ARCInstKind::RetainRV)
    return nullptr;

  const auto *Opnd = Inst.getOperand(0)->stripPointerCasts();
  if (const auto *C = dyn_cast<CallInst>(Opnd))
    return C;
  return dyn_cast<InvokeInst>(Opnd);
}

/// Return the list of PHI nodes that are equivalent to PN.
template<class PHINodeTy, class VectorTy>
void getEquivalentPHIs(PHINodeTy &PN, VectorTy &PHIList) {
  auto *BB = PN.getParent();
  for (auto &P : BB->phis()) {
    if (&P == &PN) // Do not add PN to the list.
      continue;
    unsigned I = 0, E = PN.getNumIncomingValues();
    for (; I < E; ++I) {
      auto *BB = PN.getIncomingBlock(I);
      auto *PNOpnd = PN.getIncomingValue(I)->stripPointerCasts();
      auto *POpnd = P.getIncomingValueForBlock(BB)->stripPointerCasts();
      if (PNOpnd != POpnd)
        break;
    }
    if (I == E)
      PHIList.push_back(&P);
  }
}

} // end namespace objcarc
} // end namespace llvm

#endif
