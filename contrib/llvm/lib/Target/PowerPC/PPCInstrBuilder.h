//===-- PPCInstrBuilder.h - Aides for building PPC insts --------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file exposes functions that may be used with BuildMI from the
// MachineInstrBuilder.h file to simplify generating frame and constant pool
// references.
//
// For reference, the order of operands for memory references is:
// (Operand), Dest Reg, Base Reg, and either Reg Index or Immediate
// Displacement.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_POWERPC_PPCINSTRBUILDER_H
#define LLVM_LIB_TARGET_POWERPC_PPCINSTRBUILDER_H

#include "llvm/CodeGen/MachineInstrBuilder.h"

namespace llvm {

/// addFrameReference - This function is used to add a reference to the base of
/// an abstract object on the stack frame of the current function.  This
/// reference has base register as the FrameIndex offset until it is resolved.
/// This allows a constant offset to be specified as well...
///
static inline const MachineInstrBuilder&
addFrameReference(const MachineInstrBuilder &MIB, int FI, int Offset = 0,
                  bool mem = true) {
  if (mem)
    return MIB.addImm(Offset).addFrameIndex(FI);
  else
    return MIB.addFrameIndex(FI).addImm(Offset);
}

} // End llvm namespace

#endif
