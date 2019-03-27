//===-- DifferenceEngine.cpp - Structural function/module comparison ------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This header defines the implementation of the LLVM difference
// engine, which structurally compares global values within a module.
//
//===----------------------------------------------------------------------===//

#include "DifferenceEngine.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/DenseSet.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringSet.h"
#include "llvm/IR/CFG.h"
#include "llvm/IR/CallSite.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Module.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Support/type_traits.h"
#include <utility>

using namespace llvm;

namespace {

/// A priority queue, implemented as a heap.
template <class T, class Sorter, unsigned InlineCapacity>
class PriorityQueue {
  Sorter Precedes;
  llvm::SmallVector<T, InlineCapacity> Storage;

public:
  PriorityQueue(const Sorter &Precedes) : Precedes(Precedes) {}

  /// Checks whether the heap is empty.
  bool empty() const { return Storage.empty(); }

  /// Insert a new value on the heap.
  void insert(const T &V) {
    unsigned Index = Storage.size();
    Storage.push_back(V);
    if (Index == 0) return;

    T *data = Storage.data();
    while (true) {
      unsigned Target = (Index + 1) / 2 - 1;
      if (!Precedes(data[Index], data[Target])) return;
      std::swap(data[Index], data[Target]);
      if (Target == 0) return;
      Index = Target;
    }
  }

  /// Remove the minimum value in the heap.  Only valid on a non-empty heap.
  T remove_min() {
    assert(!empty());
    T tmp = Storage[0];
    
    unsigned NewSize = Storage.size() - 1;
    if (NewSize) {
      // Move the slot at the end to the beginning.
      if (isPodLike<T>::value)
        Storage[0] = Storage[NewSize];
      else
        std::swap(Storage[0], Storage[NewSize]);

      // Bubble the root up as necessary.
      unsigned Index = 0;
      while (true) {
        // With a 1-based index, the children would be Index*2 and Index*2+1.
        unsigned R = (Index + 1) * 2;
        unsigned L = R - 1;

        // If R is out of bounds, we're done after this in any case.
        if (R >= NewSize) {
          // If L is also out of bounds, we're done immediately.
          if (L >= NewSize) break;

          // Otherwise, test whether we should swap L and Index.
          if (Precedes(Storage[L], Storage[Index]))
            std::swap(Storage[L], Storage[Index]);
          break;
        }

        // Otherwise, we need to compare with the smaller of L and R.
        // Prefer R because it's closer to the end of the array.
        unsigned IndexToTest = (Precedes(Storage[L], Storage[R]) ? L : R);

        // If Index is >= the min of L and R, then heap ordering is restored.
        if (!Precedes(Storage[IndexToTest], Storage[Index]))
          break;

        // Otherwise, keep bubbling up.
        std::swap(Storage[IndexToTest], Storage[Index]);
        Index = IndexToTest;
      }
    }
    Storage.pop_back();

    return tmp;
  }
};

/// A function-scope difference engine.
class FunctionDifferenceEngine {
  DifferenceEngine &Engine;

  /// The current mapping from old local values to new local values.
  DenseMap<Value*, Value*> Values;

  /// The current mapping from old blocks to new blocks.
  DenseMap<BasicBlock*, BasicBlock*> Blocks;

  DenseSet<std::pair<Value*, Value*> > TentativeValues;

  unsigned getUnprocPredCount(BasicBlock *Block) const {
    unsigned Count = 0;
    for (pred_iterator I = pred_begin(Block), E = pred_end(Block); I != E; ++I)
      if (!Blocks.count(*I)) Count++;
    return Count;
  }

  typedef std::pair<BasicBlock*, BasicBlock*> BlockPair;

  /// A type which sorts a priority queue by the number of unprocessed
  /// predecessor blocks it has remaining.
  ///
  /// This is actually really expensive to calculate.
  struct QueueSorter {
    const FunctionDifferenceEngine &fde;
    explicit QueueSorter(const FunctionDifferenceEngine &fde) : fde(fde) {}

    bool operator()(const BlockPair &Old, const BlockPair &New) {
      return fde.getUnprocPredCount(Old.first)
           < fde.getUnprocPredCount(New.first);
    }
  };

  /// A queue of unified blocks to process.
  PriorityQueue<BlockPair, QueueSorter, 20> Queue;

  /// Try to unify the given two blocks.  Enqueues them for processing
  /// if they haven't already been processed.
  ///
  /// Returns true if there was a problem unifying them.
  bool tryUnify(BasicBlock *L, BasicBlock *R) {
    BasicBlock *&Ref = Blocks[L];

    if (Ref) {
      if (Ref == R) return false;

      Engine.logf("successor %l cannot be equivalent to %r; "
                  "it's already equivalent to %r")
        << L << R << Ref;
      return true;
    }

    Ref = R;
    Queue.insert(BlockPair(L, R));
    return false;
  }
  
  /// Unifies two instructions, given that they're known not to have
  /// structural differences.
  void unify(Instruction *L, Instruction *R) {
    DifferenceEngine::Context C(Engine, L, R);

    bool Result = diff(L, R, true, true);
    assert(!Result && "structural differences second time around?");
    (void) Result;
    if (!L->use_empty())
      Values[L] = R;
  }

  void processQueue() {
    while (!Queue.empty()) {
      BlockPair Pair = Queue.remove_min();
      diff(Pair.first, Pair.second);
    }
  }

  void diff(BasicBlock *L, BasicBlock *R) {
    DifferenceEngine::Context C(Engine, L, R);

    BasicBlock::iterator LI = L->begin(), LE = L->end();
    BasicBlock::iterator RI = R->begin();

    do {
      assert(LI != LE && RI != R->end());
      Instruction *LeftI = &*LI, *RightI = &*RI;

      // If the instructions differ, start the more sophisticated diff
      // algorithm at the start of the block.
      if (diff(LeftI, RightI, false, false)) {
        TentativeValues.clear();
        return runBlockDiff(L->begin(), R->begin());
      }

      // Otherwise, tentatively unify them.
      if (!LeftI->use_empty())
        TentativeValues.insert(std::make_pair(LeftI, RightI));

      ++LI;
      ++RI;
    } while (LI != LE); // This is sufficient: we can't get equality of
                        // terminators if there are residual instructions.

    // Unify everything in the block, non-tentatively this time.
    TentativeValues.clear();
    for (LI = L->begin(), RI = R->begin(); LI != LE; ++LI, ++RI)
      unify(&*LI, &*RI);
  }

  bool matchForBlockDiff(Instruction *L, Instruction *R);
  void runBlockDiff(BasicBlock::iterator LI, BasicBlock::iterator RI);

  bool diffCallSites(CallSite L, CallSite R, bool Complain) {
    // FIXME: call attributes
    if (!equivalentAsOperands(L.getCalledValue(), R.getCalledValue())) {
      if (Complain) Engine.log("called functions differ");
      return true;
    }
    if (L.arg_size() != R.arg_size()) {
      if (Complain) Engine.log("argument counts differ");
      return true;
    }
    for (unsigned I = 0, E = L.arg_size(); I != E; ++I)
      if (!equivalentAsOperands(L.getArgument(I), R.getArgument(I))) {
        if (Complain)
          Engine.logf("arguments %l and %r differ")
            << L.getArgument(I) << R.getArgument(I);
        return true;
      }
    return false;
  }

  bool diff(Instruction *L, Instruction *R, bool Complain, bool TryUnify) {
    // FIXME: metadata (if Complain is set)

    // Different opcodes always imply different operations.
    if (L->getOpcode() != R->getOpcode()) {
      if (Complain) Engine.log("different instruction types");
      return true;
    }

    if (isa<CmpInst>(L)) {
      if (cast<CmpInst>(L)->getPredicate()
            != cast<CmpInst>(R)->getPredicate()) {
        if (Complain) Engine.log("different predicates");
        return true;
      }
    } else if (isa<CallInst>(L)) {
      return diffCallSites(CallSite(L), CallSite(R), Complain);
    } else if (isa<PHINode>(L)) {
      // FIXME: implement.

      // This is really weird;  type uniquing is broken?
      if (L->getType() != R->getType()) {
        if (!L->getType()->isPointerTy() || !R->getType()->isPointerTy()) {
          if (Complain) Engine.log("different phi types");
          return true;
        }
      }
      return false;

    // Terminators.
    } else if (isa<InvokeInst>(L)) {
      InvokeInst *LI = cast<InvokeInst>(L);
      InvokeInst *RI = cast<InvokeInst>(R);
      if (diffCallSites(CallSite(LI), CallSite(RI), Complain))
        return true;

      if (TryUnify) {
        tryUnify(LI->getNormalDest(), RI->getNormalDest());
        tryUnify(LI->getUnwindDest(), RI->getUnwindDest());
      }
      return false;

    } else if (isa<BranchInst>(L)) {
      BranchInst *LI = cast<BranchInst>(L);
      BranchInst *RI = cast<BranchInst>(R);
      if (LI->isConditional() != RI->isConditional()) {
        if (Complain) Engine.log("branch conditionality differs");
        return true;
      }

      if (LI->isConditional()) {
        if (!equivalentAsOperands(LI->getCondition(), RI->getCondition())) {
          if (Complain) Engine.log("branch conditions differ");
          return true;
        }
        if (TryUnify) tryUnify(LI->getSuccessor(1), RI->getSuccessor(1));
      }
      if (TryUnify) tryUnify(LI->getSuccessor(0), RI->getSuccessor(0));
      return false;

    } else if (isa<IndirectBrInst>(L)) {
      IndirectBrInst *LI = cast<IndirectBrInst>(L);
      IndirectBrInst *RI = cast<IndirectBrInst>(R);
      if (LI->getNumDestinations() != RI->getNumDestinations()) {
        if (Complain) Engine.log("indirectbr # of destinations differ");
        return true;
      }

      if (!equivalentAsOperands(LI->getAddress(), RI->getAddress())) {
        if (Complain) Engine.log("indirectbr addresses differ");
        return true;
      }

      if (TryUnify) {
        for (unsigned i = 0; i < LI->getNumDestinations(); i++) {
          tryUnify(LI->getDestination(i), RI->getDestination(i));
        }
      }
      return false;

    } else if (isa<SwitchInst>(L)) {
      SwitchInst *LI = cast<SwitchInst>(L);
      SwitchInst *RI = cast<SwitchInst>(R);
      if (!equivalentAsOperands(LI->getCondition(), RI->getCondition())) {
        if (Complain) Engine.log("switch conditions differ");
        return true;
      }
      if (TryUnify) tryUnify(LI->getDefaultDest(), RI->getDefaultDest());

      bool Difference = false;

      DenseMap<ConstantInt*,BasicBlock*> LCases;
      for (auto Case : LI->cases())
        LCases[Case.getCaseValue()] = Case.getCaseSuccessor();

      for (auto Case : RI->cases()) {
        ConstantInt *CaseValue = Case.getCaseValue();
        BasicBlock *LCase = LCases[CaseValue];
        if (LCase) {
          if (TryUnify)
            tryUnify(LCase, Case.getCaseSuccessor());
          LCases.erase(CaseValue);
        } else if (Complain || !Difference) {
          if (Complain)
            Engine.logf("right switch has extra case %r") << CaseValue;
          Difference = true;
        }
      }
      if (!Difference)
        for (DenseMap<ConstantInt*,BasicBlock*>::iterator
               I = LCases.begin(), E = LCases.end(); I != E; ++I) {
          if (Complain)
            Engine.logf("left switch has extra case %l") << I->first;
          Difference = true;
        }
      return Difference;
    } else if (isa<UnreachableInst>(L)) {
      return false;
    }

    if (L->getNumOperands() != R->getNumOperands()) {
      if (Complain) Engine.log("instructions have different operand counts");
      return true;
    }

    for (unsigned I = 0, E = L->getNumOperands(); I != E; ++I) {
      Value *LO = L->getOperand(I), *RO = R->getOperand(I);
      if (!equivalentAsOperands(LO, RO)) {
        if (Complain) Engine.logf("operands %l and %r differ") << LO << RO;
        return true;
      }
    }

    return false;
  }

  bool equivalentAsOperands(Constant *L, Constant *R) {
    // Use equality as a preliminary filter.
    if (L == R)
      return true;

    if (L->getValueID() != R->getValueID())
      return false;
    
    // Ask the engine about global values.
    if (isa<GlobalValue>(L))
      return Engine.equivalentAsOperands(cast<GlobalValue>(L),
                                         cast<GlobalValue>(R));

    // Compare constant expressions structurally.
    if (isa<ConstantExpr>(L))
      return equivalentAsOperands(cast<ConstantExpr>(L),
                                  cast<ConstantExpr>(R));

    // Constants of the "same type" don't always actually have the same
    // type; I don't know why.  Just white-list them.
    if (isa<ConstantPointerNull>(L) || isa<UndefValue>(L) || isa<ConstantAggregateZero>(L))
      return true;

    // Block addresses only match if we've already encountered the
    // block.  FIXME: tentative matches?
    if (isa<BlockAddress>(L))
      return Blocks[cast<BlockAddress>(L)->getBasicBlock()]
                 == cast<BlockAddress>(R)->getBasicBlock();

    // If L and R are ConstantVectors, compare each element
    if (isa<ConstantVector>(L)) {
      ConstantVector *CVL = cast<ConstantVector>(L);
      ConstantVector *CVR = cast<ConstantVector>(R);
      if (CVL->getType()->getNumElements() != CVR->getType()->getNumElements())
        return false;
      for (unsigned i = 0; i < CVL->getType()->getNumElements(); i++) {
        if (!equivalentAsOperands(CVL->getOperand(i), CVR->getOperand(i)))
          return false;
      }
      return true;
    }

    return false;
  }

  bool equivalentAsOperands(ConstantExpr *L, ConstantExpr *R) {
    if (L == R)
      return true;
    if (L->getOpcode() != R->getOpcode())
      return false;

    switch (L->getOpcode()) {
    case Instruction::ICmp:
    case Instruction::FCmp:
      if (L->getPredicate() != R->getPredicate())
        return false;
      break;

    case Instruction::GetElementPtr:
      // FIXME: inbounds?
      break;

    default:
      break;
    }

    if (L->getNumOperands() != R->getNumOperands())
      return false;

    for (unsigned I = 0, E = L->getNumOperands(); I != E; ++I)
      if (!equivalentAsOperands(L->getOperand(I), R->getOperand(I)))
        return false;

    return true;
  }

  bool equivalentAsOperands(Value *L, Value *R) {
    // Fall out if the values have different kind.
    // This possibly shouldn't take priority over oracles.
    if (L->getValueID() != R->getValueID())
      return false;

    // Value subtypes:  Argument, Constant, Instruction, BasicBlock,
    //                  InlineAsm, MDNode, MDString, PseudoSourceValue

    if (isa<Constant>(L))
      return equivalentAsOperands(cast<Constant>(L), cast<Constant>(R));

    if (isa<Instruction>(L))
      return Values[L] == R || TentativeValues.count(std::make_pair(L, R));

    if (isa<Argument>(L))
      return Values[L] == R;

    if (isa<BasicBlock>(L))
      return Blocks[cast<BasicBlock>(L)] != R;

    // Pretend everything else is identical.
    return true;
  }

  // Avoid a gcc warning about accessing 'this' in an initializer.
  FunctionDifferenceEngine *this_() { return this; }

public:
  FunctionDifferenceEngine(DifferenceEngine &Engine) :
    Engine(Engine), Queue(QueueSorter(*this_())) {}

  void diff(Function *L, Function *R) {
    if (L->arg_size() != R->arg_size())
      Engine.log("different argument counts");

    // Map the arguments.
    for (Function::arg_iterator
           LI = L->arg_begin(), LE = L->arg_end(),
           RI = R->arg_begin(), RE = R->arg_end();
         LI != LE && RI != RE; ++LI, ++RI)
      Values[&*LI] = &*RI;

    tryUnify(&*L->begin(), &*R->begin());
    processQueue();
  }
};

struct DiffEntry {
  DiffEntry() : Cost(0) {}

  unsigned Cost;
  llvm::SmallVector<char, 8> Path; // actually of DifferenceEngine::DiffChange
};

bool FunctionDifferenceEngine::matchForBlockDiff(Instruction *L,
                                                 Instruction *R) {
  return !diff(L, R, false, false);
}

void FunctionDifferenceEngine::runBlockDiff(BasicBlock::iterator LStart,
                                            BasicBlock::iterator RStart) {
  BasicBlock::iterator LE = LStart->getParent()->end();
  BasicBlock::iterator RE = RStart->getParent()->end();

  unsigned NL = std::distance(LStart, LE);

  SmallVector<DiffEntry, 20> Paths1(NL+1);
  SmallVector<DiffEntry, 20> Paths2(NL+1);

  DiffEntry *Cur = Paths1.data();
  DiffEntry *Next = Paths2.data();

  const unsigned LeftCost = 2;
  const unsigned RightCost = 2;
  const unsigned MatchCost = 0;

  assert(TentativeValues.empty());

  // Initialize the first column.
  for (unsigned I = 0; I != NL+1; ++I) {
    Cur[I].Cost = I * LeftCost;
    for (unsigned J = 0; J != I; ++J)
      Cur[I].Path.push_back(DC_left);
  }

  for (BasicBlock::iterator RI = RStart; RI != RE; ++RI) {
    // Initialize the first row.
    Next[0] = Cur[0];
    Next[0].Cost += RightCost;
    Next[0].Path.push_back(DC_right);

    unsigned Index = 1;
    for (BasicBlock::iterator LI = LStart; LI != LE; ++LI, ++Index) {
      if (matchForBlockDiff(&*LI, &*RI)) {
        Next[Index] = Cur[Index-1];
        Next[Index].Cost += MatchCost;
        Next[Index].Path.push_back(DC_match);
        TentativeValues.insert(std::make_pair(&*LI, &*RI));
      } else if (Next[Index-1].Cost <= Cur[Index].Cost) {
        Next[Index] = Next[Index-1];
        Next[Index].Cost += LeftCost;
        Next[Index].Path.push_back(DC_left);
      } else {
        Next[Index] = Cur[Index];
        Next[Index].Cost += RightCost;
        Next[Index].Path.push_back(DC_right);
      }
    }

    std::swap(Cur, Next);
  }

  // We don't need the tentative values anymore; everything from here
  // on out should be non-tentative.
  TentativeValues.clear();

  SmallVectorImpl<char> &Path = Cur[NL].Path;
  BasicBlock::iterator LI = LStart, RI = RStart;

  DiffLogBuilder Diff(Engine.getConsumer());

  // Drop trailing matches.
  while (Path.back() == DC_match)
    Path.pop_back();

  // Skip leading matches.
  SmallVectorImpl<char>::iterator
    PI = Path.begin(), PE = Path.end();
  while (PI != PE && *PI == DC_match) {
    unify(&*LI, &*RI);
    ++PI;
    ++LI;
    ++RI;
  }

  for (; PI != PE; ++PI) {
    switch (static_cast<DiffChange>(*PI)) {
    case DC_match:
      assert(LI != LE && RI != RE);
      {
        Instruction *L = &*LI, *R = &*RI;
        unify(L, R);
        Diff.addMatch(L, R);
      }
      ++LI; ++RI;
      break;

    case DC_left:
      assert(LI != LE);
      Diff.addLeft(&*LI);
      ++LI;
      break;

    case DC_right:
      assert(RI != RE);
      Diff.addRight(&*RI);
      ++RI;
      break;
    }
  }

  // Finishing unifying and complaining about the tails of the block,
  // which should be matches all the way through.
  while (LI != LE) {
    assert(RI != RE);
    unify(&*LI, &*RI);
    ++LI;
    ++RI;
  }

  // If the terminators have different kinds, but one is an invoke and the
  // other is an unconditional branch immediately following a call, unify
  // the results and the destinations.
  Instruction *LTerm = LStart->getParent()->getTerminator();
  Instruction *RTerm = RStart->getParent()->getTerminator();
  if (isa<BranchInst>(LTerm) && isa<InvokeInst>(RTerm)) {
    if (cast<BranchInst>(LTerm)->isConditional()) return;
    BasicBlock::iterator I = LTerm->getIterator();
    if (I == LStart->getParent()->begin()) return;
    --I;
    if (!isa<CallInst>(*I)) return;
    CallInst *LCall = cast<CallInst>(&*I);
    InvokeInst *RInvoke = cast<InvokeInst>(RTerm);
    if (!equivalentAsOperands(LCall->getCalledValue(), RInvoke->getCalledValue()))
      return;
    if (!LCall->use_empty())
      Values[LCall] = RInvoke;
    tryUnify(LTerm->getSuccessor(0), RInvoke->getNormalDest());
  } else if (isa<InvokeInst>(LTerm) && isa<BranchInst>(RTerm)) {
    if (cast<BranchInst>(RTerm)->isConditional()) return;
    BasicBlock::iterator I = RTerm->getIterator();
    if (I == RStart->getParent()->begin()) return;
    --I;
    if (!isa<CallInst>(*I)) return;
    CallInst *RCall = cast<CallInst>(I);
    InvokeInst *LInvoke = cast<InvokeInst>(LTerm);
    if (!equivalentAsOperands(LInvoke->getCalledValue(), RCall->getCalledValue()))
      return;
    if (!LInvoke->use_empty())
      Values[LInvoke] = RCall;
    tryUnify(LInvoke->getNormalDest(), RTerm->getSuccessor(0));
  }
}

}

void DifferenceEngine::Oracle::anchor() { }

void DifferenceEngine::diff(Function *L, Function *R) {
  Context C(*this, L, R);

  // FIXME: types
  // FIXME: attributes and CC
  // FIXME: parameter attributes
  
  // If both are declarations, we're done.
  if (L->empty() && R->empty())
    return;
  else if (L->empty())
    log("left function is declaration, right function is definition");
  else if (R->empty())
    log("right function is declaration, left function is definition");
  else
    FunctionDifferenceEngine(*this).diff(L, R);
}

void DifferenceEngine::diff(Module *L, Module *R) {
  StringSet<> LNames;
  SmallVector<std::pair<Function*,Function*>, 20> Queue;

  unsigned LeftAnonCount = 0;
  unsigned RightAnonCount = 0;

  for (Module::iterator I = L->begin(), E = L->end(); I != E; ++I) {
    Function *LFn = &*I;
    StringRef Name = LFn->getName();
    if (Name.empty()) {
      ++LeftAnonCount;
      continue;
    }

    LNames.insert(Name);

    if (Function *RFn = R->getFunction(LFn->getName()))
      Queue.push_back(std::make_pair(LFn, RFn));
    else
      logf("function %l exists only in left module") << LFn;
  }

  for (Module::iterator I = R->begin(), E = R->end(); I != E; ++I) {
    Function *RFn = &*I;
    StringRef Name = RFn->getName();
    if (Name.empty()) {
      ++RightAnonCount;
      continue;
    }

    if (!LNames.count(Name))
      logf("function %r exists only in right module") << RFn;
  }


  if (LeftAnonCount != 0 || RightAnonCount != 0) {
    SmallString<32> Tmp;
    logf(("not comparing " + Twine(LeftAnonCount) +
          " anonymous functions in the left module and " +
          Twine(RightAnonCount) + " in the right module")
             .toStringRef(Tmp));
  }

  for (SmallVectorImpl<std::pair<Function*,Function*> >::iterator
         I = Queue.begin(), E = Queue.end(); I != E; ++I)
    diff(I->first, I->second);
}

bool DifferenceEngine::equivalentAsOperands(GlobalValue *L, GlobalValue *R) {
  if (globalValueOracle) return (*globalValueOracle)(L, R);
  return L->getName() == R->getName();
}
