//===- SparsePropagation.h - Sparse Conditional Property Propagation ------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file implements an abstract sparse conditional propagation algorithm,
// modeled after SCCP, but with a customizable lattice function.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_ANALYSIS_SPARSEPROPAGATION_H
#define LLVM_ANALYSIS_SPARSEPROPAGATION_H

#include "llvm/IR/Instructions.h"
#include "llvm/Support/Debug.h"
#include <set>

#define DEBUG_TYPE "sparseprop"

namespace llvm {

/// A template for translating between LLVM Values and LatticeKeys. Clients must
/// provide a specialization of LatticeKeyInfo for their LatticeKey type.
template <class LatticeKey> struct LatticeKeyInfo {
  // static inline Value *getValueFromLatticeKey(LatticeKey Key);
  // static inline LatticeKey getLatticeKeyFromValue(Value *V);
};

template <class LatticeKey, class LatticeVal,
          class KeyInfo = LatticeKeyInfo<LatticeKey>>
class SparseSolver;

/// AbstractLatticeFunction - This class is implemented by the dataflow instance
/// to specify what the lattice values are and how they handle merges etc.  This
/// gives the client the power to compute lattice values from instructions,
/// constants, etc.  The current requirement is that lattice values must be
/// copyable.  At the moment, nothing tries to avoid copying.  Additionally,
/// lattice keys must be able to be used as keys of a mapping data structure.
/// Internally, the generic solver currently uses a DenseMap to map lattice keys
/// to lattice values.  If the lattice key is a non-standard type, a
/// specialization of DenseMapInfo must be provided.
template <class LatticeKey, class LatticeVal> class AbstractLatticeFunction {
private:
  LatticeVal UndefVal, OverdefinedVal, UntrackedVal;

public:
  AbstractLatticeFunction(LatticeVal undefVal, LatticeVal overdefinedVal,
                          LatticeVal untrackedVal) {
    UndefVal = undefVal;
    OverdefinedVal = overdefinedVal;
    UntrackedVal = untrackedVal;
  }

  virtual ~AbstractLatticeFunction() = default;

  LatticeVal getUndefVal()       const { return UndefVal; }
  LatticeVal getOverdefinedVal() const { return OverdefinedVal; }
  LatticeVal getUntrackedVal()   const { return UntrackedVal; }

  /// IsUntrackedValue - If the specified LatticeKey is obviously uninteresting
  /// to the analysis (i.e., it would always return UntrackedVal), this
  /// function can return true to avoid pointless work.
  virtual bool IsUntrackedValue(LatticeKey Key) { return false; }

  /// ComputeLatticeVal - Compute and return a LatticeVal corresponding to the
  /// given LatticeKey.
  virtual LatticeVal ComputeLatticeVal(LatticeKey Key) {
    return getOverdefinedVal();
  }

  /// IsSpecialCasedPHI - Given a PHI node, determine whether this PHI node is
  /// one that the we want to handle through ComputeInstructionState.
  virtual bool IsSpecialCasedPHI(PHINode *PN) { return false; }

  /// MergeValues - Compute and return the merge of the two specified lattice
  /// values.  Merging should only move one direction down the lattice to
  /// guarantee convergence (toward overdefined).
  virtual LatticeVal MergeValues(LatticeVal X, LatticeVal Y) {
    return getOverdefinedVal(); // always safe, never useful.
  }

  /// ComputeInstructionState - Compute the LatticeKeys that change as a result
  /// of executing instruction \p I. Their associated LatticeVals are store in
  /// \p ChangedValues.
  virtual void
  ComputeInstructionState(Instruction &I,
                          DenseMap<LatticeKey, LatticeVal> &ChangedValues,
                          SparseSolver<LatticeKey, LatticeVal> &SS) = 0;

  /// PrintLatticeVal - Render the given LatticeVal to the specified stream.
  virtual void PrintLatticeVal(LatticeVal LV, raw_ostream &OS);

  /// PrintLatticeKey - Render the given LatticeKey to the specified stream.
  virtual void PrintLatticeKey(LatticeKey Key, raw_ostream &OS);

  /// GetValueFromLatticeVal - If the given LatticeVal is representable as an
  /// LLVM value, return it; otherwise, return nullptr. If a type is given, the
  /// returned value must have the same type. This function is used by the
  /// generic solver in attempting to resolve branch and switch conditions.
  virtual Value *GetValueFromLatticeVal(LatticeVal LV, Type *Ty = nullptr) {
    return nullptr;
  }
};

/// SparseSolver - This class is a general purpose solver for Sparse Conditional
/// Propagation with a programmable lattice function.
template <class LatticeKey, class LatticeVal, class KeyInfo>
class SparseSolver {

  /// LatticeFunc - This is the object that knows the lattice and how to
  /// compute transfer functions.
  AbstractLatticeFunction<LatticeKey, LatticeVal> *LatticeFunc;

  /// ValueState - Holds the LatticeVals associated with LatticeKeys.
  DenseMap<LatticeKey, LatticeVal> ValueState;

  /// BBExecutable - Holds the basic blocks that are executable.
  SmallPtrSet<BasicBlock *, 16> BBExecutable;

  /// ValueWorkList - Holds values that should be processed.
  SmallVector<Value *, 64> ValueWorkList;

  /// BBWorkList - Holds basic blocks that should be processed.
  SmallVector<BasicBlock *, 64> BBWorkList;

  using Edge = std::pair<BasicBlock *, BasicBlock *>;

  /// KnownFeasibleEdges - Entries in this set are edges which have already had
  /// PHI nodes retriggered.
  std::set<Edge> KnownFeasibleEdges;

public:
  explicit SparseSolver(
      AbstractLatticeFunction<LatticeKey, LatticeVal> *Lattice)
      : LatticeFunc(Lattice) {}
  SparseSolver(const SparseSolver &) = delete;
  SparseSolver &operator=(const SparseSolver &) = delete;

  /// Solve - Solve for constants and executable blocks.
  void Solve();

  void Print(raw_ostream &OS) const;

  /// getExistingValueState - Return the LatticeVal object corresponding to the
  /// given value from the ValueState map. If the value is not in the map,
  /// UntrackedVal is returned, unlike the getValueState method.
  LatticeVal getExistingValueState(LatticeKey Key) const {
    auto I = ValueState.find(Key);
    return I != ValueState.end() ? I->second : LatticeFunc->getUntrackedVal();
  }

  /// getValueState - Return the LatticeVal object corresponding to the given
  /// value from the ValueState map. If the value is not in the map, its state
  /// is initialized.
  LatticeVal getValueState(LatticeKey Key);

  /// isEdgeFeasible - Return true if the control flow edge from the 'From'
  /// basic block to the 'To' basic block is currently feasible.  If
  /// AggressiveUndef is true, then this treats values with unknown lattice
  /// values as undefined.  This is generally only useful when solving the
  /// lattice, not when querying it.
  bool isEdgeFeasible(BasicBlock *From, BasicBlock *To,
                      bool AggressiveUndef = false);

  /// isBlockExecutable - Return true if there are any known feasible
  /// edges into the basic block.  This is generally only useful when
  /// querying the lattice.
  bool isBlockExecutable(BasicBlock *BB) const {
    return BBExecutable.count(BB);
  }

  /// MarkBlockExecutable - This method can be used by clients to mark all of
  /// the blocks that are known to be intrinsically live in the processed unit.
  void MarkBlockExecutable(BasicBlock *BB);

private:
  /// UpdateState - When the state of some LatticeKey is potentially updated to
  /// the given LatticeVal, this function notices and adds the LLVM value
  /// corresponding the key to the work list, if needed.
  void UpdateState(LatticeKey Key, LatticeVal LV);

  /// markEdgeExecutable - Mark a basic block as executable, adding it to the BB
  /// work list if it is not already executable.
  void markEdgeExecutable(BasicBlock *Source, BasicBlock *Dest);

  /// getFeasibleSuccessors - Return a vector of booleans to indicate which
  /// successors are reachable from a given terminator instruction.
  void getFeasibleSuccessors(Instruction &TI, SmallVectorImpl<bool> &Succs,
                             bool AggressiveUndef);

  void visitInst(Instruction &I);
  void visitPHINode(PHINode &I);
  void visitTerminator(Instruction &TI);
};

//===----------------------------------------------------------------------===//
//                  AbstractLatticeFunction Implementation
//===----------------------------------------------------------------------===//

template <class LatticeKey, class LatticeVal>
void AbstractLatticeFunction<LatticeKey, LatticeVal>::PrintLatticeVal(
    LatticeVal V, raw_ostream &OS) {
  if (V == UndefVal)
    OS << "undefined";
  else if (V == OverdefinedVal)
    OS << "overdefined";
  else if (V == UntrackedVal)
    OS << "untracked";
  else
    OS << "unknown lattice value";
}

template <class LatticeKey, class LatticeVal>
void AbstractLatticeFunction<LatticeKey, LatticeVal>::PrintLatticeKey(
    LatticeKey Key, raw_ostream &OS) {
  OS << "unknown lattice key";
}

//===----------------------------------------------------------------------===//
//                          SparseSolver Implementation
//===----------------------------------------------------------------------===//

template <class LatticeKey, class LatticeVal, class KeyInfo>
LatticeVal
SparseSolver<LatticeKey, LatticeVal, KeyInfo>::getValueState(LatticeKey Key) {
  auto I = ValueState.find(Key);
  if (I != ValueState.end())
    return I->second; // Common case, in the map

  if (LatticeFunc->IsUntrackedValue(Key))
    return LatticeFunc->getUntrackedVal();
  LatticeVal LV = LatticeFunc->ComputeLatticeVal(Key);

  // If this value is untracked, don't add it to the map.
  if (LV == LatticeFunc->getUntrackedVal())
    return LV;
  return ValueState[Key] = std::move(LV);
}

template <class LatticeKey, class LatticeVal, class KeyInfo>
void SparseSolver<LatticeKey, LatticeVal, KeyInfo>::UpdateState(LatticeKey Key,
                                                                LatticeVal LV) {
  auto I = ValueState.find(Key);
  if (I != ValueState.end() && I->second == LV)
    return; // No change.

  // Update the state of the given LatticeKey and add its corresponding LLVM
  // value to the work list.
  ValueState[Key] = std::move(LV);
  if (Value *V = KeyInfo::getValueFromLatticeKey(Key))
    ValueWorkList.push_back(V);
}

template <class LatticeKey, class LatticeVal, class KeyInfo>
void SparseSolver<LatticeKey, LatticeVal, KeyInfo>::MarkBlockExecutable(
    BasicBlock *BB) {
  if (!BBExecutable.insert(BB).second)
    return;
  LLVM_DEBUG(dbgs() << "Marking Block Executable: " << BB->getName() << "\n");
  BBWorkList.push_back(BB); // Add the block to the work list!
}

template <class LatticeKey, class LatticeVal, class KeyInfo>
void SparseSolver<LatticeKey, LatticeVal, KeyInfo>::markEdgeExecutable(
    BasicBlock *Source, BasicBlock *Dest) {
  if (!KnownFeasibleEdges.insert(Edge(Source, Dest)).second)
    return; // This edge is already known to be executable!

  LLVM_DEBUG(dbgs() << "Marking Edge Executable: " << Source->getName()
                    << " -> " << Dest->getName() << "\n");

  if (BBExecutable.count(Dest)) {
    // The destination is already executable, but we just made an edge
    // feasible that wasn't before.  Revisit the PHI nodes in the block
    // because they have potentially new operands.
    for (BasicBlock::iterator I = Dest->begin(); isa<PHINode>(I); ++I)
      visitPHINode(*cast<PHINode>(I));
  } else {
    MarkBlockExecutable(Dest);
  }
}

template <class LatticeKey, class LatticeVal, class KeyInfo>
void SparseSolver<LatticeKey, LatticeVal, KeyInfo>::getFeasibleSuccessors(
    Instruction &TI, SmallVectorImpl<bool> &Succs, bool AggressiveUndef) {
  Succs.resize(TI.getNumSuccessors());
  if (TI.getNumSuccessors() == 0)
    return;

  if (BranchInst *BI = dyn_cast<BranchInst>(&TI)) {
    if (BI->isUnconditional()) {
      Succs[0] = true;
      return;
    }

    LatticeVal BCValue;
    if (AggressiveUndef)
      BCValue =
          getValueState(KeyInfo::getLatticeKeyFromValue(BI->getCondition()));
    else
      BCValue = getExistingValueState(
          KeyInfo::getLatticeKeyFromValue(BI->getCondition()));

    if (BCValue == LatticeFunc->getOverdefinedVal() ||
        BCValue == LatticeFunc->getUntrackedVal()) {
      // Overdefined condition variables can branch either way.
      Succs[0] = Succs[1] = true;
      return;
    }

    // If undefined, neither is feasible yet.
    if (BCValue == LatticeFunc->getUndefVal())
      return;

    Constant *C =
        dyn_cast_or_null<Constant>(LatticeFunc->GetValueFromLatticeVal(
            std::move(BCValue), BI->getCondition()->getType()));
    if (!C || !isa<ConstantInt>(C)) {
      // Non-constant values can go either way.
      Succs[0] = Succs[1] = true;
      return;
    }

    // Constant condition variables mean the branch can only go a single way
    Succs[C->isNullValue()] = true;
    return;
  }

  if (TI.isExceptionalTerminator()) {
    Succs.assign(Succs.size(), true);
    return;
  }

  if (isa<IndirectBrInst>(TI)) {
    Succs.assign(Succs.size(), true);
    return;
  }

  SwitchInst &SI = cast<SwitchInst>(TI);
  LatticeVal SCValue;
  if (AggressiveUndef)
    SCValue = getValueState(KeyInfo::getLatticeKeyFromValue(SI.getCondition()));
  else
    SCValue = getExistingValueState(
        KeyInfo::getLatticeKeyFromValue(SI.getCondition()));

  if (SCValue == LatticeFunc->getOverdefinedVal() ||
      SCValue == LatticeFunc->getUntrackedVal()) {
    // All destinations are executable!
    Succs.assign(TI.getNumSuccessors(), true);
    return;
  }

  // If undefined, neither is feasible yet.
  if (SCValue == LatticeFunc->getUndefVal())
    return;

  Constant *C = dyn_cast_or_null<Constant>(LatticeFunc->GetValueFromLatticeVal(
      std::move(SCValue), SI.getCondition()->getType()));
  if (!C || !isa<ConstantInt>(C)) {
    // All destinations are executable!
    Succs.assign(TI.getNumSuccessors(), true);
    return;
  }
  SwitchInst::CaseHandle Case = *SI.findCaseValue(cast<ConstantInt>(C));
  Succs[Case.getSuccessorIndex()] = true;
}

template <class LatticeKey, class LatticeVal, class KeyInfo>
bool SparseSolver<LatticeKey, LatticeVal, KeyInfo>::isEdgeFeasible(
    BasicBlock *From, BasicBlock *To, bool AggressiveUndef) {
  SmallVector<bool, 16> SuccFeasible;
  Instruction *TI = From->getTerminator();
  getFeasibleSuccessors(*TI, SuccFeasible, AggressiveUndef);

  for (unsigned i = 0, e = TI->getNumSuccessors(); i != e; ++i)
    if (TI->getSuccessor(i) == To && SuccFeasible[i])
      return true;

  return false;
}

template <class LatticeKey, class LatticeVal, class KeyInfo>
void SparseSolver<LatticeKey, LatticeVal, KeyInfo>::visitTerminator(
    Instruction &TI) {
  SmallVector<bool, 16> SuccFeasible;
  getFeasibleSuccessors(TI, SuccFeasible, true);

  BasicBlock *BB = TI.getParent();

  // Mark all feasible successors executable...
  for (unsigned i = 0, e = SuccFeasible.size(); i != e; ++i)
    if (SuccFeasible[i])
      markEdgeExecutable(BB, TI.getSuccessor(i));
}

template <class LatticeKey, class LatticeVal, class KeyInfo>
void SparseSolver<LatticeKey, LatticeVal, KeyInfo>::visitPHINode(PHINode &PN) {
  // The lattice function may store more information on a PHINode than could be
  // computed from its incoming values.  For example, SSI form stores its sigma
  // functions as PHINodes with a single incoming value.
  if (LatticeFunc->IsSpecialCasedPHI(&PN)) {
    DenseMap<LatticeKey, LatticeVal> ChangedValues;
    LatticeFunc->ComputeInstructionState(PN, ChangedValues, *this);
    for (auto &ChangedValue : ChangedValues)
      if (ChangedValue.second != LatticeFunc->getUntrackedVal())
        UpdateState(std::move(ChangedValue.first),
                    std::move(ChangedValue.second));
    return;
  }

  LatticeKey Key = KeyInfo::getLatticeKeyFromValue(&PN);
  LatticeVal PNIV = getValueState(Key);
  LatticeVal Overdefined = LatticeFunc->getOverdefinedVal();

  // If this value is already overdefined (common) just return.
  if (PNIV == Overdefined || PNIV == LatticeFunc->getUntrackedVal())
    return; // Quick exit

  // Super-extra-high-degree PHI nodes are unlikely to ever be interesting,
  // and slow us down a lot.  Just mark them overdefined.
  if (PN.getNumIncomingValues() > 64) {
    UpdateState(Key, Overdefined);
    return;
  }

  // Look at all of the executable operands of the PHI node.  If any of them
  // are overdefined, the PHI becomes overdefined as well.  Otherwise, ask the
  // transfer function to give us the merge of the incoming values.
  for (unsigned i = 0, e = PN.getNumIncomingValues(); i != e; ++i) {
    // If the edge is not yet known to be feasible, it doesn't impact the PHI.
    if (!isEdgeFeasible(PN.getIncomingBlock(i), PN.getParent(), true))
      continue;

    // Merge in this value.
    LatticeVal OpVal =
        getValueState(KeyInfo::getLatticeKeyFromValue(PN.getIncomingValue(i)));
    if (OpVal != PNIV)
      PNIV = LatticeFunc->MergeValues(PNIV, OpVal);

    if (PNIV == Overdefined)
      break; // Rest of input values don't matter.
  }

  // Update the PHI with the compute value, which is the merge of the inputs.
  UpdateState(Key, PNIV);
}

template <class LatticeKey, class LatticeVal, class KeyInfo>
void SparseSolver<LatticeKey, LatticeVal, KeyInfo>::visitInst(Instruction &I) {
  // PHIs are handled by the propagation logic, they are never passed into the
  // transfer functions.
  if (PHINode *PN = dyn_cast<PHINode>(&I))
    return visitPHINode(*PN);

  // Otherwise, ask the transfer function what the result is.  If this is
  // something that we care about, remember it.
  DenseMap<LatticeKey, LatticeVal> ChangedValues;
  LatticeFunc->ComputeInstructionState(I, ChangedValues, *this);
  for (auto &ChangedValue : ChangedValues)
    if (ChangedValue.second != LatticeFunc->getUntrackedVal())
      UpdateState(ChangedValue.first, ChangedValue.second);

  if (I.isTerminator())
    visitTerminator(I);
}

template <class LatticeKey, class LatticeVal, class KeyInfo>
void SparseSolver<LatticeKey, LatticeVal, KeyInfo>::Solve() {
  // Process the work lists until they are empty!
  while (!BBWorkList.empty() || !ValueWorkList.empty()) {
    // Process the value work list.
    while (!ValueWorkList.empty()) {
      Value *V = ValueWorkList.back();
      ValueWorkList.pop_back();

      LLVM_DEBUG(dbgs() << "\nPopped off V-WL: " << *V << "\n");

      // "V" got into the work list because it made a transition. See if any
      // users are both live and in need of updating.
      for (User *U : V->users())
        if (Instruction *Inst = dyn_cast<Instruction>(U))
          if (BBExecutable.count(Inst->getParent())) // Inst is executable?
            visitInst(*Inst);
    }

    // Process the basic block work list.
    while (!BBWorkList.empty()) {
      BasicBlock *BB = BBWorkList.back();
      BBWorkList.pop_back();

      LLVM_DEBUG(dbgs() << "\nPopped off BBWL: " << *BB);

      // Notify all instructions in this basic block that they are newly
      // executable.
      for (Instruction &I : *BB)
        visitInst(I);
    }
  }
}

template <class LatticeKey, class LatticeVal, class KeyInfo>
void SparseSolver<LatticeKey, LatticeVal, KeyInfo>::Print(
    raw_ostream &OS) const {
  if (ValueState.empty())
    return;

  LatticeKey Key;
  LatticeVal LV;

  OS << "ValueState:\n";
  for (auto &Entry : ValueState) {
    std::tie(Key, LV) = Entry;
    if (LV == LatticeFunc->getUntrackedVal())
      continue;
    OS << "\t";
    LatticeFunc->PrintLatticeVal(LV, OS);
    OS << ": ";
    LatticeFunc->PrintLatticeKey(Key, OS);
    OS << "\n";
  }
}
} // end namespace llvm

#undef DEBUG_TYPE

#endif // LLVM_ANALYSIS_SPARSEPROPAGATION_H
