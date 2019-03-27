//===-- Endian.h ------------------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_UTILITY_ENDIAN_H
#define LLDB_UTILITY_ENDIAN_H

#include "lldb/lldb-enumerations.h"

#include <stdint.h>

namespace lldb_private {

namespace endian {

static union EndianTest {
  uint32_t num;
  uint8_t bytes[sizeof(uint32_t)];
} const endianTest = {0x01020304};

inline lldb::ByteOrder InlHostByteOrder() {
  return (lldb::ByteOrder)endianTest.bytes[0];
}

//    ByteOrder const InlHostByteOrder = (ByteOrder)endianTest.bytes[0];
}
}

#endif // liblldb_host_endian_h_
