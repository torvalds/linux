//===-- ARM64_ehframe_Registers.h -------------------------------------*- C++
//-*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef utility_ARM64_ehframe_Registers_h_
#define utility_ARM64_ehframe_Registers_h_

// The register numbers used in the eh_frame unwind information.
// Should be the same as DWARF register numbers.

namespace arm64_ehframe {

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
  fp, // aka x29
  lr, // aka x30
  sp, // aka x31 aka wzr
  pc, // value is 32
  cpsr
};

enum {
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
  v31 // 95
};
}

#endif // utility_ARM64_ehframe_Registers_h_
