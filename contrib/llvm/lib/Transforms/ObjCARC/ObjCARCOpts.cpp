//===- ObjCARCOpts.cpp - ObjC ARC Optimization ----------------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
/// \file
/// This file defines ObjC ARC optimizations. ARC stands for Automatic
/// Reference Counting and is a system for managing reference counts for objects
/// in Objective C.
///
/// The optimizations performed include elimination of redundant, partially
/// redundant, and inconsequential reference count operations, elimination of
/// redundant weak pointer operations, and numerous minor simplifications.
///
/// WARNING: This file knows about certain library functions. It recognizes them
/// by name, and hardwires knowledge of their semantics.
///
/// WARNING: This file knows about how certain Objective-C library functions are
/// used. Naive LLVM IR transformations which would otherwise be
/// behavior-preserving may break these assumptions.
//
//===----------------------------------------------------------------------===//

#include "ARCRuntimeEntryPoints.h"
#include "BlotMapVector.h"
#include "DependencyAnalysis.h"
#include "ObjCARC.h"
#include "ProvenanceAnalysis.h"
#include "PtrState.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/None.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/Analysis/AliasAnalysis.h"
#include "llvm/Analysis/EHPersonalities.h"
#include "llvm/Analysis/ObjCARCAliasAnalysis.h"
#include "llvm/Analysis/ObjCARCAnalysisUtils.h"
#include "llvm/Analysis/ObjCARCInstKind.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/CFG.h"
#include "llvm/IR/CallSite.h"
#include "llvm/IR/Constant.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/GlobalVariable.h"
#include "llvm/IR/InstIterator.h"
#include "llvm/IR/InstrTypes.h"
#include "llvm/IR/Instruction.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Metadata.h"
#include "llvm/IR/Type.h"
#include "llvm/IR/User.h"
#include "llvm/IR/Value.h"
#include "llvm/Pass.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/raw_ostream.h"
#include <cassert>
#include <iterator>
#include <utility>

using namespace llvm;
using namespace llvm::objcarc;

#define DEBUG_TYPE "objc-arc-opts"

/// \defgroup ARCUtilities Utility declarations/definitions specific to ARC.
/// @{

/// This is similar to GetRCIdentityRoot but it stops as soon
/// as it finds a value with multiple uses.
static const Value *FindSingleUseIdentifiedObject(const Value *Arg) {
  // ConstantData (like ConstantPointerNull and UndefValue) is used across
  // modules.  It's never a single-use value.
  if (isa<ConstantData>(Arg))
    return nullptr;

  if (Arg->hasOneUse()) {
    if (const BitCastInst *BC = dyn_cast<BitCastInst>(Arg))
      return FindSingleUseIdentifiedObject(BC->getOperand(0));
    if (const GetElementPtrInst *GEP = dyn_cast<GetElementPtrInst>(Arg))
      if (GEP->hasAllZeroIndices())
        return FindSingleUseIdentifiedObject(GEP->getPointerOperand());
    if (IsForwarding(GetBasicARCInstKind(Arg)))
      return FindSingleUseIdentifiedObject(
               cast<CallInst>(Arg)->getArgOperand(0));
    if (!IsObjCIdentifiedObject(Arg))
      return nullptr;
    return Arg;
  }

  // If we found an identifiable object but it has multiple uses, but they are
  // trivial uses, we can still consider this to be a single-use value.
  if (IsObjCIdentifiedObject(Arg)) {
    for (const User *U : Arg->users())
      if (!U->use_empty() || GetRCIdentityRoot(U) != Arg)
         return nullptr;

    return Arg;
  }

  return nullptr;
}

/// @}
///
/// \defgroup ARCOpt ARC Optimization.
/// @{

// TODO: On code like this:
//
// objc_retain(%x)
// stuff_that_cannot_release()
// objc_autorelease(%x)
// stuff_that_cannot_release()
// objc_retain(%x)
// stuff_that_cannot_release()
// objc_autorelease(%x)
//
// The second retain and autorelease can be deleted.

// TODO: It should be possible to delete
// objc_autoreleasePoolPush and objc_autoreleasePoolPop
// pairs if nothing is actually autoreleased between them. Also, autorelease
// calls followed by objc_autoreleasePoolPop calls (perhaps in ObjC++ code
// after inlining) can be turned into plain release calls.

// TODO: Critical-edge splitting. If the optimial insertion point is
// a critical edge, the current algorithm has to fail, because it doesn't
// know how to split edges. It should be possible to make the optimizer
// think in terms of edges, rather than blocks, and then split critical
// edges on demand.

// TODO: OptimizeSequences could generalized to be Interprocedural.

// TODO: Recognize that a bunch of other objc runtime calls have
// non-escaping arguments and non-releasing arguments, and may be
// non-autoreleasing.

// TODO: Sink autorelease calls as far as possible. Unfortunately we
// usually can't sink them past other calls, which would be the main
// case where it would be useful.

// TODO: The pointer returned from objc_loadWeakRetained is retained.

// TODO: Delete release+retain pairs (rare).

STATISTIC(NumNoops,       "Number of no-op objc calls eliminated");
STATISTIC(NumPartialNoops, "Number of partially no-op objc calls eliminated");
STATISTIC(NumAutoreleases,"Number of autoreleases converted to releases");
STATISTIC(NumRets,        "Number of return value forwarding "
                          "retain+autoreleases eliminated");
STATISTIC(NumRRs,         "Number of retain+release paths eliminated");
STATISTIC(NumPeeps,       "Number of calls peephole-optimized");
#ifndef NDEBUG
STATISTIC(NumRetainsBeforeOpt,
          "Number of retains before optimization");
STATISTIC(NumReleasesBeforeOpt,
          "Number of releases before optimization");
STATISTIC(NumRetainsAfterOpt,
          "Number of retains after optimization");
STATISTIC(NumReleasesAfterOpt,
          "Number of releases after optimization");
#endif

namespace {

  /// Per-BasicBlock state.
  class BBState {
    /// The number of unique control paths from the entry which can reach this
    /// block.
    unsigned TopDownPathCount = 0;

    /// The number of unique control paths to exits from this block.
    unsigned BottomUpPathCount = 0;

    /// The top-down traversal uses this to record information known about a
    /// pointer at the bottom of each block.
    BlotMapVector<const Value *, TopDownPtrState> PerPtrTopDown;

    /// The bottom-up traversal uses this to record information known about a
    /// pointer at the top of each block.
    BlotMapVector<const Value *, BottomUpPtrState> PerPtrBottomUp;

    /// Effective predecessors of the current block ignoring ignorable edges and
    /// ignored backedges.
    SmallVector<BasicBlock *, 2> Preds;

    /// Effective successors of the current block ignoring ignorable edges and
    /// ignored backedges.
    SmallVector<BasicBlock *, 2> Succs;

  public:
    static const unsigned OverflowOccurredValue;

    BBState() = default;

    using top_down_ptr_iterator = decltype(PerPtrTopDown)::iterator;
    using const_top_down_ptr_iterator = decltype(PerPtrTopDown)::const_iterator;

    top_down_ptr_iterator top_down_ptr_begin() { return PerPtrTopDown.begin(); }
    top_down_ptr_iterator top_down_ptr_end() { return PerPtrTopDown.end(); }
    const_top_down_ptr_iterator top_down_ptr_begin() const {
      return PerPtrTopDown.begin();
    }
    const_top_down_ptr_iterator top_down_ptr_end() const {
      return PerPtrTopDown.end();
    }
    bool hasTopDownPtrs() const {
      return !PerPtrTopDown.empty();
    }

    using bottom_up_ptr_iterator = decltype(PerPtrBottomUp)::iterator;
    using const_bottom_up_ptr_iterator =
        decltype(PerPtrBottomUp)::const_iterator;

    bottom_up_ptr_iterator bottom_up_ptr_begin() {
      return PerPtrBottomUp.begin();
    }
    bottom_up_ptr_iterator bottom_up_ptr_end() { return PerPtrBottomUp.end(); }
    const_bottom_up_ptr_iterator bottom_up_ptr_begin() const {
      return PerPtrBottomUp.begin();
    }
    const_bottom_up_ptr_iterator bottom_up_ptr_end() const {
      return PerPtrBottomUp.end();
    }
    bool hasBottomUpPtrs() const {
      return !PerPtrBottomUp.empty();
    }

    /// Mark this block as being an entry block, which has one path from the
    /// entry by definition.
    void SetAsEntry() { TopDownPathCount = 1; }

    /// Mark this block as being an exit block, which has one path to an exit by
    /// definition.
    void SetAsExit()  { BottomUpPathCount = 1; }

    /// Attempt to find the PtrState object describing the top down state for
    /// pointer Arg. Return a new initialized PtrState describing the top down
    /// state for Arg if we do not find one.
    TopDownPtrState &getPtrTopDownState(const Value *Arg) {
      return PerPtrTopDown[Arg];
    }

    /// Attempt to find the PtrState object describing the bottom up state for
    /// pointer Arg. Return a new initialized PtrState describing the bottom up
    /// state for Arg if we do not find one.
    BottomUpPtrState &getPtrBottomUpState(const Value *Arg) {
      return PerPtrBottomUp[Arg];
    }

    /// Attempt to find the PtrState object describing the bottom up state for
    /// pointer Arg.
    bottom_up_ptr_iterator findPtrBottomUpState(const Value *Arg) {
      return PerPtrBottomUp.find(Arg);
    }

    void clearBottomUpPointers() {
      PerPtrBottomUp.clear();
    }

    void clearTopDownPointers() {
      PerPtrTopDown.clear();
    }

    void InitFromPred(const BBState &Other);
    void InitFromSucc(const BBState &Other);
    void MergePred(const BBState &Other);
    void MergeSucc(const BBState &Other);

    /// Compute the number of possible unique paths from an entry to an exit
    /// which pass through this block. This is only valid after both the
    /// top-down and bottom-up traversals are complete.
    ///
    /// Returns true if overflow occurred. Returns false if overflow did not
    /// occur.
    bool GetAllPathCountWithOverflow(unsigned &PathCount) const {
      if (TopDownPathCount == OverflowOccurredValue ||
          BottomUpPathCount == OverflowOccurredValue)
        return true;
      unsigned long long Product =
        (unsigned long long)TopDownPathCount*BottomUpPathCount;
      // Overflow occurred if any of the upper bits of Product are set or if all
      // the lower bits of Product are all set.
      return (Product >> 32) ||
             ((PathCount = Product) == OverflowOccurredValue);
    }

    // Specialized CFG utilities.
    using edge_iterator = SmallVectorImpl<BasicBlock *>::const_iterator;

    edge_iterator pred_begin() const { return Preds.begin(); }
    edge_iterator pred_end() const { return Preds.end(); }
    edge_iterator succ_begin() const { return Succs.begin(); }
    edge_iterator succ_end() const { return Succs.end(); }

    void addSucc(BasicBlock *Succ) { Succs.push_back(Succ); }
    void addPred(BasicBlock *Pred) { Preds.push_back(Pred); }

    bool isExit() const { return Succs.empty(); }
  };

} // end anonymous namespace

const unsigned BBState::OverflowOccurredValue = 0xffffffff;

namespace llvm {

raw_ostream &operator<<(raw_ostream &OS,
                        BBState &BBState) LLVM_ATTRIBUTE_UNUSED;

} // end namespace llvm

void BBState::InitFromPred(const BBState &Other) {
  PerPtrTopDown = Other.PerPtrTopDown;
  TopDownPathCount = Other.TopDownPathCount;
}

void BBState::InitFromSucc(const BBState &Other) {
  PerPtrBottomUp = Other.PerPtrBottomUp;
  BottomUpPathCount = Other.BottomUpPathCount;
}

/// The top-down traversal uses this to merge information about predecessors to
/// form the initial state for a new block.
void BBState::MergePred(const BBState &Other) {
  if (TopDownPathCount == OverflowOccurredValue)
    return;

  // Other.TopDownPathCount can be 0, in which case it is either dead or a
  // loop backedge. Loop backedges are special.
  TopDownPathCount += Other.TopDownPathCount;

  // In order to be consistent, we clear the top down pointers when by adding
  // TopDownPathCount becomes OverflowOccurredValue even though "true" overflow
  // has not occurred.
  if (TopDownPathCount == OverflowOccurredValue) {
    clearTopDownPointers();
    return;
  }

  // Check for overflow. If we have overflow, fall back to conservative
  // behavior.
  if (TopDownPathCount < Other.TopDownPathCount) {
    TopDownPathCount = OverflowOccurredValue;
    clearTopDownPointers();
    return;
  }

  // For each entry in the other set, if our set has an entry with the same key,
  // merge the entries. Otherwise, copy the entry and merge it with an empty
  // entry.
  for (auto MI = Other.top_down_ptr_begin(), ME = Other.top_down_ptr_end();
       MI != ME; ++MI) {
    auto Pair = PerPtrTopDown.insert(*MI);
    Pair.first->second.Merge(Pair.second ? TopDownPtrState() : MI->second,
                             /*TopDown=*/true);
  }

  // For each entry in our set, if the other set doesn't have an entry with the
  // same key, force it to merge with an empty entry.
  for (auto MI = top_down_ptr_begin(), ME = top_down_ptr_end(); MI != ME; ++MI)
    if (Other.PerPtrTopDown.find(MI->first) == Other.PerPtrTopDown.end())
      MI->second.Merge(TopDownPtrState(), /*TopDown=*/true);
}

/// The bottom-up traversal uses this to merge information about successors to
/// form the initial state for a new block.
void BBState::MergeSucc(const BBState &Other) {
  if (BottomUpPathCount == OverflowOccurredValue)
    return;

  // Other.BottomUpPathCount can be 0, in which case it is either dead or a
  // loop backedge. Loop backedges are special.
  BottomUpPathCount += Other.BottomUpPathCount;

  // In order to be consistent, we clear the top down pointers when by adding
  // BottomUpPathCount becomes OverflowOccurredValue even though "true" overflow
  // has not occurred.
  if (BottomUpPathCount == OverflowOccurredValue) {
    clearBottomUpPointers();
    return;
  }

  // Check for overflow. If we have overflow, fall back to conservative
  // behavior.
  if (BottomUpPathCount < Other.BottomUpPathCount) {
    BottomUpPathCount = OverflowOccurredValue;
    clearBottomUpPointers();
    return;
  }

  // For each entry in the other set, if our set has an entry with the
  // same key, merge the entries. Otherwise, copy the entry and merge
  // it with an empty entry.
  for (auto MI = Other.bottom_up_ptr_begin(), ME = Other.bottom_up_ptr_end();
       MI != ME; ++MI) {
    auto Pair = PerPtrBottomUp.insert(*MI);
    Pair.first->second.Merge(Pair.second ? BottomUpPtrState() : MI->second,
                             /*TopDown=*/false);
  }

  // For each entry in our set, if the other set doesn't have an entry
  // with the same key, force it to merge with an empty entry.
  for (auto MI = bottom_up_ptr_begin(), ME = bottom_up_ptr_end(); MI != ME;
       ++MI)
    if (Other.PerPtrBottomUp.find(MI->first) == Other.PerPtrBottomUp.end())
      MI->second.Merge(BottomUpPtrState(), /*TopDown=*/false);
}

raw_ostream &llvm::operator<<(raw_ostream &OS, BBState &BBInfo) {
  // Dump the pointers we are tracking.
  OS << "    TopDown State:\n";
  if (!BBInfo.hasTopDownPtrs()) {
    LLVM_DEBUG(dbgs() << "        NONE!\n");
  } else {
    for (auto I = BBInfo.top_down_ptr_begin(), E = BBInfo.top_down_ptr_end();
         I != E; ++I) {
      const PtrState &P = I->second;
      OS << "        Ptr: " << *I->first
         << "\n            KnownSafe:        " << (P.IsKnownSafe()?"true":"false")
         << "\n            ImpreciseRelease: "
           << (P.IsTrackingImpreciseReleases()?"true":"false") << "\n"
         << "            HasCFGHazards:    "
           << (P.IsCFGHazardAfflicted()?"true":"false") << "\n"
         << "            KnownPositive:    "
           << (P.HasKnownPositiveRefCount()?"true":"false") << "\n"
         << "            Seq:              "
         << P.GetSeq() << "\n";
    }
  }

  OS << "    BottomUp State:\n";
  if (!BBInfo.hasBottomUpPtrs()) {
    LLVM_DEBUG(dbgs() << "        NONE!\n");
  } else {
    for (auto I = BBInfo.bottom_up_ptr_begin(), E = BBInfo.bottom_up_ptr_end();
         I != E; ++I) {
      const PtrState &P = I->second;
      OS << "        Ptr: " << *I->first
         << "\n            KnownSafe:        " << (P.IsKnownSafe()?"true":"false")
         << "\n            ImpreciseRelease: "
           << (P.IsTrackingImpreciseReleases()?"true":"false") << "\n"
         << "            HasCFGHazards:    "
           << (P.IsCFGHazardAfflicted()?"true":"false") << "\n"
         << "            KnownPositive:    "
           << (P.HasKnownPositiveRefCount()?"true":"false") << "\n"
         << "            Seq:              "
         << P.GetSeq() << "\n";
    }
  }

  return OS;
}

namespace {

  /// The main ARC optimization pass.
  class ObjCARCOpt : public FunctionPass {
    bool Changed;
    ProvenanceAnalysis PA;

    /// A cache of references to runtime entry point constants.
    ARCRuntimeEntryPoints EP;

    /// A cache of MDKinds that can be passed into other functions to propagate
    /// MDKind identifiers.
    ARCMDKindCache MDKindCache;

    /// A flag indicating whether this optimization pass should run.
    bool Run;

    /// Flags which determine whether each of the interesting runtime functions
    /// is in fact used in the current function.
    unsigned UsedInThisFunction;

    bool OptimizeRetainRVCall(Function &F, Instruction *RetainRV);
    void OptimizeAutoreleaseRVCall(Function &F, Instruction *AutoreleaseRV,
                                   ARCInstKind &Class);
    void OptimizeIndividualCalls(Function &F);

    void CheckForCFGHazards(const BasicBlock *BB,
                            DenseMap<const BasicBlock *, BBState> &BBStates,
                            BBState &MyStates) const;
    bool VisitInstructionBottomUp(Instruction *Inst, BasicBlock *BB,
                                  BlotMapVector<Value *, RRInfo> &Retains,
                                  BBState &MyStates);
    bool VisitBottomUp(BasicBlock *BB,
                       DenseMap<const BasicBlock *, BBState> &BBStates,
                       BlotMapVector<Value *, RRInfo> &Retains);
    bool VisitInstructionTopDown(Instruction *Inst,
                                 DenseMap<Value *, RRInfo> &Releases,
                                 BBState &MyStates);
    bool VisitTopDown(BasicBlock *BB,
                      DenseMap<const BasicBlock *, BBState> &BBStates,
                      DenseMap<Value *, RRInfo> &Releases);
    bool Visit(Function &F, DenseMap<const BasicBlock *, BBState> &BBStates,
               BlotMapVector<Value *, RRInfo> &Retains,
               DenseMap<Value *, RRInfo> &Releases);

    void MoveCalls(Value *Arg, RRInfo &RetainsToMove, RRInfo &ReleasesToMove,
                   BlotMapVector<Value *, RRInfo> &Retains,
                   DenseMap<Value *, RRInfo> &Releases,
                   SmallVectorImpl<Instruction *> &DeadInsts, Module *M);

    bool
    PairUpRetainsAndReleases(DenseMap<const BasicBlock *, BBState> &BBStates,
                             BlotMapVector<Value *, RRInfo> &Retains,
                             DenseMap<Value *, RRInfo> &Releases, Module *M,
                             Instruction * Retain,
                             SmallVectorImpl<Instruction *> &DeadInsts,
                             RRInfo &RetainsToMove, RRInfo &ReleasesToMove,
                             Value *Arg, bool KnownSafe,
                             bool &AnyPairsCompletelyEliminated);

    bool PerformCodePlacement(DenseMap<const BasicBlock *, BBState> &BBStates,
                              BlotMapVector<Value *, RRInfo> &Retains,
                              DenseMap<Value *, RRInfo> &Releases, Module *M);

    void OptimizeWeakCalls(Function &F);

    bool OptimizeSequences(Function &F);

    void OptimizeReturns(Function &F);

#ifndef NDEBUG
    void GatherStatistics(Function &F, bool AfterOptimization = false);
#endif

    void getAnalysisUsage(AnalysisUsage &AU) const override;
    bool doInitialization(Module &M) override;
    bool runOnFunction(Function &F) override;
    void releaseMemory() override;

  public:
    static char ID;

    ObjCARCOpt() : FunctionPass(ID) {
      initializeObjCARCOptPass(*PassRegistry::getPassRegistry());
    }
  };

} // end anonymous namespace

char ObjCARCOpt::ID = 0;

INITIALIZE_PASS_BEGIN(ObjCARCOpt,
                      "objc-arc", "ObjC ARC optimization", false, false)
INITIALIZE_PASS_DEPENDENCY(ObjCARCAAWrapperPass)
INITIALIZE_PASS_END(ObjCARCOpt,
                    "objc-arc", "ObjC ARC optimization", false, false)

Pass *llvm::createObjCARCOptPass() {
  return new ObjCARCOpt();
}

void ObjCARCOpt::getAnalysisUsage(AnalysisUsage &AU) const {
  AU.addRequired<ObjCARCAAWrapperPass>();
  AU.addRequired<AAResultsWrapperPass>();
  // ARC optimization doesn't currently split critical edges.
  AU.setPreservesCFG();
}

/// Turn objc_retainAutoreleasedReturnValue into objc_retain if the operand is
/// not a return value.  Or, if it can be paired with an
/// objc_autoreleaseReturnValue, delete the pair and return true.
bool
ObjCARCOpt::OptimizeRetainRVCall(Function &F, Instruction *RetainRV) {
  // Check for the argument being from an immediately preceding call or invoke.
  const Value *Arg = GetArgRCIdentityRoot(RetainRV);
  ImmutableCallSite CS(Arg);
  if (const Instruction *Call = CS.getInstruction()) {
    if (Call->getParent() == RetainRV->getParent()) {
      BasicBlock::const_iterator I(Call);
      ++I;
      while (IsNoopInstruction(&*I))
        ++I;
      if (&*I == RetainRV)
        return false;
    } else if (const InvokeInst *II = dyn_cast<InvokeInst>(Call)) {
      BasicBlock *RetainRVParent = RetainRV->getParent();
      if (II->getNormalDest() == RetainRVParent) {
        BasicBlock::const_iterator I = RetainRVParent->begin();
        while (IsNoopInstruction(&*I))
          ++I;
        if (&*I == RetainRV)
          return false;
      }
    }
  }

  // Track PHIs which are equivalent to our Arg.
  SmallDenseSet<const Value*, 2> EquivalentArgs;
  EquivalentArgs.insert(Arg);

  // Add PHIs that are equivalent to Arg to ArgUsers.
  if (const PHINode *PN = dyn_cast<PHINode>(Arg)) {
    SmallVector<const Value *, 2> ArgUsers;
    getEquivalentPHIs(*PN, ArgUsers);
    EquivalentArgs.insert(ArgUsers.begin(), ArgUsers.end());
  }

  // Check for being preceded by an objc_autoreleaseReturnValue on the same
  // pointer. In this case, we can delete the pair.
  BasicBlock::iterator I = RetainRV->getIterator(),
                       Begin = RetainRV->getParent()->begin();
  if (I != Begin) {
    do
      --I;
    while (I != Begin && IsNoopInstruction(&*I));
    if (GetBasicARCInstKind(&*I) == ARCInstKind::AutoreleaseRV &&
        EquivalentArgs.count(GetArgRCIdentityRoot(&*I))) {
      Changed = true;
      ++NumPeeps;

      LLVM_DEBUG(dbgs() << "Erasing autoreleaseRV,retainRV pair: " << *I << "\n"
                        << "Erasing " << *RetainRV << "\n");

      EraseInstruction(&*I);
      EraseInstruction(RetainRV);
      return true;
    }
  }

  // Turn it to a plain objc_retain.
  Changed = true;
  ++NumPeeps;

  LLVM_DEBUG(dbgs() << "Transforming objc_retainAutoreleasedReturnValue => "
                       "objc_retain since the operand is not a return value.\n"
                       "Old = "
                    << *RetainRV << "\n");

  Constant *NewDecl = EP.get(ARCRuntimeEntryPointKind::Retain);
  cast<CallInst>(RetainRV)->setCalledFunction(NewDecl);

  LLVM_DEBUG(dbgs() << "New = " << *RetainRV << "\n");

  return false;
}

/// Turn objc_autoreleaseReturnValue into objc_autorelease if the result is not
/// used as a return value.
void ObjCARCOpt::OptimizeAutoreleaseRVCall(Function &F,
                                           Instruction *AutoreleaseRV,
                                           ARCInstKind &Class) {
  // Check for a return of the pointer value.
  const Value *Ptr = GetArgRCIdentityRoot(AutoreleaseRV);

  // If the argument is ConstantPointerNull or UndefValue, its other users
  // aren't actually interesting to look at.
  if (isa<ConstantData>(Ptr))
    return;

  SmallVector<const Value *, 2> Users;
  Users.push_back(Ptr);

  // Add PHIs that are equivalent to Ptr to Users.
  if (const PHINode *PN = dyn_cast<PHINode>(Ptr))
    getEquivalentPHIs(*PN, Users);

  do {
    Ptr = Users.pop_back_val();
    for (const User *U : Ptr->users()) {
      if (isa<ReturnInst>(U) || GetBasicARCInstKind(U) == ARCInstKind::RetainRV)
        return;
      if (isa<BitCastInst>(U))
        Users.push_back(U);
    }
  } while (!Users.empty());

  Changed = true;
  ++NumPeeps;

  LLVM_DEBUG(
      dbgs() << "Transforming objc_autoreleaseReturnValue => "
                "objc_autorelease since its operand is not used as a return "
                "value.\n"
                "Old = "
             << *AutoreleaseRV << "\n");

  CallInst *AutoreleaseRVCI = cast<CallInst>(AutoreleaseRV);
  Constant *NewDecl = EP.get(ARCRuntimeEntryPointKind::Autorelease);
  AutoreleaseRVCI->setCalledFunction(NewDecl);
  AutoreleaseRVCI->setTailCall(false); // Never tail call objc_autorelease.
  Class = ARCInstKind::Autorelease;

  LLVM_DEBUG(dbgs() << "New: " << *AutoreleaseRV << "\n");
}

namespace {
Instruction *
CloneCallInstForBB(CallInst &CI, BasicBlock &BB,
                   const DenseMap<BasicBlock *, ColorVector> &BlockColors) {
  SmallVector<OperandBundleDef, 1> OpBundles;
  for (unsigned I = 0, E = CI.getNumOperandBundles(); I != E; ++I) {
    auto Bundle = CI.getOperandBundleAt(I);
    // Funclets will be reassociated in the future.
    if (Bundle.getTagID() == LLVMContext::OB_funclet)
      continue;
    OpBundles.emplace_back(Bundle);
  }

  if (!BlockColors.empty()) {
    const ColorVector &CV = BlockColors.find(&BB)->second;
    assert(CV.size() == 1 && "non-unique color for block!");
    Instruction *EHPad = CV.front()->getFirstNonPHI();
    if (EHPad->isEHPad())
      OpBundles.emplace_back("funclet", EHPad);
  }

  return CallInst::Create(&CI, OpBundles);
}
}

/// Visit each call, one at a time, and make simplifications without doing any
/// additional analysis.
void ObjCARCOpt::OptimizeIndividualCalls(Function &F) {
  LLVM_DEBUG(dbgs() << "\n== ObjCARCOpt::OptimizeIndividualCalls ==\n");
  // Reset all the flags in preparation for recomputing them.
  UsedInThisFunction = 0;

  DenseMap<BasicBlock *, ColorVector> BlockColors;
  if (F.hasPersonalityFn() &&
      isScopedEHPersonality(classifyEHPersonality(F.getPersonalityFn())))
    BlockColors = colorEHFunclets(F);

  // Visit all objc_* calls in F.
  for (inst_iterator I = inst_begin(&F), E = inst_end(&F); I != E; ) {
    Instruction *Inst = &*I++;

    ARCInstKind Class = GetBasicARCInstKind(Inst);

    LLVM_DEBUG(dbgs() << "Visiting: Class: " << Class << "; " << *Inst << "\n");

    switch (Class) {
    default: break;

    // Delete no-op casts. These function calls have special semantics, but
    // the semantics are entirely implemented via lowering in the front-end,
    // so by the time they reach the optimizer, they are just no-op calls
    // which return their argument.
    //
    // There are gray areas here, as the ability to cast reference-counted
    // pointers to raw void* and back allows code to break ARC assumptions,
    // however these are currently considered to be unimportant.
    case ARCInstKind::NoopCast:
      Changed = true;
      ++NumNoops;
      LLVM_DEBUG(dbgs() << "Erasing no-op cast: " << *Inst << "\n");
      EraseInstruction(Inst);
      continue;

    // If the pointer-to-weak-pointer is null, it's undefined behavior.
    case ARCInstKind::StoreWeak:
    case ARCInstKind::LoadWeak:
    case ARCInstKind::LoadWeakRetained:
    case ARCInstKind::InitWeak:
    case ARCInstKind::DestroyWeak: {
      CallInst *CI = cast<CallInst>(Inst);
      if (IsNullOrUndef(CI->getArgOperand(0))) {
        Changed = true;
        Type *Ty = CI->getArgOperand(0)->getType();
        new StoreInst(UndefValue::get(cast<PointerType>(Ty)->getElementType()),
                      Constant::getNullValue(Ty),
                      CI);
        Value *NewValue = UndefValue::get(CI->getType());
        LLVM_DEBUG(
            dbgs() << "A null pointer-to-weak-pointer is undefined behavior."
                      "\nOld = "
                   << *CI << "\nNew = " << *NewValue << "\n");
        CI->replaceAllUsesWith(NewValue);
        CI->eraseFromParent();
        continue;
      }
      break;
    }
    case ARCInstKind::CopyWeak:
    case ARCInstKind::MoveWeak: {
      CallInst *CI = cast<CallInst>(Inst);
      if (IsNullOrUndef(CI->getArgOperand(0)) ||
          IsNullOrUndef(CI->getArgOperand(1))) {
        Changed = true;
        Type *Ty = CI->getArgOperand(0)->getType();
        new StoreInst(UndefValue::get(cast<PointerType>(Ty)->getElementType()),
                      Constant::getNullValue(Ty),
                      CI);

        Value *NewValue = UndefValue::get(CI->getType());
        LLVM_DEBUG(
            dbgs() << "A null pointer-to-weak-pointer is undefined behavior."
                      "\nOld = "
                   << *CI << "\nNew = " << *NewValue << "\n");

        CI->replaceAllUsesWith(NewValue);
        CI->eraseFromParent();
        continue;
      }
      break;
    }
    case ARCInstKind::RetainRV:
      if (OptimizeRetainRVCall(F, Inst))
        continue;
      break;
    case ARCInstKind::AutoreleaseRV:
      OptimizeAutoreleaseRVCall(F, Inst, Class);
      break;
    }

    // objc_autorelease(x) -> objc_release(x) if x is otherwise unused.
    if (IsAutorelease(Class) && Inst->use_empty()) {
      CallInst *Call = cast<CallInst>(Inst);
      const Value *Arg = Call->getArgOperand(0);
      Arg = FindSingleUseIdentifiedObject(Arg);
      if (Arg) {
        Changed = true;
        ++NumAutoreleases;

        // Create the declaration lazily.
        LLVMContext &C = Inst->getContext();

        Constant *Decl = EP.get(ARCRuntimeEntryPointKind::Release);
        CallInst *NewCall = CallInst::Create(Decl, Call->getArgOperand(0), "",
                                             Call);
        NewCall->setMetadata(MDKindCache.get(ARCMDKindID::ImpreciseRelease),
                             MDNode::get(C, None));

        LLVM_DEBUG(
            dbgs() << "Replacing autorelease{,RV}(x) with objc_release(x) "
                      "since x is otherwise unused.\nOld: "
                   << *Call << "\nNew: " << *NewCall << "\n");

        EraseInstruction(Call);
        Inst = NewCall;
        Class = ARCInstKind::Release;
      }
    }

    // For functions which can never be passed stack arguments, add
    // a tail keyword.
    if (IsAlwaysTail(Class)) {
      Changed = true;
      LLVM_DEBUG(
          dbgs() << "Adding tail keyword to function since it can never be "
                    "passed stack args: "
                 << *Inst << "\n");
      cast<CallInst>(Inst)->setTailCall();
    }

    // Ensure that functions that can never have a "tail" keyword due to the
    // semantics of ARC truly do not do so.
    if (IsNeverTail(Class)) {
      Changed = true;
      LLVM_DEBUG(dbgs() << "Removing tail keyword from function: " << *Inst
                        << "\n");
      cast<CallInst>(Inst)->setTailCall(false);
    }

    // Set nounwind as needed.
    if (IsNoThrow(Class)) {
      Changed = true;
      LLVM_DEBUG(dbgs() << "Found no throw class. Setting nounwind on: "
                        << *Inst << "\n");
      cast<CallInst>(Inst)->setDoesNotThrow();
    }

    if (!IsNoopOnNull(Class)) {
      UsedInThisFunction |= 1 << unsigned(Class);
      continue;
    }

    const Value *Arg = GetArgRCIdentityRoot(Inst);

    // ARC calls with null are no-ops. Delete them.
    if (IsNullOrUndef(Arg)) {
      Changed = true;
      ++NumNoops;
      LLVM_DEBUG(dbgs() << "ARC calls with  null are no-ops. Erasing: " << *Inst
                        << "\n");
      EraseInstruction(Inst);
      continue;
    }

    // Keep track of which of retain, release, autorelease, and retain_block
    // are actually present in this function.
    UsedInThisFunction |= 1 << unsigned(Class);

    // If Arg is a PHI, and one or more incoming values to the
    // PHI are null, and the call is control-equivalent to the PHI, and there
    // are no relevant side effects between the PHI and the call, and the call
    // is not a release that doesn't have the clang.imprecise_release tag, the
    // call could be pushed up to just those paths with non-null incoming
    // values. For now, don't bother splitting critical edges for this.
    if (Class == ARCInstKind::Release &&
        !Inst->getMetadata(MDKindCache.get(ARCMDKindID::ImpreciseRelease)))
      continue;

    SmallVector<std::pair<Instruction *, const Value *>, 4> Worklist;
    Worklist.push_back(std::make_pair(Inst, Arg));
    do {
      std::pair<Instruction *, const Value *> Pair = Worklist.pop_back_val();
      Inst = Pair.first;
      Arg = Pair.second;

      const PHINode *PN = dyn_cast<PHINode>(Arg);
      if (!PN) continue;

      // Determine if the PHI has any null operands, or any incoming
      // critical edges.
      bool HasNull = false;
      bool HasCriticalEdges = false;
      for (unsigned i = 0, e = PN->getNumIncomingValues(); i != e; ++i) {
        Value *Incoming =
          GetRCIdentityRoot(PN->getIncomingValue(i));
        if (IsNullOrUndef(Incoming))
          HasNull = true;
        else if (PN->getIncomingBlock(i)->getTerminator()->getNumSuccessors() !=
                 1) {
          HasCriticalEdges = true;
          break;
        }
      }
      // If we have null operands and no critical edges, optimize.
      if (!HasCriticalEdges && HasNull) {
        SmallPtrSet<Instruction *, 4> DependingInstructions;
        SmallPtrSet<const BasicBlock *, 4> Visited;

        // Check that there is nothing that cares about the reference
        // count between the call and the phi.
        switch (Class) {
        case ARCInstKind::Retain:
        case ARCInstKind::RetainBlock:
          // These can always be moved up.
          break;
        case ARCInstKind::Release:
          // These can't be moved across things that care about the retain
          // count.
          FindDependencies(NeedsPositiveRetainCount, Arg,
                           Inst->getParent(), Inst,
                           DependingInstructions, Visited, PA);
          break;
        case ARCInstKind::Autorelease:
          // These can't be moved across autorelease pool scope boundaries.
          FindDependencies(AutoreleasePoolBoundary, Arg,
                           Inst->getParent(), Inst,
                           DependingInstructions, Visited, PA);
          break;
        case ARCInstKind::ClaimRV:
        case ARCInstKind::RetainRV:
        case ARCInstKind::AutoreleaseRV:
          // Don't move these; the RV optimization depends on the autoreleaseRV
          // being tail called, and the retainRV being immediately after a call
          // (which might still happen if we get lucky with codegen layout, but
          // it's not worth taking the chance).
          continue;
        default:
          llvm_unreachable("Invalid dependence flavor");
        }

        if (DependingInstructions.size() == 1 &&
            *DependingInstructions.begin() == PN) {
          Changed = true;
          ++NumPartialNoops;
          // Clone the call into each predecessor that has a non-null value.
          CallInst *CInst = cast<CallInst>(Inst);
          Type *ParamTy = CInst->getArgOperand(0)->getType();
          for (unsigned i = 0, e = PN->getNumIncomingValues(); i != e; ++i) {
            Value *Incoming =
              GetRCIdentityRoot(PN->getIncomingValue(i));
            if (!IsNullOrUndef(Incoming)) {
              Value *Op = PN->getIncomingValue(i);
              Instruction *InsertPos = &PN->getIncomingBlock(i)->back();
              CallInst *Clone = cast<CallInst>(CloneCallInstForBB(
                  *CInst, *InsertPos->getParent(), BlockColors));
              if (Op->getType() != ParamTy)
                Op = new BitCastInst(Op, ParamTy, "", InsertPos);
              Clone->setArgOperand(0, Op);
              Clone->insertBefore(InsertPos);

              LLVM_DEBUG(dbgs() << "Cloning " << *CInst
                                << "\n"
                                   "And inserting clone at "
                                << *InsertPos << "\n");
              Worklist.push_back(std::make_pair(Clone, Incoming));
            }
          }
          // Erase the original call.
          LLVM_DEBUG(dbgs() << "Erasing: " << *CInst << "\n");
          EraseInstruction(CInst);
          continue;
        }
      }
    } while (!Worklist.empty());
  }
}

/// If we have a top down pointer in the S_Use state, make sure that there are
/// no CFG hazards by checking the states of various bottom up pointers.
static void CheckForUseCFGHazard(const Sequence SuccSSeq,
                                 const bool SuccSRRIKnownSafe,
                                 TopDownPtrState &S,
                                 bool &SomeSuccHasSame,
                                 bool &AllSuccsHaveSame,
                                 bool &NotAllSeqEqualButKnownSafe,
                                 bool &ShouldContinue) {
  switch (SuccSSeq) {
  case S_CanRelease: {
    if (!S.IsKnownSafe() && !SuccSRRIKnownSafe) {
      S.ClearSequenceProgress();
      break;
    }
    S.SetCFGHazardAfflicted(true);
    ShouldContinue = true;
    break;
  }
  case S_Use:
    SomeSuccHasSame = true;
    break;
  case S_Stop:
  case S_Release:
  case S_MovableRelease:
    if (!S.IsKnownSafe() && !SuccSRRIKnownSafe)
      AllSuccsHaveSame = false;
    else
      NotAllSeqEqualButKnownSafe = true;
    break;
  case S_Retain:
    llvm_unreachable("bottom-up pointer in retain state!");
  case S_None:
    llvm_unreachable("This should have been handled earlier.");
  }
}

/// If we have a Top Down pointer in the S_CanRelease state, make sure that
/// there are no CFG hazards by checking the states of various bottom up
/// pointers.
static void CheckForCanReleaseCFGHazard(const Sequence SuccSSeq,
                                        const bool SuccSRRIKnownSafe,
                                        TopDownPtrState &S,
                                        bool &SomeSuccHasSame,
                                        bool &AllSuccsHaveSame,
                                        bool &NotAllSeqEqualButKnownSafe) {
  switch (SuccSSeq) {
  case S_CanRelease:
    SomeSuccHasSame = true;
    break;
  case S_Stop:
  case S_Release:
  case S_MovableRelease:
  case S_Use:
    if (!S.IsKnownSafe() && !SuccSRRIKnownSafe)
      AllSuccsHaveSame = false;
    else
      NotAllSeqEqualButKnownSafe = true;
    break;
  case S_Retain:
    llvm_unreachable("bottom-up pointer in retain state!");
  case S_None:
    llvm_unreachable("This should have been handled earlier.");
  }
}

/// Check for critical edges, loop boundaries, irreducible control flow, or
/// other CFG structures where moving code across the edge would result in it
/// being executed more.
void
ObjCARCOpt::CheckForCFGHazards(const BasicBlock *BB,
                               DenseMap<const BasicBlock *, BBState> &BBStates,
                               BBState &MyStates) const {
  // If any top-down local-use or possible-dec has a succ which is earlier in
  // the sequence, forget it.
  for (auto I = MyStates.top_down_ptr_begin(), E = MyStates.top_down_ptr_end();
       I != E; ++I) {
    TopDownPtrState &S = I->second;
    const Sequence Seq = I->second.GetSeq();

    // We only care about S_Retain, S_CanRelease, and S_Use.
    if (Seq == S_None)
      continue;

    // Make sure that if extra top down states are added in the future that this
    // code is updated to handle it.
    assert((Seq == S_Retain || Seq == S_CanRelease || Seq == S_Use) &&
           "Unknown top down sequence state.");

    const Value *Arg = I->first;
    bool SomeSuccHasSame = false;
    bool AllSuccsHaveSame = true;
    bool NotAllSeqEqualButKnownSafe = false;

    for (const BasicBlock *Succ : successors(BB)) {
      // If VisitBottomUp has pointer information for this successor, take
      // what we know about it.
      const DenseMap<const BasicBlock *, BBState>::iterator BBI =
          BBStates.find(Succ);
      assert(BBI != BBStates.end());
      const BottomUpPtrState &SuccS = BBI->second.getPtrBottomUpState(Arg);
      const Sequence SuccSSeq = SuccS.GetSeq();

      // If bottom up, the pointer is in an S_None state, clear the sequence
      // progress since the sequence in the bottom up state finished
      // suggesting a mismatch in between retains/releases. This is true for
      // all three cases that we are handling here: S_Retain, S_Use, and
      // S_CanRelease.
      if (SuccSSeq == S_None) {
        S.ClearSequenceProgress();
        continue;
      }

      // If we have S_Use or S_CanRelease, perform our check for cfg hazard
      // checks.
      const bool SuccSRRIKnownSafe = SuccS.IsKnownSafe();

      // *NOTE* We do not use Seq from above here since we are allowing for
      // S.GetSeq() to change while we are visiting basic blocks.
      switch(S.GetSeq()) {
      case S_Use: {
        bool ShouldContinue = false;
        CheckForUseCFGHazard(SuccSSeq, SuccSRRIKnownSafe, S, SomeSuccHasSame,
                             AllSuccsHaveSame, NotAllSeqEqualButKnownSafe,
                             ShouldContinue);
        if (ShouldContinue)
          continue;
        break;
      }
      case S_CanRelease:
        CheckForCanReleaseCFGHazard(SuccSSeq, SuccSRRIKnownSafe, S,
                                    SomeSuccHasSame, AllSuccsHaveSame,
                                    NotAllSeqEqualButKnownSafe);
        break;
      case S_Retain:
      case S_None:
      case S_Stop:
      case S_Release:
      case S_MovableRelease:
        break;
      }
    }

    // If the state at the other end of any of the successor edges
    // matches the current state, require all edges to match. This
    // guards against loops in the middle of a sequence.
    if (SomeSuccHasSame && !AllSuccsHaveSame) {
      S.ClearSequenceProgress();
    } else if (NotAllSeqEqualButKnownSafe) {
      // If we would have cleared the state foregoing the fact that we are known
      // safe, stop code motion. This is because whether or not it is safe to
      // remove RR pairs via KnownSafe is an orthogonal concept to whether we
      // are allowed to perform code motion.
      S.SetCFGHazardAfflicted(true);
    }
  }
}

bool ObjCARCOpt::VisitInstructionBottomUp(
    Instruction *Inst, BasicBlock *BB, BlotMapVector<Value *, RRInfo> &Retains,
    BBState &MyStates) {
  bool NestingDetected = false;
  ARCInstKind Class = GetARCInstKind(Inst);
  const Value *Arg = nullptr;

  LLVM_DEBUG(dbgs() << "        Class: " << Class << "\n");

  switch (Class) {
  case ARCInstKind::Release: {
    Arg = GetArgRCIdentityRoot(Inst);

    BottomUpPtrState &S = MyStates.getPtrBottomUpState(Arg);
    NestingDetected |= S.InitBottomUp(MDKindCache, Inst);
    break;
  }
  case ARCInstKind::RetainBlock:
    // In OptimizeIndividualCalls, we have strength reduced all optimizable
    // objc_retainBlocks to objc_retains. Thus at this point any
    // objc_retainBlocks that we see are not optimizable.
    break;
  case ARCInstKind::Retain:
  case ARCInstKind::RetainRV: {
    Arg = GetArgRCIdentityRoot(Inst);
    BottomUpPtrState &S = MyStates.getPtrBottomUpState(Arg);
    if (S.MatchWithRetain()) {
      // Don't do retain+release tracking for ARCInstKind::RetainRV, because
      // it's better to let it remain as the first instruction after a call.
      if (Class != ARCInstKind::RetainRV) {
        LLVM_DEBUG(dbgs() << "        Matching with: " << *Inst << "\n");
        Retains[Inst] = S.GetRRInfo();
      }
      S.ClearSequenceProgress();
    }
    // A retain moving bottom up can be a use.
    break;
  }
  case ARCInstKind::AutoreleasepoolPop:
    // Conservatively, clear MyStates for all known pointers.
    MyStates.clearBottomUpPointers();
    return NestingDetected;
  case ARCInstKind::AutoreleasepoolPush:
  case ARCInstKind::None:
    // These are irrelevant.
    return NestingDetected;
  default:
    break;
  }

  // Consider any other possible effects of this instruction on each
  // pointer being tracked.
  for (auto MI = MyStates.bottom_up_ptr_begin(),
            ME = MyStates.bottom_up_ptr_end();
       MI != ME; ++MI) {
    const Value *Ptr = MI->first;
    if (Ptr == Arg)
      continue; // Handled above.
    BottomUpPtrState &S = MI->second;

    if (S.HandlePotentialAlterRefCount(Inst, Ptr, PA, Class))
      continue;

    S.HandlePotentialUse(BB, Inst, Ptr, PA, Class);
  }

  return NestingDetected;
}

bool ObjCARCOpt::VisitBottomUp(BasicBlock *BB,
                               DenseMap<const BasicBlock *, BBState> &BBStates,
                               BlotMapVector<Value *, RRInfo> &Retains) {
  LLVM_DEBUG(dbgs() << "\n== ObjCARCOpt::VisitBottomUp ==\n");

  bool NestingDetected = false;
  BBState &MyStates = BBStates[BB];

  // Merge the states from each successor to compute the initial state
  // for the current block.
  BBState::edge_iterator SI(MyStates.succ_begin()),
                         SE(MyStates.succ_end());
  if (SI != SE) {
    const BasicBlock *Succ = *SI;
    DenseMap<const BasicBlock *, BBState>::iterator I = BBStates.find(Succ);
    assert(I != BBStates.end());
    MyStates.InitFromSucc(I->second);
    ++SI;
    for (; SI != SE; ++SI) {
      Succ = *SI;
      I = BBStates.find(Succ);
      assert(I != BBStates.end());
      MyStates.MergeSucc(I->second);
    }
  }

  LLVM_DEBUG(dbgs() << "Before:\n"
                    << BBStates[BB] << "\n"
                    << "Performing Dataflow:\n");

  // Visit all the instructions, bottom-up.
  for (BasicBlock::iterator I = BB->end(), E = BB->begin(); I != E; --I) {
    Instruction *Inst = &*std::prev(I);

    // Invoke instructions are visited as part of their successors (below).
    if (isa<InvokeInst>(Inst))
      continue;

    LLVM_DEBUG(dbgs() << "    Visiting " << *Inst << "\n");

    NestingDetected |= VisitInstructionBottomUp(Inst, BB, Retains, MyStates);
  }

  // If there's a predecessor with an invoke, visit the invoke as if it were
  // part of this block, since we can't insert code after an invoke in its own
  // block, and we don't want to split critical edges.
  for (BBState::edge_iterator PI(MyStates.pred_begin()),
       PE(MyStates.pred_end()); PI != PE; ++PI) {
    BasicBlock *Pred = *PI;
    if (InvokeInst *II = dyn_cast<InvokeInst>(&Pred->back()))
      NestingDetected |= VisitInstructionBottomUp(II, BB, Retains, MyStates);
  }

  LLVM_DEBUG(dbgs() << "\nFinal State:\n" << BBStates[BB] << "\n");

  return NestingDetected;
}

bool
ObjCARCOpt::VisitInstructionTopDown(Instruction *Inst,
                                    DenseMap<Value *, RRInfo> &Releases,
                                    BBState &MyStates) {
  bool NestingDetected = false;
  ARCInstKind Class = GetARCInstKind(Inst);
  const Value *Arg = nullptr;

  LLVM_DEBUG(dbgs() << "        Class: " << Class << "\n");

  switch (Class) {
  case ARCInstKind::RetainBlock:
    // In OptimizeIndividualCalls, we have strength reduced all optimizable
    // objc_retainBlocks to objc_retains. Thus at this point any
    // objc_retainBlocks that we see are not optimizable. We need to break since
    // a retain can be a potential use.
    break;
  case ARCInstKind::Retain:
  case ARCInstKind::RetainRV: {
    Arg = GetArgRCIdentityRoot(Inst);
    TopDownPtrState &S = MyStates.getPtrTopDownState(Arg);
    NestingDetected |= S.InitTopDown(Class, Inst);
    // A retain can be a potential use; proceed to the generic checking
    // code below.
    break;
  }
  case ARCInstKind::Release: {
    Arg = GetArgRCIdentityRoot(Inst);
    TopDownPtrState &S = MyStates.getPtrTopDownState(Arg);
    // Try to form a tentative pair in between this release instruction and the
    // top down pointers that we are tracking.
    if (S.MatchWithRelease(MDKindCache, Inst)) {
      // If we succeed, copy S's RRInfo into the Release -> {Retain Set
      // Map}. Then we clear S.
      LLVM_DEBUG(dbgs() << "        Matching with: " << *Inst << "\n");
      Releases[Inst] = S.GetRRInfo();
      S.ClearSequenceProgress();
    }
    break;
  }
  case ARCInstKind::AutoreleasepoolPop:
    // Conservatively, clear MyStates for all known pointers.
    MyStates.clearTopDownPointers();
    return false;
  case ARCInstKind::AutoreleasepoolPush:
  case ARCInstKind::None:
    // These can not be uses of
    return false;
  default:
    break;
  }

  // Consider any other possible effects of this instruction on each
  // pointer being tracked.
  for (auto MI = MyStates.top_down_ptr_begin(),
            ME = MyStates.top_down_ptr_end();
       MI != ME; ++MI) {
    const Value *Ptr = MI->first;
    if (Ptr == Arg)
      continue; // Handled above.
    TopDownPtrState &S = MI->second;
    if (S.HandlePotentialAlterRefCount(Inst, Ptr, PA, Class))
      continue;

    S.HandlePotentialUse(Inst, Ptr, PA, Class);
  }

  return NestingDetected;
}

bool
ObjCARCOpt::VisitTopDown(BasicBlock *BB,
                         DenseMap<const BasicBlock *, BBState> &BBStates,
                         DenseMap<Value *, RRInfo> &Releases) {
  LLVM_DEBUG(dbgs() << "\n== ObjCARCOpt::VisitTopDown ==\n");
  bool NestingDetected = false;
  BBState &MyStates = BBStates[BB];

  // Merge the states from each predecessor to compute the initial state
  // for the current block.
  BBState::edge_iterator PI(MyStates.pred_begin()),
                         PE(MyStates.pred_end());
  if (PI != PE) {
    const BasicBlock *Pred = *PI;
    DenseMap<const BasicBlock *, BBState>::iterator I = BBStates.find(Pred);
    assert(I != BBStates.end());
    MyStates.InitFromPred(I->second);
    ++PI;
    for (; PI != PE; ++PI) {
      Pred = *PI;
      I = BBStates.find(Pred);
      assert(I != BBStates.end());
      MyStates.MergePred(I->second);
    }
  }

  LLVM_DEBUG(dbgs() << "Before:\n"
                    << BBStates[BB] << "\n"
                    << "Performing Dataflow:\n");

  // Visit all the instructions, top-down.
  for (Instruction &Inst : *BB) {
    LLVM_DEBUG(dbgs() << "    Visiting " << Inst << "\n");

    NestingDetected |= VisitInstructionTopDown(&Inst, Releases, MyStates);
  }

  LLVM_DEBUG(dbgs() << "\nState Before Checking for CFG Hazards:\n"
                    << BBStates[BB] << "\n\n");
  CheckForCFGHazards(BB, BBStates, MyStates);
  LLVM_DEBUG(dbgs() << "Final State:\n" << BBStates[BB] << "\n");
  return NestingDetected;
}

static void
ComputePostOrders(Function &F,
                  SmallVectorImpl<BasicBlock *> &PostOrder,
                  SmallVectorImpl<BasicBlock *> &ReverseCFGPostOrder,
                  unsigned NoObjCARCExceptionsMDKind,
                  DenseMap<const BasicBlock *, BBState> &BBStates) {
  /// The visited set, for doing DFS walks.
  SmallPtrSet<BasicBlock *, 16> Visited;

  // Do DFS, computing the PostOrder.
  SmallPtrSet<BasicBlock *, 16> OnStack;
  SmallVector<std::pair<BasicBlock *, succ_iterator>, 16> SuccStack;

  // Functions always have exactly one entry block, and we don't have
  // any other block that we treat like an entry block.
  BasicBlock *EntryBB = &F.getEntryBlock();
  BBState &MyStates = BBStates[EntryBB];
  MyStates.SetAsEntry();
  Instruction *EntryTI = EntryBB->getTerminator();
  SuccStack.push_back(std::make_pair(EntryBB, succ_iterator(EntryTI)));
  Visited.insert(EntryBB);
  OnStack.insert(EntryBB);
  do {
  dfs_next_succ:
    BasicBlock *CurrBB = SuccStack.back().first;
    succ_iterator SE(CurrBB->getTerminator(), false);

    while (SuccStack.back().second != SE) {
      BasicBlock *SuccBB = *SuccStack.back().second++;
      if (Visited.insert(SuccBB).second) {
        SuccStack.push_back(
            std::make_pair(SuccBB, succ_iterator(SuccBB->getTerminator())));
        BBStates[CurrBB].addSucc(SuccBB);
        BBState &SuccStates = BBStates[SuccBB];
        SuccStates.addPred(CurrBB);
        OnStack.insert(SuccBB);
        goto dfs_next_succ;
      }

      if (!OnStack.count(SuccBB)) {
        BBStates[CurrBB].addSucc(SuccBB);
        BBStates[SuccBB].addPred(CurrBB);
      }
    }
    OnStack.erase(CurrBB);
    PostOrder.push_back(CurrBB);
    SuccStack.pop_back();
  } while (!SuccStack.empty());

  Visited.clear();

  // Do reverse-CFG DFS, computing the reverse-CFG PostOrder.
  // Functions may have many exits, and there also blocks which we treat
  // as exits due to ignored edges.
  SmallVector<std::pair<BasicBlock *, BBState::edge_iterator>, 16> PredStack;
  for (BasicBlock &ExitBB : F) {
    BBState &MyStates = BBStates[&ExitBB];
    if (!MyStates.isExit())
      continue;

    MyStates.SetAsExit();

    PredStack.push_back(std::make_pair(&ExitBB, MyStates.pred_begin()));
    Visited.insert(&ExitBB);
    while (!PredStack.empty()) {
    reverse_dfs_next_succ:
      BBState::edge_iterator PE = BBStates[PredStack.back().first].pred_end();
      while (PredStack.back().second != PE) {
        BasicBlock *BB = *PredStack.back().second++;
        if (Visited.insert(BB).second) {
          PredStack.push_back(std::make_pair(BB, BBStates[BB].pred_begin()));
          goto reverse_dfs_next_succ;
        }
      }
      ReverseCFGPostOrder.push_back(PredStack.pop_back_val().first);
    }
  }
}

// Visit the function both top-down and bottom-up.
bool ObjCARCOpt::Visit(Function &F,
                       DenseMap<const BasicBlock *, BBState> &BBStates,
                       BlotMapVector<Value *, RRInfo> &Retains,
                       DenseMap<Value *, RRInfo> &Releases) {
  // Use reverse-postorder traversals, because we magically know that loops
  // will be well behaved, i.e. they won't repeatedly call retain on a single
  // pointer without doing a release. We can't use the ReversePostOrderTraversal
  // class here because we want the reverse-CFG postorder to consider each
  // function exit point, and we want to ignore selected cycle edges.
  SmallVector<BasicBlock *, 16> PostOrder;
  SmallVector<BasicBlock *, 16> ReverseCFGPostOrder;
  ComputePostOrders(F, PostOrder, ReverseCFGPostOrder,
                    MDKindCache.get(ARCMDKindID::NoObjCARCExceptions),
                    BBStates);

  // Use reverse-postorder on the reverse CFG for bottom-up.
  bool BottomUpNestingDetected = false;
  for (BasicBlock *BB : llvm::reverse(ReverseCFGPostOrder))
    BottomUpNestingDetected |= VisitBottomUp(BB, BBStates, Retains);

  // Use reverse-postorder for top-down.
  bool TopDownNestingDetected = false;
  for (BasicBlock *BB : llvm::reverse(PostOrder))
    TopDownNestingDetected |= VisitTopDown(BB, BBStates, Releases);

  return TopDownNestingDetected && BottomUpNestingDetected;
}

/// Move the calls in RetainsToMove and ReleasesToMove.
void ObjCARCOpt::MoveCalls(Value *Arg, RRInfo &RetainsToMove,
                           RRInfo &ReleasesToMove,
                           BlotMapVector<Value *, RRInfo> &Retains,
                           DenseMap<Value *, RRInfo> &Releases,
                           SmallVectorImpl<Instruction *> &DeadInsts,
                           Module *M) {
  Type *ArgTy = Arg->getType();
  Type *ParamTy = PointerType::getUnqual(Type::getInt8Ty(ArgTy->getContext()));

  LLVM_DEBUG(dbgs() << "== ObjCARCOpt::MoveCalls ==\n");

  // Insert the new retain and release calls.
  for (Instruction *InsertPt : ReleasesToMove.ReverseInsertPts) {
    Value *MyArg = ArgTy == ParamTy ? Arg :
                   new BitCastInst(Arg, ParamTy, "", InsertPt);
    Constant *Decl = EP.get(ARCRuntimeEntryPointKind::Retain);
    CallInst *Call = CallInst::Create(Decl, MyArg, "", InsertPt);
    Call->setDoesNotThrow();
    Call->setTailCall();

    LLVM_DEBUG(dbgs() << "Inserting new Retain: " << *Call
                      << "\n"
                         "At insertion point: "
                      << *InsertPt << "\n");
  }
  for (Instruction *InsertPt : RetainsToMove.ReverseInsertPts) {
    Value *MyArg = ArgTy == ParamTy ? Arg :
                   new BitCastInst(Arg, ParamTy, "", InsertPt);
    Constant *Decl = EP.get(ARCRuntimeEntryPointKind::Release);
    CallInst *Call = CallInst::Create(Decl, MyArg, "", InsertPt);
    // Attach a clang.imprecise_release metadata tag, if appropriate.
    if (MDNode *M = ReleasesToMove.ReleaseMetadata)
      Call->setMetadata(MDKindCache.get(ARCMDKindID::ImpreciseRelease), M);
    Call->setDoesNotThrow();
    if (ReleasesToMove.IsTailCallRelease)
      Call->setTailCall();

    LLVM_DEBUG(dbgs() << "Inserting new Release: " << *Call
                      << "\n"
                         "At insertion point: "
                      << *InsertPt << "\n");
  }

  // Delete the original retain and release calls.
  for (Instruction *OrigRetain : RetainsToMove.Calls) {
    Retains.blot(OrigRetain);
    DeadInsts.push_back(OrigRetain);
    LLVM_DEBUG(dbgs() << "Deleting retain: " << *OrigRetain << "\n");
  }
  for (Instruction *OrigRelease : ReleasesToMove.Calls) {
    Releases.erase(OrigRelease);
    DeadInsts.push_back(OrigRelease);
    LLVM_DEBUG(dbgs() << "Deleting release: " << *OrigRelease << "\n");
  }
}

bool ObjCARCOpt::PairUpRetainsAndReleases(
    DenseMap<const BasicBlock *, BBState> &BBStates,
    BlotMapVector<Value *, RRInfo> &Retains,
    DenseMap<Value *, RRInfo> &Releases, Module *M,
    Instruction *Retain,
    SmallVectorImpl<Instruction *> &DeadInsts, RRInfo &RetainsToMove,
    RRInfo &ReleasesToMove, Value *Arg, bool KnownSafe,
    bool &AnyPairsCompletelyEliminated) {
  // If a pair happens in a region where it is known that the reference count
  // is already incremented, we can similarly ignore possible decrements unless
  // we are dealing with a retainable object with multiple provenance sources.
  bool KnownSafeTD = true, KnownSafeBU = true;
  bool CFGHazardAfflicted = false;

  // Connect the dots between the top-down-collected RetainsToMove and
  // bottom-up-collected ReleasesToMove to form sets of related calls.
  // This is an iterative process so that we connect multiple releases
  // to multiple retains if needed.
  unsigned OldDelta = 0;
  unsigned NewDelta = 0;
  unsigned OldCount = 0;
  unsigned NewCount = 0;
  bool FirstRelease = true;
  for (SmallVector<Instruction *, 4> NewRetains{Retain};;) {
    SmallVector<Instruction *, 4> NewReleases;
    for (Instruction *NewRetain : NewRetains) {
      auto It = Retains.find(NewRetain);
      assert(It != Retains.end());
      const RRInfo &NewRetainRRI = It->second;
      KnownSafeTD &= NewRetainRRI.KnownSafe;
      CFGHazardAfflicted |= NewRetainRRI.CFGHazardAfflicted;
      for (Instruction *NewRetainRelease : NewRetainRRI.Calls) {
        auto Jt = Releases.find(NewRetainRelease);
        if (Jt == Releases.end())
          return false;
        const RRInfo &NewRetainReleaseRRI = Jt->second;

        // If the release does not have a reference to the retain as well,
        // something happened which is unaccounted for. Do not do anything.
        //
        // This can happen if we catch an additive overflow during path count
        // merging.
        if (!NewRetainReleaseRRI.Calls.count(NewRetain))
          return false;

        if (ReleasesToMove.Calls.insert(NewRetainRelease).second) {
          // If we overflow when we compute the path count, don't remove/move
          // anything.
          const BBState &NRRBBState = BBStates[NewRetainRelease->getParent()];
          unsigned PathCount = BBState::OverflowOccurredValue;
          if (NRRBBState.GetAllPathCountWithOverflow(PathCount))
            return false;
          assert(PathCount != BBState::OverflowOccurredValue &&
                 "PathCount at this point can not be "
                 "OverflowOccurredValue.");
          OldDelta -= PathCount;

          // Merge the ReleaseMetadata and IsTailCallRelease values.
          if (FirstRelease) {
            ReleasesToMove.ReleaseMetadata =
              NewRetainReleaseRRI.ReleaseMetadata;
            ReleasesToMove.IsTailCallRelease =
              NewRetainReleaseRRI.IsTailCallRelease;
            FirstRelease = false;
          } else {
            if (ReleasesToMove.ReleaseMetadata !=
                NewRetainReleaseRRI.ReleaseMetadata)
              ReleasesToMove.ReleaseMetadata = nullptr;
            if (ReleasesToMove.IsTailCallRelease !=
                NewRetainReleaseRRI.IsTailCallRelease)
              ReleasesToMove.IsTailCallRelease = false;
          }

          // Collect the optimal insertion points.
          if (!KnownSafe)
            for (Instruction *RIP : NewRetainReleaseRRI.ReverseInsertPts) {
              if (ReleasesToMove.ReverseInsertPts.insert(RIP).second) {
                // If we overflow when we compute the path count, don't
                // remove/move anything.
                const BBState &RIPBBState = BBStates[RIP->getParent()];
                PathCount = BBState::OverflowOccurredValue;
                if (RIPBBState.GetAllPathCountWithOverflow(PathCount))
                  return false;
                assert(PathCount != BBState::OverflowOccurredValue &&
                       "PathCount at this point can not be "
                       "OverflowOccurredValue.");
                NewDelta -= PathCount;
              }
            }
          NewReleases.push_back(NewRetainRelease);
        }
      }
    }
    NewRetains.clear();
    if (NewReleases.empty()) break;

    // Back the other way.
    for (Instruction *NewRelease : NewReleases) {
      auto It = Releases.find(NewRelease);
      assert(It != Releases.end());
      const RRInfo &NewReleaseRRI = It->second;
      KnownSafeBU &= NewReleaseRRI.KnownSafe;
      CFGHazardAfflicted |= NewReleaseRRI.CFGHazardAfflicted;
      for (Instruction *NewReleaseRetain : NewReleaseRRI.Calls) {
        auto Jt = Retains.find(NewReleaseRetain);
        if (Jt == Retains.end())
          return false;
        const RRInfo &NewReleaseRetainRRI = Jt->second;

        // If the retain does not have a reference to the release as well,
        // something happened which is unaccounted for. Do not do anything.
        //
        // This can happen if we catch an additive overflow during path count
        // merging.
        if (!NewReleaseRetainRRI.Calls.count(NewRelease))
          return false;

        if (RetainsToMove.Calls.insert(NewReleaseRetain).second) {
          // If we overflow when we compute the path count, don't remove/move
          // anything.
          const BBState &NRRBBState = BBStates[NewReleaseRetain->getParent()];
          unsigned PathCount = BBState::OverflowOccurredValue;
          if (NRRBBState.GetAllPathCountWithOverflow(PathCount))
            return false;
          assert(PathCount != BBState::OverflowOccurredValue &&
                 "PathCount at this point can not be "
                 "OverflowOccurredValue.");
          OldDelta += PathCount;
          OldCount += PathCount;

          // Collect the optimal insertion points.
          if (!KnownSafe)
            for (Instruction *RIP : NewReleaseRetainRRI.ReverseInsertPts) {
              if (RetainsToMove.ReverseInsertPts.insert(RIP).second) {
                // If we overflow when we compute the path count, don't
                // remove/move anything.
                const BBState &RIPBBState = BBStates[RIP->getParent()];

                PathCount = BBState::OverflowOccurredValue;
                if (RIPBBState.GetAllPathCountWithOverflow(PathCount))
                  return false;
                assert(PathCount != BBState::OverflowOccurredValue &&
                       "PathCount at this point can not be "
                       "OverflowOccurredValue.");
                NewDelta += PathCount;
                NewCount += PathCount;
              }
            }
          NewRetains.push_back(NewReleaseRetain);
        }
      }
    }
    if (NewRetains.empty()) break;
  }

  // We can only remove pointers if we are known safe in both directions.
  bool UnconditionallySafe = KnownSafeTD && KnownSafeBU;
  if (UnconditionallySafe) {
    RetainsToMove.ReverseInsertPts.clear();
    ReleasesToMove.ReverseInsertPts.clear();
    NewCount = 0;
  } else {
    // Determine whether the new insertion points we computed preserve the
    // balance of retain and release calls through the program.
    // TODO: If the fully aggressive solution isn't valid, try to find a
    // less aggressive solution which is.
    if (NewDelta != 0)
      return false;

    // At this point, we are not going to remove any RR pairs, but we still are
    // able to move RR pairs. If one of our pointers is afflicted with
    // CFGHazards, we cannot perform such code motion so exit early.
    const bool WillPerformCodeMotion =
        !RetainsToMove.ReverseInsertPts.empty() ||
        !ReleasesToMove.ReverseInsertPts.empty();
    if (CFGHazardAfflicted && WillPerformCodeMotion)
      return false;
  }

  // Determine whether the original call points are balanced in the retain and
  // release calls through the program. If not, conservatively don't touch
  // them.
  // TODO: It's theoretically possible to do code motion in this case, as
  // long as the existing imbalances are maintained.
  if (OldDelta != 0)
    return false;

  Changed = true;
  assert(OldCount != 0 && "Unreachable code?");
  NumRRs += OldCount - NewCount;
  // Set to true if we completely removed any RR pairs.
  AnyPairsCompletelyEliminated = NewCount == 0;

  // We can move calls!
  return true;
}

/// Identify pairings between the retains and releases, and delete and/or move
/// them.
bool ObjCARCOpt::PerformCodePlacement(
    DenseMap<const BasicBlock *, BBState> &BBStates,
    BlotMapVector<Value *, RRInfo> &Retains,
    DenseMap<Value *, RRInfo> &Releases, Module *M) {
  LLVM_DEBUG(dbgs() << "\n== ObjCARCOpt::PerformCodePlacement ==\n");

  bool AnyPairsCompletelyEliminated = false;
  SmallVector<Instruction *, 8> DeadInsts;

  // Visit each retain.
  for (BlotMapVector<Value *, RRInfo>::const_iterator I = Retains.begin(),
                                                      E = Retains.end();
       I != E; ++I) {
    Value *V = I->first;
    if (!V) continue; // blotted

    Instruction *Retain = cast<Instruction>(V);

    LLVM_DEBUG(dbgs() << "Visiting: " << *Retain << "\n");

    Value *Arg = GetArgRCIdentityRoot(Retain);

    // If the object being released is in static or stack storage, we know it's
    // not being managed by ObjC reference counting, so we can delete pairs
    // regardless of what possible decrements or uses lie between them.
    bool KnownSafe = isa<Constant>(Arg) || isa<AllocaInst>(Arg);

    // A constant pointer can't be pointing to an object on the heap. It may
    // be reference-counted, but it won't be deleted.
    if (const LoadInst *LI = dyn_cast<LoadInst>(Arg))
      if (const GlobalVariable *GV =
            dyn_cast<GlobalVariable>(
              GetRCIdentityRoot(LI->getPointerOperand())))
        if (GV->isConstant())
          KnownSafe = true;

    // Connect the dots between the top-down-collected RetainsToMove and
    // bottom-up-collected ReleasesToMove to form sets of related calls.
    RRInfo RetainsToMove, ReleasesToMove;

    bool PerformMoveCalls = PairUpRetainsAndReleases(
        BBStates, Retains, Releases, M, Retain, DeadInsts,
        RetainsToMove, ReleasesToMove, Arg, KnownSafe,
        AnyPairsCompletelyEliminated);

    if (PerformMoveCalls) {
      // Ok, everything checks out and we're all set. Let's move/delete some
      // code!
      MoveCalls(Arg, RetainsToMove, ReleasesToMove,
                Retains, Releases, DeadInsts, M);
    }
  }

  // Now that we're done moving everything, we can delete the newly dead
  // instructions, as we no longer need them as insert points.
  while (!DeadInsts.empty())
    EraseInstruction(DeadInsts.pop_back_val());

  return AnyPairsCompletelyEliminated;
}

/// Weak pointer optimizations.
void ObjCARCOpt::OptimizeWeakCalls(Function &F) {
  LLVM_DEBUG(dbgs() << "\n== ObjCARCOpt::OptimizeWeakCalls ==\n");

  // First, do memdep-style RLE and S2L optimizations. We can't use memdep
  // itself because it uses AliasAnalysis and we need to do provenance
  // queries instead.
  for (inst_iterator I = inst_begin(&F), E = inst_end(&F); I != E; ) {
    Instruction *Inst = &*I++;

    LLVM_DEBUG(dbgs() << "Visiting: " << *Inst << "\n");

    ARCInstKind Class = GetBasicARCInstKind(Inst);
    if (Class != ARCInstKind::LoadWeak &&
        Class != ARCInstKind::LoadWeakRetained)
      continue;

    // Delete objc_loadWeak calls with no users.
    if (Class == ARCInstKind::LoadWeak && Inst->use_empty()) {
      Inst->eraseFromParent();
      continue;
    }

    // TODO: For now, just look for an earlier available version of this value
    // within the same block. Theoretically, we could do memdep-style non-local
    // analysis too, but that would want caching. A better approach would be to
    // use the technique that EarlyCSE uses.
    inst_iterator Current = std::prev(I);
    BasicBlock *CurrentBB = &*Current.getBasicBlockIterator();
    for (BasicBlock::iterator B = CurrentBB->begin(),
                              J = Current.getInstructionIterator();
         J != B; --J) {
      Instruction *EarlierInst = &*std::prev(J);
      ARCInstKind EarlierClass = GetARCInstKind(EarlierInst);
      switch (EarlierClass) {
      case ARCInstKind::LoadWeak:
      case ARCInstKind::LoadWeakRetained: {
        // If this is loading from the same pointer, replace this load's value
        // with that one.
        CallInst *Call = cast<CallInst>(Inst);
        CallInst *EarlierCall = cast<CallInst>(EarlierInst);
        Value *Arg = Call->getArgOperand(0);
        Value *EarlierArg = EarlierCall->getArgOperand(0);
        switch (PA.getAA()->alias(Arg, EarlierArg)) {
        case MustAlias:
          Changed = true;
          // If the load has a builtin retain, insert a plain retain for it.
          if (Class == ARCInstKind::LoadWeakRetained) {
            Constant *Decl = EP.get(ARCRuntimeEntryPointKind::Retain);
            CallInst *CI = CallInst::Create(Decl, EarlierCall, "", Call);
            CI->setTailCall();
          }
          // Zap the fully redundant load.
          Call->replaceAllUsesWith(EarlierCall);
          Call->eraseFromParent();
          goto clobbered;
        case MayAlias:
        case PartialAlias:
          goto clobbered;
        case NoAlias:
          break;
        }
        break;
      }
      case ARCInstKind::StoreWeak:
      case ARCInstKind::InitWeak: {
        // If this is storing to the same pointer and has the same size etc.
        // replace this load's value with the stored value.
        CallInst *Call = cast<CallInst>(Inst);
        CallInst *EarlierCall = cast<CallInst>(EarlierInst);
        Value *Arg = Call->getArgOperand(0);
        Value *EarlierArg = EarlierCall->getArgOperand(0);
        switch (PA.getAA()->alias(Arg, EarlierArg)) {
        case MustAlias:
          Changed = true;
          // If the load has a builtin retain, insert a plain retain for it.
          if (Class == ARCInstKind::LoadWeakRetained) {
            Constant *Decl = EP.get(ARCRuntimeEntryPointKind::Retain);
            CallInst *CI = CallInst::Create(Decl, EarlierCall, "", Call);
            CI->setTailCall();
          }
          // Zap the fully redundant load.
          Call->replaceAllUsesWith(EarlierCall->getArgOperand(1));
          Call->eraseFromParent();
          goto clobbered;
        case MayAlias:
        case PartialAlias:
          goto clobbered;
        case NoAlias:
          break;
        }
        break;
      }
      case ARCInstKind::MoveWeak:
      case ARCInstKind::CopyWeak:
        // TOOD: Grab the copied value.
        goto clobbered;
      case ARCInstKind::AutoreleasepoolPush:
      case ARCInstKind::None:
      case ARCInstKind::IntrinsicUser:
      case ARCInstKind::User:
        // Weak pointers are only modified through the weak entry points
        // (and arbitrary calls, which could call the weak entry points).
        break;
      default:
        // Anything else could modify the weak pointer.
        goto clobbered;
      }
    }
  clobbered:;
  }

  // Then, for each destroyWeak with an alloca operand, check to see if
  // the alloca and all its users can be zapped.
  for (inst_iterator I = inst_begin(&F), E = inst_end(&F); I != E; ) {
    Instruction *Inst = &*I++;
    ARCInstKind Class = GetBasicARCInstKind(Inst);
    if (Class != ARCInstKind::DestroyWeak)
      continue;

    CallInst *Call = cast<CallInst>(Inst);
    Value *Arg = Call->getArgOperand(0);
    if (AllocaInst *Alloca = dyn_cast<AllocaInst>(Arg)) {
      for (User *U : Alloca->users()) {
        const Instruction *UserInst = cast<Instruction>(U);
        switch (GetBasicARCInstKind(UserInst)) {
        case ARCInstKind::InitWeak:
        case ARCInstKind::StoreWeak:
        case ARCInstKind::DestroyWeak:
          continue;
        default:
          goto done;
        }
      }
      Changed = true;
      for (auto UI = Alloca->user_begin(), UE = Alloca->user_end(); UI != UE;) {
        CallInst *UserInst = cast<CallInst>(*UI++);
        switch (GetBasicARCInstKind(UserInst)) {
        case ARCInstKind::InitWeak:
        case ARCInstKind::StoreWeak:
          // These functions return their second argument.
          UserInst->replaceAllUsesWith(UserInst->getArgOperand(1));
          break;
        case ARCInstKind::DestroyWeak:
          // No return value.
          break;
        default:
          llvm_unreachable("alloca really is used!");
        }
        UserInst->eraseFromParent();
      }
      Alloca->eraseFromParent();
    done:;
    }
  }
}

/// Identify program paths which execute sequences of retains and releases which
/// can be eliminated.
bool ObjCARCOpt::OptimizeSequences(Function &F) {
  // Releases, Retains - These are used to store the results of the main flow
  // analysis. These use Value* as the key instead of Instruction* so that the
  // map stays valid when we get around to rewriting code and calls get
  // replaced by arguments.
  DenseMap<Value *, RRInfo> Releases;
  BlotMapVector<Value *, RRInfo> Retains;

  // This is used during the traversal of the function to track the
  // states for each identified object at each block.
  DenseMap<const BasicBlock *, BBState> BBStates;

  // Analyze the CFG of the function, and all instructions.
  bool NestingDetected = Visit(F, BBStates, Retains, Releases);

  // Transform.
  bool AnyPairsCompletelyEliminated = PerformCodePlacement(BBStates, Retains,
                                                           Releases,
                                                           F.getParent());

  return AnyPairsCompletelyEliminated && NestingDetected;
}

/// Check if there is a dependent call earlier that does not have anything in
/// between the Retain and the call that can affect the reference count of their
/// shared pointer argument. Note that Retain need not be in BB.
static bool
HasSafePathToPredecessorCall(const Value *Arg, Instruction *Retain,
                             SmallPtrSetImpl<Instruction *> &DepInsts,
                             SmallPtrSetImpl<const BasicBlock *> &Visited,
                             ProvenanceAnalysis &PA) {
  FindDependencies(CanChangeRetainCount, Arg, Retain->getParent(), Retain,
                   DepInsts, Visited, PA);
  if (DepInsts.size() != 1)
    return false;

  auto *Call = dyn_cast_or_null<CallInst>(*DepInsts.begin());

  // Check that the pointer is the return value of the call.
  if (!Call || Arg != Call)
    return false;

  // Check that the call is a regular call.
  ARCInstKind Class = GetBasicARCInstKind(Call);
  return Class == ARCInstKind::CallOrUser || Class == ARCInstKind::Call;
}

/// Find a dependent retain that precedes the given autorelease for which there
/// is nothing in between the two instructions that can affect the ref count of
/// Arg.
static CallInst *
FindPredecessorRetainWithSafePath(const Value *Arg, BasicBlock *BB,
                                  Instruction *Autorelease,
                                  SmallPtrSetImpl<Instruction *> &DepInsts,
                                  SmallPtrSetImpl<const BasicBlock *> &Visited,
                                  ProvenanceAnalysis &PA) {
  FindDependencies(CanChangeRetainCount, Arg,
                   BB, Autorelease, DepInsts, Visited, PA);
  if (DepInsts.size() != 1)
    return nullptr;

  auto *Retain = dyn_cast_or_null<CallInst>(*DepInsts.begin());

  // Check that we found a retain with the same argument.
  if (!Retain || !IsRetain(GetBasicARCInstKind(Retain)) ||
      GetArgRCIdentityRoot(Retain) != Arg) {
    return nullptr;
  }

  return Retain;
}

/// Look for an ``autorelease'' instruction dependent on Arg such that there are
/// no instructions dependent on Arg that need a positive ref count in between
/// the autorelease and the ret.
static CallInst *
FindPredecessorAutoreleaseWithSafePath(const Value *Arg, BasicBlock *BB,
                                       ReturnInst *Ret,
                                       SmallPtrSetImpl<Instruction *> &DepInsts,
                                       SmallPtrSetImpl<const BasicBlock *> &V,
                                       ProvenanceAnalysis &PA) {
  FindDependencies(NeedsPositiveRetainCount, Arg,
                   BB, Ret, DepInsts, V, PA);
  if (DepInsts.size() != 1)
    return nullptr;

  auto *Autorelease = dyn_cast_or_null<CallInst>(*DepInsts.begin());
  if (!Autorelease)
    return nullptr;
  ARCInstKind AutoreleaseClass = GetBasicARCInstKind(Autorelease);
  if (!IsAutorelease(AutoreleaseClass))
    return nullptr;
  if (GetArgRCIdentityRoot(Autorelease) != Arg)
    return nullptr;

  return Autorelease;
}

/// Look for this pattern:
/// \code
///    %call = call i8* @something(...)
///    %2 = call i8* @objc_retain(i8* %call)
///    %3 = call i8* @objc_autorelease(i8* %2)
///    ret i8* %3
/// \endcode
/// And delete the retain and autorelease.
void ObjCARCOpt::OptimizeReturns(Function &F) {
  if (!F.getReturnType()->isPointerTy())
    return;

  LLVM_DEBUG(dbgs() << "\n== ObjCARCOpt::OptimizeReturns ==\n");

  SmallPtrSet<Instruction *, 4> DependingInstructions;
  SmallPtrSet<const BasicBlock *, 4> Visited;
  for (BasicBlock &BB: F) {
    ReturnInst *Ret = dyn_cast<ReturnInst>(&BB.back());
    if (!Ret)
      continue;

    LLVM_DEBUG(dbgs() << "Visiting: " << *Ret << "\n");

    const Value *Arg = GetRCIdentityRoot(Ret->getOperand(0));

    // Look for an ``autorelease'' instruction that is a predecessor of Ret and
    // dependent on Arg such that there are no instructions dependent on Arg
    // that need a positive ref count in between the autorelease and Ret.
    CallInst *Autorelease = FindPredecessorAutoreleaseWithSafePath(
        Arg, &BB, Ret, DependingInstructions, Visited, PA);
    DependingInstructions.clear();
    Visited.clear();

    if (!Autorelease)
      continue;

    CallInst *Retain = FindPredecessorRetainWithSafePath(
        Arg, Autorelease->getParent(), Autorelease, DependingInstructions,
        Visited, PA);
    DependingInstructions.clear();
    Visited.clear();

    if (!Retain)
      continue;

    // Check that there is nothing that can affect the reference count
    // between the retain and the call.  Note that Retain need not be in BB.
    bool HasSafePathToCall = HasSafePathToPredecessorCall(Arg, Retain,
                                                          DependingInstructions,
                                                          Visited, PA);
    DependingInstructions.clear();
    Visited.clear();

    if (!HasSafePathToCall)
      continue;

    // If so, we can zap the retain and autorelease.
    Changed = true;
    ++NumRets;
    LLVM_DEBUG(dbgs() << "Erasing: " << *Retain << "\nErasing: " << *Autorelease
                      << "\n");
    EraseInstruction(Retain);
    EraseInstruction(Autorelease);
  }
}

#ifndef NDEBUG
void
ObjCARCOpt::GatherStatistics(Function &F, bool AfterOptimization) {
  Statistic &NumRetains =
      AfterOptimization ? NumRetainsAfterOpt : NumRetainsBeforeOpt;
  Statistic &NumReleases =
      AfterOptimization ? NumReleasesAfterOpt : NumReleasesBeforeOpt;

  for (inst_iterator I = inst_begin(&F), E = inst_end(&F); I != E; ) {
    Instruction *Inst = &*I++;
    switch (GetBasicARCInstKind(Inst)) {
    default:
      break;
    case ARCInstKind::Retain:
      ++NumRetains;
      break;
    case ARCInstKind::Release:
      ++NumReleases;
      break;
    }
  }
}
#endif

bool ObjCARCOpt::doInitialization(Module &M) {
  if (!EnableARCOpts)
    return false;

  // If nothing in the Module uses ARC, don't do anything.
  Run = ModuleHasARC(M);
  if (!Run)
    return false;

  // Intuitively, objc_retain and others are nocapture, however in practice
  // they are not, because they return their argument value. And objc_release
  // calls finalizers which can have arbitrary side effects.
  MDKindCache.init(&M);

  // Initialize our runtime entry point cache.
  EP.init(&M);

  return false;
}

bool ObjCARCOpt::runOnFunction(Function &F) {
  if (!EnableARCOpts)
    return false;

  // If nothing in the Module uses ARC, don't do anything.
  if (!Run)
    return false;

  Changed = false;

  LLVM_DEBUG(dbgs() << "<<< ObjCARCOpt: Visiting Function: " << F.getName()
                    << " >>>"
                       "\n");

  PA.setAA(&getAnalysis<AAResultsWrapperPass>().getAAResults());

#ifndef NDEBUG
  if (AreStatisticsEnabled()) {
    GatherStatistics(F, false);
  }
#endif

  // This pass performs several distinct transformations. As a compile-time aid
  // when compiling code that isn't ObjC, skip these if the relevant ObjC
  // library functions aren't declared.

  // Preliminary optimizations. This also computes UsedInThisFunction.
  OptimizeIndividualCalls(F);

  // Optimizations for weak pointers.
  if (UsedInThisFunction & ((1 << unsigned(ARCInstKind::LoadWeak)) |
                            (1 << unsigned(ARCInstKind::LoadWeakRetained)) |
                            (1 << unsigned(ARCInstKind::StoreWeak)) |
                            (1 << unsigned(ARCInstKind::InitWeak)) |
                            (1 << unsigned(ARCInstKind::CopyWeak)) |
                            (1 << unsigned(ARCInstKind::MoveWeak)) |
                            (1 << unsigned(ARCInstKind::DestroyWeak))))
    OptimizeWeakCalls(F);

  // Optimizations for retain+release pairs.
  if (UsedInThisFunction & ((1 << unsigned(ARCInstKind::Retain)) |
                            (1 << unsigned(ARCInstKind::RetainRV)) |
                            (1 << unsigned(ARCInstKind::RetainBlock))))
    if (UsedInThisFunction & (1 << unsigned(ARCInstKind::Release)))
      // Run OptimizeSequences until it either stops making changes or
      // no retain+release pair nesting is detected.
      while (OptimizeSequences(F)) {}

  // Optimizations if objc_autorelease is used.
  if (UsedInThisFunction & ((1 << unsigned(ARCInstKind::Autorelease)) |
                            (1 << unsigned(ARCInstKind::AutoreleaseRV))))
    OptimizeReturns(F);

  // Gather statistics after optimization.
#ifndef NDEBUG
  if (AreStatisticsEnabled()) {
    GatherStatistics(F, true);
  }
#endif

  LLVM_DEBUG(dbgs() << "\n");

  return Changed;
}

void ObjCARCOpt::releaseMemory() {
  PA.clear();
}

/// @}
///
