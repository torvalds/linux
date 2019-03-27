//===- BlockIndexer.cpp - FDR Block Indexing VIsitor ----------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// An implementation of the RecordVisitor which generates a mapping between a
// thread and a range of records representing a block.
//
//===----------------------------------------------------------------------===//
#include "llvm/XRay/BlockIndexer.h"

namespace llvm {
namespace xray {

Error BlockIndexer::visit(BufferExtents &) { return Error::success(); }

Error BlockIndexer::visit(WallclockRecord &R) {
  CurrentBlock.Records.push_back(&R);
  CurrentBlock.WallclockTime = &R;
  return Error::success();
}

Error BlockIndexer::visit(NewCPUIDRecord &R) {
  CurrentBlock.Records.push_back(&R);
  return Error::success();
}

Error BlockIndexer::visit(TSCWrapRecord &R) {
  CurrentBlock.Records.push_back(&R);
  return Error::success();
}

Error BlockIndexer::visit(CustomEventRecord &R) {
  CurrentBlock.Records.push_back(&R);
  return Error::success();
}

Error BlockIndexer::visit(CustomEventRecordV5 &R) {
  CurrentBlock.Records.push_back(&R);
  return Error::success();
}

Error BlockIndexer::visit(TypedEventRecord &R) {
  CurrentBlock.Records.push_back(&R);
  return Error::success();
}

Error BlockIndexer::visit(CallArgRecord &R) {
  CurrentBlock.Records.push_back(&R);
  return Error::success();
}

Error BlockIndexer::visit(PIDRecord &R) {
  CurrentBlock.ProcessID = R.pid();
  CurrentBlock.Records.push_back(&R);
  return Error::success();
}

Error BlockIndexer::visit(NewBufferRecord &R) {
  if (!CurrentBlock.Records.empty())
    if (auto E = flush())
      return E;

  CurrentBlock.ThreadID = R.tid();
  CurrentBlock.Records.push_back(&R);
  return Error::success();
}

Error BlockIndexer::visit(EndBufferRecord &R) {
  CurrentBlock.Records.push_back(&R);
  return Error::success();
}

Error BlockIndexer::visit(FunctionRecord &R) {
  CurrentBlock.Records.push_back(&R);
  return Error::success();
}

Error BlockIndexer::flush() {
  Index::iterator It;
  std::tie(It, std::ignore) =
      Indices.insert({{CurrentBlock.ProcessID, CurrentBlock.ThreadID}, {}});
  It->second.push_back({CurrentBlock.ProcessID, CurrentBlock.ThreadID,
                        CurrentBlock.WallclockTime,
                        std::move(CurrentBlock.Records)});
  CurrentBlock.ProcessID = 0;
  CurrentBlock.ThreadID = 0;
  CurrentBlock.Records = {};
  CurrentBlock.WallclockTime = nullptr;
  return Error::success();
}

} // namespace xray
} // namespace llvm
