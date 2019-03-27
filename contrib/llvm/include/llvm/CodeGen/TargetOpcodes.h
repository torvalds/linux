//===-- llvm/CodeGen/TargetOpcodes.h - Target Indep Opcodes -----*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file defines the target independent instruction opcodes.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CODEGEN_TARGETOPCODES_H
#define LLVM_CODEGEN_TARGETOPCODES_H

namespace llvm {

/// Invariant opcodes: All instruction sets have these as their low opcodes.
///
namespace TargetOpcode {
enum {
#define HANDLE_TARGET_OPCODE(OPC) OPC,
#define HANDLE_TARGET_OPCODE_MARKER(IDENT, OPC) IDENT = OPC,
#include "llvm/Support/TargetOpcodes.def"
};
} // end namespace TargetOpcode

/// Check whether the given Opcode is a generic opcode that is not supposed
/// to appear after ISel.
inline bool isPreISelGenericOpcode(unsigned Opcode) {
  return Opcode >= TargetOpcode::PRE_ISEL_GENERIC_OPCODE_START &&
         Opcode <= TargetOpcode::PRE_ISEL_GENERIC_OPCODE_END;
}

/// Check whether the given Opcode is a target-specific opcode.
inline bool isTargetSpecificOpcode(unsigned Opcode) {
  return Opcode > TargetOpcode::PRE_ISEL_GENERIC_OPCODE_END;
}
} // end namespace llvm

#endif
