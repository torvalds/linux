//===-- xray_mips.cc --------------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file is a part of XRay, a dynamic runtime instrumentation system.
//
// Implementation of MIPS-specific routines (32-bit).
//
//===----------------------------------------------------------------------===//
#include "sanitizer_common/sanitizer_common.h"
#include "xray_defs.h"
#include "xray_interface_internal.h"
#include <atomic>

namespace __xray {

// The machine codes for some instructions used in runtime patching.
enum PatchOpcodes : uint32_t {
  PO_ADDIU = 0x24000000, // addiu rt, rs, imm
  PO_SW = 0xAC000000,    // sw rt, offset(sp)
  PO_LUI = 0x3C000000,   // lui rs, %hi(address)
  PO_ORI = 0x34000000,   // ori rt, rs, %lo(address)
  PO_JALR = 0x0000F809,  // jalr rs
  PO_LW = 0x8C000000,    // lw rt, offset(address)
  PO_B44 = 0x1000000b,   // b #44
  PO_NOP = 0x0,          // nop
};

enum RegNum : uint32_t {
  RN_T0 = 0x8,
  RN_T9 = 0x19,
  RN_RA = 0x1F,
  RN_SP = 0x1D,
};

inline static uint32_t encodeInstruction(uint32_t Opcode, uint32_t Rs,
                                         uint32_t Rt,
                                         uint32_t Imm) XRAY_NEVER_INSTRUMENT {
  return (Opcode | Rs << 21 | Rt << 16 | Imm);
}

inline static uint32_t
encodeSpecialInstruction(uint32_t Opcode, uint32_t Rs, uint32_t Rt, uint32_t Rd,
                         uint32_t Imm) XRAY_NEVER_INSTRUMENT {
  return (Rs << 21 | Rt << 16 | Rd << 11 | Imm << 6 | Opcode);
}

inline static bool patchSled(const bool Enable, const uint32_t FuncId,
                             const XRaySledEntry &Sled,
                             void (*TracingHook)()) XRAY_NEVER_INSTRUMENT {
  // When |Enable| == true,
  // We replace the following compile-time stub (sled):
  //
  // xray_sled_n:
  //	B .tmpN
  //	11 NOPs (44 bytes)
  //	.tmpN
  //	ADDIU T9, T9, 44
  //
  // With the following runtime patch:
  //
  // xray_sled_n (32-bit):
  //    addiu sp, sp, -8                        ;create stack frame
  //    nop
  //    sw ra, 4(sp)                            ;save return address
  //    sw t9, 0(sp)                            ;save register t9
  //    lui t9, %hi(__xray_FunctionEntry/Exit)
  //    ori t9, t9, %lo(__xray_FunctionEntry/Exit)
  //    lui t0, %hi(function_id)
  //    jalr t9                                 ;call Tracing hook
  //    ori t0, t0, %lo(function_id)            ;pass function id (delay slot)
  //    lw t9, 0(sp)                            ;restore register t9
  //    lw ra, 4(sp)                            ;restore return address
  //    addiu sp, sp, 8                         ;delete stack frame
  //
  // We add 44 bytes to t9 because we want to adjust the function pointer to
  // the actual start of function i.e. the address just after the noop sled.
  // We do this because gp displacement relocation is emitted at the start of
  // of the function i.e after the nop sled and to correctly calculate the
  // global offset table address, t9 must hold the address of the instruction
  // containing the gp displacement relocation.
  // FIXME: Is this correct for the static relocation model?
  //
  // Replacement of the first 4-byte instruction should be the last and atomic
  // operation, so that the user code which reaches the sled concurrently
  // either jumps over the whole sled, or executes the whole sled when the
  // latter is ready.
  //
  // When |Enable|==false, we set back the first instruction in the sled to be
  //   B #44

  if (Enable) {
    uint32_t LoTracingHookAddr =
        reinterpret_cast<int32_t>(TracingHook) & 0xffff;
    uint32_t HiTracingHookAddr =
        (reinterpret_cast<int32_t>(TracingHook) >> 16) & 0xffff;
    uint32_t LoFunctionID = FuncId & 0xffff;
    uint32_t HiFunctionID = (FuncId >> 16) & 0xffff;
    *reinterpret_cast<uint32_t *>(Sled.Address + 8) = encodeInstruction(
        PatchOpcodes::PO_SW, RegNum::RN_SP, RegNum::RN_RA, 0x4);
    *reinterpret_cast<uint32_t *>(Sled.Address + 12) = encodeInstruction(
        PatchOpcodes::PO_SW, RegNum::RN_SP, RegNum::RN_T9, 0x0);
    *reinterpret_cast<uint32_t *>(Sled.Address + 16) = encodeInstruction(
        PatchOpcodes::PO_LUI, 0x0, RegNum::RN_T9, HiTracingHookAddr);
    *reinterpret_cast<uint32_t *>(Sled.Address + 20) = encodeInstruction(
        PatchOpcodes::PO_ORI, RegNum::RN_T9, RegNum::RN_T9, LoTracingHookAddr);
    *reinterpret_cast<uint32_t *>(Sled.Address + 24) = encodeInstruction(
        PatchOpcodes::PO_LUI, 0x0, RegNum::RN_T0, HiFunctionID);
    *reinterpret_cast<uint32_t *>(Sled.Address + 28) = encodeSpecialInstruction(
        PatchOpcodes::PO_JALR, RegNum::RN_T9, 0x0, RegNum::RN_RA, 0X0);
    *reinterpret_cast<uint32_t *>(Sled.Address + 32) = encodeInstruction(
        PatchOpcodes::PO_ORI, RegNum::RN_T0, RegNum::RN_T0, LoFunctionID);
    *reinterpret_cast<uint32_t *>(Sled.Address + 36) = encodeInstruction(
        PatchOpcodes::PO_LW, RegNum::RN_SP, RegNum::RN_T9, 0x0);
    *reinterpret_cast<uint32_t *>(Sled.Address + 40) = encodeInstruction(
        PatchOpcodes::PO_LW, RegNum::RN_SP, RegNum::RN_RA, 0x4);
    *reinterpret_cast<uint32_t *>(Sled.Address + 44) = encodeInstruction(
        PatchOpcodes::PO_ADDIU, RegNum::RN_SP, RegNum::RN_SP, 0x8);
    uint32_t CreateStackSpaceInstr = encodeInstruction(
        PatchOpcodes::PO_ADDIU, RegNum::RN_SP, RegNum::RN_SP, 0xFFF8);
    std::atomic_store_explicit(
        reinterpret_cast<std::atomic<uint32_t> *>(Sled.Address),
        uint32_t(CreateStackSpaceInstr), std::memory_order_release);
  } else {
    std::atomic_store_explicit(
        reinterpret_cast<std::atomic<uint32_t> *>(Sled.Address),
        uint32_t(PatchOpcodes::PO_B44), std::memory_order_release);
  }
  return true;
}

bool patchFunctionEntry(const bool Enable, const uint32_t FuncId,
                        const XRaySledEntry &Sled,
                        void (*Trampoline)()) XRAY_NEVER_INSTRUMENT {
  return patchSled(Enable, FuncId, Sled, Trampoline);
}

bool patchFunctionExit(const bool Enable, const uint32_t FuncId,
                       const XRaySledEntry &Sled) XRAY_NEVER_INSTRUMENT {
  return patchSled(Enable, FuncId, Sled, __xray_FunctionExit);
}

bool patchFunctionTailExit(const bool Enable, const uint32_t FuncId,
                           const XRaySledEntry &Sled) XRAY_NEVER_INSTRUMENT {
  // FIXME: In the future we'd need to distinguish between non-tail exits and
  // tail exits for better information preservation.
  return patchSled(Enable, FuncId, Sled, __xray_FunctionExit);
}

bool patchCustomEvent(const bool Enable, const uint32_t FuncId,
                      const XRaySledEntry &Sled) XRAY_NEVER_INSTRUMENT {
  // FIXME: Implement in mips?
  return false;
}

bool patchTypedEvent(const bool Enable, const uint32_t FuncId,
                     const XRaySledEntry &Sled) XRAY_NEVER_INSTRUMENT {
  // FIXME: Implement in mips?
  return false;
}

} // namespace __xray

extern "C" void __xray_ArgLoggerEntry() XRAY_NEVER_INSTRUMENT {
  // FIXME: this will have to be implemented in the trampoline assembly file
}
