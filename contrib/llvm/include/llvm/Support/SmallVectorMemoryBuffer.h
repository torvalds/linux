//===- SmallVectorMemoryBuffer.h --------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file declares a wrapper class to hold the memory into which an
// object will be generated.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_EXECUTIONENGINE_OBJECTMEMORYBUFFER_H
#define LLVM_EXECUTIONENGINE_OBJECTMEMORYBUFFER_H

#include "llvm/ADT/SmallVector.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/raw_ostream.h"

namespace llvm {

/// SmallVector-backed MemoryBuffer instance.
///
/// This class enables efficient construction of MemoryBuffers from SmallVector
/// instances. This is useful for MCJIT and Orc, where object files are streamed
/// into SmallVectors, then inspected using ObjectFile (which takes a
/// MemoryBuffer).
class SmallVectorMemoryBuffer : public MemoryBuffer {
public:
  /// Construct an SmallVectorMemoryBuffer from the given SmallVector
  /// r-value.
  ///
  /// FIXME: It'd be nice for this to be a non-templated constructor taking a
  /// SmallVectorImpl here instead of a templated one taking a SmallVector<N>,
  /// but SmallVector's move-construction/assignment currently only take
  /// SmallVectors. If/when that is fixed we can simplify this constructor and
  /// the following one.
  SmallVectorMemoryBuffer(SmallVectorImpl<char> &&SV)
      : SV(std::move(SV)), BufferName("<in-memory object>") {
    init(this->SV.begin(), this->SV.end(), false);
  }

  /// Construct a named SmallVectorMemoryBuffer from the given
  /// SmallVector r-value and StringRef.
  SmallVectorMemoryBuffer(SmallVectorImpl<char> &&SV, StringRef Name)
      : SV(std::move(SV)), BufferName(Name) {
    init(this->SV.begin(), this->SV.end(), false);
  }

  // Key function.
  ~SmallVectorMemoryBuffer() override;

  StringRef getBufferIdentifier() const override { return BufferName; }

  BufferKind getBufferKind() const override { return MemoryBuffer_Malloc; }

private:
  SmallVector<char, 0> SV;
  std::string BufferName;
};

} // namespace llvm

#endif
