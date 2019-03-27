//===----- ScheduleDAGFast.cpp - Fast poor list scheduler -----------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This implements a fast scheduler.
//
//===----------------------------------------------------------------------===//

#include "InstrEmitter.h"
#include "ScheduleDAGSDNodes.h"
#include "SDNodeDbgValue.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallSet.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/CodeGen/SchedulerRegistry.h"
#include "llvm/CodeGen/SelectionDAGISel.h"
#include "llvm/CodeGen/TargetInstrInfo.h"
#include "llvm/CodeGen/TargetRegisterInfo.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/IR/InlineAsm.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/raw_ostream.h"
using namespace llvm;

#define DEBUG_TYPE "pre-RA-sched"

STATISTIC(NumUnfolds,    "Number of nodes unfolded");
STATISTIC(NumDups,       "Number of duplicated nodes");
STATISTIC(NumPRCopies,   "Number of physical copies");

static RegisterScheduler
  fastDAGScheduler("fast", "Fast suboptimal list scheduling",
                   createFastDAGScheduler);
static RegisterScheduler
  linearizeDAGScheduler("linearize", "Linearize DAG, no scheduling",
                        createDAGLinearizer);


namespace {
  /// FastPriorityQueue - A degenerate priority queue that considers
  /// all nodes to have the same priority.
  ///
  struct FastPriorityQueue {
    SmallVector<SUnit *, 16> Queue;

    bool empty() const { return Queue.empty(); }

    void push(SUnit *U) {
      Queue.push_back(U);
    }

    SUnit *pop() {
      if (empty()) return nullptr;
      SUnit *V = Queue.back();
      Queue.pop_back();
      return V;
    }
  };

//===----------------------------------------------------------------------===//
/// ScheduleDAGFast - The actual "fast" list scheduler implementation.
///
class ScheduleDAGFast : public ScheduleDAGSDNodes {
private:
  /// AvailableQueue - The priority queue to use for the available SUnits.
  FastPriorityQueue AvailableQueue;

  /// LiveRegDefs - A set of physical registers and their definition
  /// that are "live". These nodes must be scheduled before any other nodes that
  /// modifies the registers can be scheduled.
  unsigned NumLiveRegs;
  std::vector<SUnit*> LiveRegDefs;
  std::vector<unsigned> LiveRegCycles;

public:
  ScheduleDAGFast(MachineFunction &mf)
    : ScheduleDAGSDNodes(mf) {}

  void Schedule() override;

  /// AddPred - adds a predecessor edge to SUnit SU.
  /// This returns true if this is a new predecessor.
  void AddPred(SUnit *SU, const SDep &D) {
    SU->addPred(D);
  }

  /// RemovePred - removes a predecessor edge from SUnit SU.
  /// This returns true if an edge was removed.
  void RemovePred(SUnit *SU, const SDep &D) {
    SU->removePred(D);
  }

private:
  void ReleasePred(SUnit *SU, SDep *PredEdge);
  void ReleasePredecessors(SUnit *SU, unsigned CurCycle);
  void ScheduleNodeBottomUp(SUnit*, unsigned);
  SUnit *CopyAndMoveSuccessors(SUnit*);
  void InsertCopiesAndMoveSuccs(SUnit*, unsigned,
                                const TargetRegisterClass*,
                                const TargetRegisterClass*,
                                SmallVectorImpl<SUnit*>&);
  bool DelayForLiveRegsBottomUp(SUnit*, SmallVectorImpl<unsigned>&);
  void ListScheduleBottomUp();

  /// forceUnitLatencies - The fast scheduler doesn't care about real latencies.
  bool forceUnitLatencies() const override { return true; }
};
}  // end anonymous namespace


/// Schedule - Schedule the DAG using list scheduling.
void ScheduleDAGFast::Schedule() {
  LLVM_DEBUG(dbgs() << "********** List Scheduling **********\n");

  NumLiveRegs = 0;
  LiveRegDefs.resize(TRI->getNumRegs(), nullptr);
  LiveRegCycles.resize(TRI->getNumRegs(), 0);

  // Build the scheduling graph.
  BuildSchedGraph(nullptr);

  LLVM_DEBUG(dump());

  // Execute the actual scheduling loop.
  ListScheduleBottomUp();
}

//===----------------------------------------------------------------------===//
//  Bottom-Up Scheduling
//===----------------------------------------------------------------------===//

/// ReleasePred - Decrement the NumSuccsLeft count of a predecessor. Add it to
/// the AvailableQueue if the count reaches zero. Also update its cycle bound.
void ScheduleDAGFast::ReleasePred(SUnit *SU, SDep *PredEdge) {
  SUnit *PredSU = PredEdge->getSUnit();

#ifndef NDEBUG
  if (PredSU->NumSuccsLeft == 0) {
    dbgs() << "*** Scheduling failed! ***\n";
    dumpNode(*PredSU);
    dbgs() << " has been released too many times!\n";
    llvm_unreachable(nullptr);
  }
#endif
  --PredSU->NumSuccsLeft;

  // If all the node's successors are scheduled, this node is ready
  // to be scheduled. Ignore the special EntrySU node.
  if (PredSU->NumSuccsLeft == 0 && PredSU != &EntrySU) {
    PredSU->isAvailable = true;
    AvailableQueue.push(PredSU);
  }
}

void ScheduleDAGFast::ReleasePredecessors(SUnit *SU, unsigned CurCycle) {
  // Bottom up: release predecessors
  for (SDep &Pred : SU->Preds) {
    ReleasePred(SU, &Pred);
    if (Pred.isAssignedRegDep()) {
      // This is a physical register dependency and it's impossible or
      // expensive to copy the register. Make sure nothing that can
      // clobber the register is scheduled between the predecessor and
      // this node.
      if (!LiveRegDefs[Pred.getReg()]) {
        ++NumLiveRegs;
        LiveRegDefs[Pred.getReg()] = Pred.getSUnit();
        LiveRegCycles[Pred.getReg()] = CurCycle;
      }
    }
  }
}

/// ScheduleNodeBottomUp - Add the node to the schedule. Decrement the pending
/// count of its predecessors. If a predecessor pending count is zero, add it to
/// the Available queue.
void ScheduleDAGFast::ScheduleNodeBottomUp(SUnit *SU, unsigned CurCycle) {
  LLVM_DEBUG(dbgs() << "*** Scheduling [" << CurCycle << "]: ");
  LLVM_DEBUG(dumpNode(*SU));

  assert(CurCycle >= SU->getHeight() && "Node scheduled below its height!");
  SU->setHeightToAtLeast(CurCycle);
  Sequence.push_back(SU);

  ReleasePredecessors(SU, CurCycle);

  // Release all the implicit physical register defs that are live.
  for (SDep &Succ : SU->Succs) {
    if (Succ.isAssignedRegDep()) {
      if (LiveRegCycles[Succ.getReg()] == Succ.getSUnit()->getHeight()) {
        assert(NumLiveRegs > 0 && "NumLiveRegs is already zero!");
        assert(LiveRegDefs[Succ.getReg()] == SU &&
               "Physical register dependency violated?");
        --NumLiveRegs;
        LiveRegDefs[Succ.getReg()] = nullptr;
        LiveRegCycles[Succ.getReg()] = 0;
      }
    }
  }

  SU->isScheduled = true;
}

/// CopyAndMoveSuccessors - Clone the specified node and move its scheduled
/// successors to the newly created node.
SUnit *ScheduleDAGFast::CopyAndMoveSuccessors(SUnit *SU) {
  if (SU->getNode()->getGluedNode())
    return nullptr;

  SDNode *N = SU->getNode();
  if (!N)
    return nullptr;

  SUnit *NewSU;
  bool TryUnfold = false;
  for (unsigned i = 0, e = N->getNumValues(); i != e; ++i) {
    MVT VT = N->getSimpleValueType(i);
    if (VT == MVT::Glue)
      return nullptr;
    else if (VT == MVT::Other)
      TryUnfold = true;
  }
  for (const SDValue &Op : N->op_values()) {
    MVT VT = Op.getNode()->getSimpleValueType(Op.getResNo());
    if (VT == MVT::Glue)
      return nullptr;
  }

  if (TryUnfold) {
    SmallVector<SDNode*, 2> NewNodes;
    if (!TII->unfoldMemoryOperand(*DAG, N, NewNodes))
      return nullptr;

    LLVM_DEBUG(dbgs() << "Unfolding SU # " << SU->NodeNum << "\n");
    assert(NewNodes.size() == 2 && "Expected a load folding node!");

    N = NewNodes[1];
    SDNode *LoadNode = NewNodes[0];
    unsigned NumVals = N->getNumValues();
    unsigned OldNumVals = SU->getNode()->getNumValues();
    for (unsigned i = 0; i != NumVals; ++i)
      DAG->ReplaceAllUsesOfValueWith(SDValue(SU->getNode(), i), SDValue(N, i));
    DAG->ReplaceAllUsesOfValueWith(SDValue(SU->getNode(), OldNumVals-1),
                                   SDValue(LoadNode, 1));

    SUnit *NewSU = newSUnit(N);
    assert(N->getNodeId() == -1 && "Node already inserted!");
    N->setNodeId(NewSU->NodeNum);

    const MCInstrDesc &MCID = TII->get(N->getMachineOpcode());
    for (unsigned i = 0; i != MCID.getNumOperands(); ++i) {
      if (MCID.getOperandConstraint(i, MCOI::TIED_TO) != -1) {
        NewSU->isTwoAddress = true;
        break;
      }
    }
    if (MCID.isCommutable())
      NewSU->isCommutable = true;

    // LoadNode may already exist. This can happen when there is another
    // load from the same location and producing the same type of value
    // but it has different alignment or volatileness.
    bool isNewLoad = true;
    SUnit *LoadSU;
    if (LoadNode->getNodeId() != -1) {
      LoadSU = &SUnits[LoadNode->getNodeId()];
      isNewLoad = false;
    } else {
      LoadSU = newSUnit(LoadNode);
      LoadNode->setNodeId(LoadSU->NodeNum);
    }

    SDep ChainPred;
    SmallVector<SDep, 4> ChainSuccs;
    SmallVector<SDep, 4> LoadPreds;
    SmallVector<SDep, 4> NodePreds;
    SmallVector<SDep, 4> NodeSuccs;
    for (SDep &Pred : SU->Preds) {
      if (Pred.isCtrl())
        ChainPred = Pred;
      else if (Pred.getSUnit()->getNode() &&
               Pred.getSUnit()->getNode()->isOperandOf(LoadNode))
        LoadPreds.push_back(Pred);
      else
        NodePreds.push_back(Pred);
    }
    for (SDep &Succ : SU->Succs) {
      if (Succ.isCtrl())
        ChainSuccs.push_back(Succ);
      else
        NodeSuccs.push_back(Succ);
    }

    if (ChainPred.getSUnit()) {
      RemovePred(SU, ChainPred);
      if (isNewLoad)
        AddPred(LoadSU, ChainPred);
    }
    for (unsigned i = 0, e = LoadPreds.size(); i != e; ++i) {
      const SDep &Pred = LoadPreds[i];
      RemovePred(SU, Pred);
      if (isNewLoad) {
        AddPred(LoadSU, Pred);
      }
    }
    for (unsigned i = 0, e = NodePreds.size(); i != e; ++i) {
      const SDep &Pred = NodePreds[i];
      RemovePred(SU, Pred);
      AddPred(NewSU, Pred);
    }
    for (unsigned i = 0, e = NodeSuccs.size(); i != e; ++i) {
      SDep D = NodeSuccs[i];
      SUnit *SuccDep = D.getSUnit();
      D.setSUnit(SU);
      RemovePred(SuccDep, D);
      D.setSUnit(NewSU);
      AddPred(SuccDep, D);
    }
    for (unsigned i = 0, e = ChainSuccs.size(); i != e; ++i) {
      SDep D = ChainSuccs[i];
      SUnit *SuccDep = D.getSUnit();
      D.setSUnit(SU);
      RemovePred(SuccDep, D);
      if (isNewLoad) {
        D.setSUnit(LoadSU);
        AddPred(SuccDep, D);
      }
    }
    if (isNewLoad) {
      SDep D(LoadSU, SDep::Barrier);
      D.setLatency(LoadSU->Latency);
      AddPred(NewSU, D);
    }

    ++NumUnfolds;

    if (NewSU->NumSuccsLeft == 0) {
      NewSU->isAvailable = true;
      return NewSU;
    }
    SU = NewSU;
  }

  LLVM_DEBUG(dbgs() << "Duplicating SU # " << SU->NodeNum << "\n");
  NewSU = Clone(SU);

  // New SUnit has the exact same predecessors.
  for (SDep &Pred : SU->Preds)
    if (!Pred.isArtificial())
      AddPred(NewSU, Pred);

  // Only copy scheduled successors. Cut them from old node's successor
  // list and move them over.
  SmallVector<std::pair<SUnit *, SDep>, 4> DelDeps;
  for (SDep &Succ : SU->Succs) {
    if (Succ.isArtificial())
      continue;
    SUnit *SuccSU = Succ.getSUnit();
    if (SuccSU->isScheduled) {
      SDep D = Succ;
      D.setSUnit(NewSU);
      AddPred(SuccSU, D);
      D.setSUnit(SU);
      DelDeps.push_back(std::make_pair(SuccSU, D));
    }
  }
  for (unsigned i = 0, e = DelDeps.size(); i != e; ++i)
    RemovePred(DelDeps[i].first, DelDeps[i].second);

  ++NumDups;
  return NewSU;
}

/// InsertCopiesAndMoveSuccs - Insert register copies and move all
/// scheduled successors of the given SUnit to the last copy.
void ScheduleDAGFast::InsertCopiesAndMoveSuccs(SUnit *SU, unsigned Reg,
                                              const TargetRegisterClass *DestRC,
                                              const TargetRegisterClass *SrcRC,
                                              SmallVectorImpl<SUnit*> &Copies) {
  SUnit *CopyFromSU = newSUnit(static_cast<SDNode *>(nullptr));
  CopyFromSU->CopySrcRC = SrcRC;
  CopyFromSU->CopyDstRC = DestRC;

  SUnit *CopyToSU = newSUnit(static_cast<SDNode *>(nullptr));
  CopyToSU->CopySrcRC = DestRC;
  CopyToSU->CopyDstRC = SrcRC;

  // Only copy scheduled successors. Cut them from old node's successor
  // list and move them over.
  SmallVector<std::pair<SUnit *, SDep>, 4> DelDeps;
  for (SDep &Succ : SU->Succs) {
    if (Succ.isArtificial())
      continue;
    SUnit *SuccSU = Succ.getSUnit();
    if (SuccSU->isScheduled) {
      SDep D = Succ;
      D.setSUnit(CopyToSU);
      AddPred(SuccSU, D);
      DelDeps.push_back(std::make_pair(SuccSU, Succ));
    }
  }
  for (unsigned i = 0, e = DelDeps.size(); i != e; ++i) {
    RemovePred(DelDeps[i].first, DelDeps[i].second);
  }
  SDep FromDep(SU, SDep::Data, Reg);
  FromDep.setLatency(SU->Latency);
  AddPred(CopyFromSU, FromDep);
  SDep ToDep(CopyFromSU, SDep::Data, 0);
  ToDep.setLatency(CopyFromSU->Latency);
  AddPred(CopyToSU, ToDep);

  Copies.push_back(CopyFromSU);
  Copies.push_back(CopyToSU);

  ++NumPRCopies;
}

/// getPhysicalRegisterVT - Returns the ValueType of the physical register
/// definition of the specified node.
/// FIXME: Move to SelectionDAG?
static MVT getPhysicalRegisterVT(SDNode *N, unsigned Reg,
                                 const TargetInstrInfo *TII) {
  unsigned NumRes;
  if (N->getOpcode() == ISD::CopyFromReg) {
    // CopyFromReg has: "chain, Val, glue" so operand 1 gives the type.
    NumRes = 1;
  } else {
    const MCInstrDesc &MCID = TII->get(N->getMachineOpcode());
    assert(MCID.ImplicitDefs && "Physical reg def must be in implicit def list!");
    NumRes = MCID.getNumDefs();
    for (const MCPhysReg *ImpDef = MCID.getImplicitDefs(); *ImpDef; ++ImpDef) {
      if (Reg == *ImpDef)
        break;
      ++NumRes;
    }
  }
  return N->getSimpleValueType(NumRes);
}

/// CheckForLiveRegDef - Return true and update live register vector if the
/// specified register def of the specified SUnit clobbers any "live" registers.
static bool CheckForLiveRegDef(SUnit *SU, unsigned Reg,
                               std::vector<SUnit*> &LiveRegDefs,
                               SmallSet<unsigned, 4> &RegAdded,
                               SmallVectorImpl<unsigned> &LRegs,
                               const TargetRegisterInfo *TRI) {
  bool Added = false;
  for (MCRegAliasIterator AI(Reg, TRI, true); AI.isValid(); ++AI) {
    if (LiveRegDefs[*AI] && LiveRegDefs[*AI] != SU) {
      if (RegAdded.insert(*AI).second) {
        LRegs.push_back(*AI);
        Added = true;
      }
    }
  }
  return Added;
}

/// DelayForLiveRegsBottomUp - Returns true if it is necessary to delay
/// scheduling of the given node to satisfy live physical register dependencies.
/// If the specific node is the last one that's available to schedule, do
/// whatever is necessary (i.e. backtracking or cloning) to make it possible.
bool ScheduleDAGFast::DelayForLiveRegsBottomUp(SUnit *SU,
                                              SmallVectorImpl<unsigned> &LRegs){
  if (NumLiveRegs == 0)
    return false;

  SmallSet<unsigned, 4> RegAdded;
  // If this node would clobber any "live" register, then it's not ready.
  for (SDep &Pred : SU->Preds) {
    if (Pred.isAssignedRegDep()) {
      CheckForLiveRegDef(Pred.getSUnit(), Pred.getReg(), LiveRegDefs,
                         RegAdded, LRegs, TRI);
    }
  }

  for (SDNode *Node = SU->getNode(); Node; Node = Node->getGluedNode()) {
    if (Node->getOpcode() == ISD::INLINEASM) {
      // Inline asm can clobber physical defs.
      unsigned NumOps = Node->getNumOperands();
      if (Node->getOperand(NumOps-1).getValueType() == MVT::Glue)
        --NumOps;  // Ignore the glue operand.

      for (unsigned i = InlineAsm::Op_FirstOperand; i != NumOps;) {
        unsigned Flags =
          cast<ConstantSDNode>(Node->getOperand(i))->getZExtValue();
        unsigned NumVals = InlineAsm::getNumOperandRegisters(Flags);

        ++i; // Skip the ID value.
        if (InlineAsm::isRegDefKind(Flags) ||
            InlineAsm::isRegDefEarlyClobberKind(Flags) ||
            InlineAsm::isClobberKind(Flags)) {
          // Check for def of register or earlyclobber register.
          for (; NumVals; --NumVals, ++i) {
            unsigned Reg = cast<RegisterSDNode>(Node->getOperand(i))->getReg();
            if (TargetRegisterInfo::isPhysicalRegister(Reg))
              CheckForLiveRegDef(SU, Reg, LiveRegDefs, RegAdded, LRegs, TRI);
          }
        } else
          i += NumVals;
      }
      continue;
    }
    if (!Node->isMachineOpcode())
      continue;
    const MCInstrDesc &MCID = TII->get(Node->getMachineOpcode());
    if (!MCID.ImplicitDefs)
      continue;
    for (const MCPhysReg *Reg = MCID.getImplicitDefs(); *Reg; ++Reg) {
      CheckForLiveRegDef(SU, *Reg, LiveRegDefs, RegAdded, LRegs, TRI);
    }
  }
  return !LRegs.empty();
}


/// ListScheduleBottomUp - The main loop of list scheduling for bottom-up
/// schedulers.
void ScheduleDAGFast::ListScheduleBottomUp() {
  unsigned CurCycle = 0;

  // Release any predecessors of the special Exit node.
  ReleasePredecessors(&ExitSU, CurCycle);

  // Add root to Available queue.
  if (!SUnits.empty()) {
    SUnit *RootSU = &SUnits[DAG->getRoot().getNode()->getNodeId()];
    assert(RootSU->Succs.empty() && "Graph root shouldn't have successors!");
    RootSU->isAvailable = true;
    AvailableQueue.push(RootSU);
  }

  // While Available queue is not empty, grab the node with the highest
  // priority. If it is not ready put it back.  Schedule the node.
  SmallVector<SUnit*, 4> NotReady;
  DenseMap<SUnit*, SmallVector<unsigned, 4> > LRegsMap;
  Sequence.reserve(SUnits.size());
  while (!AvailableQueue.empty()) {
    bool Delayed = false;
    LRegsMap.clear();
    SUnit *CurSU = AvailableQueue.pop();
    while (CurSU) {
      SmallVector<unsigned, 4> LRegs;
      if (!DelayForLiveRegsBottomUp(CurSU, LRegs))
        break;
      Delayed = true;
      LRegsMap.insert(std::make_pair(CurSU, LRegs));

      CurSU->isPending = true;  // This SU is not in AvailableQueue right now.
      NotReady.push_back(CurSU);
      CurSU = AvailableQueue.pop();
    }

    // All candidates are delayed due to live physical reg dependencies.
    // Try code duplication or inserting cross class copies
    // to resolve it.
    if (Delayed && !CurSU) {
      if (!CurSU) {
        // Try duplicating the nodes that produces these
        // "expensive to copy" values to break the dependency. In case even
        // that doesn't work, insert cross class copies.
        SUnit *TrySU = NotReady[0];
        SmallVectorImpl<unsigned> &LRegs = LRegsMap[TrySU];
        assert(LRegs.size() == 1 && "Can't handle this yet!");
        unsigned Reg = LRegs[0];
        SUnit *LRDef = LiveRegDefs[Reg];
        MVT VT = getPhysicalRegisterVT(LRDef->getNode(), Reg, TII);
        const TargetRegisterClass *RC =
          TRI->getMinimalPhysRegClass(Reg, VT);
        const TargetRegisterClass *DestRC = TRI->getCrossCopyRegClass(RC);

        // If cross copy register class is the same as RC, then it must be
        // possible copy the value directly. Do not try duplicate the def.
        // If cross copy register class is not the same as RC, then it's
        // possible to copy the value but it require cross register class copies
        // and it is expensive.
        // If cross copy register class is null, then it's not possible to copy
        // the value at all.
        SUnit *NewDef = nullptr;
        if (DestRC != RC) {
          NewDef = CopyAndMoveSuccessors(LRDef);
          if (!DestRC && !NewDef)
            report_fatal_error("Can't handle live physical "
                               "register dependency!");
        }
        if (!NewDef) {
          // Issue copies, these can be expensive cross register class copies.
          SmallVector<SUnit*, 2> Copies;
          InsertCopiesAndMoveSuccs(LRDef, Reg, DestRC, RC, Copies);
          LLVM_DEBUG(dbgs() << "Adding an edge from SU # " << TrySU->NodeNum
                            << " to SU #" << Copies.front()->NodeNum << "\n");
          AddPred(TrySU, SDep(Copies.front(), SDep::Artificial));
          NewDef = Copies.back();
        }

        LLVM_DEBUG(dbgs() << "Adding an edge from SU # " << NewDef->NodeNum
                          << " to SU #" << TrySU->NodeNum << "\n");
        LiveRegDefs[Reg] = NewDef;
        AddPred(NewDef, SDep(TrySU, SDep::Artificial));
        TrySU->isAvailable = false;
        CurSU = NewDef;
      }

      if (!CurSU) {
        llvm_unreachable("Unable to resolve live physical register dependencies!");
      }
    }

    // Add the nodes that aren't ready back onto the available list.
    for (unsigned i = 0, e = NotReady.size(); i != e; ++i) {
      NotReady[i]->isPending = false;
      // May no longer be available due to backtracking.
      if (NotReady[i]->isAvailable)
        AvailableQueue.push(NotReady[i]);
    }
    NotReady.clear();

    if (CurSU)
      ScheduleNodeBottomUp(CurSU, CurCycle);
    ++CurCycle;
  }

  // Reverse the order since it is bottom up.
  std::reverse(Sequence.begin(), Sequence.end());

#ifndef NDEBUG
  VerifyScheduledSequence(/*isBottomUp=*/true);
#endif
}


namespace {
//===----------------------------------------------------------------------===//
// ScheduleDAGLinearize - No scheduling scheduler, it simply linearize the
// DAG in topological order.
// IMPORTANT: this may not work for targets with phyreg dependency.
//
class ScheduleDAGLinearize : public ScheduleDAGSDNodes {
public:
  ScheduleDAGLinearize(MachineFunction &mf) : ScheduleDAGSDNodes(mf) {}

  void Schedule() override;

  MachineBasicBlock *
    EmitSchedule(MachineBasicBlock::iterator &InsertPos) override;

private:
  std::vector<SDNode*> Sequence;
  DenseMap<SDNode*, SDNode*> GluedMap;  // Cache glue to its user

  void ScheduleNode(SDNode *N);
};
} // end anonymous namespace

void ScheduleDAGLinearize::ScheduleNode(SDNode *N) {
  if (N->getNodeId() != 0)
    llvm_unreachable(nullptr);

  if (!N->isMachineOpcode() &&
      (N->getOpcode() == ISD::EntryToken || isPassiveNode(N)))
    // These nodes do not need to be translated into MIs.
    return;

  LLVM_DEBUG(dbgs() << "\n*** Scheduling: ");
  LLVM_DEBUG(N->dump(DAG));
  Sequence.push_back(N);

  unsigned NumOps = N->getNumOperands();
  if (unsigned NumLeft = NumOps) {
    SDNode *GluedOpN = nullptr;
    do {
      const SDValue &Op = N->getOperand(NumLeft-1);
      SDNode *OpN = Op.getNode();

      if (NumLeft == NumOps && Op.getValueType() == MVT::Glue) {
        // Schedule glue operand right above N.
        GluedOpN = OpN;
        assert(OpN->getNodeId() != 0 && "Glue operand not ready?");
        OpN->setNodeId(0);
        ScheduleNode(OpN);
        continue;
      }

      if (OpN == GluedOpN)
        // Glue operand is already scheduled.
        continue;

      DenseMap<SDNode*, SDNode*>::iterator DI = GluedMap.find(OpN);
      if (DI != GluedMap.end() && DI->second != N)
        // Users of glues are counted against the glued users.
        OpN = DI->second;

      unsigned Degree = OpN->getNodeId();
      assert(Degree > 0 && "Predecessor over-released!");
      OpN->setNodeId(--Degree);
      if (Degree == 0)
        ScheduleNode(OpN);
    } while (--NumLeft);
  }
}

/// findGluedUser - Find the representative use of a glue value by walking
/// the use chain.
static SDNode *findGluedUser(SDNode *N) {
  while (SDNode *Glued = N->getGluedUser())
    N = Glued;
  return N;
}

void ScheduleDAGLinearize::Schedule() {
  LLVM_DEBUG(dbgs() << "********** DAG Linearization **********\n");

  SmallVector<SDNode*, 8> Glues;
  unsigned DAGSize = 0;
  for (SDNode &Node : DAG->allnodes()) {
    SDNode *N = &Node;

    // Use node id to record degree.
    unsigned Degree = N->use_size();
    N->setNodeId(Degree);
    unsigned NumVals = N->getNumValues();
    if (NumVals && N->getValueType(NumVals-1) == MVT::Glue &&
        N->hasAnyUseOfValue(NumVals-1)) {
      SDNode *User = findGluedUser(N);
      if (User) {
        Glues.push_back(N);
        GluedMap.insert(std::make_pair(N, User));
      }
    }

    if (N->isMachineOpcode() ||
        (N->getOpcode() != ISD::EntryToken && !isPassiveNode(N)))
      ++DAGSize;
  }

  for (unsigned i = 0, e = Glues.size(); i != e; ++i) {
    SDNode *Glue = Glues[i];
    SDNode *GUser = GluedMap[Glue];
    unsigned Degree = Glue->getNodeId();
    unsigned UDegree = GUser->getNodeId();

    // Glue user must be scheduled together with the glue operand. So other
    // users of the glue operand must be treated as its users.
    SDNode *ImmGUser = Glue->getGluedUser();
    for (const SDNode *U : Glue->uses())
      if (U == ImmGUser)
        --Degree;
    GUser->setNodeId(UDegree + Degree);
    Glue->setNodeId(1);
  }

  Sequence.reserve(DAGSize);
  ScheduleNode(DAG->getRoot().getNode());
}

MachineBasicBlock*
ScheduleDAGLinearize::EmitSchedule(MachineBasicBlock::iterator &InsertPos) {
  InstrEmitter Emitter(BB, InsertPos);
  DenseMap<SDValue, unsigned> VRBaseMap;

  LLVM_DEBUG({ dbgs() << "\n*** Final schedule ***\n"; });

  unsigned NumNodes = Sequence.size();
  MachineBasicBlock *BB = Emitter.getBlock();
  for (unsigned i = 0; i != NumNodes; ++i) {
    SDNode *N = Sequence[NumNodes-i-1];
    LLVM_DEBUG(N->dump(DAG));
    Emitter.EmitNode(N, false, false, VRBaseMap);

    // Emit any debug values associated with the node.
    if (N->getHasDebugValue()) {
      MachineBasicBlock::iterator InsertPos = Emitter.getInsertPos();
      for (auto DV : DAG->GetDbgValues(N)) {
        if (!DV->isEmitted())
          if (auto *DbgMI = Emitter.EmitDbgValue(DV, VRBaseMap))
            BB->insert(InsertPos, DbgMI);
      }
    }
  }

  LLVM_DEBUG(dbgs() << '\n');

  InsertPos = Emitter.getInsertPos();
  return Emitter.getBlock();
}

//===----------------------------------------------------------------------===//
//                         Public Constructor Functions
//===----------------------------------------------------------------------===//

llvm::ScheduleDAGSDNodes *
llvm::createFastDAGScheduler(SelectionDAGISel *IS, CodeGenOpt::Level) {
  return new ScheduleDAGFast(*IS->MF);
}

llvm::ScheduleDAGSDNodes *
llvm::createDAGLinearizer(SelectionDAGISel *IS, CodeGenOpt::Level) {
  return new ScheduleDAGLinearize(*IS->MF);
}
