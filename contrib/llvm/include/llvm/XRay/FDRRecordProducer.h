//===- FDRRecordProducer.h - XRay FDR Mode Record Producer ----------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
#ifndef LLVM_INCLUDE_LLVM_XRAY_FDRRECORDPRODUCER_H_
#define LLVM_INCLUDE_LLVM_XRAY_FDRRECORDPRODUCER_H_

#include "llvm/Support/Error.h"
#include "llvm/XRay/FDRRecords.h"
#include "llvm/XRay/XRayRecord.h"
#include <memory>

namespace llvm {
namespace xray {

class RecordProducer {
public:
  /// All producer implementations must yield either an Error or a non-nullptr
  /// unique_ptr<Record>.
  virtual Expected<std::unique_ptr<Record>> produce() = 0;
  virtual ~RecordProducer() = default;
};

class FileBasedRecordProducer : public RecordProducer {
  const XRayFileHeader &Header;
  DataExtractor &E;
  uint32_t &OffsetPtr;
  uint32_t CurrentBufferBytes = 0;

  // Helper function which gets the next record by speculatively reading through
  // the log, finding a buffer extents record.
  Expected<std::unique_ptr<Record>> findNextBufferExtent();

public:
  FileBasedRecordProducer(const XRayFileHeader &FH, DataExtractor &DE,
                          uint32_t &OP)
      : Header(FH), E(DE), OffsetPtr(OP) {}

  /// This producer encapsulates the logic for loading a File-backed
  /// RecordProducer hidden behind a DataExtractor.
  Expected<std::unique_ptr<Record>> produce() override;
};

} // namespace xray
} // namespace llvm

#endif // LLVM_INCLUDE_LLVM_XRAY_FDRRECORDPRODUCER_H_
