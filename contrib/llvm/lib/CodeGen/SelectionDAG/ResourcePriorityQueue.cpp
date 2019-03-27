//===- ResourcePriorityQueue.cpp - A DFA-oriented priority queue -*- C++ -*-==//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file implements the ResourcePriorityQueue class, which is a
// SchedulingPriorityQueue that prioritizes instructions using DFA state to
// reduce the length of the critical path through the basic block
// on VLIW platforms.
// The scheduler is basically a top-down adaptable list scheduler with DFA
// resource tracking added to the cost function.
// DFA is queried as a state machine to model "packets/bundles" during
// schedule. Currently packets/bundles are discarded at the end of
// scheduling, affecting only order of instructions.
//
//===----------------------------------------------------------------------===//

#include "llvm/CodeGen/ResourcePriorityQueue.h"
#include "llvm/CodeGen/MachineInstr.h"
#include "llvm/CodeGen/SelectionDAGNodes.h"
#include "llvm/CodeGen/TargetLowering.h"
#include "llvm/CodeGen/TargetSubtargetInfo.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Target/TargetMachine.h"

using namespace llvm;

#define DEBUG_TYPE "scheduler"

static cl::opt<bool> DisableDFASched("disable-dfa-sched", cl::Hidden,
  cl::ZeroOrMore, cl::init(false),
  cl::desc("Disable use of DFA during scheduling"));

static cl::opt<int> RegPressureThreshold(
  "dfa-sched-reg-pressure-threshold", cl::Hidden, cl::ZeroOrMore, cl::init(5),
  cl::desc("Track reg pressure and switch priority to in-depth"));

ResourcePriorityQueue::ResourcePriorityQueue(SelectionDAGISel *IS)
    : Picker(this), InstrItins(IS->MF->getSubtarget().getInstrItineraryData()) {
  const TargetSubtargetInfo &STI = IS->MF->getSubtarget();
  TRI = STI.getRegisterInfo();
  TLI = IS->TLI;
  TII = STI.getInstrInfo();
  ResourcesModel.reset(TII->CreateTargetScheduleState(STI));
  // This hard requirement could be relaxed, but for now
  // do not let it proceed.
  assert(ResourcesModel && "Unimplemented CreateTargetScheduleState.");

  unsigned NumRC = TRI->getNumRegClasses();
  RegLimit.resize(NumRC);
  RegPressure.resize(NumRC);
  std::fill(RegLimit.begin(), RegLimit.end(), 0);
  std::fill(RegPressure.begin(), RegPressure.end(), 0);
  for (const TargetRegisterClass *RC : TRI->regclasses())
    RegLimit[RC->getID()] = TRI->getRegPressureLimit(RC, *IS->MF);

  ParallelLiveRanges = 0;
  HorizontalVerticalBalance = 0;
}

unsigned
ResourcePriorityQueue::numberRCValPredInSU(SUnit *SU, unsigned RCId) {
  unsigned NumberDeps = 0;
  for (SDep &Pred : SU->Preds) {
    if (Pred.isCtrl())
      continue;

    SUnit *PredSU = Pred.getSUnit();
    const SDNode *ScegN = PredSU->getNode();

    if (!ScegN)
      continue;

    // If value is passed to CopyToReg, it is probably
    // live outside BB.
    switch (ScegN->getOpcode()) {
      default:  break;
      case ISD::TokenFactor:    break;
      case ISD::CopyFromReg:    NumberDeps++;  break;
      case ISD::CopyToReg:      break;
      case ISD::INLINEASM:      break;
    }
    if (!ScegN->isMachineOpcode())
      continue;

    for (unsigned i = 0, e = ScegN->getNumValues(); i != e; ++i) {
      MVT VT = ScegN->getSimpleValueType(i);
      if (TLI->isTypeLegal(VT)
          && (TLI->getRegClassFor(VT)->getID() == RCId)) {
        NumberDeps++;
        break;
      }
    }
  }
  return NumberDeps;
}

unsigned ResourcePriorityQueue::numberRCValSuccInSU(SUnit *SU,
                                                    unsigned RCId) {
  unsigned NumberDeps = 0;
  for (const SDep &Succ : SU->Succs) {
    if (Succ.isCtrl())
      continue;

    SUnit *SuccSU = Succ.getSUnit();
    const SDNode *ScegN = SuccSU->getNode();
    if (!ScegN)
      continue;

    // If value is passed to CopyToReg, it is probably
    // live outside BB.
    switch (ScegN->getOpcode()) {
      default:  break;
      case ISD::TokenFactor:    break;
      case ISD::CopyFromReg:    break;
      case ISD::CopyToReg:      NumberDeps++;  break;
      case ISD::INLINEASM:      break;
    }
    if (!ScegN->isMachineOpcode())
      continue;

    for (unsigned i = 0, e = ScegN->getNumOperands(); i != e; ++i) {
      const SDValue &Op = ScegN->getOperand(i);
      MVT VT = Op.getNode()->getSimpleValueType(Op.getResNo());
      if (TLI->isTypeLegal(VT)
          && (TLI->getRegClassFor(VT)->getID() == RCId)) {
        NumberDeps++;
        break;
      }
    }
  }
  return NumberDeps;
}

static unsigned numberCtrlDepsInSU(SUnit *SU) {
  unsigned NumberDeps = 0;
  for (const SDep &Succ : SU->Succs)
    if (Succ.isCtrl())
      NumberDeps++;

  return NumberDeps;
}

static unsigned numberCtrlPredInSU(SUnit *SU) {
  unsigned NumberDeps = 0;
  for (SDep &Pred : SU->Preds)
    if (Pred.isCtrl())
      NumberDeps++;

  return NumberDeps;
}

///
/// Initialize nodes.
///
void ResourcePriorityQueue::initNodes(std::vector<SUnit> &sunits) {
  SUnits = &sunits;
  NumNodesSolelyBlocking.resize(SUnits->size(), 0);

  for (unsigned i = 0, e = SUnits->size(); i != e; ++i) {
    SUnit *SU = &(*SUnits)[i];
    initNumRegDefsLeft(SU);
    SU->NodeQueueId = 0;
  }
}

/// This heuristic is used if DFA scheduling is not desired
/// for some VLIW platform.
bool resource_sort::operator()(const SUnit *LHS, const SUnit *RHS) const {
  // The isScheduleHigh flag allows nodes with wraparound dependencies that
  // cannot easily be modeled as edges with latencies to be scheduled as
  // soon as possible in a top-down schedule.
  if (LHS->isScheduleHigh && !RHS->isScheduleHigh)
    return false;

  if (!LHS->isScheduleHigh && RHS->isScheduleHigh)
    return true;

  unsigned LHSNum = LHS->NodeNum;
  unsigned RHSNum = RHS->NodeNum;

  // The most important heuristic is scheduling the critical path.
  unsigned LHSLatency = PQ->getLatency(LHSNum);
  unsigned RHSLatency = PQ->getLatency(RHSNum);
  if (LHSLatency < RHSLatency) return true;
  if (LHSLatency > RHSLatency) return false;

  // After that, if two nodes have identical latencies, look to see if one will
  // unblock more other nodes than the other.
  unsigned LHSBlocked = PQ->getNumSolelyBlockNodes(LHSNum);
  unsigned RHSBlocked = PQ->getNumSolelyBlockNodes(RHSNum);
  if (LHSBlocked < RHSBlocked) return true;
  if (LHSBlocked > RHSBlocked) return false;

  // Finally, just to provide a stable ordering, use the node number as a
  // deciding factor.
  return LHSNum < RHSNum;
}


/// getSingleUnscheduledPred - If there is exactly one unscheduled predecessor
/// of SU, return it, otherwise return null.
SUnit *ResourcePriorityQueue::getSingleUnscheduledPred(SUnit *SU) {
  SUnit *OnlyAvailablePred = nullptr;
  for (const SDep &Pred : SU->Preds) {
    SUnit &PredSU = *Pred.getSUnit();
    if (!PredSU.isScheduled) {
      // We found an available, but not scheduled, predecessor.  If it's the
      // only one we have found, keep track of it... otherwise give up.
      if (OnlyAvailablePred && OnlyAvailablePred != &PredSU)
        return nullptr;
      OnlyAvailablePred = &PredSU;
    }
  }
  return OnlyAvailablePred;
}

void ResourcePriorityQueue::push(SUnit *SU) {
  // Look at all of the successors of this node.  Count the number of nodes that
  // this node is the sole unscheduled node for.
  unsigned NumNodesBlocking = 0;
  for (const SDep &Succ : SU->Succs)
    if (getSingleUnscheduledPred(Succ.getSUnit()) == SU)
      ++NumNodesBlocking;

  NumNodesSolelyBlocking[SU->NodeNum] = NumNodesBlocking;
  Queue.push_back(SU);
}

/// Check if scheduling of this SU is possible
/// in the current packet.
bool ResourcePriorityQueue::isResourceAvailable(SUnit *SU) {
  if (!SU || !SU->getNode())
    return false;

  // If this is a compound instruction,
  // it is likely to be a call. Do not delay it.
  if (SU->getNode()->getGluedNode())
    return true;

  // First see if the pipeline could receive this instruction
  // in the current cycle.
  if (SU->getNode()->isMachineOpcode())
    switch (SU->getNode()->getMachineOpcode()) {
    default:
      if (!ResourcesModel->canReserveResources(&TII->get(
          SU->getNode()->getMachineOpcode())))
           return false;
      break;
    case TargetOpcode::EXTRACT_SUBREG:
    case TargetOpcode::INSERT_SUBREG:
    case TargetOpcode::SUBREG_TO_REG:
    case TargetOpcode::REG_SEQUENCE:
    case TargetOpcode::IMPLICIT_DEF:
        break;
    }

  // Now see if there are no other dependencies
  // to instructions already in the packet.
  for (unsigned i = 0, e = Packet.size(); i != e; ++i)
    for (const SDep &Succ : Packet[i]->Succs) {
      // Since we do not add pseudos to packets, might as well
      // ignore order deps.
      if (Succ.isCtrl())
        continue;

      if (Succ.getSUnit() == SU)
        return false;
    }

  return true;
}

/// Keep track of available resources.
void ResourcePriorityQueue::reserveResources(SUnit *SU) {
  // If this SU does not fit in the packet
  // start a new one.
  if (!isResourceAvailable(SU) || SU->getNode()->getGluedNode()) {
    ResourcesModel->clearResources();
    Packet.clear();
  }

  if (SU->getNode() && SU->getNode()->isMachineOpcode()) {
    switch (SU->getNode()->getMachineOpcode()) {
    default:
      ResourcesModel->reserveResources(&TII->get(
        SU->getNode()->getMachineOpcode()));
      break;
    case TargetOpcode::EXTRACT_SUBREG:
    case TargetOpcode::INSERT_SUBREG:
    case TargetOpcode::SUBREG_TO_REG:
    case TargetOpcode::REG_SEQUENCE:
    case TargetOpcode::IMPLICIT_DEF:
      break;
    }
    Packet.push_back(SU);
  }
  // Forcefully end packet for PseudoOps.
  else {
    ResourcesModel->clearResources();
    Packet.clear();
  }

  // If packet is now full, reset the state so in the next cycle
  // we start fresh.
  if (Packet.size() >= InstrItins->SchedModel.IssueWidth) {
    ResourcesModel->clearResources();
    Packet.clear();
  }
}

int ResourcePriorityQueue::rawRegPressureDelta(SUnit *SU, unsigned RCId) {
  int RegBalance = 0;

  if (!SU || !SU->getNode() || !SU->getNode()->isMachineOpcode())
    return RegBalance;

  // Gen estimate.
  for (unsigned i = 0, e = SU->getNode()->getNumValues(); i != e; ++i) {
      MVT VT = SU->getNode()->getSimpleValueType(i);
      if (TLI->isTypeLegal(VT)
          && TLI->getRegClassFor(VT)
          && TLI->getRegClassFor(VT)->getID() == RCId)
        RegBalance += numberRCValSuccInSU(SU, RCId);
  }
  // Kill estimate.
  for (unsigned i = 0, e = SU->getNode()->getNumOperands(); i != e; ++i) {
      const SDValue &Op = SU->getNode()->getOperand(i);
      MVT VT = Op.getNode()->getSimpleValueType(Op.getResNo());
      if (isa<ConstantSDNode>(Op.getNode()))
        continue;

      if (TLI->isTypeLegal(VT) && TLI->getRegClassFor(VT)
          && TLI->getRegClassFor(VT)->getID() == RCId)
        RegBalance -= numberRCValPredInSU(SU, RCId);
  }
  return RegBalance;
}

/// Estimates change in reg pressure from this SU.
/// It is achieved by trivial tracking of defined
/// and used vregs in dependent instructions.
/// The RawPressure flag makes this function to ignore
/// existing reg file sizes, and report raw def/use
/// balance.
int ResourcePriorityQueue::regPressureDelta(SUnit *SU, bool RawPressure) {
  int RegBalance = 0;

  if (!SU || !SU->getNode() || !SU->getNode()->isMachineOpcode())
    return RegBalance;

  if (RawPressure) {
    for (const TargetRegisterClass *RC : TRI->regclasses())
      RegBalance += rawRegPressureDelta(SU, RC->getID());
  }
  else {
    for (const TargetRegisterClass *RC : TRI->regclasses()) {
      if ((RegPressure[RC->getID()] +
           rawRegPressureDelta(SU, RC->getID()) > 0) &&
          (RegPressure[RC->getID()] +
           rawRegPressureDelta(SU, RC->getID())  >= RegLimit[RC->getID()]))
        RegBalance += rawRegPressureDelta(SU, RC->getID());
    }
  }

  return RegBalance;
}

// Constants used to denote relative importance of
// heuristic components for cost computation.
static const unsigned PriorityOne = 200;
static const unsigned PriorityTwo = 50;
static const unsigned PriorityThree = 15;
static const unsigned PriorityFour = 5;
static const unsigned ScaleOne = 20;
static const unsigned ScaleTwo = 10;
static const unsigned ScaleThree = 5;
static const unsigned FactorOne = 2;

/// Returns single number reflecting benefit of scheduling SU
/// in the current cycle.
int ResourcePriorityQueue::SUSchedulingCost(SUnit *SU) {
  // Initial trivial priority.
  int ResCount = 1;

  // Do not waste time on a node that is already scheduled.
  if (SU->isScheduled)
    return ResCount;

  // Forced priority is high.
  if (SU->isScheduleHigh)
    ResCount += PriorityOne;

  // Adaptable scheduling
  // A small, but very parallel
  // region, where reg pressure is an issue.
  if (HorizontalVerticalBalance > RegPressureThreshold) {
    // Critical path first
    ResCount += (SU->getHeight() * ScaleTwo);
    // If resources are available for it, multiply the
    // chance of scheduling.
    if (isResourceAvailable(SU))
      ResCount <<= FactorOne;

    // Consider change to reg pressure from scheduling
    // this SU.
    ResCount -= (regPressureDelta(SU,true) * ScaleOne);
  }
  // Default heuristic, greeady and
  // critical path driven.
  else {
    // Critical path first.
    ResCount += (SU->getHeight() * ScaleTwo);
    // Now see how many instructions is blocked by this SU.
    ResCount += (NumNodesSolelyBlocking[SU->NodeNum] * ScaleTwo);
    // If resources are available for it, multiply the
    // chance of scheduling.
    if (isResourceAvailable(SU))
      ResCount <<= FactorOne;

    ResCount -= (regPressureDelta(SU) * ScaleTwo);
  }

  // These are platform-specific things.
  // Will need to go into the back end
  // and accessed from here via a hook.
  for (SDNode *N = SU->getNode(); N; N = N->getGluedNode()) {
    if (N->isMachineOpcode()) {
      const MCInstrDesc &TID = TII->get(N->getMachineOpcode());
      if (TID.isCall())
        ResCount += (PriorityTwo + (ScaleThree*N->getNumValues()));
    }
    else
      switch (N->getOpcode()) {
      default:  break;
      case ISD::TokenFactor:
      case ISD::CopyFromReg:
      case ISD::CopyToReg:
        ResCount += PriorityFour;
        break;

      case ISD::INLINEASM:
        ResCount += PriorityThree;
        break;
      }
  }
  return ResCount;
}


/// Main resource tracking point.
void ResourcePriorityQueue::scheduledNode(SUnit *SU) {
  // Use NULL entry as an event marker to reset
  // the DFA state.
  if (!SU) {
    ResourcesModel->clearResources();
    Packet.clear();
    return;
  }

  const SDNode *ScegN = SU->getNode();
  // Update reg pressure tracking.
  // First update current node.
  if (ScegN->isMachineOpcode()) {
    // Estimate generated regs.
    for (unsigned i = 0, e = ScegN->getNumValues(); i != e; ++i) {
      MVT VT = ScegN->getSimpleValueType(i);

      if (TLI->isTypeLegal(VT)) {
        const TargetRegisterClass *RC = TLI->getRegClassFor(VT);
        if (RC)
          RegPressure[RC->getID()] += numberRCValSuccInSU(SU, RC->getID());
      }
    }
    // Estimate killed regs.
    for (unsigned i = 0, e = ScegN->getNumOperands(); i != e; ++i) {
      const SDValue &Op = ScegN->getOperand(i);
      MVT VT = Op.getNode()->getSimpleValueType(Op.getResNo());

      if (TLI->isTypeLegal(VT)) {
        const TargetRegisterClass *RC = TLI->getRegClassFor(VT);
        if (RC) {
          if (RegPressure[RC->getID()] >
            (numberRCValPredInSU(SU, RC->getID())))
            RegPressure[RC->getID()] -= numberRCValPredInSU(SU, RC->getID());
          else RegPressure[RC->getID()] = 0;
        }
      }
    }
    for (SDep &Pred : SU->Preds) {
      if (Pred.isCtrl() || (Pred.getSUnit()->NumRegDefsLeft == 0))
        continue;
      --Pred.getSUnit()->NumRegDefsLeft;
    }
  }

  // Reserve resources for this SU.
  reserveResources(SU);

  // Adjust number of parallel live ranges.
  // Heuristic is simple - node with no data successors reduces
  // number of live ranges. All others, increase it.
  unsigned NumberNonControlDeps = 0;

  for (const SDep &Succ : SU->Succs) {
    adjustPriorityOfUnscheduledPreds(Succ.getSUnit());
    if (!Succ.isCtrl())
      NumberNonControlDeps++;
  }

  if (!NumberNonControlDeps) {
    if (ParallelLiveRanges >= SU->NumPreds)
      ParallelLiveRanges -= SU->NumPreds;
    else
      ParallelLiveRanges = 0;

  }
  else
    ParallelLiveRanges += SU->NumRegDefsLeft;

  // Track parallel live chains.
  HorizontalVerticalBalance += (SU->Succs.size() - numberCtrlDepsInSU(SU));
  HorizontalVerticalBalance -= (SU->Preds.size() - numberCtrlPredInSU(SU));
}

void ResourcePriorityQueue::initNumRegDefsLeft(SUnit *SU) {
  unsigned  NodeNumDefs = 0;
  for (SDNode *N = SU->getNode(); N; N = N->getGluedNode())
    if (N->isMachineOpcode()) {
      const MCInstrDesc &TID = TII->get(N->getMachineOpcode());
      // No register need be allocated for this.
      if (N->getMachineOpcode() == TargetOpcode::IMPLICIT_DEF) {
        NodeNumDefs = 0;
        break;
      }
      NodeNumDefs = std::min(N->getNumValues(), TID.getNumDefs());
    }
    else
      switch(N->getOpcode()) {
        default:     break;
        case ISD::CopyFromReg:
          NodeNumDefs++;
          break;
        case ISD::INLINEASM:
          NodeNumDefs++;
          break;
      }

  SU->NumRegDefsLeft = NodeNumDefs;
}

/// adjustPriorityOfUnscheduledPreds - One of the predecessors of SU was just
/// scheduled.  If SU is not itself available, then there is at least one
/// predecessor node that has not been scheduled yet.  If SU has exactly ONE
/// unscheduled predecessor, we want to increase its priority: it getting
/// scheduled will make this node available, so it is better than some other
/// node of the same priority that will not make a node available.
void ResourcePriorityQueue::adjustPriorityOfUnscheduledPreds(SUnit *SU) {
  if (SU->isAvailable) return;  // All preds scheduled.

  SUnit *OnlyAvailablePred = getSingleUnscheduledPred(SU);
  if (!OnlyAvailablePred || !OnlyAvailablePred->isAvailable)
    return;

  // Okay, we found a single predecessor that is available, but not scheduled.
  // Since it is available, it must be in the priority queue.  First remove it.
  remove(OnlyAvailablePred);

  // Reinsert the node into the priority queue, which recomputes its
  // NumNodesSolelyBlocking value.
  push(OnlyAvailablePred);
}


/// Main access point - returns next instructions
/// to be placed in scheduling sequence.
SUnit *ResourcePriorityQueue::pop() {
  if (empty())
    return nullptr;

  std::vector<SUnit *>::iterator Best = Queue.begin();
  if (!DisableDFASched) {
    int BestCost = SUSchedulingCost(*Best);
    for (auto I = std::next(Queue.begin()), E = Queue.end(); I != E; ++I) {

      if (SUSchedulingCost(*I) > BestCost) {
        BestCost = SUSchedulingCost(*I);
        Best = I;
      }
    }
  }
  // Use default TD scheduling mechanism.
  else {
    for (auto I = std::next(Queue.begin()), E = Queue.end(); I != E; ++I)
      if (Picker(*Best, *I))
        Best = I;
  }

  SUnit *V = *Best;
  if (Best != std::prev(Queue.end()))
    std::swap(*Best, Queue.back());

  Queue.pop_back();

  return V;
}


void ResourcePriorityQueue::remove(SUnit *SU) {
  assert(!Queue.empty() && "Queue is empty!");
  std::vector<SUnit *>::iterator I = find(Queue, SU);
  if (I != std::prev(Queue.end()))
    std::swap(*I, Queue.back());

  Queue.pop_back();
}
