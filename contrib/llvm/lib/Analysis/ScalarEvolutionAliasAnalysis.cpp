//===- ScalarEvolutionAliasAnalysis.cpp - SCEV-based Alias Analysis -------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file defines the ScalarEvolutionAliasAnalysis pass, which implements a
// simple alias analysis implemented in terms of ScalarEvolution queries.
//
// This differs from traditional loop dependence analysis in that it tests
// for dependencies within a single iteration of a loop, rather than
// dependencies between different iterations.
//
// ScalarEvolution has a more complete understanding of pointer arithmetic
// than BasicAliasAnalysis' collection of ad-hoc analyses.
//
//===----------------------------------------------------------------------===//

#include "llvm/Analysis/ScalarEvolutionAliasAnalysis.h"
using namespace llvm;

AliasResult SCEVAAResult::alias(const MemoryLocation &LocA,
                                const MemoryLocation &LocB) {
  // If either of the memory references is empty, it doesn't matter what the
  // pointer values are. This allows the code below to ignore this special
  // case.
  if (LocA.Size.isZero() || LocB.Size.isZero())
    return NoAlias;

  // This is SCEVAAResult. Get the SCEVs!
  const SCEV *AS = SE.getSCEV(const_cast<Value *>(LocA.Ptr));
  const SCEV *BS = SE.getSCEV(const_cast<Value *>(LocB.Ptr));

  // If they evaluate to the same expression, it's a MustAlias.
  if (AS == BS)
    return MustAlias;

  // If something is known about the difference between the two addresses,
  // see if it's enough to prove a NoAlias.
  if (SE.getEffectiveSCEVType(AS->getType()) ==
      SE.getEffectiveSCEVType(BS->getType())) {
    unsigned BitWidth = SE.getTypeSizeInBits(AS->getType());
    APInt ASizeInt(BitWidth, LocA.Size.hasValue()
                                 ? LocA.Size.getValue()
                                 : MemoryLocation::UnknownSize);
    APInt BSizeInt(BitWidth, LocB.Size.hasValue()
                                 ? LocB.Size.getValue()
                                 : MemoryLocation::UnknownSize);

    // Compute the difference between the two pointers.
    const SCEV *BA = SE.getMinusSCEV(BS, AS);

    // Test whether the difference is known to be great enough that memory of
    // the given sizes don't overlap. This assumes that ASizeInt and BSizeInt
    // are non-zero, which is special-cased above.
    if (ASizeInt.ule(SE.getUnsignedRange(BA).getUnsignedMin()) &&
        (-BSizeInt).uge(SE.getUnsignedRange(BA).getUnsignedMax()))
      return NoAlias;

    // Folding the subtraction while preserving range information can be tricky
    // (because of INT_MIN, etc.); if the prior test failed, swap AS and BS
    // and try again to see if things fold better that way.

    // Compute the difference between the two pointers.
    const SCEV *AB = SE.getMinusSCEV(AS, BS);

    // Test whether the difference is known to be great enough that memory of
    // the given sizes don't overlap. This assumes that ASizeInt and BSizeInt
    // are non-zero, which is special-cased above.
    if (BSizeInt.ule(SE.getUnsignedRange(AB).getUnsignedMin()) &&
        (-ASizeInt).uge(SE.getUnsignedRange(AB).getUnsignedMax()))
      return NoAlias;
  }

  // If ScalarEvolution can find an underlying object, form a new query.
  // The correctness of this depends on ScalarEvolution not recognizing
  // inttoptr and ptrtoint operators.
  Value *AO = GetBaseValue(AS);
  Value *BO = GetBaseValue(BS);
  if ((AO && AO != LocA.Ptr) || (BO && BO != LocB.Ptr))
    if (alias(MemoryLocation(AO ? AO : LocA.Ptr,
                             AO ? LocationSize::unknown() : LocA.Size,
                             AO ? AAMDNodes() : LocA.AATags),
              MemoryLocation(BO ? BO : LocB.Ptr,
                             BO ? LocationSize::unknown() : LocB.Size,
                             BO ? AAMDNodes() : LocB.AATags)) == NoAlias)
      return NoAlias;

  // Forward the query to the next analysis.
  return AAResultBase::alias(LocA, LocB);
}

/// Given an expression, try to find a base value.
///
/// Returns null if none was found.
Value *SCEVAAResult::GetBaseValue(const SCEV *S) {
  if (const SCEVAddRecExpr *AR = dyn_cast<SCEVAddRecExpr>(S)) {
    // In an addrec, assume that the base will be in the start, rather
    // than the step.
    return GetBaseValue(AR->getStart());
  } else if (const SCEVAddExpr *A = dyn_cast<SCEVAddExpr>(S)) {
    // If there's a pointer operand, it'll be sorted at the end of the list.
    const SCEV *Last = A->getOperand(A->getNumOperands() - 1);
    if (Last->getType()->isPointerTy())
      return GetBaseValue(Last);
  } else if (const SCEVUnknown *U = dyn_cast<SCEVUnknown>(S)) {
    // This is a leaf node.
    return U->getValue();
  }
  // No Identified object found.
  return nullptr;
}

AnalysisKey SCEVAA::Key;

SCEVAAResult SCEVAA::run(Function &F, FunctionAnalysisManager &AM) {
  return SCEVAAResult(AM.getResult<ScalarEvolutionAnalysis>(F));
}

char SCEVAAWrapperPass::ID = 0;
INITIALIZE_PASS_BEGIN(SCEVAAWrapperPass, "scev-aa",
                      "ScalarEvolution-based Alias Analysis", false, true)
INITIALIZE_PASS_DEPENDENCY(ScalarEvolutionWrapperPass)
INITIALIZE_PASS_END(SCEVAAWrapperPass, "scev-aa",
                    "ScalarEvolution-based Alias Analysis", false, true)

FunctionPass *llvm::createSCEVAAWrapperPass() {
  return new SCEVAAWrapperPass();
}

SCEVAAWrapperPass::SCEVAAWrapperPass() : FunctionPass(ID) {
  initializeSCEVAAWrapperPassPass(*PassRegistry::getPassRegistry());
}

bool SCEVAAWrapperPass::runOnFunction(Function &F) {
  Result.reset(
      new SCEVAAResult(getAnalysis<ScalarEvolutionWrapperPass>().getSE()));
  return false;
}

void SCEVAAWrapperPass::getAnalysisUsage(AnalysisUsage &AU) const {
  AU.setPreservesAll();
  AU.addRequired<ScalarEvolutionWrapperPass>();
}
