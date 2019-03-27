//===- TypeMetadataUtils.cpp - Utilities related to type metadata ---------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file contains functions that make it easier to manipulate type metadata
// for devirtualization.
//
//===----------------------------------------------------------------------===//

#include "llvm/Analysis/TypeMetadataUtils.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/Dominators.h"
#include "llvm/IR/Intrinsics.h"
#include "llvm/IR/Module.h"

using namespace llvm;

// Search for virtual calls that call FPtr and add them to DevirtCalls.
static void
findCallsAtConstantOffset(SmallVectorImpl<DevirtCallSite> &DevirtCalls,
                          bool *HasNonCallUses, Value *FPtr, uint64_t Offset,
                          const CallInst *CI, DominatorTree &DT) {
  for (const Use &U : FPtr->uses()) {
    Instruction *User = cast<Instruction>(U.getUser());
    // Ignore this instruction if it is not dominated by the type intrinsic
    // being analyzed. Otherwise we may transform a call sharing the same
    // vtable pointer incorrectly. Specifically, this situation can arise
    // after indirect call promotion and inlining, where we may have uses
    // of the vtable pointer guarded by a function pointer check, and a fallback
    // indirect call.
    if (!DT.dominates(CI, User))
      continue;
    if (isa<BitCastInst>(User)) {
      findCallsAtConstantOffset(DevirtCalls, HasNonCallUses, User, Offset, CI,
                                DT);
    } else if (auto CI = dyn_cast<CallInst>(User)) {
      DevirtCalls.push_back({Offset, CI});
    } else if (auto II = dyn_cast<InvokeInst>(User)) {
      DevirtCalls.push_back({Offset, II});
    } else if (HasNonCallUses) {
      *HasNonCallUses = true;
    }
  }
}

// Search for virtual calls that load from VPtr and add them to DevirtCalls.
static void findLoadCallsAtConstantOffset(
    const Module *M, SmallVectorImpl<DevirtCallSite> &DevirtCalls, Value *VPtr,
    int64_t Offset, const CallInst *CI, DominatorTree &DT) {
  for (const Use &U : VPtr->uses()) {
    Value *User = U.getUser();
    if (isa<BitCastInst>(User)) {
      findLoadCallsAtConstantOffset(M, DevirtCalls, User, Offset, CI, DT);
    } else if (isa<LoadInst>(User)) {
      findCallsAtConstantOffset(DevirtCalls, nullptr, User, Offset, CI, DT);
    } else if (auto GEP = dyn_cast<GetElementPtrInst>(User)) {
      // Take into account the GEP offset.
      if (VPtr == GEP->getPointerOperand() && GEP->hasAllConstantIndices()) {
        SmallVector<Value *, 8> Indices(GEP->op_begin() + 1, GEP->op_end());
        int64_t GEPOffset = M->getDataLayout().getIndexedOffsetInType(
            GEP->getSourceElementType(), Indices);
        findLoadCallsAtConstantOffset(M, DevirtCalls, User, Offset + GEPOffset,
                                      CI, DT);
      }
    }
  }
}

void llvm::findDevirtualizableCallsForTypeTest(
    SmallVectorImpl<DevirtCallSite> &DevirtCalls,
    SmallVectorImpl<CallInst *> &Assumes, const CallInst *CI,
    DominatorTree &DT) {
  assert(CI->getCalledFunction()->getIntrinsicID() == Intrinsic::type_test);

  const Module *M = CI->getParent()->getParent()->getParent();

  // Find llvm.assume intrinsics for this llvm.type.test call.
  for (const Use &CIU : CI->uses()) {
    if (auto *AssumeCI = dyn_cast<CallInst>(CIU.getUser())) {
      Function *F = AssumeCI->getCalledFunction();
      if (F && F->getIntrinsicID() == Intrinsic::assume)
        Assumes.push_back(AssumeCI);
    }
  }

  // If we found any, search for virtual calls based on %p and add them to
  // DevirtCalls.
  if (!Assumes.empty())
    findLoadCallsAtConstantOffset(
        M, DevirtCalls, CI->getArgOperand(0)->stripPointerCasts(), 0, CI, DT);
}

void llvm::findDevirtualizableCallsForTypeCheckedLoad(
    SmallVectorImpl<DevirtCallSite> &DevirtCalls,
    SmallVectorImpl<Instruction *> &LoadedPtrs,
    SmallVectorImpl<Instruction *> &Preds, bool &HasNonCallUses,
    const CallInst *CI, DominatorTree &DT) {
  assert(CI->getCalledFunction()->getIntrinsicID() ==
         Intrinsic::type_checked_load);

  auto *Offset = dyn_cast<ConstantInt>(CI->getArgOperand(1));
  if (!Offset) {
    HasNonCallUses = true;
    return;
  }

  for (const Use &U : CI->uses()) {
    auto CIU = U.getUser();
    if (auto EVI = dyn_cast<ExtractValueInst>(CIU)) {
      if (EVI->getNumIndices() == 1 && EVI->getIndices()[0] == 0) {
        LoadedPtrs.push_back(EVI);
        continue;
      }
      if (EVI->getNumIndices() == 1 && EVI->getIndices()[0] == 1) {
        Preds.push_back(EVI);
        continue;
      }
    }
    HasNonCallUses = true;
  }

  for (Value *LoadedPtr : LoadedPtrs)
    findCallsAtConstantOffset(DevirtCalls, &HasNonCallUses, LoadedPtr,
                              Offset->getZExtValue(), CI, DT);
}
