//===-- SafepointIRVerifier.cpp - Verify gc.statepoint invariants ---------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// Run a sanity check on the IR to ensure that Safepoints - if they've been
// inserted - were inserted correctly.  In particular, look for use of
// non-relocated values after a safepoint.  It's primary use is to check the
// correctness of safepoint insertion immediately after insertion, but it can
// also be used to verify that later transforms have not found a way to break
// safepoint semenatics.
//
// In its current form, this verify checks a property which is sufficient, but
// not neccessary for correctness.  There are some cases where an unrelocated
// pointer can be used after the safepoint.  Consider this example:
//
//    a = ...
//    b = ...
//    (a',b') = safepoint(a,b)
//    c = cmp eq a b
//    br c, ..., ....
//
// Because it is valid to reorder 'c' above the safepoint, this is legal.  In
// practice, this is a somewhat uncommon transform, but CodeGenPrep does create
// idioms like this.  The verifier knows about these cases and avoids reporting
// false positives.
//
//===----------------------------------------------------------------------===//

#include "llvm/ADT/DenseSet.h"
#include "llvm/ADT/PostOrderIterator.h"
#include "llvm/ADT/SetOperations.h"
#include "llvm/ADT/SetVector.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/Dominators.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Intrinsics.h"
#include "llvm/IR/IntrinsicInst.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Value.h"
#include "llvm/IR/SafepointIRVerifier.h"
#include "llvm/IR/Statepoint.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/raw_ostream.h"

#define DEBUG_TYPE "safepoint-ir-verifier"

using namespace llvm;

/// This option is used for writing test cases.  Instead of crashing the program
/// when verification fails, report a message to the console (for FileCheck
/// usage) and continue execution as if nothing happened.
static cl::opt<bool> PrintOnly("safepoint-ir-verifier-print-only",
                               cl::init(false));

namespace {

/// This CFG Deadness finds dead blocks and edges. Algorithm starts with a set
/// of blocks unreachable from entry then propagates deadness using foldable
/// conditional branches without modifying CFG. So GVN does but it changes CFG
/// by splitting critical edges. In most cases passes rely on SimplifyCFG to
/// clean up dead blocks, but in some cases, like verification or loop passes
/// it's not possible.
class CFGDeadness {
  const DominatorTree *DT = nullptr;
  SetVector<const BasicBlock *> DeadBlocks;
  SetVector<const Use *> DeadEdges; // Contains all dead edges from live blocks.

public:
  /// Return the edge that coresponds to the predecessor.
  static const Use& getEdge(const_pred_iterator &PredIt) {
    auto &PU = PredIt.getUse();
    return PU.getUser()->getOperandUse(PU.getOperandNo());
  }

  /// Return true if there is at least one live edge that corresponds to the
  /// basic block InBB listed in the phi node.
  bool hasLiveIncomingEdge(const PHINode *PN, const BasicBlock *InBB) const {
    assert(!isDeadBlock(InBB) && "block must be live");
    const BasicBlock* BB = PN->getParent();
    bool Listed = false;
    for (const_pred_iterator PredIt(BB), End(BB, true); PredIt != End; ++PredIt) {
      if (InBB == *PredIt) {
        if (!isDeadEdge(&getEdge(PredIt)))
          return true;
        Listed = true;
      }
    }
    (void)Listed;
    assert(Listed && "basic block is not found among incoming blocks");
    return false;
  }


  bool isDeadBlock(const BasicBlock *BB) const {
    return DeadBlocks.count(BB);
  }

  bool isDeadEdge(const Use *U) const {
    assert(dyn_cast<Instruction>(U->getUser())->isTerminator() &&
           "edge must be operand of terminator");
    assert(cast_or_null<BasicBlock>(U->get()) &&
           "edge must refer to basic block");
    assert(!isDeadBlock(dyn_cast<Instruction>(U->getUser())->getParent()) &&
           "isDeadEdge() must be applied to edge from live block");
    return DeadEdges.count(U);
  }

  bool hasLiveIncomingEdges(const BasicBlock *BB) const {
    // Check if all incoming edges are dead.
    for (const_pred_iterator PredIt(BB), End(BB, true); PredIt != End; ++PredIt) {
      auto &PU = PredIt.getUse();
      const Use &U = PU.getUser()->getOperandUse(PU.getOperandNo());
      if (!isDeadBlock(*PredIt) && !isDeadEdge(&U))
        return true; // Found a live edge.
    }
    return false;
  }

  void processFunction(const Function &F, const DominatorTree &DT) {
    this->DT = &DT;

    // Start with all blocks unreachable from entry.
    for (const BasicBlock &BB : F)
      if (!DT.isReachableFromEntry(&BB))
        DeadBlocks.insert(&BB);

    // Top-down walk of the dominator tree
    ReversePostOrderTraversal<const Function *> RPOT(&F);
    for (const BasicBlock *BB : RPOT) {
      const Instruction *TI = BB->getTerminator();
      assert(TI && "blocks must be well formed");

      // For conditional branches, we can perform simple conditional propagation on
      // the condition value itself.
      const BranchInst *BI = dyn_cast<BranchInst>(TI);
      if (!BI || !BI->isConditional() || !isa<Constant>(BI->getCondition()))
        continue;

      // If a branch has two identical successors, we cannot declare either dead.
      if (BI->getSuccessor(0) == BI->getSuccessor(1))
        continue;

      ConstantInt *Cond = dyn_cast<ConstantInt>(BI->getCondition());
      if (!Cond)
        continue;

      addDeadEdge(BI->getOperandUse(Cond->getZExtValue() ? 1 : 2));
    }
  }

protected:
  void addDeadBlock(const BasicBlock *BB) {
    SmallVector<const BasicBlock *, 4> NewDead;
    SmallSetVector<const BasicBlock *, 4> DF;

    NewDead.push_back(BB);
    while (!NewDead.empty()) {
      const BasicBlock *D = NewDead.pop_back_val();
      if (isDeadBlock(D))
        continue;

      // All blocks dominated by D are dead.
      SmallVector<BasicBlock *, 8> Dom;
      DT->getDescendants(const_cast<BasicBlock*>(D), Dom);
      // Do not need to mark all in and out edges dead
      // because BB is marked dead and this is enough
      // to run further.
      DeadBlocks.insert(Dom.begin(), Dom.end());

      // Figure out the dominance-frontier(D).
      for (BasicBlock *B : Dom)
        for (BasicBlock *S : successors(B))
          if (!isDeadBlock(S) && !hasLiveIncomingEdges(S))
            NewDead.push_back(S);
    }
  }

  void addDeadEdge(const Use &DeadEdge) {
    if (!DeadEdges.insert(&DeadEdge))
      return;

    BasicBlock *BB = cast_or_null<BasicBlock>(DeadEdge.get());
    if (hasLiveIncomingEdges(BB))
      return;

    addDeadBlock(BB);
  }
};
} // namespace

static void Verify(const Function &F, const DominatorTree &DT,
                   const CFGDeadness &CD);

namespace {

struct SafepointIRVerifier : public FunctionPass {
  static char ID; // Pass identification, replacement for typeid
  SafepointIRVerifier() : FunctionPass(ID) {
    initializeSafepointIRVerifierPass(*PassRegistry::getPassRegistry());
  }

  bool runOnFunction(Function &F) override {
    auto &DT = getAnalysis<DominatorTreeWrapperPass>().getDomTree();
    CFGDeadness CD;
    CD.processFunction(F, DT);
    Verify(F, DT, CD);
    return false; // no modifications
  }

  void getAnalysisUsage(AnalysisUsage &AU) const override {
    AU.addRequiredID(DominatorTreeWrapperPass::ID);
    AU.setPreservesAll();
  }

  StringRef getPassName() const override { return "safepoint verifier"; }
};
} // namespace

void llvm::verifySafepointIR(Function &F) {
  SafepointIRVerifier pass;
  pass.runOnFunction(F);
}

char SafepointIRVerifier::ID = 0;

FunctionPass *llvm::createSafepointIRVerifierPass() {
  return new SafepointIRVerifier();
}

INITIALIZE_PASS_BEGIN(SafepointIRVerifier, "verify-safepoint-ir",
                      "Safepoint IR Verifier", false, false)
INITIALIZE_PASS_DEPENDENCY(DominatorTreeWrapperPass)
INITIALIZE_PASS_END(SafepointIRVerifier, "verify-safepoint-ir",
                    "Safepoint IR Verifier", false, false)

static bool isGCPointerType(Type *T) {
  if (auto *PT = dyn_cast<PointerType>(T))
    // For the sake of this example GC, we arbitrarily pick addrspace(1) as our
    // GC managed heap.  We know that a pointer into this heap needs to be
    // updated and that no other pointer does.
    return (1 == PT->getAddressSpace());
  return false;
}

static bool containsGCPtrType(Type *Ty) {
  if (isGCPointerType(Ty))
    return true;
  if (VectorType *VT = dyn_cast<VectorType>(Ty))
    return isGCPointerType(VT->getScalarType());
  if (ArrayType *AT = dyn_cast<ArrayType>(Ty))
    return containsGCPtrType(AT->getElementType());
  if (StructType *ST = dyn_cast<StructType>(Ty))
    return llvm::any_of(ST->elements(), containsGCPtrType);
  return false;
}

// Debugging aid -- prints a [Begin, End) range of values.
template<typename IteratorTy>
static void PrintValueSet(raw_ostream &OS, IteratorTy Begin, IteratorTy End) {
  OS << "[ ";
  while (Begin != End) {
    OS << **Begin << " ";
    ++Begin;
  }
  OS << "]";
}

/// The verifier algorithm is phrased in terms of availability.  The set of
/// values "available" at a given point in the control flow graph is the set of
/// correctly relocated value at that point, and is a subset of the set of
/// definitions dominating that point.

using AvailableValueSet = DenseSet<const Value *>;

/// State we compute and track per basic block.
struct BasicBlockState {
  // Set of values available coming in, before the phi nodes
  AvailableValueSet AvailableIn;

  // Set of values available going out
  AvailableValueSet AvailableOut;

  // AvailableOut minus AvailableIn.
  // All elements are Instructions
  AvailableValueSet Contribution;

  // True if this block contains a safepoint and thus AvailableIn does not
  // contribute to AvailableOut.
  bool Cleared = false;
};

/// A given derived pointer can have multiple base pointers through phi/selects.
/// This type indicates when the base pointer is exclusively constant
/// (ExclusivelySomeConstant), and if that constant is proven to be exclusively
/// null, we record that as ExclusivelyNull. In all other cases, the BaseType is
/// NonConstant.
enum BaseType {
  NonConstant = 1, // Base pointers is not exclusively constant.
  ExclusivelyNull,
  ExclusivelySomeConstant // Base pointers for a given derived pointer is from a
                          // set of constants, but they are not exclusively
                          // null.
};

/// Return the baseType for Val which states whether Val is exclusively
/// derived from constant/null, or not exclusively derived from constant.
/// Val is exclusively derived off a constant base when all operands of phi and
/// selects are derived off a constant base.
static enum BaseType getBaseType(const Value *Val) {

  SmallVector<const Value *, 32> Worklist;
  DenseSet<const Value *> Visited;
  bool isExclusivelyDerivedFromNull = true;
  Worklist.push_back(Val);
  // Strip through all the bitcasts and geps to get base pointer. Also check for
  // the exclusive value when there can be multiple base pointers (through phis
  // or selects).
  while(!Worklist.empty()) {
    const Value *V = Worklist.pop_back_val();
    if (!Visited.insert(V).second)
      continue;

    if (const auto *CI = dyn_cast<CastInst>(V)) {
      Worklist.push_back(CI->stripPointerCasts());
      continue;
    }
    if (const auto *GEP = dyn_cast<GetElementPtrInst>(V)) {
      Worklist.push_back(GEP->getPointerOperand());
      continue;
    }
    // Push all the incoming values of phi node into the worklist for
    // processing.
    if (const auto *PN = dyn_cast<PHINode>(V)) {
      for (Value *InV: PN->incoming_values())
        Worklist.push_back(InV);
      continue;
    }
    if (const auto *SI = dyn_cast<SelectInst>(V)) {
      // Push in the true and false values
      Worklist.push_back(SI->getTrueValue());
      Worklist.push_back(SI->getFalseValue());
      continue;
    }
    if (isa<Constant>(V)) {
      // We found at least one base pointer which is non-null, so this derived
      // pointer is not exclusively derived from null.
      if (V != Constant::getNullValue(V->getType()))
        isExclusivelyDerivedFromNull = false;
      // Continue processing the remaining values to make sure it's exclusively
      // constant.
      continue;
    }
    // At this point, we know that the base pointer is not exclusively
    // constant.
    return BaseType::NonConstant;
  }
  // Now, we know that the base pointer is exclusively constant, but we need to
  // differentiate between exclusive null constant and non-null constant.
  return isExclusivelyDerivedFromNull ? BaseType::ExclusivelyNull
                                      : BaseType::ExclusivelySomeConstant;
}

static bool isNotExclusivelyConstantDerived(const Value *V) {
  return getBaseType(V) == BaseType::NonConstant;
}

namespace {
class InstructionVerifier;

/// Builds BasicBlockState for each BB of the function.
/// It can traverse function for verification and provides all required
/// information.
///
/// GC pointer may be in one of three states: relocated, unrelocated and
/// poisoned.
/// Relocated pointer may be used without any restrictions.
/// Unrelocated pointer cannot be dereferenced, passed as argument to any call
/// or returned. Unrelocated pointer may be safely compared against another
/// unrelocated pointer or against a pointer exclusively derived from null.
/// Poisoned pointers are produced when we somehow derive pointer from relocated
/// and unrelocated pointers (e.g. phi, select). This pointers may be safely
/// used in a very limited number of situations. Currently the only way to use
/// it is comparison against constant exclusively derived from null. All
/// limitations arise due to their undefined state: this pointers should be
/// treated as relocated and unrelocated simultaneously.
/// Rules of deriving:
/// R + U = P - that's where the poisoned pointers come from
/// P + X = P
/// U + U = U
/// R + R = R
/// X + C = X
/// Where "+" - any operation that somehow derive pointer, U - unrelocated,
/// R - relocated and P - poisoned, C - constant, X - U or R or P or C or
/// nothing (in case when "+" is unary operation).
/// Deriving of pointers by itself is always safe.
/// NOTE: when we are making decision on the status of instruction's result:
/// a) for phi we need to check status of each input *at the end of
///    corresponding predecessor BB*.
/// b) for other instructions we need to check status of each input *at the
///    current point*.
///
/// FIXME: This works fairly well except one case
///     bb1:
///     p = *some GC-ptr def*
///     p1 = gep p, offset
///         /     |
///        /      |
///    bb2:       |
///    safepoint  |
///        \      |
///         \     |
///      bb3:
///      p2 = phi [p, bb2] [p1, bb1]
///      p3 = phi [p, bb2] [p, bb1]
///      here p and p1 is unrelocated
///           p2 and p3 is poisoned (though they shouldn't be)
///
/// This leads to some weird results:
///      cmp eq p, p2 - illegal instruction (false-positive)
///      cmp eq p1, p2 - illegal instruction (false-positive)
///      cmp eq p, p3 - illegal instruction (false-positive)
///      cmp eq p, p1 - ok
/// To fix this we need to introduce conception of generations and be able to
/// check if two values belong to one generation or not. This way p2 will be
/// considered to be unrelocated and no false alarm will happen.
class GCPtrTracker {
  const Function &F;
  const CFGDeadness &CD;
  SpecificBumpPtrAllocator<BasicBlockState> BSAllocator;
  DenseMap<const BasicBlock *, BasicBlockState *> BlockMap;
  // This set contains defs of unrelocated pointers that are proved to be legal
  // and don't need verification.
  DenseSet<const Instruction *> ValidUnrelocatedDefs;
  // This set contains poisoned defs. They can be safely ignored during
  // verification too.
  DenseSet<const Value *> PoisonedDefs;

public:
  GCPtrTracker(const Function &F, const DominatorTree &DT,
               const CFGDeadness &CD);

  bool hasLiveIncomingEdge(const PHINode *PN, const BasicBlock *InBB) const {
    return CD.hasLiveIncomingEdge(PN, InBB);
  }

  BasicBlockState *getBasicBlockState(const BasicBlock *BB);
  const BasicBlockState *getBasicBlockState(const BasicBlock *BB) const;

  bool isValuePoisoned(const Value *V) const { return PoisonedDefs.count(V); }

  /// Traverse each BB of the function and call
  /// InstructionVerifier::verifyInstruction for each possibly invalid
  /// instruction.
  /// It destructively modifies GCPtrTracker so it's passed via rvalue reference
  /// in order to prohibit further usages of GCPtrTracker as it'll be in
  /// inconsistent state.
  static void verifyFunction(GCPtrTracker &&Tracker,
                             InstructionVerifier &Verifier);

  /// Returns true for reachable and live blocks.
  bool isMapped(const BasicBlock *BB) const {
    return BlockMap.find(BB) != BlockMap.end();
  }

private:
  /// Returns true if the instruction may be safely skipped during verification.
  bool instructionMayBeSkipped(const Instruction *I) const;

  /// Iterates over all BBs from BlockMap and recalculates AvailableIn/Out for
  /// each of them until it converges.
  void recalculateBBsStates();

  /// Remove from Contribution all defs that legally produce unrelocated
  /// pointers and saves them to ValidUnrelocatedDefs.
  /// Though Contribution should belong to BBS it is passed separately with
  /// different const-modifier in order to emphasize (and guarantee) that only
  /// Contribution will be changed.
  /// Returns true if Contribution was changed otherwise false.
  bool removeValidUnrelocatedDefs(const BasicBlock *BB,
                                  const BasicBlockState *BBS,
                                  AvailableValueSet &Contribution);

  /// Gather all the definitions dominating the start of BB into Result. This is
  /// simply the defs introduced by every dominating basic block and the
  /// function arguments.
  void gatherDominatingDefs(const BasicBlock *BB, AvailableValueSet &Result,
                            const DominatorTree &DT);

  /// Compute the AvailableOut set for BB, based on the BasicBlockState BBS,
  /// which is the BasicBlockState for BB.
  /// ContributionChanged is set when the verifier runs for the first time
  /// (in this case Contribution was changed from 'empty' to its initial state)
  /// or when Contribution of this BB was changed since last computation.
  static void transferBlock(const BasicBlock *BB, BasicBlockState &BBS,
                            bool ContributionChanged);

  /// Model the effect of an instruction on the set of available values.
  static void transferInstruction(const Instruction &I, bool &Cleared,
                                  AvailableValueSet &Available);
};

/// It is a visitor for GCPtrTracker::verifyFunction. It decides if the
/// instruction (which uses heap reference) is legal or not, given our safepoint
/// semantics.
class InstructionVerifier {
  bool AnyInvalidUses = false;

public:
  void verifyInstruction(const GCPtrTracker *Tracker, const Instruction &I,
                         const AvailableValueSet &AvailableSet);

  bool hasAnyInvalidUses() const { return AnyInvalidUses; }

private:
  void reportInvalidUse(const Value &V, const Instruction &I);
};
} // end anonymous namespace

GCPtrTracker::GCPtrTracker(const Function &F, const DominatorTree &DT,
                           const CFGDeadness &CD) : F(F), CD(CD) {
  // Calculate Contribution of each live BB.
  // Allocate BB states for live blocks.
  for (const BasicBlock &BB : F)
    if (!CD.isDeadBlock(&BB)) {
      BasicBlockState *BBS = new (BSAllocator.Allocate()) BasicBlockState;
      for (const auto &I : BB)
        transferInstruction(I, BBS->Cleared, BBS->Contribution);
      BlockMap[&BB] = BBS;
    }

  // Initialize AvailableIn/Out sets of each BB using only information about
  // dominating BBs.
  for (auto &BBI : BlockMap) {
    gatherDominatingDefs(BBI.first, BBI.second->AvailableIn, DT);
    transferBlock(BBI.first, *BBI.second, true);
  }

  // Simulate the flow of defs through the CFG and recalculate AvailableIn/Out
  // sets of each BB until it converges. If any def is proved to be an
  // unrelocated pointer, it will be removed from all BBSs.
  recalculateBBsStates();
}

BasicBlockState *GCPtrTracker::getBasicBlockState(const BasicBlock *BB) {
  auto it = BlockMap.find(BB);
  return it != BlockMap.end() ? it->second : nullptr;
}

const BasicBlockState *GCPtrTracker::getBasicBlockState(
    const BasicBlock *BB) const {
  return const_cast<GCPtrTracker *>(this)->getBasicBlockState(BB);
}

bool GCPtrTracker::instructionMayBeSkipped(const Instruction *I) const {
  // Poisoned defs are skipped since they are always safe by itself by
  // definition (for details see comment to this class).
  return ValidUnrelocatedDefs.count(I) || PoisonedDefs.count(I);
}

void GCPtrTracker::verifyFunction(GCPtrTracker &&Tracker,
                                  InstructionVerifier &Verifier) {
  // We need RPO here to a) report always the first error b) report errors in
  // same order from run to run.
  ReversePostOrderTraversal<const Function *> RPOT(&Tracker.F);
  for (const BasicBlock *BB : RPOT) {
    BasicBlockState *BBS = Tracker.getBasicBlockState(BB);
    if (!BBS)
      continue;

    // We destructively modify AvailableIn as we traverse the block instruction
    // by instruction.
    AvailableValueSet &AvailableSet = BBS->AvailableIn;
    for (const Instruction &I : *BB) {
      if (Tracker.instructionMayBeSkipped(&I))
        continue; // This instruction shouldn't be added to AvailableSet.

      Verifier.verifyInstruction(&Tracker, I, AvailableSet);

      // Model the effect of current instruction on AvailableSet to keep the set
      // relevant at each point of BB.
      bool Cleared = false;
      transferInstruction(I, Cleared, AvailableSet);
      (void)Cleared;
    }
  }
}

void GCPtrTracker::recalculateBBsStates() {
  SetVector<const BasicBlock *> Worklist;
  // TODO: This order is suboptimal, it's better to replace it with priority
  // queue where priority is RPO number of BB.
  for (auto &BBI : BlockMap)
    Worklist.insert(BBI.first);

  // This loop iterates the AvailableIn/Out sets until it converges.
  // The AvailableIn and AvailableOut sets decrease as we iterate.
  while (!Worklist.empty()) {
    const BasicBlock *BB = Worklist.pop_back_val();
    BasicBlockState *BBS = getBasicBlockState(BB);
    if (!BBS)
      continue; // Ignore dead successors.

    size_t OldInCount = BBS->AvailableIn.size();
    for (const_pred_iterator PredIt(BB), End(BB, true); PredIt != End; ++PredIt) {
      const BasicBlock *PBB = *PredIt;
      BasicBlockState *PBBS = getBasicBlockState(PBB);
      if (PBBS && !CD.isDeadEdge(&CFGDeadness::getEdge(PredIt)))
        set_intersect(BBS->AvailableIn, PBBS->AvailableOut);
    }

    assert(OldInCount >= BBS->AvailableIn.size() && "invariant!");

    bool InputsChanged = OldInCount != BBS->AvailableIn.size();
    bool ContributionChanged =
        removeValidUnrelocatedDefs(BB, BBS, BBS->Contribution);
    if (!InputsChanged && !ContributionChanged)
      continue;

    size_t OldOutCount = BBS->AvailableOut.size();
    transferBlock(BB, *BBS, ContributionChanged);
    if (OldOutCount != BBS->AvailableOut.size()) {
      assert(OldOutCount > BBS->AvailableOut.size() && "invariant!");
      Worklist.insert(succ_begin(BB), succ_end(BB));
    }
  }
}

bool GCPtrTracker::removeValidUnrelocatedDefs(const BasicBlock *BB,
                                              const BasicBlockState *BBS,
                                              AvailableValueSet &Contribution) {
  assert(&BBS->Contribution == &Contribution &&
         "Passed Contribution should be from the passed BasicBlockState!");
  AvailableValueSet AvailableSet = BBS->AvailableIn;
  bool ContributionChanged = false;
  // For explanation why instructions are processed this way see
  // "Rules of deriving" in the comment to this class.
  for (const Instruction &I : *BB) {
    bool ValidUnrelocatedPointerDef = false;
    bool PoisonedPointerDef = false;
    // TODO: `select` instructions should be handled here too.
    if (const PHINode *PN = dyn_cast<PHINode>(&I)) {
      if (containsGCPtrType(PN->getType())) {
        // If both is true, output is poisoned.
        bool HasRelocatedInputs = false;
        bool HasUnrelocatedInputs = false;
        for (unsigned i = 0, e = PN->getNumIncomingValues(); i != e; ++i) {
          const BasicBlock *InBB = PN->getIncomingBlock(i);
          if (!isMapped(InBB) ||
              !CD.hasLiveIncomingEdge(PN, InBB))
            continue; // Skip dead block or dead edge.

          const Value *InValue = PN->getIncomingValue(i);

          if (isNotExclusivelyConstantDerived(InValue)) {
            if (isValuePoisoned(InValue)) {
              // If any of inputs is poisoned, output is always poisoned too.
              HasRelocatedInputs = true;
              HasUnrelocatedInputs = true;
              break;
            }
            if (BlockMap[InBB]->AvailableOut.count(InValue))
              HasRelocatedInputs = true;
            else
              HasUnrelocatedInputs = true;
          }
        }
        if (HasUnrelocatedInputs) {
          if (HasRelocatedInputs)
            PoisonedPointerDef = true;
          else
            ValidUnrelocatedPointerDef = true;
        }
      }
    } else if ((isa<GetElementPtrInst>(I) || isa<BitCastInst>(I)) &&
               containsGCPtrType(I.getType())) {
      // GEP/bitcast of unrelocated pointer is legal by itself but this def
      // shouldn't appear in any AvailableSet.
      for (const Value *V : I.operands())
        if (containsGCPtrType(V->getType()) &&
            isNotExclusivelyConstantDerived(V) && !AvailableSet.count(V)) {
          if (isValuePoisoned(V))
            PoisonedPointerDef = true;
          else
            ValidUnrelocatedPointerDef = true;
          break;
        }
    }
    assert(!(ValidUnrelocatedPointerDef && PoisonedPointerDef) &&
           "Value cannot be both unrelocated and poisoned!");
    if (ValidUnrelocatedPointerDef) {
      // Remove def of unrelocated pointer from Contribution of this BB and
      // trigger update of all its successors.
      Contribution.erase(&I);
      PoisonedDefs.erase(&I);
      ValidUnrelocatedDefs.insert(&I);
      LLVM_DEBUG(dbgs() << "Removing urelocated " << I
                        << " from Contribution of " << BB->getName() << "\n");
      ContributionChanged = true;
    } else if (PoisonedPointerDef) {
      // Mark pointer as poisoned, remove its def from Contribution and trigger
      // update of all successors.
      Contribution.erase(&I);
      PoisonedDefs.insert(&I);
      LLVM_DEBUG(dbgs() << "Removing poisoned " << I << " from Contribution of "
                        << BB->getName() << "\n");
      ContributionChanged = true;
    } else {
      bool Cleared = false;
      transferInstruction(I, Cleared, AvailableSet);
      (void)Cleared;
    }
  }
  return ContributionChanged;
}

void GCPtrTracker::gatherDominatingDefs(const BasicBlock *BB,
                                        AvailableValueSet &Result,
                                        const DominatorTree &DT) {
  DomTreeNode *DTN = DT[const_cast<BasicBlock *>(BB)];

  assert(DTN && "Unreachable blocks are ignored");
  while (DTN->getIDom()) {
    DTN = DTN->getIDom();
    auto BBS = getBasicBlockState(DTN->getBlock());
    assert(BBS && "immediate dominator cannot be dead for a live block");
    const auto &Defs = BBS->Contribution;
    Result.insert(Defs.begin(), Defs.end());
    // If this block is 'Cleared', then nothing LiveIn to this block can be
    // available after this block completes.  Note: This turns out to be
    // really important for reducing memory consuption of the initial available
    // sets and thus peak memory usage by this verifier.
    if (BBS->Cleared)
      return;
  }

  for (const Argument &A : BB->getParent()->args())
    if (containsGCPtrType(A.getType()))
      Result.insert(&A);
}

void GCPtrTracker::transferBlock(const BasicBlock *BB, BasicBlockState &BBS,
                                 bool ContributionChanged) {
  const AvailableValueSet &AvailableIn = BBS.AvailableIn;
  AvailableValueSet &AvailableOut = BBS.AvailableOut;

  if (BBS.Cleared) {
    // AvailableOut will change only when Contribution changed.
    if (ContributionChanged)
      AvailableOut = BBS.Contribution;
  } else {
    // Otherwise, we need to reduce the AvailableOut set by things which are no
    // longer in our AvailableIn
    AvailableValueSet Temp = BBS.Contribution;
    set_union(Temp, AvailableIn);
    AvailableOut = std::move(Temp);
  }

  LLVM_DEBUG(dbgs() << "Transfered block " << BB->getName() << " from ";
             PrintValueSet(dbgs(), AvailableIn.begin(), AvailableIn.end());
             dbgs() << " to ";
             PrintValueSet(dbgs(), AvailableOut.begin(), AvailableOut.end());
             dbgs() << "\n";);
}

void GCPtrTracker::transferInstruction(const Instruction &I, bool &Cleared,
                                       AvailableValueSet &Available) {
  if (isStatepoint(I)) {
    Cleared = true;
    Available.clear();
  } else if (containsGCPtrType(I.getType()))
    Available.insert(&I);
}

void InstructionVerifier::verifyInstruction(
    const GCPtrTracker *Tracker, const Instruction &I,
    const AvailableValueSet &AvailableSet) {
  if (const PHINode *PN = dyn_cast<PHINode>(&I)) {
    if (containsGCPtrType(PN->getType()))
      for (unsigned i = 0, e = PN->getNumIncomingValues(); i != e; ++i) {
        const BasicBlock *InBB = PN->getIncomingBlock(i);
        const BasicBlockState *InBBS = Tracker->getBasicBlockState(InBB);
        if (!InBBS ||
            !Tracker->hasLiveIncomingEdge(PN, InBB))
          continue; // Skip dead block or dead edge.

        const Value *InValue = PN->getIncomingValue(i);

        if (isNotExclusivelyConstantDerived(InValue) &&
            !InBBS->AvailableOut.count(InValue))
          reportInvalidUse(*InValue, *PN);
      }
  } else if (isa<CmpInst>(I) &&
             containsGCPtrType(I.getOperand(0)->getType())) {
    Value *LHS = I.getOperand(0), *RHS = I.getOperand(1);
    enum BaseType baseTyLHS = getBaseType(LHS),
                  baseTyRHS = getBaseType(RHS);

    // Returns true if LHS and RHS are unrelocated pointers and they are
    // valid unrelocated uses.
    auto hasValidUnrelocatedUse = [&AvailableSet, Tracker, baseTyLHS, baseTyRHS,
                                   &LHS, &RHS] () {
        // A cmp instruction has valid unrelocated pointer operands only if
        // both operands are unrelocated pointers.
        // In the comparison between two pointers, if one is an unrelocated
        // use, the other *should be* an unrelocated use, for this
        // instruction to contain valid unrelocated uses. This unrelocated
        // use can be a null constant as well, or another unrelocated
        // pointer.
        if (AvailableSet.count(LHS) || AvailableSet.count(RHS))
          return false;
        // Constant pointers (that are not exclusively null) may have
        // meaning in different VMs, so we cannot reorder the compare
        // against constant pointers before the safepoint. In other words,
        // comparison of an unrelocated use against a non-null constant
        // maybe invalid.
        if ((baseTyLHS == BaseType::ExclusivelySomeConstant &&
             baseTyRHS == BaseType::NonConstant) ||
            (baseTyLHS == BaseType::NonConstant &&
             baseTyRHS == BaseType::ExclusivelySomeConstant))
          return false;

        // If one of pointers is poisoned and other is not exclusively derived
        // from null it is an invalid expression: it produces poisoned result
        // and unless we want to track all defs (not only gc pointers) the only
        // option is to prohibit such instructions.
        if ((Tracker->isValuePoisoned(LHS) && baseTyRHS != ExclusivelyNull) ||
            (Tracker->isValuePoisoned(RHS) && baseTyLHS != ExclusivelyNull))
            return false;

        // All other cases are valid cases enumerated below:
        // 1. Comparison between an exclusively derived null pointer and a
        // constant base pointer.
        // 2. Comparison between an exclusively derived null pointer and a
        // non-constant unrelocated base pointer.
        // 3. Comparison between 2 unrelocated pointers.
        // 4. Comparison between a pointer exclusively derived from null and a
        // non-constant poisoned pointer.
        return true;
    };
    if (!hasValidUnrelocatedUse()) {
      // Print out all non-constant derived pointers that are unrelocated
      // uses, which are invalid.
      if (baseTyLHS == BaseType::NonConstant && !AvailableSet.count(LHS))
        reportInvalidUse(*LHS, I);
      if (baseTyRHS == BaseType::NonConstant && !AvailableSet.count(RHS))
        reportInvalidUse(*RHS, I);
    }
  } else {
    for (const Value *V : I.operands())
      if (containsGCPtrType(V->getType()) &&
          isNotExclusivelyConstantDerived(V) && !AvailableSet.count(V))
        reportInvalidUse(*V, I);
  }
}

void InstructionVerifier::reportInvalidUse(const Value &V,
                                           const Instruction &I) {
  errs() << "Illegal use of unrelocated value found!\n";
  errs() << "Def: " << V << "\n";
  errs() << "Use: " << I << "\n";
  if (!PrintOnly)
    abort();
  AnyInvalidUses = true;
}

static void Verify(const Function &F, const DominatorTree &DT,
                   const CFGDeadness &CD) {
  LLVM_DEBUG(dbgs() << "Verifying gc pointers in function: " << F.getName()
                    << "\n");
  if (PrintOnly)
    dbgs() << "Verifying gc pointers in function: " << F.getName() << "\n";

  GCPtrTracker Tracker(F, DT, CD);

  // We now have all the information we need to decide if the use of a heap
  // reference is legal or not, given our safepoint semantics.

  InstructionVerifier Verifier;
  GCPtrTracker::verifyFunction(std::move(Tracker), Verifier);

  if (PrintOnly && !Verifier.hasAnyInvalidUses()) {
    dbgs() << "No illegal uses found by SafepointIRVerifier in: " << F.getName()
           << "\n";
  }
}
