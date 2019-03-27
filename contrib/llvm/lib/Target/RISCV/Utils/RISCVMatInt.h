//===- RISCVMatInt.h - Immediate materialisation ---------------*- C++ -*--===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_RISCV_MATINT_H
#define LLVM_LIB_TARGET_RISCV_MATINT_H

#include "llvm/ADT/SmallVector.h"
#include "llvm/Support/MachineValueType.h"
#include <cstdint>

namespace llvm {

namespace RISCVMatInt {
struct Inst {
  unsigned Opc;
  int64_t Imm;

  Inst(unsigned Opc, int64_t Imm) : Opc(Opc), Imm(Imm) {}
};
using InstSeq = SmallVector<Inst, 8>;

// Helper to generate an instruction sequence that will materialise the given
// immediate value into a register. A sequence of instructions represented by
// a simple struct produced rather than directly emitting the instructions in
// order to allow this helper to be used from both the MC layer and during
// instruction selection.
void generateInstSeq(int64_t Val, bool IsRV64, InstSeq &Res);
} // namespace RISCVMatInt
} // namespace llvm
#endif
