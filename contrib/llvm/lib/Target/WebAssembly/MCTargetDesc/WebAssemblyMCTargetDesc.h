//==- WebAssemblyMCTargetDesc.h - WebAssembly Target Descriptions -*- C++ -*-=//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file provides WebAssembly-specific target descriptions.
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_WEBASSEMBLY_MCTARGETDESC_WEBASSEMBLYMCTARGETDESC_H
#define LLVM_LIB_TARGET_WEBASSEMBLY_MCTARGETDESC_WEBASSEMBLYMCTARGETDESC_H

#include "llvm/BinaryFormat/Wasm.h"
#include "llvm/MC/MCInstrDesc.h"
#include "llvm/Support/DataTypes.h"
#include <memory>

namespace llvm {

class MCAsmBackend;
class MCCodeEmitter;
class MCContext;
class MCInstrInfo;
class MCObjectTargetWriter;
class MCSubtargetInfo;
class MVT;
class Target;
class Triple;
class raw_pwrite_stream;

Target &getTheWebAssemblyTarget32();
Target &getTheWebAssemblyTarget64();

MCCodeEmitter *createWebAssemblyMCCodeEmitter(const MCInstrInfo &MCII);

MCAsmBackend *createWebAssemblyAsmBackend(const Triple &TT);

std::unique_ptr<MCObjectTargetWriter>
createWebAssemblyWasmObjectWriter(bool Is64Bit);

namespace WebAssembly {
enum OperandType {
  /// Basic block label in a branch construct.
  OPERAND_BASIC_BLOCK = MCOI::OPERAND_FIRST_TARGET,
  /// Local index.
  OPERAND_LOCAL,
  /// Global index.
  OPERAND_GLOBAL,
  /// 32-bit integer immediates.
  OPERAND_I32IMM,
  /// 64-bit integer immediates.
  OPERAND_I64IMM,
  /// 32-bit floating-point immediates.
  OPERAND_F32IMM,
  /// 64-bit floating-point immediates.
  OPERAND_F64IMM,
  /// 8-bit vector lane immediate
  OPERAND_VEC_I8IMM,
  /// 16-bit vector lane immediate
  OPERAND_VEC_I16IMM,
  /// 32-bit vector lane immediate
  OPERAND_VEC_I32IMM,
  /// 64-bit vector lane immediate
  OPERAND_VEC_I64IMM,
  /// 32-bit unsigned function indices.
  OPERAND_FUNCTION32,
  /// 32-bit unsigned memory offsets.
  OPERAND_OFFSET32,
  /// p2align immediate for load and store address alignment.
  OPERAND_P2ALIGN,
  /// signature immediate for block/loop.
  OPERAND_SIGNATURE,
  /// type signature immediate for call_indirect.
  OPERAND_TYPEINDEX,
  /// Event index.
  OPERAND_EVENT,
  /// A list of branch targets for br_list.
  OPERAND_BRLIST,
};
} // end namespace WebAssembly

namespace WebAssemblyII {

/// Target Operand Flag enum.
enum TOF {
  MO_NO_FLAG = 0,

  // Flags to indicate the type of the symbol being referenced
  MO_SYMBOL_FUNCTION = 0x1,
  MO_SYMBOL_GLOBAL = 0x2,
  MO_SYMBOL_EVENT = 0x4,
  MO_SYMBOL_MASK = 0x7,
};
} // end namespace WebAssemblyII

} // end namespace llvm

// Defines symbolic names for WebAssembly registers. This defines a mapping from
// register name to register number.
//
#define GET_REGINFO_ENUM
#include "WebAssemblyGenRegisterInfo.inc"

// Defines symbolic names for the WebAssembly instructions.
//
#define GET_INSTRINFO_ENUM
#include "WebAssemblyGenInstrInfo.inc"

#define GET_SUBTARGETINFO_ENUM
#include "WebAssemblyGenSubtargetInfo.inc"

namespace llvm {
namespace WebAssembly {

/// Return the default p2align value for a load or store with the given opcode.
inline unsigned GetDefaultP2Align(unsigned Opcode) {
  switch (Opcode) {
  case WebAssembly::LOAD8_S_I32:
  case WebAssembly::LOAD8_S_I32_S:
  case WebAssembly::LOAD8_U_I32:
  case WebAssembly::LOAD8_U_I32_S:
  case WebAssembly::LOAD8_S_I64:
  case WebAssembly::LOAD8_S_I64_S:
  case WebAssembly::LOAD8_U_I64:
  case WebAssembly::LOAD8_U_I64_S:
  case WebAssembly::ATOMIC_LOAD8_U_I32:
  case WebAssembly::ATOMIC_LOAD8_U_I32_S:
  case WebAssembly::ATOMIC_LOAD8_U_I64:
  case WebAssembly::ATOMIC_LOAD8_U_I64_S:
  case WebAssembly::STORE8_I32:
  case WebAssembly::STORE8_I32_S:
  case WebAssembly::STORE8_I64:
  case WebAssembly::STORE8_I64_S:
  case WebAssembly::ATOMIC_STORE8_I32:
  case WebAssembly::ATOMIC_STORE8_I32_S:
  case WebAssembly::ATOMIC_STORE8_I64:
  case WebAssembly::ATOMIC_STORE8_I64_S:
  case WebAssembly::ATOMIC_RMW8_U_ADD_I32:
  case WebAssembly::ATOMIC_RMW8_U_ADD_I32_S:
  case WebAssembly::ATOMIC_RMW8_U_ADD_I64:
  case WebAssembly::ATOMIC_RMW8_U_ADD_I64_S:
  case WebAssembly::ATOMIC_RMW8_U_SUB_I32:
  case WebAssembly::ATOMIC_RMW8_U_SUB_I32_S:
  case WebAssembly::ATOMIC_RMW8_U_SUB_I64:
  case WebAssembly::ATOMIC_RMW8_U_SUB_I64_S:
  case WebAssembly::ATOMIC_RMW8_U_AND_I32:
  case WebAssembly::ATOMIC_RMW8_U_AND_I32_S:
  case WebAssembly::ATOMIC_RMW8_U_AND_I64:
  case WebAssembly::ATOMIC_RMW8_U_AND_I64_S:
  case WebAssembly::ATOMIC_RMW8_U_OR_I32:
  case WebAssembly::ATOMIC_RMW8_U_OR_I32_S:
  case WebAssembly::ATOMIC_RMW8_U_OR_I64:
  case WebAssembly::ATOMIC_RMW8_U_OR_I64_S:
  case WebAssembly::ATOMIC_RMW8_U_XOR_I32:
  case WebAssembly::ATOMIC_RMW8_U_XOR_I32_S:
  case WebAssembly::ATOMIC_RMW8_U_XOR_I64:
  case WebAssembly::ATOMIC_RMW8_U_XOR_I64_S:
  case WebAssembly::ATOMIC_RMW8_U_XCHG_I32:
  case WebAssembly::ATOMIC_RMW8_U_XCHG_I32_S:
  case WebAssembly::ATOMIC_RMW8_U_XCHG_I64:
  case WebAssembly::ATOMIC_RMW8_U_XCHG_I64_S:
  case WebAssembly::ATOMIC_RMW8_U_CMPXCHG_I32:
  case WebAssembly::ATOMIC_RMW8_U_CMPXCHG_I32_S:
  case WebAssembly::ATOMIC_RMW8_U_CMPXCHG_I64:
  case WebAssembly::ATOMIC_RMW8_U_CMPXCHG_I64_S:
    return 0;
  case WebAssembly::LOAD16_S_I32:
  case WebAssembly::LOAD16_S_I32_S:
  case WebAssembly::LOAD16_U_I32:
  case WebAssembly::LOAD16_U_I32_S:
  case WebAssembly::LOAD16_S_I64:
  case WebAssembly::LOAD16_S_I64_S:
  case WebAssembly::LOAD16_U_I64:
  case WebAssembly::LOAD16_U_I64_S:
  case WebAssembly::ATOMIC_LOAD16_U_I32:
  case WebAssembly::ATOMIC_LOAD16_U_I32_S:
  case WebAssembly::ATOMIC_LOAD16_U_I64:
  case WebAssembly::ATOMIC_LOAD16_U_I64_S:
  case WebAssembly::STORE16_I32:
  case WebAssembly::STORE16_I32_S:
  case WebAssembly::STORE16_I64:
  case WebAssembly::STORE16_I64_S:
  case WebAssembly::ATOMIC_STORE16_I32:
  case WebAssembly::ATOMIC_STORE16_I32_S:
  case WebAssembly::ATOMIC_STORE16_I64:
  case WebAssembly::ATOMIC_STORE16_I64_S:
  case WebAssembly::ATOMIC_RMW16_U_ADD_I32:
  case WebAssembly::ATOMIC_RMW16_U_ADD_I32_S:
  case WebAssembly::ATOMIC_RMW16_U_ADD_I64:
  case WebAssembly::ATOMIC_RMW16_U_ADD_I64_S:
  case WebAssembly::ATOMIC_RMW16_U_SUB_I32:
  case WebAssembly::ATOMIC_RMW16_U_SUB_I32_S:
  case WebAssembly::ATOMIC_RMW16_U_SUB_I64:
  case WebAssembly::ATOMIC_RMW16_U_SUB_I64_S:
  case WebAssembly::ATOMIC_RMW16_U_AND_I32:
  case WebAssembly::ATOMIC_RMW16_U_AND_I32_S:
  case WebAssembly::ATOMIC_RMW16_U_AND_I64:
  case WebAssembly::ATOMIC_RMW16_U_AND_I64_S:
  case WebAssembly::ATOMIC_RMW16_U_OR_I32:
  case WebAssembly::ATOMIC_RMW16_U_OR_I32_S:
  case WebAssembly::ATOMIC_RMW16_U_OR_I64:
  case WebAssembly::ATOMIC_RMW16_U_OR_I64_S:
  case WebAssembly::ATOMIC_RMW16_U_XOR_I32:
  case WebAssembly::ATOMIC_RMW16_U_XOR_I32_S:
  case WebAssembly::ATOMIC_RMW16_U_XOR_I64:
  case WebAssembly::ATOMIC_RMW16_U_XOR_I64_S:
  case WebAssembly::ATOMIC_RMW16_U_XCHG_I32:
  case WebAssembly::ATOMIC_RMW16_U_XCHG_I32_S:
  case WebAssembly::ATOMIC_RMW16_U_XCHG_I64:
  case WebAssembly::ATOMIC_RMW16_U_XCHG_I64_S:
  case WebAssembly::ATOMIC_RMW16_U_CMPXCHG_I32:
  case WebAssembly::ATOMIC_RMW16_U_CMPXCHG_I32_S:
  case WebAssembly::ATOMIC_RMW16_U_CMPXCHG_I64:
  case WebAssembly::ATOMIC_RMW16_U_CMPXCHG_I64_S:
    return 1;
  case WebAssembly::LOAD_I32:
  case WebAssembly::LOAD_I32_S:
  case WebAssembly::LOAD_F32:
  case WebAssembly::LOAD_F32_S:
  case WebAssembly::STORE_I32:
  case WebAssembly::STORE_I32_S:
  case WebAssembly::STORE_F32:
  case WebAssembly::STORE_F32_S:
  case WebAssembly::LOAD32_S_I64:
  case WebAssembly::LOAD32_S_I64_S:
  case WebAssembly::LOAD32_U_I64:
  case WebAssembly::LOAD32_U_I64_S:
  case WebAssembly::STORE32_I64:
  case WebAssembly::STORE32_I64_S:
  case WebAssembly::ATOMIC_LOAD_I32:
  case WebAssembly::ATOMIC_LOAD_I32_S:
  case WebAssembly::ATOMIC_LOAD32_U_I64:
  case WebAssembly::ATOMIC_LOAD32_U_I64_S:
  case WebAssembly::ATOMIC_STORE_I32:
  case WebAssembly::ATOMIC_STORE_I32_S:
  case WebAssembly::ATOMIC_STORE32_I64:
  case WebAssembly::ATOMIC_STORE32_I64_S:
  case WebAssembly::ATOMIC_RMW_ADD_I32:
  case WebAssembly::ATOMIC_RMW_ADD_I32_S:
  case WebAssembly::ATOMIC_RMW32_U_ADD_I64:
  case WebAssembly::ATOMIC_RMW32_U_ADD_I64_S:
  case WebAssembly::ATOMIC_RMW_SUB_I32:
  case WebAssembly::ATOMIC_RMW_SUB_I32_S:
  case WebAssembly::ATOMIC_RMW32_U_SUB_I64:
  case WebAssembly::ATOMIC_RMW32_U_SUB_I64_S:
  case WebAssembly::ATOMIC_RMW_AND_I32:
  case WebAssembly::ATOMIC_RMW_AND_I32_S:
  case WebAssembly::ATOMIC_RMW32_U_AND_I64:
  case WebAssembly::ATOMIC_RMW32_U_AND_I64_S:
  case WebAssembly::ATOMIC_RMW_OR_I32:
  case WebAssembly::ATOMIC_RMW_OR_I32_S:
  case WebAssembly::ATOMIC_RMW32_U_OR_I64:
  case WebAssembly::ATOMIC_RMW32_U_OR_I64_S:
  case WebAssembly::ATOMIC_RMW_XOR_I32:
  case WebAssembly::ATOMIC_RMW_XOR_I32_S:
  case WebAssembly::ATOMIC_RMW32_U_XOR_I64:
  case WebAssembly::ATOMIC_RMW32_U_XOR_I64_S:
  case WebAssembly::ATOMIC_RMW_XCHG_I32:
  case WebAssembly::ATOMIC_RMW_XCHG_I32_S:
  case WebAssembly::ATOMIC_RMW32_U_XCHG_I64:
  case WebAssembly::ATOMIC_RMW32_U_XCHG_I64_S:
  case WebAssembly::ATOMIC_RMW_CMPXCHG_I32:
  case WebAssembly::ATOMIC_RMW_CMPXCHG_I32_S:
  case WebAssembly::ATOMIC_RMW32_U_CMPXCHG_I64:
  case WebAssembly::ATOMIC_RMW32_U_CMPXCHG_I64_S:
  case WebAssembly::ATOMIC_NOTIFY:
  case WebAssembly::ATOMIC_NOTIFY_S:
  case WebAssembly::ATOMIC_WAIT_I32:
  case WebAssembly::ATOMIC_WAIT_I32_S:
    return 2;
  case WebAssembly::LOAD_I64:
  case WebAssembly::LOAD_I64_S:
  case WebAssembly::LOAD_F64:
  case WebAssembly::LOAD_F64_S:
  case WebAssembly::STORE_I64:
  case WebAssembly::STORE_I64_S:
  case WebAssembly::STORE_F64:
  case WebAssembly::STORE_F64_S:
  case WebAssembly::ATOMIC_LOAD_I64:
  case WebAssembly::ATOMIC_LOAD_I64_S:
  case WebAssembly::ATOMIC_STORE_I64:
  case WebAssembly::ATOMIC_STORE_I64_S:
  case WebAssembly::ATOMIC_RMW_ADD_I64:
  case WebAssembly::ATOMIC_RMW_ADD_I64_S:
  case WebAssembly::ATOMIC_RMW_SUB_I64:
  case WebAssembly::ATOMIC_RMW_SUB_I64_S:
  case WebAssembly::ATOMIC_RMW_AND_I64:
  case WebAssembly::ATOMIC_RMW_AND_I64_S:
  case WebAssembly::ATOMIC_RMW_OR_I64:
  case WebAssembly::ATOMIC_RMW_OR_I64_S:
  case WebAssembly::ATOMIC_RMW_XOR_I64:
  case WebAssembly::ATOMIC_RMW_XOR_I64_S:
  case WebAssembly::ATOMIC_RMW_XCHG_I64:
  case WebAssembly::ATOMIC_RMW_XCHG_I64_S:
  case WebAssembly::ATOMIC_RMW_CMPXCHG_I64:
  case WebAssembly::ATOMIC_RMW_CMPXCHG_I64_S:
  case WebAssembly::ATOMIC_WAIT_I64:
  case WebAssembly::ATOMIC_WAIT_I64_S:
    return 3;
  case WebAssembly::LOAD_v16i8:
  case WebAssembly::LOAD_v16i8_S:
  case WebAssembly::LOAD_v8i16:
  case WebAssembly::LOAD_v8i16_S:
  case WebAssembly::LOAD_v4i32:
  case WebAssembly::LOAD_v4i32_S:
  case WebAssembly::LOAD_v2i64:
  case WebAssembly::LOAD_v2i64_S:
  case WebAssembly::LOAD_v4f32:
  case WebAssembly::LOAD_v4f32_S:
  case WebAssembly::LOAD_v2f64:
  case WebAssembly::LOAD_v2f64_S:
  case WebAssembly::STORE_v16i8:
  case WebAssembly::STORE_v16i8_S:
  case WebAssembly::STORE_v8i16:
  case WebAssembly::STORE_v8i16_S:
  case WebAssembly::STORE_v4i32:
  case WebAssembly::STORE_v4i32_S:
  case WebAssembly::STORE_v2i64:
  case WebAssembly::STORE_v2i64_S:
  case WebAssembly::STORE_v4f32:
  case WebAssembly::STORE_v4f32_S:
  case WebAssembly::STORE_v2f64:
  case WebAssembly::STORE_v2f64_S:
    return 4;
  default:
    llvm_unreachable("Only loads and stores have p2align values");
  }
}

/// The operand number of the load or store address in load/store instructions.
static const unsigned LoadAddressOperandNo = 3;
static const unsigned StoreAddressOperandNo = 2;

/// The operand number of the load or store p2align in load/store instructions.
static const unsigned LoadP2AlignOperandNo = 1;
static const unsigned StoreP2AlignOperandNo = 0;

/// This is used to indicate block signatures.
enum class ExprType : unsigned {
  Void = 0x40,
  I32 = 0x7F,
  I64 = 0x7E,
  F32 = 0x7D,
  F64 = 0x7C,
  V128 = 0x7B,
  ExceptRef = 0x68,
  Invalid = 0x00
};

/// Instruction opcodes emitted via means other than CodeGen.
static const unsigned Nop = 0x01;
static const unsigned End = 0x0b;

wasm::ValType toValType(const MVT &Ty);

} // end namespace WebAssembly
} // end namespace llvm

#endif
