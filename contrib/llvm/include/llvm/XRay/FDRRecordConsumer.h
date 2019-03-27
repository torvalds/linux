//===- FDRRecordConsumer.h - XRay Flight Data Recorder Mode Records -------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
#ifndef LLVM_INCLUDE_LLVM_XRAY_FDRRECORDCONSUMER_H_
#define LLVM_INCLUDE_LLVM_XRAY_FDRRECORDCONSUMER_H_

#include "llvm/Support/Error.h"
#include "llvm/XRay/FDRRecords.h"
#include <algorithm>
#include <memory>
#include <vector>

namespace llvm {
namespace xray {

class RecordConsumer {
public:
  virtual Error consume(std::unique_ptr<Record> R) = 0;
  virtual ~RecordConsumer() = default;
};

// This consumer will collect all the records into a vector of records, in
// arrival order.
class LogBuilderConsumer : public RecordConsumer {
  std::vector<std::unique_ptr<Record>> &Records;

public:
  explicit LogBuilderConsumer(std::vector<std::unique_ptr<Record>> &R)
      : RecordConsumer(), Records(R) {}

  Error consume(std::unique_ptr<Record> R) override;
};

// A PipelineConsumer applies a set of visitors to every consumed Record, in the
// order by which the visitors are added to the pipeline in the order of
// appearance.
class PipelineConsumer : public RecordConsumer {
  std::vector<RecordVisitor *> Visitors;

public:
  PipelineConsumer(std::initializer_list<RecordVisitor *> V)
      : RecordConsumer(), Visitors(V) {}

  Error consume(std::unique_ptr<Record> R) override;
};

} // namespace xray
} // namespace llvm

#endif // LLVM_INCLUDE_LLVM_XRAY_FDRRECORDCONSUMER_H_
