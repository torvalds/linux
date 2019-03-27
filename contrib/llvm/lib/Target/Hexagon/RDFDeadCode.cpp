//===--- RDFDeadCode.cpp --------------------------------------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// RDF-based generic dead code elimination.

#include "RDFDeadCode.h"
#include "RDFGraph.h"
#include "RDFLiveness.h"

#include "llvm/ADT/SetVector.h"
#include "llvm/CodeGen/MachineBasicBlock.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"

#include <queue>

using namespace llvm;
using namespace rdf;

// This drastically improves execution time in "collect" over using
// SetVector as a work queue, and popping the first element from it.
template<typename T> struct DeadCodeElimination::SetQueue {
  SetQueue() : Set(), Queue() {}

  bool empty() const {
    return Queue.empty();
  }
  T pop_front() {
    T V = Queue.front();
    Queue.pop();
    Set.erase(V);
    return V;
  }
  void push_back(T V) {
    if (Set.count(V))
      return;
    Queue.push(V);
    Set.insert(V);
  }

private:
  DenseSet<T> Set;
  std::queue<T> Queue;
};


// Check if the given instruction has observable side-effects, i.e. if
// it should be considered "live". It is safe for this function to be
// overly conservative (i.e. return "true" for all instructions), but it
// is not safe to return "false" for an instruction that should not be
// considered removable.
bool DeadCodeElimination::isLiveInstr(const MachineInstr *MI) const {
  if (MI->mayStore() || MI->isBranch() || MI->isCall() || MI->isReturn())
    return true;
  if (MI->hasOrderedMemoryRef() || MI->hasUnmodeledSideEffects() ||
      MI->isPosition())
    return true;
  if (MI->isPHI())
    return false;
  for (auto &Op : MI->operands()) {
    if (Op.isReg() && MRI.isReserved(Op.getReg()))
      return true;
    if (Op.isRegMask()) {
      const uint32_t *BM = Op.getRegMask();
      for (unsigned R = 0, RN = DFG.getTRI().getNumRegs(); R != RN; ++R) {
        if (BM[R/32] & (1u << (R%32)))
          continue;
        if (MRI.isReserved(R))
          return true;
      }
    }
  }
  return false;
}

void DeadCodeElimination::scanInstr(NodeAddr<InstrNode*> IA,
      SetQueue<NodeId> &WorkQ) {
  if (!DFG.IsCode<NodeAttrs::Stmt>(IA))
    return;
  if (!isLiveInstr(NodeAddr<StmtNode*>(IA).Addr->getCode()))
    return;
  for (NodeAddr<RefNode*> RA : IA.Addr->members(DFG)) {
    if (!LiveNodes.count(RA.Id))
      WorkQ.push_back(RA.Id);
  }
}

void DeadCodeElimination::processDef(NodeAddr<DefNode*> DA,
      SetQueue<NodeId> &WorkQ) {
  NodeAddr<InstrNode*> IA = DA.Addr->getOwner(DFG);
  for (NodeAddr<UseNode*> UA : IA.Addr->members_if(DFG.IsUse, DFG)) {
    if (!LiveNodes.count(UA.Id))
      WorkQ.push_back(UA.Id);
  }
  for (NodeAddr<DefNode*> TA : DFG.getRelatedRefs(IA, DA))
    LiveNodes.insert(TA.Id);
}

void DeadCodeElimination::processUse(NodeAddr<UseNode*> UA,
      SetQueue<NodeId> &WorkQ) {
  for (NodeAddr<DefNode*> DA : LV.getAllReachingDefs(UA)) {
    if (!LiveNodes.count(DA.Id))
      WorkQ.push_back(DA.Id);
  }
}

// Traverse the DFG and collect the set dead RefNodes and the set of
// dead instructions. Return "true" if any of these sets is non-empty,
// "false" otherwise.
bool DeadCodeElimination::collect() {
  // This function works by first finding all live nodes. The dead nodes
  // are then the complement of the set of live nodes.
  //
  // Assume that all nodes are dead. Identify instructions which must be
  // considered live, i.e. instructions with observable side-effects, such
  // as calls and stores. All arguments of such instructions are considered
  // live. For each live def, all operands used in the corresponding
  // instruction are considered live. For each live use, all its reaching
  // defs are considered live.
  LiveNodes.clear();
  SetQueue<NodeId> WorkQ;
  for (NodeAddr<BlockNode*> BA : DFG.getFunc().Addr->members(DFG))
    for (NodeAddr<InstrNode*> IA : BA.Addr->members(DFG))
      scanInstr(IA, WorkQ);

  while (!WorkQ.empty()) {
    NodeId N = WorkQ.pop_front();
    LiveNodes.insert(N);
    auto RA = DFG.addr<RefNode*>(N);
    if (DFG.IsDef(RA))
      processDef(RA, WorkQ);
    else
      processUse(RA, WorkQ);
  }

  if (trace()) {
    dbgs() << "Live nodes:\n";
    for (NodeId N : LiveNodes) {
      auto RA = DFG.addr<RefNode*>(N);
      dbgs() << PrintNode<RefNode*>(RA, DFG) << "\n";
    }
  }

  auto IsDead = [this] (NodeAddr<InstrNode*> IA) -> bool {
    for (NodeAddr<DefNode*> DA : IA.Addr->members_if(DFG.IsDef, DFG))
      if (LiveNodes.count(DA.Id))
        return false;
    return true;
  };

  for (NodeAddr<BlockNode*> BA : DFG.getFunc().Addr->members(DFG)) {
    for (NodeAddr<InstrNode*> IA : BA.Addr->members(DFG)) {
      for (NodeAddr<RefNode*> RA : IA.Addr->members(DFG))
        if (!LiveNodes.count(RA.Id))
          DeadNodes.insert(RA.Id);
      if (DFG.IsCode<NodeAttrs::Stmt>(IA))
        if (isLiveInstr(NodeAddr<StmtNode*>(IA).Addr->getCode()))
          continue;
      if (IsDead(IA)) {
        DeadInstrs.insert(IA.Id);
        if (trace())
          dbgs() << "Dead instr: " << PrintNode<InstrNode*>(IA, DFG) << "\n";
      }
    }
  }

  return !DeadNodes.empty();
}

// Erase the nodes given in the Nodes set from DFG. In addition to removing
// them from the DFG, if a node corresponds to a statement, the corresponding
// machine instruction is erased from the function.
bool DeadCodeElimination::erase(const SetVector<NodeId> &Nodes) {
  if (Nodes.empty())
    return false;

  // Prepare the actual set of ref nodes to remove: ref nodes from Nodes
  // are included directly, for each InstrNode in Nodes, include the set
  // of all RefNodes from it.
  NodeList DRNs, DINs;
  for (auto I : Nodes) {
    auto BA = DFG.addr<NodeBase*>(I);
    uint16_t Type = BA.Addr->getType();
    if (Type == NodeAttrs::Ref) {
      DRNs.push_back(DFG.addr<RefNode*>(I));
      continue;
    }

    // If it's a code node, add all ref nodes from it.
    uint16_t Kind = BA.Addr->getKind();
    if (Kind == NodeAttrs::Stmt || Kind == NodeAttrs::Phi) {
      for (auto N : NodeAddr<CodeNode*>(BA).Addr->members(DFG))
        DRNs.push_back(N);
      DINs.push_back(DFG.addr<InstrNode*>(I));
    } else {
      llvm_unreachable("Unexpected code node");
      return false;
    }
  }

  // Sort the list so that use nodes are removed first. This makes the
  // "unlink" functions a bit faster.
  auto UsesFirst = [] (NodeAddr<RefNode*> A, NodeAddr<RefNode*> B) -> bool {
    uint16_t KindA = A.Addr->getKind(), KindB = B.Addr->getKind();
    if (KindA == NodeAttrs::Use && KindB == NodeAttrs::Def)
      return true;
    if (KindA == NodeAttrs::Def && KindB == NodeAttrs::Use)
      return false;
    return A.Id < B.Id;
  };
  llvm::sort(DRNs, UsesFirst);

  if (trace())
    dbgs() << "Removing dead ref nodes:\n";
  for (NodeAddr<RefNode*> RA : DRNs) {
    if (trace())
      dbgs() << "  " << PrintNode<RefNode*>(RA, DFG) << '\n';
    if (DFG.IsUse(RA))
      DFG.unlinkUse(RA, true);
    else if (DFG.IsDef(RA))
      DFG.unlinkDef(RA, true);
  }

  // Now, remove all dead instruction nodes.
  for (NodeAddr<InstrNode*> IA : DINs) {
    NodeAddr<BlockNode*> BA = IA.Addr->getOwner(DFG);
    BA.Addr->removeMember(IA, DFG);
    if (!DFG.IsCode<NodeAttrs::Stmt>(IA))
      continue;

    MachineInstr *MI = NodeAddr<StmtNode*>(IA).Addr->getCode();
    if (trace())
      dbgs() << "erasing: " << *MI;
    MI->eraseFromParent();
  }
  return true;
}
