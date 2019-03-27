//===--------------------- Instruction.cpp ----------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file defines abstractions used by the Pipeline to model register reads,
// register writes and instructions.
//
//===----------------------------------------------------------------------===//

#include "llvm/MCA/Instruction.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/raw_ostream.h"

namespace llvm {
namespace mca {

void ReadState::writeStartEvent(unsigned Cycles) {
  assert(DependentWrites);
  assert(CyclesLeft == UNKNOWN_CYCLES);

  // This read may be dependent on more than one write. This typically occurs
  // when a definition is the result of multiple writes where at least one
  // write does a partial register update.
  // The HW is forced to do some extra bookkeeping to track of all the
  // dependent writes, and implement a merging scheme for the partial writes.
  --DependentWrites;
  TotalCycles = std::max(TotalCycles, Cycles);

  if (!DependentWrites) {
    CyclesLeft = TotalCycles;
    IsReady = !CyclesLeft;
  }
}

void WriteState::onInstructionIssued() {
  assert(CyclesLeft == UNKNOWN_CYCLES);
  // Update the number of cycles left based on the WriteDescriptor info.
  CyclesLeft = getLatency();

  // Now that the time left before write-back is known, notify
  // all the users.
  for (const std::pair<ReadState *, int> &User : Users) {
    ReadState *RS = User.first;
    unsigned ReadCycles = std::max(0, CyclesLeft - User.second);
    RS->writeStartEvent(ReadCycles);
  }

  // Notify any writes that are in a false dependency with this write.
  if (PartialWrite)
    PartialWrite->writeStartEvent(CyclesLeft);
}

void WriteState::addUser(ReadState *User, int ReadAdvance) {
  // If CyclesLeft is different than -1, then we don't need to
  // update the list of users. We can just notify the user with
  // the actual number of cycles left (which may be zero).
  if (CyclesLeft != UNKNOWN_CYCLES) {
    unsigned ReadCycles = std::max(0, CyclesLeft - ReadAdvance);
    User->writeStartEvent(ReadCycles);
    return;
  }

  if (llvm::find_if(Users, [&User](const std::pair<ReadState *, int> &Use) {
        return Use.first == User;
      }) == Users.end()) {
    Users.emplace_back(User, ReadAdvance);
  }
}

void WriteState::addUser(WriteState *User) {
  if (CyclesLeft != UNKNOWN_CYCLES) {
    User->writeStartEvent(std::max(0, CyclesLeft));
    return;
  }

  assert(!PartialWrite && "PartialWrite already set!");
  PartialWrite = User;
  User->setDependentWrite(this);
}

void WriteState::cycleEvent() {
  // Note: CyclesLeft can be a negative number. It is an error to
  // make it an unsigned quantity because users of this write may
  // specify a negative ReadAdvance.
  if (CyclesLeft != UNKNOWN_CYCLES)
    CyclesLeft--;

  if (DependentWriteCyclesLeft)
    DependentWriteCyclesLeft--;
}

void ReadState::cycleEvent() {
  // Update the total number of cycles.
  if (DependentWrites && TotalCycles) {
    --TotalCycles;
    return;
  }

  // Bail out immediately if we don't know how many cycles are left.
  if (CyclesLeft == UNKNOWN_CYCLES)
    return;

  if (CyclesLeft) {
    --CyclesLeft;
    IsReady = !CyclesLeft;
  }
}

#ifndef NDEBUG
void WriteState::dump() const {
  dbgs() << "{ OpIdx=" << WD->OpIndex << ", Lat=" << getLatency() << ", RegID "
         << getRegisterID() << ", Cycles Left=" << getCyclesLeft() << " }";
}

void WriteRef::dump() const {
  dbgs() << "IID=" << getSourceIndex() << ' ';
  if (isValid())
    getWriteState()->dump();
  else
    dbgs() << "(null)";
}
#endif

void Instruction::dispatch(unsigned RCUToken) {
  assert(Stage == IS_INVALID);
  Stage = IS_AVAILABLE;
  RCUTokenID = RCUToken;

  // Check if input operands are already available.
  update();
}

void Instruction::execute() {
  assert(Stage == IS_READY);
  Stage = IS_EXECUTING;

  // Set the cycles left before the write-back stage.
  CyclesLeft = getLatency();

  for (WriteState &WS : getDefs())
    WS.onInstructionIssued();

  // Transition to the "executed" stage if this is a zero-latency instruction.
  if (!CyclesLeft)
    Stage = IS_EXECUTED;
}

void Instruction::forceExecuted() {
  assert(Stage == IS_READY && "Invalid internal state!");
  CyclesLeft = 0;
  Stage = IS_EXECUTED;
}

void Instruction::update() {
  assert(isDispatched() && "Unexpected instruction stage found!");

  if (!all_of(getUses(), [](const ReadState &Use) { return Use.isReady(); }))
    return;

  // A partial register write cannot complete before a dependent write.
  auto IsDefReady = [&](const WriteState &Def) {
    if (!Def.getDependentWrite()) {
      unsigned CyclesLeft = Def.getDependentWriteCyclesLeft();
      return !CyclesLeft || CyclesLeft < getLatency();
    }
    return false;
  };

  if (all_of(getDefs(), IsDefReady))
    Stage = IS_READY;
}

void Instruction::cycleEvent() {
  if (isReady())
    return;

  if (isDispatched()) {
    for (ReadState &Use : getUses())
      Use.cycleEvent();

    for (WriteState &Def : getDefs())
      Def.cycleEvent();

    update();
    return;
  }

  assert(isExecuting() && "Instruction not in-flight?");
  assert(CyclesLeft && "Instruction already executed?");
  for (WriteState &Def : getDefs())
    Def.cycleEvent();
  CyclesLeft--;
  if (!CyclesLeft)
    Stage = IS_EXECUTED;
}

const unsigned WriteRef::INVALID_IID = std::numeric_limits<unsigned>::max();

} // namespace mca
} // namespace llvm
