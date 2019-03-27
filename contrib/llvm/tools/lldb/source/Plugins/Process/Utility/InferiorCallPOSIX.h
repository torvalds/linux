//===-- InferiorCallPOSIX.h -------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef lldb_InferiorCallPOSIX_h_
#define lldb_InferiorCallPOSIX_h_

// Inferior execution of POSIX functions.

#include "lldb/lldb-types.h"

namespace lldb_private {

class Process;

enum MmapProt {
  eMmapProtNone = 0,
  eMmapProtExec = 1,
  eMmapProtRead = 2,
  eMmapProtWrite = 4
};

bool InferiorCallMmap(Process *proc, lldb::addr_t &allocated_addr,
                      lldb::addr_t addr, lldb::addr_t length, unsigned prot,
                      unsigned flags, lldb::addr_t fd, lldb::addr_t offset);

bool InferiorCallMunmap(Process *proc, lldb::addr_t addr, lldb::addr_t length);

bool InferiorCall(Process *proc, const Address *address,
                  lldb::addr_t &returned_func, bool trap_exceptions = false);

} // namespace lldb_private

#endif // lldb_InferiorCallPOSIX_h_
