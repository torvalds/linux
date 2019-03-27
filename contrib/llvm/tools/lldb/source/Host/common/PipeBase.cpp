//===-- source/Host/common/PipeBase.cpp -------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "lldb/Host/PipeBase.h"

using namespace lldb_private;

PipeBase::~PipeBase() = default;

Status PipeBase::OpenAsWriter(llvm::StringRef name,
                              bool child_process_inherit) {
  return OpenAsWriterWithTimeout(name, child_process_inherit,
                                 std::chrono::microseconds::zero());
}

Status PipeBase::Read(void *buf, size_t size, size_t &bytes_read) {
  return ReadWithTimeout(buf, size, std::chrono::microseconds::zero(),
                         bytes_read);
}
