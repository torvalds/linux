//==- raw_sha1_ostream.h - raw_ostream that compute SHA1        --*- C++ -*-==//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
//  This file defines the raw_sha1_ostream class.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_SUPPORT_RAW_SHA1_OSTREAM_H
#define LLVM_SUPPORT_RAW_SHA1_OSTREAM_H

#include "llvm/ADT/ArrayRef.h"
#include "llvm/Support/SHA1.h"
#include "llvm/Support/raw_ostream.h"

namespace llvm {

/// A raw_ostream that hash the content using the sha1 algorithm.
class raw_sha1_ostream : public raw_ostream {
  SHA1 State;

  /// See raw_ostream::write_impl.
  void write_impl(const char *Ptr, size_t Size) override {
    State.update(ArrayRef<uint8_t>((const uint8_t *)Ptr, Size));
  }

public:
  /// Return the current SHA1 hash for the content of the stream
  StringRef sha1() {
    flush();
    return State.result();
  }

  /// Reset the internal state to start over from scratch.
  void resetHash() { State.init(); }

  uint64_t current_pos() const override { return 0; }
};

} // end llvm namespace

#endif
