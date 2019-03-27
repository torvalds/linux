//===- BlockVerifier.h - FDR Block Verifier -------------------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// An implementation of the RecordVisitor which verifies a sequence of records
// associated with a block, following the FDR mode log format's specifications.
//
//===----------------------------------------------------------------------===//
#ifndef LLVM_INCLUDE_LLVM_XRAY_BLOCKVERIFIER_H_
#define LLVM_INCLUDE_LLVM_XRAY_BLOCKVERIFIER_H_

#include "llvm/XRay/FDRRecords.h"
#include <array>
#include <bitset>

namespace llvm {
namespace xray {

class BlockVerifier : public RecordVisitor {
public:
  // We force State elements to be size_t, to be used as indices for containers.
  enum class State : std::size_t {
    Unknown,
    BufferExtents,
    NewBuffer,
    WallClockTime,
    PIDEntry,
    NewCPUId,
    TSCWrap,
    CustomEvent,
    TypedEvent,
    Function,
    CallArg,
    EndOfBuffer,
    StateMax,
  };

private:
  // We keep track of the current record seen by the verifier.
  State CurrentRecord = State::Unknown;

  // Transitions the current record to the new record, records an error on
  // invalid transitions.
  Error transition(State To);

public:
  Error visit(BufferExtents &) override;
  Error visit(WallclockRecord &) override;
  Error visit(NewCPUIDRecord &) override;
  Error visit(TSCWrapRecord &) override;
  Error visit(CustomEventRecord &) override;
  Error visit(CallArgRecord &) override;
  Error visit(PIDRecord &) override;
  Error visit(NewBufferRecord &) override;
  Error visit(EndBufferRecord &) override;
  Error visit(FunctionRecord &) override;
  Error visit(CustomEventRecordV5 &) override;
  Error visit(TypedEventRecord &) override;

  Error verify();
  void reset();
};

} // namespace xray
} // namespace llvm

#endif // LLVM_INCLUDE_LLVM_XRAY_BLOCKVERIFIER_H_
