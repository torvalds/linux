//===- DeadStoreElimination.cpp - Fast Dead Store Elimination -------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file implements a trivial dead store elimination that only considers
// basic-block local redundant stores.
//
// FIXME: This should eventually be extended to be a post-dominator tree
// traversal.  Doing so would be pretty trivial.
//
//===----------------------------------------------------------------------===//

#include "llvm/Transforms/Scalar/DeadStoreElimination.h"
#include "llvm/ADT/APInt.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/SetVector.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Analysis/AliasAnalysis.h"
#include "llvm/Analysis/CaptureTracking.h"
#include "llvm/Analysis/GlobalsModRef.h"
#include "llvm/Analysis/MemoryBuiltins.h"
#include "llvm/Analysis/MemoryDependenceAnalysis.h"
#include "llvm/Analysis/MemoryLocation.h"
#include "llvm/Analysis/TargetLibraryInfo.h"
#include "llvm/Transforms/Utils/Local.h"
#include "llvm/Analysis/ValueTracking.h"
#include "llvm/IR/Argument.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/CallSite.h"
#include "llvm/IR/Constant.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/IR/Dominators.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/InstrTypes.h"
#include "llvm/IR/Instruction.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/IntrinsicInst.h"
#include "llvm/IR/Intrinsics.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/PassManager.h"
#include "llvm/IR/Value.h"
#include "llvm/Pass.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/MathExtras.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Transforms/Scalar.h"
#include <algorithm>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <map>
#include <utility>

using namespace llvm;

#define DEBUG_TYPE "dse"

STATISTIC(NumRedundantStores, "Number of redundant stores deleted");
STATISTIC(NumFastStores, "Number of stores deleted");
STATISTIC(NumFastOther, "Number of other instrs removed");
STATISTIC(NumCompletePartials, "Number of stores dead by later partials");
STATISTIC(NumModifiedStores, "Number of stores modified");

static cl::opt<bool>
EnablePartialOverwriteTracking("enable-dse-partial-overwrite-tracking",
  cl::init(true), cl::Hidden,
  cl::desc("Enable partial-overwrite tracking in DSE"));

static cl::opt<bool>
EnablePartialStoreMerging("enable-dse-partial-store-merging",
  cl::init(true), cl::Hidden,
  cl::desc("Enable partial store merging in DSE"));

//===----------------------------------------------------------------------===//
// Helper functions
//===----------------------------------------------------------------------===//
using OverlapIntervalsTy = std::map<int64_t, int64_t>;
using InstOverlapIntervalsTy = DenseMap<Instruction *, OverlapIntervalsTy>;

/// Delete this instruction.  Before we do, go through and zero out all the
/// operands of this instruction.  If any of them become dead, delete them and
/// the computation tree that feeds them.
/// If ValueSet is non-null, remove any deleted instructions from it as well.
static void
deleteDeadInstruction(Instruction *I, BasicBlock::iterator *BBI,
                      MemoryDependenceResults &MD, const TargetLibraryInfo &TLI,
                      InstOverlapIntervalsTy &IOL,
                      DenseMap<Instruction*, size_t> *InstrOrdering,
                      SmallSetVector<Value *, 16> *ValueSet = nullptr) {
  SmallVector<Instruction*, 32> NowDeadInsts;

  NowDeadInsts.push_back(I);
  --NumFastOther;

  // Keeping the iterator straight is a pain, so we let this routine tell the
  // caller what the next instruction is after we're done mucking about.
  BasicBlock::iterator NewIter = *BBI;

  // Before we touch this instruction, remove it from memdep!
  do {
    Instruction *DeadInst = NowDeadInsts.pop_back_val();
    ++NumFastOther;

    // Try to preserve debug information attached to the dead instruction.
    salvageDebugInfo(*DeadInst);

    // This instruction is dead, zap it, in stages.  Start by removing it from
    // MemDep, which needs to know the operands and needs it to be in the
    // function.
    MD.removeInstruction(DeadInst);

    for (unsigned op = 0, e = DeadInst->getNumOperands(); op != e; ++op) {
      Value *Op = DeadInst->getOperand(op);
      DeadInst->setOperand(op, nullptr);

      // If this operand just became dead, add it to the NowDeadInsts list.
      if (!Op->use_empty()) continue;

      if (Instruction *OpI = dyn_cast<Instruction>(Op))
        if (isInstructionTriviallyDead(OpI, &TLI))
          NowDeadInsts.push_back(OpI);
    }

    if (ValueSet) ValueSet->remove(DeadInst);
    InstrOrdering->erase(DeadInst);
    IOL.erase(DeadInst);

    if (NewIter == DeadInst->getIterator())
      NewIter = DeadInst->eraseFromParent();
    else
      DeadInst->eraseFromParent();
  } while (!NowDeadInsts.empty());
  *BBI = NewIter;
}

/// Does this instruction write some memory?  This only returns true for things
/// that we can analyze with other helpers below.
static bool hasAnalyzableMemoryWrite(Instruction *I,
                                     const TargetLibraryInfo &TLI) {
  if (isa<StoreInst>(I))
    return true;
  if (IntrinsicInst *II = dyn_cast<IntrinsicInst>(I)) {
    switch (II->getIntrinsicID()) {
    default:
      return false;
    case Intrinsic::memset:
    case Intrinsic::memmove:
    case Intrinsic::memcpy:
    case Intrinsic::memcpy_element_unordered_atomic:
    case Intrinsic::memmove_element_unordered_atomic:
    case Intrinsic::memset_element_unordered_atomic:
    case Intrinsic::init_trampoline:
    case Intrinsic::lifetime_end:
      return true;
    }
  }
  if (auto CS = CallSite(I)) {
    if (Function *F = CS.getCalledFunction()) {
      StringRef FnName = F->getName();
      if (TLI.has(LibFunc_strcpy) && FnName == TLI.getName(LibFunc_strcpy))
        return true;
      if (TLI.has(LibFunc_strncpy) && FnName == TLI.getName(LibFunc_strncpy))
        return true;
      if (TLI.has(LibFunc_strcat) && FnName == TLI.getName(LibFunc_strcat))
        return true;
      if (TLI.has(LibFunc_strncat) && FnName == TLI.getName(LibFunc_strncat))
        return true;
    }
  }
  return false;
}

/// Return a Location stored to by the specified instruction. If isRemovable
/// returns true, this function and getLocForRead completely describe the memory
/// operations for this instruction.
static MemoryLocation getLocForWrite(Instruction *Inst) {

  if (StoreInst *SI = dyn_cast<StoreInst>(Inst))
    return MemoryLocation::get(SI);

  if (auto *MI = dyn_cast<AnyMemIntrinsic>(Inst)) {
    // memcpy/memmove/memset.
    MemoryLocation Loc = MemoryLocation::getForDest(MI);
    return Loc;
  }

  if (IntrinsicInst *II = dyn_cast<IntrinsicInst>(Inst)) {
    switch (II->getIntrinsicID()) {
    default:
      return MemoryLocation(); // Unhandled intrinsic.
    case Intrinsic::init_trampoline:
      return MemoryLocation(II->getArgOperand(0));
    case Intrinsic::lifetime_end: {
      uint64_t Len = cast<ConstantInt>(II->getArgOperand(0))->getZExtValue();
      return MemoryLocation(II->getArgOperand(1), Len);
    }
    }
  }
  if (auto CS = CallSite(Inst))
    // All the supported TLI functions so far happen to have dest as their
    // first argument.
    return MemoryLocation(CS.getArgument(0));
  return MemoryLocation();
}

/// Return the location read by the specified "hasAnalyzableMemoryWrite"
/// instruction if any.
static MemoryLocation getLocForRead(Instruction *Inst,
                                    const TargetLibraryInfo &TLI) {
  assert(hasAnalyzableMemoryWrite(Inst, TLI) && "Unknown instruction case");

  // The only instructions that both read and write are the mem transfer
  // instructions (memcpy/memmove).
  if (auto *MTI = dyn_cast<AnyMemTransferInst>(Inst))
    return MemoryLocation::getForSource(MTI);
  return MemoryLocation();
}

/// If the value of this instruction and the memory it writes to is unused, may
/// we delete this instruction?
static bool isRemovable(Instruction *I) {
  // Don't remove volatile/atomic stores.
  if (StoreInst *SI = dyn_cast<StoreInst>(I))
    return SI->isUnordered();

  if (IntrinsicInst *II = dyn_cast<IntrinsicInst>(I)) {
    switch (II->getIntrinsicID()) {
    default: llvm_unreachable("doesn't pass 'hasAnalyzableMemoryWrite' predicate");
    case Intrinsic::lifetime_end:
      // Never remove dead lifetime_end's, e.g. because it is followed by a
      // free.
      return false;
    case Intrinsic::init_trampoline:
      // Always safe to remove init_trampoline.
      return true;
    case Intrinsic::memset:
    case Intrinsic::memmove:
    case Intrinsic::memcpy:
      // Don't remove volatile memory intrinsics.
      return !cast<MemIntrinsic>(II)->isVolatile();
    case Intrinsic::memcpy_element_unordered_atomic:
    case Intrinsic::memmove_element_unordered_atomic:
    case Intrinsic::memset_element_unordered_atomic:
      return true;
    }
  }

  // note: only get here for calls with analyzable writes - i.e. libcalls
  if (auto CS = CallSite(I))
    return CS.getInstruction()->use_empty();

  return false;
}

/// Returns true if the end of this instruction can be safely shortened in
/// length.
static bool isShortenableAtTheEnd(Instruction *I) {
  // Don't shorten stores for now
  if (isa<StoreInst>(I))
    return false;

  if (IntrinsicInst *II = dyn_cast<IntrinsicInst>(I)) {
    switch (II->getIntrinsicID()) {
      default: return false;
      case Intrinsic::memset:
      case Intrinsic::memcpy:
      case Intrinsic::memcpy_element_unordered_atomic:
      case Intrinsic::memset_element_unordered_atomic:
        // Do shorten memory intrinsics.
        // FIXME: Add memmove if it's also safe to transform.
        return true;
    }
  }

  // Don't shorten libcalls calls for now.

  return false;
}

/// Returns true if the beginning of this instruction can be safely shortened
/// in length.
static bool isShortenableAtTheBeginning(Instruction *I) {
  // FIXME: Handle only memset for now. Supporting memcpy/memmove should be
  // easily done by offsetting the source address.
  return isa<AnyMemSetInst>(I);
}

/// Return the pointer that is being written to.
static Value *getStoredPointerOperand(Instruction *I) {
  //TODO: factor this to reuse getLocForWrite
  MemoryLocation Loc = getLocForWrite(I);
  assert(Loc.Ptr &&
         "unable to find pointer written for analyzable instruction?");
  // TODO: most APIs don't expect const Value *
  return const_cast<Value*>(Loc.Ptr);
}

static uint64_t getPointerSize(const Value *V, const DataLayout &DL,
                               const TargetLibraryInfo &TLI,
                               const Function *F) {
  uint64_t Size;
  ObjectSizeOpts Opts;
  Opts.NullIsUnknownSize = NullPointerIsDefined(F);

  if (getObjectSize(V, Size, DL, &TLI, Opts))
    return Size;
  return MemoryLocation::UnknownSize;
}

namespace {

enum OverwriteResult {
  OW_Begin,
  OW_Complete,
  OW_End,
  OW_PartialEarlierWithFullLater,
  OW_Unknown
};

} // end anonymous namespace

/// Return 'OW_Complete' if a store to the 'Later' location completely
/// overwrites a store to the 'Earlier' location, 'OW_End' if the end of the
/// 'Earlier' location is completely overwritten by 'Later', 'OW_Begin' if the
/// beginning of the 'Earlier' location is overwritten by 'Later'.
/// 'OW_PartialEarlierWithFullLater' means that an earlier (big) store was
/// overwritten by a latter (smaller) store which doesn't write outside the big
/// store's memory locations. Returns 'OW_Unknown' if nothing can be determined.
static OverwriteResult isOverwrite(const MemoryLocation &Later,
                                   const MemoryLocation &Earlier,
                                   const DataLayout &DL,
                                   const TargetLibraryInfo &TLI,
                                   int64_t &EarlierOff, int64_t &LaterOff,
                                   Instruction *DepWrite,
                                   InstOverlapIntervalsTy &IOL,
                                   AliasAnalysis &AA,
                                   const Function *F) {
  // FIXME: Vet that this works for size upper-bounds. Seems unlikely that we'll
  // get imprecise values here, though (except for unknown sizes).
  if (!Later.Size.isPrecise() || !Earlier.Size.isPrecise())
    return OW_Unknown;

  const uint64_t LaterSize = Later.Size.getValue();
  const uint64_t EarlierSize = Earlier.Size.getValue();

  const Value *P1 = Earlier.Ptr->stripPointerCasts();
  const Value *P2 = Later.Ptr->stripPointerCasts();

  // If the start pointers are the same, we just have to compare sizes to see if
  // the later store was larger than the earlier store.
  if (P1 == P2 || AA.isMustAlias(P1, P2)) {
    // Make sure that the Later size is >= the Earlier size.
    if (LaterSize >= EarlierSize)
      return OW_Complete;
  }

  // Check to see if the later store is to the entire object (either a global,
  // an alloca, or a byval/inalloca argument).  If so, then it clearly
  // overwrites any other store to the same object.
  const Value *UO1 = GetUnderlyingObject(P1, DL),
              *UO2 = GetUnderlyingObject(P2, DL);

  // If we can't resolve the same pointers to the same object, then we can't
  // analyze them at all.
  if (UO1 != UO2)
    return OW_Unknown;

  // If the "Later" store is to a recognizable object, get its size.
  uint64_t ObjectSize = getPointerSize(UO2, DL, TLI, F);
  if (ObjectSize != MemoryLocation::UnknownSize)
    if (ObjectSize == LaterSize && ObjectSize >= EarlierSize)
      return OW_Complete;

  // Okay, we have stores to two completely different pointers.  Try to
  // decompose the pointer into a "base + constant_offset" form.  If the base
  // pointers are equal, then we can reason about the two stores.
  EarlierOff = 0;
  LaterOff = 0;
  const Value *BP1 = GetPointerBaseWithConstantOffset(P1, EarlierOff, DL);
  const Value *BP2 = GetPointerBaseWithConstantOffset(P2, LaterOff, DL);

  // If the base pointers still differ, we have two completely different stores.
  if (BP1 != BP2)
    return OW_Unknown;

  // The later store completely overlaps the earlier store if:
  //
  // 1. Both start at the same offset and the later one's size is greater than
  //    or equal to the earlier one's, or
  //
  //      |--earlier--|
  //      |--   later   --|
  //
  // 2. The earlier store has an offset greater than the later offset, but which
  //    still lies completely within the later store.
  //
  //        |--earlier--|
  //    |-----  later  ------|
  //
  // We have to be careful here as *Off is signed while *.Size is unsigned.
  if (EarlierOff >= LaterOff &&
      LaterSize >= EarlierSize &&
      uint64_t(EarlierOff - LaterOff) + EarlierSize <= LaterSize)
    return OW_Complete;

  // We may now overlap, although the overlap is not complete. There might also
  // be other incomplete overlaps, and together, they might cover the complete
  // earlier write.
  // Note: The correctness of this logic depends on the fact that this function
  // is not even called providing DepWrite when there are any intervening reads.
  if (EnablePartialOverwriteTracking &&
      LaterOff < int64_t(EarlierOff + EarlierSize) &&
      int64_t(LaterOff + LaterSize) >= EarlierOff) {

    // Insert our part of the overlap into the map.
    auto &IM = IOL[DepWrite];
    LLVM_DEBUG(dbgs() << "DSE: Partial overwrite: Earlier [" << EarlierOff
                      << ", " << int64_t(EarlierOff + EarlierSize)
                      << ") Later [" << LaterOff << ", "
                      << int64_t(LaterOff + LaterSize) << ")\n");

    // Make sure that we only insert non-overlapping intervals and combine
    // adjacent intervals. The intervals are stored in the map with the ending
    // offset as the key (in the half-open sense) and the starting offset as
    // the value.
    int64_t LaterIntStart = LaterOff, LaterIntEnd = LaterOff + LaterSize;

    // Find any intervals ending at, or after, LaterIntStart which start
    // before LaterIntEnd.
    auto ILI = IM.lower_bound(LaterIntStart);
    if (ILI != IM.end() && ILI->second <= LaterIntEnd) {
      // This existing interval is overlapped with the current store somewhere
      // in [LaterIntStart, LaterIntEnd]. Merge them by erasing the existing
      // intervals and adjusting our start and end.
      LaterIntStart = std::min(LaterIntStart, ILI->second);
      LaterIntEnd = std::max(LaterIntEnd, ILI->first);
      ILI = IM.erase(ILI);

      // Continue erasing and adjusting our end in case other previous
      // intervals are also overlapped with the current store.
      //
      // |--- ealier 1 ---|  |--- ealier 2 ---|
      //     |------- later---------|
      //
      while (ILI != IM.end() && ILI->second <= LaterIntEnd) {
        assert(ILI->second > LaterIntStart && "Unexpected interval");
        LaterIntEnd = std::max(LaterIntEnd, ILI->first);
        ILI = IM.erase(ILI);
      }
    }

    IM[LaterIntEnd] = LaterIntStart;

    ILI = IM.begin();
    if (ILI->second <= EarlierOff &&
        ILI->first >= int64_t(EarlierOff + EarlierSize)) {
      LLVM_DEBUG(dbgs() << "DSE: Full overwrite from partials: Earlier ["
                        << EarlierOff << ", "
                        << int64_t(EarlierOff + EarlierSize)
                        << ") Composite Later [" << ILI->second << ", "
                        << ILI->first << ")\n");
      ++NumCompletePartials;
      return OW_Complete;
    }
  }

  // Check for an earlier store which writes to all the memory locations that
  // the later store writes to.
  if (EnablePartialStoreMerging && LaterOff >= EarlierOff &&
      int64_t(EarlierOff + EarlierSize) > LaterOff &&
      uint64_t(LaterOff - EarlierOff) + LaterSize <= EarlierSize) {
    LLVM_DEBUG(dbgs() << "DSE: Partial overwrite an earlier load ["
                      << EarlierOff << ", "
                      << int64_t(EarlierOff + EarlierSize)
                      << ") by a later store [" << LaterOff << ", "
                      << int64_t(LaterOff + LaterSize) << ")\n");
    // TODO: Maybe come up with a better name?
    return OW_PartialEarlierWithFullLater;
  }

  // Another interesting case is if the later store overwrites the end of the
  // earlier store.
  //
  //      |--earlier--|
  //                |--   later   --|
  //
  // In this case we may want to trim the size of earlier to avoid generating
  // writes to addresses which will definitely be overwritten later
  if (!EnablePartialOverwriteTracking &&
      (LaterOff > EarlierOff && LaterOff < int64_t(EarlierOff + EarlierSize) &&
       int64_t(LaterOff + LaterSize) >= int64_t(EarlierOff + EarlierSize)))
    return OW_End;

  // Finally, we also need to check if the later store overwrites the beginning
  // of the earlier store.
  //
  //                |--earlier--|
  //      |--   later   --|
  //
  // In this case we may want to move the destination address and trim the size
  // of earlier to avoid generating writes to addresses which will definitely
  // be overwritten later.
  if (!EnablePartialOverwriteTracking &&
      (LaterOff <= EarlierOff && int64_t(LaterOff + LaterSize) > EarlierOff)) {
    assert(int64_t(LaterOff + LaterSize) < int64_t(EarlierOff + EarlierSize) &&
           "Expect to be handled as OW_Complete");
    return OW_Begin;
  }
  // Otherwise, they don't completely overlap.
  return OW_Unknown;
}

/// If 'Inst' might be a self read (i.e. a noop copy of a
/// memory region into an identical pointer) then it doesn't actually make its
/// input dead in the traditional sense.  Consider this case:
///
///   memmove(A <- B)
///   memmove(A <- A)
///
/// In this case, the second store to A does not make the first store to A dead.
/// The usual situation isn't an explicit A<-A store like this (which can be
/// trivially removed) but a case where two pointers may alias.
///
/// This function detects when it is unsafe to remove a dependent instruction
/// because the DSE inducing instruction may be a self-read.
static bool isPossibleSelfRead(Instruction *Inst,
                               const MemoryLocation &InstStoreLoc,
                               Instruction *DepWrite,
                               const TargetLibraryInfo &TLI,
                               AliasAnalysis &AA) {
  // Self reads can only happen for instructions that read memory.  Get the
  // location read.
  MemoryLocation InstReadLoc = getLocForRead(Inst, TLI);
  if (!InstReadLoc.Ptr)
    return false; // Not a reading instruction.

  // If the read and written loc obviously don't alias, it isn't a read.
  if (AA.isNoAlias(InstReadLoc, InstStoreLoc))
    return false;

  if (isa<AnyMemCpyInst>(Inst)) {
    // LLVM's memcpy overlap semantics are not fully fleshed out (see PR11763)
    // but in practice memcpy(A <- B) either means that A and B are disjoint or
    // are equal (i.e. there are not partial overlaps).  Given that, if we have:
    //
    //   memcpy/memmove(A <- B)  // DepWrite
    //   memcpy(A <- B)  // Inst
    //
    // with Inst reading/writing a >= size than DepWrite, we can reason as
    // follows:
    //
    //   - If A == B then both the copies are no-ops, so the DepWrite can be
    //     removed.
    //   - If A != B then A and B are disjoint locations in Inst.  Since
    //     Inst.size >= DepWrite.size A and B are disjoint in DepWrite too.
    //     Therefore DepWrite can be removed.
    MemoryLocation DepReadLoc = getLocForRead(DepWrite, TLI);

    if (DepReadLoc.Ptr && AA.isMustAlias(InstReadLoc.Ptr, DepReadLoc.Ptr))
      return false;
  }

  // If DepWrite doesn't read memory or if we can't prove it is a must alias,
  // then it can't be considered dead.
  return true;
}

/// Returns true if the memory which is accessed by the second instruction is not
/// modified between the first and the second instruction.
/// Precondition: Second instruction must be dominated by the first
/// instruction.
static bool memoryIsNotModifiedBetween(Instruction *FirstI,
                                       Instruction *SecondI,
                                       AliasAnalysis *AA) {
  SmallVector<BasicBlock *, 16> WorkList;
  SmallPtrSet<BasicBlock *, 8> Visited;
  BasicBlock::iterator FirstBBI(FirstI);
  ++FirstBBI;
  BasicBlock::iterator SecondBBI(SecondI);
  BasicBlock *FirstBB = FirstI->getParent();
  BasicBlock *SecondBB = SecondI->getParent();
  MemoryLocation MemLoc = MemoryLocation::get(SecondI);

  // Start checking the store-block.
  WorkList.push_back(SecondBB);
  bool isFirstBlock = true;

  // Check all blocks going backward until we reach the load-block.
  while (!WorkList.empty()) {
    BasicBlock *B = WorkList.pop_back_val();

    // Ignore instructions before LI if this is the FirstBB.
    BasicBlock::iterator BI = (B == FirstBB ? FirstBBI : B->begin());

    BasicBlock::iterator EI;
    if (isFirstBlock) {
      // Ignore instructions after SI if this is the first visit of SecondBB.
      assert(B == SecondBB && "first block is not the store block");
      EI = SecondBBI;
      isFirstBlock = false;
    } else {
      // It's not SecondBB or (in case of a loop) the second visit of SecondBB.
      // In this case we also have to look at instructions after SI.
      EI = B->end();
    }
    for (; BI != EI; ++BI) {
      Instruction *I = &*BI;
      if (I->mayWriteToMemory() && I != SecondI)
        if (isModSet(AA->getModRefInfo(I, MemLoc)))
          return false;
    }
    if (B != FirstBB) {
      assert(B != &FirstBB->getParent()->getEntryBlock() &&
          "Should not hit the entry block because SI must be dominated by LI");
      for (auto PredI = pred_begin(B), PE = pred_end(B); PredI != PE; ++PredI) {
        if (!Visited.insert(*PredI).second)
          continue;
        WorkList.push_back(*PredI);
      }
    }
  }
  return true;
}

/// Find all blocks that will unconditionally lead to the block BB and append
/// them to F.
static void findUnconditionalPreds(SmallVectorImpl<BasicBlock *> &Blocks,
                                   BasicBlock *BB, DominatorTree *DT) {
  for (pred_iterator I = pred_begin(BB), E = pred_end(BB); I != E; ++I) {
    BasicBlock *Pred = *I;
    if (Pred == BB) continue;
    Instruction *PredTI = Pred->getTerminator();
    if (PredTI->getNumSuccessors() != 1)
      continue;

    if (DT->isReachableFromEntry(Pred))
      Blocks.push_back(Pred);
  }
}

/// Handle frees of entire structures whose dependency is a store
/// to a field of that structure.
static bool handleFree(CallInst *F, AliasAnalysis *AA,
                       MemoryDependenceResults *MD, DominatorTree *DT,
                       const TargetLibraryInfo *TLI,
                       InstOverlapIntervalsTy &IOL,
                       DenseMap<Instruction*, size_t> *InstrOrdering) {
  bool MadeChange = false;

  MemoryLocation Loc = MemoryLocation(F->getOperand(0));
  SmallVector<BasicBlock *, 16> Blocks;
  Blocks.push_back(F->getParent());
  const DataLayout &DL = F->getModule()->getDataLayout();

  while (!Blocks.empty()) {
    BasicBlock *BB = Blocks.pop_back_val();
    Instruction *InstPt = BB->getTerminator();
    if (BB == F->getParent()) InstPt = F;

    MemDepResult Dep =
        MD->getPointerDependencyFrom(Loc, false, InstPt->getIterator(), BB);
    while (Dep.isDef() || Dep.isClobber()) {
      Instruction *Dependency = Dep.getInst();
      if (!hasAnalyzableMemoryWrite(Dependency, *TLI) ||
          !isRemovable(Dependency))
        break;

      Value *DepPointer =
          GetUnderlyingObject(getStoredPointerOperand(Dependency), DL);

      // Check for aliasing.
      if (!AA->isMustAlias(F->getArgOperand(0), DepPointer))
        break;

      LLVM_DEBUG(
          dbgs() << "DSE: Dead Store to soon to be freed memory:\n  DEAD: "
                 << *Dependency << '\n');

      // DCE instructions only used to calculate that store.
      BasicBlock::iterator BBI(Dependency);
      deleteDeadInstruction(Dependency, &BBI, *MD, *TLI, IOL, InstrOrdering);
      ++NumFastStores;
      MadeChange = true;

      // Inst's old Dependency is now deleted. Compute the next dependency,
      // which may also be dead, as in
      //    s[0] = 0;
      //    s[1] = 0; // This has just been deleted.
      //    free(s);
      Dep = MD->getPointerDependencyFrom(Loc, false, BBI, BB);
    }

    if (Dep.isNonLocal())
      findUnconditionalPreds(Blocks, BB, DT);
  }

  return MadeChange;
}

/// Check to see if the specified location may alias any of the stack objects in
/// the DeadStackObjects set. If so, they become live because the location is
/// being loaded.
static void removeAccessedObjects(const MemoryLocation &LoadedLoc,
                                  SmallSetVector<Value *, 16> &DeadStackObjects,
                                  const DataLayout &DL, AliasAnalysis *AA,
                                  const TargetLibraryInfo *TLI,
                                  const Function *F) {
  const Value *UnderlyingPointer = GetUnderlyingObject(LoadedLoc.Ptr, DL);

  // A constant can't be in the dead pointer set.
  if (isa<Constant>(UnderlyingPointer))
    return;

  // If the kill pointer can be easily reduced to an alloca, don't bother doing
  // extraneous AA queries.
  if (isa<AllocaInst>(UnderlyingPointer) || isa<Argument>(UnderlyingPointer)) {
    DeadStackObjects.remove(const_cast<Value*>(UnderlyingPointer));
    return;
  }

  // Remove objects that could alias LoadedLoc.
  DeadStackObjects.remove_if([&](Value *I) {
    // See if the loaded location could alias the stack location.
    MemoryLocation StackLoc(I, getPointerSize(I, DL, *TLI, F));
    return !AA->isNoAlias(StackLoc, LoadedLoc);
  });
}

/// Remove dead stores to stack-allocated locations in the function end block.
/// Ex:
/// %A = alloca i32
/// ...
/// store i32 1, i32* %A
/// ret void
static bool handleEndBlock(BasicBlock &BB, AliasAnalysis *AA,
                             MemoryDependenceResults *MD,
                             const TargetLibraryInfo *TLI,
                             InstOverlapIntervalsTy &IOL,
                             DenseMap<Instruction*, size_t> *InstrOrdering) {
  bool MadeChange = false;

  // Keep track of all of the stack objects that are dead at the end of the
  // function.
  SmallSetVector<Value*, 16> DeadStackObjects;

  // Find all of the alloca'd pointers in the entry block.
  BasicBlock &Entry = BB.getParent()->front();
  for (Instruction &I : Entry) {
    if (isa<AllocaInst>(&I))
      DeadStackObjects.insert(&I);

    // Okay, so these are dead heap objects, but if the pointer never escapes
    // then it's leaked by this function anyways.
    else if (isAllocLikeFn(&I, TLI) && !PointerMayBeCaptured(&I, true, true))
      DeadStackObjects.insert(&I);
  }

  // Treat byval or inalloca arguments the same, stores to them are dead at the
  // end of the function.
  for (Argument &AI : BB.getParent()->args())
    if (AI.hasByValOrInAllocaAttr())
      DeadStackObjects.insert(&AI);

  const DataLayout &DL = BB.getModule()->getDataLayout();

  // Scan the basic block backwards
  for (BasicBlock::iterator BBI = BB.end(); BBI != BB.begin(); ){
    --BBI;

    // If we find a store, check to see if it points into a dead stack value.
    if (hasAnalyzableMemoryWrite(&*BBI, *TLI) && isRemovable(&*BBI)) {
      // See through pointer-to-pointer bitcasts
      SmallVector<Value *, 4> Pointers;
      GetUnderlyingObjects(getStoredPointerOperand(&*BBI), Pointers, DL);

      // Stores to stack values are valid candidates for removal.
      bool AllDead = true;
      for (Value *Pointer : Pointers)
        if (!DeadStackObjects.count(Pointer)) {
          AllDead = false;
          break;
        }

      if (AllDead) {
        Instruction *Dead = &*BBI;

        LLVM_DEBUG(dbgs() << "DSE: Dead Store at End of Block:\n  DEAD: "
                          << *Dead << "\n  Objects: ";
                   for (SmallVectorImpl<Value *>::iterator I = Pointers.begin(),
                        E = Pointers.end();
                        I != E; ++I) {
                     dbgs() << **I;
                     if (std::next(I) != E)
                       dbgs() << ", ";
                   } dbgs()
                   << '\n');

        // DCE instructions only used to calculate that store.
        deleteDeadInstruction(Dead, &BBI, *MD, *TLI, IOL, InstrOrdering, &DeadStackObjects);
        ++NumFastStores;
        MadeChange = true;
        continue;
      }
    }

    // Remove any dead non-memory-mutating instructions.
    if (isInstructionTriviallyDead(&*BBI, TLI)) {
      LLVM_DEBUG(dbgs() << "DSE: Removing trivially dead instruction:\n  DEAD: "
                        << *&*BBI << '\n');
      deleteDeadInstruction(&*BBI, &BBI, *MD, *TLI, IOL, InstrOrdering, &DeadStackObjects);
      ++NumFastOther;
      MadeChange = true;
      continue;
    }

    if (isa<AllocaInst>(BBI)) {
      // Remove allocas from the list of dead stack objects; there can't be
      // any references before the definition.
      DeadStackObjects.remove(&*BBI);
      continue;
    }

    if (auto *Call = dyn_cast<CallBase>(&*BBI)) {
      // Remove allocation function calls from the list of dead stack objects;
      // there can't be any references before the definition.
      if (isAllocLikeFn(&*BBI, TLI))
        DeadStackObjects.remove(&*BBI);

      // If this call does not access memory, it can't be loading any of our
      // pointers.
      if (AA->doesNotAccessMemory(Call))
        continue;

      // If the call might load from any of our allocas, then any store above
      // the call is live.
      DeadStackObjects.remove_if([&](Value *I) {
        // See if the call site touches the value.
        return isRefSet(AA->getModRefInfo(
            Call, I, getPointerSize(I, DL, *TLI, BB.getParent())));
      });

      // If all of the allocas were clobbered by the call then we're not going
      // to find anything else to process.
      if (DeadStackObjects.empty())
        break;

      continue;
    }

    // We can remove the dead stores, irrespective of the fence and its ordering
    // (release/acquire/seq_cst). Fences only constraints the ordering of
    // already visible stores, it does not make a store visible to other
    // threads. So, skipping over a fence does not change a store from being
    // dead.
    if (isa<FenceInst>(*BBI))
      continue;

    MemoryLocation LoadedLoc;

    // If we encounter a use of the pointer, it is no longer considered dead
    if (LoadInst *L = dyn_cast<LoadInst>(BBI)) {
      if (!L->isUnordered()) // Be conservative with atomic/volatile load
        break;
      LoadedLoc = MemoryLocation::get(L);
    } else if (VAArgInst *V = dyn_cast<VAArgInst>(BBI)) {
      LoadedLoc = MemoryLocation::get(V);
    } else if (!BBI->mayReadFromMemory()) {
      // Instruction doesn't read memory.  Note that stores that weren't removed
      // above will hit this case.
      continue;
    } else {
      // Unknown inst; assume it clobbers everything.
      break;
    }

    // Remove any allocas from the DeadPointer set that are loaded, as this
    // makes any stores above the access live.
    removeAccessedObjects(LoadedLoc, DeadStackObjects, DL, AA, TLI, BB.getParent());

    // If all of the allocas were clobbered by the access then we're not going
    // to find anything else to process.
    if (DeadStackObjects.empty())
      break;
  }

  return MadeChange;
}

static bool tryToShorten(Instruction *EarlierWrite, int64_t &EarlierOffset,
                         int64_t &EarlierSize, int64_t LaterOffset,
                         int64_t LaterSize, bool IsOverwriteEnd) {
  // TODO: base this on the target vector size so that if the earlier
  // store was too small to get vector writes anyway then its likely
  // a good idea to shorten it
  // Power of 2 vector writes are probably always a bad idea to optimize
  // as any store/memset/memcpy is likely using vector instructions so
  // shortening it to not vector size is likely to be slower
  auto *EarlierIntrinsic = cast<AnyMemIntrinsic>(EarlierWrite);
  unsigned EarlierWriteAlign = EarlierIntrinsic->getDestAlignment();
  if (!IsOverwriteEnd)
    LaterOffset = int64_t(LaterOffset + LaterSize);

  if (!(isPowerOf2_64(LaterOffset) && EarlierWriteAlign <= LaterOffset) &&
      !((EarlierWriteAlign != 0) && LaterOffset % EarlierWriteAlign == 0))
    return false;

  int64_t NewLength = IsOverwriteEnd
                          ? LaterOffset - EarlierOffset
                          : EarlierSize - (LaterOffset - EarlierOffset);

  if (auto *AMI = dyn_cast<AtomicMemIntrinsic>(EarlierWrite)) {
    // When shortening an atomic memory intrinsic, the newly shortened
    // length must remain an integer multiple of the element size.
    const uint32_t ElementSize = AMI->getElementSizeInBytes();
    if (0 != NewLength % ElementSize)
      return false;
  }

  LLVM_DEBUG(dbgs() << "DSE: Remove Dead Store:\n  OW "
                    << (IsOverwriteEnd ? "END" : "BEGIN") << ": "
                    << *EarlierWrite << "\n  KILLER (offset " << LaterOffset
                    << ", " << EarlierSize << ")\n");

  Value *EarlierWriteLength = EarlierIntrinsic->getLength();
  Value *TrimmedLength =
      ConstantInt::get(EarlierWriteLength->getType(), NewLength);
  EarlierIntrinsic->setLength(TrimmedLength);

  EarlierSize = NewLength;
  if (!IsOverwriteEnd) {
    int64_t OffsetMoved = (LaterOffset - EarlierOffset);
    Value *Indices[1] = {
        ConstantInt::get(EarlierWriteLength->getType(), OffsetMoved)};
    GetElementPtrInst *NewDestGEP = GetElementPtrInst::CreateInBounds(
        EarlierIntrinsic->getRawDest(), Indices, "", EarlierWrite);
    EarlierIntrinsic->setDest(NewDestGEP);
    EarlierOffset = EarlierOffset + OffsetMoved;
  }
  return true;
}

static bool tryToShortenEnd(Instruction *EarlierWrite,
                            OverlapIntervalsTy &IntervalMap,
                            int64_t &EarlierStart, int64_t &EarlierSize) {
  if (IntervalMap.empty() || !isShortenableAtTheEnd(EarlierWrite))
    return false;

  OverlapIntervalsTy::iterator OII = --IntervalMap.end();
  int64_t LaterStart = OII->second;
  int64_t LaterSize = OII->first - LaterStart;

  if (LaterStart > EarlierStart && LaterStart < EarlierStart + EarlierSize &&
      LaterStart + LaterSize >= EarlierStart + EarlierSize) {
    if (tryToShorten(EarlierWrite, EarlierStart, EarlierSize, LaterStart,
                     LaterSize, true)) {
      IntervalMap.erase(OII);
      return true;
    }
  }
  return false;
}

static bool tryToShortenBegin(Instruction *EarlierWrite,
                              OverlapIntervalsTy &IntervalMap,
                              int64_t &EarlierStart, int64_t &EarlierSize) {
  if (IntervalMap.empty() || !isShortenableAtTheBeginning(EarlierWrite))
    return false;

  OverlapIntervalsTy::iterator OII = IntervalMap.begin();
  int64_t LaterStart = OII->second;
  int64_t LaterSize = OII->first - LaterStart;

  if (LaterStart <= EarlierStart && LaterStart + LaterSize > EarlierStart) {
    assert(LaterStart + LaterSize < EarlierStart + EarlierSize &&
           "Should have been handled as OW_Complete");
    if (tryToShorten(EarlierWrite, EarlierStart, EarlierSize, LaterStart,
                     LaterSize, false)) {
      IntervalMap.erase(OII);
      return true;
    }
  }
  return false;
}

static bool removePartiallyOverlappedStores(AliasAnalysis *AA,
                                            const DataLayout &DL,
                                            InstOverlapIntervalsTy &IOL) {
  bool Changed = false;
  for (auto OI : IOL) {
    Instruction *EarlierWrite = OI.first;
    MemoryLocation Loc = getLocForWrite(EarlierWrite);
    assert(isRemovable(EarlierWrite) && "Expect only removable instruction");

    const Value *Ptr = Loc.Ptr->stripPointerCasts();
    int64_t EarlierStart = 0;
    int64_t EarlierSize = int64_t(Loc.Size.getValue());
    GetPointerBaseWithConstantOffset(Ptr, EarlierStart, DL);
    OverlapIntervalsTy &IntervalMap = OI.second;
    Changed |=
        tryToShortenEnd(EarlierWrite, IntervalMap, EarlierStart, EarlierSize);
    if (IntervalMap.empty())
      continue;
    Changed |=
        tryToShortenBegin(EarlierWrite, IntervalMap, EarlierStart, EarlierSize);
  }
  return Changed;
}

static bool eliminateNoopStore(Instruction *Inst, BasicBlock::iterator &BBI,
                               AliasAnalysis *AA, MemoryDependenceResults *MD,
                               const DataLayout &DL,
                               const TargetLibraryInfo *TLI,
                               InstOverlapIntervalsTy &IOL,
                               DenseMap<Instruction*, size_t> *InstrOrdering) {
  // Must be a store instruction.
  StoreInst *SI = dyn_cast<StoreInst>(Inst);
  if (!SI)
    return false;

  // If we're storing the same value back to a pointer that we just loaded from,
  // then the store can be removed.
  if (LoadInst *DepLoad = dyn_cast<LoadInst>(SI->getValueOperand())) {
    if (SI->getPointerOperand() == DepLoad->getPointerOperand() &&
        isRemovable(SI) && memoryIsNotModifiedBetween(DepLoad, SI, AA)) {

      LLVM_DEBUG(
          dbgs() << "DSE: Remove Store Of Load from same pointer:\n  LOAD: "
                 << *DepLoad << "\n  STORE: " << *SI << '\n');

      deleteDeadInstruction(SI, &BBI, *MD, *TLI, IOL, InstrOrdering);
      ++NumRedundantStores;
      return true;
    }
  }

  // Remove null stores into the calloc'ed objects
  Constant *StoredConstant = dyn_cast<Constant>(SI->getValueOperand());
  if (StoredConstant && StoredConstant->isNullValue() && isRemovable(SI)) {
    Instruction *UnderlyingPointer =
        dyn_cast<Instruction>(GetUnderlyingObject(SI->getPointerOperand(), DL));

    if (UnderlyingPointer && isCallocLikeFn(UnderlyingPointer, TLI) &&
        memoryIsNotModifiedBetween(UnderlyingPointer, SI, AA)) {
      LLVM_DEBUG(
          dbgs() << "DSE: Remove null store to the calloc'ed object:\n  DEAD: "
                 << *Inst << "\n  OBJECT: " << *UnderlyingPointer << '\n');

      deleteDeadInstruction(SI, &BBI, *MD, *TLI, IOL, InstrOrdering);
      ++NumRedundantStores;
      return true;
    }
  }
  return false;
}

static bool eliminateDeadStores(BasicBlock &BB, AliasAnalysis *AA,
                                MemoryDependenceResults *MD, DominatorTree *DT,
                                const TargetLibraryInfo *TLI) {
  const DataLayout &DL = BB.getModule()->getDataLayout();
  bool MadeChange = false;

  // FIXME: Maybe change this to use some abstraction like OrderedBasicBlock?
  // The current OrderedBasicBlock can't deal with mutation at the moment.
  size_t LastThrowingInstIndex = 0;
  DenseMap<Instruction*, size_t> InstrOrdering;
  size_t InstrIndex = 1;

  // A map of interval maps representing partially-overwritten value parts.
  InstOverlapIntervalsTy IOL;

  // Do a top-down walk on the BB.
  for (BasicBlock::iterator BBI = BB.begin(), BBE = BB.end(); BBI != BBE; ) {
    // Handle 'free' calls specially.
    if (CallInst *F = isFreeCall(&*BBI, TLI)) {
      MadeChange |= handleFree(F, AA, MD, DT, TLI, IOL, &InstrOrdering);
      // Increment BBI after handleFree has potentially deleted instructions.
      // This ensures we maintain a valid iterator.
      ++BBI;
      continue;
    }

    Instruction *Inst = &*BBI++;

    size_t CurInstNumber = InstrIndex++;
    InstrOrdering.insert(std::make_pair(Inst, CurInstNumber));
    if (Inst->mayThrow()) {
      LastThrowingInstIndex = CurInstNumber;
      continue;
    }

    // Check to see if Inst writes to memory.  If not, continue.
    if (!hasAnalyzableMemoryWrite(Inst, *TLI))
      continue;

    // eliminateNoopStore will update in iterator, if necessary.
    if (eliminateNoopStore(Inst, BBI, AA, MD, DL, TLI, IOL, &InstrOrdering)) {
      MadeChange = true;
      continue;
    }

    // If we find something that writes memory, get its memory dependence.
    MemDepResult InstDep = MD->getDependency(Inst);

    // Ignore any store where we can't find a local dependence.
    // FIXME: cross-block DSE would be fun. :)
    if (!InstDep.isDef() && !InstDep.isClobber())
      continue;

    // Figure out what location is being stored to.
    MemoryLocation Loc = getLocForWrite(Inst);

    // If we didn't get a useful location, fail.
    if (!Loc.Ptr)
      continue;

    // Loop until we find a store we can eliminate or a load that
    // invalidates the analysis. Without an upper bound on the number of
    // instructions examined, this analysis can become very time-consuming.
    // However, the potential gain diminishes as we process more instructions
    // without eliminating any of them. Therefore, we limit the number of
    // instructions we look at.
    auto Limit = MD->getDefaultBlockScanLimit();
    while (InstDep.isDef() || InstDep.isClobber()) {
      // Get the memory clobbered by the instruction we depend on.  MemDep will
      // skip any instructions that 'Loc' clearly doesn't interact with.  If we
      // end up depending on a may- or must-aliased load, then we can't optimize
      // away the store and we bail out.  However, if we depend on something
      // that overwrites the memory location we *can* potentially optimize it.
      //
      // Find out what memory location the dependent instruction stores.
      Instruction *DepWrite = InstDep.getInst();
      if (!hasAnalyzableMemoryWrite(DepWrite, *TLI))
        break;
      MemoryLocation DepLoc = getLocForWrite(DepWrite);
      // If we didn't get a useful location, or if it isn't a size, bail out.
      if (!DepLoc.Ptr)
        break;

      // Make sure we don't look past a call which might throw. This is an
      // issue because MemoryDependenceAnalysis works in the wrong direction:
      // it finds instructions which dominate the current instruction, rather than
      // instructions which are post-dominated by the current instruction.
      //
      // If the underlying object is a non-escaping memory allocation, any store
      // to it is dead along the unwind edge. Otherwise, we need to preserve
      // the store.
      size_t DepIndex = InstrOrdering.lookup(DepWrite);
      assert(DepIndex && "Unexpected instruction");
      if (DepIndex <= LastThrowingInstIndex) {
        const Value* Underlying = GetUnderlyingObject(DepLoc.Ptr, DL);
        bool IsStoreDeadOnUnwind = isa<AllocaInst>(Underlying);
        if (!IsStoreDeadOnUnwind) {
            // We're looking for a call to an allocation function
            // where the allocation doesn't escape before the last
            // throwing instruction; PointerMayBeCaptured
            // reasonably fast approximation.
            IsStoreDeadOnUnwind = isAllocLikeFn(Underlying, TLI) &&
                !PointerMayBeCaptured(Underlying, false, true);
        }
        if (!IsStoreDeadOnUnwind)
          break;
      }

      // If we find a write that is a) removable (i.e., non-volatile), b) is
      // completely obliterated by the store to 'Loc', and c) which we know that
      // 'Inst' doesn't load from, then we can remove it.
      // Also try to merge two stores if a later one only touches memory written
      // to by the earlier one.
      if (isRemovable(DepWrite) &&
          !isPossibleSelfRead(Inst, Loc, DepWrite, *TLI, *AA)) {
        int64_t InstWriteOffset, DepWriteOffset;
        OverwriteResult OR = isOverwrite(Loc, DepLoc, DL, *TLI, DepWriteOffset,
                                         InstWriteOffset, DepWrite, IOL, *AA,
                                         BB.getParent());
        if (OR == OW_Complete) {
          LLVM_DEBUG(dbgs() << "DSE: Remove Dead Store:\n  DEAD: " << *DepWrite
                            << "\n  KILLER: " << *Inst << '\n');

          // Delete the store and now-dead instructions that feed it.
          deleteDeadInstruction(DepWrite, &BBI, *MD, *TLI, IOL, &InstrOrdering);
          ++NumFastStores;
          MadeChange = true;

          // We erased DepWrite; start over.
          InstDep = MD->getDependency(Inst);
          continue;
        } else if ((OR == OW_End && isShortenableAtTheEnd(DepWrite)) ||
                   ((OR == OW_Begin &&
                     isShortenableAtTheBeginning(DepWrite)))) {
          assert(!EnablePartialOverwriteTracking && "Do not expect to perform "
                                                    "when partial-overwrite "
                                                    "tracking is enabled");
          // The overwrite result is known, so these must be known, too.
          int64_t EarlierSize = DepLoc.Size.getValue();
          int64_t LaterSize = Loc.Size.getValue();
          bool IsOverwriteEnd = (OR == OW_End);
          MadeChange |= tryToShorten(DepWrite, DepWriteOffset, EarlierSize,
                                    InstWriteOffset, LaterSize, IsOverwriteEnd);
        } else if (EnablePartialStoreMerging &&
                   OR == OW_PartialEarlierWithFullLater) {
          auto *Earlier = dyn_cast<StoreInst>(DepWrite);
          auto *Later = dyn_cast<StoreInst>(Inst);
          if (Earlier && isa<ConstantInt>(Earlier->getValueOperand()) &&
              Later && isa<ConstantInt>(Later->getValueOperand()) &&
              memoryIsNotModifiedBetween(Earlier, Later, AA)) {
            // If the store we find is:
            //   a) partially overwritten by the store to 'Loc'
            //   b) the later store is fully contained in the earlier one and
            //   c) they both have a constant value
            // Merge the two stores, replacing the earlier store's value with a
            // merge of both values.
            // TODO: Deal with other constant types (vectors, etc), and probably
            // some mem intrinsics (if needed)

            APInt EarlierValue =
                cast<ConstantInt>(Earlier->getValueOperand())->getValue();
            APInt LaterValue =
                cast<ConstantInt>(Later->getValueOperand())->getValue();
            unsigned LaterBits = LaterValue.getBitWidth();
            assert(EarlierValue.getBitWidth() > LaterValue.getBitWidth());
            LaterValue = LaterValue.zext(EarlierValue.getBitWidth());

            // Offset of the smaller store inside the larger store
            unsigned BitOffsetDiff = (InstWriteOffset - DepWriteOffset) * 8;
            unsigned LShiftAmount =
                DL.isBigEndian()
                    ? EarlierValue.getBitWidth() - BitOffsetDiff - LaterBits
                    : BitOffsetDiff;
            APInt Mask =
                APInt::getBitsSet(EarlierValue.getBitWidth(), LShiftAmount,
                                  LShiftAmount + LaterBits);
            // Clear the bits we'll be replacing, then OR with the smaller
            // store, shifted appropriately.
            APInt Merged =
                (EarlierValue & ~Mask) | (LaterValue << LShiftAmount);
            LLVM_DEBUG(dbgs() << "DSE: Merge Stores:\n  Earlier: " << *DepWrite
                              << "\n  Later: " << *Inst
                              << "\n  Merged Value: " << Merged << '\n');

            auto *SI = new StoreInst(
                ConstantInt::get(Earlier->getValueOperand()->getType(), Merged),
                Earlier->getPointerOperand(), false, Earlier->getAlignment(),
                Earlier->getOrdering(), Earlier->getSyncScopeID(), DepWrite);

            unsigned MDToKeep[] = {LLVMContext::MD_dbg, LLVMContext::MD_tbaa,
                                   LLVMContext::MD_alias_scope,
                                   LLVMContext::MD_noalias,
                                   LLVMContext::MD_nontemporal};
            SI->copyMetadata(*DepWrite, MDToKeep);
            ++NumModifiedStores;

            // Remove earlier, wider, store
            size_t Idx = InstrOrdering.lookup(DepWrite);
            InstrOrdering.erase(DepWrite);
            InstrOrdering.insert(std::make_pair(SI, Idx));

            // Delete the old stores and now-dead instructions that feed them.
            deleteDeadInstruction(Inst, &BBI, *MD, *TLI, IOL, &InstrOrdering);
            deleteDeadInstruction(DepWrite, &BBI, *MD, *TLI, IOL,
                                  &InstrOrdering);
            MadeChange = true;

            // We erased DepWrite and Inst (Loc); start over.
            break;
          }
        }
      }

      // If this is a may-aliased store that is clobbering the store value, we
      // can keep searching past it for another must-aliased pointer that stores
      // to the same location.  For example, in:
      //   store -> P
      //   store -> Q
      //   store -> P
      // we can remove the first store to P even though we don't know if P and Q
      // alias.
      if (DepWrite == &BB.front()) break;

      // Can't look past this instruction if it might read 'Loc'.
      if (isRefSet(AA->getModRefInfo(DepWrite, Loc)))
        break;

      InstDep = MD->getPointerDependencyFrom(Loc, /*isLoad=*/ false,
                                             DepWrite->getIterator(), &BB,
                                             /*QueryInst=*/ nullptr, &Limit);
    }
  }

  if (EnablePartialOverwriteTracking)
    MadeChange |= removePartiallyOverlappedStores(AA, DL, IOL);

  // If this block ends in a return, unwind, or unreachable, all allocas are
  // dead at its end, which means stores to them are also dead.
  if (BB.getTerminator()->getNumSuccessors() == 0)
    MadeChange |= handleEndBlock(BB, AA, MD, TLI, IOL, &InstrOrdering);

  return MadeChange;
}

static bool eliminateDeadStores(Function &F, AliasAnalysis *AA,
                                MemoryDependenceResults *MD, DominatorTree *DT,
                                const TargetLibraryInfo *TLI) {
  bool MadeChange = false;
  for (BasicBlock &BB : F)
    // Only check non-dead blocks.  Dead blocks may have strange pointer
    // cycles that will confuse alias analysis.
    if (DT->isReachableFromEntry(&BB))
      MadeChange |= eliminateDeadStores(BB, AA, MD, DT, TLI);

  return MadeChange;
}

//===----------------------------------------------------------------------===//
// DSE Pass
//===----------------------------------------------------------------------===//
PreservedAnalyses DSEPass::run(Function &F, FunctionAnalysisManager &AM) {
  AliasAnalysis *AA = &AM.getResult<AAManager>(F);
  DominatorTree *DT = &AM.getResult<DominatorTreeAnalysis>(F);
  MemoryDependenceResults *MD = &AM.getResult<MemoryDependenceAnalysis>(F);
  const TargetLibraryInfo *TLI = &AM.getResult<TargetLibraryAnalysis>(F);

  if (!eliminateDeadStores(F, AA, MD, DT, TLI))
    return PreservedAnalyses::all();

  PreservedAnalyses PA;
  PA.preserveSet<CFGAnalyses>();
  PA.preserve<GlobalsAA>();
  PA.preserve<MemoryDependenceAnalysis>();
  return PA;
}

namespace {

/// A legacy pass for the legacy pass manager that wraps \c DSEPass.
class DSELegacyPass : public FunctionPass {
public:
  static char ID; // Pass identification, replacement for typeid

  DSELegacyPass() : FunctionPass(ID) {
    initializeDSELegacyPassPass(*PassRegistry::getPassRegistry());
  }

  bool runOnFunction(Function &F) override {
    if (skipFunction(F))
      return false;

    DominatorTree *DT = &getAnalysis<DominatorTreeWrapperPass>().getDomTree();
    AliasAnalysis *AA = &getAnalysis<AAResultsWrapperPass>().getAAResults();
    MemoryDependenceResults *MD =
        &getAnalysis<MemoryDependenceWrapperPass>().getMemDep();
    const TargetLibraryInfo *TLI =
        &getAnalysis<TargetLibraryInfoWrapperPass>().getTLI();

    return eliminateDeadStores(F, AA, MD, DT, TLI);
  }

  void getAnalysisUsage(AnalysisUsage &AU) const override {
    AU.setPreservesCFG();
    AU.addRequired<DominatorTreeWrapperPass>();
    AU.addRequired<AAResultsWrapperPass>();
    AU.addRequired<MemoryDependenceWrapperPass>();
    AU.addRequired<TargetLibraryInfoWrapperPass>();
    AU.addPreserved<DominatorTreeWrapperPass>();
    AU.addPreserved<GlobalsAAWrapperPass>();
    AU.addPreserved<MemoryDependenceWrapperPass>();
  }
};

} // end anonymous namespace

char DSELegacyPass::ID = 0;

INITIALIZE_PASS_BEGIN(DSELegacyPass, "dse", "Dead Store Elimination", false,
                      false)
INITIALIZE_PASS_DEPENDENCY(DominatorTreeWrapperPass)
INITIALIZE_PASS_DEPENDENCY(AAResultsWrapperPass)
INITIALIZE_PASS_DEPENDENCY(GlobalsAAWrapperPass)
INITIALIZE_PASS_DEPENDENCY(MemoryDependenceWrapperPass)
INITIALIZE_PASS_DEPENDENCY(TargetLibraryInfoWrapperPass)
INITIALIZE_PASS_END(DSELegacyPass, "dse", "Dead Store Elimination", false,
                    false)

FunctionPass *llvm::createDeadStoreEliminationPass() {
  return new DSELegacyPass();
}
