//===-- lldb-private.h ------------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef lldb_lldb_private_h_
#define lldb_lldb_private_h_

#if defined(__cplusplus)

#include "lldb/lldb-private-defines.h"
#include "lldb/lldb-private-enumerations.h"
#include "lldb/lldb-private-interfaces.h"
#include "lldb/lldb-private-types.h"
#include "lldb/lldb-public.h"

namespace lldb_private {

const char *GetVersion();

} // namespace lldb_private

#endif // defined(__cplusplus)

#endif // lldb_lldb_private_h_
