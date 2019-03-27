//===- MemoryDependenceAnalysis.cpp - Mem Deps Implementation -------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file implements an analysis that determines, for a given memory
// operation, what preceding memory operations it depends on.  It builds on
// alias analysis information, and tries to provide a lazy, caching interface to
// a common kind of alias information query.
//
//===----------------------------------------------------------------------===//

#include "llvm/Analysis/MemoryDependenceAnalysis.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/Analysis/AliasAnalysis.h"
#include "llvm/Analysis/AssumptionCache.h"
#include "llvm/Analysis/MemoryBuiltins.h"
#include "llvm/Analysis/MemoryLocation.h"
#include "llvm/Analysis/OrderedBasicBlock.h"
#include "llvm/Analysis/PHITransAddr.h"
#include "llvm/Analysis/PhiValues.h"
#include "llvm/Analysis/TargetLibraryInfo.h"
#include "llvm/Analysis/ValueTracking.h"
#include "llvm/IR/Attributes.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/Dominators.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/InstrTypes.h"
#include "llvm/IR/Instruction.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/IntrinsicInst.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Metadata.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/PredIteratorCache.h"
#include "llvm/IR/Type.h"
#include "llvm/IR/Use.h"
#include "llvm/IR/User.h"
#include "llvm/IR/Value.h"
#include "llvm/Pass.h"
#include "llvm/Support/AtomicOrdering.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/MathExtras.h"
#include <algorithm>
#include <cassert>
#include <cstdint>
#include <iterator>
#include <utility>

using namespace llvm;

#define DEBUG_TYPE "memdep"

STATISTIC(NumCacheNonLocal, "Number of fully cached non-local responses");
STATISTIC(NumCacheDirtyNonLocal, "Number of dirty cached non-local responses");
STATISTIC(NumUncacheNonLocal, "Number of uncached non-local responses");

STATISTIC(NumCacheNonLocalPtr,
          "Number of fully cached non-local ptr responses");
STATISTIC(NumCacheDirtyNonLocalPtr,
          "Number of cached, but dirty, non-local ptr responses");
STATISTIC(NumUncacheNonLocalPtr, "Number of uncached non-local ptr responses");
STATISTIC(NumCacheCompleteNonLocalPtr,
          "Number of block queries that were completely cached");

// Limit for the number of instructions to scan in a block.

static cl::opt<unsigned> BlockScanLimit(
    "memdep-block-scan-limit", cl::Hidden, cl::init(100),
    cl::desc("The number of instructions to scan in a block in memory "
             "dependency analysis (default = 100)"));

static cl::opt<unsigned>
    BlockNumberLimit("memdep-block-number-limit", cl::Hidden, cl::init(1000),
                     cl::desc("The number of blocks to scan during memory "
                              "dependency analysis (default = 1000)"));

// Limit on the number of memdep results to process.
static const unsigned int NumResultsLimit = 100;

/// This is a helper function that removes Val from 'Inst's set in ReverseMap.
///
/// If the set becomes empty, remove Inst's entry.
template <typename KeyTy>
static void
RemoveFromReverseMap(DenseMap<Instruction *, SmallPtrSet<KeyTy, 4>> &ReverseMap,
                     Instruction *Inst, KeyTy Val) {
  typename DenseMap<Instruction *, SmallPtrSet<KeyTy, 4>>::iterator InstIt =
      ReverseMap.find(Inst);
  assert(InstIt != ReverseMap.end() && "Reverse map out of sync?");
  bool Found = InstIt->second.erase(Val);
  assert(Found && "Invalid reverse map!");
  (void)Found;
  if (InstIt->second.empty())
    ReverseMap.erase(InstIt);
}

/// If the given instruction references a specific memory location, fill in Loc
/// with the details, otherwise set Loc.Ptr to null.
///
/// Returns a ModRefInfo value describing the general behavior of the
/// instruction.
static ModRefInfo GetLocation(const Instruction *Inst, MemoryLocation &Loc,
                              const TargetLibraryInfo &TLI) {
  if (const LoadInst *LI = dyn_cast<LoadInst>(Inst)) {
    if (LI->isUnordered()) {
      Loc = MemoryLocation::get(LI);
      return ModRefInfo::Ref;
    }
    if (LI->getOrdering() == AtomicOrdering::Monotonic) {
      Loc = MemoryLocation::get(LI);
      return ModRefInfo::ModRef;
    }
    Loc = MemoryLocation();
    return ModRefInfo::ModRef;
  }

  if (const StoreInst *SI = dyn_cast<StoreInst>(Inst)) {
    if (SI->isUnordered()) {
      Loc = MemoryLocation::get(SI);
      return ModRefInfo::Mod;
    }
    if (SI->getOrdering() == AtomicOrdering::Monotonic) {
      Loc = MemoryLocation::get(SI);
      return ModRefInfo::ModRef;
    }
    Loc = MemoryLocation();
    return ModRefInfo::ModRef;
  }

  if (const VAArgInst *V = dyn_cast<VAArgInst>(Inst)) {
    Loc = MemoryLocation::get(V);
    return ModRefInfo::ModRef;
  }

  if (const CallInst *CI = isFreeCall(Inst, &TLI)) {
    // calls to free() deallocate the entire structure
    Loc = MemoryLocation(CI->getArgOperand(0));
    return ModRefInfo::Mod;
  }

  if (const IntrinsicInst *II = dyn_cast<IntrinsicInst>(Inst)) {
    switch (II->getIntrinsicID()) {
    case Intrinsic::lifetime_start:
    case Intrinsic::lifetime_end:
    case Intrinsic::invariant_start:
      Loc = MemoryLocation::getForArgument(II, 1, TLI);
      // These intrinsics don't really modify the memory, but returning Mod
      // will allow them to be handled conservatively.
      return ModRefInfo::Mod;
    case Intrinsic::invariant_end:
      Loc = MemoryLocation::getForArgument(II, 2, TLI);
      // These intrinsics don't really modify the memory, but returning Mod
      // will allow them to be handled conservatively.
      return ModRefInfo::Mod;
    default:
      break;
    }
  }

  // Otherwise, just do the coarse-grained thing that always works.
  if (Inst->mayWriteToMemory())
    return ModRefInfo::ModRef;
  if (Inst->mayReadFromMemory())
    return ModRefInfo::Ref;
  return ModRefInfo::NoModRef;
}

/// Private helper for finding the local dependencies of a call site.
MemDepResult MemoryDependenceResults::getCallDependencyFrom(
    CallBase *Call, bool isReadOnlyCall, BasicBlock::iterator ScanIt,
    BasicBlock *BB) {
  unsigned Limit = BlockScanLimit;

  // Walk backwards through the block, looking for dependencies.
  while (ScanIt != BB->begin()) {
    Instruction *Inst = &*--ScanIt;
    // Debug intrinsics don't cause dependences and should not affect Limit
    if (isa<DbgInfoIntrinsic>(Inst))
      continue;

    // Limit the amount of scanning we do so we don't end up with quadratic
    // running time on extreme testcases.
    --Limit;
    if (!Limit)
      return MemDepResult::getUnknown();

    // If this inst is a memory op, get the pointer it accessed
    MemoryLocation Loc;
    ModRefInfo MR = GetLocation(Inst, Loc, TLI);
    if (Loc.Ptr) {
      // A simple instruction.
      if (isModOrRefSet(AA.getModRefInfo(Call, Loc)))
        return MemDepResult::getClobber(Inst);
      continue;
    }

    if (auto *CallB = dyn_cast<CallBase>(Inst)) {
      // If these two calls do not interfere, look past it.
      if (isNoModRef(AA.getModRefInfo(Call, CallB))) {
        // If the two calls are the same, return Inst as a Def, so that
        // Call can be found redundant and eliminated.
        if (isReadOnlyCall && !isModSet(MR) &&
            Call->isIdenticalToWhenDefined(CallB))
          return MemDepResult::getDef(Inst);

        // Otherwise if the two calls don't interact (e.g. CallB is readnone)
        // keep scanning.
        continue;
      } else
        return MemDepResult::getClobber(Inst);
    }

    // If we could not obtain a pointer for the instruction and the instruction
    // touches memory then assume that this is a dependency.
    if (isModOrRefSet(MR))
      return MemDepResult::getClobber(Inst);
  }

  // No dependence found.  If this is the entry block of the function, it is
  // unknown, otherwise it is non-local.
  if (BB != &BB->getParent()->getEntryBlock())
    return MemDepResult::getNonLocal();
  return MemDepResult::getNonFuncLocal();
}

unsigned MemoryDependenceResults::getLoadLoadClobberFullWidthSize(
    const Value *MemLocBase, int64_t MemLocOffs, unsigned MemLocSize,
    const LoadInst *LI) {
  // We can only extend simple integer loads.
  if (!isa<IntegerType>(LI->getType()) || !LI->isSimple())
    return 0;

  // Load widening is hostile to ThreadSanitizer: it may cause false positives
  // or make the reports more cryptic (access sizes are wrong).
  if (LI->getParent()->getParent()->hasFnAttribute(Attribute::SanitizeThread))
    return 0;

  const DataLayout &DL = LI->getModule()->getDataLayout();

  // Get the base of this load.
  int64_t LIOffs = 0;
  const Value *LIBase =
      GetPointerBaseWithConstantOffset(LI->getPointerOperand(), LIOffs, DL);

  // If the two pointers are not based on the same pointer, we can't tell that
  // they are related.
  if (LIBase != MemLocBase)
    return 0;

  // Okay, the two values are based on the same pointer, but returned as
  // no-alias.  This happens when we have things like two byte loads at "P+1"
  // and "P+3".  Check to see if increasing the size of the "LI" load up to its
  // alignment (or the largest native integer type) will allow us to load all
  // the bits required by MemLoc.

  // If MemLoc is before LI, then no widening of LI will help us out.
  if (MemLocOffs < LIOffs)
    return 0;

  // Get the alignment of the load in bytes.  We assume that it is safe to load
  // any legal integer up to this size without a problem.  For example, if we're
  // looking at an i8 load on x86-32 that is known 1024 byte aligned, we can
  // widen it up to an i32 load.  If it is known 2-byte aligned, we can widen it
  // to i16.
  unsigned LoadAlign = LI->getAlignment();

  int64_t MemLocEnd = MemLocOffs + MemLocSize;

  // If no amount of rounding up will let MemLoc fit into LI, then bail out.
  if (LIOffs + LoadAlign < MemLocEnd)
    return 0;

  // This is the size of the load to try.  Start with the next larger power of
  // two.
  unsigned NewLoadByteSize = LI->getType()->getPrimitiveSizeInBits() / 8U;
  NewLoadByteSize = NextPowerOf2(NewLoadByteSize);

  while (true) {
    // If this load size is bigger than our known alignment or would not fit
    // into a native integer register, then we fail.
    if (NewLoadByteSize > LoadAlign ||
        !DL.fitsInLegalInteger(NewLoadByteSize * 8))
      return 0;

    if (LIOffs + NewLoadByteSize > MemLocEnd &&
        (LI->getParent()->getParent()->hasFnAttribute(
             Attribute::SanitizeAddress) ||
         LI->getParent()->getParent()->hasFnAttribute(
             Attribute::SanitizeHWAddress)))
      // We will be reading past the location accessed by the original program.
      // While this is safe in a regular build, Address Safety analysis tools
      // may start reporting false warnings. So, don't do widening.
      return 0;

    // If a load of this width would include all of MemLoc, then we succeed.
    if (LIOffs + NewLoadByteSize >= MemLocEnd)
      return NewLoadByteSize;

    NewLoadByteSize <<= 1;
  }
}

static bool isVolatile(Instruction *Inst) {
  if (auto *LI = dyn_cast<LoadInst>(Inst))
    return LI->isVolatile();
  if (auto *SI = dyn_cast<StoreInst>(Inst))
    return SI->isVolatile();
  if (auto *AI = dyn_cast<AtomicCmpXchgInst>(Inst))
    return AI->isVolatile();
  return false;
}

MemDepResult MemoryDependenceResults::getPointerDependencyFrom(
    const MemoryLocation &MemLoc, bool isLoad, BasicBlock::iterator ScanIt,
    BasicBlock *BB, Instruction *QueryInst, unsigned *Limit) {
  MemDepResult InvariantGroupDependency = MemDepResult::getUnknown();
  if (QueryInst != nullptr) {
    if (auto *LI = dyn_cast<LoadInst>(QueryInst)) {
      InvariantGroupDependency = getInvariantGroupPointerDependency(LI, BB);

      if (InvariantGroupDependency.isDef())
        return InvariantGroupDependency;
    }
  }
  MemDepResult SimpleDep = getSimplePointerDependencyFrom(
      MemLoc, isLoad, ScanIt, BB, QueryInst, Limit);
  if (SimpleDep.isDef())
    return SimpleDep;
  // Non-local invariant group dependency indicates there is non local Def
  // (it only returns nonLocal if it finds nonLocal def), which is better than
  // local clobber and everything else.
  if (InvariantGroupDependency.isNonLocal())
    return InvariantGroupDependency;

  assert(InvariantGroupDependency.isUnknown() &&
         "InvariantGroupDependency should be only unknown at this point");
  return SimpleDep;
}

MemDepResult
MemoryDependenceResults::getInvariantGroupPointerDependency(LoadInst *LI,
                                                            BasicBlock *BB) {

  if (!LI->getMetadata(LLVMContext::MD_invariant_group))
    return MemDepResult::getUnknown();

  // Take the ptr operand after all casts and geps 0. This way we can search
  // cast graph down only.
  Value *LoadOperand = LI->getPointerOperand()->stripPointerCasts();

  // It's is not safe to walk the use list of global value, because function
  // passes aren't allowed to look outside their functions.
  // FIXME: this could be fixed by filtering instructions from outside
  // of current function.
  if (isa<GlobalValue>(LoadOperand))
    return MemDepResult::getUnknown();

  // Queue to process all pointers that are equivalent to load operand.
  SmallVector<const Value *, 8> LoadOperandsQueue;
  LoadOperandsQueue.push_back(LoadOperand);

  Instruction *ClosestDependency = nullptr;
  // Order of instructions in uses list is unpredictible. In order to always
  // get the same result, we will look for the closest dominance.
  auto GetClosestDependency = [this](Instruction *Best, Instruction *Other) {
    assert(Other && "Must call it with not null instruction");
    if (Best == nullptr || DT.dominates(Best, Other))
      return Other;
    return Best;
  };

  // FIXME: This loop is O(N^2) because dominates can be O(n) and in worst case
  // we will see all the instructions. This should be fixed in MSSA.
  while (!LoadOperandsQueue.empty()) {
    const Value *Ptr = LoadOperandsQueue.pop_back_val();
    assert(Ptr && !isa<GlobalValue>(Ptr) &&
           "Null or GlobalValue should not be inserted");

    for (const Use &Us : Ptr->uses()) {
      auto *U = dyn_cast<Instruction>(Us.getUser());
      if (!U || U == LI || !DT.dominates(U, LI))
        continue;

      // Bitcast or gep with zeros are using Ptr. Add to queue to check it's
      // users.      U = bitcast Ptr
      if (isa<BitCastInst>(U)) {
        LoadOperandsQueue.push_back(U);
        continue;
      }
      // Gep with zeros is equivalent to bitcast.
      // FIXME: we are not sure if some bitcast should be canonicalized to gep 0
      // or gep 0 to bitcast because of SROA, so there are 2 forms. When
      // typeless pointers will be ready then both cases will be gone
      // (and this BFS also won't be needed).
      if (auto *GEP = dyn_cast<GetElementPtrInst>(U))
        if (GEP->hasAllZeroIndices()) {
          LoadOperandsQueue.push_back(U);
          continue;
        }

      // If we hit load/store with the same invariant.group metadata (and the
      // same pointer operand) we can assume that value pointed by pointer
      // operand didn't change.
      if ((isa<LoadInst>(U) || isa<StoreInst>(U)) &&
          U->getMetadata(LLVMContext::MD_invariant_group) != nullptr)
        ClosestDependency = GetClosestDependency(ClosestDependency, U);
    }
  }

  if (!ClosestDependency)
    return MemDepResult::getUnknown();
  if (ClosestDependency->getParent() == BB)
    return MemDepResult::getDef(ClosestDependency);
  // Def(U) can't be returned here because it is non-local. If local
  // dependency won't be found then return nonLocal counting that the
  // user will call getNonLocalPointerDependency, which will return cached
  // result.
  NonLocalDefsCache.try_emplace(
      LI, NonLocalDepResult(ClosestDependency->getParent(),
                            MemDepResult::getDef(ClosestDependency), nullptr));
  ReverseNonLocalDefsCache[ClosestDependency].insert(LI);
  return MemDepResult::getNonLocal();
}

MemDepResult MemoryDependenceResults::getSimplePointerDependencyFrom(
    const MemoryLocation &MemLoc, bool isLoad, BasicBlock::iterator ScanIt,
    BasicBlock *BB, Instruction *QueryInst, unsigned *Limit) {
  bool isInvariantLoad = false;

  if (!Limit) {
    unsigned DefaultLimit = BlockScanLimit;
    return getSimplePointerDependencyFrom(MemLoc, isLoad, ScanIt, BB, QueryInst,
                                          &DefaultLimit);
  }

  // We must be careful with atomic accesses, as they may allow another thread
  //   to touch this location, clobbering it. We are conservative: if the
  //   QueryInst is not a simple (non-atomic) memory access, we automatically
  //   return getClobber.
  // If it is simple, we know based on the results of
  // "Compiler testing via a theory of sound optimisations in the C11/C++11
  //   memory model" in PLDI 2013, that a non-atomic location can only be
  //   clobbered between a pair of a release and an acquire action, with no
  //   access to the location in between.
  // Here is an example for giving the general intuition behind this rule.
  // In the following code:
  //   store x 0;
  //   release action; [1]
  //   acquire action; [4]
  //   %val = load x;
  // It is unsafe to replace %val by 0 because another thread may be running:
  //   acquire action; [2]
  //   store x 42;
  //   release action; [3]
  // with synchronization from 1 to 2 and from 3 to 4, resulting in %val
  // being 42. A key property of this program however is that if either
  // 1 or 4 were missing, there would be a race between the store of 42
  // either the store of 0 or the load (making the whole program racy).
  // The paper mentioned above shows that the same property is respected
  // by every program that can detect any optimization of that kind: either
  // it is racy (undefined) or there is a release followed by an acquire
  // between the pair of accesses under consideration.

  // If the load is invariant, we "know" that it doesn't alias *any* write. We
  // do want to respect mustalias results since defs are useful for value
  // forwarding, but any mayalias write can be assumed to be noalias.
  // Arguably, this logic should be pushed inside AliasAnalysis itself.
  if (isLoad && QueryInst) {
    LoadInst *LI = dyn_cast<LoadInst>(QueryInst);
    if (LI && LI->getMetadata(LLVMContext::MD_invariant_load) != nullptr)
      isInvariantLoad = true;
  }

  const DataLayout &DL = BB->getModule()->getDataLayout();

  // Create a numbered basic block to lazily compute and cache instruction
  // positions inside a BB. This is used to provide fast queries for relative
  // position between two instructions in a BB and can be used by
  // AliasAnalysis::callCapturesBefore.
  OrderedBasicBlock OBB(BB);

  // Return "true" if and only if the instruction I is either a non-simple
  // load or a non-simple store.
  auto isNonSimpleLoadOrStore = [](Instruction *I) -> bool {
    if (auto *LI = dyn_cast<LoadInst>(I))
      return !LI->isSimple();
    if (auto *SI = dyn_cast<StoreInst>(I))
      return !SI->isSimple();
    return false;
  };

  // Return "true" if I is not a load and not a store, but it does access
  // memory.
  auto isOtherMemAccess = [](Instruction *I) -> bool {
    return !isa<LoadInst>(I) && !isa<StoreInst>(I) && I->mayReadOrWriteMemory();
  };

  // Walk backwards through the basic block, looking for dependencies.
  while (ScanIt != BB->begin()) {
    Instruction *Inst = &*--ScanIt;

    if (IntrinsicInst *II = dyn_cast<IntrinsicInst>(Inst))
      // Debug intrinsics don't (and can't) cause dependencies.
      if (isa<DbgInfoIntrinsic>(II))
        continue;

    // Limit the amount of scanning we do so we don't end up with quadratic
    // running time on extreme testcases.
    --*Limit;
    if (!*Limit)
      return MemDepResult::getUnknown();

    if (IntrinsicInst *II = dyn_cast<IntrinsicInst>(Inst)) {
      // If we reach a lifetime begin or end marker, then the query ends here
      // because the value is undefined.
      if (II->getIntrinsicID() == Intrinsic::lifetime_start) {
        // FIXME: This only considers queries directly on the invariant-tagged
        // pointer, not on query pointers that are indexed off of them.  It'd
        // be nice to handle that at some point (the right approach is to use
        // GetPointerBaseWithConstantOffset).
        if (AA.isMustAlias(MemoryLocation(II->getArgOperand(1)), MemLoc))
          return MemDepResult::getDef(II);
        continue;
      }
    }

    // Values depend on loads if the pointers are must aliased.  This means
    // that a load depends on another must aliased load from the same value.
    // One exception is atomic loads: a value can depend on an atomic load that
    // it does not alias with when this atomic load indicates that another
    // thread may be accessing the location.
    if (LoadInst *LI = dyn_cast<LoadInst>(Inst)) {
      // While volatile access cannot be eliminated, they do not have to clobber
      // non-aliasing locations, as normal accesses, for example, can be safely
      // reordered with volatile accesses.
      if (LI->isVolatile()) {
        if (!QueryInst)
          // Original QueryInst *may* be volatile
          return MemDepResult::getClobber(LI);
        if (isVolatile(QueryInst))
          // Ordering required if QueryInst is itself volatile
          return MemDepResult::getClobber(LI);
        // Otherwise, volatile doesn't imply any special ordering
      }

      // Atomic loads have complications involved.
      // A Monotonic (or higher) load is OK if the query inst is itself not
      // atomic.
      // FIXME: This is overly conservative.
      if (LI->isAtomic() && isStrongerThanUnordered(LI->getOrdering())) {
        if (!QueryInst || isNonSimpleLoadOrStore(QueryInst) ||
            isOtherMemAccess(QueryInst))
          return MemDepResult::getClobber(LI);
        if (LI->getOrdering() != AtomicOrdering::Monotonic)
          return MemDepResult::getClobber(LI);
      }

      MemoryLocation LoadLoc = MemoryLocation::get(LI);

      // If we found a pointer, check if it could be the same as our pointer.
      AliasResult R = AA.alias(LoadLoc, MemLoc);

      if (isLoad) {
        if (R == NoAlias)
          continue;

        // Must aliased loads are defs of each other.
        if (R == MustAlias)
          return MemDepResult::getDef(Inst);

#if 0 // FIXME: Temporarily disabled. GVN is cleverly rewriting loads
      // in terms of clobbering loads, but since it does this by looking
      // at the clobbering load directly, it doesn't know about any
      // phi translation that may have happened along the way.

        // If we have a partial alias, then return this as a clobber for the
        // client to handle.
        if (R == PartialAlias)
          return MemDepResult::getClobber(Inst);
#endif

        // Random may-alias loads don't depend on each other without a
        // dependence.
        continue;
      }

      // Stores don't depend on other no-aliased accesses.
      if (R == NoAlias)
        continue;

      // Stores don't alias loads from read-only memory.
      if (AA.pointsToConstantMemory(LoadLoc))
        continue;

      // Stores depend on may/must aliased loads.
      return MemDepResult::getDef(Inst);
    }

    if (StoreInst *SI = dyn_cast<StoreInst>(Inst)) {
      // Atomic stores have complications involved.
      // A Monotonic store is OK if the query inst is itself not atomic.
      // FIXME: This is overly conservative.
      if (!SI->isUnordered() && SI->isAtomic()) {
        if (!QueryInst || isNonSimpleLoadOrStore(QueryInst) ||
            isOtherMemAccess(QueryInst))
          return MemDepResult::getClobber(SI);
        if (SI->getOrdering() != AtomicOrdering::Monotonic)
          return MemDepResult::getClobber(SI);
      }

      // FIXME: this is overly conservative.
      // While volatile access cannot be eliminated, they do not have to clobber
      // non-aliasing locations, as normal accesses can for example be reordered
      // with volatile accesses.
      if (SI->isVolatile())
        if (!QueryInst || isNonSimpleLoadOrStore(QueryInst) ||
            isOtherMemAccess(QueryInst))
          return MemDepResult::getClobber(SI);

      // If alias analysis can tell that this store is guaranteed to not modify
      // the query pointer, ignore it.  Use getModRefInfo to handle cases where
      // the query pointer points to constant memory etc.
      if (!isModOrRefSet(AA.getModRefInfo(SI, MemLoc)))
        continue;

      // Ok, this store might clobber the query pointer.  Check to see if it is
      // a must alias: in this case, we want to return this as a def.
      // FIXME: Use ModRefInfo::Must bit from getModRefInfo call above.
      MemoryLocation StoreLoc = MemoryLocation::get(SI);

      // If we found a pointer, check if it could be the same as our pointer.
      AliasResult R = AA.alias(StoreLoc, MemLoc);

      if (R == NoAlias)
        continue;
      if (R == MustAlias)
        return MemDepResult::getDef(Inst);
      if (isInvariantLoad)
        continue;
      return MemDepResult::getClobber(Inst);
    }

    // If this is an allocation, and if we know that the accessed pointer is to
    // the allocation, return Def.  This means that there is no dependence and
    // the access can be optimized based on that.  For example, a load could
    // turn into undef.  Note that we can bypass the allocation itself when
    // looking for a clobber in many cases; that's an alias property and is
    // handled by BasicAA.
    if (isa<AllocaInst>(Inst) || isNoAliasFn(Inst, &TLI)) {
      const Value *AccessPtr = GetUnderlyingObject(MemLoc.Ptr, DL);
      if (AccessPtr == Inst || AA.isMustAlias(Inst, AccessPtr))
        return MemDepResult::getDef(Inst);
    }

    if (isInvariantLoad)
      continue;

    // A release fence requires that all stores complete before it, but does
    // not prevent the reordering of following loads or stores 'before' the
    // fence.  As a result, we look past it when finding a dependency for
    // loads.  DSE uses this to find preceeding stores to delete and thus we
    // can't bypass the fence if the query instruction is a store.
    if (FenceInst *FI = dyn_cast<FenceInst>(Inst))
      if (isLoad && FI->getOrdering() == AtomicOrdering::Release)
        continue;

    // See if this instruction (e.g. a call or vaarg) mod/ref's the pointer.
    ModRefInfo MR = AA.getModRefInfo(Inst, MemLoc);
    // If necessary, perform additional analysis.
    if (isModAndRefSet(MR))
      MR = AA.callCapturesBefore(Inst, MemLoc, &DT, &OBB);
    switch (clearMust(MR)) {
    case ModRefInfo::NoModRef:
      // If the call has no effect on the queried pointer, just ignore it.
      continue;
    case ModRefInfo::Mod:
      return MemDepResult::getClobber(Inst);
    case ModRefInfo::Ref:
      // If the call is known to never store to the pointer, and if this is a
      // load query, we can safely ignore it (scan past it).
      if (isLoad)
        continue;
      LLVM_FALLTHROUGH;
    default:
      // Otherwise, there is a potential dependence.  Return a clobber.
      return MemDepResult::getClobber(Inst);
    }
  }

  // No dependence found.  If this is the entry block of the function, it is
  // unknown, otherwise it is non-local.
  if (BB != &BB->getParent()->getEntryBlock())
    return MemDepResult::getNonLocal();
  return MemDepResult::getNonFuncLocal();
}

MemDepResult MemoryDependenceResults::getDependency(Instruction *QueryInst) {
  Instruction *ScanPos = QueryInst;

  // Check for a cached result
  MemDepResult &LocalCache = LocalDeps[QueryInst];

  // If the cached entry is non-dirty, just return it.  Note that this depends
  // on MemDepResult's default constructing to 'dirty'.
  if (!LocalCache.isDirty())
    return LocalCache;

  // Otherwise, if we have a dirty entry, we know we can start the scan at that
  // instruction, which may save us some work.
  if (Instruction *Inst = LocalCache.getInst()) {
    ScanPos = Inst;

    RemoveFromReverseMap(ReverseLocalDeps, Inst, QueryInst);
  }

  BasicBlock *QueryParent = QueryInst->getParent();

  // Do the scan.
  if (BasicBlock::iterator(QueryInst) == QueryParent->begin()) {
    // No dependence found. If this is the entry block of the function, it is
    // unknown, otherwise it is non-local.
    if (QueryParent != &QueryParent->getParent()->getEntryBlock())
      LocalCache = MemDepResult::getNonLocal();
    else
      LocalCache = MemDepResult::getNonFuncLocal();
  } else {
    MemoryLocation MemLoc;
    ModRefInfo MR = GetLocation(QueryInst, MemLoc, TLI);
    if (MemLoc.Ptr) {
      // If we can do a pointer scan, make it happen.
      bool isLoad = !isModSet(MR);
      if (auto *II = dyn_cast<IntrinsicInst>(QueryInst))
        isLoad |= II->getIntrinsicID() == Intrinsic::lifetime_start;

      LocalCache = getPointerDependencyFrom(
          MemLoc, isLoad, ScanPos->getIterator(), QueryParent, QueryInst);
    } else if (auto *QueryCall = dyn_cast<CallBase>(QueryInst)) {
      bool isReadOnly = AA.onlyReadsMemory(QueryCall);
      LocalCache = getCallDependencyFrom(QueryCall, isReadOnly,
                                         ScanPos->getIterator(), QueryParent);
    } else
      // Non-memory instruction.
      LocalCache = MemDepResult::getUnknown();
  }

  // Remember the result!
  if (Instruction *I = LocalCache.getInst())
    ReverseLocalDeps[I].insert(QueryInst);

  return LocalCache;
}

#ifndef NDEBUG
/// This method is used when -debug is specified to verify that cache arrays
/// are properly kept sorted.
static void AssertSorted(MemoryDependenceResults::NonLocalDepInfo &Cache,
                         int Count = -1) {
  if (Count == -1)
    Count = Cache.size();
  assert(std::is_sorted(Cache.begin(), Cache.begin() + Count) &&
         "Cache isn't sorted!");
}
#endif

const MemoryDependenceResults::NonLocalDepInfo &
MemoryDependenceResults::getNonLocalCallDependency(CallBase *QueryCall) {
  assert(getDependency(QueryCall).isNonLocal() &&
         "getNonLocalCallDependency should only be used on calls with "
         "non-local deps!");
  PerInstNLInfo &CacheP = NonLocalDeps[QueryCall];
  NonLocalDepInfo &Cache = CacheP.first;

  // This is the set of blocks that need to be recomputed.  In the cached case,
  // this can happen due to instructions being deleted etc. In the uncached
  // case, this starts out as the set of predecessors we care about.
  SmallVector<BasicBlock *, 32> DirtyBlocks;

  if (!Cache.empty()) {
    // Okay, we have a cache entry.  If we know it is not dirty, just return it
    // with no computation.
    if (!CacheP.second) {
      ++NumCacheNonLocal;
      return Cache;
    }

    // If we already have a partially computed set of results, scan them to
    // determine what is dirty, seeding our initial DirtyBlocks worklist.
    for (auto &Entry : Cache)
      if (Entry.getResult().isDirty())
        DirtyBlocks.push_back(Entry.getBB());

    // Sort the cache so that we can do fast binary search lookups below.
    llvm::sort(Cache);

    ++NumCacheDirtyNonLocal;
    // cerr << "CACHED CASE: " << DirtyBlocks.size() << " dirty: "
    //     << Cache.size() << " cached: " << *QueryInst;
  } else {
    // Seed DirtyBlocks with each of the preds of QueryInst's block.
    BasicBlock *QueryBB = QueryCall->getParent();
    for (BasicBlock *Pred : PredCache.get(QueryBB))
      DirtyBlocks.push_back(Pred);
    ++NumUncacheNonLocal;
  }

  // isReadonlyCall - If this is a read-only call, we can be more aggressive.
  bool isReadonlyCall = AA.onlyReadsMemory(QueryCall);

  SmallPtrSet<BasicBlock *, 32> Visited;

  unsigned NumSortedEntries = Cache.size();
  LLVM_DEBUG(AssertSorted(Cache));

  // Iterate while we still have blocks to update.
  while (!DirtyBlocks.empty()) {
    BasicBlock *DirtyBB = DirtyBlocks.back();
    DirtyBlocks.pop_back();

    // Already processed this block?
    if (!Visited.insert(DirtyBB).second)
      continue;

    // Do a binary search to see if we already have an entry for this block in
    // the cache set.  If so, find it.
    LLVM_DEBUG(AssertSorted(Cache, NumSortedEntries));
    NonLocalDepInfo::iterator Entry =
        std::upper_bound(Cache.begin(), Cache.begin() + NumSortedEntries,
                         NonLocalDepEntry(DirtyBB));
    if (Entry != Cache.begin() && std::prev(Entry)->getBB() == DirtyBB)
      --Entry;

    NonLocalDepEntry *ExistingResult = nullptr;
    if (Entry != Cache.begin() + NumSortedEntries &&
        Entry->getBB() == DirtyBB) {
      // If we already have an entry, and if it isn't already dirty, the block
      // is done.
      if (!Entry->getResult().isDirty())
        continue;

      // Otherwise, remember this slot so we can update the value.
      ExistingResult = &*Entry;
    }

    // If the dirty entry has a pointer, start scanning from it so we don't have
    // to rescan the entire block.
    BasicBlock::iterator ScanPos = DirtyBB->end();
    if (ExistingResult) {
      if (Instruction *Inst = ExistingResult->getResult().getInst()) {
        ScanPos = Inst->getIterator();
        // We're removing QueryInst's use of Inst.
        RemoveFromReverseMap<Instruction *>(ReverseNonLocalDeps, Inst,
                                            QueryCall);
      }
    }

    // Find out if this block has a local dependency for QueryInst.
    MemDepResult Dep;

    if (ScanPos != DirtyBB->begin()) {
      Dep = getCallDependencyFrom(QueryCall, isReadonlyCall, ScanPos, DirtyBB);
    } else if (DirtyBB != &DirtyBB->getParent()->getEntryBlock()) {
      // No dependence found.  If this is the entry block of the function, it is
      // a clobber, otherwise it is unknown.
      Dep = MemDepResult::getNonLocal();
    } else {
      Dep = MemDepResult::getNonFuncLocal();
    }

    // If we had a dirty entry for the block, update it.  Otherwise, just add
    // a new entry.
    if (ExistingResult)
      ExistingResult->setResult(Dep);
    else
      Cache.push_back(NonLocalDepEntry(DirtyBB, Dep));

    // If the block has a dependency (i.e. it isn't completely transparent to
    // the value), remember the association!
    if (!Dep.isNonLocal()) {
      // Keep the ReverseNonLocalDeps map up to date so we can efficiently
      // update this when we remove instructions.
      if (Instruction *Inst = Dep.getInst())
        ReverseNonLocalDeps[Inst].insert(QueryCall);
    } else {

      // If the block *is* completely transparent to the load, we need to check
      // the predecessors of this block.  Add them to our worklist.
      for (BasicBlock *Pred : PredCache.get(DirtyBB))
        DirtyBlocks.push_back(Pred);
    }
  }

  return Cache;
}

void MemoryDependenceResults::getNonLocalPointerDependency(
    Instruction *QueryInst, SmallVectorImpl<NonLocalDepResult> &Result) {
  const MemoryLocation Loc = MemoryLocation::get(QueryInst);
  bool isLoad = isa<LoadInst>(QueryInst);
  BasicBlock *FromBB = QueryInst->getParent();
  assert(FromBB);

  assert(Loc.Ptr->getType()->isPointerTy() &&
         "Can't get pointer deps of a non-pointer!");
  Result.clear();
  {
    // Check if there is cached Def with invariant.group.
    auto NonLocalDefIt = NonLocalDefsCache.find(QueryInst);
    if (NonLocalDefIt != NonLocalDefsCache.end()) {
      Result.push_back(NonLocalDefIt->second);
      ReverseNonLocalDefsCache[NonLocalDefIt->second.getResult().getInst()]
          .erase(QueryInst);
      NonLocalDefsCache.erase(NonLocalDefIt);
      return;
    }
  }
  // This routine does not expect to deal with volatile instructions.
  // Doing so would require piping through the QueryInst all the way through.
  // TODO: volatiles can't be elided, but they can be reordered with other
  // non-volatile accesses.

  // We currently give up on any instruction which is ordered, but we do handle
  // atomic instructions which are unordered.
  // TODO: Handle ordered instructions
  auto isOrdered = [](Instruction *Inst) {
    if (LoadInst *LI = dyn_cast<LoadInst>(Inst)) {
      return !LI->isUnordered();
    } else if (StoreInst *SI = dyn_cast<StoreInst>(Inst)) {
      return !SI->isUnordered();
    }
    return false;
  };
  if (isVolatile(QueryInst) || isOrdered(QueryInst)) {
    Result.push_back(NonLocalDepResult(FromBB, MemDepResult::getUnknown(),
                                       const_cast<Value *>(Loc.Ptr)));
    return;
  }
  const DataLayout &DL = FromBB->getModule()->getDataLayout();
  PHITransAddr Address(const_cast<Value *>(Loc.Ptr), DL, &AC);

  // This is the set of blocks we've inspected, and the pointer we consider in
  // each block.  Because of critical edges, we currently bail out if querying
  // a block with multiple different pointers.  This can happen during PHI
  // translation.
  DenseMap<BasicBlock *, Value *> Visited;
  if (getNonLocalPointerDepFromBB(QueryInst, Address, Loc, isLoad, FromBB,
                                   Result, Visited, true))
    return;
  Result.clear();
  Result.push_back(NonLocalDepResult(FromBB, MemDepResult::getUnknown(),
                                     const_cast<Value *>(Loc.Ptr)));
}

/// Compute the memdep value for BB with Pointer/PointeeSize using either
/// cached information in Cache or by doing a lookup (which may use dirty cache
/// info if available).
///
/// If we do a lookup, add the result to the cache.
MemDepResult MemoryDependenceResults::GetNonLocalInfoForBlock(
    Instruction *QueryInst, const MemoryLocation &Loc, bool isLoad,
    BasicBlock *BB, NonLocalDepInfo *Cache, unsigned NumSortedEntries) {

  // Do a binary search to see if we already have an entry for this block in
  // the cache set.  If so, find it.
  NonLocalDepInfo::iterator Entry = std::upper_bound(
      Cache->begin(), Cache->begin() + NumSortedEntries, NonLocalDepEntry(BB));
  if (Entry != Cache->begin() && (Entry - 1)->getBB() == BB)
    --Entry;

  NonLocalDepEntry *ExistingResult = nullptr;
  if (Entry != Cache->begin() + NumSortedEntries && Entry->getBB() == BB)
    ExistingResult = &*Entry;

  // If we have a cached entry, and it is non-dirty, use it as the value for
  // this dependency.
  if (ExistingResult && !ExistingResult->getResult().isDirty()) {
    ++NumCacheNonLocalPtr;
    return ExistingResult->getResult();
  }

  // Otherwise, we have to scan for the value.  If we have a dirty cache
  // entry, start scanning from its position, otherwise we scan from the end
  // of the block.
  BasicBlock::iterator ScanPos = BB->end();
  if (ExistingResult && ExistingResult->getResult().getInst()) {
    assert(ExistingResult->getResult().getInst()->getParent() == BB &&
           "Instruction invalidated?");
    ++NumCacheDirtyNonLocalPtr;
    ScanPos = ExistingResult->getResult().getInst()->getIterator();

    // Eliminating the dirty entry from 'Cache', so update the reverse info.
    ValueIsLoadPair CacheKey(Loc.Ptr, isLoad);
    RemoveFromReverseMap(ReverseNonLocalPtrDeps, &*ScanPos, CacheKey);
  } else {
    ++NumUncacheNonLocalPtr;
  }

  // Scan the block for the dependency.
  MemDepResult Dep =
      getPointerDependencyFrom(Loc, isLoad, ScanPos, BB, QueryInst);

  // If we had a dirty entry for the block, update it.  Otherwise, just add
  // a new entry.
  if (ExistingResult)
    ExistingResult->setResult(Dep);
  else
    Cache->push_back(NonLocalDepEntry(BB, Dep));

  // If the block has a dependency (i.e. it isn't completely transparent to
  // the value), remember the reverse association because we just added it
  // to Cache!
  if (!Dep.isDef() && !Dep.isClobber())
    return Dep;

  // Keep the ReverseNonLocalPtrDeps map up to date so we can efficiently
  // update MemDep when we remove instructions.
  Instruction *Inst = Dep.getInst();
  assert(Inst && "Didn't depend on anything?");
  ValueIsLoadPair CacheKey(Loc.Ptr, isLoad);
  ReverseNonLocalPtrDeps[Inst].insert(CacheKey);
  return Dep;
}

/// Sort the NonLocalDepInfo cache, given a certain number of elements in the
/// array that are already properly ordered.
///
/// This is optimized for the case when only a few entries are added.
static void
SortNonLocalDepInfoCache(MemoryDependenceResults::NonLocalDepInfo &Cache,
                         unsigned NumSortedEntries) {
  switch (Cache.size() - NumSortedEntries) {
  case 0:
    // done, no new entries.
    break;
  case 2: {
    // Two new entries, insert the last one into place.
    NonLocalDepEntry Val = Cache.back();
    Cache.pop_back();
    MemoryDependenceResults::NonLocalDepInfo::iterator Entry =
        std::upper_bound(Cache.begin(), Cache.end() - 1, Val);
    Cache.insert(Entry, Val);
    LLVM_FALLTHROUGH;
  }
  case 1:
    // One new entry, Just insert the new value at the appropriate position.
    if (Cache.size() != 1) {
      NonLocalDepEntry Val = Cache.back();
      Cache.pop_back();
      MemoryDependenceResults::NonLocalDepInfo::iterator Entry =
          std::upper_bound(Cache.begin(), Cache.end(), Val);
      Cache.insert(Entry, Val);
    }
    break;
  default:
    // Added many values, do a full scale sort.
    llvm::sort(Cache);
    break;
  }
}

/// Perform a dependency query based on pointer/pointeesize starting at the end
/// of StartBB.
///
/// Add any clobber/def results to the results vector and keep track of which
/// blocks are visited in 'Visited'.
///
/// This has special behavior for the first block queries (when SkipFirstBlock
/// is true).  In this special case, it ignores the contents of the specified
/// block and starts returning dependence info for its predecessors.
///
/// This function returns true on success, or false to indicate that it could
/// not compute dependence information for some reason.  This should be treated
/// as a clobber dependence on the first instruction in the predecessor block.
bool MemoryDependenceResults::getNonLocalPointerDepFromBB(
    Instruction *QueryInst, const PHITransAddr &Pointer,
    const MemoryLocation &Loc, bool isLoad, BasicBlock *StartBB,
    SmallVectorImpl<NonLocalDepResult> &Result,
    DenseMap<BasicBlock *, Value *> &Visited, bool SkipFirstBlock) {
  // Look up the cached info for Pointer.
  ValueIsLoadPair CacheKey(Pointer.getAddr(), isLoad);

  // Set up a temporary NLPI value. If the map doesn't yet have an entry for
  // CacheKey, this value will be inserted as the associated value. Otherwise,
  // it'll be ignored, and we'll have to check to see if the cached size and
  // aa tags are consistent with the current query.
  NonLocalPointerInfo InitialNLPI;
  InitialNLPI.Size = Loc.Size;
  InitialNLPI.AATags = Loc.AATags;

  // Get the NLPI for CacheKey, inserting one into the map if it doesn't
  // already have one.
  std::pair<CachedNonLocalPointerInfo::iterator, bool> Pair =
      NonLocalPointerDeps.insert(std::make_pair(CacheKey, InitialNLPI));
  NonLocalPointerInfo *CacheInfo = &Pair.first->second;

  // If we already have a cache entry for this CacheKey, we may need to do some
  // work to reconcile the cache entry and the current query.
  if (!Pair.second) {
    if (CacheInfo->Size != Loc.Size) {
      bool ThrowOutEverything;
      if (CacheInfo->Size.hasValue() && Loc.Size.hasValue()) {
        // FIXME: We may be able to do better in the face of results with mixed
        // precision. We don't appear to get them in practice, though, so just
        // be conservative.
        ThrowOutEverything =
            CacheInfo->Size.isPrecise() != Loc.Size.isPrecise() ||
            CacheInfo->Size.getValue() < Loc.Size.getValue();
      } else {
        // For our purposes, unknown size > all others.
        ThrowOutEverything = !Loc.Size.hasValue();
      }

      if (ThrowOutEverything) {
        // The query's Size is greater than the cached one. Throw out the
        // cached data and proceed with the query at the greater size.
        CacheInfo->Pair = BBSkipFirstBlockPair();
        CacheInfo->Size = Loc.Size;
        for (auto &Entry : CacheInfo->NonLocalDeps)
          if (Instruction *Inst = Entry.getResult().getInst())
            RemoveFromReverseMap(ReverseNonLocalPtrDeps, Inst, CacheKey);
        CacheInfo->NonLocalDeps.clear();
      } else {
        // This query's Size is less than the cached one. Conservatively restart
        // the query using the greater size.
        return getNonLocalPointerDepFromBB(
            QueryInst, Pointer, Loc.getWithNewSize(CacheInfo->Size), isLoad,
            StartBB, Result, Visited, SkipFirstBlock);
      }
    }

    // If the query's AATags are inconsistent with the cached one,
    // conservatively throw out the cached data and restart the query with
    // no tag if needed.
    if (CacheInfo->AATags != Loc.AATags) {
      if (CacheInfo->AATags) {
        CacheInfo->Pair = BBSkipFirstBlockPair();
        CacheInfo->AATags = AAMDNodes();
        for (auto &Entry : CacheInfo->NonLocalDeps)
          if (Instruction *Inst = Entry.getResult().getInst())
            RemoveFromReverseMap(ReverseNonLocalPtrDeps, Inst, CacheKey);
        CacheInfo->NonLocalDeps.clear();
      }
      if (Loc.AATags)
        return getNonLocalPointerDepFromBB(
            QueryInst, Pointer, Loc.getWithoutAATags(), isLoad, StartBB, Result,
            Visited, SkipFirstBlock);
    }
  }

  NonLocalDepInfo *Cache = &CacheInfo->NonLocalDeps;

  // If we have valid cached information for exactly the block we are
  // investigating, just return it with no recomputation.
  if (CacheInfo->Pair == BBSkipFirstBlockPair(StartBB, SkipFirstBlock)) {
    // We have a fully cached result for this query then we can just return the
    // cached results and populate the visited set.  However, we have to verify
    // that we don't already have conflicting results for these blocks.  Check
    // to ensure that if a block in the results set is in the visited set that
    // it was for the same pointer query.
    if (!Visited.empty()) {
      for (auto &Entry : *Cache) {
        DenseMap<BasicBlock *, Value *>::iterator VI =
            Visited.find(Entry.getBB());
        if (VI == Visited.end() || VI->second == Pointer.getAddr())
          continue;

        // We have a pointer mismatch in a block.  Just return false, saying
        // that something was clobbered in this result.  We could also do a
        // non-fully cached query, but there is little point in doing this.
        return false;
      }
    }

    Value *Addr = Pointer.getAddr();
    for (auto &Entry : *Cache) {
      Visited.insert(std::make_pair(Entry.getBB(), Addr));
      if (Entry.getResult().isNonLocal()) {
        continue;
      }

      if (DT.isReachableFromEntry(Entry.getBB())) {
        Result.push_back(
            NonLocalDepResult(Entry.getBB(), Entry.getResult(), Addr));
      }
    }
    ++NumCacheCompleteNonLocalPtr;
    return true;
  }

  // Otherwise, either this is a new block, a block with an invalid cache
  // pointer or one that we're about to invalidate by putting more info into it
  // than its valid cache info.  If empty, the result will be valid cache info,
  // otherwise it isn't.
  if (Cache->empty())
    CacheInfo->Pair = BBSkipFirstBlockPair(StartBB, SkipFirstBlock);
  else
    CacheInfo->Pair = BBSkipFirstBlockPair();

  SmallVector<BasicBlock *, 32> Worklist;
  Worklist.push_back(StartBB);

  // PredList used inside loop.
  SmallVector<std::pair<BasicBlock *, PHITransAddr>, 16> PredList;

  // Keep track of the entries that we know are sorted.  Previously cached
  // entries will all be sorted.  The entries we add we only sort on demand (we
  // don't insert every element into its sorted position).  We know that we
  // won't get any reuse from currently inserted values, because we don't
  // revisit blocks after we insert info for them.
  unsigned NumSortedEntries = Cache->size();
  unsigned WorklistEntries = BlockNumberLimit;
  bool GotWorklistLimit = false;
  LLVM_DEBUG(AssertSorted(*Cache));

  while (!Worklist.empty()) {
    BasicBlock *BB = Worklist.pop_back_val();

    // If we do process a large number of blocks it becomes very expensive and
    // likely it isn't worth worrying about
    if (Result.size() > NumResultsLimit) {
      Worklist.clear();
      // Sort it now (if needed) so that recursive invocations of
      // getNonLocalPointerDepFromBB and other routines that could reuse the
      // cache value will only see properly sorted cache arrays.
      if (Cache && NumSortedEntries != Cache->size()) {
        SortNonLocalDepInfoCache(*Cache, NumSortedEntries);
      }
      // Since we bail out, the "Cache" set won't contain all of the
      // results for the query.  This is ok (we can still use it to accelerate
      // specific block queries) but we can't do the fastpath "return all
      // results from the set".  Clear out the indicator for this.
      CacheInfo->Pair = BBSkipFirstBlockPair();
      return false;
    }

    // Skip the first block if we have it.
    if (!SkipFirstBlock) {
      // Analyze the dependency of *Pointer in FromBB.  See if we already have
      // been here.
      assert(Visited.count(BB) && "Should check 'visited' before adding to WL");

      // Get the dependency info for Pointer in BB.  If we have cached
      // information, we will use it, otherwise we compute it.
      LLVM_DEBUG(AssertSorted(*Cache, NumSortedEntries));
      MemDepResult Dep = GetNonLocalInfoForBlock(QueryInst, Loc, isLoad, BB,
                                                 Cache, NumSortedEntries);

      // If we got a Def or Clobber, add this to the list of results.
      if (!Dep.isNonLocal()) {
        if (DT.isReachableFromEntry(BB)) {
          Result.push_back(NonLocalDepResult(BB, Dep, Pointer.getAddr()));
          continue;
        }
      }
    }

    // If 'Pointer' is an instruction defined in this block, then we need to do
    // phi translation to change it into a value live in the predecessor block.
    // If not, we just add the predecessors to the worklist and scan them with
    // the same Pointer.
    if (!Pointer.NeedsPHITranslationFromBlock(BB)) {
      SkipFirstBlock = false;
      SmallVector<BasicBlock *, 16> NewBlocks;
      for (BasicBlock *Pred : PredCache.get(BB)) {
        // Verify that we haven't looked at this block yet.
        std::pair<DenseMap<BasicBlock *, Value *>::iterator, bool> InsertRes =
            Visited.insert(std::make_pair(Pred, Pointer.getAddr()));
        if (InsertRes.second) {
          // First time we've looked at *PI.
          NewBlocks.push_back(Pred);
          continue;
        }

        // If we have seen this block before, but it was with a different
        // pointer then we have a phi translation failure and we have to treat
        // this as a clobber.
        if (InsertRes.first->second != Pointer.getAddr()) {
          // Make sure to clean up the Visited map before continuing on to
          // PredTranslationFailure.
          for (unsigned i = 0; i < NewBlocks.size(); i++)
            Visited.erase(NewBlocks[i]);
          goto PredTranslationFailure;
        }
      }
      if (NewBlocks.size() > WorklistEntries) {
        // Make sure to clean up the Visited map before continuing on to
        // PredTranslationFailure.
        for (unsigned i = 0; i < NewBlocks.size(); i++)
          Visited.erase(NewBlocks[i]);
        GotWorklistLimit = true;
        goto PredTranslationFailure;
      }
      WorklistEntries -= NewBlocks.size();
      Worklist.append(NewBlocks.begin(), NewBlocks.end());
      continue;
    }

    // We do need to do phi translation, if we know ahead of time we can't phi
    // translate this value, don't even try.
    if (!Pointer.IsPotentiallyPHITranslatable())
      goto PredTranslationFailure;

    // We may have added values to the cache list before this PHI translation.
    // If so, we haven't done anything to ensure that the cache remains sorted.
    // Sort it now (if needed) so that recursive invocations of
    // getNonLocalPointerDepFromBB and other routines that could reuse the cache
    // value will only see properly sorted cache arrays.
    if (Cache && NumSortedEntries != Cache->size()) {
      SortNonLocalDepInfoCache(*Cache, NumSortedEntries);
      NumSortedEntries = Cache->size();
    }
    Cache = nullptr;

    PredList.clear();
    for (BasicBlock *Pred : PredCache.get(BB)) {
      PredList.push_back(std::make_pair(Pred, Pointer));

      // Get the PHI translated pointer in this predecessor.  This can fail if
      // not translatable, in which case the getAddr() returns null.
      PHITransAddr &PredPointer = PredList.back().second;
      PredPointer.PHITranslateValue(BB, Pred, &DT, /*MustDominate=*/false);
      Value *PredPtrVal = PredPointer.getAddr();

      // Check to see if we have already visited this pred block with another
      // pointer.  If so, we can't do this lookup.  This failure can occur
      // with PHI translation when a critical edge exists and the PHI node in
      // the successor translates to a pointer value different than the
      // pointer the block was first analyzed with.
      std::pair<DenseMap<BasicBlock *, Value *>::iterator, bool> InsertRes =
          Visited.insert(std::make_pair(Pred, PredPtrVal));

      if (!InsertRes.second) {
        // We found the pred; take it off the list of preds to visit.
        PredList.pop_back();

        // If the predecessor was visited with PredPtr, then we already did
        // the analysis and can ignore it.
        if (InsertRes.first->second == PredPtrVal)
          continue;

        // Otherwise, the block was previously analyzed with a different
        // pointer.  We can't represent the result of this case, so we just
        // treat this as a phi translation failure.

        // Make sure to clean up the Visited map before continuing on to
        // PredTranslationFailure.
        for (unsigned i = 0, n = PredList.size(); i < n; ++i)
          Visited.erase(PredList[i].first);

        goto PredTranslationFailure;
      }
    }

    // Actually process results here; this need to be a separate loop to avoid
    // calling getNonLocalPointerDepFromBB for blocks we don't want to return
    // any results for.  (getNonLocalPointerDepFromBB will modify our
    // datastructures in ways the code after the PredTranslationFailure label
    // doesn't expect.)
    for (unsigned i = 0, n = PredList.size(); i < n; ++i) {
      BasicBlock *Pred = PredList[i].first;
      PHITransAddr &PredPointer = PredList[i].second;
      Value *PredPtrVal = PredPointer.getAddr();

      bool CanTranslate = true;
      // If PHI translation was unable to find an available pointer in this
      // predecessor, then we have to assume that the pointer is clobbered in
      // that predecessor.  We can still do PRE of the load, which would insert
      // a computation of the pointer in this predecessor.
      if (!PredPtrVal)
        CanTranslate = false;

      // FIXME: it is entirely possible that PHI translating will end up with
      // the same value.  Consider PHI translating something like:
      // X = phi [x, bb1], [y, bb2].  PHI translating for bb1 doesn't *need*
      // to recurse here, pedantically speaking.

      // If getNonLocalPointerDepFromBB fails here, that means the cached
      // result conflicted with the Visited list; we have to conservatively
      // assume it is unknown, but this also does not block PRE of the load.
      if (!CanTranslate ||
          !getNonLocalPointerDepFromBB(QueryInst, PredPointer,
                                      Loc.getWithNewPtr(PredPtrVal), isLoad,
                                      Pred, Result, Visited)) {
        // Add the entry to the Result list.
        NonLocalDepResult Entry(Pred, MemDepResult::getUnknown(), PredPtrVal);
        Result.push_back(Entry);

        // Since we had a phi translation failure, the cache for CacheKey won't
        // include all of the entries that we need to immediately satisfy future
        // queries.  Mark this in NonLocalPointerDeps by setting the
        // BBSkipFirstBlockPair pointer to null.  This requires reuse of the
        // cached value to do more work but not miss the phi trans failure.
        NonLocalPointerInfo &NLPI = NonLocalPointerDeps[CacheKey];
        NLPI.Pair = BBSkipFirstBlockPair();
        continue;
      }
    }

    // Refresh the CacheInfo/Cache pointer so that it isn't invalidated.
    CacheInfo = &NonLocalPointerDeps[CacheKey];
    Cache = &CacheInfo->NonLocalDeps;
    NumSortedEntries = Cache->size();

    // Since we did phi translation, the "Cache" set won't contain all of the
    // results for the query.  This is ok (we can still use it to accelerate
    // specific block queries) but we can't do the fastpath "return all
    // results from the set"  Clear out the indicator for this.
    CacheInfo->Pair = BBSkipFirstBlockPair();
    SkipFirstBlock = false;
    continue;

  PredTranslationFailure:
    // The following code is "failure"; we can't produce a sane translation
    // for the given block.  It assumes that we haven't modified any of
    // our datastructures while processing the current block.

    if (!Cache) {
      // Refresh the CacheInfo/Cache pointer if it got invalidated.
      CacheInfo = &NonLocalPointerDeps[CacheKey];
      Cache = &CacheInfo->NonLocalDeps;
      NumSortedEntries = Cache->size();
    }

    // Since we failed phi translation, the "Cache" set won't contain all of the
    // results for the query.  This is ok (we can still use it to accelerate
    // specific block queries) but we can't do the fastpath "return all
    // results from the set".  Clear out the indicator for this.
    CacheInfo->Pair = BBSkipFirstBlockPair();

    // If *nothing* works, mark the pointer as unknown.
    //
    // If this is the magic first block, return this as a clobber of the whole
    // incoming value.  Since we can't phi translate to one of the predecessors,
    // we have to bail out.
    if (SkipFirstBlock)
      return false;

    bool foundBlock = false;
    for (NonLocalDepEntry &I : llvm::reverse(*Cache)) {
      if (I.getBB() != BB)
        continue;

      assert((GotWorklistLimit || I.getResult().isNonLocal() ||
              !DT.isReachableFromEntry(BB)) &&
             "Should only be here with transparent block");
      foundBlock = true;
      I.setResult(MemDepResult::getUnknown());
      Result.push_back(
          NonLocalDepResult(I.getBB(), I.getResult(), Pointer.getAddr()));
      break;
    }
    (void)foundBlock; (void)GotWorklistLimit;
    assert((foundBlock || GotWorklistLimit) && "Current block not in cache?");
  }

  // Okay, we're done now.  If we added new values to the cache, re-sort it.
  SortNonLocalDepInfoCache(*Cache, NumSortedEntries);
  LLVM_DEBUG(AssertSorted(*Cache));
  return true;
}

/// If P exists in CachedNonLocalPointerInfo or NonLocalDefsCache, remove it.
void MemoryDependenceResults::RemoveCachedNonLocalPointerDependencies(
    ValueIsLoadPair P) {

  // Most of the time this cache is empty.
  if (!NonLocalDefsCache.empty()) {
    auto it = NonLocalDefsCache.find(P.getPointer());
    if (it != NonLocalDefsCache.end()) {
      RemoveFromReverseMap(ReverseNonLocalDefsCache,
                           it->second.getResult().getInst(), P.getPointer());
      NonLocalDefsCache.erase(it);
    }

    if (auto *I = dyn_cast<Instruction>(P.getPointer())) {
      auto toRemoveIt = ReverseNonLocalDefsCache.find(I);
      if (toRemoveIt != ReverseNonLocalDefsCache.end()) {
        for (const auto &entry : toRemoveIt->second)
          NonLocalDefsCache.erase(entry);
        ReverseNonLocalDefsCache.erase(toRemoveIt);
      }
    }
  }

  CachedNonLocalPointerInfo::iterator It = NonLocalPointerDeps.find(P);
  if (It == NonLocalPointerDeps.end())
    return;

  // Remove all of the entries in the BB->val map.  This involves removing
  // instructions from the reverse map.
  NonLocalDepInfo &PInfo = It->second.NonLocalDeps;

  for (unsigned i = 0, e = PInfo.size(); i != e; ++i) {
    Instruction *Target = PInfo[i].getResult().getInst();
    if (!Target)
      continue; // Ignore non-local dep results.
    assert(Target->getParent() == PInfo[i].getBB());

    // Eliminating the dirty entry from 'Cache', so update the reverse info.
    RemoveFromReverseMap(ReverseNonLocalPtrDeps, Target, P);
  }

  // Remove P from NonLocalPointerDeps (which deletes NonLocalDepInfo).
  NonLocalPointerDeps.erase(It);
}

void MemoryDependenceResults::invalidateCachedPointerInfo(Value *Ptr) {
  // If Ptr isn't really a pointer, just ignore it.
  if (!Ptr->getType()->isPointerTy())
    return;
  // Flush store info for the pointer.
  RemoveCachedNonLocalPointerDependencies(ValueIsLoadPair(Ptr, false));
  // Flush load info for the pointer.
  RemoveCachedNonLocalPointerDependencies(ValueIsLoadPair(Ptr, true));
  // Invalidate phis that use the pointer.
  PV.invalidateValue(Ptr);
}

void MemoryDependenceResults::invalidateCachedPredecessors() {
  PredCache.clear();
}

void MemoryDependenceResults::removeInstruction(Instruction *RemInst) {
  // Walk through the Non-local dependencies, removing this one as the value
  // for any cached queries.
  NonLocalDepMapType::iterator NLDI = NonLocalDeps.find(RemInst);
  if (NLDI != NonLocalDeps.end()) {
    NonLocalDepInfo &BlockMap = NLDI->second.first;
    for (auto &Entry : BlockMap)
      if (Instruction *Inst = Entry.getResult().getInst())
        RemoveFromReverseMap(ReverseNonLocalDeps, Inst, RemInst);
    NonLocalDeps.erase(NLDI);
  }

  // If we have a cached local dependence query for this instruction, remove it.
  LocalDepMapType::iterator LocalDepEntry = LocalDeps.find(RemInst);
  if (LocalDepEntry != LocalDeps.end()) {
    // Remove us from DepInst's reverse set now that the local dep info is gone.
    if (Instruction *Inst = LocalDepEntry->second.getInst())
      RemoveFromReverseMap(ReverseLocalDeps, Inst, RemInst);

    // Remove this local dependency info.
    LocalDeps.erase(LocalDepEntry);
  }

  // If we have any cached pointer dependencies on this instruction, remove
  // them.  If the instruction has non-pointer type, then it can't be a pointer
  // base.

  // Remove it from both the load info and the store info.  The instruction
  // can't be in either of these maps if it is non-pointer.
  if (RemInst->getType()->isPointerTy()) {
    RemoveCachedNonLocalPointerDependencies(ValueIsLoadPair(RemInst, false));
    RemoveCachedNonLocalPointerDependencies(ValueIsLoadPair(RemInst, true));
  }

  // Loop over all of the things that depend on the instruction we're removing.
  SmallVector<std::pair<Instruction *, Instruction *>, 8> ReverseDepsToAdd;

  // If we find RemInst as a clobber or Def in any of the maps for other values,
  // we need to replace its entry with a dirty version of the instruction after
  // it.  If RemInst is a terminator, we use a null dirty value.
  //
  // Using a dirty version of the instruction after RemInst saves having to scan
  // the entire block to get to this point.
  MemDepResult NewDirtyVal;
  if (!RemInst->isTerminator())
    NewDirtyVal = MemDepResult::getDirty(&*++RemInst->getIterator());

  ReverseDepMapType::iterator ReverseDepIt = ReverseLocalDeps.find(RemInst);
  if (ReverseDepIt != ReverseLocalDeps.end()) {
    // RemInst can't be the terminator if it has local stuff depending on it.
    assert(!ReverseDepIt->second.empty() && !RemInst->isTerminator() &&
           "Nothing can locally depend on a terminator");

    for (Instruction *InstDependingOnRemInst : ReverseDepIt->second) {
      assert(InstDependingOnRemInst != RemInst &&
             "Already removed our local dep info");

      LocalDeps[InstDependingOnRemInst] = NewDirtyVal;

      // Make sure to remember that new things depend on NewDepInst.
      assert(NewDirtyVal.getInst() &&
             "There is no way something else can have "
             "a local dep on this if it is a terminator!");
      ReverseDepsToAdd.push_back(
          std::make_pair(NewDirtyVal.getInst(), InstDependingOnRemInst));
    }

    ReverseLocalDeps.erase(ReverseDepIt);

    // Add new reverse deps after scanning the set, to avoid invalidating the
    // 'ReverseDeps' reference.
    while (!ReverseDepsToAdd.empty()) {
      ReverseLocalDeps[ReverseDepsToAdd.back().first].insert(
          ReverseDepsToAdd.back().second);
      ReverseDepsToAdd.pop_back();
    }
  }

  ReverseDepIt = ReverseNonLocalDeps.find(RemInst);
  if (ReverseDepIt != ReverseNonLocalDeps.end()) {
    for (Instruction *I : ReverseDepIt->second) {
      assert(I != RemInst && "Already removed NonLocalDep info for RemInst");

      PerInstNLInfo &INLD = NonLocalDeps[I];
      // The information is now dirty!
      INLD.second = true;

      for (auto &Entry : INLD.first) {
        if (Entry.getResult().getInst() != RemInst)
          continue;

        // Convert to a dirty entry for the subsequent instruction.
        Entry.setResult(NewDirtyVal);

        if (Instruction *NextI = NewDirtyVal.getInst())
          ReverseDepsToAdd.push_back(std::make_pair(NextI, I));
      }
    }

    ReverseNonLocalDeps.erase(ReverseDepIt);

    // Add new reverse deps after scanning the set, to avoid invalidating 'Set'
    while (!ReverseDepsToAdd.empty()) {
      ReverseNonLocalDeps[ReverseDepsToAdd.back().first].insert(
          ReverseDepsToAdd.back().second);
      ReverseDepsToAdd.pop_back();
    }
  }

  // If the instruction is in ReverseNonLocalPtrDeps then it appears as a
  // value in the NonLocalPointerDeps info.
  ReverseNonLocalPtrDepTy::iterator ReversePtrDepIt =
      ReverseNonLocalPtrDeps.find(RemInst);
  if (ReversePtrDepIt != ReverseNonLocalPtrDeps.end()) {
    SmallVector<std::pair<Instruction *, ValueIsLoadPair>, 8>
        ReversePtrDepsToAdd;

    for (ValueIsLoadPair P : ReversePtrDepIt->second) {
      assert(P.getPointer() != RemInst &&
             "Already removed NonLocalPointerDeps info for RemInst");

      NonLocalDepInfo &NLPDI = NonLocalPointerDeps[P].NonLocalDeps;

      // The cache is not valid for any specific block anymore.
      NonLocalPointerDeps[P].Pair = BBSkipFirstBlockPair();

      // Update any entries for RemInst to use the instruction after it.
      for (auto &Entry : NLPDI) {
        if (Entry.getResult().getInst() != RemInst)
          continue;

        // Convert to a dirty entry for the subsequent instruction.
        Entry.setResult(NewDirtyVal);

        if (Instruction *NewDirtyInst = NewDirtyVal.getInst())
          ReversePtrDepsToAdd.push_back(std::make_pair(NewDirtyInst, P));
      }

      // Re-sort the NonLocalDepInfo.  Changing the dirty entry to its
      // subsequent value may invalidate the sortedness.
      llvm::sort(NLPDI);
    }

    ReverseNonLocalPtrDeps.erase(ReversePtrDepIt);

    while (!ReversePtrDepsToAdd.empty()) {
      ReverseNonLocalPtrDeps[ReversePtrDepsToAdd.back().first].insert(
          ReversePtrDepsToAdd.back().second);
      ReversePtrDepsToAdd.pop_back();
    }
  }

  // Invalidate phis that use the removed instruction.
  PV.invalidateValue(RemInst);

  assert(!NonLocalDeps.count(RemInst) && "RemInst got reinserted?");
  LLVM_DEBUG(verifyRemoved(RemInst));
}

/// Verify that the specified instruction does not occur in our internal data
/// structures.
///
/// This function verifies by asserting in debug builds.
void MemoryDependenceResults::verifyRemoved(Instruction *D) const {
#ifndef NDEBUG
  for (const auto &DepKV : LocalDeps) {
    assert(DepKV.first != D && "Inst occurs in data structures");
    assert(DepKV.second.getInst() != D && "Inst occurs in data structures");
  }

  for (const auto &DepKV : NonLocalPointerDeps) {
    assert(DepKV.first.getPointer() != D && "Inst occurs in NLPD map key");
    for (const auto &Entry : DepKV.second.NonLocalDeps)
      assert(Entry.getResult().getInst() != D && "Inst occurs as NLPD value");
  }

  for (const auto &DepKV : NonLocalDeps) {
    assert(DepKV.first != D && "Inst occurs in data structures");
    const PerInstNLInfo &INLD = DepKV.second;
    for (const auto &Entry : INLD.first)
      assert(Entry.getResult().getInst() != D &&
             "Inst occurs in data structures");
  }

  for (const auto &DepKV : ReverseLocalDeps) {
    assert(DepKV.first != D && "Inst occurs in data structures");
    for (Instruction *Inst : DepKV.second)
      assert(Inst != D && "Inst occurs in data structures");
  }

  for (const auto &DepKV : ReverseNonLocalDeps) {
    assert(DepKV.first != D && "Inst occurs in data structures");
    for (Instruction *Inst : DepKV.second)
      assert(Inst != D && "Inst occurs in data structures");
  }

  for (const auto &DepKV : ReverseNonLocalPtrDeps) {
    assert(DepKV.first != D && "Inst occurs in rev NLPD map");

    for (ValueIsLoadPair P : DepKV.second)
      assert(P != ValueIsLoadPair(D, false) && P != ValueIsLoadPair(D, true) &&
             "Inst occurs in ReverseNonLocalPtrDeps map");
  }
#endif
}

AnalysisKey MemoryDependenceAnalysis::Key;

MemoryDependenceResults
MemoryDependenceAnalysis::run(Function &F, FunctionAnalysisManager &AM) {
  auto &AA = AM.getResult<AAManager>(F);
  auto &AC = AM.getResult<AssumptionAnalysis>(F);
  auto &TLI = AM.getResult<TargetLibraryAnalysis>(F);
  auto &DT = AM.getResult<DominatorTreeAnalysis>(F);
  auto &PV = AM.getResult<PhiValuesAnalysis>(F);
  return MemoryDependenceResults(AA, AC, TLI, DT, PV);
}

char MemoryDependenceWrapperPass::ID = 0;

INITIALIZE_PASS_BEGIN(MemoryDependenceWrapperPass, "memdep",
                      "Memory Dependence Analysis", false, true)
INITIALIZE_PASS_DEPENDENCY(AssumptionCacheTracker)
INITIALIZE_PASS_DEPENDENCY(AAResultsWrapperPass)
INITIALIZE_PASS_DEPENDENCY(DominatorTreeWrapperPass)
INITIALIZE_PASS_DEPENDENCY(TargetLibraryInfoWrapperPass)
INITIALIZE_PASS_DEPENDENCY(PhiValuesWrapperPass)
INITIALIZE_PASS_END(MemoryDependenceWrapperPass, "memdep",
                    "Memory Dependence Analysis", false, true)

MemoryDependenceWrapperPass::MemoryDependenceWrapperPass() : FunctionPass(ID) {
  initializeMemoryDependenceWrapperPassPass(*PassRegistry::getPassRegistry());
}

MemoryDependenceWrapperPass::~MemoryDependenceWrapperPass() = default;

void MemoryDependenceWrapperPass::releaseMemory() {
  MemDep.reset();
}

void MemoryDependenceWrapperPass::getAnalysisUsage(AnalysisUsage &AU) const {
  AU.setPreservesAll();
  AU.addRequired<AssumptionCacheTracker>();
  AU.addRequired<DominatorTreeWrapperPass>();
  AU.addRequired<PhiValuesWrapperPass>();
  AU.addRequiredTransitive<AAResultsWrapperPass>();
  AU.addRequiredTransitive<TargetLibraryInfoWrapperPass>();
}

bool MemoryDependenceResults::invalidate(Function &F, const PreservedAnalyses &PA,
                               FunctionAnalysisManager::Invalidator &Inv) {
  // Check whether our analysis is preserved.
  auto PAC = PA.getChecker<MemoryDependenceAnalysis>();
  if (!PAC.preserved() && !PAC.preservedSet<AllAnalysesOn<Function>>())
    // If not, give up now.
    return true;

  // Check whether the analyses we depend on became invalid for any reason.
  if (Inv.invalidate<AAManager>(F, PA) ||
      Inv.invalidate<AssumptionAnalysis>(F, PA) ||
      Inv.invalidate<DominatorTreeAnalysis>(F, PA) ||
      Inv.invalidate<PhiValuesAnalysis>(F, PA))
    return true;

  // Otherwise this analysis result remains valid.
  return false;
}

unsigned MemoryDependenceResults::getDefaultBlockScanLimit() const {
  return BlockScanLimit;
}

bool MemoryDependenceWrapperPass::runOnFunction(Function &F) {
  auto &AA = getAnalysis<AAResultsWrapperPass>().getAAResults();
  auto &AC = getAnalysis<AssumptionCacheTracker>().getAssumptionCache(F);
  auto &TLI = getAnalysis<TargetLibraryInfoWrapperPass>().getTLI();
  auto &DT = getAnalysis<DominatorTreeWrapperPass>().getDomTree();
  auto &PV = getAnalysis<PhiValuesWrapperPass>().getResult();
  MemDep.emplace(AA, AC, TLI, DT, PV);
  return false;
}
