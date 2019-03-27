//===- BlockPrinter.h - FDR Block Pretty Printer -------------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// An implementation of the RecordVisitor which formats a block of records for
// easier human consumption.
//
//===----------------------------------------------------------------------===//
#ifndef LLVM_INCLUDE_LLVM_XRAY_BLOCKPRINTER_H_
#define LLVM_INCLUDE_LLVM_XRAY_BLOCKPRINTER_H_

#include "llvm/Support/raw_ostream.h"
#include "llvm/XRay/FDRRecords.h"
#include "llvm/XRay/RecordPrinter.h"

namespace llvm {
namespace xray {

class BlockPrinter : public RecordVisitor {
  enum class State {
    Start,
    Preamble,
    Metadata,
    Function,
    Arg,
    CustomEvent,
    End,
  };

  raw_ostream &OS;
  RecordPrinter &RP;
  State CurrentState = State::Start;

public:
  explicit BlockPrinter(raw_ostream &O, RecordPrinter &P)
      : RecordVisitor(), OS(O), RP(P) {}

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

  void reset() { CurrentState = State::Start; }
};

} // namespace xray
} // namespace llvm

#endif // LLVM_INCLUDE_LLVM_XRAY_BLOCKPRINTER_H_
