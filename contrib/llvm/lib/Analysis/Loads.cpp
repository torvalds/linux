//===- Loads.cpp - Local load analysis ------------------------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file defines simple local analyses for load instructions.
//
//===----------------------------------------------------------------------===//

#include "llvm/Analysis/Loads.h"
#include "llvm/Analysis/AliasAnalysis.h"
#include "llvm/Analysis/ValueTracking.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/IR/GlobalAlias.h"
#include "llvm/IR/GlobalVariable.h"
#include "llvm/IR/IntrinsicInst.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Operator.h"
#include "llvm/IR/Statepoint.h"

using namespace llvm;

static bool isAligned(const Value *Base, const APInt &Offset, unsigned Align,
                      const DataLayout &DL) {
  APInt BaseAlign(Offset.getBitWidth(), Base->getPointerAlignment(DL));

  if (!BaseAlign) {
    Type *Ty = Base->getType()->getPointerElementType();
    if (!Ty->isSized())
      return false;
    BaseAlign = DL.getABITypeAlignment(Ty);
  }

  APInt Alignment(Offset.getBitWidth(), Align);

  assert(Alignment.isPowerOf2() && "must be a power of 2!");
  return BaseAlign.uge(Alignment) && !(Offset & (Alignment-1));
}

static bool isAligned(const Value *Base, unsigned Align, const DataLayout &DL) {
  Type *Ty = Base->getType();
  assert(Ty->isSized() && "must be sized");
  APInt Offset(DL.getTypeStoreSizeInBits(Ty), 0);
  return isAligned(Base, Offset, Align, DL);
}

/// Test if V is always a pointer to allocated and suitably aligned memory for
/// a simple load or store.
static bool isDereferenceableAndAlignedPointer(
    const Value *V, unsigned Align, const APInt &Size, const DataLayout &DL,
    const Instruction *CtxI, const DominatorTree *DT,
    SmallPtrSetImpl<const Value *> &Visited) {
  // Already visited?  Bail out, we've likely hit unreachable code.
  if (!Visited.insert(V).second)
    return false;

  // Note that it is not safe to speculate into a malloc'd region because
  // malloc may return null.

  // bitcast instructions are no-ops as far as dereferenceability is concerned.
  if (const BitCastOperator *BC = dyn_cast<BitCastOperator>(V))
    return isDereferenceableAndAlignedPointer(BC->getOperand(0), Align, Size,
                                              DL, CtxI, DT, Visited);

  bool CheckForNonNull = false;
  APInt KnownDerefBytes(Size.getBitWidth(),
                        V->getPointerDereferenceableBytes(DL, CheckForNonNull));
  if (KnownDerefBytes.getBoolValue()) {
    if (KnownDerefBytes.uge(Size))
      if (!CheckForNonNull || isKnownNonZero(V, DL, 0, nullptr, CtxI, DT))
        return isAligned(V, Align, DL);
  }

  // For GEPs, determine if the indexing lands within the allocated object.
  if (const GEPOperator *GEP = dyn_cast<GEPOperator>(V)) {
    const Value *Base = GEP->getPointerOperand();

    APInt Offset(DL.getIndexTypeSizeInBits(GEP->getType()), 0);
    if (!GEP->accumulateConstantOffset(DL, Offset) || Offset.isNegative() ||
        !Offset.urem(APInt(Offset.getBitWidth(), Align)).isMinValue())
      return false;

    // If the base pointer is dereferenceable for Offset+Size bytes, then the
    // GEP (== Base + Offset) is dereferenceable for Size bytes.  If the base
    // pointer is aligned to Align bytes, and the Offset is divisible by Align
    // then the GEP (== Base + Offset == k_0 * Align + k_1 * Align) is also
    // aligned to Align bytes.

    // Offset and Size may have different bit widths if we have visited an
    // addrspacecast, so we can't do arithmetic directly on the APInt values.
    return isDereferenceableAndAlignedPointer(
        Base, Align, Offset + Size.sextOrTrunc(Offset.getBitWidth()),
        DL, CtxI, DT, Visited);
  }

  // For gc.relocate, look through relocations
  if (const GCRelocateInst *RelocateInst = dyn_cast<GCRelocateInst>(V))
    return isDereferenceableAndAlignedPointer(
        RelocateInst->getDerivedPtr(), Align, Size, DL, CtxI, DT, Visited);

  if (const AddrSpaceCastInst *ASC = dyn_cast<AddrSpaceCastInst>(V))
    return isDereferenceableAndAlignedPointer(ASC->getOperand(0), Align, Size,
                                              DL, CtxI, DT, Visited);

  if (const auto *Call = dyn_cast<CallBase>(V))
    if (auto *RP = getArgumentAliasingToReturnedPointer(Call))
      return isDereferenceableAndAlignedPointer(RP, Align, Size, DL, CtxI, DT,
                                                Visited);

  // If we don't know, assume the worst.
  return false;
}

bool llvm::isDereferenceableAndAlignedPointer(const Value *V, unsigned Align,
                                              const APInt &Size,
                                              const DataLayout &DL,
                                              const Instruction *CtxI,
                                              const DominatorTree *DT) {
  SmallPtrSet<const Value *, 32> Visited;
  return ::isDereferenceableAndAlignedPointer(V, Align, Size, DL, CtxI, DT,
                                              Visited);
}

bool llvm::isDereferenceableAndAlignedPointer(const Value *V, unsigned Align,
                                              const DataLayout &DL,
                                              const Instruction *CtxI,
                                              const DominatorTree *DT) {
  // When dereferenceability information is provided by a dereferenceable
  // attribute, we know exactly how many bytes are dereferenceable. If we can
  // determine the exact offset to the attributed variable, we can use that
  // information here.
  Type *VTy = V->getType();
  Type *Ty = VTy->getPointerElementType();

  // Require ABI alignment for loads without alignment specification
  if (Align == 0)
    Align = DL.getABITypeAlignment(Ty);

  if (!Ty->isSized())
    return false;

  SmallPtrSet<const Value *, 32> Visited;
  return ::isDereferenceableAndAlignedPointer(
      V, Align, APInt(DL.getIndexTypeSizeInBits(VTy), DL.getTypeStoreSize(Ty)), DL,
      CtxI, DT, Visited);
}

bool llvm::isDereferenceablePointer(const Value *V, const DataLayout &DL,
                                    const Instruction *CtxI,
                                    const DominatorTree *DT) {
  return isDereferenceableAndAlignedPointer(V, 1, DL, CtxI, DT);
}

/// Test if A and B will obviously have the same value.
///
/// This includes recognizing that %t0 and %t1 will have the same
/// value in code like this:
/// \code
///   %t0 = getelementptr \@a, 0, 3
///   store i32 0, i32* %t0
///   %t1 = getelementptr \@a, 0, 3
///   %t2 = load i32* %t1
/// \endcode
///
static bool AreEquivalentAddressValues(const Value *A, const Value *B) {
  // Test if the values are trivially equivalent.
  if (A == B)
    return true;

  // Test if the values come from identical arithmetic instructions.
  // Use isIdenticalToWhenDefined instead of isIdenticalTo because
  // this function is only used when one address use dominates the
  // other, which means that they'll always either have the same
  // value or one of them will have an undefined value.
  if (isa<BinaryOperator>(A) || isa<CastInst>(A) || isa<PHINode>(A) ||
      isa<GetElementPtrInst>(A))
    if (const Instruction *BI = dyn_cast<Instruction>(B))
      if (cast<Instruction>(A)->isIdenticalToWhenDefined(BI))
        return true;

  // Otherwise they may not be equivalent.
  return false;
}

/// Check if executing a load of this pointer value cannot trap.
///
/// If DT and ScanFrom are specified this method performs context-sensitive
/// analysis and returns true if it is safe to load immediately before ScanFrom.
///
/// If it is not obviously safe to load from the specified pointer, we do
/// a quick local scan of the basic block containing \c ScanFrom, to determine
/// if the address is already accessed.
///
/// This uses the pointee type to determine how many bytes need to be safe to
/// load from the pointer.
bool llvm::isSafeToLoadUnconditionally(Value *V, unsigned Align,
                                       const DataLayout &DL,
                                       Instruction *ScanFrom,
                                       const DominatorTree *DT) {
  // Zero alignment means that the load has the ABI alignment for the target
  if (Align == 0)
    Align = DL.getABITypeAlignment(V->getType()->getPointerElementType());
  assert(isPowerOf2_32(Align));

  // If DT is not specified we can't make context-sensitive query
  const Instruction* CtxI = DT ? ScanFrom : nullptr;
  if (isDereferenceableAndAlignedPointer(V, Align, DL, CtxI, DT))
    return true;

  int64_t ByteOffset = 0;
  Value *Base = V;
  Base = GetPointerBaseWithConstantOffset(V, ByteOffset, DL);

  if (ByteOffset < 0) // out of bounds
    return false;

  Type *BaseType = nullptr;
  unsigned BaseAlign = 0;
  if (const AllocaInst *AI = dyn_cast<AllocaInst>(Base)) {
    // An alloca is safe to load from as load as it is suitably aligned.
    BaseType = AI->getAllocatedType();
    BaseAlign = AI->getAlignment();
  } else if (const GlobalVariable *GV = dyn_cast<GlobalVariable>(Base)) {
    // Global variables are not necessarily safe to load from if they are
    // interposed arbitrarily. Their size may change or they may be weak and
    // require a test to determine if they were in fact provided.
    if (!GV->isInterposable()) {
      BaseType = GV->getType()->getElementType();
      BaseAlign = GV->getAlignment();
    }
  }

  PointerType *AddrTy = cast<PointerType>(V->getType());
  uint64_t LoadSize = DL.getTypeStoreSize(AddrTy->getElementType());

  // If we found a base allocated type from either an alloca or global variable,
  // try to see if we are definitively within the allocated region. We need to
  // know the size of the base type and the loaded type to do anything in this
  // case.
  if (BaseType && BaseType->isSized()) {
    if (BaseAlign == 0)
      BaseAlign = DL.getPrefTypeAlignment(BaseType);

    if (Align <= BaseAlign) {
      // Check if the load is within the bounds of the underlying object.
      if (ByteOffset + LoadSize <= DL.getTypeAllocSize(BaseType) &&
          ((ByteOffset % Align) == 0))
        return true;
    }
  }

  if (!ScanFrom)
    return false;

  // Otherwise, be a little bit aggressive by scanning the local block where we
  // want to check to see if the pointer is already being loaded or stored
  // from/to.  If so, the previous load or store would have already trapped,
  // so there is no harm doing an extra load (also, CSE will later eliminate
  // the load entirely).
  BasicBlock::iterator BBI = ScanFrom->getIterator(),
                       E = ScanFrom->getParent()->begin();

  // We can at least always strip pointer casts even though we can't use the
  // base here.
  V = V->stripPointerCasts();

  while (BBI != E) {
    --BBI;

    // If we see a free or a call which may write to memory (i.e. which might do
    // a free) the pointer could be marked invalid.
    if (isa<CallInst>(BBI) && BBI->mayWriteToMemory() &&
        !isa<DbgInfoIntrinsic>(BBI))
      return false;

    Value *AccessedPtr;
    unsigned AccessedAlign;
    if (LoadInst *LI = dyn_cast<LoadInst>(BBI)) {
      AccessedPtr = LI->getPointerOperand();
      AccessedAlign = LI->getAlignment();
    } else if (StoreInst *SI = dyn_cast<StoreInst>(BBI)) {
      AccessedPtr = SI->getPointerOperand();
      AccessedAlign = SI->getAlignment();
    } else
      continue;

    Type *AccessedTy = AccessedPtr->getType()->getPointerElementType();
    if (AccessedAlign == 0)
      AccessedAlign = DL.getABITypeAlignment(AccessedTy);
    if (AccessedAlign < Align)
      continue;

    // Handle trivial cases.
    if (AccessedPtr == V)
      return true;

    if (AreEquivalentAddressValues(AccessedPtr->stripPointerCasts(), V) &&
        LoadSize <= DL.getTypeStoreSize(AccessedTy))
      return true;
  }
  return false;
}

/// DefMaxInstsToScan - the default number of maximum instructions
/// to scan in the block, used by FindAvailableLoadedValue().
/// FindAvailableLoadedValue() was introduced in r60148, to improve jump
/// threading in part by eliminating partially redundant loads.
/// At that point, the value of MaxInstsToScan was already set to '6'
/// without documented explanation.
cl::opt<unsigned>
llvm::DefMaxInstsToScan("available-load-scan-limit", cl::init(6), cl::Hidden,
  cl::desc("Use this to specify the default maximum number of instructions "
           "to scan backward from a given instruction, when searching for "
           "available loaded value"));

Value *llvm::FindAvailableLoadedValue(LoadInst *Load,
                                      BasicBlock *ScanBB,
                                      BasicBlock::iterator &ScanFrom,
                                      unsigned MaxInstsToScan,
                                      AliasAnalysis *AA, bool *IsLoad,
                                      unsigned *NumScanedInst) {
  // Don't CSE load that is volatile or anything stronger than unordered.
  if (!Load->isUnordered())
    return nullptr;

  return FindAvailablePtrLoadStore(
      Load->getPointerOperand(), Load->getType(), Load->isAtomic(), ScanBB,
      ScanFrom, MaxInstsToScan, AA, IsLoad, NumScanedInst);
}

Value *llvm::FindAvailablePtrLoadStore(Value *Ptr, Type *AccessTy,
                                       bool AtLeastAtomic, BasicBlock *ScanBB,
                                       BasicBlock::iterator &ScanFrom,
                                       unsigned MaxInstsToScan,
                                       AliasAnalysis *AA, bool *IsLoadCSE,
                                       unsigned *NumScanedInst) {
  if (MaxInstsToScan == 0)
    MaxInstsToScan = ~0U;

  const DataLayout &DL = ScanBB->getModule()->getDataLayout();

  // Try to get the store size for the type.
  auto AccessSize = LocationSize::precise(DL.getTypeStoreSize(AccessTy));

  Value *StrippedPtr = Ptr->stripPointerCasts();

  while (ScanFrom != ScanBB->begin()) {
    // We must ignore debug info directives when counting (otherwise they
    // would affect codegen).
    Instruction *Inst = &*--ScanFrom;
    if (isa<DbgInfoIntrinsic>(Inst))
      continue;

    // Restore ScanFrom to expected value in case next test succeeds
    ScanFrom++;

    if (NumScanedInst)
      ++(*NumScanedInst);

    // Don't scan huge blocks.
    if (MaxInstsToScan-- == 0)
      return nullptr;

    --ScanFrom;
    // If this is a load of Ptr, the loaded value is available.
    // (This is true even if the load is volatile or atomic, although
    // those cases are unlikely.)
    if (LoadInst *LI = dyn_cast<LoadInst>(Inst))
      if (AreEquivalentAddressValues(
              LI->getPointerOperand()->stripPointerCasts(), StrippedPtr) &&
          CastInst::isBitOrNoopPointerCastable(LI->getType(), AccessTy, DL)) {

        // We can value forward from an atomic to a non-atomic, but not the
        // other way around.
        if (LI->isAtomic() < AtLeastAtomic)
          return nullptr;

        if (IsLoadCSE)
            *IsLoadCSE = true;
        return LI;
      }

    if (StoreInst *SI = dyn_cast<StoreInst>(Inst)) {
      Value *StorePtr = SI->getPointerOperand()->stripPointerCasts();
      // If this is a store through Ptr, the value is available!
      // (This is true even if the store is volatile or atomic, although
      // those cases are unlikely.)
      if (AreEquivalentAddressValues(StorePtr, StrippedPtr) &&
          CastInst::isBitOrNoopPointerCastable(SI->getValueOperand()->getType(),
                                               AccessTy, DL)) {

        // We can value forward from an atomic to a non-atomic, but not the
        // other way around.
        if (SI->isAtomic() < AtLeastAtomic)
          return nullptr;

        if (IsLoadCSE)
          *IsLoadCSE = false;
        return SI->getOperand(0);
      }

      // If both StrippedPtr and StorePtr reach all the way to an alloca or
      // global and they are different, ignore the store. This is a trivial form
      // of alias analysis that is important for reg2mem'd code.
      if ((isa<AllocaInst>(StrippedPtr) || isa<GlobalVariable>(StrippedPtr)) &&
          (isa<AllocaInst>(StorePtr) || isa<GlobalVariable>(StorePtr)) &&
          StrippedPtr != StorePtr)
        continue;

      // If we have alias analysis and it says the store won't modify the loaded
      // value, ignore the store.
      if (AA && !isModSet(AA->getModRefInfo(SI, StrippedPtr, AccessSize)))
        continue;

      // Otherwise the store that may or may not alias the pointer, bail out.
      ++ScanFrom;
      return nullptr;
    }

    // If this is some other instruction that may clobber Ptr, bail out.
    if (Inst->mayWriteToMemory()) {
      // If alias analysis claims that it really won't modify the load,
      // ignore it.
      if (AA && !isModSet(AA->getModRefInfo(Inst, StrippedPtr, AccessSize)))
        continue;

      // May modify the pointer, bail out.
      ++ScanFrom;
      return nullptr;
    }
  }

  // Got to the start of the block, we didn't find it, but are done for this
  // block.
  return nullptr;
}
