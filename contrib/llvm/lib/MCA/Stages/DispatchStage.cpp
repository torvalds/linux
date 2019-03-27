//===--------------------- DispatchStage.cpp --------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
/// \file
///
/// This file models the dispatch component of an instruction pipeline.
///
/// The DispatchStage is responsible for updating instruction dependencies
/// and communicating to the simulated instruction scheduler that an instruction
/// is ready to be scheduled for execution.
///
//===----------------------------------------------------------------------===//

#include "llvm/MCA/Stages/DispatchStage.h"
#include "llvm/MCA/HWEventListener.h"
#include "llvm/MCA/HardwareUnits/Scheduler.h"
#include "llvm/Support/Debug.h"

#define DEBUG_TYPE "llvm-mca"

namespace llvm {
namespace mca {

void DispatchStage::notifyInstructionDispatched(const InstRef &IR,
                                                ArrayRef<unsigned> UsedRegs,
                                                unsigned UOps) const {
  LLVM_DEBUG(dbgs() << "[E] Instruction Dispatched: #" << IR << '\n');
  notifyEvent<HWInstructionEvent>(
      HWInstructionDispatchedEvent(IR, UsedRegs, UOps));
}

bool DispatchStage::checkPRF(const InstRef &IR) const {
  SmallVector<unsigned, 4> RegDefs;
  for (const WriteState &RegDef : IR.getInstruction()->getDefs())
    RegDefs.emplace_back(RegDef.getRegisterID());

  const unsigned RegisterMask = PRF.isAvailable(RegDefs);
  // A mask with all zeroes means: register files are available.
  if (RegisterMask) {
    notifyEvent<HWStallEvent>(
        HWStallEvent(HWStallEvent::RegisterFileStall, IR));
    return false;
  }

  return true;
}

bool DispatchStage::checkRCU(const InstRef &IR) const {
  const unsigned NumMicroOps = IR.getInstruction()->getDesc().NumMicroOps;
  if (RCU.isAvailable(NumMicroOps))
    return true;
  notifyEvent<HWStallEvent>(
      HWStallEvent(HWStallEvent::RetireControlUnitStall, IR));
  return false;
}

bool DispatchStage::canDispatch(const InstRef &IR) const {
  return checkRCU(IR) && checkPRF(IR) && checkNextStage(IR);
}

void DispatchStage::updateRAWDependencies(ReadState &RS,
                                          const MCSubtargetInfo &STI) {
  SmallVector<WriteRef, 4> DependentWrites;

  // Collect all the dependent writes, and update RS internal state.
  PRF.addRegisterRead(RS, DependentWrites);

  // We know that this read depends on all the writes in DependentWrites.
  // For each write, check if we have ReadAdvance information, and use it
  // to figure out in how many cycles this read becomes available.
  const ReadDescriptor &RD = RS.getDescriptor();
  const MCSchedModel &SM = STI.getSchedModel();
  const MCSchedClassDesc *SC = SM.getSchedClassDesc(RD.SchedClassID);
  for (WriteRef &WR : DependentWrites) {
    WriteState &WS = *WR.getWriteState();
    unsigned WriteResID = WS.getWriteResourceID();
    int ReadAdvance = STI.getReadAdvanceCycles(SC, RD.UseIndex, WriteResID);
    WS.addUser(&RS, ReadAdvance);
  }
}

Error DispatchStage::dispatch(InstRef IR) {
  assert(!CarryOver && "Cannot dispatch another instruction!");
  Instruction &IS = *IR.getInstruction();
  const InstrDesc &Desc = IS.getDesc();
  const unsigned NumMicroOps = Desc.NumMicroOps;
  if (NumMicroOps > DispatchWidth) {
    assert(AvailableEntries == DispatchWidth);
    AvailableEntries = 0;
    CarryOver = NumMicroOps - DispatchWidth;
    CarriedOver = IR;
  } else {
    assert(AvailableEntries >= NumMicroOps);
    AvailableEntries -= NumMicroOps;
  }

  // Check if this instructions ends the dispatch group.
  if (Desc.EndGroup)
    AvailableEntries = 0;

  // Check if this is an optimizable reg-reg move.
  bool IsEliminated = false;
  if (IS.isOptimizableMove()) {
    assert(IS.getDefs().size() == 1 && "Expected a single input!");
    assert(IS.getUses().size() == 1 && "Expected a single output!");
    IsEliminated = PRF.tryEliminateMove(IS.getDefs()[0], IS.getUses()[0]);
  }

  // A dependency-breaking instruction doesn't have to wait on the register
  // input operands, and it is often optimized at register renaming stage.
  // Update RAW dependencies if this instruction is not a dependency-breaking
  // instruction. A dependency-breaking instruction is a zero-latency
  // instruction that doesn't consume hardware resources.
  // An example of dependency-breaking instruction on X86 is a zero-idiom XOR.
  //
  // We also don't update data dependencies for instructions that have been
  // eliminated at register renaming stage.
  if (!IsEliminated) {
    for (ReadState &RS : IS.getUses())
      updateRAWDependencies(RS, STI);
  }

  // By default, a dependency-breaking zero-idiom is expected to be optimized
  // at register renaming stage. That means, no physical register is allocated
  // to the instruction.
  SmallVector<unsigned, 4> RegisterFiles(PRF.getNumRegisterFiles());
  for (WriteState &WS : IS.getDefs())
    PRF.addRegisterWrite(WriteRef(IR.getSourceIndex(), &WS), RegisterFiles);

  // Reserve slots in the RCU, and notify the instruction that it has been
  // dispatched to the schedulers for execution.
  IS.dispatch(RCU.reserveSlot(IR, NumMicroOps));

  // Notify listeners of the "instruction dispatched" event,
  // and move IR to the next stage.
  notifyInstructionDispatched(IR, RegisterFiles,
                              std::min(DispatchWidth, NumMicroOps));
  return moveToTheNextStage(IR);
}

Error DispatchStage::cycleStart() {
  PRF.cycleStart();

  if (!CarryOver) {
    AvailableEntries = DispatchWidth;
    return ErrorSuccess();
  }

  AvailableEntries = CarryOver >= DispatchWidth ? 0 : DispatchWidth - CarryOver;
  unsigned DispatchedOpcodes = DispatchWidth - AvailableEntries;
  CarryOver -= DispatchedOpcodes;
  assert(CarriedOver && "Invalid dispatched instruction");

  SmallVector<unsigned, 8> RegisterFiles(PRF.getNumRegisterFiles(), 0U);
  notifyInstructionDispatched(CarriedOver, RegisterFiles, DispatchedOpcodes);
  if (!CarryOver)
    CarriedOver = InstRef();
  return ErrorSuccess();
}

bool DispatchStage::isAvailable(const InstRef &IR) const {
  const InstrDesc &Desc = IR.getInstruction()->getDesc();
  unsigned Required = std::min(Desc.NumMicroOps, DispatchWidth);
  if (Required > AvailableEntries)
    return false;

  if (Desc.BeginGroup && AvailableEntries != DispatchWidth)
    return false;

  // The dispatch logic doesn't internally buffer instructions.  It only accepts
  // instructions that can be successfully moved to the next stage during this
  // same cycle.
  return canDispatch(IR);
}

Error DispatchStage::execute(InstRef &IR) {
  assert(canDispatch(IR) && "Cannot dispatch another instruction!");
  return dispatch(IR);
}

#ifndef NDEBUG
void DispatchStage::dump() const {
  PRF.dump();
  RCU.dump();
}
#endif
} // namespace mca
} // namespace llvm
