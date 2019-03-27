//===- LoopReroll.cpp - Loop rerolling pass -------------------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This pass implements a simple loop reroller.
//
//===----------------------------------------------------------------------===//

#include "llvm/ADT/APInt.h"
#include "llvm/ADT/BitVector.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/DenseSet.h"
#include "llvm/ADT/MapVector.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/Analysis/AliasAnalysis.h"
#include "llvm/Analysis/AliasSetTracker.h"
#include "llvm/Analysis/LoopInfo.h"
#include "llvm/Analysis/LoopPass.h"
#include "llvm/Analysis/ScalarEvolution.h"
#include "llvm/Analysis/ScalarEvolutionExpander.h"
#include "llvm/Analysis/ScalarEvolutionExpressions.h"
#include "llvm/Analysis/TargetLibraryInfo.h"
#include "llvm/Transforms/Utils/Local.h"
#include "llvm/Analysis/ValueTracking.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/Dominators.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/InstrTypes.h"
#include "llvm/IR/Instruction.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/IntrinsicInst.h"
#include "llvm/IR/Intrinsics.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Type.h"
#include "llvm/IR/Use.h"
#include "llvm/IR/User.h"
#include "llvm/IR/Value.h"
#include "llvm/Pass.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Transforms/Scalar.h"
#include "llvm/Transforms/Utils.h"
#include "llvm/Transforms/Utils/BasicBlockUtils.h"
#include "llvm/Transforms/Utils/LoopUtils.h"
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <iterator>
#include <map>
#include <utility>

using namespace llvm;

#define DEBUG_TYPE "loop-reroll"

STATISTIC(NumRerolledLoops, "Number of rerolled loops");

static cl::opt<unsigned>
NumToleratedFailedMatches("reroll-num-tolerated-failed-matches", cl::init(400),
                          cl::Hidden,
                          cl::desc("The maximum number of failures to tolerate"
                                   " during fuzzy matching. (default: 400)"));

// This loop re-rolling transformation aims to transform loops like this:
//
// int foo(int a);
// void bar(int *x) {
//   for (int i = 0; i < 500; i += 3) {
//     foo(i);
//     foo(i+1);
//     foo(i+2);
//   }
// }
//
// into a loop like this:
//
// void bar(int *x) {
//   for (int i = 0; i < 500; ++i)
//     foo(i);
// }
//
// It does this by looking for loops that, besides the latch code, are composed
// of isomorphic DAGs of instructions, with each DAG rooted at some increment
// to the induction variable, and where each DAG is isomorphic to the DAG
// rooted at the induction variable (excepting the sub-DAGs which root the
// other induction-variable increments). In other words, we're looking for loop
// bodies of the form:
//
// %iv = phi [ (preheader, ...), (body, %iv.next) ]
// f(%iv)
// %iv.1 = add %iv, 1                <-- a root increment
// f(%iv.1)
// %iv.2 = add %iv, 2                <-- a root increment
// f(%iv.2)
// %iv.scale_m_1 = add %iv, scale-1  <-- a root increment
// f(%iv.scale_m_1)
// ...
// %iv.next = add %iv, scale
// %cmp = icmp(%iv, ...)
// br %cmp, header, exit
//
// where each f(i) is a set of instructions that, collectively, are a function
// only of i (and other loop-invariant values).
//
// As a special case, we can also reroll loops like this:
//
// int foo(int);
// void bar(int *x) {
//   for (int i = 0; i < 500; ++i) {
//     x[3*i] = foo(0);
//     x[3*i+1] = foo(0);
//     x[3*i+2] = foo(0);
//   }
// }
//
// into this:
//
// void bar(int *x) {
//   for (int i = 0; i < 1500; ++i)
//     x[i] = foo(0);
// }
//
// in which case, we're looking for inputs like this:
//
// %iv = phi [ (preheader, ...), (body, %iv.next) ]
// %scaled.iv = mul %iv, scale
// f(%scaled.iv)
// %scaled.iv.1 = add %scaled.iv, 1
// f(%scaled.iv.1)
// %scaled.iv.2 = add %scaled.iv, 2
// f(%scaled.iv.2)
// %scaled.iv.scale_m_1 = add %scaled.iv, scale-1
// f(%scaled.iv.scale_m_1)
// ...
// %iv.next = add %iv, 1
// %cmp = icmp(%iv, ...)
// br %cmp, header, exit

namespace {

  enum IterationLimits {
    /// The maximum number of iterations that we'll try and reroll.
    IL_MaxRerollIterations = 32,
    /// The bitvector index used by loop induction variables and other
    /// instructions that belong to all iterations.
    IL_All,
    IL_End
  };

  class LoopReroll : public LoopPass {
  public:
    static char ID; // Pass ID, replacement for typeid

    LoopReroll() : LoopPass(ID) {
      initializeLoopRerollPass(*PassRegistry::getPassRegistry());
    }

    bool runOnLoop(Loop *L, LPPassManager &LPM) override;

    void getAnalysisUsage(AnalysisUsage &AU) const override {
      AU.addRequired<TargetLibraryInfoWrapperPass>();
      getLoopAnalysisUsage(AU);
    }

  protected:
    AliasAnalysis *AA;
    LoopInfo *LI;
    ScalarEvolution *SE;
    TargetLibraryInfo *TLI;
    DominatorTree *DT;
    bool PreserveLCSSA;

    using SmallInstructionVector = SmallVector<Instruction *, 16>;
    using SmallInstructionSet = SmallPtrSet<Instruction *, 16>;

    // Map between induction variable and its increment
    DenseMap<Instruction *, int64_t> IVToIncMap;

    // For loop with multiple induction variable, remember the one used only to
    // control the loop.
    Instruction *LoopControlIV;

    // A chain of isomorphic instructions, identified by a single-use PHI
    // representing a reduction. Only the last value may be used outside the
    // loop.
    struct SimpleLoopReduction {
      SimpleLoopReduction(Instruction *P, Loop *L) : Instructions(1, P) {
        assert(isa<PHINode>(P) && "First reduction instruction must be a PHI");
        add(L);
      }

      bool valid() const {
        return Valid;
      }

      Instruction *getPHI() const {
        assert(Valid && "Using invalid reduction");
        return Instructions.front();
      }

      Instruction *getReducedValue() const {
        assert(Valid && "Using invalid reduction");
        return Instructions.back();
      }

      Instruction *get(size_t i) const {
        assert(Valid && "Using invalid reduction");
        return Instructions[i+1];
      }

      Instruction *operator [] (size_t i) const { return get(i); }

      // The size, ignoring the initial PHI.
      size_t size() const {
        assert(Valid && "Using invalid reduction");
        return Instructions.size()-1;
      }

      using iterator = SmallInstructionVector::iterator;
      using const_iterator = SmallInstructionVector::const_iterator;

      iterator begin() {
        assert(Valid && "Using invalid reduction");
        return std::next(Instructions.begin());
      }

      const_iterator begin() const {
        assert(Valid && "Using invalid reduction");
        return std::next(Instructions.begin());
      }

      iterator end() { return Instructions.end(); }
      const_iterator end() const { return Instructions.end(); }

    protected:
      bool Valid = false;
      SmallInstructionVector Instructions;

      void add(Loop *L);
    };

    // The set of all reductions, and state tracking of possible reductions
    // during loop instruction processing.
    struct ReductionTracker {
      using SmallReductionVector = SmallVector<SimpleLoopReduction, 16>;

      // Add a new possible reduction.
      void addSLR(SimpleLoopReduction &SLR) { PossibleReds.push_back(SLR); }

      // Setup to track possible reductions corresponding to the provided
      // rerolling scale. Only reductions with a number of non-PHI instructions
      // that is divisible by the scale are considered. Three instructions sets
      // are filled in:
      //   - A set of all possible instructions in eligible reductions.
      //   - A set of all PHIs in eligible reductions
      //   - A set of all reduced values (last instructions) in eligible
      //     reductions.
      void restrictToScale(uint64_t Scale,
                           SmallInstructionSet &PossibleRedSet,
                           SmallInstructionSet &PossibleRedPHISet,
                           SmallInstructionSet &PossibleRedLastSet) {
        PossibleRedIdx.clear();
        PossibleRedIter.clear();
        Reds.clear();

        for (unsigned i = 0, e = PossibleReds.size(); i != e; ++i)
          if (PossibleReds[i].size() % Scale == 0) {
            PossibleRedLastSet.insert(PossibleReds[i].getReducedValue());
            PossibleRedPHISet.insert(PossibleReds[i].getPHI());

            PossibleRedSet.insert(PossibleReds[i].getPHI());
            PossibleRedIdx[PossibleReds[i].getPHI()] = i;
            for (Instruction *J : PossibleReds[i]) {
              PossibleRedSet.insert(J);
              PossibleRedIdx[J] = i;
            }
          }
      }

      // The functions below are used while processing the loop instructions.

      // Are the two instructions both from reductions, and furthermore, from
      // the same reduction?
      bool isPairInSame(Instruction *J1, Instruction *J2) {
        DenseMap<Instruction *, int>::iterator J1I = PossibleRedIdx.find(J1);
        if (J1I != PossibleRedIdx.end()) {
          DenseMap<Instruction *, int>::iterator J2I = PossibleRedIdx.find(J2);
          if (J2I != PossibleRedIdx.end() && J1I->second == J2I->second)
            return true;
        }

        return false;
      }

      // The two provided instructions, the first from the base iteration, and
      // the second from iteration i, form a matched pair. If these are part of
      // a reduction, record that fact.
      void recordPair(Instruction *J1, Instruction *J2, unsigned i) {
        if (PossibleRedIdx.count(J1)) {
          assert(PossibleRedIdx.count(J2) &&
                 "Recording reduction vs. non-reduction instruction?");

          PossibleRedIter[J1] = 0;
          PossibleRedIter[J2] = i;

          int Idx = PossibleRedIdx[J1];
          assert(Idx == PossibleRedIdx[J2] &&
                 "Recording pair from different reductions?");
          Reds.insert(Idx);
        }
      }

      // The functions below can be called after we've finished processing all
      // instructions in the loop, and we know which reductions were selected.

      bool validateSelected();
      void replaceSelected();

    protected:
      // The vector of all possible reductions (for any scale).
      SmallReductionVector PossibleReds;

      DenseMap<Instruction *, int> PossibleRedIdx;
      DenseMap<Instruction *, int> PossibleRedIter;
      DenseSet<int> Reds;
    };

    // A DAGRootSet models an induction variable being used in a rerollable
    // loop. For example,
    //
    //   x[i*3+0] = y1
    //   x[i*3+1] = y2
    //   x[i*3+2] = y3
    //
    //   Base instruction -> i*3
    //                    +---+----+
    //                   /    |     \
    //               ST[y1]  +1     +2  <-- Roots
    //                        |      |
    //                      ST[y2] ST[y3]
    //
    // There may be multiple DAGRoots, for example:
    //
    //   x[i*2+0] = ...   (1)
    //   x[i*2+1] = ...   (1)
    //   x[i*2+4] = ...   (2)
    //   x[i*2+5] = ...   (2)
    //   x[(i+1234)*2+5678] = ... (3)
    //   x[(i+1234)*2+5679] = ... (3)
    //
    // The loop will be rerolled by adding a new loop induction variable,
    // one for the Base instruction in each DAGRootSet.
    //
    struct DAGRootSet {
      Instruction *BaseInst;
      SmallInstructionVector Roots;

      // The instructions between IV and BaseInst (but not including BaseInst).
      SmallInstructionSet SubsumedInsts;
    };

    // The set of all DAG roots, and state tracking of all roots
    // for a particular induction variable.
    struct DAGRootTracker {
      DAGRootTracker(LoopReroll *Parent, Loop *L, Instruction *IV,
                     ScalarEvolution *SE, AliasAnalysis *AA,
                     TargetLibraryInfo *TLI, DominatorTree *DT, LoopInfo *LI,
                     bool PreserveLCSSA,
                     DenseMap<Instruction *, int64_t> &IncrMap,
                     Instruction *LoopCtrlIV)
          : Parent(Parent), L(L), SE(SE), AA(AA), TLI(TLI), DT(DT), LI(LI),
            PreserveLCSSA(PreserveLCSSA), IV(IV), IVToIncMap(IncrMap),
            LoopControlIV(LoopCtrlIV) {}

      /// Stage 1: Find all the DAG roots for the induction variable.
      bool findRoots();

      /// Stage 2: Validate if the found roots are valid.
      bool validate(ReductionTracker &Reductions);

      /// Stage 3: Assuming validate() returned true, perform the
      /// replacement.
      /// @param BackedgeTakenCount The backedge-taken count of L.
      void replace(const SCEV *BackedgeTakenCount);

    protected:
      using UsesTy = MapVector<Instruction *, BitVector>;

      void findRootsRecursive(Instruction *IVU,
                              SmallInstructionSet SubsumedInsts);
      bool findRootsBase(Instruction *IVU, SmallInstructionSet SubsumedInsts);
      bool collectPossibleRoots(Instruction *Base,
                                std::map<int64_t,Instruction*> &Roots);
      bool validateRootSet(DAGRootSet &DRS);

      bool collectUsedInstructions(SmallInstructionSet &PossibleRedSet);
      void collectInLoopUserSet(const SmallInstructionVector &Roots,
                                const SmallInstructionSet &Exclude,
                                const SmallInstructionSet &Final,
                                DenseSet<Instruction *> &Users);
      void collectInLoopUserSet(Instruction *Root,
                                const SmallInstructionSet &Exclude,
                                const SmallInstructionSet &Final,
                                DenseSet<Instruction *> &Users);

      UsesTy::iterator nextInstr(int Val, UsesTy &In,
                                 const SmallInstructionSet &Exclude,
                                 UsesTy::iterator *StartI=nullptr);
      bool isBaseInst(Instruction *I);
      bool isRootInst(Instruction *I);
      bool instrDependsOn(Instruction *I,
                          UsesTy::iterator Start,
                          UsesTy::iterator End);
      void replaceIV(DAGRootSet &DRS, const SCEV *Start, const SCEV *IncrExpr);

      LoopReroll *Parent;

      // Members of Parent, replicated here for brevity.
      Loop *L;
      ScalarEvolution *SE;
      AliasAnalysis *AA;
      TargetLibraryInfo *TLI;
      DominatorTree *DT;
      LoopInfo *LI;
      bool PreserveLCSSA;

      // The loop induction variable.
      Instruction *IV;

      // Loop step amount.
      int64_t Inc;

      // Loop reroll count; if Inc == 1, this records the scaling applied
      // to the indvar: a[i*2+0] = ...; a[i*2+1] = ... ;
      // If Inc is not 1, Scale = Inc.
      uint64_t Scale;

      // The roots themselves.
      SmallVector<DAGRootSet,16> RootSets;

      // All increment instructions for IV.
      SmallInstructionVector LoopIncs;

      // Map of all instructions in the loop (in order) to the iterations
      // they are used in (or specially, IL_All for instructions
      // used in the loop increment mechanism).
      UsesTy Uses;

      // Map between induction variable and its increment
      DenseMap<Instruction *, int64_t> &IVToIncMap;

      Instruction *LoopControlIV;
    };

    // Check if it is a compare-like instruction whose user is a branch
    bool isCompareUsedByBranch(Instruction *I) {
      auto *TI = I->getParent()->getTerminator();
      if (!isa<BranchInst>(TI) || !isa<CmpInst>(I))
        return false;
      return I->hasOneUse() && TI->getOperand(0) == I;
    };

    bool isLoopControlIV(Loop *L, Instruction *IV);
    void collectPossibleIVs(Loop *L, SmallInstructionVector &PossibleIVs);
    void collectPossibleReductions(Loop *L,
           ReductionTracker &Reductions);
    bool reroll(Instruction *IV, Loop *L, BasicBlock *Header,
                const SCEV *BackedgeTakenCount, ReductionTracker &Reductions);
  };

} // end anonymous namespace

char LoopReroll::ID = 0;

INITIALIZE_PASS_BEGIN(LoopReroll, "loop-reroll", "Reroll loops", false, false)
INITIALIZE_PASS_DEPENDENCY(LoopPass)
INITIALIZE_PASS_DEPENDENCY(TargetLibraryInfoWrapperPass)
INITIALIZE_PASS_END(LoopReroll, "loop-reroll", "Reroll loops", false, false)

Pass *llvm::createLoopRerollPass() {
  return new LoopReroll;
}

// Returns true if the provided instruction is used outside the given loop.
// This operates like Instruction::isUsedOutsideOfBlock, but considers PHIs in
// non-loop blocks to be outside the loop.
static bool hasUsesOutsideLoop(Instruction *I, Loop *L) {
  for (User *U : I->users()) {
    if (!L->contains(cast<Instruction>(U)))
      return true;
  }
  return false;
}

// Check if an IV is only used to control the loop. There are two cases:
// 1. It only has one use which is loop increment, and the increment is only
// used by comparison and the PHI (could has sext with nsw in between), and the
// comparison is only used by branch.
// 2. It is used by loop increment and the comparison, the loop increment is
// only used by the PHI, and the comparison is used only by the branch.
bool LoopReroll::isLoopControlIV(Loop *L, Instruction *IV) {
  unsigned IVUses = IV->getNumUses();
  if (IVUses != 2 && IVUses != 1)
    return false;

  for (auto *User : IV->users()) {
    int32_t IncOrCmpUses = User->getNumUses();
    bool IsCompInst = isCompareUsedByBranch(cast<Instruction>(User));

    // User can only have one or two uses.
    if (IncOrCmpUses != 2 && IncOrCmpUses != 1)
      return false;

    // Case 1
    if (IVUses == 1) {
      // The only user must be the loop increment.
      // The loop increment must have two uses.
      if (IsCompInst || IncOrCmpUses != 2)
        return false;
    }

    // Case 2
    if (IVUses == 2 && IncOrCmpUses != 1)
      return false;

    // The users of the IV must be a binary operation or a comparison
    if (auto *BO = dyn_cast<BinaryOperator>(User)) {
      if (BO->getOpcode() == Instruction::Add) {
        // Loop Increment
        // User of Loop Increment should be either PHI or CMP
        for (auto *UU : User->users()) {
          if (PHINode *PN = dyn_cast<PHINode>(UU)) {
            if (PN != IV)
              return false;
          }
          // Must be a CMP or an ext (of a value with nsw) then CMP
          else {
            Instruction *UUser = dyn_cast<Instruction>(UU);
            // Skip SExt if we are extending an nsw value
            // TODO: Allow ZExt too
            if (BO->hasNoSignedWrap() && UUser && UUser->hasOneUse() &&
                isa<SExtInst>(UUser))
              UUser = dyn_cast<Instruction>(*(UUser->user_begin()));
            if (!isCompareUsedByBranch(UUser))
              return false;
          }
        }
      } else
        return false;
      // Compare : can only have one use, and must be branch
    } else if (!IsCompInst)
      return false;
  }
  return true;
}

// Collect the list of loop induction variables with respect to which it might
// be possible to reroll the loop.
void LoopReroll::collectPossibleIVs(Loop *L,
                                    SmallInstructionVector &PossibleIVs) {
  BasicBlock *Header = L->getHeader();
  for (BasicBlock::iterator I = Header->begin(),
       IE = Header->getFirstInsertionPt(); I != IE; ++I) {
    if (!isa<PHINode>(I))
      continue;
    if (!I->getType()->isIntegerTy() && !I->getType()->isPointerTy())
      continue;

    if (const SCEVAddRecExpr *PHISCEV =
            dyn_cast<SCEVAddRecExpr>(SE->getSCEV(&*I))) {
      if (PHISCEV->getLoop() != L)
        continue;
      if (!PHISCEV->isAffine())
        continue;
      auto IncSCEV = dyn_cast<SCEVConstant>(PHISCEV->getStepRecurrence(*SE));
      if (IncSCEV) {
        IVToIncMap[&*I] = IncSCEV->getValue()->getSExtValue();
        LLVM_DEBUG(dbgs() << "LRR: Possible IV: " << *I << " = " << *PHISCEV
                          << "\n");

        if (isLoopControlIV(L, &*I)) {
          assert(!LoopControlIV && "Found two loop control only IV");
          LoopControlIV = &(*I);
          LLVM_DEBUG(dbgs() << "LRR: Possible loop control only IV: " << *I
                            << " = " << *PHISCEV << "\n");
        } else
          PossibleIVs.push_back(&*I);
      }
    }
  }
}

// Add the remainder of the reduction-variable chain to the instruction vector
// (the initial PHINode has already been added). If successful, the object is
// marked as valid.
void LoopReroll::SimpleLoopReduction::add(Loop *L) {
  assert(!Valid && "Cannot add to an already-valid chain");

  // The reduction variable must be a chain of single-use instructions
  // (including the PHI), except for the last value (which is used by the PHI
  // and also outside the loop).
  Instruction *C = Instructions.front();
  if (C->user_empty())
    return;

  do {
    C = cast<Instruction>(*C->user_begin());
    if (C->hasOneUse()) {
      if (!C->isBinaryOp())
        return;

      if (!(isa<PHINode>(Instructions.back()) ||
            C->isSameOperationAs(Instructions.back())))
        return;

      Instructions.push_back(C);
    }
  } while (C->hasOneUse());

  if (Instructions.size() < 2 ||
      !C->isSameOperationAs(Instructions.back()) ||
      C->use_empty())
    return;

  // C is now the (potential) last instruction in the reduction chain.
  for (User *U : C->users()) {
    // The only in-loop user can be the initial PHI.
    if (L->contains(cast<Instruction>(U)))
      if (cast<Instruction>(U) != Instructions.front())
        return;
  }

  Instructions.push_back(C);
  Valid = true;
}

// Collect the vector of possible reduction variables.
void LoopReroll::collectPossibleReductions(Loop *L,
  ReductionTracker &Reductions) {
  BasicBlock *Header = L->getHeader();
  for (BasicBlock::iterator I = Header->begin(),
       IE = Header->getFirstInsertionPt(); I != IE; ++I) {
    if (!isa<PHINode>(I))
      continue;
    if (!I->getType()->isSingleValueType())
      continue;

    SimpleLoopReduction SLR(&*I, L);
    if (!SLR.valid())
      continue;

    LLVM_DEBUG(dbgs() << "LRR: Possible reduction: " << *I << " (with "
                      << SLR.size() << " chained instructions)\n");
    Reductions.addSLR(SLR);
  }
}

// Collect the set of all users of the provided root instruction. This set of
// users contains not only the direct users of the root instruction, but also
// all users of those users, and so on. There are two exceptions:
//
//   1. Instructions in the set of excluded instructions are never added to the
//   use set (even if they are users). This is used, for example, to exclude
//   including root increments in the use set of the primary IV.
//
//   2. Instructions in the set of final instructions are added to the use set
//   if they are users, but their users are not added. This is used, for
//   example, to prevent a reduction update from forcing all later reduction
//   updates into the use set.
void LoopReroll::DAGRootTracker::collectInLoopUserSet(
  Instruction *Root, const SmallInstructionSet &Exclude,
  const SmallInstructionSet &Final,
  DenseSet<Instruction *> &Users) {
  SmallInstructionVector Queue(1, Root);
  while (!Queue.empty()) {
    Instruction *I = Queue.pop_back_val();
    if (!Users.insert(I).second)
      continue;

    if (!Final.count(I))
      for (Use &U : I->uses()) {
        Instruction *User = cast<Instruction>(U.getUser());
        if (PHINode *PN = dyn_cast<PHINode>(User)) {
          // Ignore "wrap-around" uses to PHIs of this loop's header.
          if (PN->getIncomingBlock(U) == L->getHeader())
            continue;
        }

        if (L->contains(User) && !Exclude.count(User)) {
          Queue.push_back(User);
        }
      }

    // We also want to collect single-user "feeder" values.
    for (User::op_iterator OI = I->op_begin(),
         OIE = I->op_end(); OI != OIE; ++OI) {
      if (Instruction *Op = dyn_cast<Instruction>(*OI))
        if (Op->hasOneUse() && L->contains(Op) && !Exclude.count(Op) &&
            !Final.count(Op))
          Queue.push_back(Op);
    }
  }
}

// Collect all of the users of all of the provided root instructions (combined
// into a single set).
void LoopReroll::DAGRootTracker::collectInLoopUserSet(
  const SmallInstructionVector &Roots,
  const SmallInstructionSet &Exclude,
  const SmallInstructionSet &Final,
  DenseSet<Instruction *> &Users) {
  for (Instruction *Root : Roots)
    collectInLoopUserSet(Root, Exclude, Final, Users);
}

static bool isUnorderedLoadStore(Instruction *I) {
  if (LoadInst *LI = dyn_cast<LoadInst>(I))
    return LI->isUnordered();
  if (StoreInst *SI = dyn_cast<StoreInst>(I))
    return SI->isUnordered();
  if (MemIntrinsic *MI = dyn_cast<MemIntrinsic>(I))
    return !MI->isVolatile();
  return false;
}

/// Return true if IVU is a "simple" arithmetic operation.
/// This is used for narrowing the search space for DAGRoots; only arithmetic
/// and GEPs can be part of a DAGRoot.
static bool isSimpleArithmeticOp(User *IVU) {
  if (Instruction *I = dyn_cast<Instruction>(IVU)) {
    switch (I->getOpcode()) {
    default: return false;
    case Instruction::Add:
    case Instruction::Sub:
    case Instruction::Mul:
    case Instruction::Shl:
    case Instruction::AShr:
    case Instruction::LShr:
    case Instruction::GetElementPtr:
    case Instruction::Trunc:
    case Instruction::ZExt:
    case Instruction::SExt:
      return true;
    }
  }
  return false;
}

static bool isLoopIncrement(User *U, Instruction *IV) {
  BinaryOperator *BO = dyn_cast<BinaryOperator>(U);

  if ((BO && BO->getOpcode() != Instruction::Add) ||
      (!BO && !isa<GetElementPtrInst>(U)))
    return false;

  for (auto *UU : U->users()) {
    PHINode *PN = dyn_cast<PHINode>(UU);
    if (PN && PN == IV)
      return true;
  }
  return false;
}

bool LoopReroll::DAGRootTracker::
collectPossibleRoots(Instruction *Base, std::map<int64_t,Instruction*> &Roots) {
  SmallInstructionVector BaseUsers;

  for (auto *I : Base->users()) {
    ConstantInt *CI = nullptr;

    if (isLoopIncrement(I, IV)) {
      LoopIncs.push_back(cast<Instruction>(I));
      continue;
    }

    // The root nodes must be either GEPs, ORs or ADDs.
    if (auto *BO = dyn_cast<BinaryOperator>(I)) {
      if (BO->getOpcode() == Instruction::Add ||
          BO->getOpcode() == Instruction::Or)
        CI = dyn_cast<ConstantInt>(BO->getOperand(1));
    } else if (auto *GEP = dyn_cast<GetElementPtrInst>(I)) {
      Value *LastOperand = GEP->getOperand(GEP->getNumOperands()-1);
      CI = dyn_cast<ConstantInt>(LastOperand);
    }

    if (!CI) {
      if (Instruction *II = dyn_cast<Instruction>(I)) {
        BaseUsers.push_back(II);
        continue;
      } else {
        LLVM_DEBUG(dbgs() << "LRR: Aborting due to non-instruction: " << *I
                          << "\n");
        return false;
      }
    }

    int64_t V = std::abs(CI->getValue().getSExtValue());
    if (Roots.find(V) != Roots.end())
      // No duplicates, please.
      return false;

    Roots[V] = cast<Instruction>(I);
  }

  // Make sure we have at least two roots.
  if (Roots.empty() || (Roots.size() == 1 && BaseUsers.empty()))
    return false;

  // If we found non-loop-inc, non-root users of Base, assume they are
  // for the zeroth root index. This is because "add %a, 0" gets optimized
  // away.
  if (BaseUsers.size()) {
    if (Roots.find(0) != Roots.end()) {
      LLVM_DEBUG(dbgs() << "LRR: Multiple roots found for base - aborting!\n");
      return false;
    }
    Roots[0] = Base;
  }

  // Calculate the number of users of the base, or lowest indexed, iteration.
  unsigned NumBaseUses = BaseUsers.size();
  if (NumBaseUses == 0)
    NumBaseUses = Roots.begin()->second->getNumUses();

  // Check that every node has the same number of users.
  for (auto &KV : Roots) {
    if (KV.first == 0)
      continue;
    if (!KV.second->hasNUses(NumBaseUses)) {
      LLVM_DEBUG(dbgs() << "LRR: Aborting - Root and Base #users not the same: "
                        << "#Base=" << NumBaseUses
                        << ", #Root=" << KV.second->getNumUses() << "\n");
      return false;
    }
  }

  return true;
}

void LoopReroll::DAGRootTracker::
findRootsRecursive(Instruction *I, SmallInstructionSet SubsumedInsts) {
  // Does the user look like it could be part of a root set?
  // All its users must be simple arithmetic ops.
  if (I->hasNUsesOrMore(IL_MaxRerollIterations + 1))
    return;

  if (I != IV && findRootsBase(I, SubsumedInsts))
    return;

  SubsumedInsts.insert(I);

  for (User *V : I->users()) {
    Instruction *I = cast<Instruction>(V);
    if (is_contained(LoopIncs, I))
      continue;

    if (!isSimpleArithmeticOp(I))
      continue;

    // The recursive call makes a copy of SubsumedInsts.
    findRootsRecursive(I, SubsumedInsts);
  }
}

bool LoopReroll::DAGRootTracker::validateRootSet(DAGRootSet &DRS) {
  if (DRS.Roots.empty())
    return false;

  // Consider a DAGRootSet with N-1 roots (so N different values including
  //   BaseInst).
  // Define d = Roots[0] - BaseInst, which should be the same as
  //   Roots[I] - Roots[I-1] for all I in [1..N).
  // Define D = BaseInst@J - BaseInst@J-1, where "@J" means the value at the
  //   loop iteration J.
  //
  // Now, For the loop iterations to be consecutive:
  //   D = d * N
  const auto *ADR = dyn_cast<SCEVAddRecExpr>(SE->getSCEV(DRS.BaseInst));
  if (!ADR)
    return false;
  unsigned N = DRS.Roots.size() + 1;
  const SCEV *StepSCEV = SE->getMinusSCEV(SE->getSCEV(DRS.Roots[0]), ADR);
  const SCEV *ScaleSCEV = SE->getConstant(StepSCEV->getType(), N);
  if (ADR->getStepRecurrence(*SE) != SE->getMulExpr(StepSCEV, ScaleSCEV))
    return false;

  return true;
}

bool LoopReroll::DAGRootTracker::
findRootsBase(Instruction *IVU, SmallInstructionSet SubsumedInsts) {
  // The base of a RootSet must be an AddRec, so it can be erased.
  const auto *IVU_ADR = dyn_cast<SCEVAddRecExpr>(SE->getSCEV(IVU));
  if (!IVU_ADR || IVU_ADR->getLoop() != L)
    return false;

  std::map<int64_t, Instruction*> V;
  if (!collectPossibleRoots(IVU, V))
    return false;

  // If we didn't get a root for index zero, then IVU must be
  // subsumed.
  if (V.find(0) == V.end())
    SubsumedInsts.insert(IVU);

  // Partition the vector into monotonically increasing indexes.
  DAGRootSet DRS;
  DRS.BaseInst = nullptr;

  SmallVector<DAGRootSet, 16> PotentialRootSets;

  for (auto &KV : V) {
    if (!DRS.BaseInst) {
      DRS.BaseInst = KV.second;
      DRS.SubsumedInsts = SubsumedInsts;
    } else if (DRS.Roots.empty()) {
      DRS.Roots.push_back(KV.second);
    } else if (V.find(KV.first - 1) != V.end()) {
      DRS.Roots.push_back(KV.second);
    } else {
      // Linear sequence terminated.
      if (!validateRootSet(DRS))
        return false;

      // Construct a new DAGRootSet with the next sequence.
      PotentialRootSets.push_back(DRS);
      DRS.BaseInst = KV.second;
      DRS.Roots.clear();
    }
  }

  if (!validateRootSet(DRS))
    return false;

  PotentialRootSets.push_back(DRS);

  RootSets.append(PotentialRootSets.begin(), PotentialRootSets.end());

  return true;
}

bool LoopReroll::DAGRootTracker::findRoots() {
  Inc = IVToIncMap[IV];

  assert(RootSets.empty() && "Unclean state!");
  if (std::abs(Inc) == 1) {
    for (auto *IVU : IV->users()) {
      if (isLoopIncrement(IVU, IV))
        LoopIncs.push_back(cast<Instruction>(IVU));
    }
    findRootsRecursive(IV, SmallInstructionSet());
    LoopIncs.push_back(IV);
  } else {
    if (!findRootsBase(IV, SmallInstructionSet()))
      return false;
  }

  // Ensure all sets have the same size.
  if (RootSets.empty()) {
    LLVM_DEBUG(dbgs() << "LRR: Aborting because no root sets found!\n");
    return false;
  }
  for (auto &V : RootSets) {
    if (V.Roots.empty() || V.Roots.size() != RootSets[0].Roots.size()) {
      LLVM_DEBUG(
          dbgs()
          << "LRR: Aborting because not all root sets have the same size\n");
      return false;
    }
  }

  Scale = RootSets[0].Roots.size() + 1;

  if (Scale > IL_MaxRerollIterations) {
    LLVM_DEBUG(dbgs() << "LRR: Aborting - too many iterations found. "
                      << "#Found=" << Scale
                      << ", #Max=" << IL_MaxRerollIterations << "\n");
    return false;
  }

  LLVM_DEBUG(dbgs() << "LRR: Successfully found roots: Scale=" << Scale
                    << "\n");

  return true;
}

bool LoopReroll::DAGRootTracker::collectUsedInstructions(SmallInstructionSet &PossibleRedSet) {
  // Populate the MapVector with all instructions in the block, in order first,
  // so we can iterate over the contents later in perfect order.
  for (auto &I : *L->getHeader()) {
    Uses[&I].resize(IL_End);
  }

  SmallInstructionSet Exclude;
  for (auto &DRS : RootSets) {
    Exclude.insert(DRS.Roots.begin(), DRS.Roots.end());
    Exclude.insert(DRS.SubsumedInsts.begin(), DRS.SubsumedInsts.end());
    Exclude.insert(DRS.BaseInst);
  }
  Exclude.insert(LoopIncs.begin(), LoopIncs.end());

  for (auto &DRS : RootSets) {
    DenseSet<Instruction*> VBase;
    collectInLoopUserSet(DRS.BaseInst, Exclude, PossibleRedSet, VBase);
    for (auto *I : VBase) {
      Uses[I].set(0);
    }

    unsigned Idx = 1;
    for (auto *Root : DRS.Roots) {
      DenseSet<Instruction*> V;
      collectInLoopUserSet(Root, Exclude, PossibleRedSet, V);

      // While we're here, check the use sets are the same size.
      if (V.size() != VBase.size()) {
        LLVM_DEBUG(dbgs() << "LRR: Aborting - use sets are different sizes\n");
        return false;
      }

      for (auto *I : V) {
        Uses[I].set(Idx);
      }
      ++Idx;
    }

    // Make sure our subsumed instructions are remembered too.
    for (auto *I : DRS.SubsumedInsts) {
      Uses[I].set(IL_All);
    }
  }

  // Make sure the loop increments are also accounted for.

  Exclude.clear();
  for (auto &DRS : RootSets) {
    Exclude.insert(DRS.Roots.begin(), DRS.Roots.end());
    Exclude.insert(DRS.SubsumedInsts.begin(), DRS.SubsumedInsts.end());
    Exclude.insert(DRS.BaseInst);
  }

  DenseSet<Instruction*> V;
  collectInLoopUserSet(LoopIncs, Exclude, PossibleRedSet, V);
  for (auto *I : V) {
    Uses[I].set(IL_All);
  }

  return true;
}

/// Get the next instruction in "In" that is a member of set Val.
/// Start searching from StartI, and do not return anything in Exclude.
/// If StartI is not given, start from In.begin().
LoopReroll::DAGRootTracker::UsesTy::iterator
LoopReroll::DAGRootTracker::nextInstr(int Val, UsesTy &In,
                                      const SmallInstructionSet &Exclude,
                                      UsesTy::iterator *StartI) {
  UsesTy::iterator I = StartI ? *StartI : In.begin();
  while (I != In.end() && (I->second.test(Val) == 0 ||
                           Exclude.count(I->first) != 0))
    ++I;
  return I;
}

bool LoopReroll::DAGRootTracker::isBaseInst(Instruction *I) {
  for (auto &DRS : RootSets) {
    if (DRS.BaseInst == I)
      return true;
  }
  return false;
}

bool LoopReroll::DAGRootTracker::isRootInst(Instruction *I) {
  for (auto &DRS : RootSets) {
    if (is_contained(DRS.Roots, I))
      return true;
  }
  return false;
}

/// Return true if instruction I depends on any instruction between
/// Start and End.
bool LoopReroll::DAGRootTracker::instrDependsOn(Instruction *I,
                                                UsesTy::iterator Start,
                                                UsesTy::iterator End) {
  for (auto *U : I->users()) {
    for (auto It = Start; It != End; ++It)
      if (U == It->first)
        return true;
  }
  return false;
}

static bool isIgnorableInst(const Instruction *I) {
  if (isa<DbgInfoIntrinsic>(I))
    return true;
  const IntrinsicInst* II = dyn_cast<IntrinsicInst>(I);
  if (!II)
    return false;
  switch (II->getIntrinsicID()) {
    default:
      return false;
    case Intrinsic::annotation:
    case Intrinsic::ptr_annotation:
    case Intrinsic::var_annotation:
    // TODO: the following intrinsics may also be whitelisted:
    //   lifetime_start, lifetime_end, invariant_start, invariant_end
      return true;
  }
  return false;
}

bool LoopReroll::DAGRootTracker::validate(ReductionTracker &Reductions) {
  // We now need to check for equivalence of the use graph of each root with
  // that of the primary induction variable (excluding the roots). Our goal
  // here is not to solve the full graph isomorphism problem, but rather to
  // catch common cases without a lot of work. As a result, we will assume
  // that the relative order of the instructions in each unrolled iteration
  // is the same (although we will not make an assumption about how the
  // different iterations are intermixed). Note that while the order must be
  // the same, the instructions may not be in the same basic block.

  // An array of just the possible reductions for this scale factor. When we
  // collect the set of all users of some root instructions, these reduction
  // instructions are treated as 'final' (their uses are not considered).
  // This is important because we don't want the root use set to search down
  // the reduction chain.
  SmallInstructionSet PossibleRedSet;
  SmallInstructionSet PossibleRedLastSet;
  SmallInstructionSet PossibleRedPHISet;
  Reductions.restrictToScale(Scale, PossibleRedSet,
                             PossibleRedPHISet, PossibleRedLastSet);

  // Populate "Uses" with where each instruction is used.
  if (!collectUsedInstructions(PossibleRedSet))
    return false;

  // Make sure we mark the reduction PHIs as used in all iterations.
  for (auto *I : PossibleRedPHISet) {
    Uses[I].set(IL_All);
  }

  // Make sure we mark loop-control-only PHIs as used in all iterations. See
  // comment above LoopReroll::isLoopControlIV for more information.
  BasicBlock *Header = L->getHeader();
  if (LoopControlIV && LoopControlIV != IV) {
    for (auto *U : LoopControlIV->users()) {
      Instruction *IVUser = dyn_cast<Instruction>(U);
      // IVUser could be loop increment or compare
      Uses[IVUser].set(IL_All);
      for (auto *UU : IVUser->users()) {
        Instruction *UUser = dyn_cast<Instruction>(UU);
        // UUser could be compare, PHI or branch
        Uses[UUser].set(IL_All);
        // Skip SExt
        if (isa<SExtInst>(UUser)) {
          UUser = dyn_cast<Instruction>(*(UUser->user_begin()));
          Uses[UUser].set(IL_All);
        }
        // Is UUser a compare instruction?
        if (UU->hasOneUse()) {
          Instruction *BI = dyn_cast<BranchInst>(*UUser->user_begin());
          if (BI == cast<BranchInst>(Header->getTerminator()))
            Uses[BI].set(IL_All);
        }
      }
    }
  }

  // Make sure all instructions in the loop are in one and only one
  // set.
  for (auto &KV : Uses) {
    if (KV.second.count() != 1 && !isIgnorableInst(KV.first)) {
      LLVM_DEBUG(
          dbgs() << "LRR: Aborting - instruction is not used in 1 iteration: "
                 << *KV.first << " (#uses=" << KV.second.count() << ")\n");
      return false;
    }
  }

  LLVM_DEBUG(for (auto &KV
                  : Uses) {
    dbgs() << "LRR: " << KV.second.find_first() << "\t" << *KV.first << "\n";
  });

  for (unsigned Iter = 1; Iter < Scale; ++Iter) {
    // In addition to regular aliasing information, we need to look for
    // instructions from later (future) iterations that have side effects
    // preventing us from reordering them past other instructions with side
    // effects.
    bool FutureSideEffects = false;
    AliasSetTracker AST(*AA);
    // The map between instructions in f(%iv.(i+1)) and f(%iv).
    DenseMap<Value *, Value *> BaseMap;

    // Compare iteration Iter to the base.
    SmallInstructionSet Visited;
    auto BaseIt = nextInstr(0, Uses, Visited);
    auto RootIt = nextInstr(Iter, Uses, Visited);
    auto LastRootIt = Uses.begin();

    while (BaseIt != Uses.end() && RootIt != Uses.end()) {
      Instruction *BaseInst = BaseIt->first;
      Instruction *RootInst = RootIt->first;

      // Skip over the IV or root instructions; only match their users.
      bool Continue = false;
      if (isBaseInst(BaseInst)) {
        Visited.insert(BaseInst);
        BaseIt = nextInstr(0, Uses, Visited);
        Continue = true;
      }
      if (isRootInst(RootInst)) {
        LastRootIt = RootIt;
        Visited.insert(RootInst);
        RootIt = nextInstr(Iter, Uses, Visited);
        Continue = true;
      }
      if (Continue) continue;

      if (!BaseInst->isSameOperationAs(RootInst)) {
        // Last chance saloon. We don't try and solve the full isomorphism
        // problem, but try and at least catch the case where two instructions
        // *of different types* are round the wrong way. We won't be able to
        // efficiently tell, given two ADD instructions, which way around we
        // should match them, but given an ADD and a SUB, we can at least infer
        // which one is which.
        //
        // This should allow us to deal with a greater subset of the isomorphism
        // problem. It does however change a linear algorithm into a quadratic
        // one, so limit the number of probes we do.
        auto TryIt = RootIt;
        unsigned N = NumToleratedFailedMatches;
        while (TryIt != Uses.end() &&
               !BaseInst->isSameOperationAs(TryIt->first) &&
               N--) {
          ++TryIt;
          TryIt = nextInstr(Iter, Uses, Visited, &TryIt);
        }

        if (TryIt == Uses.end() || TryIt == RootIt ||
            instrDependsOn(TryIt->first, RootIt, TryIt)) {
          LLVM_DEBUG(dbgs() << "LRR: iteration root match failed at "
                            << *BaseInst << " vs. " << *RootInst << "\n");
          return false;
        }

        RootIt = TryIt;
        RootInst = TryIt->first;
      }

      // All instructions between the last root and this root
      // may belong to some other iteration. If they belong to a
      // future iteration, then they're dangerous to alias with.
      //
      // Note that because we allow a limited amount of flexibility in the order
      // that we visit nodes, LastRootIt might be *before* RootIt, in which
      // case we've already checked this set of instructions so we shouldn't
      // do anything.
      for (; LastRootIt < RootIt; ++LastRootIt) {
        Instruction *I = LastRootIt->first;
        if (LastRootIt->second.find_first() < (int)Iter)
          continue;
        if (I->mayWriteToMemory())
          AST.add(I);
        // Note: This is specifically guarded by a check on isa<PHINode>,
        // which while a valid (somewhat arbitrary) micro-optimization, is
        // needed because otherwise isSafeToSpeculativelyExecute returns
        // false on PHI nodes.
        if (!isa<PHINode>(I) && !isUnorderedLoadStore(I) &&
            !isSafeToSpeculativelyExecute(I))
          // Intervening instructions cause side effects.
          FutureSideEffects = true;
      }

      // Make sure that this instruction, which is in the use set of this
      // root instruction, does not also belong to the base set or the set of
      // some other root instruction.
      if (RootIt->second.count() > 1) {
        LLVM_DEBUG(dbgs() << "LRR: iteration root match failed at " << *BaseInst
                          << " vs. " << *RootInst << " (prev. case overlap)\n");
        return false;
      }

      // Make sure that we don't alias with any instruction in the alias set
      // tracker. If we do, then we depend on a future iteration, and we
      // can't reroll.
      if (RootInst->mayReadFromMemory())
        for (auto &K : AST) {
          if (K.aliasesUnknownInst(RootInst, *AA)) {
            LLVM_DEBUG(dbgs() << "LRR: iteration root match failed at "
                              << *BaseInst << " vs. " << *RootInst
                              << " (depends on future store)\n");
            return false;
          }
        }

      // If we've past an instruction from a future iteration that may have
      // side effects, and this instruction might also, then we can't reorder
      // them, and this matching fails. As an exception, we allow the alias
      // set tracker to handle regular (unordered) load/store dependencies.
      if (FutureSideEffects && ((!isUnorderedLoadStore(BaseInst) &&
                                 !isSafeToSpeculativelyExecute(BaseInst)) ||
                                (!isUnorderedLoadStore(RootInst) &&
                                 !isSafeToSpeculativelyExecute(RootInst)))) {
        LLVM_DEBUG(dbgs() << "LRR: iteration root match failed at " << *BaseInst
                          << " vs. " << *RootInst
                          << " (side effects prevent reordering)\n");
        return false;
      }

      // For instructions that are part of a reduction, if the operation is
      // associative, then don't bother matching the operands (because we
      // already know that the instructions are isomorphic, and the order
      // within the iteration does not matter). For non-associative reductions,
      // we do need to match the operands, because we need to reject
      // out-of-order instructions within an iteration!
      // For example (assume floating-point addition), we need to reject this:
      //   x += a[i]; x += b[i];
      //   x += a[i+1]; x += b[i+1];
      //   x += b[i+2]; x += a[i+2];
      bool InReduction = Reductions.isPairInSame(BaseInst, RootInst);

      if (!(InReduction && BaseInst->isAssociative())) {
        bool Swapped = false, SomeOpMatched = false;
        for (unsigned j = 0; j < BaseInst->getNumOperands(); ++j) {
          Value *Op2 = RootInst->getOperand(j);

          // If this is part of a reduction (and the operation is not
          // associatve), then we match all operands, but not those that are
          // part of the reduction.
          if (InReduction)
            if (Instruction *Op2I = dyn_cast<Instruction>(Op2))
              if (Reductions.isPairInSame(RootInst, Op2I))
                continue;

          DenseMap<Value *, Value *>::iterator BMI = BaseMap.find(Op2);
          if (BMI != BaseMap.end()) {
            Op2 = BMI->second;
          } else {
            for (auto &DRS : RootSets) {
              if (DRS.Roots[Iter-1] == (Instruction*) Op2) {
                Op2 = DRS.BaseInst;
                break;
              }
            }
          }

          if (BaseInst->getOperand(Swapped ? unsigned(!j) : j) != Op2) {
            // If we've not already decided to swap the matched operands, and
            // we've not already matched our first operand (note that we could
            // have skipped matching the first operand because it is part of a
            // reduction above), and the instruction is commutative, then try
            // the swapped match.
            if (!Swapped && BaseInst->isCommutative() && !SomeOpMatched &&
                BaseInst->getOperand(!j) == Op2) {
              Swapped = true;
            } else {
              LLVM_DEBUG(dbgs()
                         << "LRR: iteration root match failed at " << *BaseInst
                         << " vs. " << *RootInst << " (operand " << j << ")\n");
              return false;
            }
          }

          SomeOpMatched = true;
        }
      }

      if ((!PossibleRedLastSet.count(BaseInst) &&
           hasUsesOutsideLoop(BaseInst, L)) ||
          (!PossibleRedLastSet.count(RootInst) &&
           hasUsesOutsideLoop(RootInst, L))) {
        LLVM_DEBUG(dbgs() << "LRR: iteration root match failed at " << *BaseInst
                          << " vs. " << *RootInst << " (uses outside loop)\n");
        return false;
      }

      Reductions.recordPair(BaseInst, RootInst, Iter);
      BaseMap.insert(std::make_pair(RootInst, BaseInst));

      LastRootIt = RootIt;
      Visited.insert(BaseInst);
      Visited.insert(RootInst);
      BaseIt = nextInstr(0, Uses, Visited);
      RootIt = nextInstr(Iter, Uses, Visited);
    }
    assert(BaseIt == Uses.end() && RootIt == Uses.end() &&
           "Mismatched set sizes!");
  }

  LLVM_DEBUG(dbgs() << "LRR: Matched all iteration increments for " << *IV
                    << "\n");

  return true;
}

void LoopReroll::DAGRootTracker::replace(const SCEV *BackedgeTakenCount) {
  BasicBlock *Header = L->getHeader();

  // Compute the start and increment for each BaseInst before we start erasing
  // instructions.
  SmallVector<const SCEV *, 8> StartExprs;
  SmallVector<const SCEV *, 8> IncrExprs;
  for (auto &DRS : RootSets) {
    const SCEVAddRecExpr *IVSCEV =
        cast<SCEVAddRecExpr>(SE->getSCEV(DRS.BaseInst));
    StartExprs.push_back(IVSCEV->getStart());
    IncrExprs.push_back(SE->getMinusSCEV(SE->getSCEV(DRS.Roots[0]), IVSCEV));
  }

  // Remove instructions associated with non-base iterations.
  for (BasicBlock::reverse_iterator J = Header->rbegin(), JE = Header->rend();
       J != JE;) {
    unsigned I = Uses[&*J].find_first();
    if (I > 0 && I < IL_All) {
      LLVM_DEBUG(dbgs() << "LRR: removing: " << *J << "\n");
      J++->eraseFromParent();
      continue;
    }

    ++J;
  }

  // Rewrite each BaseInst using SCEV.
  for (size_t i = 0, e = RootSets.size(); i != e; ++i)
    // Insert the new induction variable.
    replaceIV(RootSets[i], StartExprs[i], IncrExprs[i]);

  { // Limit the lifetime of SCEVExpander.
    BranchInst *BI = cast<BranchInst>(Header->getTerminator());
    const DataLayout &DL = Header->getModule()->getDataLayout();
    SCEVExpander Expander(*SE, DL, "reroll");
    auto Zero = SE->getZero(BackedgeTakenCount->getType());
    auto One = SE->getOne(BackedgeTakenCount->getType());
    auto NewIVSCEV = SE->getAddRecExpr(Zero, One, L, SCEV::FlagAnyWrap);
    Value *NewIV =
        Expander.expandCodeFor(NewIVSCEV, BackedgeTakenCount->getType(),
                               Header->getFirstNonPHIOrDbg());
    // FIXME: This arithmetic can overflow.
    auto TripCount = SE->getAddExpr(BackedgeTakenCount, One);
    auto ScaledTripCount = SE->getMulExpr(
        TripCount, SE->getConstant(BackedgeTakenCount->getType(), Scale));
    auto ScaledBECount = SE->getMinusSCEV(ScaledTripCount, One);
    Value *TakenCount =
        Expander.expandCodeFor(ScaledBECount, BackedgeTakenCount->getType(),
                               Header->getFirstNonPHIOrDbg());
    Value *Cond =
        new ICmpInst(BI, CmpInst::ICMP_EQ, NewIV, TakenCount, "exitcond");
    BI->setCondition(Cond);

    if (BI->getSuccessor(1) != Header)
      BI->swapSuccessors();
  }

  SimplifyInstructionsInBlock(Header, TLI);
  DeleteDeadPHIs(Header, TLI);
}

void LoopReroll::DAGRootTracker::replaceIV(DAGRootSet &DRS,
                                           const SCEV *Start,
                                           const SCEV *IncrExpr) {
  BasicBlock *Header = L->getHeader();
  Instruction *Inst = DRS.BaseInst;

  const SCEV *NewIVSCEV =
      SE->getAddRecExpr(Start, IncrExpr, L, SCEV::FlagAnyWrap);

  { // Limit the lifetime of SCEVExpander.
    const DataLayout &DL = Header->getModule()->getDataLayout();
    SCEVExpander Expander(*SE, DL, "reroll");
    Value *NewIV = Expander.expandCodeFor(NewIVSCEV, Inst->getType(),
                                          Header->getFirstNonPHIOrDbg());

    for (auto &KV : Uses)
      if (KV.second.find_first() == 0)
        KV.first->replaceUsesOfWith(Inst, NewIV);
  }
}

// Validate the selected reductions. All iterations must have an isomorphic
// part of the reduction chain and, for non-associative reductions, the chain
// entries must appear in order.
bool LoopReroll::ReductionTracker::validateSelected() {
  // For a non-associative reduction, the chain entries must appear in order.
  for (int i : Reds) {
    int PrevIter = 0, BaseCount = 0, Count = 0;
    for (Instruction *J : PossibleReds[i]) {
      // Note that all instructions in the chain must have been found because
      // all instructions in the function must have been assigned to some
      // iteration.
      int Iter = PossibleRedIter[J];
      if (Iter != PrevIter && Iter != PrevIter + 1 &&
          !PossibleReds[i].getReducedValue()->isAssociative()) {
        LLVM_DEBUG(dbgs() << "LRR: Out-of-order non-associative reduction: "
                          << J << "\n");
        return false;
      }

      if (Iter != PrevIter) {
        if (Count != BaseCount) {
          LLVM_DEBUG(dbgs()
                     << "LRR: Iteration " << PrevIter << " reduction use count "
                     << Count << " is not equal to the base use count "
                     << BaseCount << "\n");
          return false;
        }

        Count = 0;
      }

      ++Count;
      if (Iter == 0)
        ++BaseCount;

      PrevIter = Iter;
    }
  }

  return true;
}

// For all selected reductions, remove all parts except those in the first
// iteration (and the PHI). Replace outside uses of the reduced value with uses
// of the first-iteration reduced value (in other words, reroll the selected
// reductions).
void LoopReroll::ReductionTracker::replaceSelected() {
  // Fixup reductions to refer to the last instruction associated with the
  // first iteration (not the last).
  for (int i : Reds) {
    int j = 0;
    for (int e = PossibleReds[i].size(); j != e; ++j)
      if (PossibleRedIter[PossibleReds[i][j]] != 0) {
        --j;
        break;
      }

    // Replace users with the new end-of-chain value.
    SmallInstructionVector Users;
    for (User *U : PossibleReds[i].getReducedValue()->users()) {
      Users.push_back(cast<Instruction>(U));
    }

    for (Instruction *User : Users)
      User->replaceUsesOfWith(PossibleReds[i].getReducedValue(),
                              PossibleReds[i][j]);
  }
}

// Reroll the provided loop with respect to the provided induction variable.
// Generally, we're looking for a loop like this:
//
// %iv = phi [ (preheader, ...), (body, %iv.next) ]
// f(%iv)
// %iv.1 = add %iv, 1                <-- a root increment
// f(%iv.1)
// %iv.2 = add %iv, 2                <-- a root increment
// f(%iv.2)
// %iv.scale_m_1 = add %iv, scale-1  <-- a root increment
// f(%iv.scale_m_1)
// ...
// %iv.next = add %iv, scale
// %cmp = icmp(%iv, ...)
// br %cmp, header, exit
//
// Notably, we do not require that f(%iv), f(%iv.1), etc. be isolated groups of
// instructions. In other words, the instructions in f(%iv), f(%iv.1), etc. can
// be intermixed with eachother. The restriction imposed by this algorithm is
// that the relative order of the isomorphic instructions in f(%iv), f(%iv.1),
// etc. be the same.
//
// First, we collect the use set of %iv, excluding the other increment roots.
// This gives us f(%iv). Then we iterate over the loop instructions (scale-1)
// times, having collected the use set of f(%iv.(i+1)), during which we:
//   - Ensure that the next unmatched instruction in f(%iv) is isomorphic to
//     the next unmatched instruction in f(%iv.(i+1)).
//   - Ensure that both matched instructions don't have any external users
//     (with the exception of last-in-chain reduction instructions).
//   - Track the (aliasing) write set, and other side effects, of all
//     instructions that belong to future iterations that come before the matched
//     instructions. If the matched instructions read from that write set, then
//     f(%iv) or f(%iv.(i+1)) has some dependency on instructions in
//     f(%iv.(j+1)) for some j > i, and we cannot reroll the loop. Similarly,
//     if any of these future instructions had side effects (could not be
//     speculatively executed), and so do the matched instructions, when we
//     cannot reorder those side-effect-producing instructions, and rerolling
//     fails.
//
// Finally, we make sure that all loop instructions are either loop increment
// roots, belong to simple latch code, parts of validated reductions, part of
// f(%iv) or part of some f(%iv.i). If all of that is true (and all reductions
// have been validated), then we reroll the loop.
bool LoopReroll::reroll(Instruction *IV, Loop *L, BasicBlock *Header,
                        const SCEV *BackedgeTakenCount,
                        ReductionTracker &Reductions) {
  DAGRootTracker DAGRoots(this, L, IV, SE, AA, TLI, DT, LI, PreserveLCSSA,
                          IVToIncMap, LoopControlIV);

  if (!DAGRoots.findRoots())
    return false;
  LLVM_DEBUG(dbgs() << "LRR: Found all root induction increments for: " << *IV
                    << "\n");

  if (!DAGRoots.validate(Reductions))
    return false;
  if (!Reductions.validateSelected())
    return false;
  // At this point, we've validated the rerolling, and we're committed to
  // making changes!

  Reductions.replaceSelected();
  DAGRoots.replace(BackedgeTakenCount);

  ++NumRerolledLoops;
  return true;
}

bool LoopReroll::runOnLoop(Loop *L, LPPassManager &LPM) {
  if (skipLoop(L))
    return false;

  AA = &getAnalysis<AAResultsWrapperPass>().getAAResults();
  LI = &getAnalysis<LoopInfoWrapperPass>().getLoopInfo();
  SE = &getAnalysis<ScalarEvolutionWrapperPass>().getSE();
  TLI = &getAnalysis<TargetLibraryInfoWrapperPass>().getTLI();
  DT = &getAnalysis<DominatorTreeWrapperPass>().getDomTree();
  PreserveLCSSA = mustPreserveAnalysisID(LCSSAID);

  BasicBlock *Header = L->getHeader();
  LLVM_DEBUG(dbgs() << "LRR: F[" << Header->getParent()->getName() << "] Loop %"
                    << Header->getName() << " (" << L->getNumBlocks()
                    << " block(s))\n");

  // For now, we'll handle only single BB loops.
  if (L->getNumBlocks() > 1)
    return false;

  if (!SE->hasLoopInvariantBackedgeTakenCount(L))
    return false;

  const SCEV *BackedgeTakenCount = SE->getBackedgeTakenCount(L);
  LLVM_DEBUG(dbgs() << "\n Before Reroll:\n" << *(L->getHeader()) << "\n");
  LLVM_DEBUG(dbgs() << "LRR: backedge-taken count = " << *BackedgeTakenCount
               << "\n");

  // First, we need to find the induction variable with respect to which we can
  // reroll (there may be several possible options).
  SmallInstructionVector PossibleIVs;
  IVToIncMap.clear();
  LoopControlIV = nullptr;
  collectPossibleIVs(L, PossibleIVs);

  if (PossibleIVs.empty()) {
    LLVM_DEBUG(dbgs() << "LRR: No possible IVs found\n");
    return false;
  }

  ReductionTracker Reductions;
  collectPossibleReductions(L, Reductions);
  bool Changed = false;

  // For each possible IV, collect the associated possible set of 'root' nodes
  // (i+1, i+2, etc.).
  for (Instruction *PossibleIV : PossibleIVs)
    if (reroll(PossibleIV, L, Header, BackedgeTakenCount, Reductions)) {
      Changed = true;
      break;
    }
  LLVM_DEBUG(dbgs() << "\n After Reroll:\n" << *(L->getHeader()) << "\n");

  // Trip count of L has changed so SE must be re-evaluated.
  if (Changed)
    SE->forgetLoop(L);

  return Changed;
}
