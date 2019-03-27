//===- DependencyAnalysis.cpp - ObjC ARC Optimization ---------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
/// \file
///
/// This file defines special dependency analysis routines used in Objective C
/// ARC Optimizations.
///
/// WARNING: This file knows about certain library functions. It recognizes them
/// by name, and hardwires knowledge of their semantics.
///
/// WARNING: This file knows about how certain Objective-C library functions are
/// used. Naive LLVM IR transformations which would otherwise be
/// behavior-preserving may break these assumptions.
///
//===----------------------------------------------------------------------===//

#include "DependencyAnalysis.h"
#include "ObjCARC.h"
#include "ProvenanceAnalysis.h"
#include "llvm/IR/CFG.h"

using namespace llvm;
using namespace llvm::objcarc;

#define DEBUG_TYPE "objc-arc-dependency"

/// Test whether the given instruction can result in a reference count
/// modification (positive or negative) for the pointer's object.
bool llvm::objcarc::CanAlterRefCount(const Instruction *Inst, const Value *Ptr,
                                     ProvenanceAnalysis &PA,
                                     ARCInstKind Class) {
  switch (Class) {
  case ARCInstKind::Autorelease:
  case ARCInstKind::AutoreleaseRV:
  case ARCInstKind::IntrinsicUser:
  case ARCInstKind::User:
    // These operations never directly modify a reference count.
    return false;
  default: break;
  }

  const auto *Call = cast<CallBase>(Inst);

  // See if AliasAnalysis can help us with the call.
  FunctionModRefBehavior MRB = PA.getAA()->getModRefBehavior(Call);
  if (AliasAnalysis::onlyReadsMemory(MRB))
    return false;
  if (AliasAnalysis::onlyAccessesArgPointees(MRB)) {
    const DataLayout &DL = Inst->getModule()->getDataLayout();
    for (const Value *Op : Call->args()) {
      if (IsPotentialRetainableObjPtr(Op, *PA.getAA()) &&
          PA.related(Ptr, Op, DL))
        return true;
    }
    return false;
  }

  // Assume the worst.
  return true;
}

bool llvm::objcarc::CanDecrementRefCount(const Instruction *Inst,
                                         const Value *Ptr,
                                         ProvenanceAnalysis &PA,
                                         ARCInstKind Class) {
  // First perform a quick check if Class can not touch ref counts.
  if (!CanDecrementRefCount(Class))
    return false;

  // Otherwise, just use CanAlterRefCount for now.
  return CanAlterRefCount(Inst, Ptr, PA, Class);
}

/// Test whether the given instruction can "use" the given pointer's object in a
/// way that requires the reference count to be positive.
bool llvm::objcarc::CanUse(const Instruction *Inst, const Value *Ptr,
                           ProvenanceAnalysis &PA, ARCInstKind Class) {
  // ARCInstKind::Call operations (as opposed to
  // ARCInstKind::CallOrUser) never "use" objc pointers.
  if (Class == ARCInstKind::Call)
    return false;

  const DataLayout &DL = Inst->getModule()->getDataLayout();

  // Consider various instructions which may have pointer arguments which are
  // not "uses".
  if (const ICmpInst *ICI = dyn_cast<ICmpInst>(Inst)) {
    // Comparing a pointer with null, or any other constant, isn't really a use,
    // because we don't care what the pointer points to, or about the values
    // of any other dynamic reference-counted pointers.
    if (!IsPotentialRetainableObjPtr(ICI->getOperand(1), *PA.getAA()))
      return false;
  } else if (auto CS = ImmutableCallSite(Inst)) {
    // For calls, just check the arguments (and not the callee operand).
    for (ImmutableCallSite::arg_iterator OI = CS.arg_begin(),
         OE = CS.arg_end(); OI != OE; ++OI) {
      const Value *Op = *OI;
      if (IsPotentialRetainableObjPtr(Op, *PA.getAA()) &&
          PA.related(Ptr, Op, DL))
        return true;
    }
    return false;
  } else if (const StoreInst *SI = dyn_cast<StoreInst>(Inst)) {
    // Special-case stores, because we don't care about the stored value, just
    // the store address.
    const Value *Op = GetUnderlyingObjCPtr(SI->getPointerOperand(), DL);
    // If we can't tell what the underlying object was, assume there is a
    // dependence.
    return IsPotentialRetainableObjPtr(Op, *PA.getAA()) &&
           PA.related(Op, Ptr, DL);
  }

  // Check each operand for a match.
  for (User::const_op_iterator OI = Inst->op_begin(), OE = Inst->op_end();
       OI != OE; ++OI) {
    const Value *Op = *OI;
    if (IsPotentialRetainableObjPtr(Op, *PA.getAA()) && PA.related(Ptr, Op, DL))
      return true;
  }
  return false;
}

/// Test if there can be dependencies on Inst through Arg. This function only
/// tests dependencies relevant for removing pairs of calls.
bool
llvm::objcarc::Depends(DependenceKind Flavor, Instruction *Inst,
                       const Value *Arg, ProvenanceAnalysis &PA) {
  // If we've reached the definition of Arg, stop.
  if (Inst == Arg)
    return true;

  switch (Flavor) {
  case NeedsPositiveRetainCount: {
    ARCInstKind Class = GetARCInstKind(Inst);
    switch (Class) {
    case ARCInstKind::AutoreleasepoolPop:
    case ARCInstKind::AutoreleasepoolPush:
    case ARCInstKind::None:
      return false;
    default:
      return CanUse(Inst, Arg, PA, Class);
    }
  }

  case AutoreleasePoolBoundary: {
    ARCInstKind Class = GetARCInstKind(Inst);
    switch (Class) {
    case ARCInstKind::AutoreleasepoolPop:
    case ARCInstKind::AutoreleasepoolPush:
      // These mark the end and begin of an autorelease pool scope.
      return true;
    default:
      // Nothing else does this.
      return false;
    }
  }

  case CanChangeRetainCount: {
    ARCInstKind Class = GetARCInstKind(Inst);
    switch (Class) {
    case ARCInstKind::AutoreleasepoolPop:
      // Conservatively assume this can decrement any count.
      return true;
    case ARCInstKind::AutoreleasepoolPush:
    case ARCInstKind::None:
      return false;
    default:
      return CanAlterRefCount(Inst, Arg, PA, Class);
    }
  }

  case RetainAutoreleaseDep:
    switch (GetBasicARCInstKind(Inst)) {
    case ARCInstKind::AutoreleasepoolPop:
    case ARCInstKind::AutoreleasepoolPush:
      // Don't merge an objc_autorelease with an objc_retain inside a different
      // autoreleasepool scope.
      return true;
    case ARCInstKind::Retain:
    case ARCInstKind::RetainRV:
      // Check for a retain of the same pointer for merging.
      return GetArgRCIdentityRoot(Inst) == Arg;
    default:
      // Nothing else matters for objc_retainAutorelease formation.
      return false;
    }

  case RetainAutoreleaseRVDep: {
    ARCInstKind Class = GetBasicARCInstKind(Inst);
    switch (Class) {
    case ARCInstKind::Retain:
    case ARCInstKind::RetainRV:
      // Check for a retain of the same pointer for merging.
      return GetArgRCIdentityRoot(Inst) == Arg;
    default:
      // Anything that can autorelease interrupts
      // retainAutoreleaseReturnValue formation.
      return CanInterruptRV(Class);
    }
  }

  case RetainRVDep:
    return CanInterruptRV(GetBasicARCInstKind(Inst));
  }

  llvm_unreachable("Invalid dependence flavor");
}

/// Walk up the CFG from StartPos (which is in StartBB) and find local and
/// non-local dependencies on Arg.
///
/// TODO: Cache results?
void
llvm::objcarc::FindDependencies(DependenceKind Flavor,
                                const Value *Arg,
                                BasicBlock *StartBB, Instruction *StartInst,
                                SmallPtrSetImpl<Instruction *> &DependingInsts,
                                SmallPtrSetImpl<const BasicBlock *> &Visited,
                                ProvenanceAnalysis &PA) {
  BasicBlock::iterator StartPos = StartInst->getIterator();

  SmallVector<std::pair<BasicBlock *, BasicBlock::iterator>, 4> Worklist;
  Worklist.push_back(std::make_pair(StartBB, StartPos));
  do {
    std::pair<BasicBlock *, BasicBlock::iterator> Pair =
      Worklist.pop_back_val();
    BasicBlock *LocalStartBB = Pair.first;
    BasicBlock::iterator LocalStartPos = Pair.second;
    BasicBlock::iterator StartBBBegin = LocalStartBB->begin();
    for (;;) {
      if (LocalStartPos == StartBBBegin) {
        pred_iterator PI(LocalStartBB), PE(LocalStartBB, false);
        if (PI == PE)
          // If we've reached the function entry, produce a null dependence.
          DependingInsts.insert(nullptr);
        else
          // Add the predecessors to the worklist.
          do {
            BasicBlock *PredBB = *PI;
            if (Visited.insert(PredBB).second)
              Worklist.push_back(std::make_pair(PredBB, PredBB->end()));
          } while (++PI != PE);
        break;
      }

      Instruction *Inst = &*--LocalStartPos;
      if (Depends(Flavor, Inst, Arg, PA)) {
        DependingInsts.insert(Inst);
        break;
      }
    }
  } while (!Worklist.empty());

  // Determine whether the original StartBB post-dominates all of the blocks we
  // visited. If not, insert a sentinal indicating that most optimizations are
  // not safe.
  for (const BasicBlock *BB : Visited) {
    if (BB == StartBB)
      continue;
    for (const BasicBlock *Succ : successors(BB))
      if (Succ != StartBB && !Visited.count(Succ)) {
        DependingInsts.insert(reinterpret_cast<Instruction *>(-1));
        return;
      }
  }
}
