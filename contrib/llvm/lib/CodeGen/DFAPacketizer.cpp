//=- llvm/CodeGen/DFAPacketizer.cpp - DFA Packetizer for VLIW -*- C++ -*-=====//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
// This class implements a deterministic finite automaton (DFA) based
// packetizing mechanism for VLIW architectures. It provides APIs to
// determine whether there exists a legal mapping of instructions to
// functional unit assignments in a packet. The DFA is auto-generated from
// the target's Schedule.td file.
//
// A DFA consists of 3 major elements: states, inputs, and transitions. For
// the packetizing mechanism, the input is the set of instruction classes for
// a target. The state models all possible combinations of functional unit
// consumption for a given set of instructions in a packet. A transition
// models the addition of an instruction to a packet. In the DFA constructed
// by this class, if an instruction can be added to a packet, then a valid
// transition exists from the corresponding state. Invalid transitions
// indicate that the instruction cannot be added to the current packet.
//
//===----------------------------------------------------------------------===//

#include "llvm/CodeGen/DFAPacketizer.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineInstr.h"
#include "llvm/CodeGen/MachineInstrBundle.h"
#include "llvm/CodeGen/ScheduleDAG.h"
#include "llvm/CodeGen/ScheduleDAGInstrs.h"
#include "llvm/CodeGen/TargetInstrInfo.h"
#include "llvm/CodeGen/TargetSubtargetInfo.h"
#include "llvm/MC/MCInstrDesc.h"
#include "llvm/MC/MCInstrItineraries.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/raw_ostream.h"
#include <algorithm>
#include <cassert>
#include <iterator>
#include <memory>
#include <vector>

using namespace llvm;

#define DEBUG_TYPE "packets"

static cl::opt<unsigned> InstrLimit("dfa-instr-limit", cl::Hidden,
  cl::init(0), cl::desc("If present, stops packetizing after N instructions"));

static unsigned InstrCount = 0;

// --------------------------------------------------------------------
// Definitions shared between DFAPacketizer.cpp and DFAPacketizerEmitter.cpp

static DFAInput addDFAFuncUnits(DFAInput Inp, unsigned FuncUnits) {
  return (Inp << DFA_MAX_RESOURCES) | FuncUnits;
}

/// Return the DFAInput for an instruction class input vector.
/// This function is used in both DFAPacketizer.cpp and in
/// DFAPacketizerEmitter.cpp.
static DFAInput getDFAInsnInput(const std::vector<unsigned> &InsnClass) {
  DFAInput InsnInput = 0;
  assert((InsnClass.size() <= DFA_MAX_RESTERMS) &&
         "Exceeded maximum number of DFA terms");
  for (auto U : InsnClass)
    InsnInput = addDFAFuncUnits(InsnInput, U);
  return InsnInput;
}

// --------------------------------------------------------------------

DFAPacketizer::DFAPacketizer(const InstrItineraryData *I,
                             const DFAStateInput (*SIT)[2],
                             const unsigned *SET):
  InstrItins(I), DFAStateInputTable(SIT), DFAStateEntryTable(SET) {
  // Make sure DFA types are large enough for the number of terms & resources.
  static_assert((DFA_MAX_RESTERMS * DFA_MAX_RESOURCES) <=
                    (8 * sizeof(DFAInput)),
                "(DFA_MAX_RESTERMS * DFA_MAX_RESOURCES) too big for DFAInput");
  static_assert(
      (DFA_MAX_RESTERMS * DFA_MAX_RESOURCES) <= (8 * sizeof(DFAStateInput)),
      "(DFA_MAX_RESTERMS * DFA_MAX_RESOURCES) too big for DFAStateInput");
}

// Read the DFA transition table and update CachedTable.
//
// Format of the transition tables:
// DFAStateInputTable[][2] = pairs of <Input, Transition> for all valid
//                           transitions
// DFAStateEntryTable[i] = Index of the first entry in DFAStateInputTable
//                         for the ith state
//
void DFAPacketizer::ReadTable(unsigned int state) {
  unsigned ThisState = DFAStateEntryTable[state];
  unsigned NextStateInTable = DFAStateEntryTable[state+1];
  // Early exit in case CachedTable has already contains this
  // state's transitions.
  if (CachedTable.count(UnsignPair(state, DFAStateInputTable[ThisState][0])))
    return;

  for (unsigned i = ThisState; i < NextStateInTable; i++)
    CachedTable[UnsignPair(state, DFAStateInputTable[i][0])] =
      DFAStateInputTable[i][1];
}

// Return the DFAInput for an instruction class.
DFAInput DFAPacketizer::getInsnInput(unsigned InsnClass) {
  // Note: this logic must match that in DFAPacketizerDefs.h for input vectors.
  DFAInput InsnInput = 0;
  unsigned i = 0;
  (void)i;
  for (const InstrStage *IS = InstrItins->beginStage(InsnClass),
       *IE = InstrItins->endStage(InsnClass); IS != IE; ++IS) {
    InsnInput = addDFAFuncUnits(InsnInput, IS->getUnits());
    assert((i++ < DFA_MAX_RESTERMS) && "Exceeded maximum number of DFA inputs");
  }
  return InsnInput;
}

// Return the DFAInput for an instruction class input vector.
DFAInput DFAPacketizer::getInsnInput(const std::vector<unsigned> &InsnClass) {
  return getDFAInsnInput(InsnClass);
}

// Check if the resources occupied by a MCInstrDesc are available in the
// current state.
bool DFAPacketizer::canReserveResources(const MCInstrDesc *MID) {
  unsigned InsnClass = MID->getSchedClass();
  DFAInput InsnInput = getInsnInput(InsnClass);
  UnsignPair StateTrans = UnsignPair(CurrentState, InsnInput);
  ReadTable(CurrentState);
  return CachedTable.count(StateTrans) != 0;
}

// Reserve the resources occupied by a MCInstrDesc and change the current
// state to reflect that change.
void DFAPacketizer::reserveResources(const MCInstrDesc *MID) {
  unsigned InsnClass = MID->getSchedClass();
  DFAInput InsnInput = getInsnInput(InsnClass);
  UnsignPair StateTrans = UnsignPair(CurrentState, InsnInput);
  ReadTable(CurrentState);
  assert(CachedTable.count(StateTrans) != 0);
  CurrentState = CachedTable[StateTrans];
}

// Check if the resources occupied by a machine instruction are available
// in the current state.
bool DFAPacketizer::canReserveResources(MachineInstr &MI) {
  const MCInstrDesc &MID = MI.getDesc();
  return canReserveResources(&MID);
}

// Reserve the resources occupied by a machine instruction and change the
// current state to reflect that change.
void DFAPacketizer::reserveResources(MachineInstr &MI) {
  const MCInstrDesc &MID = MI.getDesc();
  reserveResources(&MID);
}

namespace llvm {

// This class extends ScheduleDAGInstrs and overrides the schedule method
// to build the dependence graph.
class DefaultVLIWScheduler : public ScheduleDAGInstrs {
private:
  AliasAnalysis *AA;
  /// Ordered list of DAG postprocessing steps.
  std::vector<std::unique_ptr<ScheduleDAGMutation>> Mutations;

public:
  DefaultVLIWScheduler(MachineFunction &MF, MachineLoopInfo &MLI,
                       AliasAnalysis *AA);

  // Actual scheduling work.
  void schedule() override;

  /// DefaultVLIWScheduler takes ownership of the Mutation object.
  void addMutation(std::unique_ptr<ScheduleDAGMutation> Mutation) {
    Mutations.push_back(std::move(Mutation));
  }

protected:
  void postprocessDAG();
};

} // end namespace llvm

DefaultVLIWScheduler::DefaultVLIWScheduler(MachineFunction &MF,
                                           MachineLoopInfo &MLI,
                                           AliasAnalysis *AA)
    : ScheduleDAGInstrs(MF, &MLI), AA(AA) {
  CanHandleTerminators = true;
}

/// Apply each ScheduleDAGMutation step in order.
void DefaultVLIWScheduler::postprocessDAG() {
  for (auto &M : Mutations)
    M->apply(this);
}

void DefaultVLIWScheduler::schedule() {
  // Build the scheduling graph.
  buildSchedGraph(AA);
  postprocessDAG();
}

VLIWPacketizerList::VLIWPacketizerList(MachineFunction &mf,
                                       MachineLoopInfo &mli, AliasAnalysis *aa)
    : MF(mf), TII(mf.getSubtarget().getInstrInfo()), AA(aa) {
  ResourceTracker = TII->CreateTargetScheduleState(MF.getSubtarget());
  VLIWScheduler = new DefaultVLIWScheduler(MF, mli, AA);
}

VLIWPacketizerList::~VLIWPacketizerList() {
  delete VLIWScheduler;
  delete ResourceTracker;
}

// End the current packet, bundle packet instructions and reset DFA state.
void VLIWPacketizerList::endPacket(MachineBasicBlock *MBB,
                                   MachineBasicBlock::iterator MI) {
  LLVM_DEBUG({
    if (!CurrentPacketMIs.empty()) {
      dbgs() << "Finalizing packet:\n";
      for (MachineInstr *MI : CurrentPacketMIs)
        dbgs() << " * " << *MI;
    }
  });
  if (CurrentPacketMIs.size() > 1) {
    MachineInstr &MIFirst = *CurrentPacketMIs.front();
    finalizeBundle(*MBB, MIFirst.getIterator(), MI.getInstrIterator());
  }
  CurrentPacketMIs.clear();
  ResourceTracker->clearResources();
  LLVM_DEBUG(dbgs() << "End packet\n");
}

// Bundle machine instructions into packets.
void VLIWPacketizerList::PacketizeMIs(MachineBasicBlock *MBB,
                                      MachineBasicBlock::iterator BeginItr,
                                      MachineBasicBlock::iterator EndItr) {
  assert(VLIWScheduler && "VLIW Scheduler is not initialized!");
  VLIWScheduler->startBlock(MBB);
  VLIWScheduler->enterRegion(MBB, BeginItr, EndItr,
                             std::distance(BeginItr, EndItr));
  VLIWScheduler->schedule();

  LLVM_DEBUG({
    dbgs() << "Scheduling DAG of the packetize region\n";
    VLIWScheduler->dump();
  });

  // Generate MI -> SU map.
  MIToSUnit.clear();
  for (SUnit &SU : VLIWScheduler->SUnits)
    MIToSUnit[SU.getInstr()] = &SU;

  bool LimitPresent = InstrLimit.getPosition();

  // The main packetizer loop.
  for (; BeginItr != EndItr; ++BeginItr) {
    if (LimitPresent) {
      if (InstrCount >= InstrLimit) {
        EndItr = BeginItr;
        break;
      }
      InstrCount++;
    }
    MachineInstr &MI = *BeginItr;
    initPacketizerState();

    // End the current packet if needed.
    if (isSoloInstruction(MI)) {
      endPacket(MBB, MI);
      continue;
    }

    // Ignore pseudo instructions.
    if (ignorePseudoInstruction(MI, MBB))
      continue;

    SUnit *SUI = MIToSUnit[&MI];
    assert(SUI && "Missing SUnit Info!");

    // Ask DFA if machine resource is available for MI.
    LLVM_DEBUG(dbgs() << "Checking resources for adding MI to packet " << MI);

    bool ResourceAvail = ResourceTracker->canReserveResources(MI);
    LLVM_DEBUG({
      if (ResourceAvail)
        dbgs() << "  Resources are available for adding MI to packet\n";
      else
        dbgs() << "  Resources NOT available\n";
    });
    if (ResourceAvail && shouldAddToPacket(MI)) {
      // Dependency check for MI with instructions in CurrentPacketMIs.
      for (auto MJ : CurrentPacketMIs) {
        SUnit *SUJ = MIToSUnit[MJ];
        assert(SUJ && "Missing SUnit Info!");

        LLVM_DEBUG(dbgs() << "  Checking against MJ " << *MJ);
        // Is it legal to packetize SUI and SUJ together.
        if (!isLegalToPacketizeTogether(SUI, SUJ)) {
          LLVM_DEBUG(dbgs() << "  Not legal to add MI, try to prune\n");
          // Allow packetization if dependency can be pruned.
          if (!isLegalToPruneDependencies(SUI, SUJ)) {
            // End the packet if dependency cannot be pruned.
            LLVM_DEBUG(dbgs()
                       << "  Could not prune dependencies for adding MI\n");
            endPacket(MBB, MI);
            break;
          }
          LLVM_DEBUG(dbgs() << "  Pruned dependence for adding MI\n");
        }
      }
    } else {
      LLVM_DEBUG(if (ResourceAvail) dbgs()
                 << "Resources are available, but instruction should not be "
                    "added to packet\n  "
                 << MI);
      // End the packet if resource is not available, or if the instruction
      // shoud not be added to the current packet.
      endPacket(MBB, MI);
    }

    // Add MI to the current packet.
    LLVM_DEBUG(dbgs() << "* Adding MI to packet " << MI << '\n');
    BeginItr = addToPacket(MI);
  } // For all instructions in the packetization range.

  // End any packet left behind.
  endPacket(MBB, EndItr);
  VLIWScheduler->exitRegion();
  VLIWScheduler->finishBlock();
}

bool VLIWPacketizerList::alias(const MachineMemOperand &Op1,
                               const MachineMemOperand &Op2,
                               bool UseTBAA) const {
  if (!Op1.getValue() || !Op2.getValue())
    return true;

  int64_t MinOffset = std::min(Op1.getOffset(), Op2.getOffset());
  int64_t Overlapa = Op1.getSize() + Op1.getOffset() - MinOffset;
  int64_t Overlapb = Op2.getSize() + Op2.getOffset() - MinOffset;

  AliasResult AAResult =
      AA->alias(MemoryLocation(Op1.getValue(), Overlapa,
                               UseTBAA ? Op1.getAAInfo() : AAMDNodes()),
                MemoryLocation(Op2.getValue(), Overlapb,
                               UseTBAA ? Op2.getAAInfo() : AAMDNodes()));

  return AAResult != NoAlias;
}

bool VLIWPacketizerList::alias(const MachineInstr &MI1,
                               const MachineInstr &MI2,
                               bool UseTBAA) const {
  if (MI1.memoperands_empty() || MI2.memoperands_empty())
    return true;

  for (const MachineMemOperand *Op1 : MI1.memoperands())
    for (const MachineMemOperand *Op2 : MI2.memoperands())
      if (alias(*Op1, *Op2, UseTBAA))
        return true;
  return false;
}

// Add a DAG mutation object to the ordered list.
void VLIWPacketizerList::addMutation(
      std::unique_ptr<ScheduleDAGMutation> Mutation) {
  VLIWScheduler->addMutation(std::move(Mutation));
}
