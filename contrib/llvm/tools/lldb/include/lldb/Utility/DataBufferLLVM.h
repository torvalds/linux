//===--- DataBufferLLVM.h ---------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_CORE_DATABUFFERLLVM_H
#define LLDB_CORE_DATABUFFERLLVM_H

#include "lldb/Utility/DataBuffer.h"
#include "lldb/lldb-types.h"

#include <memory>
#include <stdint.h>

namespace llvm {
class WritableMemoryBuffer;
class Twine;
}

namespace lldb_private {

class FileSystem;
class DataBufferLLVM : public DataBuffer {
public:
  ~DataBufferLLVM();

  uint8_t *GetBytes() override;
  const uint8_t *GetBytes() const override;
  lldb::offset_t GetByteSize() const override;

  char *GetChars() { return reinterpret_cast<char *>(GetBytes()); }

private:
  friend FileSystem;
  /// Construct a DataBufferLLVM from \p Buffer.  \p Buffer must be a valid
  /// pointer.
  explicit DataBufferLLVM(std::unique_ptr<llvm::WritableMemoryBuffer> Buffer);

  std::unique_ptr<llvm::WritableMemoryBuffer> Buffer;
};
}

#endif
