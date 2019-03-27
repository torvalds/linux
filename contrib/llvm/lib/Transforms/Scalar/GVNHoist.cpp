//===- GVNHoist.cpp - Hoist scalar and load expressions -------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This pass hoists expressions from branches to a common dominator. It uses
// GVN (global value numbering) to discover expressions computing the same
// values. The primary goals of code-hoisting are:
// 1. To reduce the code size.
// 2. In some cases reduce critical path (by exposing more ILP).
//
// The algorithm factors out the reachability of values such that multiple
// queries to find reachability of values are fast. This is based on finding the
// ANTIC points in the CFG which do not change during hoisting. The ANTIC points
// are basically the dominance-frontiers in the inverse graph. So we introduce a
// data structure (CHI nodes) to keep track of values flowing out of a basic
// block. We only do this for values with multiple occurrences in the function
// as they are the potential hoistable candidates. This approach allows us to
// hoist instructions to a basic block with more than two successors, as well as
// deal with infinite loops in a trivial way.
//
// Limitations: This pass does not hoist fully redundant expressions because
// they are already handled by GVN-PRE. It is advisable to run gvn-hoist before
// and after gvn-pre because gvn-pre creates opportunities for more instructions
// to be hoisted.
//
// Hoisting may affect the performance in some cases. To mitigate that, hoisting
// is disabled in the following cases.
// 1. Scalars across calls.
// 2. geps when corresponding load/store cannot be hoisted.
//===----------------------------------------------------------------------===//

#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/DenseSet.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/ADT/iterator_range.h"
#include "llvm/Analysis/AliasAnalysis.h"
#include "llvm/Analysis/GlobalsModRef.h"
#include "llvm/Analysis/IteratedDominanceFrontier.h"
#include "llvm/Analysis/MemoryDependenceAnalysis.h"
#include "llvm/Analysis/MemorySSA.h"
#include "llvm/Analysis/MemorySSAUpdater.h"
#include "llvm/Analysis/PostDominators.h"
#include "llvm/Transforms/Utils/Local.h"
#include "llvm/Analysis/ValueTracking.h"
#include "llvm/IR/Argument.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/CFG.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/Dominators.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/InstrTypes.h"
#include "llvm/IR/Instruction.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/IntrinsicInst.h"
#include "llvm/IR/Intrinsics.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/PassManager.h"
#include "llvm/IR/Use.h"
#include "llvm/IR/User.h"
#include "llvm/IR/Value.h"
#include "llvm/Pass.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Transforms/Scalar.h"
#include "llvm/Transforms/Scalar/GVN.h"
#include <algorithm>
#include <cassert>
#include <iterator>
#include <memory>
#include <utility>
#include <vector>

using namespace llvm;

#define DEBUG_TYPE "gvn-hoist"

STATISTIC(NumHoisted, "Number of instructions hoisted");
STATISTIC(NumRemoved, "Number of instructions removed");
STATISTIC(NumLoadsHoisted, "Number of loads hoisted");
STATISTIC(NumLoadsRemoved, "Number of loads removed");
STATISTIC(NumStoresHoisted, "Number of stores hoisted");
STATISTIC(NumStoresRemoved, "Number of stores removed");
STATISTIC(NumCallsHoisted, "Number of calls hoisted");
STATISTIC(NumCallsRemoved, "Number of calls removed");

static cl::opt<int>
    MaxHoistedThreshold("gvn-max-hoisted", cl::Hidden, cl::init(-1),
                        cl::desc("Max number of instructions to hoist "
                                 "(default unlimited = -1)"));

static cl::opt<int> MaxNumberOfBBSInPath(
    "gvn-hoist-max-bbs", cl::Hidden, cl::init(4),
    cl::desc("Max number of basic blocks on the path between "
             "hoisting locations (default = 4, unlimited = -1)"));

static cl::opt<int> MaxDepthInBB(
    "gvn-hoist-max-depth", cl::Hidden, cl::init(100),
    cl::desc("Hoist instructions from the beginning of the BB up to the "
             "maximum specified depth (default = 100, unlimited = -1)"));

static cl::opt<int>
    MaxChainLength("gvn-hoist-max-chain-length", cl::Hidden, cl::init(10),
                   cl::desc("Maximum length of dependent chains to hoist "
                            "(default = 10, unlimited = -1)"));

namespace llvm {

using BBSideEffectsSet = DenseMap<const BasicBlock *, bool>;
using SmallVecInsn = SmallVector<Instruction *, 4>;
using SmallVecImplInsn = SmallVectorImpl<Instruction *>;

// Each element of a hoisting list contains the basic block where to hoist and
// a list of instructions to be hoisted.
using HoistingPointInfo = std::pair<BasicBlock *, SmallVecInsn>;

using HoistingPointList = SmallVector<HoistingPointInfo, 4>;

// A map from a pair of VNs to all the instructions with those VNs.
using VNType = std::pair<unsigned, unsigned>;

using VNtoInsns = DenseMap<VNType, SmallVector<Instruction *, 4>>;

// CHI keeps information about values flowing out of a basic block.  It is
// similar to PHI but in the inverse graph, and used for outgoing values on each
// edge. For conciseness, it is computed only for instructions with multiple
// occurrences in the CFG because they are the only hoistable candidates.
//     A (CHI[{V, B, I1}, {V, C, I2}]
//  /     \
// /       \
// B(I1)  C (I2)
// The Value number for both I1 and I2 is V, the CHI node will save the
// instruction as well as the edge where the value is flowing to.
struct CHIArg {
  VNType VN;

  // Edge destination (shows the direction of flow), may not be where the I is.
  BasicBlock *Dest;

  // The instruction (VN) which uses the values flowing out of CHI.
  Instruction *I;

  bool operator==(const CHIArg &A) { return VN == A.VN; }
  bool operator!=(const CHIArg &A) { return !(*this == A); }
};

using CHIIt = SmallVectorImpl<CHIArg>::iterator;
using CHIArgs = iterator_range<CHIIt>;
using OutValuesType = DenseMap<BasicBlock *, SmallVector<CHIArg, 2>>;
using InValuesType =
    DenseMap<BasicBlock *, SmallVector<std::pair<VNType, Instruction *>, 2>>;

// An invalid value number Used when inserting a single value number into
// VNtoInsns.
enum : unsigned { InvalidVN = ~2U };

// Records all scalar instructions candidate for code hoisting.
class InsnInfo {
  VNtoInsns VNtoScalars;

public:
  // Inserts I and its value number in VNtoScalars.
  void insert(Instruction *I, GVN::ValueTable &VN) {
    // Scalar instruction.
    unsigned V = VN.lookupOrAdd(I);
    VNtoScalars[{V, InvalidVN}].push_back(I);
  }

  const VNtoInsns &getVNTable() const { return VNtoScalars; }
};

// Records all load instructions candidate for code hoisting.
class LoadInfo {
  VNtoInsns VNtoLoads;

public:
  // Insert Load and the value number of its memory address in VNtoLoads.
  void insert(LoadInst *Load, GVN::ValueTable &VN) {
    if (Load->isSimple()) {
      unsigned V = VN.lookupOrAdd(Load->getPointerOperand());
      VNtoLoads[{V, InvalidVN}].push_back(Load);
    }
  }

  const VNtoInsns &getVNTable() const { return VNtoLoads; }
};

// Records all store instructions candidate for code hoisting.
class StoreInfo {
  VNtoInsns VNtoStores;

public:
  // Insert the Store and a hash number of the store address and the stored
  // value in VNtoStores.
  void insert(StoreInst *Store, GVN::ValueTable &VN) {
    if (!Store->isSimple())
      return;
    // Hash the store address and the stored value.
    Value *Ptr = Store->getPointerOperand();
    Value *Val = Store->getValueOperand();
    VNtoStores[{VN.lookupOrAdd(Ptr), VN.lookupOrAdd(Val)}].push_back(Store);
  }

  const VNtoInsns &getVNTable() const { return VNtoStores; }
};

// Records all call instructions candidate for code hoisting.
class CallInfo {
  VNtoInsns VNtoCallsScalars;
  VNtoInsns VNtoCallsLoads;
  VNtoInsns VNtoCallsStores;

public:
  // Insert Call and its value numbering in one of the VNtoCalls* containers.
  void insert(CallInst *Call, GVN::ValueTable &VN) {
    // A call that doesNotAccessMemory is handled as a Scalar,
    // onlyReadsMemory will be handled as a Load instruction,
    // all other calls will be handled as stores.
    unsigned V = VN.lookupOrAdd(Call);
    auto Entry = std::make_pair(V, InvalidVN);

    if (Call->doesNotAccessMemory())
      VNtoCallsScalars[Entry].push_back(Call);
    else if (Call->onlyReadsMemory())
      VNtoCallsLoads[Entry].push_back(Call);
    else
      VNtoCallsStores[Entry].push_back(Call);
  }

  const VNtoInsns &getScalarVNTable() const { return VNtoCallsScalars; }
  const VNtoInsns &getLoadVNTable() const { return VNtoCallsLoads; }
  const VNtoInsns &getStoreVNTable() const { return VNtoCallsStores; }
};

static void combineKnownMetadata(Instruction *ReplInst, Instruction *I) {
  static const unsigned KnownIDs[] = {
      LLVMContext::MD_tbaa,           LLVMContext::MD_alias_scope,
      LLVMContext::MD_noalias,        LLVMContext::MD_range,
      LLVMContext::MD_fpmath,         LLVMContext::MD_invariant_load,
      LLVMContext::MD_invariant_group, LLVMContext::MD_access_group};
  combineMetadata(ReplInst, I, KnownIDs, true);
}

// This pass hoists common computations across branches sharing common
// dominator. The primary goal is to reduce the code size, and in some
// cases reduce critical path (by exposing more ILP).
class GVNHoist {
public:
  GVNHoist(DominatorTree *DT, PostDominatorTree *PDT, AliasAnalysis *AA,
           MemoryDependenceResults *MD, MemorySSA *MSSA)
      : DT(DT), PDT(PDT), AA(AA), MD(MD), MSSA(MSSA),
        MSSAUpdater(llvm::make_unique<MemorySSAUpdater>(MSSA)) {}

  bool run(Function &F) {
    NumFuncArgs = F.arg_size();
    VN.setDomTree(DT);
    VN.setAliasAnalysis(AA);
    VN.setMemDep(MD);
    bool Res = false;
    // Perform DFS Numbering of instructions.
    unsigned BBI = 0;
    for (const BasicBlock *BB : depth_first(&F.getEntryBlock())) {
      DFSNumber[BB] = ++BBI;
      unsigned I = 0;
      for (auto &Inst : *BB)
        DFSNumber[&Inst] = ++I;
    }

    int ChainLength = 0;

    // FIXME: use lazy evaluation of VN to avoid the fix-point computation.
    while (true) {
      if (MaxChainLength != -1 && ++ChainLength >= MaxChainLength)
        return Res;

      auto HoistStat = hoistExpressions(F);
      if (HoistStat.first + HoistStat.second == 0)
        return Res;

      if (HoistStat.second > 0)
        // To address a limitation of the current GVN, we need to rerun the
        // hoisting after we hoisted loads or stores in order to be able to
        // hoist all scalars dependent on the hoisted ld/st.
        VN.clear();

      Res = true;
    }

    return Res;
  }

  // Copied from NewGVN.cpp
  // This function provides global ranking of operations so that we can place
  // them in a canonical order.  Note that rank alone is not necessarily enough
  // for a complete ordering, as constants all have the same rank.  However,
  // generally, we will simplify an operation with all constants so that it
  // doesn't matter what order they appear in.
  unsigned int rank(const Value *V) const {
    // Prefer constants to undef to anything else
    // Undef is a constant, have to check it first.
    // Prefer smaller constants to constantexprs
    if (isa<ConstantExpr>(V))
      return 2;
    if (isa<UndefValue>(V))
      return 1;
    if (isa<Constant>(V))
      return 0;
    else if (auto *A = dyn_cast<Argument>(V))
      return 3 + A->getArgNo();

    // Need to shift the instruction DFS by number of arguments + 3 to account
    // for the constant and argument ranking above.
    auto Result = DFSNumber.lookup(V);
    if (Result > 0)
      return 4 + NumFuncArgs + Result;
    // Unreachable or something else, just return a really large number.
    return ~0;
  }

private:
  GVN::ValueTable VN;
  DominatorTree *DT;
  PostDominatorTree *PDT;
  AliasAnalysis *AA;
  MemoryDependenceResults *MD;
  MemorySSA *MSSA;
  std::unique_ptr<MemorySSAUpdater> MSSAUpdater;
  DenseMap<const Value *, unsigned> DFSNumber;
  BBSideEffectsSet BBSideEffects;
  DenseSet<const BasicBlock *> HoistBarrier;
  SmallVector<BasicBlock *, 32> IDFBlocks;
  unsigned NumFuncArgs;
  const bool HoistingGeps = false;

  enum InsKind { Unknown, Scalar, Load, Store };

  // Return true when there are exception handling in BB.
  bool hasEH(const BasicBlock *BB) {
    auto It = BBSideEffects.find(BB);
    if (It != BBSideEffects.end())
      return It->second;

    if (BB->isEHPad() || BB->hasAddressTaken()) {
      BBSideEffects[BB] = true;
      return true;
    }

    if (BB->getTerminator()->mayThrow()) {
      BBSideEffects[BB] = true;
      return true;
    }

    BBSideEffects[BB] = false;
    return false;
  }

  // Return true when a successor of BB dominates A.
  bool successorDominate(const BasicBlock *BB, const BasicBlock *A) {
    for (const BasicBlock *Succ : successors(BB))
      if (DT->dominates(Succ, A))
        return true;

    return false;
  }

  // Return true when I1 appears before I2 in the instructions of BB.
  bool firstInBB(const Instruction *I1, const Instruction *I2) {
    assert(I1->getParent() == I2->getParent());
    unsigned I1DFS = DFSNumber.lookup(I1);
    unsigned I2DFS = DFSNumber.lookup(I2);
    assert(I1DFS && I2DFS);
    return I1DFS < I2DFS;
  }

  // Return true when there are memory uses of Def in BB.
  bool hasMemoryUse(const Instruction *NewPt, MemoryDef *Def,
                    const BasicBlock *BB) {
    const MemorySSA::AccessList *Acc = MSSA->getBlockAccesses(BB);
    if (!Acc)
      return false;

    Instruction *OldPt = Def->getMemoryInst();
    const BasicBlock *OldBB = OldPt->getParent();
    const BasicBlock *NewBB = NewPt->getParent();
    bool ReachedNewPt = false;

    for (const MemoryAccess &MA : *Acc)
      if (const MemoryUse *MU = dyn_cast<MemoryUse>(&MA)) {
        Instruction *Insn = MU->getMemoryInst();

        // Do not check whether MU aliases Def when MU occurs after OldPt.
        if (BB == OldBB && firstInBB(OldPt, Insn))
          break;

        // Do not check whether MU aliases Def when MU occurs before NewPt.
        if (BB == NewBB) {
          if (!ReachedNewPt) {
            if (firstInBB(Insn, NewPt))
              continue;
            ReachedNewPt = true;
          }
        }
        if (MemorySSAUtil::defClobbersUseOrDef(Def, MU, *AA))
          return true;
      }

    return false;
  }

  bool hasEHhelper(const BasicBlock *BB, const BasicBlock *SrcBB,
                   int &NBBsOnAllPaths) {
    // Stop walk once the limit is reached.
    if (NBBsOnAllPaths == 0)
      return true;

    // Impossible to hoist with exceptions on the path.
    if (hasEH(BB))
      return true;

    // No such instruction after HoistBarrier in a basic block was
    // selected for hoisting so instructions selected within basic block with
    // a hoist barrier can be hoisted.
    if ((BB != SrcBB) && HoistBarrier.count(BB))
      return true;

    return false;
  }

  // Return true when there are exception handling or loads of memory Def
  // between Def and NewPt.  This function is only called for stores: Def is
  // the MemoryDef of the store to be hoisted.

  // Decrement by 1 NBBsOnAllPaths for each block between HoistPt and BB, and
  // return true when the counter NBBsOnAllPaths reaces 0, except when it is
  // initialized to -1 which is unlimited.
  bool hasEHOrLoadsOnPath(const Instruction *NewPt, MemoryDef *Def,
                          int &NBBsOnAllPaths) {
    const BasicBlock *NewBB = NewPt->getParent();
    const BasicBlock *OldBB = Def->getBlock();
    assert(DT->dominates(NewBB, OldBB) && "invalid path");
    assert(DT->dominates(Def->getDefiningAccess()->getBlock(), NewBB) &&
           "def does not dominate new hoisting point");

    // Walk all basic blocks reachable in depth-first iteration on the inverse
    // CFG from OldBB to NewBB. These blocks are all the blocks that may be
    // executed between the execution of NewBB and OldBB. Hoisting an expression
    // from OldBB into NewBB has to be safe on all execution paths.
    for (auto I = idf_begin(OldBB), E = idf_end(OldBB); I != E;) {
      const BasicBlock *BB = *I;
      if (BB == NewBB) {
        // Stop traversal when reaching HoistPt.
        I.skipChildren();
        continue;
      }

      if (hasEHhelper(BB, OldBB, NBBsOnAllPaths))
        return true;

      // Check that we do not move a store past loads.
      if (hasMemoryUse(NewPt, Def, BB))
        return true;

      // -1 is unlimited number of blocks on all paths.
      if (NBBsOnAllPaths != -1)
        --NBBsOnAllPaths;

      ++I;
    }

    return false;
  }

  // Return true when there are exception handling between HoistPt and BB.
  // Decrement by 1 NBBsOnAllPaths for each block between HoistPt and BB, and
  // return true when the counter NBBsOnAllPaths reaches 0, except when it is
  // initialized to -1 which is unlimited.
  bool hasEHOnPath(const BasicBlock *HoistPt, const BasicBlock *SrcBB,
                   int &NBBsOnAllPaths) {
    assert(DT->dominates(HoistPt, SrcBB) && "Invalid path");

    // Walk all basic blocks reachable in depth-first iteration on
    // the inverse CFG from BBInsn to NewHoistPt. These blocks are all the
    // blocks that may be executed between the execution of NewHoistPt and
    // BBInsn. Hoisting an expression from BBInsn into NewHoistPt has to be safe
    // on all execution paths.
    for (auto I = idf_begin(SrcBB), E = idf_end(SrcBB); I != E;) {
      const BasicBlock *BB = *I;
      if (BB == HoistPt) {
        // Stop traversal when reaching NewHoistPt.
        I.skipChildren();
        continue;
      }

      if (hasEHhelper(BB, SrcBB, NBBsOnAllPaths))
        return true;

      // -1 is unlimited number of blocks on all paths.
      if (NBBsOnAllPaths != -1)
        --NBBsOnAllPaths;

      ++I;
    }

    return false;
  }

  // Return true when it is safe to hoist a memory load or store U from OldPt
  // to NewPt.
  bool safeToHoistLdSt(const Instruction *NewPt, const Instruction *OldPt,
                       MemoryUseOrDef *U, InsKind K, int &NBBsOnAllPaths) {
    // In place hoisting is safe.
    if (NewPt == OldPt)
      return true;

    const BasicBlock *NewBB = NewPt->getParent();
    const BasicBlock *OldBB = OldPt->getParent();
    const BasicBlock *UBB = U->getBlock();

    // Check for dependences on the Memory SSA.
    MemoryAccess *D = U->getDefiningAccess();
    BasicBlock *DBB = D->getBlock();
    if (DT->properlyDominates(NewBB, DBB))
      // Cannot move the load or store to NewBB above its definition in DBB.
      return false;

    if (NewBB == DBB && !MSSA->isLiveOnEntryDef(D))
      if (auto *UD = dyn_cast<MemoryUseOrDef>(D))
        if (!firstInBB(UD->getMemoryInst(), NewPt))
          // Cannot move the load or store to NewPt above its definition in D.
          return false;

    // Check for unsafe hoistings due to side effects.
    if (K == InsKind::Store) {
      if (hasEHOrLoadsOnPath(NewPt, dyn_cast<MemoryDef>(U), NBBsOnAllPaths))
        return false;
    } else if (hasEHOnPath(NewBB, OldBB, NBBsOnAllPaths))
      return false;

    if (UBB == NewBB) {
      if (DT->properlyDominates(DBB, NewBB))
        return true;
      assert(UBB == DBB);
      assert(MSSA->locallyDominates(D, U));
    }

    // No side effects: it is safe to hoist.
    return true;
  }

  // Return true when it is safe to hoist scalar instructions from all blocks in
  // WL to HoistBB.
  bool safeToHoistScalar(const BasicBlock *HoistBB, const BasicBlock *BB,
                         int &NBBsOnAllPaths) {
    return !hasEHOnPath(HoistBB, BB, NBBsOnAllPaths);
  }

  // In the inverse CFG, the dominance frontier of basic block (BB) is the
  // point where ANTIC needs to be computed for instructions which are going
  // to be hoisted. Since this point does not change during gvn-hoist,
  // we compute it only once (on demand).
  // The ides is inspired from:
  // "Partial Redundancy Elimination in SSA Form"
  // ROBERT KENNEDY, SUN CHAN, SHIN-MING LIU, RAYMOND LO, PENG TU and FRED CHOW
  // They use similar idea in the forward graph to find fully redundant and
  // partially redundant expressions, here it is used in the inverse graph to
  // find fully anticipable instructions at merge point (post-dominator in
  // the inverse CFG).
  // Returns the edge via which an instruction in BB will get the values from.

  // Returns true when the values are flowing out to each edge.
  bool valueAnticipable(CHIArgs C, Instruction *TI) const {
    if (TI->getNumSuccessors() > (unsigned)size(C))
      return false; // Not enough args in this CHI.

    for (auto CHI : C) {
      BasicBlock *Dest = CHI.Dest;
      // Find if all the edges have values flowing out of BB.
      bool Found = llvm::any_of(
          successors(TI), [Dest](const BasicBlock *BB) { return BB == Dest; });
      if (!Found)
        return false;
    }
    return true;
  }

  // Check if it is safe to hoist values tracked by CHI in the range
  // [Begin, End) and accumulate them in Safe.
  void checkSafety(CHIArgs C, BasicBlock *BB, InsKind K,
                   SmallVectorImpl<CHIArg> &Safe) {
    int NumBBsOnAllPaths = MaxNumberOfBBSInPath;
    for (auto CHI : C) {
      Instruction *Insn = CHI.I;
      if (!Insn) // No instruction was inserted in this CHI.
        continue;
      if (K == InsKind::Scalar) {
        if (safeToHoistScalar(BB, Insn->getParent(), NumBBsOnAllPaths))
          Safe.push_back(CHI);
      } else {
        MemoryUseOrDef *UD = MSSA->getMemoryAccess(Insn);
        if (safeToHoistLdSt(BB->getTerminator(), Insn, UD, K, NumBBsOnAllPaths))
          Safe.push_back(CHI);
      }
    }
  }

  using RenameStackType = DenseMap<VNType, SmallVector<Instruction *, 2>>;

  // Push all the VNs corresponding to BB into RenameStack.
  void fillRenameStack(BasicBlock *BB, InValuesType &ValueBBs,
                       RenameStackType &RenameStack) {
    auto it1 = ValueBBs.find(BB);
    if (it1 != ValueBBs.end()) {
      // Iterate in reverse order to keep lower ranked values on the top.
      for (std::pair<VNType, Instruction *> &VI : reverse(it1->second)) {
        // Get the value of instruction I
        LLVM_DEBUG(dbgs() << "\nPushing on stack: " << *VI.second);
        RenameStack[VI.first].push_back(VI.second);
      }
    }
  }

  void fillChiArgs(BasicBlock *BB, OutValuesType &CHIBBs,
                   RenameStackType &RenameStack) {
    // For each *predecessor* (because Post-DOM) of BB check if it has a CHI
    for (auto Pred : predecessors(BB)) {
      auto P = CHIBBs.find(Pred);
      if (P == CHIBBs.end()) {
        continue;
      }
      LLVM_DEBUG(dbgs() << "\nLooking at CHIs in: " << Pred->getName(););
      // A CHI is found (BB -> Pred is an edge in the CFG)
      // Pop the stack until Top(V) = Ve.
      auto &VCHI = P->second;
      for (auto It = VCHI.begin(), E = VCHI.end(); It != E;) {
        CHIArg &C = *It;
        if (!C.Dest) {
          auto si = RenameStack.find(C.VN);
          // The Basic Block where CHI is must dominate the value we want to
          // track in a CHI. In the PDom walk, there can be values in the
          // stack which are not control dependent e.g., nested loop.
          if (si != RenameStack.end() && si->second.size() &&
              DT->properlyDominates(Pred, si->second.back()->getParent())) {
            C.Dest = BB;                     // Assign the edge
            C.I = si->second.pop_back_val(); // Assign the argument
            LLVM_DEBUG(dbgs()
                       << "\nCHI Inserted in BB: " << C.Dest->getName() << *C.I
                       << ", VN: " << C.VN.first << ", " << C.VN.second);
          }
          // Move to next CHI of a different value
          It = std::find_if(It, VCHI.end(),
                            [It](CHIArg &A) { return A != *It; });
        } else
          ++It;
      }
    }
  }

  // Walk the post-dominator tree top-down and use a stack for each value to
  // store the last value you see. When you hit a CHI from a given edge, the
  // value to use as the argument is at the top of the stack, add the value to
  // CHI and pop.
  void insertCHI(InValuesType &ValueBBs, OutValuesType &CHIBBs) {
    auto Root = PDT->getNode(nullptr);
    if (!Root)
      return;
    // Depth first walk on PDom tree to fill the CHIargs at each PDF.
    RenameStackType RenameStack;
    for (auto Node : depth_first(Root)) {
      BasicBlock *BB = Node->getBlock();
      if (!BB)
        continue;

      // Collect all values in BB and push to stack.
      fillRenameStack(BB, ValueBBs, RenameStack);

      // Fill outgoing values in each CHI corresponding to BB.
      fillChiArgs(BB, CHIBBs, RenameStack);
    }
  }

  // Walk all the CHI-nodes to find ones which have a empty-entry and remove
  // them Then collect all the instructions which are safe to hoist and see if
  // they form a list of anticipable values. OutValues contains CHIs
  // corresponding to each basic block.
  void findHoistableCandidates(OutValuesType &CHIBBs, InsKind K,
                               HoistingPointList &HPL) {
    auto cmpVN = [](const CHIArg &A, const CHIArg &B) { return A.VN < B.VN; };

    // CHIArgs now have the outgoing values, so check for anticipability and
    // accumulate hoistable candidates in HPL.
    for (std::pair<BasicBlock *, SmallVector<CHIArg, 2>> &A : CHIBBs) {
      BasicBlock *BB = A.first;
      SmallVectorImpl<CHIArg> &CHIs = A.second;
      // Vector of PHIs contains PHIs for different instructions.
      // Sort the args according to their VNs, such that identical
      // instructions are together.
      std::stable_sort(CHIs.begin(), CHIs.end(), cmpVN);
      auto TI = BB->getTerminator();
      auto B = CHIs.begin();
      // [PreIt, PHIIt) form a range of CHIs which have identical VNs.
      auto PHIIt = std::find_if(CHIs.begin(), CHIs.end(),
                                 [B](CHIArg &A) { return A != *B; });
      auto PrevIt = CHIs.begin();
      while (PrevIt != PHIIt) {
        // Collect values which satisfy safety checks.
        SmallVector<CHIArg, 2> Safe;
        // We check for safety first because there might be multiple values in
        // the same path, some of which are not safe to be hoisted, but overall
        // each edge has at least one value which can be hoisted, making the
        // value anticipable along that path.
        checkSafety(make_range(PrevIt, PHIIt), BB, K, Safe);

        // List of safe values should be anticipable at TI.
        if (valueAnticipable(make_range(Safe.begin(), Safe.end()), TI)) {
          HPL.push_back({BB, SmallVecInsn()});
          SmallVecInsn &V = HPL.back().second;
          for (auto B : Safe)
            V.push_back(B.I);
        }

        // Check other VNs
        PrevIt = PHIIt;
        PHIIt = std::find_if(PrevIt, CHIs.end(),
                             [PrevIt](CHIArg &A) { return A != *PrevIt; });
      }
    }
  }

  // Compute insertion points for each values which can be fully anticipated at
  // a dominator. HPL contains all such values.
  void computeInsertionPoints(const VNtoInsns &Map, HoistingPointList &HPL,
                              InsKind K) {
    // Sort VNs based on their rankings
    std::vector<VNType> Ranks;
    for (const auto &Entry : Map) {
      Ranks.push_back(Entry.first);
    }

    // TODO: Remove fully-redundant expressions.
    // Get instruction from the Map, assume that all the Instructions
    // with same VNs have same rank (this is an approximation).
    llvm::sort(Ranks, [this, &Map](const VNType &r1, const VNType &r2) {
      return (rank(*Map.lookup(r1).begin()) < rank(*Map.lookup(r2).begin()));
    });

    // - Sort VNs according to their rank, and start with lowest ranked VN
    // - Take a VN and for each instruction with same VN
    //   - Find the dominance frontier in the inverse graph (PDF)
    //   - Insert the chi-node at PDF
    // - Remove the chi-nodes with missing entries
    // - Remove values from CHI-nodes which do not truly flow out, e.g.,
    //   modified along the path.
    // - Collect the remaining values that are still anticipable
    SmallVector<BasicBlock *, 2> IDFBlocks;
    ReverseIDFCalculator IDFs(*PDT);
    OutValuesType OutValue;
    InValuesType InValue;
    for (const auto &R : Ranks) {
      const SmallVecInsn &V = Map.lookup(R);
      if (V.size() < 2)
        continue;
      const VNType &VN = R;
      SmallPtrSet<BasicBlock *, 2> VNBlocks;
      for (auto &I : V) {
        BasicBlock *BBI = I->getParent();
        if (!hasEH(BBI))
          VNBlocks.insert(BBI);
      }
      // Compute the Post Dominance Frontiers of each basic block
      // The dominance frontier of a live block X in the reverse
      // control graph is the set of blocks upon which X is control
      // dependent. The following sequence computes the set of blocks
      // which currently have dead terminators that are control
      // dependence sources of a block which is in NewLiveBlocks.
      IDFs.setDefiningBlocks(VNBlocks);
      IDFBlocks.clear();
      IDFs.calculate(IDFBlocks);

      // Make a map of BB vs instructions to be hoisted.
      for (unsigned i = 0; i < V.size(); ++i) {
        InValue[V[i]->getParent()].push_back(std::make_pair(VN, V[i]));
      }
      // Insert empty CHI node for this VN. This is used to factor out
      // basic blocks where the ANTIC can potentially change.
      for (auto IDFB : IDFBlocks) {
        for (unsigned i = 0; i < V.size(); ++i) {
          CHIArg C = {VN, nullptr, nullptr};
           // Ignore spurious PDFs.
          if (DT->properlyDominates(IDFB, V[i]->getParent())) {
            OutValue[IDFB].push_back(C);
            LLVM_DEBUG(dbgs() << "\nInsertion a CHI for BB: " << IDFB->getName()
                              << ", for Insn: " << *V[i]);
          }
        }
      }
    }

    // Insert CHI args at each PDF to iterate on factored graph of
    // control dependence.
    insertCHI(InValue, OutValue);
    // Using the CHI args inserted at each PDF, find fully anticipable values.
    findHoistableCandidates(OutValue, K, HPL);
  }

  // Return true when all operands of Instr are available at insertion point
  // HoistPt. When limiting the number of hoisted expressions, one could hoist
  // a load without hoisting its access function. So before hoisting any
  // expression, make sure that all its operands are available at insert point.
  bool allOperandsAvailable(const Instruction *I,
                            const BasicBlock *HoistPt) const {
    for (const Use &Op : I->operands())
      if (const auto *Inst = dyn_cast<Instruction>(&Op))
        if (!DT->dominates(Inst->getParent(), HoistPt))
          return false;

    return true;
  }

  // Same as allOperandsAvailable with recursive check for GEP operands.
  bool allGepOperandsAvailable(const Instruction *I,
                               const BasicBlock *HoistPt) const {
    for (const Use &Op : I->operands())
      if (const auto *Inst = dyn_cast<Instruction>(&Op))
        if (!DT->dominates(Inst->getParent(), HoistPt)) {
          if (const GetElementPtrInst *GepOp =
                  dyn_cast<GetElementPtrInst>(Inst)) {
            if (!allGepOperandsAvailable(GepOp, HoistPt))
              return false;
            // Gep is available if all operands of GepOp are available.
          } else {
            // Gep is not available if it has operands other than GEPs that are
            // defined in blocks not dominating HoistPt.
            return false;
          }
        }
    return true;
  }

  // Make all operands of the GEP available.
  void makeGepsAvailable(Instruction *Repl, BasicBlock *HoistPt,
                         const SmallVecInsn &InstructionsToHoist,
                         Instruction *Gep) const {
    assert(allGepOperandsAvailable(Gep, HoistPt) &&
           "GEP operands not available");

    Instruction *ClonedGep = Gep->clone();
    for (unsigned i = 0, e = Gep->getNumOperands(); i != e; ++i)
      if (Instruction *Op = dyn_cast<Instruction>(Gep->getOperand(i))) {
        // Check whether the operand is already available.
        if (DT->dominates(Op->getParent(), HoistPt))
          continue;

        // As a GEP can refer to other GEPs, recursively make all the operands
        // of this GEP available at HoistPt.
        if (GetElementPtrInst *GepOp = dyn_cast<GetElementPtrInst>(Op))
          makeGepsAvailable(ClonedGep, HoistPt, InstructionsToHoist, GepOp);
      }

    // Copy Gep and replace its uses in Repl with ClonedGep.
    ClonedGep->insertBefore(HoistPt->getTerminator());

    // Conservatively discard any optimization hints, they may differ on the
    // other paths.
    ClonedGep->dropUnknownNonDebugMetadata();

    // If we have optimization hints which agree with each other along different
    // paths, preserve them.
    for (const Instruction *OtherInst : InstructionsToHoist) {
      const GetElementPtrInst *OtherGep;
      if (auto *OtherLd = dyn_cast<LoadInst>(OtherInst))
        OtherGep = cast<GetElementPtrInst>(OtherLd->getPointerOperand());
      else
        OtherGep = cast<GetElementPtrInst>(
            cast<StoreInst>(OtherInst)->getPointerOperand());
      ClonedGep->andIRFlags(OtherGep);
    }

    // Replace uses of Gep with ClonedGep in Repl.
    Repl->replaceUsesOfWith(Gep, ClonedGep);
  }

  void updateAlignment(Instruction *I, Instruction *Repl) {
    if (auto *ReplacementLoad = dyn_cast<LoadInst>(Repl)) {
      ReplacementLoad->setAlignment(
          std::min(ReplacementLoad->getAlignment(),
                   cast<LoadInst>(I)->getAlignment()));
      ++NumLoadsRemoved;
    } else if (auto *ReplacementStore = dyn_cast<StoreInst>(Repl)) {
      ReplacementStore->setAlignment(
          std::min(ReplacementStore->getAlignment(),
                   cast<StoreInst>(I)->getAlignment()));
      ++NumStoresRemoved;
    } else if (auto *ReplacementAlloca = dyn_cast<AllocaInst>(Repl)) {
      ReplacementAlloca->setAlignment(
          std::max(ReplacementAlloca->getAlignment(),
                   cast<AllocaInst>(I)->getAlignment()));
    } else if (isa<CallInst>(Repl)) {
      ++NumCallsRemoved;
    }
  }

  // Remove all the instructions in Candidates and replace their usage with Repl.
  // Returns the number of instructions removed.
  unsigned rauw(const SmallVecInsn &Candidates, Instruction *Repl,
                MemoryUseOrDef *NewMemAcc) {
    unsigned NR = 0;
    for (Instruction *I : Candidates) {
      if (I != Repl) {
        ++NR;
        updateAlignment(I, Repl);
        if (NewMemAcc) {
          // Update the uses of the old MSSA access with NewMemAcc.
          MemoryAccess *OldMA = MSSA->getMemoryAccess(I);
          OldMA->replaceAllUsesWith(NewMemAcc);
          MSSAUpdater->removeMemoryAccess(OldMA);
        }

        Repl->andIRFlags(I);
        combineKnownMetadata(Repl, I);
        I->replaceAllUsesWith(Repl);
        // Also invalidate the Alias Analysis cache.
        MD->removeInstruction(I);
        I->eraseFromParent();
      }
    }
    return NR;
  }

  // Replace all Memory PHI usage with NewMemAcc.
  void raMPHIuw(MemoryUseOrDef *NewMemAcc) {
    SmallPtrSet<MemoryPhi *, 4> UsePhis;
    for (User *U : NewMemAcc->users())
      if (MemoryPhi *Phi = dyn_cast<MemoryPhi>(U))
        UsePhis.insert(Phi);

    for (MemoryPhi *Phi : UsePhis) {
      auto In = Phi->incoming_values();
      if (llvm::all_of(In, [&](Use &U) { return U == NewMemAcc; })) {
        Phi->replaceAllUsesWith(NewMemAcc);
        MSSAUpdater->removeMemoryAccess(Phi);
      }
    }
  }

  // Remove all other instructions and replace them with Repl.
  unsigned removeAndReplace(const SmallVecInsn &Candidates, Instruction *Repl,
                            BasicBlock *DestBB, bool MoveAccess) {
    MemoryUseOrDef *NewMemAcc = MSSA->getMemoryAccess(Repl);
    if (MoveAccess && NewMemAcc) {
        // The definition of this ld/st will not change: ld/st hoisting is
        // legal when the ld/st is not moved past its current definition.
        MSSAUpdater->moveToPlace(NewMemAcc, DestBB, MemorySSA::End);
    }

    // Replace all other instructions with Repl with memory access NewMemAcc.
    unsigned NR = rauw(Candidates, Repl, NewMemAcc);

    // Remove MemorySSA phi nodes with the same arguments.
    if (NewMemAcc)
      raMPHIuw(NewMemAcc);
    return NR;
  }

  // In the case Repl is a load or a store, we make all their GEPs
  // available: GEPs are not hoisted by default to avoid the address
  // computations to be hoisted without the associated load or store.
  bool makeGepOperandsAvailable(Instruction *Repl, BasicBlock *HoistPt,
                                const SmallVecInsn &InstructionsToHoist) const {
    // Check whether the GEP of a ld/st can be synthesized at HoistPt.
    GetElementPtrInst *Gep = nullptr;
    Instruction *Val = nullptr;
    if (auto *Ld = dyn_cast<LoadInst>(Repl)) {
      Gep = dyn_cast<GetElementPtrInst>(Ld->getPointerOperand());
    } else if (auto *St = dyn_cast<StoreInst>(Repl)) {
      Gep = dyn_cast<GetElementPtrInst>(St->getPointerOperand());
      Val = dyn_cast<Instruction>(St->getValueOperand());
      // Check that the stored value is available.
      if (Val) {
        if (isa<GetElementPtrInst>(Val)) {
          // Check whether we can compute the GEP at HoistPt.
          if (!allGepOperandsAvailable(Val, HoistPt))
            return false;
        } else if (!DT->dominates(Val->getParent(), HoistPt))
          return false;
      }
    }

    // Check whether we can compute the Gep at HoistPt.
    if (!Gep || !allGepOperandsAvailable(Gep, HoistPt))
      return false;

    makeGepsAvailable(Repl, HoistPt, InstructionsToHoist, Gep);

    if (Val && isa<GetElementPtrInst>(Val))
      makeGepsAvailable(Repl, HoistPt, InstructionsToHoist, Val);

    return true;
  }

  std::pair<unsigned, unsigned> hoist(HoistingPointList &HPL) {
    unsigned NI = 0, NL = 0, NS = 0, NC = 0, NR = 0;
    for (const HoistingPointInfo &HP : HPL) {
      // Find out whether we already have one of the instructions in HoistPt,
      // in which case we do not have to move it.
      BasicBlock *DestBB = HP.first;
      const SmallVecInsn &InstructionsToHoist = HP.second;
      Instruction *Repl = nullptr;
      for (Instruction *I : InstructionsToHoist)
        if (I->getParent() == DestBB)
          // If there are two instructions in HoistPt to be hoisted in place:
          // update Repl to be the first one, such that we can rename the uses
          // of the second based on the first.
          if (!Repl || firstInBB(I, Repl))
            Repl = I;

      // Keep track of whether we moved the instruction so we know whether we
      // should move the MemoryAccess.
      bool MoveAccess = true;
      if (Repl) {
        // Repl is already in HoistPt: it remains in place.
        assert(allOperandsAvailable(Repl, DestBB) &&
               "instruction depends on operands that are not available");
        MoveAccess = false;
      } else {
        // When we do not find Repl in HoistPt, select the first in the list
        // and move it to HoistPt.
        Repl = InstructionsToHoist.front();

        // We can move Repl in HoistPt only when all operands are available.
        // The order in which hoistings are done may influence the availability
        // of operands.
        if (!allOperandsAvailable(Repl, DestBB)) {
          // When HoistingGeps there is nothing more we can do to make the
          // operands available: just continue.
          if (HoistingGeps)
            continue;

          // When not HoistingGeps we need to copy the GEPs.
          if (!makeGepOperandsAvailable(Repl, DestBB, InstructionsToHoist))
            continue;
        }

        // Move the instruction at the end of HoistPt.
        Instruction *Last = DestBB->getTerminator();
        MD->removeInstruction(Repl);
        Repl->moveBefore(Last);

        DFSNumber[Repl] = DFSNumber[Last]++;
      }

      NR += removeAndReplace(InstructionsToHoist, Repl, DestBB, MoveAccess);

      if (isa<LoadInst>(Repl))
        ++NL;
      else if (isa<StoreInst>(Repl))
        ++NS;
      else if (isa<CallInst>(Repl))
        ++NC;
      else // Scalar
        ++NI;
    }

    NumHoisted += NL + NS + NC + NI;
    NumRemoved += NR;
    NumLoadsHoisted += NL;
    NumStoresHoisted += NS;
    NumCallsHoisted += NC;
    return {NI, NL + NC + NS};
  }

  // Hoist all expressions. Returns Number of scalars hoisted
  // and number of non-scalars hoisted.
  std::pair<unsigned, unsigned> hoistExpressions(Function &F) {
    InsnInfo II;
    LoadInfo LI;
    StoreInfo SI;
    CallInfo CI;
    for (BasicBlock *BB : depth_first(&F.getEntryBlock())) {
      int InstructionNb = 0;
      for (Instruction &I1 : *BB) {
        // If I1 cannot guarantee progress, subsequent instructions
        // in BB cannot be hoisted anyways.
        if (!isGuaranteedToTransferExecutionToSuccessor(&I1)) {
          HoistBarrier.insert(BB);
          break;
        }
        // Only hoist the first instructions in BB up to MaxDepthInBB. Hoisting
        // deeper may increase the register pressure and compilation time.
        if (MaxDepthInBB != -1 && InstructionNb++ >= MaxDepthInBB)
          break;

        // Do not value number terminator instructions.
        if (I1.isTerminator())
          break;

        if (auto *Load = dyn_cast<LoadInst>(&I1))
          LI.insert(Load, VN);
        else if (auto *Store = dyn_cast<StoreInst>(&I1))
          SI.insert(Store, VN);
        else if (auto *Call = dyn_cast<CallInst>(&I1)) {
          if (auto *Intr = dyn_cast<IntrinsicInst>(Call)) {
            if (isa<DbgInfoIntrinsic>(Intr) ||
                Intr->getIntrinsicID() == Intrinsic::assume ||
                Intr->getIntrinsicID() == Intrinsic::sideeffect)
              continue;
          }
          if (Call->mayHaveSideEffects())
            break;

          if (Call->isConvergent())
            break;

          CI.insert(Call, VN);
        } else if (HoistingGeps || !isa<GetElementPtrInst>(&I1))
          // Do not hoist scalars past calls that may write to memory because
          // that could result in spills later. geps are handled separately.
          // TODO: We can relax this for targets like AArch64 as they have more
          // registers than X86.
          II.insert(&I1, VN);
      }
    }

    HoistingPointList HPL;
    computeInsertionPoints(II.getVNTable(), HPL, InsKind::Scalar);
    computeInsertionPoints(LI.getVNTable(), HPL, InsKind::Load);
    computeInsertionPoints(SI.getVNTable(), HPL, InsKind::Store);
    computeInsertionPoints(CI.getScalarVNTable(), HPL, InsKind::Scalar);
    computeInsertionPoints(CI.getLoadVNTable(), HPL, InsKind::Load);
    computeInsertionPoints(CI.getStoreVNTable(), HPL, InsKind::Store);
    return hoist(HPL);
  }
};

class GVNHoistLegacyPass : public FunctionPass {
public:
  static char ID;

  GVNHoistLegacyPass() : FunctionPass(ID) {
    initializeGVNHoistLegacyPassPass(*PassRegistry::getPassRegistry());
  }

  bool runOnFunction(Function &F) override {
    if (skipFunction(F))
      return false;
    auto &DT = getAnalysis<DominatorTreeWrapperPass>().getDomTree();
    auto &PDT = getAnalysis<PostDominatorTreeWrapperPass>().getPostDomTree();
    auto &AA = getAnalysis<AAResultsWrapperPass>().getAAResults();
    auto &MD = getAnalysis<MemoryDependenceWrapperPass>().getMemDep();
    auto &MSSA = getAnalysis<MemorySSAWrapperPass>().getMSSA();

    GVNHoist G(&DT, &PDT, &AA, &MD, &MSSA);
    return G.run(F);
  }

  void getAnalysisUsage(AnalysisUsage &AU) const override {
    AU.addRequired<DominatorTreeWrapperPass>();
    AU.addRequired<PostDominatorTreeWrapperPass>();
    AU.addRequired<AAResultsWrapperPass>();
    AU.addRequired<MemoryDependenceWrapperPass>();
    AU.addRequired<MemorySSAWrapperPass>();
    AU.addPreserved<DominatorTreeWrapperPass>();
    AU.addPreserved<MemorySSAWrapperPass>();
    AU.addPreserved<GlobalsAAWrapperPass>();
  }
};

} // end namespace llvm

PreservedAnalyses GVNHoistPass::run(Function &F, FunctionAnalysisManager &AM) {
  DominatorTree &DT = AM.getResult<DominatorTreeAnalysis>(F);
  PostDominatorTree &PDT = AM.getResult<PostDominatorTreeAnalysis>(F);
  AliasAnalysis &AA = AM.getResult<AAManager>(F);
  MemoryDependenceResults &MD = AM.getResult<MemoryDependenceAnalysis>(F);
  MemorySSA &MSSA = AM.getResult<MemorySSAAnalysis>(F).getMSSA();
  GVNHoist G(&DT, &PDT, &AA, &MD, &MSSA);
  if (!G.run(F))
    return PreservedAnalyses::all();

  PreservedAnalyses PA;
  PA.preserve<DominatorTreeAnalysis>();
  PA.preserve<MemorySSAAnalysis>();
  PA.preserve<GlobalsAA>();
  return PA;
}

char GVNHoistLegacyPass::ID = 0;

INITIALIZE_PASS_BEGIN(GVNHoistLegacyPass, "gvn-hoist",
                      "Early GVN Hoisting of Expressions", false, false)
INITIALIZE_PASS_DEPENDENCY(MemoryDependenceWrapperPass)
INITIALIZE_PASS_DEPENDENCY(MemorySSAWrapperPass)
INITIALIZE_PASS_DEPENDENCY(DominatorTreeWrapperPass)
INITIALIZE_PASS_DEPENDENCY(PostDominatorTreeWrapperPass)
INITIALIZE_PASS_DEPENDENCY(AAResultsWrapperPass)
INITIALIZE_PASS_END(GVNHoistLegacyPass, "gvn-hoist",
                    "Early GVN Hoisting of Expressions", false, false)

FunctionPass *llvm::createGVNHoistPass() { return new GVNHoistLegacyPass(); }
