//===-- UserID.cpp ----------------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "lldb/Utility/UserID.h"
#include "lldb/Utility/Stream.h"

#include <inttypes.h>

using namespace lldb;
using namespace lldb_private;

Stream &lldb_private::operator<<(Stream &strm, const UserID &uid) {
  strm.Printf("{0x%8.8" PRIx64 "}", uid.GetID());
  return strm;
}
