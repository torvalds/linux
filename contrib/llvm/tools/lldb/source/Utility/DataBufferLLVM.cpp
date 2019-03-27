//===--- DataBufferLLVM.cpp -------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "lldb/Utility/DataBufferLLVM.h"

#include "llvm/ADT/Twine.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/MemoryBuffer.h"

#include <assert.h>
#include <type_traits>

using namespace lldb_private;

DataBufferLLVM::DataBufferLLVM(
    std::unique_ptr<llvm::WritableMemoryBuffer> MemBuffer)
    : Buffer(std::move(MemBuffer)) {
  assert(Buffer != nullptr &&
         "Cannot construct a DataBufferLLVM with a null buffer");
}

DataBufferLLVM::~DataBufferLLVM() {}

uint8_t *DataBufferLLVM::GetBytes() {
  return reinterpret_cast<uint8_t *>(Buffer->getBufferStart());
}

const uint8_t *DataBufferLLVM::GetBytes() const {
  return reinterpret_cast<const uint8_t *>(Buffer->getBufferStart());
}

lldb::offset_t DataBufferLLVM::GetByteSize() const {
  return Buffer->getBufferSize();
}
