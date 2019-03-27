//===- BlockVerifier.cpp - FDR Block Verifier -----------------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
#include "llvm/XRay/BlockVerifier.h"
#include "llvm/Support/Error.h"

namespace llvm {
namespace xray {
namespace {

constexpr unsigned long long mask(BlockVerifier::State S) {
  return 1uLL << static_cast<std::size_t>(S);
}

constexpr std::size_t number(BlockVerifier::State S) {
  return static_cast<std::size_t>(S);
}

StringRef recordToString(BlockVerifier::State R) {
  switch (R) {
  case BlockVerifier::State::BufferExtents:
    return "BufferExtents";
  case BlockVerifier::State::NewBuffer:
    return "NewBuffer";
  case BlockVerifier::State::WallClockTime:
    return "WallClockTime";
  case BlockVerifier::State::PIDEntry:
    return "PIDEntry";
  case BlockVerifier::State::NewCPUId:
    return "NewCPUId";
  case BlockVerifier::State::TSCWrap:
    return "TSCWrap";
  case BlockVerifier::State::CustomEvent:
    return "CustomEvent";
  case BlockVerifier::State::Function:
    return "Function";
  case BlockVerifier::State::CallArg:
    return "CallArg";
  case BlockVerifier::State::EndOfBuffer:
    return "EndOfBuffer";
  case BlockVerifier::State::TypedEvent:
    return "TypedEvent";
  case BlockVerifier::State::StateMax:
  case BlockVerifier::State::Unknown:
    return "Unknown";
  }
  llvm_unreachable("Unkown state!");
}

struct Transition {
  BlockVerifier::State From;
  std::bitset<number(BlockVerifier::State::StateMax)> ToStates;
};

} // namespace

Error BlockVerifier::transition(State To) {
  using ToSet = std::bitset<number(State::StateMax)>;
  static constexpr std::array<const Transition, number(State::StateMax)>
      TransitionTable{{{State::Unknown,
                        {mask(State::BufferExtents) | mask(State::NewBuffer)}},

                       {State::BufferExtents, {mask(State::NewBuffer)}},

                       {State::NewBuffer, {mask(State::WallClockTime)}},

                       {State::WallClockTime,
                        {mask(State::PIDEntry) | mask(State::NewCPUId)}},

                       {State::PIDEntry, {mask(State::NewCPUId)}},

                       {State::NewCPUId,
                        {mask(State::NewCPUId) | mask(State::TSCWrap) |
                         mask(State::CustomEvent) | mask(State::Function) |
                         mask(State::EndOfBuffer) | mask(State::TypedEvent)}},

                       {State::TSCWrap,
                        {mask(State::TSCWrap) | mask(State::NewCPUId) |
                         mask(State::CustomEvent) | mask(State::Function) |
                         mask(State::EndOfBuffer) | mask(State::TypedEvent)}},

                       {State::CustomEvent,
                        {mask(State::CustomEvent) | mask(State::TSCWrap) |
                         mask(State::NewCPUId) | mask(State::Function) |
                         mask(State::EndOfBuffer) | mask(State::TypedEvent)}},

                       {State::TypedEvent,
                        {mask(State::TypedEvent) | mask(State::TSCWrap) |
                         mask(State::NewCPUId) | mask(State::Function) |
                         mask(State::EndOfBuffer) | mask(State::CustomEvent)}},

                       {State::Function,
                        {mask(State::Function) | mask(State::TSCWrap) |
                         mask(State::NewCPUId) | mask(State::CustomEvent) |
                         mask(State::CallArg) | mask(State::EndOfBuffer) |
                         mask(State::TypedEvent)}},

                       {State::CallArg,
                        {mask(State::CallArg) | mask(State::Function) |
                         mask(State::TSCWrap) | mask(State::NewCPUId) |
                         mask(State::CustomEvent) | mask(State::EndOfBuffer) |
                         mask(State::TypedEvent)}},

                       {State::EndOfBuffer, {}}}};

  if (CurrentRecord >= State::StateMax)
    return createStringError(
        std::make_error_code(std::errc::executable_format_error),
        "BUG (BlockVerifier): Cannot find transition table entry for %s, "
        "transitioning to %s.",
        recordToString(CurrentRecord).data(), recordToString(To).data());

  // If we're at an EndOfBuffer record, we ignore anything that follows that
  // isn't a NewBuffer record.
  if (CurrentRecord == State::EndOfBuffer && To != State::NewBuffer)
    return Error::success();

  auto &Mapping = TransitionTable[number(CurrentRecord)];
  auto &Destinations = Mapping.ToStates;
  assert(Mapping.From == CurrentRecord &&
         "BUG: Wrong index for record mapping.");
  if ((Destinations & ToSet(mask(To))) == 0)
    return createStringError(
        std::make_error_code(std::errc::executable_format_error),
        "BlockVerifier: Invalid transition from %s to %s.",
        recordToString(CurrentRecord).data(), recordToString(To).data());

  CurrentRecord = To;
  return Error::success();
} // namespace xray

Error BlockVerifier::visit(BufferExtents &) {
  return transition(State::BufferExtents);
}

Error BlockVerifier::visit(WallclockRecord &) {
  return transition(State::WallClockTime);
}

Error BlockVerifier::visit(NewCPUIDRecord &) {
  return transition(State::NewCPUId);
}

Error BlockVerifier::visit(TSCWrapRecord &) {
  return transition(State::TSCWrap);
}

Error BlockVerifier::visit(CustomEventRecord &) {
  return transition(State::CustomEvent);
}

Error BlockVerifier::visit(CustomEventRecordV5 &) {
  return transition(State::CustomEvent);
}

Error BlockVerifier::visit(TypedEventRecord &) {
  return transition(State::TypedEvent);
}

Error BlockVerifier::visit(CallArgRecord &) {
  return transition(State::CallArg);
}

Error BlockVerifier::visit(PIDRecord &) { return transition(State::PIDEntry); }

Error BlockVerifier::visit(NewBufferRecord &) {
  return transition(State::NewBuffer);
}

Error BlockVerifier::visit(EndBufferRecord &) {
  return transition(State::EndOfBuffer);
}

Error BlockVerifier::visit(FunctionRecord &) {
  return transition(State::Function);
}

Error BlockVerifier::verify() {
  // The known terminal conditions are the following:
  switch (CurrentRecord) {
  case State::EndOfBuffer:
  case State::NewCPUId:
  case State::CustomEvent:
  case State::TypedEvent:
  case State::Function:
  case State::CallArg:
  case State::TSCWrap:
    return Error::success();
  default:
    return createStringError(
        std::make_error_code(std::errc::executable_format_error),
        "BlockVerifier: Invalid terminal condition %s, malformed block.",
        recordToString(CurrentRecord).data());
  }
}

void BlockVerifier::reset() { CurrentRecord = State::Unknown; }

} // namespace xray
} // namespace llvm
