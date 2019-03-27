//===-- ARM64_DWARF_Registers.h ---------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef utility_ARM64_DWARF_Registers_h_
#define utility_ARM64_DWARF_Registers_h_

#include "lldb/lldb-private.h"

namespace arm64_dwarf {

enum {
  x0 = 0,
  x1,
  x2,
  x3,
  x4,
  x5,
  x6,
  x7,
  x8,
  x9,
  x10,
  x11,
  x12,
  x13,
  x14,
  x15,
  x16,
  x17,
  x18,
  x19,
  x20,
  x21,
  x22,
  x23,
  x24,
  x25,
  x26,
  x27,
  x28,
  x29 = 29,
  fp = x29,
  x30 = 30,
  lr = x30,
  x31 = 31,
  sp = x31,
  pc = 32,
  cpsr = 33,
  // 34-63 reserved

  // V0-V31 (128 bit vector registers)
  v0 = 64,
  v1,
  v2,
  v3,
  v4,
  v5,
  v6,
  v7,
  v8,
  v9,
  v10,
  v11,
  v12,
  v13,
  v14,
  v15,
  v16,
  v17,
  v18,
  v19,
  v20,
  v21,
  v22,
  v23,
  v24,
  v25,
  v26,
  v27,
  v28,
  v29,
  v30,
  v31

  // 96-127 reserved
};

} // namespace arm64_dwarf

#endif // utility_ARM64_DWARF_Registers_h_
