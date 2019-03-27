//===- FDRLogBuilder.h - XRay FDR Log Building Utility --------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
#ifndef LLVM_INCLUDE_LLVM_XRAY_FDRLOGBUILDER_H_
#define LLVM_INCLUDE_LLVM_XRAY_FDRLOGBUILDER_H_

#include "llvm/XRay/FDRRecords.h"

namespace llvm {
namespace xray {

/// The LogBuilder class allows for creating ad-hoc collections of records
/// through the `add<...>(...)` function. An example use of this API is in
/// crafting arbitrary sequences of records:
///
///   auto Records = LogBuilder()
///       .add<BufferExtents>(256)
///       .add<NewBufferRecord>(1)
///       .consume();
///
class LogBuilder {
  std::vector<std::unique_ptr<Record>> Records;

public:
  template <class R, class... T> LogBuilder &add(T &&... A) {
    Records.emplace_back(new R(std::forward<T>(A)...));
    return *this;
  }

  std::vector<std::unique_ptr<Record>> consume() { return std::move(Records); }
};

} // namespace xray
} // namespace llvm

#endif // LLVM_INCLUDE_LLVM_XRAY_FDRLOGBUILDER_H_
