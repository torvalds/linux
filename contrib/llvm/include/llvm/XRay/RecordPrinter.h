//===- RecordPrinter.h - FDR Record Printer -------------------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// An implementation of the RecordVisitor which prints an individual record's
// data in an adhoc format, suitable for human inspection.
//
//===----------------------------------------------------------------------===//
#ifndef LLVM_INCLUDE_LLVM_XRAY_RECORDPRINTER_H_
#define LLVM_INCLUDE_LLVM_XRAY_RECORDPRINTER_H_

#include "llvm/Support/raw_ostream.h"
#include "llvm/XRay/FDRRecords.h"

namespace llvm {
namespace xray {

class RecordPrinter : public RecordVisitor {
  raw_ostream &OS;
  std::string Delim;

public:
  explicit RecordPrinter(raw_ostream &O, std::string D)
      : RecordVisitor(), OS(O), Delim(std::move(D)) {}

  explicit RecordPrinter(raw_ostream &O) : RecordPrinter(O, ""){};

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
};

} // namespace xray
} // namespace llvm

#endif // LLVM_INCLUDE_LLVM_XRAY_RECORDPRINTER_H
