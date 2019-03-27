//===-- StreamCallback.h -----------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef liblldb_StreamCallback_h_
#define liblldb_StreamCallback_h_

#include "lldb/lldb-types.h"
#include "llvm/Support/raw_ostream.h"

#include <stddef.h>
#include <stdint.h>

namespace lldb_private {

class StreamCallback : public llvm::raw_ostream {
public:
  StreamCallback(lldb::LogOutputCallback callback, void *baton);
  ~StreamCallback() override = default;

private:
  lldb::LogOutputCallback m_callback;
  void *m_baton;

  void write_impl(const char *Ptr, size_t Size) override;
  uint64_t current_pos() const override;
};

} // namespace lldb_private

#endif // liblldb_StreamCallback_h
