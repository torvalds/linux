//===- FDRRecordConsumer.h - XRay Flight Data Recorder Mode Records -------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
#include "llvm/XRay/FDRRecordConsumer.h"

namespace llvm {
namespace xray {

Error LogBuilderConsumer::consume(std::unique_ptr<Record> R) {
  if (!R)
    return createStringError(
        std::make_error_code(std::errc::invalid_argument),
        "Must not call RecordConsumer::consume() with a null pointer.");
  Records.push_back(std::move(R));
  return Error::success();
}

Error PipelineConsumer::consume(std::unique_ptr<Record> R) {
  if (!R)
    return createStringError(
        std::make_error_code(std::errc::invalid_argument),
        "Must not call RecordConsumer::consume() with a null pointer.");

  // We apply all of the visitors in order, and concatenate errors
  // appropriately.
  Error Result = Error::success();
  for (auto *V : Visitors)
    Result = joinErrors(std::move(Result), R->apply(*V));
  return Result;
}

} // namespace xray
} // namespace llvm
