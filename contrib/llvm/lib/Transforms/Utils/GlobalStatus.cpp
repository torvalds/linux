//===-- GlobalStatus.cpp - Compute status info for globals -----------------==//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "llvm/Transforms/Utils/GlobalStatus.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/CallSite.h"
#include "llvm/IR/Constant.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/GlobalValue.h"
#include "llvm/IR/GlobalVariable.h"
#include "llvm/IR/InstrTypes.h"
#include "llvm/IR/Instruction.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/IntrinsicInst.h"
#include "llvm/IR/Use.h"
#include "llvm/IR/User.h"
#include "llvm/IR/Value.h"
#include "llvm/Support/AtomicOrdering.h"
#include "llvm/Support/Casting.h"
#include <algorithm>
#include <cassert>

using namespace llvm;

/// Return the stronger of the two ordering. If the two orderings are acquire
/// and release, then return AcquireRelease.
///
static AtomicOrdering strongerOrdering(AtomicOrdering X, AtomicOrdering Y) {
  if ((X == AtomicOrdering::Acquire && Y == AtomicOrdering::Release) ||
      (Y == AtomicOrdering::Acquire && X == AtomicOrdering::Release))
    return AtomicOrdering::AcquireRelease;
  return (AtomicOrdering)std::max((unsigned)X, (unsigned)Y);
}

/// It is safe to destroy a constant iff it is only used by constants itself.
/// Note that constants cannot be cyclic, so this test is pretty easy to
/// implement recursively.
///
bool llvm::isSafeToDestroyConstant(const Constant *C) {
  if (isa<GlobalValue>(C))
    return false;

  if (isa<ConstantData>(C))
    return false;

  for (const User *U : C->users())
    if (const Constant *CU = dyn_cast<Constant>(U)) {
      if (!isSafeToDestroyConstant(CU))
        return false;
    } else
      return false;
  return true;
}

static bool analyzeGlobalAux(const Value *V, GlobalStatus &GS,
                             SmallPtrSetImpl<const Value *> &VisitedUsers) {
  if (const GlobalVariable *GV = dyn_cast<GlobalVariable>(V))
    if (GV->isExternallyInitialized())
      GS.StoredType = GlobalStatus::StoredOnce;

  for (const Use &U : V->uses()) {
    const User *UR = U.getUser();
    if (const ConstantExpr *CE = dyn_cast<ConstantExpr>(UR)) {
      GS.HasNonInstructionUser = true;

      // If the result of the constantexpr isn't pointer type, then we won't
      // know to expect it in various places.  Just reject early.
      if (!isa<PointerType>(CE->getType()))
        return true;

      // FIXME: Do we need to add constexpr selects to VisitedUsers?
      if (analyzeGlobalAux(CE, GS, VisitedUsers))
        return true;
    } else if (const Instruction *I = dyn_cast<Instruction>(UR)) {
      if (!GS.HasMultipleAccessingFunctions) {
        const Function *F = I->getParent()->getParent();
        if (!GS.AccessingFunction)
          GS.AccessingFunction = F;
        else if (GS.AccessingFunction != F)
          GS.HasMultipleAccessingFunctions = true;
      }
      if (const LoadInst *LI = dyn_cast<LoadInst>(I)) {
        GS.IsLoaded = true;
        // Don't hack on volatile loads.
        if (LI->isVolatile())
          return true;
        GS.Ordering = strongerOrdering(GS.Ordering, LI->getOrdering());
      } else if (const StoreInst *SI = dyn_cast<StoreInst>(I)) {
        // Don't allow a store OF the address, only stores TO the address.
        if (SI->getOperand(0) == V)
          return true;

        // Don't hack on volatile stores.
        if (SI->isVolatile())
          return true;

        GS.Ordering = strongerOrdering(GS.Ordering, SI->getOrdering());

        // If this is a direct store to the global (i.e., the global is a scalar
        // value, not an aggregate), keep more specific information about
        // stores.
        if (GS.StoredType != GlobalStatus::Stored) {
          if (const GlobalVariable *GV =
                  dyn_cast<GlobalVariable>(SI->getOperand(1))) {
            Value *StoredVal = SI->getOperand(0);

            if (Constant *C = dyn_cast<Constant>(StoredVal)) {
              if (C->isThreadDependent()) {
                // The stored value changes between threads; don't track it.
                return true;
              }
            }

            if (GV->hasInitializer() && StoredVal == GV->getInitializer()) {
              if (GS.StoredType < GlobalStatus::InitializerStored)
                GS.StoredType = GlobalStatus::InitializerStored;
            } else if (isa<LoadInst>(StoredVal) &&
                       cast<LoadInst>(StoredVal)->getOperand(0) == GV) {
              if (GS.StoredType < GlobalStatus::InitializerStored)
                GS.StoredType = GlobalStatus::InitializerStored;
            } else if (GS.StoredType < GlobalStatus::StoredOnce) {
              GS.StoredType = GlobalStatus::StoredOnce;
              GS.StoredOnceValue = StoredVal;
            } else if (GS.StoredType == GlobalStatus::StoredOnce &&
                       GS.StoredOnceValue == StoredVal) {
              // noop.
            } else {
              GS.StoredType = GlobalStatus::Stored;
            }
          } else {
            GS.StoredType = GlobalStatus::Stored;
          }
        }
      } else if (isa<BitCastInst>(I) || isa<GetElementPtrInst>(I)) {
        // Skip over bitcasts and GEPs; we don't care about the type or offset
        // of the pointer.
        if (analyzeGlobalAux(I, GS, VisitedUsers))
          return true;
      } else if (isa<SelectInst>(I) || isa<PHINode>(I)) {
        // Look through selects and PHIs to find if the pointer is
        // conditionally accessed. Make sure we only visit an instruction
        // once; otherwise, we can get infinite recursion or exponential
        // compile time.
        if (VisitedUsers.insert(I).second)
          if (analyzeGlobalAux(I, GS, VisitedUsers))
            return true;
      } else if (isa<CmpInst>(I)) {
        GS.IsCompared = true;
      } else if (const MemTransferInst *MTI = dyn_cast<MemTransferInst>(I)) {
        if (MTI->isVolatile())
          return true;
        if (MTI->getArgOperand(0) == V)
          GS.StoredType = GlobalStatus::Stored;
        if (MTI->getArgOperand(1) == V)
          GS.IsLoaded = true;
      } else if (const MemSetInst *MSI = dyn_cast<MemSetInst>(I)) {
        assert(MSI->getArgOperand(0) == V && "Memset only takes one pointer!");
        if (MSI->isVolatile())
          return true;
        GS.StoredType = GlobalStatus::Stored;
      } else if (auto C = ImmutableCallSite(I)) {
        if (!C.isCallee(&U))
          return true;
        GS.IsLoaded = true;
      } else {
        return true; // Any other non-load instruction might take address!
      }
    } else if (const Constant *C = dyn_cast<Constant>(UR)) {
      GS.HasNonInstructionUser = true;
      // We might have a dead and dangling constant hanging off of here.
      if (!isSafeToDestroyConstant(C))
        return true;
    } else {
      GS.HasNonInstructionUser = true;
      // Otherwise must be some other user.
      return true;
    }
  }

  return false;
}

GlobalStatus::GlobalStatus() = default;

bool GlobalStatus::analyzeGlobal(const Value *V, GlobalStatus &GS) {
  SmallPtrSet<const Value *, 16> VisitedUsers;
  return analyzeGlobalAux(V, GS, VisitedUsers);
}
