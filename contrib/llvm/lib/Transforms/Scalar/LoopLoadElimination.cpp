//===- LoopLoadElimination.cpp - Loop Load Elimination Pass ---------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file implement a loop-aware load elimination pass.
//
// It uses LoopAccessAnalysis to identify loop-carried dependences with a
// distance of one between stores and loads.  These form the candidates for the
// transformation.  The source value of each store then propagated to the user
// of the corresponding load.  This makes the load dead.
//
// The pass can also version the loop and add memchecks in order to prove that
// may-aliasing stores can't change the value in memory before it's read by the
// load.
//
//===----------------------------------------------------------------------===//

#include "llvm/Transforms/Scalar/LoopLoadElimination.h"
#include "llvm/ADT/APInt.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/DepthFirstIterator.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/Analysis/AliasAnalysis.h"
#include "llvm/Analysis/AssumptionCache.h"
#include "llvm/Analysis/GlobalsModRef.h"
#include "llvm/Analysis/LoopAccessAnalysis.h"
#include "llvm/Analysis/LoopAnalysisManager.h"
#include "llvm/Analysis/LoopInfo.h"
#include "llvm/Analysis/ScalarEvolution.h"
#include "llvm/Analysis/ScalarEvolutionExpander.h"
#include "llvm/Analysis/ScalarEvolutionExpressions.h"
#include "llvm/Analysis/TargetLibraryInfo.h"
#include "llvm/Analysis/TargetTransformInfo.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/IR/Dominators.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/PassManager.h"
#include "llvm/IR/Type.h"
#include "llvm/IR/Value.h"
#include "llvm/Pass.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Transforms/Scalar.h"
#include "llvm/Transforms/Utils.h"
#include "llvm/Transforms/Utils/LoopVersioning.h"
#include <algorithm>
#include <cassert>
#include <forward_list>
#include <set>
#include <tuple>
#include <utility>

using namespace llvm;

#define LLE_OPTION "loop-load-elim"
#define DEBUG_TYPE LLE_OPTION

static cl::opt<unsigned> CheckPerElim(
    "runtime-check-per-loop-load-elim", cl::Hidden,
    cl::desc("Max number of memchecks allowed per eliminated load on average"),
    cl::init(1));

static cl::opt<unsigned> LoadElimSCEVCheckThreshold(
    "loop-load-elimination-scev-check-threshold", cl::init(8), cl::Hidden,
    cl::desc("The maximum number of SCEV checks allowed for Loop "
             "Load Elimination"));

STATISTIC(NumLoopLoadEliminted, "Number of loads eliminated by LLE");

namespace {

/// Represent a store-to-forwarding candidate.
struct StoreToLoadForwardingCandidate {
  LoadInst *Load;
  StoreInst *Store;

  StoreToLoadForwardingCandidate(LoadInst *Load, StoreInst *Store)
      : Load(Load), Store(Store) {}

  /// Return true if the dependence from the store to the load has a
  /// distance of one.  E.g. A[i+1] = A[i]
  bool isDependenceDistanceOfOne(PredicatedScalarEvolution &PSE,
                                 Loop *L) const {
    Value *LoadPtr = Load->getPointerOperand();
    Value *StorePtr = Store->getPointerOperand();
    Type *LoadPtrType = LoadPtr->getType();
    Type *LoadType = LoadPtrType->getPointerElementType();

    assert(LoadPtrType->getPointerAddressSpace() ==
               StorePtr->getType()->getPointerAddressSpace() &&
           LoadType == StorePtr->getType()->getPointerElementType() &&
           "Should be a known dependence");

    // Currently we only support accesses with unit stride.  FIXME: we should be
    // able to handle non unit stirde as well as long as the stride is equal to
    // the dependence distance.
    if (getPtrStride(PSE, LoadPtr, L) != 1 ||
        getPtrStride(PSE, StorePtr, L) != 1)
      return false;

    auto &DL = Load->getParent()->getModule()->getDataLayout();
    unsigned TypeByteSize = DL.getTypeAllocSize(const_cast<Type *>(LoadType));

    auto *LoadPtrSCEV = cast<SCEVAddRecExpr>(PSE.getSCEV(LoadPtr));
    auto *StorePtrSCEV = cast<SCEVAddRecExpr>(PSE.getSCEV(StorePtr));

    // We don't need to check non-wrapping here because forward/backward
    // dependence wouldn't be valid if these weren't monotonic accesses.
    auto *Dist = cast<SCEVConstant>(
        PSE.getSE()->getMinusSCEV(StorePtrSCEV, LoadPtrSCEV));
    const APInt &Val = Dist->getAPInt();
    return Val == TypeByteSize;
  }

  Value *getLoadPtr() const { return Load->getPointerOperand(); }

#ifndef NDEBUG
  friend raw_ostream &operator<<(raw_ostream &OS,
                                 const StoreToLoadForwardingCandidate &Cand) {
    OS << *Cand.Store << " -->\n";
    OS.indent(2) << *Cand.Load << "\n";
    return OS;
  }
#endif
};

} // end anonymous namespace

/// Check if the store dominates all latches, so as long as there is no
/// intervening store this value will be loaded in the next iteration.
static bool doesStoreDominatesAllLatches(BasicBlock *StoreBlock, Loop *L,
                                         DominatorTree *DT) {
  SmallVector<BasicBlock *, 8> Latches;
  L->getLoopLatches(Latches);
  return llvm::all_of(Latches, [&](const BasicBlock *Latch) {
    return DT->dominates(StoreBlock, Latch);
  });
}

/// Return true if the load is not executed on all paths in the loop.
static bool isLoadConditional(LoadInst *Load, Loop *L) {
  return Load->getParent() != L->getHeader();
}

namespace {

/// The per-loop class that does most of the work.
class LoadEliminationForLoop {
public:
  LoadEliminationForLoop(Loop *L, LoopInfo *LI, const LoopAccessInfo &LAI,
                         DominatorTree *DT)
      : L(L), LI(LI), LAI(LAI), DT(DT), PSE(LAI.getPSE()) {}

  /// Look through the loop-carried and loop-independent dependences in
  /// this loop and find store->load dependences.
  ///
  /// Note that no candidate is returned if LAA has failed to analyze the loop
  /// (e.g. if it's not bottom-tested, contains volatile memops, etc.)
  std::forward_list<StoreToLoadForwardingCandidate>
  findStoreToLoadDependences(const LoopAccessInfo &LAI) {
    std::forward_list<StoreToLoadForwardingCandidate> Candidates;

    const auto *Deps = LAI.getDepChecker().getDependences();
    if (!Deps)
      return Candidates;

    // Find store->load dependences (consequently true dep).  Both lexically
    // forward and backward dependences qualify.  Disqualify loads that have
    // other unknown dependences.

    SmallPtrSet<Instruction *, 4> LoadsWithUnknownDepedence;

    for (const auto &Dep : *Deps) {
      Instruction *Source = Dep.getSource(LAI);
      Instruction *Destination = Dep.getDestination(LAI);

      if (Dep.Type == MemoryDepChecker::Dependence::Unknown) {
        if (isa<LoadInst>(Source))
          LoadsWithUnknownDepedence.insert(Source);
        if (isa<LoadInst>(Destination))
          LoadsWithUnknownDepedence.insert(Destination);
        continue;
      }

      if (Dep.isBackward())
        // Note that the designations source and destination follow the program
        // order, i.e. source is always first.  (The direction is given by the
        // DepType.)
        std::swap(Source, Destination);
      else
        assert(Dep.isForward() && "Needs to be a forward dependence");

      auto *Store = dyn_cast<StoreInst>(Source);
      if (!Store)
        continue;
      auto *Load = dyn_cast<LoadInst>(Destination);
      if (!Load)
        continue;

      // Only progagate the value if they are of the same type.
      if (Store->getPointerOperandType() != Load->getPointerOperandType())
        continue;

      Candidates.emplace_front(Load, Store);
    }

    if (!LoadsWithUnknownDepedence.empty())
      Candidates.remove_if([&](const StoreToLoadForwardingCandidate &C) {
        return LoadsWithUnknownDepedence.count(C.Load);
      });

    return Candidates;
  }

  /// Return the index of the instruction according to program order.
  unsigned getInstrIndex(Instruction *Inst) {
    auto I = InstOrder.find(Inst);
    assert(I != InstOrder.end() && "No index for instruction");
    return I->second;
  }

  /// If a load has multiple candidates associated (i.e. different
  /// stores), it means that it could be forwarding from multiple stores
  /// depending on control flow.  Remove these candidates.
  ///
  /// Here, we rely on LAA to include the relevant loop-independent dependences.
  /// LAA is known to omit these in the very simple case when the read and the
  /// write within an alias set always takes place using the *same* pointer.
  ///
  /// However, we know that this is not the case here, i.e. we can rely on LAA
  /// to provide us with loop-independent dependences for the cases we're
  /// interested.  Consider the case for example where a loop-independent
  /// dependece S1->S2 invalidates the forwarding S3->S2.
  ///
  ///         A[i]   = ...   (S1)
  ///         ...    = A[i]  (S2)
  ///         A[i+1] = ...   (S3)
  ///
  /// LAA will perform dependence analysis here because there are two
  /// *different* pointers involved in the same alias set (&A[i] and &A[i+1]).
  void removeDependencesFromMultipleStores(
      std::forward_list<StoreToLoadForwardingCandidate> &Candidates) {
    // If Store is nullptr it means that we have multiple stores forwarding to
    // this store.
    using LoadToSingleCandT =
        DenseMap<LoadInst *, const StoreToLoadForwardingCandidate *>;
    LoadToSingleCandT LoadToSingleCand;

    for (const auto &Cand : Candidates) {
      bool NewElt;
      LoadToSingleCandT::iterator Iter;

      std::tie(Iter, NewElt) =
          LoadToSingleCand.insert(std::make_pair(Cand.Load, &Cand));
      if (!NewElt) {
        const StoreToLoadForwardingCandidate *&OtherCand = Iter->second;
        // Already multiple stores forward to this load.
        if (OtherCand == nullptr)
          continue;

        // Handle the very basic case when the two stores are in the same block
        // so deciding which one forwards is easy.  The later one forwards as
        // long as they both have a dependence distance of one to the load.
        if (Cand.Store->getParent() == OtherCand->Store->getParent() &&
            Cand.isDependenceDistanceOfOne(PSE, L) &&
            OtherCand->isDependenceDistanceOfOne(PSE, L)) {
          // They are in the same block, the later one will forward to the load.
          if (getInstrIndex(OtherCand->Store) < getInstrIndex(Cand.Store))
            OtherCand = &Cand;
        } else
          OtherCand = nullptr;
      }
    }

    Candidates.remove_if([&](const StoreToLoadForwardingCandidate &Cand) {
      if (LoadToSingleCand[Cand.Load] != &Cand) {
        LLVM_DEBUG(
            dbgs() << "Removing from candidates: \n"
                   << Cand
                   << "  The load may have multiple stores forwarding to "
                   << "it\n");
        return true;
      }
      return false;
    });
  }

  /// Given two pointers operations by their RuntimePointerChecking
  /// indices, return true if they require an alias check.
  ///
  /// We need a check if one is a pointer for a candidate load and the other is
  /// a pointer for a possibly intervening store.
  bool needsChecking(unsigned PtrIdx1, unsigned PtrIdx2,
                     const SmallPtrSet<Value *, 4> &PtrsWrittenOnFwdingPath,
                     const std::set<Value *> &CandLoadPtrs) {
    Value *Ptr1 =
        LAI.getRuntimePointerChecking()->getPointerInfo(PtrIdx1).PointerValue;
    Value *Ptr2 =
        LAI.getRuntimePointerChecking()->getPointerInfo(PtrIdx2).PointerValue;
    return ((PtrsWrittenOnFwdingPath.count(Ptr1) && CandLoadPtrs.count(Ptr2)) ||
            (PtrsWrittenOnFwdingPath.count(Ptr2) && CandLoadPtrs.count(Ptr1)));
  }

  /// Return pointers that are possibly written to on the path from a
  /// forwarding store to a load.
  ///
  /// These pointers need to be alias-checked against the forwarding candidates.
  SmallPtrSet<Value *, 4> findPointersWrittenOnForwardingPath(
      const SmallVectorImpl<StoreToLoadForwardingCandidate> &Candidates) {
    // From FirstStore to LastLoad neither of the elimination candidate loads
    // should overlap with any of the stores.
    //
    // E.g.:
    //
    // st1 C[i]
    // ld1 B[i] <-------,
    // ld0 A[i] <----,  |              * LastLoad
    // ...           |  |
    // st2 E[i]      |  |
    // st3 B[i+1] -- | -'              * FirstStore
    // st0 A[i+1] ---'
    // st4 D[i]
    //
    // st0 forwards to ld0 if the accesses in st4 and st1 don't overlap with
    // ld0.

    LoadInst *LastLoad =
        std::max_element(Candidates.begin(), Candidates.end(),
                         [&](const StoreToLoadForwardingCandidate &A,
                             const StoreToLoadForwardingCandidate &B) {
                           return getInstrIndex(A.Load) < getInstrIndex(B.Load);
                         })
            ->Load;
    StoreInst *FirstStore =
        std::min_element(Candidates.begin(), Candidates.end(),
                         [&](const StoreToLoadForwardingCandidate &A,
                             const StoreToLoadForwardingCandidate &B) {
                           return getInstrIndex(A.Store) <
                                  getInstrIndex(B.Store);
                         })
            ->Store;

    // We're looking for stores after the first forwarding store until the end
    // of the loop, then from the beginning of the loop until the last
    // forwarded-to load.  Collect the pointer for the stores.
    SmallPtrSet<Value *, 4> PtrsWrittenOnFwdingPath;

    auto InsertStorePtr = [&](Instruction *I) {
      if (auto *S = dyn_cast<StoreInst>(I))
        PtrsWrittenOnFwdingPath.insert(S->getPointerOperand());
    };
    const auto &MemInstrs = LAI.getDepChecker().getMemoryInstructions();
    std::for_each(MemInstrs.begin() + getInstrIndex(FirstStore) + 1,
                  MemInstrs.end(), InsertStorePtr);
    std::for_each(MemInstrs.begin(), &MemInstrs[getInstrIndex(LastLoad)],
                  InsertStorePtr);

    return PtrsWrittenOnFwdingPath;
  }

  /// Determine the pointer alias checks to prove that there are no
  /// intervening stores.
  SmallVector<RuntimePointerChecking::PointerCheck, 4> collectMemchecks(
      const SmallVectorImpl<StoreToLoadForwardingCandidate> &Candidates) {

    SmallPtrSet<Value *, 4> PtrsWrittenOnFwdingPath =
        findPointersWrittenOnForwardingPath(Candidates);

    // Collect the pointers of the candidate loads.
    // FIXME: SmallPtrSet does not work with std::inserter.
    std::set<Value *> CandLoadPtrs;
    transform(Candidates,
                   std::inserter(CandLoadPtrs, CandLoadPtrs.begin()),
                   std::mem_fn(&StoreToLoadForwardingCandidate::getLoadPtr));

    const auto &AllChecks = LAI.getRuntimePointerChecking()->getChecks();
    SmallVector<RuntimePointerChecking::PointerCheck, 4> Checks;

    copy_if(AllChecks, std::back_inserter(Checks),
            [&](const RuntimePointerChecking::PointerCheck &Check) {
              for (auto PtrIdx1 : Check.first->Members)
                for (auto PtrIdx2 : Check.second->Members)
                  if (needsChecking(PtrIdx1, PtrIdx2, PtrsWrittenOnFwdingPath,
                                    CandLoadPtrs))
                    return true;
              return false;
            });

    LLVM_DEBUG(dbgs() << "\nPointer Checks (count: " << Checks.size()
                      << "):\n");
    LLVM_DEBUG(LAI.getRuntimePointerChecking()->printChecks(dbgs(), Checks));

    return Checks;
  }

  /// Perform the transformation for a candidate.
  void
  propagateStoredValueToLoadUsers(const StoreToLoadForwardingCandidate &Cand,
                                  SCEVExpander &SEE) {
    // loop:
    //      %x = load %gep_i
    //         = ... %x
    //      store %y, %gep_i_plus_1
    //
    // =>
    //
    // ph:
    //      %x.initial = load %gep_0
    // loop:
    //      %x.storeforward = phi [%x.initial, %ph] [%y, %loop]
    //      %x = load %gep_i            <---- now dead
    //         = ... %x.storeforward
    //      store %y, %gep_i_plus_1

    Value *Ptr = Cand.Load->getPointerOperand();
    auto *PtrSCEV = cast<SCEVAddRecExpr>(PSE.getSCEV(Ptr));
    auto *PH = L->getLoopPreheader();
    Value *InitialPtr = SEE.expandCodeFor(PtrSCEV->getStart(), Ptr->getType(),
                                          PH->getTerminator());
    Value *Initial =
        new LoadInst(InitialPtr, "load_initial", /* isVolatile */ false,
                     Cand.Load->getAlignment(), PH->getTerminator());

    PHINode *PHI = PHINode::Create(Initial->getType(), 2, "store_forwarded",
                                   &L->getHeader()->front());
    PHI->addIncoming(Initial, PH);
    PHI->addIncoming(Cand.Store->getOperand(0), L->getLoopLatch());

    Cand.Load->replaceAllUsesWith(PHI);
  }

  /// Top-level driver for each loop: find store->load forwarding
  /// candidates, add run-time checks and perform transformation.
  bool processLoop() {
    LLVM_DEBUG(dbgs() << "\nIn \"" << L->getHeader()->getParent()->getName()
                      << "\" checking " << *L << "\n");

    // Look for store-to-load forwarding cases across the
    // backedge. E.g.:
    //
    // loop:
    //      %x = load %gep_i
    //         = ... %x
    //      store %y, %gep_i_plus_1
    //
    // =>
    //
    // ph:
    //      %x.initial = load %gep_0
    // loop:
    //      %x.storeforward = phi [%x.initial, %ph] [%y, %loop]
    //      %x = load %gep_i            <---- now dead
    //         = ... %x.storeforward
    //      store %y, %gep_i_plus_1

    // First start with store->load dependences.
    auto StoreToLoadDependences = findStoreToLoadDependences(LAI);
    if (StoreToLoadDependences.empty())
      return false;

    // Generate an index for each load and store according to the original
    // program order.  This will be used later.
    InstOrder = LAI.getDepChecker().generateInstructionOrderMap();

    // To keep things simple for now, remove those where the load is potentially
    // fed by multiple stores.
    removeDependencesFromMultipleStores(StoreToLoadDependences);
    if (StoreToLoadDependences.empty())
      return false;

    // Filter the candidates further.
    SmallVector<StoreToLoadForwardingCandidate, 4> Candidates;
    unsigned NumForwarding = 0;
    for (const StoreToLoadForwardingCandidate Cand : StoreToLoadDependences) {
      LLVM_DEBUG(dbgs() << "Candidate " << Cand);

      // Make sure that the stored values is available everywhere in the loop in
      // the next iteration.
      if (!doesStoreDominatesAllLatches(Cand.Store->getParent(), L, DT))
        continue;

      // If the load is conditional we can't hoist its 0-iteration instance to
      // the preheader because that would make it unconditional.  Thus we would
      // access a memory location that the original loop did not access.
      if (isLoadConditional(Cand.Load, L))
        continue;

      // Check whether the SCEV difference is the same as the induction step,
      // thus we load the value in the next iteration.
      if (!Cand.isDependenceDistanceOfOne(PSE, L))
        continue;

      ++NumForwarding;
      LLVM_DEBUG(
          dbgs()
          << NumForwarding
          << ". Valid store-to-load forwarding across the loop backedge\n");
      Candidates.push_back(Cand);
    }
    if (Candidates.empty())
      return false;

    // Check intervening may-alias stores.  These need runtime checks for alias
    // disambiguation.
    SmallVector<RuntimePointerChecking::PointerCheck, 4> Checks =
        collectMemchecks(Candidates);

    // Too many checks are likely to outweigh the benefits of forwarding.
    if (Checks.size() > Candidates.size() * CheckPerElim) {
      LLVM_DEBUG(dbgs() << "Too many run-time checks needed.\n");
      return false;
    }

    if (LAI.getPSE().getUnionPredicate().getComplexity() >
        LoadElimSCEVCheckThreshold) {
      LLVM_DEBUG(dbgs() << "Too many SCEV run-time checks needed.\n");
      return false;
    }

    if (!Checks.empty() || !LAI.getPSE().getUnionPredicate().isAlwaysTrue()) {
      if (L->getHeader()->getParent()->optForSize()) {
        LLVM_DEBUG(
            dbgs() << "Versioning is needed but not allowed when optimizing "
                      "for size.\n");
        return false;
      }

      if (!L->isLoopSimplifyForm()) {
        LLVM_DEBUG(dbgs() << "Loop is not is loop-simplify form");
        return false;
      }

      // Point of no-return, start the transformation.  First, version the loop
      // if necessary.

      LoopVersioning LV(LAI, L, LI, DT, PSE.getSE(), false);
      LV.setAliasChecks(std::move(Checks));
      LV.setSCEVChecks(LAI.getPSE().getUnionPredicate());
      LV.versionLoop();
    }

    // Next, propagate the value stored by the store to the users of the load.
    // Also for the first iteration, generate the initial value of the load.
    SCEVExpander SEE(*PSE.getSE(), L->getHeader()->getModule()->getDataLayout(),
                     "storeforward");
    for (const auto &Cand : Candidates)
      propagateStoredValueToLoadUsers(Cand, SEE);
    NumLoopLoadEliminted += NumForwarding;

    return true;
  }

private:
  Loop *L;

  /// Maps the load/store instructions to their index according to
  /// program order.
  DenseMap<Instruction *, unsigned> InstOrder;

  // Analyses used.
  LoopInfo *LI;
  const LoopAccessInfo &LAI;
  DominatorTree *DT;
  PredicatedScalarEvolution PSE;
};

} // end anonymous namespace

static bool
eliminateLoadsAcrossLoops(Function &F, LoopInfo &LI, DominatorTree &DT,
                          function_ref<const LoopAccessInfo &(Loop &)> GetLAI) {
  // Build up a worklist of inner-loops to transform to avoid iterator
  // invalidation.
  // FIXME: This logic comes from other passes that actually change the loop
  // nest structure. It isn't clear this is necessary (or useful) for a pass
  // which merely optimizes the use of loads in a loop.
  SmallVector<Loop *, 8> Worklist;

  for (Loop *TopLevelLoop : LI)
    for (Loop *L : depth_first(TopLevelLoop))
      // We only handle inner-most loops.
      if (L->empty())
        Worklist.push_back(L);

  // Now walk the identified inner loops.
  bool Changed = false;
  for (Loop *L : Worklist) {
    // The actual work is performed by LoadEliminationForLoop.
    LoadEliminationForLoop LEL(L, &LI, GetLAI(*L), &DT);
    Changed |= LEL.processLoop();
  }
  return Changed;
}

namespace {

/// The pass.  Most of the work is delegated to the per-loop
/// LoadEliminationForLoop class.
class LoopLoadElimination : public FunctionPass {
public:
  static char ID;

  LoopLoadElimination() : FunctionPass(ID) {
    initializeLoopLoadEliminationPass(*PassRegistry::getPassRegistry());
  }

  bool runOnFunction(Function &F) override {
    if (skipFunction(F))
      return false;

    auto &LI = getAnalysis<LoopInfoWrapperPass>().getLoopInfo();
    auto &LAA = getAnalysis<LoopAccessLegacyAnalysis>();
    auto &DT = getAnalysis<DominatorTreeWrapperPass>().getDomTree();

    // Process each loop nest in the function.
    return eliminateLoadsAcrossLoops(
        F, LI, DT,
        [&LAA](Loop &L) -> const LoopAccessInfo & { return LAA.getInfo(&L); });
  }

  void getAnalysisUsage(AnalysisUsage &AU) const override {
    AU.addRequiredID(LoopSimplifyID);
    AU.addRequired<LoopInfoWrapperPass>();
    AU.addPreserved<LoopInfoWrapperPass>();
    AU.addRequired<LoopAccessLegacyAnalysis>();
    AU.addRequired<ScalarEvolutionWrapperPass>();
    AU.addRequired<DominatorTreeWrapperPass>();
    AU.addPreserved<DominatorTreeWrapperPass>();
    AU.addPreserved<GlobalsAAWrapperPass>();
  }
};

} // end anonymous namespace

char LoopLoadElimination::ID;

static const char LLE_name[] = "Loop Load Elimination";

INITIALIZE_PASS_BEGIN(LoopLoadElimination, LLE_OPTION, LLE_name, false, false)
INITIALIZE_PASS_DEPENDENCY(LoopInfoWrapperPass)
INITIALIZE_PASS_DEPENDENCY(LoopAccessLegacyAnalysis)
INITIALIZE_PASS_DEPENDENCY(DominatorTreeWrapperPass)
INITIALIZE_PASS_DEPENDENCY(ScalarEvolutionWrapperPass)
INITIALIZE_PASS_DEPENDENCY(LoopSimplify)
INITIALIZE_PASS_END(LoopLoadElimination, LLE_OPTION, LLE_name, false, false)

FunctionPass *llvm::createLoopLoadEliminationPass() {
  return new LoopLoadElimination();
}

PreservedAnalyses LoopLoadEliminationPass::run(Function &F,
                                               FunctionAnalysisManager &AM) {
  auto &SE = AM.getResult<ScalarEvolutionAnalysis>(F);
  auto &LI = AM.getResult<LoopAnalysis>(F);
  auto &TTI = AM.getResult<TargetIRAnalysis>(F);
  auto &DT = AM.getResult<DominatorTreeAnalysis>(F);
  auto &TLI = AM.getResult<TargetLibraryAnalysis>(F);
  auto &AA = AM.getResult<AAManager>(F);
  auto &AC = AM.getResult<AssumptionAnalysis>(F);

  auto &LAM = AM.getResult<LoopAnalysisManagerFunctionProxy>(F).getManager();
  bool Changed = eliminateLoadsAcrossLoops(
      F, LI, DT, [&](Loop &L) -> const LoopAccessInfo & {
        LoopStandardAnalysisResults AR = {AA, AC,  DT,  LI,
                                          SE, TLI, TTI, nullptr};
        return LAM.getResult<LoopAccessAnalysis>(L, AR);
      });

  if (!Changed)
    return PreservedAnalyses::all();

  PreservedAnalyses PA;
  return PA;
}
