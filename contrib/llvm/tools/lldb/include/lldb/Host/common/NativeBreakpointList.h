//===-- NativeBreakpointList.h ----------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef liblldb_NativeBreakpointList_h_
#define liblldb_NativeBreakpointList_h_

#include "lldb/lldb-private-forward.h"
#include "lldb/lldb-types.h"
#include <map>

namespace lldb_private {

struct HardwareBreakpoint {
  lldb::addr_t m_addr;
  size_t m_size;
};

using HardwareBreakpointMap = std::map<lldb::addr_t, HardwareBreakpoint>;
}

#endif // ifndef liblldb_NativeBreakpointList_h_
