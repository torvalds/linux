//===- MemCpyOptimizer.cpp - Optimize use of memcpy and friends -----------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This pass performs various transformations related to eliminating memcpy
// calls, or transforming sets of stores into memset's.
//
//===----------------------------------------------------------------------===//

#include "llvm/Transforms/Scalar/MemCpyOptimizer.h"
#include "llvm/ADT/DenseSet.h"
#include "llvm/ADT/None.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/ADT/iterator_range.h"
#include "llvm/Analysis/AliasAnalysis.h"
#include "llvm/Analysis/AssumptionCache.h"
#include "llvm/Analysis/GlobalsModRef.h"
#include "llvm/Analysis/MemoryDependenceAnalysis.h"
#include "llvm/Analysis/MemoryLocation.h"
#include "llvm/Analysis/TargetLibraryInfo.h"
#include "llvm/Transforms/Utils/Local.h"
#include "llvm/Analysis/ValueTracking.h"
#include "llvm/IR/Argument.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/CallSite.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/Dominators.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/GetElementPtrTypeIterator.h"
#include "llvm/IR/GlobalVariable.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/InstrTypes.h"
#include "llvm/IR/Instruction.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/IntrinsicInst.h"
#include "llvm/IR/Intrinsics.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Operator.h"
#include "llvm/IR/PassManager.h"
#include "llvm/IR/Type.h"
#include "llvm/IR/User.h"
#include "llvm/IR/Value.h"
#include "llvm/Pass.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/MathExtras.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Transforms/Scalar.h"
#include <algorithm>
#include <cassert>
#include <cstdint>
#include <utility>

using namespace llvm;

#define DEBUG_TYPE "memcpyopt"

STATISTIC(NumMemCpyInstr, "Number of memcpy instructions deleted");
STATISTIC(NumMemSetInfer, "Number of memsets inferred");
STATISTIC(NumMoveToCpy,   "Number of memmoves converted to memcpy");
STATISTIC(NumCpyToSet,    "Number of memcpys converted to memset");

static int64_t GetOffsetFromIndex(const GEPOperator *GEP, unsigned Idx,
                                  bool &VariableIdxFound,
                                  const DataLayout &DL) {
  // Skip over the first indices.
  gep_type_iterator GTI = gep_type_begin(GEP);
  for (unsigned i = 1; i != Idx; ++i, ++GTI)
    /*skip along*/;

  // Compute the offset implied by the rest of the indices.
  int64_t Offset = 0;
  for (unsigned i = Idx, e = GEP->getNumOperands(); i != e; ++i, ++GTI) {
    ConstantInt *OpC = dyn_cast<ConstantInt>(GEP->getOperand(i));
    if (!OpC)
      return VariableIdxFound = true;
    if (OpC->isZero()) continue;  // No offset.

    // Handle struct indices, which add their field offset to the pointer.
    if (StructType *STy = GTI.getStructTypeOrNull()) {
      Offset += DL.getStructLayout(STy)->getElementOffset(OpC->getZExtValue());
      continue;
    }

    // Otherwise, we have a sequential type like an array or vector.  Multiply
    // the index by the ElementSize.
    uint64_t Size = DL.getTypeAllocSize(GTI.getIndexedType());
    Offset += Size*OpC->getSExtValue();
  }

  return Offset;
}

/// Return true if Ptr1 is provably equal to Ptr2 plus a constant offset, and
/// return that constant offset. For example, Ptr1 might be &A[42], and Ptr2
/// might be &A[40]. In this case offset would be -8.
static bool IsPointerOffset(Value *Ptr1, Value *Ptr2, int64_t &Offset,
                            const DataLayout &DL) {
  Ptr1 = Ptr1->stripPointerCasts();
  Ptr2 = Ptr2->stripPointerCasts();

  // Handle the trivial case first.
  if (Ptr1 == Ptr2) {
    Offset = 0;
    return true;
  }

  GEPOperator *GEP1 = dyn_cast<GEPOperator>(Ptr1);
  GEPOperator *GEP2 = dyn_cast<GEPOperator>(Ptr2);

  bool VariableIdxFound = false;

  // If one pointer is a GEP and the other isn't, then see if the GEP is a
  // constant offset from the base, as in "P" and "gep P, 1".
  if (GEP1 && !GEP2 && GEP1->getOperand(0)->stripPointerCasts() == Ptr2) {
    Offset = -GetOffsetFromIndex(GEP1, 1, VariableIdxFound, DL);
    return !VariableIdxFound;
  }

  if (GEP2 && !GEP1 && GEP2->getOperand(0)->stripPointerCasts() == Ptr1) {
    Offset = GetOffsetFromIndex(GEP2, 1, VariableIdxFound, DL);
    return !VariableIdxFound;
  }

  // Right now we handle the case when Ptr1/Ptr2 are both GEPs with an identical
  // base.  After that base, they may have some number of common (and
  // potentially variable) indices.  After that they handle some constant
  // offset, which determines their offset from each other.  At this point, we
  // handle no other case.
  if (!GEP1 || !GEP2 || GEP1->getOperand(0) != GEP2->getOperand(0))
    return false;

  // Skip any common indices and track the GEP types.
  unsigned Idx = 1;
  for (; Idx != GEP1->getNumOperands() && Idx != GEP2->getNumOperands(); ++Idx)
    if (GEP1->getOperand(Idx) != GEP2->getOperand(Idx))
      break;

  int64_t Offset1 = GetOffsetFromIndex(GEP1, Idx, VariableIdxFound, DL);
  int64_t Offset2 = GetOffsetFromIndex(GEP2, Idx, VariableIdxFound, DL);
  if (VariableIdxFound) return false;

  Offset = Offset2-Offset1;
  return true;
}

namespace {

/// Represents a range of memset'd bytes with the ByteVal value.
/// This allows us to analyze stores like:
///   store 0 -> P+1
///   store 0 -> P+0
///   store 0 -> P+3
///   store 0 -> P+2
/// which sometimes happens with stores to arrays of structs etc.  When we see
/// the first store, we make a range [1, 2).  The second store extends the range
/// to [0, 2).  The third makes a new range [2, 3).  The fourth store joins the
/// two ranges into [0, 3) which is memset'able.
struct MemsetRange {
  // Start/End - A semi range that describes the span that this range covers.
  // The range is closed at the start and open at the end: [Start, End).
  int64_t Start, End;

  /// StartPtr - The getelementptr instruction that points to the start of the
  /// range.
  Value *StartPtr;

  /// Alignment - The known alignment of the first store.
  unsigned Alignment;

  /// TheStores - The actual stores that make up this range.
  SmallVector<Instruction*, 16> TheStores;

  bool isProfitableToUseMemset(const DataLayout &DL) const;
};

} // end anonymous namespace

bool MemsetRange::isProfitableToUseMemset(const DataLayout &DL) const {
  // If we found more than 4 stores to merge or 16 bytes, use memset.
  if (TheStores.size() >= 4 || End-Start >= 16) return true;

  // If there is nothing to merge, don't do anything.
  if (TheStores.size() < 2) return false;

  // If any of the stores are a memset, then it is always good to extend the
  // memset.
  for (Instruction *SI : TheStores)
    if (!isa<StoreInst>(SI))
      return true;

  // Assume that the code generator is capable of merging pairs of stores
  // together if it wants to.
  if (TheStores.size() == 2) return false;

  // If we have fewer than 8 stores, it can still be worthwhile to do this.
  // For example, merging 4 i8 stores into an i32 store is useful almost always.
  // However, merging 2 32-bit stores isn't useful on a 32-bit architecture (the
  // memset will be split into 2 32-bit stores anyway) and doing so can
  // pessimize the llvm optimizer.
  //
  // Since we don't have perfect knowledge here, make some assumptions: assume
  // the maximum GPR width is the same size as the largest legal integer
  // size. If so, check to see whether we will end up actually reducing the
  // number of stores used.
  unsigned Bytes = unsigned(End-Start);
  unsigned MaxIntSize = DL.getLargestLegalIntTypeSizeInBits() / 8;
  if (MaxIntSize == 0)
    MaxIntSize = 1;
  unsigned NumPointerStores = Bytes / MaxIntSize;

  // Assume the remaining bytes if any are done a byte at a time.
  unsigned NumByteStores = Bytes % MaxIntSize;

  // If we will reduce the # stores (according to this heuristic), do the
  // transformation.  This encourages merging 4 x i8 -> i32 and 2 x i16 -> i32
  // etc.
  return TheStores.size() > NumPointerStores+NumByteStores;
}

namespace {

class MemsetRanges {
  using range_iterator = SmallVectorImpl<MemsetRange>::iterator;

  /// A sorted list of the memset ranges.
  SmallVector<MemsetRange, 8> Ranges;

  const DataLayout &DL;

public:
  MemsetRanges(const DataLayout &DL) : DL(DL) {}

  using const_iterator = SmallVectorImpl<MemsetRange>::const_iterator;

  const_iterator begin() const { return Ranges.begin(); }
  const_iterator end() const { return Ranges.end(); }
  bool empty() const { return Ranges.empty(); }

  void addInst(int64_t OffsetFromFirst, Instruction *Inst) {
    if (StoreInst *SI = dyn_cast<StoreInst>(Inst))
      addStore(OffsetFromFirst, SI);
    else
      addMemSet(OffsetFromFirst, cast<MemSetInst>(Inst));
  }

  void addStore(int64_t OffsetFromFirst, StoreInst *SI) {
    int64_t StoreSize = DL.getTypeStoreSize(SI->getOperand(0)->getType());

    addRange(OffsetFromFirst, StoreSize,
             SI->getPointerOperand(), SI->getAlignment(), SI);
  }

  void addMemSet(int64_t OffsetFromFirst, MemSetInst *MSI) {
    int64_t Size = cast<ConstantInt>(MSI->getLength())->getZExtValue();
    addRange(OffsetFromFirst, Size, MSI->getDest(), MSI->getDestAlignment(), MSI);
  }

  void addRange(int64_t Start, int64_t Size, Value *Ptr,
                unsigned Alignment, Instruction *Inst);
};

} // end anonymous namespace

/// Add a new store to the MemsetRanges data structure.  This adds a
/// new range for the specified store at the specified offset, merging into
/// existing ranges as appropriate.
void MemsetRanges::addRange(int64_t Start, int64_t Size, Value *Ptr,
                            unsigned Alignment, Instruction *Inst) {
  int64_t End = Start+Size;

  range_iterator I = std::lower_bound(Ranges.begin(), Ranges.end(), Start,
    [](const MemsetRange &LHS, int64_t RHS) { return LHS.End < RHS; });

  // We now know that I == E, in which case we didn't find anything to merge
  // with, or that Start <= I->End.  If End < I->Start or I == E, then we need
  // to insert a new range.  Handle this now.
  if (I == Ranges.end() || End < I->Start) {
    MemsetRange &R = *Ranges.insert(I, MemsetRange());
    R.Start        = Start;
    R.End          = End;
    R.StartPtr     = Ptr;
    R.Alignment    = Alignment;
    R.TheStores.push_back(Inst);
    return;
  }

  // This store overlaps with I, add it.
  I->TheStores.push_back(Inst);

  // At this point, we may have an interval that completely contains our store.
  // If so, just add it to the interval and return.
  if (I->Start <= Start && I->End >= End)
    return;

  // Now we know that Start <= I->End and End >= I->Start so the range overlaps
  // but is not entirely contained within the range.

  // See if the range extends the start of the range.  In this case, it couldn't
  // possibly cause it to join the prior range, because otherwise we would have
  // stopped on *it*.
  if (Start < I->Start) {
    I->Start = Start;
    I->StartPtr = Ptr;
    I->Alignment = Alignment;
  }

  // Now we know that Start <= I->End and Start >= I->Start (so the startpoint
  // is in or right at the end of I), and that End >= I->Start.  Extend I out to
  // End.
  if (End > I->End) {
    I->End = End;
    range_iterator NextI = I;
    while (++NextI != Ranges.end() && End >= NextI->Start) {
      // Merge the range in.
      I->TheStores.append(NextI->TheStores.begin(), NextI->TheStores.end());
      if (NextI->End > I->End)
        I->End = NextI->End;
      Ranges.erase(NextI);
      NextI = I;
    }
  }
}

//===----------------------------------------------------------------------===//
//                         MemCpyOptLegacyPass Pass
//===----------------------------------------------------------------------===//

namespace {

class MemCpyOptLegacyPass : public FunctionPass {
  MemCpyOptPass Impl;

public:
  static char ID; // Pass identification, replacement for typeid

  MemCpyOptLegacyPass() : FunctionPass(ID) {
    initializeMemCpyOptLegacyPassPass(*PassRegistry::getPassRegistry());
  }

  bool runOnFunction(Function &F) override;

private:
  // This transformation requires dominator postdominator info
  void getAnalysisUsage(AnalysisUsage &AU) const override {
    AU.setPreservesCFG();
    AU.addRequired<AssumptionCacheTracker>();
    AU.addRequired<DominatorTreeWrapperPass>();
    AU.addRequired<MemoryDependenceWrapperPass>();
    AU.addRequired<AAResultsWrapperPass>();
    AU.addRequired<TargetLibraryInfoWrapperPass>();
    AU.addPreserved<GlobalsAAWrapperPass>();
    AU.addPreserved<MemoryDependenceWrapperPass>();
  }
};

} // end anonymous namespace

char MemCpyOptLegacyPass::ID = 0;

/// The public interface to this file...
FunctionPass *llvm::createMemCpyOptPass() { return new MemCpyOptLegacyPass(); }

INITIALIZE_PASS_BEGIN(MemCpyOptLegacyPass, "memcpyopt", "MemCpy Optimization",
                      false, false)
INITIALIZE_PASS_DEPENDENCY(AssumptionCacheTracker)
INITIALIZE_PASS_DEPENDENCY(DominatorTreeWrapperPass)
INITIALIZE_PASS_DEPENDENCY(MemoryDependenceWrapperPass)
INITIALIZE_PASS_DEPENDENCY(TargetLibraryInfoWrapperPass)
INITIALIZE_PASS_DEPENDENCY(AAResultsWrapperPass)
INITIALIZE_PASS_DEPENDENCY(GlobalsAAWrapperPass)
INITIALIZE_PASS_END(MemCpyOptLegacyPass, "memcpyopt", "MemCpy Optimization",
                    false, false)

/// When scanning forward over instructions, we look for some other patterns to
/// fold away. In particular, this looks for stores to neighboring locations of
/// memory. If it sees enough consecutive ones, it attempts to merge them
/// together into a memcpy/memset.
Instruction *MemCpyOptPass::tryMergingIntoMemset(Instruction *StartInst,
                                                 Value *StartPtr,
                                                 Value *ByteVal) {
  const DataLayout &DL = StartInst->getModule()->getDataLayout();

  // Okay, so we now have a single store that can be splatable.  Scan to find
  // all subsequent stores of the same value to offset from the same pointer.
  // Join these together into ranges, so we can decide whether contiguous blocks
  // are stored.
  MemsetRanges Ranges(DL);

  BasicBlock::iterator BI(StartInst);
  for (++BI; !BI->isTerminator(); ++BI) {
    if (!isa<StoreInst>(BI) && !isa<MemSetInst>(BI)) {
      // If the instruction is readnone, ignore it, otherwise bail out.  We
      // don't even allow readonly here because we don't want something like:
      // A[1] = 2; strlen(A); A[2] = 2; -> memcpy(A, ...); strlen(A).
      if (BI->mayWriteToMemory() || BI->mayReadFromMemory())
        break;
      continue;
    }

    if (StoreInst *NextStore = dyn_cast<StoreInst>(BI)) {
      // If this is a store, see if we can merge it in.
      if (!NextStore->isSimple()) break;

      // Check to see if this stored value is of the same byte-splattable value.
      Value *StoredByte = isBytewiseValue(NextStore->getOperand(0));
      if (isa<UndefValue>(ByteVal) && StoredByte)
        ByteVal = StoredByte;
      if (ByteVal != StoredByte)
        break;

      // Check to see if this store is to a constant offset from the start ptr.
      int64_t Offset;
      if (!IsPointerOffset(StartPtr, NextStore->getPointerOperand(), Offset,
                           DL))
        break;

      Ranges.addStore(Offset, NextStore);
    } else {
      MemSetInst *MSI = cast<MemSetInst>(BI);

      if (MSI->isVolatile() || ByteVal != MSI->getValue() ||
          !isa<ConstantInt>(MSI->getLength()))
        break;

      // Check to see if this store is to a constant offset from the start ptr.
      int64_t Offset;
      if (!IsPointerOffset(StartPtr, MSI->getDest(), Offset, DL))
        break;

      Ranges.addMemSet(Offset, MSI);
    }
  }

  // If we have no ranges, then we just had a single store with nothing that
  // could be merged in.  This is a very common case of course.
  if (Ranges.empty())
    return nullptr;

  // If we had at least one store that could be merged in, add the starting
  // store as well.  We try to avoid this unless there is at least something
  // interesting as a small compile-time optimization.
  Ranges.addInst(0, StartInst);

  // If we create any memsets, we put it right before the first instruction that
  // isn't part of the memset block.  This ensure that the memset is dominated
  // by any addressing instruction needed by the start of the block.
  IRBuilder<> Builder(&*BI);

  // Now that we have full information about ranges, loop over the ranges and
  // emit memset's for anything big enough to be worthwhile.
  Instruction *AMemSet = nullptr;
  for (const MemsetRange &Range : Ranges) {
    if (Range.TheStores.size() == 1) continue;

    // If it is profitable to lower this range to memset, do so now.
    if (!Range.isProfitableToUseMemset(DL))
      continue;

    // Otherwise, we do want to transform this!  Create a new memset.
    // Get the starting pointer of the block.
    StartPtr = Range.StartPtr;

    // Determine alignment
    unsigned Alignment = Range.Alignment;
    if (Alignment == 0) {
      Type *EltType =
        cast<PointerType>(StartPtr->getType())->getElementType();
      Alignment = DL.getABITypeAlignment(EltType);
    }

    AMemSet =
      Builder.CreateMemSet(StartPtr, ByteVal, Range.End-Range.Start, Alignment);

    LLVM_DEBUG(dbgs() << "Replace stores:\n"; for (Instruction *SI
                                                   : Range.TheStores) dbgs()
                                              << *SI << '\n';
               dbgs() << "With: " << *AMemSet << '\n');

    if (!Range.TheStores.empty())
      AMemSet->setDebugLoc(Range.TheStores[0]->getDebugLoc());

    // Zap all the stores.
    for (Instruction *SI : Range.TheStores) {
      MD->removeInstruction(SI);
      SI->eraseFromParent();
    }
    ++NumMemSetInfer;
  }

  return AMemSet;
}

static unsigned findStoreAlignment(const DataLayout &DL, const StoreInst *SI) {
  unsigned StoreAlign = SI->getAlignment();
  if (!StoreAlign)
    StoreAlign = DL.getABITypeAlignment(SI->getOperand(0)->getType());
  return StoreAlign;
}

static unsigned findLoadAlignment(const DataLayout &DL, const LoadInst *LI) {
  unsigned LoadAlign = LI->getAlignment();
  if (!LoadAlign)
    LoadAlign = DL.getABITypeAlignment(LI->getType());
  return LoadAlign;
}

static unsigned findCommonAlignment(const DataLayout &DL, const StoreInst *SI,
                                     const LoadInst *LI) {
  unsigned StoreAlign = findStoreAlignment(DL, SI);
  unsigned LoadAlign = findLoadAlignment(DL, LI);
  return MinAlign(StoreAlign, LoadAlign);
}

// This method try to lift a store instruction before position P.
// It will lift the store and its argument + that anything that
// may alias with these.
// The method returns true if it was successful.
static bool moveUp(AliasAnalysis &AA, StoreInst *SI, Instruction *P,
                   const LoadInst *LI) {
  // If the store alias this position, early bail out.
  MemoryLocation StoreLoc = MemoryLocation::get(SI);
  if (isModOrRefSet(AA.getModRefInfo(P, StoreLoc)))
    return false;

  // Keep track of the arguments of all instruction we plan to lift
  // so we can make sure to lift them as well if appropriate.
  DenseSet<Instruction*> Args;
  if (auto *Ptr = dyn_cast<Instruction>(SI->getPointerOperand()))
    if (Ptr->getParent() == SI->getParent())
      Args.insert(Ptr);

  // Instruction to lift before P.
  SmallVector<Instruction*, 8> ToLift;

  // Memory locations of lifted instructions.
  SmallVector<MemoryLocation, 8> MemLocs{StoreLoc};

  // Lifted calls.
  SmallVector<const CallBase *, 8> Calls;

  const MemoryLocation LoadLoc = MemoryLocation::get(LI);

  for (auto I = --SI->getIterator(), E = P->getIterator(); I != E; --I) {
    auto *C = &*I;

    bool MayAlias = isModOrRefSet(AA.getModRefInfo(C, None));

    bool NeedLift = false;
    if (Args.erase(C))
      NeedLift = true;
    else if (MayAlias) {
      NeedLift = llvm::any_of(MemLocs, [C, &AA](const MemoryLocation &ML) {
        return isModOrRefSet(AA.getModRefInfo(C, ML));
      });

      if (!NeedLift)
        NeedLift = llvm::any_of(Calls, [C, &AA](const CallBase *Call) {
          return isModOrRefSet(AA.getModRefInfo(C, Call));
        });
    }

    if (!NeedLift)
      continue;

    if (MayAlias) {
      // Since LI is implicitly moved downwards past the lifted instructions,
      // none of them may modify its source.
      if (isModSet(AA.getModRefInfo(C, LoadLoc)))
        return false;
      else if (const auto *Call = dyn_cast<CallBase>(C)) {
        // If we can't lift this before P, it's game over.
        if (isModOrRefSet(AA.getModRefInfo(P, Call)))
          return false;

        Calls.push_back(Call);
      } else if (isa<LoadInst>(C) || isa<StoreInst>(C) || isa<VAArgInst>(C)) {
        // If we can't lift this before P, it's game over.
        auto ML = MemoryLocation::get(C);
        if (isModOrRefSet(AA.getModRefInfo(P, ML)))
          return false;

        MemLocs.push_back(ML);
      } else
        // We don't know how to lift this instruction.
        return false;
    }

    ToLift.push_back(C);
    for (unsigned k = 0, e = C->getNumOperands(); k != e; ++k)
      if (auto *A = dyn_cast<Instruction>(C->getOperand(k)))
        if (A->getParent() == SI->getParent())
          Args.insert(A);
  }

  // We made it, we need to lift
  for (auto *I : llvm::reverse(ToLift)) {
    LLVM_DEBUG(dbgs() << "Lifting " << *I << " before " << *P << "\n");
    I->moveBefore(P);
  }

  return true;
}

bool MemCpyOptPass::processStore(StoreInst *SI, BasicBlock::iterator &BBI) {
  if (!SI->isSimple()) return false;

  // Avoid merging nontemporal stores since the resulting
  // memcpy/memset would not be able to preserve the nontemporal hint.
  // In theory we could teach how to propagate the !nontemporal metadata to
  // memset calls. However, that change would force the backend to
  // conservatively expand !nontemporal memset calls back to sequences of
  // store instructions (effectively undoing the merging).
  if (SI->getMetadata(LLVMContext::MD_nontemporal))
    return false;

  const DataLayout &DL = SI->getModule()->getDataLayout();

  // Load to store forwarding can be interpreted as memcpy.
  if (LoadInst *LI = dyn_cast<LoadInst>(SI->getOperand(0))) {
    if (LI->isSimple() && LI->hasOneUse() &&
        LI->getParent() == SI->getParent()) {

      auto *T = LI->getType();
      if (T->isAggregateType()) {
        AliasAnalysis &AA = LookupAliasAnalysis();
        MemoryLocation LoadLoc = MemoryLocation::get(LI);

        // We use alias analysis to check if an instruction may store to
        // the memory we load from in between the load and the store. If
        // such an instruction is found, we try to promote there instead
        // of at the store position.
        Instruction *P = SI;
        for (auto &I : make_range(++LI->getIterator(), SI->getIterator())) {
          if (isModSet(AA.getModRefInfo(&I, LoadLoc))) {
            P = &I;
            break;
          }
        }

        // We found an instruction that may write to the loaded memory.
        // We can try to promote at this position instead of the store
        // position if nothing alias the store memory after this and the store
        // destination is not in the range.
        if (P && P != SI) {
          if (!moveUp(AA, SI, P, LI))
            P = nullptr;
        }

        // If a valid insertion position is found, then we can promote
        // the load/store pair to a memcpy.
        if (P) {
          // If we load from memory that may alias the memory we store to,
          // memmove must be used to preserve semantic. If not, memcpy can
          // be used.
          bool UseMemMove = false;
          if (!AA.isNoAlias(MemoryLocation::get(SI), LoadLoc))
            UseMemMove = true;

          uint64_t Size = DL.getTypeStoreSize(T);

          IRBuilder<> Builder(P);
          Instruction *M;
          if (UseMemMove)
            M = Builder.CreateMemMove(
                SI->getPointerOperand(), findStoreAlignment(DL, SI),
                LI->getPointerOperand(), findLoadAlignment(DL, LI), Size);
          else
            M = Builder.CreateMemCpy(
                SI->getPointerOperand(), findStoreAlignment(DL, SI),
                LI->getPointerOperand(), findLoadAlignment(DL, LI), Size);

          LLVM_DEBUG(dbgs() << "Promoting " << *LI << " to " << *SI << " => "
                            << *M << "\n");

          MD->removeInstruction(SI);
          SI->eraseFromParent();
          MD->removeInstruction(LI);
          LI->eraseFromParent();
          ++NumMemCpyInstr;

          // Make sure we do not invalidate the iterator.
          BBI = M->getIterator();
          return true;
        }
      }

      // Detect cases where we're performing call slot forwarding, but
      // happen to be using a load-store pair to implement it, rather than
      // a memcpy.
      MemDepResult ldep = MD->getDependency(LI);
      CallInst *C = nullptr;
      if (ldep.isClobber() && !isa<MemCpyInst>(ldep.getInst()))
        C = dyn_cast<CallInst>(ldep.getInst());

      if (C) {
        // Check that nothing touches the dest of the "copy" between
        // the call and the store.
        Value *CpyDest = SI->getPointerOperand()->stripPointerCasts();
        bool CpyDestIsLocal = isa<AllocaInst>(CpyDest);
        AliasAnalysis &AA = LookupAliasAnalysis();
        MemoryLocation StoreLoc = MemoryLocation::get(SI);
        for (BasicBlock::iterator I = --SI->getIterator(), E = C->getIterator();
             I != E; --I) {
          if (isModOrRefSet(AA.getModRefInfo(&*I, StoreLoc))) {
            C = nullptr;
            break;
          }
          // The store to dest may never happen if an exception can be thrown
          // between the load and the store.
          if (I->mayThrow() && !CpyDestIsLocal) {
            C = nullptr;
            break;
          }
        }
      }

      if (C) {
        bool changed = performCallSlotOptzn(
            LI, SI->getPointerOperand()->stripPointerCasts(),
            LI->getPointerOperand()->stripPointerCasts(),
            DL.getTypeStoreSize(SI->getOperand(0)->getType()),
            findCommonAlignment(DL, SI, LI), C);
        if (changed) {
          MD->removeInstruction(SI);
          SI->eraseFromParent();
          MD->removeInstruction(LI);
          LI->eraseFromParent();
          ++NumMemCpyInstr;
          return true;
        }
      }
    }
  }

  // There are two cases that are interesting for this code to handle: memcpy
  // and memset.  Right now we only handle memset.

  // Ensure that the value being stored is something that can be memset'able a
  // byte at a time like "0" or "-1" or any width, as well as things like
  // 0xA0A0A0A0 and 0.0.
  auto *V = SI->getOperand(0);
  if (Value *ByteVal = isBytewiseValue(V)) {
    if (Instruction *I = tryMergingIntoMemset(SI, SI->getPointerOperand(),
                                              ByteVal)) {
      BBI = I->getIterator(); // Don't invalidate iterator.
      return true;
    }

    // If we have an aggregate, we try to promote it to memset regardless
    // of opportunity for merging as it can expose optimization opportunities
    // in subsequent passes.
    auto *T = V->getType();
    if (T->isAggregateType()) {
      uint64_t Size = DL.getTypeStoreSize(T);
      unsigned Align = SI->getAlignment();
      if (!Align)
        Align = DL.getABITypeAlignment(T);
      IRBuilder<> Builder(SI);
      auto *M =
          Builder.CreateMemSet(SI->getPointerOperand(), ByteVal, Size, Align);

      LLVM_DEBUG(dbgs() << "Promoting " << *SI << " to " << *M << "\n");

      MD->removeInstruction(SI);
      SI->eraseFromParent();
      NumMemSetInfer++;

      // Make sure we do not invalidate the iterator.
      BBI = M->getIterator();
      return true;
    }
  }

  return false;
}

bool MemCpyOptPass::processMemSet(MemSetInst *MSI, BasicBlock::iterator &BBI) {
  // See if there is another memset or store neighboring this memset which
  // allows us to widen out the memset to do a single larger store.
  if (isa<ConstantInt>(MSI->getLength()) && !MSI->isVolatile())
    if (Instruction *I = tryMergingIntoMemset(MSI, MSI->getDest(),
                                              MSI->getValue())) {
      BBI = I->getIterator(); // Don't invalidate iterator.
      return true;
    }
  return false;
}

/// Takes a memcpy and a call that it depends on,
/// and checks for the possibility of a call slot optimization by having
/// the call write its result directly into the destination of the memcpy.
bool MemCpyOptPass::performCallSlotOptzn(Instruction *cpy, Value *cpyDest,
                                         Value *cpySrc, uint64_t cpyLen,
                                         unsigned cpyAlign, CallInst *C) {
  // The general transformation to keep in mind is
  //
  //   call @func(..., src, ...)
  //   memcpy(dest, src, ...)
  //
  // ->
  //
  //   memcpy(dest, src, ...)
  //   call @func(..., dest, ...)
  //
  // Since moving the memcpy is technically awkward, we additionally check that
  // src only holds uninitialized values at the moment of the call, meaning that
  // the memcpy can be discarded rather than moved.

  // Lifetime marks shouldn't be operated on.
  if (Function *F = C->getCalledFunction())
    if (F->isIntrinsic() && F->getIntrinsicID() == Intrinsic::lifetime_start)
      return false;

  // Deliberately get the source and destination with bitcasts stripped away,
  // because we'll need to do type comparisons based on the underlying type.
  CallSite CS(C);

  // Require that src be an alloca.  This simplifies the reasoning considerably.
  AllocaInst *srcAlloca = dyn_cast<AllocaInst>(cpySrc);
  if (!srcAlloca)
    return false;

  ConstantInt *srcArraySize = dyn_cast<ConstantInt>(srcAlloca->getArraySize());
  if (!srcArraySize)
    return false;

  const DataLayout &DL = cpy->getModule()->getDataLayout();
  uint64_t srcSize = DL.getTypeAllocSize(srcAlloca->getAllocatedType()) *
                     srcArraySize->getZExtValue();

  if (cpyLen < srcSize)
    return false;

  // Check that accessing the first srcSize bytes of dest will not cause a
  // trap.  Otherwise the transform is invalid since it might cause a trap
  // to occur earlier than it otherwise would.
  if (AllocaInst *A = dyn_cast<AllocaInst>(cpyDest)) {
    // The destination is an alloca.  Check it is larger than srcSize.
    ConstantInt *destArraySize = dyn_cast<ConstantInt>(A->getArraySize());
    if (!destArraySize)
      return false;

    uint64_t destSize = DL.getTypeAllocSize(A->getAllocatedType()) *
                        destArraySize->getZExtValue();

    if (destSize < srcSize)
      return false;
  } else if (Argument *A = dyn_cast<Argument>(cpyDest)) {
    // The store to dest may never happen if the call can throw.
    if (C->mayThrow())
      return false;

    if (A->getDereferenceableBytes() < srcSize) {
      // If the destination is an sret parameter then only accesses that are
      // outside of the returned struct type can trap.
      if (!A->hasStructRetAttr())
        return false;

      Type *StructTy = cast<PointerType>(A->getType())->getElementType();
      if (!StructTy->isSized()) {
        // The call may never return and hence the copy-instruction may never
        // be executed, and therefore it's not safe to say "the destination
        // has at least <cpyLen> bytes, as implied by the copy-instruction",
        return false;
      }

      uint64_t destSize = DL.getTypeAllocSize(StructTy);
      if (destSize < srcSize)
        return false;
    }
  } else {
    return false;
  }

  // Check that dest points to memory that is at least as aligned as src.
  unsigned srcAlign = srcAlloca->getAlignment();
  if (!srcAlign)
    srcAlign = DL.getABITypeAlignment(srcAlloca->getAllocatedType());
  bool isDestSufficientlyAligned = srcAlign <= cpyAlign;
  // If dest is not aligned enough and we can't increase its alignment then
  // bail out.
  if (!isDestSufficientlyAligned && !isa<AllocaInst>(cpyDest))
    return false;

  // Check that src is not accessed except via the call and the memcpy.  This
  // guarantees that it holds only undefined values when passed in (so the final
  // memcpy can be dropped), that it is not read or written between the call and
  // the memcpy, and that writing beyond the end of it is undefined.
  SmallVector<User*, 8> srcUseList(srcAlloca->user_begin(),
                                   srcAlloca->user_end());
  while (!srcUseList.empty()) {
    User *U = srcUseList.pop_back_val();

    if (isa<BitCastInst>(U) || isa<AddrSpaceCastInst>(U)) {
      for (User *UU : U->users())
        srcUseList.push_back(UU);
      continue;
    }
    if (GetElementPtrInst *G = dyn_cast<GetElementPtrInst>(U)) {
      if (!G->hasAllZeroIndices())
        return false;

      for (User *UU : U->users())
        srcUseList.push_back(UU);
      continue;
    }
    if (const IntrinsicInst *IT = dyn_cast<IntrinsicInst>(U))
      if (IT->isLifetimeStartOrEnd())
        continue;

    if (U != C && U != cpy)
      return false;
  }

  // Check that src isn't captured by the called function since the
  // transformation can cause aliasing issues in that case.
  for (unsigned i = 0, e = CS.arg_size(); i != e; ++i)
    if (CS.getArgument(i) == cpySrc && !CS.doesNotCapture(i))
      return false;

  // Since we're changing the parameter to the callsite, we need to make sure
  // that what would be the new parameter dominates the callsite.
  DominatorTree &DT = LookupDomTree();
  if (Instruction *cpyDestInst = dyn_cast<Instruction>(cpyDest))
    if (!DT.dominates(cpyDestInst, C))
      return false;

  // In addition to knowing that the call does not access src in some
  // unexpected manner, for example via a global, which we deduce from
  // the use analysis, we also need to know that it does not sneakily
  // access dest.  We rely on AA to figure this out for us.
  AliasAnalysis &AA = LookupAliasAnalysis();
  ModRefInfo MR = AA.getModRefInfo(C, cpyDest, LocationSize::precise(srcSize));
  // If necessary, perform additional analysis.
  if (isModOrRefSet(MR))
    MR = AA.callCapturesBefore(C, cpyDest, LocationSize::precise(srcSize), &DT);
  if (isModOrRefSet(MR))
    return false;

  // We can't create address space casts here because we don't know if they're
  // safe for the target.
  if (cpySrc->getType()->getPointerAddressSpace() !=
      cpyDest->getType()->getPointerAddressSpace())
    return false;
  for (unsigned i = 0; i < CS.arg_size(); ++i)
    if (CS.getArgument(i)->stripPointerCasts() == cpySrc &&
        cpySrc->getType()->getPointerAddressSpace() !=
        CS.getArgument(i)->getType()->getPointerAddressSpace())
      return false;

  // All the checks have passed, so do the transformation.
  bool changedArgument = false;
  for (unsigned i = 0; i < CS.arg_size(); ++i)
    if (CS.getArgument(i)->stripPointerCasts() == cpySrc) {
      Value *Dest = cpySrc->getType() == cpyDest->getType() ?  cpyDest
        : CastInst::CreatePointerCast(cpyDest, cpySrc->getType(),
                                      cpyDest->getName(), C);
      changedArgument = true;
      if (CS.getArgument(i)->getType() == Dest->getType())
        CS.setArgument(i, Dest);
      else
        CS.setArgument(i, CastInst::CreatePointerCast(Dest,
                          CS.getArgument(i)->getType(), Dest->getName(), C));
    }

  if (!changedArgument)
    return false;

  // If the destination wasn't sufficiently aligned then increase its alignment.
  if (!isDestSufficientlyAligned) {
    assert(isa<AllocaInst>(cpyDest) && "Can only increase alloca alignment!");
    cast<AllocaInst>(cpyDest)->setAlignment(srcAlign);
  }

  // Drop any cached information about the call, because we may have changed
  // its dependence information by changing its parameter.
  MD->removeInstruction(C);

  // Update AA metadata
  // FIXME: MD_tbaa_struct and MD_mem_parallel_loop_access should also be
  // handled here, but combineMetadata doesn't support them yet
  unsigned KnownIDs[] = {LLVMContext::MD_tbaa, LLVMContext::MD_alias_scope,
                         LLVMContext::MD_noalias,
                         LLVMContext::MD_invariant_group,
                         LLVMContext::MD_access_group};
  combineMetadata(C, cpy, KnownIDs, true);

  // Remove the memcpy.
  MD->removeInstruction(cpy);
  ++NumMemCpyInstr;

  return true;
}

/// We've found that the (upward scanning) memory dependence of memcpy 'M' is
/// the memcpy 'MDep'. Try to simplify M to copy from MDep's input if we can.
bool MemCpyOptPass::processMemCpyMemCpyDependence(MemCpyInst *M,
                                                  MemCpyInst *MDep) {
  // We can only transforms memcpy's where the dest of one is the source of the
  // other.
  if (M->getSource() != MDep->getDest() || MDep->isVolatile())
    return false;

  // If dep instruction is reading from our current input, then it is a noop
  // transfer and substituting the input won't change this instruction.  Just
  // ignore the input and let someone else zap MDep.  This handles cases like:
  //    memcpy(a <- a)
  //    memcpy(b <- a)
  if (M->getSource() == MDep->getSource())
    return false;

  // Second, the length of the memcpy's must be the same, or the preceding one
  // must be larger than the following one.
  ConstantInt *MDepLen = dyn_cast<ConstantInt>(MDep->getLength());
  ConstantInt *MLen = dyn_cast<ConstantInt>(M->getLength());
  if (!MDepLen || !MLen || MDepLen->getZExtValue() < MLen->getZExtValue())
    return false;

  AliasAnalysis &AA = LookupAliasAnalysis();

  // Verify that the copied-from memory doesn't change in between the two
  // transfers.  For example, in:
  //    memcpy(a <- b)
  //    *b = 42;
  //    memcpy(c <- a)
  // It would be invalid to transform the second memcpy into memcpy(c <- b).
  //
  // TODO: If the code between M and MDep is transparent to the destination "c",
  // then we could still perform the xform by moving M up to the first memcpy.
  //
  // NOTE: This is conservative, it will stop on any read from the source loc,
  // not just the defining memcpy.
  MemDepResult SourceDep =
      MD->getPointerDependencyFrom(MemoryLocation::getForSource(MDep), false,
                                   M->getIterator(), M->getParent());
  if (!SourceDep.isClobber() || SourceDep.getInst() != MDep)
    return false;

  // If the dest of the second might alias the source of the first, then the
  // source and dest might overlap.  We still want to eliminate the intermediate
  // value, but we have to generate a memmove instead of memcpy.
  bool UseMemMove = false;
  if (!AA.isNoAlias(MemoryLocation::getForDest(M),
                    MemoryLocation::getForSource(MDep)))
    UseMemMove = true;

  // If all checks passed, then we can transform M.
  LLVM_DEBUG(dbgs() << "MemCpyOptPass: Forwarding memcpy->memcpy src:\n"
                    << *MDep << '\n' << *M << '\n');

  // TODO: Is this worth it if we're creating a less aligned memcpy? For
  // example we could be moving from movaps -> movq on x86.
  IRBuilder<> Builder(M);
  if (UseMemMove)
    Builder.CreateMemMove(M->getRawDest(), M->getDestAlignment(),
                          MDep->getRawSource(), MDep->getSourceAlignment(),
                          M->getLength(), M->isVolatile());
  else
    Builder.CreateMemCpy(M->getRawDest(), M->getDestAlignment(),
                         MDep->getRawSource(), MDep->getSourceAlignment(),
                         M->getLength(), M->isVolatile());

  // Remove the instruction we're replacing.
  MD->removeInstruction(M);
  M->eraseFromParent();
  ++NumMemCpyInstr;
  return true;
}

/// We've found that the (upward scanning) memory dependence of \p MemCpy is
/// \p MemSet.  Try to simplify \p MemSet to only set the trailing bytes that
/// weren't copied over by \p MemCpy.
///
/// In other words, transform:
/// \code
///   memset(dst, c, dst_size);
///   memcpy(dst, src, src_size);
/// \endcode
/// into:
/// \code
///   memcpy(dst, src, src_size);
///   memset(dst + src_size, c, dst_size <= src_size ? 0 : dst_size - src_size);
/// \endcode
bool MemCpyOptPass::processMemSetMemCpyDependence(MemCpyInst *MemCpy,
                                                  MemSetInst *MemSet) {
  // We can only transform memset/memcpy with the same destination.
  if (MemSet->getDest() != MemCpy->getDest())
    return false;

  // Check that there are no other dependencies on the memset destination.
  MemDepResult DstDepInfo =
      MD->getPointerDependencyFrom(MemoryLocation::getForDest(MemSet), false,
                                   MemCpy->getIterator(), MemCpy->getParent());
  if (DstDepInfo.getInst() != MemSet)
    return false;

  // Use the same i8* dest as the memcpy, killing the memset dest if different.
  Value *Dest = MemCpy->getRawDest();
  Value *DestSize = MemSet->getLength();
  Value *SrcSize = MemCpy->getLength();

  // By default, create an unaligned memset.
  unsigned Align = 1;
  // If Dest is aligned, and SrcSize is constant, use the minimum alignment
  // of the sum.
  const unsigned DestAlign =
      std::max(MemSet->getDestAlignment(), MemCpy->getDestAlignment());
  if (DestAlign > 1)
    if (ConstantInt *SrcSizeC = dyn_cast<ConstantInt>(SrcSize))
      Align = MinAlign(SrcSizeC->getZExtValue(), DestAlign);

  IRBuilder<> Builder(MemCpy);

  // If the sizes have different types, zext the smaller one.
  if (DestSize->getType() != SrcSize->getType()) {
    if (DestSize->getType()->getIntegerBitWidth() >
        SrcSize->getType()->getIntegerBitWidth())
      SrcSize = Builder.CreateZExt(SrcSize, DestSize->getType());
    else
      DestSize = Builder.CreateZExt(DestSize, SrcSize->getType());
  }

  Value *Ule = Builder.CreateICmpULE(DestSize, SrcSize);
  Value *SizeDiff = Builder.CreateSub(DestSize, SrcSize);
  Value *MemsetLen = Builder.CreateSelect(
      Ule, ConstantInt::getNullValue(DestSize->getType()), SizeDiff);
  Builder.CreateMemSet(Builder.CreateGEP(Dest, SrcSize), MemSet->getOperand(1),
                       MemsetLen, Align);

  MD->removeInstruction(MemSet);
  MemSet->eraseFromParent();
  return true;
}

/// Determine whether the instruction has undefined content for the given Size,
/// either because it was freshly alloca'd or started its lifetime.
static bool hasUndefContents(Instruction *I, ConstantInt *Size) {
  if (isa<AllocaInst>(I))
    return true;

  if (IntrinsicInst *II = dyn_cast<IntrinsicInst>(I))
    if (II->getIntrinsicID() == Intrinsic::lifetime_start)
      if (ConstantInt *LTSize = dyn_cast<ConstantInt>(II->getArgOperand(0)))
        if (LTSize->getZExtValue() >= Size->getZExtValue())
          return true;

  return false;
}

/// Transform memcpy to memset when its source was just memset.
/// In other words, turn:
/// \code
///   memset(dst1, c, dst1_size);
///   memcpy(dst2, dst1, dst2_size);
/// \endcode
/// into:
/// \code
///   memset(dst1, c, dst1_size);
///   memset(dst2, c, dst2_size);
/// \endcode
/// When dst2_size <= dst1_size.
///
/// The \p MemCpy must have a Constant length.
bool MemCpyOptPass::performMemCpyToMemSetOptzn(MemCpyInst *MemCpy,
                                               MemSetInst *MemSet) {
  AliasAnalysis &AA = LookupAliasAnalysis();

  // Make sure that memcpy(..., memset(...), ...), that is we are memsetting and
  // memcpying from the same address. Otherwise it is hard to reason about.
  if (!AA.isMustAlias(MemSet->getRawDest(), MemCpy->getRawSource()))
    return false;

  // A known memset size is required.
  ConstantInt *MemSetSize = dyn_cast<ConstantInt>(MemSet->getLength());
  if (!MemSetSize)
    return false;

  // Make sure the memcpy doesn't read any more than what the memset wrote.
  // Don't worry about sizes larger than i64.
  ConstantInt *CopySize = cast<ConstantInt>(MemCpy->getLength());
  if (CopySize->getZExtValue() > MemSetSize->getZExtValue()) {
    // If the memcpy is larger than the memset, but the memory was undef prior
    // to the memset, we can just ignore the tail. Technically we're only
    // interested in the bytes from MemSetSize..CopySize here, but as we can't
    // easily represent this location, we use the full 0..CopySize range.
    MemoryLocation MemCpyLoc = MemoryLocation::getForSource(MemCpy);
    MemDepResult DepInfo = MD->getPointerDependencyFrom(
        MemCpyLoc, true, MemSet->getIterator(), MemSet->getParent());
    if (DepInfo.isDef() && hasUndefContents(DepInfo.getInst(), CopySize))
      CopySize = MemSetSize;
    else
      return false;
  }

  IRBuilder<> Builder(MemCpy);
  Builder.CreateMemSet(MemCpy->getRawDest(), MemSet->getOperand(1),
                       CopySize, MemCpy->getDestAlignment());
  return true;
}

/// Perform simplification of memcpy's.  If we have memcpy A
/// which copies X to Y, and memcpy B which copies Y to Z, then we can rewrite
/// B to be a memcpy from X to Z (or potentially a memmove, depending on
/// circumstances). This allows later passes to remove the first memcpy
/// altogether.
bool MemCpyOptPass::processMemCpy(MemCpyInst *M) {
  // We can only optimize non-volatile memcpy's.
  if (M->isVolatile()) return false;

  // If the source and destination of the memcpy are the same, then zap it.
  if (M->getSource() == M->getDest()) {
    MD->removeInstruction(M);
    M->eraseFromParent();
    return false;
  }

  // If copying from a constant, try to turn the memcpy into a memset.
  if (GlobalVariable *GV = dyn_cast<GlobalVariable>(M->getSource()))
    if (GV->isConstant() && GV->hasDefinitiveInitializer())
      if (Value *ByteVal = isBytewiseValue(GV->getInitializer())) {
        IRBuilder<> Builder(M);
        Builder.CreateMemSet(M->getRawDest(), ByteVal, M->getLength(),
                             M->getDestAlignment(), false);
        MD->removeInstruction(M);
        M->eraseFromParent();
        ++NumCpyToSet;
        return true;
      }

  MemDepResult DepInfo = MD->getDependency(M);

  // Try to turn a partially redundant memset + memcpy into
  // memcpy + smaller memset.  We don't need the memcpy size for this.
  if (DepInfo.isClobber())
    if (MemSetInst *MDep = dyn_cast<MemSetInst>(DepInfo.getInst()))
      if (processMemSetMemCpyDependence(M, MDep))
        return true;

  // The optimizations after this point require the memcpy size.
  ConstantInt *CopySize = dyn_cast<ConstantInt>(M->getLength());
  if (!CopySize) return false;

  // There are four possible optimizations we can do for memcpy:
  //   a) memcpy-memcpy xform which exposes redundance for DSE.
  //   b) call-memcpy xform for return slot optimization.
  //   c) memcpy from freshly alloca'd space or space that has just started its
  //      lifetime copies undefined data, and we can therefore eliminate the
  //      memcpy in favor of the data that was already at the destination.
  //   d) memcpy from a just-memset'd source can be turned into memset.
  if (DepInfo.isClobber()) {
    if (CallInst *C = dyn_cast<CallInst>(DepInfo.getInst())) {
      // FIXME: Can we pass in either of dest/src alignment here instead
      // of conservatively taking the minimum?
      unsigned Align = MinAlign(M->getDestAlignment(), M->getSourceAlignment());
      if (performCallSlotOptzn(M, M->getDest(), M->getSource(),
                               CopySize->getZExtValue(), Align,
                               C)) {
        MD->removeInstruction(M);
        M->eraseFromParent();
        return true;
      }
    }
  }

  MemoryLocation SrcLoc = MemoryLocation::getForSource(M);
  MemDepResult SrcDepInfo = MD->getPointerDependencyFrom(
      SrcLoc, true, M->getIterator(), M->getParent());

  if (SrcDepInfo.isClobber()) {
    if (MemCpyInst *MDep = dyn_cast<MemCpyInst>(SrcDepInfo.getInst()))
      return processMemCpyMemCpyDependence(M, MDep);
  } else if (SrcDepInfo.isDef()) {
    if (hasUndefContents(SrcDepInfo.getInst(), CopySize)) {
      MD->removeInstruction(M);
      M->eraseFromParent();
      ++NumMemCpyInstr;
      return true;
    }
  }

  if (SrcDepInfo.isClobber())
    if (MemSetInst *MDep = dyn_cast<MemSetInst>(SrcDepInfo.getInst()))
      if (performMemCpyToMemSetOptzn(M, MDep)) {
        MD->removeInstruction(M);
        M->eraseFromParent();
        ++NumCpyToSet;
        return true;
      }

  return false;
}

/// Transforms memmove calls to memcpy calls when the src/dst are guaranteed
/// not to alias.
bool MemCpyOptPass::processMemMove(MemMoveInst *M) {
  AliasAnalysis &AA = LookupAliasAnalysis();

  if (!TLI->has(LibFunc_memmove))
    return false;

  // See if the pointers alias.
  if (!AA.isNoAlias(MemoryLocation::getForDest(M),
                    MemoryLocation::getForSource(M)))
    return false;

  LLVM_DEBUG(dbgs() << "MemCpyOptPass: Optimizing memmove -> memcpy: " << *M
                    << "\n");

  // If not, then we know we can transform this.
  Type *ArgTys[3] = { M->getRawDest()->getType(),
                      M->getRawSource()->getType(),
                      M->getLength()->getType() };
  M->setCalledFunction(Intrinsic::getDeclaration(M->getModule(),
                                                 Intrinsic::memcpy, ArgTys));

  // MemDep may have over conservative information about this instruction, just
  // conservatively flush it from the cache.
  MD->removeInstruction(M);

  ++NumMoveToCpy;
  return true;
}

/// This is called on every byval argument in call sites.
bool MemCpyOptPass::processByValArgument(CallSite CS, unsigned ArgNo) {
  const DataLayout &DL = CS.getCaller()->getParent()->getDataLayout();
  // Find out what feeds this byval argument.
  Value *ByValArg = CS.getArgument(ArgNo);
  Type *ByValTy = cast<PointerType>(ByValArg->getType())->getElementType();
  uint64_t ByValSize = DL.getTypeAllocSize(ByValTy);
  MemDepResult DepInfo = MD->getPointerDependencyFrom(
      MemoryLocation(ByValArg, LocationSize::precise(ByValSize)), true,
      CS.getInstruction()->getIterator(), CS.getInstruction()->getParent());
  if (!DepInfo.isClobber())
    return false;

  // If the byval argument isn't fed by a memcpy, ignore it.  If it is fed by
  // a memcpy, see if we can byval from the source of the memcpy instead of the
  // result.
  MemCpyInst *MDep = dyn_cast<MemCpyInst>(DepInfo.getInst());
  if (!MDep || MDep->isVolatile() ||
      ByValArg->stripPointerCasts() != MDep->getDest())
    return false;

  // The length of the memcpy must be larger or equal to the size of the byval.
  ConstantInt *C1 = dyn_cast<ConstantInt>(MDep->getLength());
  if (!C1 || C1->getValue().getZExtValue() < ByValSize)
    return false;

  // Get the alignment of the byval.  If the call doesn't specify the alignment,
  // then it is some target specific value that we can't know.
  unsigned ByValAlign = CS.getParamAlignment(ArgNo);
  if (ByValAlign == 0) return false;

  // If it is greater than the memcpy, then we check to see if we can force the
  // source of the memcpy to the alignment we need.  If we fail, we bail out.
  AssumptionCache &AC = LookupAssumptionCache();
  DominatorTree &DT = LookupDomTree();
  if (MDep->getSourceAlignment() < ByValAlign &&
      getOrEnforceKnownAlignment(MDep->getSource(), ByValAlign, DL,
                                 CS.getInstruction(), &AC, &DT) < ByValAlign)
    return false;

  // The address space of the memcpy source must match the byval argument
  if (MDep->getSource()->getType()->getPointerAddressSpace() !=
      ByValArg->getType()->getPointerAddressSpace())
    return false;

  // Verify that the copied-from memory doesn't change in between the memcpy and
  // the byval call.
  //    memcpy(a <- b)
  //    *b = 42;
  //    foo(*a)
  // It would be invalid to transform the second memcpy into foo(*b).
  //
  // NOTE: This is conservative, it will stop on any read from the source loc,
  // not just the defining memcpy.
  MemDepResult SourceDep = MD->getPointerDependencyFrom(
      MemoryLocation::getForSource(MDep), false,
      CS.getInstruction()->getIterator(), MDep->getParent());
  if (!SourceDep.isClobber() || SourceDep.getInst() != MDep)
    return false;

  Value *TmpCast = MDep->getSource();
  if (MDep->getSource()->getType() != ByValArg->getType())
    TmpCast = new BitCastInst(MDep->getSource(), ByValArg->getType(),
                              "tmpcast", CS.getInstruction());

  LLVM_DEBUG(dbgs() << "MemCpyOptPass: Forwarding memcpy to byval:\n"
                    << "  " << *MDep << "\n"
                    << "  " << *CS.getInstruction() << "\n");

  // Otherwise we're good!  Update the byval argument.
  CS.setArgument(ArgNo, TmpCast);
  ++NumMemCpyInstr;
  return true;
}

/// Executes one iteration of MemCpyOptPass.
bool MemCpyOptPass::iterateOnFunction(Function &F) {
  bool MadeChange = false;

  DominatorTree &DT = LookupDomTree();

  // Walk all instruction in the function.
  for (BasicBlock &BB : F) {
    // Skip unreachable blocks. For example processStore assumes that an
    // instruction in a BB can't be dominated by a later instruction in the
    // same BB (which is a scenario that can happen for an unreachable BB that
    // has itself as a predecessor).
    if (!DT.isReachableFromEntry(&BB))
      continue;

    for (BasicBlock::iterator BI = BB.begin(), BE = BB.end(); BI != BE;) {
        // Avoid invalidating the iterator.
      Instruction *I = &*BI++;

      bool RepeatInstruction = false;

      if (StoreInst *SI = dyn_cast<StoreInst>(I))
        MadeChange |= processStore(SI, BI);
      else if (MemSetInst *M = dyn_cast<MemSetInst>(I))
        RepeatInstruction = processMemSet(M, BI);
      else if (MemCpyInst *M = dyn_cast<MemCpyInst>(I))
        RepeatInstruction = processMemCpy(M);
      else if (MemMoveInst *M = dyn_cast<MemMoveInst>(I))
        RepeatInstruction = processMemMove(M);
      else if (auto CS = CallSite(I)) {
        for (unsigned i = 0, e = CS.arg_size(); i != e; ++i)
          if (CS.isByValArgument(i))
            MadeChange |= processByValArgument(CS, i);
      }

      // Reprocess the instruction if desired.
      if (RepeatInstruction) {
        if (BI != BB.begin())
          --BI;
        MadeChange = true;
      }
    }
  }

  return MadeChange;
}

PreservedAnalyses MemCpyOptPass::run(Function &F, FunctionAnalysisManager &AM) {
  auto &MD = AM.getResult<MemoryDependenceAnalysis>(F);
  auto &TLI = AM.getResult<TargetLibraryAnalysis>(F);

  auto LookupAliasAnalysis = [&]() -> AliasAnalysis & {
    return AM.getResult<AAManager>(F);
  };
  auto LookupAssumptionCache = [&]() -> AssumptionCache & {
    return AM.getResult<AssumptionAnalysis>(F);
  };
  auto LookupDomTree = [&]() -> DominatorTree & {
    return AM.getResult<DominatorTreeAnalysis>(F);
  };

  bool MadeChange = runImpl(F, &MD, &TLI, LookupAliasAnalysis,
                            LookupAssumptionCache, LookupDomTree);
  if (!MadeChange)
    return PreservedAnalyses::all();

  PreservedAnalyses PA;
  PA.preserveSet<CFGAnalyses>();
  PA.preserve<GlobalsAA>();
  PA.preserve<MemoryDependenceAnalysis>();
  return PA;
}

bool MemCpyOptPass::runImpl(
    Function &F, MemoryDependenceResults *MD_, TargetLibraryInfo *TLI_,
    std::function<AliasAnalysis &()> LookupAliasAnalysis_,
    std::function<AssumptionCache &()> LookupAssumptionCache_,
    std::function<DominatorTree &()> LookupDomTree_) {
  bool MadeChange = false;
  MD = MD_;
  TLI = TLI_;
  LookupAliasAnalysis = std::move(LookupAliasAnalysis_);
  LookupAssumptionCache = std::move(LookupAssumptionCache_);
  LookupDomTree = std::move(LookupDomTree_);

  // If we don't have at least memset and memcpy, there is little point of doing
  // anything here.  These are required by a freestanding implementation, so if
  // even they are disabled, there is no point in trying hard.
  if (!TLI->has(LibFunc_memset) || !TLI->has(LibFunc_memcpy))
    return false;

  while (true) {
    if (!iterateOnFunction(F))
      break;
    MadeChange = true;
  }

  MD = nullptr;
  return MadeChange;
}

/// This is the main transformation entry point for a function.
bool MemCpyOptLegacyPass::runOnFunction(Function &F) {
  if (skipFunction(F))
    return false;

  auto *MD = &getAnalysis<MemoryDependenceWrapperPass>().getMemDep();
  auto *TLI = &getAnalysis<TargetLibraryInfoWrapperPass>().getTLI();

  auto LookupAliasAnalysis = [this]() -> AliasAnalysis & {
    return getAnalysis<AAResultsWrapperPass>().getAAResults();
  };
  auto LookupAssumptionCache = [this, &F]() -> AssumptionCache & {
    return getAnalysis<AssumptionCacheTracker>().getAssumptionCache(F);
  };
  auto LookupDomTree = [this]() -> DominatorTree & {
    return getAnalysis<DominatorTreeWrapperPass>().getDomTree();
  };

  return Impl.runImpl(F, MD, TLI, LookupAliasAnalysis, LookupAssumptionCache,
                      LookupDomTree);
}
