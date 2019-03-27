//===- ProvenanceAnalysis.cpp - ObjC ARC Optimization ---------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
/// \file
///
/// This file defines a special form of Alias Analysis called ``Provenance
/// Analysis''. The word ``provenance'' refers to the history of the ownership
/// of an object. Thus ``Provenance Analysis'' is an analysis which attempts to
/// use various techniques to determine if locally
///
/// WARNING: This file knows about certain library functions. It recognizes them
/// by name, and hardwires knowledge of their semantics.
///
/// WARNING: This file knows about how certain Objective-C library functions are
/// used. Naive LLVM IR transformations which would otherwise be
/// behavior-preserving may break these assumptions.
//
//===----------------------------------------------------------------------===//

#include "ProvenanceAnalysis.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/Analysis/AliasAnalysis.h"
#include "llvm/Analysis/ObjCARCAnalysisUtils.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Use.h"
#include "llvm/IR/User.h"
#include "llvm/IR/Value.h"
#include "llvm/Support/Casting.h"
#include <utility>

using namespace llvm;
using namespace llvm::objcarc;

bool ProvenanceAnalysis::relatedSelect(const SelectInst *A,
                                       const Value *B) {
  const DataLayout &DL = A->getModule()->getDataLayout();
  // If the values are Selects with the same condition, we can do a more precise
  // check: just check for relations between the values on corresponding arms.
  if (const SelectInst *SB = dyn_cast<SelectInst>(B))
    if (A->getCondition() == SB->getCondition())
      return related(A->getTrueValue(), SB->getTrueValue(), DL) ||
             related(A->getFalseValue(), SB->getFalseValue(), DL);

  // Check both arms of the Select node individually.
  return related(A->getTrueValue(), B, DL) ||
         related(A->getFalseValue(), B, DL);
}

bool ProvenanceAnalysis::relatedPHI(const PHINode *A,
                                    const Value *B) {
  const DataLayout &DL = A->getModule()->getDataLayout();
  // If the values are PHIs in the same block, we can do a more precise as well
  // as efficient check: just check for relations between the values on
  // corresponding edges.
  if (const PHINode *PNB = dyn_cast<PHINode>(B))
    if (PNB->getParent() == A->getParent()) {
      for (unsigned i = 0, e = A->getNumIncomingValues(); i != e; ++i)
        if (related(A->getIncomingValue(i),
                    PNB->getIncomingValueForBlock(A->getIncomingBlock(i)), DL))
          return true;
      return false;
    }

  // Check each unique source of the PHI node against B.
  SmallPtrSet<const Value *, 4> UniqueSrc;
  for (Value *PV1 : A->incoming_values()) {
    if (UniqueSrc.insert(PV1).second && related(PV1, B, DL))
      return true;
  }

  // All of the arms checked out.
  return false;
}

/// Test if the value of P, or any value covered by its provenance, is ever
/// stored within the function (not counting callees).
static bool IsStoredObjCPointer(const Value *P) {
  SmallPtrSet<const Value *, 8> Visited;
  SmallVector<const Value *, 8> Worklist;
  Worklist.push_back(P);
  Visited.insert(P);
  do {
    P = Worklist.pop_back_val();
    for (const Use &U : P->uses()) {
      const User *Ur = U.getUser();
      if (isa<StoreInst>(Ur)) {
        if (U.getOperandNo() == 0)
          // The pointer is stored.
          return true;
        // The pointed is stored through.
        continue;
      }
      if (isa<CallInst>(Ur))
        // The pointer is passed as an argument, ignore this.
        continue;
      if (isa<PtrToIntInst>(P))
        // Assume the worst.
        return true;
      if (Visited.insert(Ur).second)
        Worklist.push_back(Ur);
    }
  } while (!Worklist.empty());

  // Everything checked out.
  return false;
}

bool ProvenanceAnalysis::relatedCheck(const Value *A, const Value *B,
                                      const DataLayout &DL) {
  // Ask regular AliasAnalysis, for a first approximation.
  switch (AA->alias(A, B)) {
  case NoAlias:
    return false;
  case MustAlias:
  case PartialAlias:
    return true;
  case MayAlias:
    break;
  }

  bool AIsIdentified = IsObjCIdentifiedObject(A);
  bool BIsIdentified = IsObjCIdentifiedObject(B);

  // An ObjC-Identified object can't alias a load if it is never locally stored.
  if (AIsIdentified) {
    // Check for an obvious escape.
    if (isa<LoadInst>(B))
      return IsStoredObjCPointer(A);
    if (BIsIdentified) {
      // Check for an obvious escape.
      if (isa<LoadInst>(A))
        return IsStoredObjCPointer(B);
      // Both pointers are identified and escapes aren't an evident problem.
      return false;
    }
  } else if (BIsIdentified) {
    // Check for an obvious escape.
    if (isa<LoadInst>(A))
      return IsStoredObjCPointer(B);
  }

   // Special handling for PHI and Select.
  if (const PHINode *PN = dyn_cast<PHINode>(A))
    return relatedPHI(PN, B);
  if (const PHINode *PN = dyn_cast<PHINode>(B))
    return relatedPHI(PN, A);
  if (const SelectInst *S = dyn_cast<SelectInst>(A))
    return relatedSelect(S, B);
  if (const SelectInst *S = dyn_cast<SelectInst>(B))
    return relatedSelect(S, A);

  // Conservative.
  return true;
}

bool ProvenanceAnalysis::related(const Value *A, const Value *B,
                                 const DataLayout &DL) {
  A = GetUnderlyingObjCPtrCached(A, DL, UnderlyingObjCPtrCache);
  B = GetUnderlyingObjCPtrCached(B, DL, UnderlyingObjCPtrCache);

  // Quick check.
  if (A == B)
    return true;

  // Begin by inserting a conservative value into the map. If the insertion
  // fails, we have the answer already. If it succeeds, leave it there until we
  // compute the real answer to guard against recursive queries.
  if (A > B) std::swap(A, B);
  std::pair<CachedResultsTy::iterator, bool> Pair =
    CachedResults.insert(std::make_pair(ValuePairTy(A, B), true));
  if (!Pair.second)
    return Pair.first->second;

  bool Result = relatedCheck(A, B, DL);
  CachedResults[ValuePairTy(A, B)] = Result;
  return Result;
}
