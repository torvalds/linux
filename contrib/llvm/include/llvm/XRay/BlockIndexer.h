//===- BlockIndexer.h - FDR Block Indexing Visitor ------------------------===//
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
#ifndef LLVM_LIB_XRAY_BLOCKINDEXER_H_
#define LLVM_LIB_XRAY_BLOCKINDEXER_H_

#include "llvm/ADT/DenseMap.h"
#include "llvm/XRay/FDRRecords.h"
#include <cstdint>
#include <vector>

namespace llvm {
namespace xray {

// The BlockIndexer will gather all related records associated with a
// process+thread and group them by 'Block'.
class BlockIndexer : public RecordVisitor {
public:
  struct Block {
    uint64_t ProcessID;
    int32_t ThreadID;
    WallclockRecord *WallclockTime;
    std::vector<Record *> Records;
  };

  // This maps the process + thread combination to a sequence of blocks.
  using Index = DenseMap<std::pair<uint64_t, int32_t>, std::vector<Block>>;

private:
  Index &Indices;

  Block CurrentBlock{0, 0, nullptr, {}};

public:
  explicit BlockIndexer(Index &I) : RecordVisitor(), Indices(I) {}

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

  /// The flush() function will clear out the current state of the visitor, to
  /// allow for explicitly flushing a block's records to the currently
  /// recognized thread and process combination.
  Error flush();
};

} // namespace xray
} // namespace llvm

#endif // LLVM_LIB_XRAY_BLOCKINDEXER_H_
