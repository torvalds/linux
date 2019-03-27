//===-- StreamGDBRemote.cpp -------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "lldb/Utility/StreamGDBRemote.h"

#include "lldb/Utility/Flags.h"
#include "lldb/Utility/Stream.h"

#include <stdio.h>

using namespace lldb;
using namespace lldb_private;

StreamGDBRemote::StreamGDBRemote() : StreamString() {}

StreamGDBRemote::StreamGDBRemote(uint32_t flags, uint32_t addr_size,
                                 ByteOrder byte_order)
    : StreamString(flags, addr_size, byte_order) {}

StreamGDBRemote::~StreamGDBRemote() {}

int StreamGDBRemote::PutEscapedBytes(const void *s, size_t src_len) {
  int bytes_written = 0;
  const uint8_t *src = (const uint8_t *)s;
  bool binary_is_set = m_flags.Test(eBinary);
  m_flags.Clear(eBinary);
  while (src_len) {
    uint8_t byte = *src;
    src++;
    src_len--;
    if (byte == 0x23 || byte == 0x24 || byte == 0x7d || byte == 0x2a) {
      bytes_written += PutChar(0x7d);
      byte ^= 0x20;
    }
    bytes_written += PutChar(byte);
  };
  if (binary_is_set)
    m_flags.Set(eBinary);
  return bytes_written;
}
