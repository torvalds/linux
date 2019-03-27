//===-- StreamCallback.cpp -------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "lldb/Utility/StreamCallback.h"

#include <string>

using namespace lldb_private;

StreamCallback::StreamCallback(lldb::LogOutputCallback callback, void *baton)
    : llvm::raw_ostream(true), m_callback(callback), m_baton(baton) {}

void StreamCallback::write_impl(const char *Ptr, size_t Size) {
  m_callback(std::string(Ptr, Size).c_str(), m_baton);
}

uint64_t StreamCallback::current_pos() const { return 0; }
