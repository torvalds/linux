//===-- WebAssemblyUtilities.cpp - WebAssembly Utility Functions ----------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file implements several utility functions for WebAssembly.
///
//===----------------------------------------------------------------------===//

#include "WebAssemblyUtilities.h"
#include "WebAssemblyMachineFunctionInfo.h"
#include "llvm/CodeGen/MachineInstr.h"
#include "llvm/CodeGen/MachineLoopInfo.h"
using namespace llvm;

const char *const WebAssembly::ClangCallTerminateFn = "__clang_call_terminate";
const char *const WebAssembly::CxaBeginCatchFn = "__cxa_begin_catch";
const char *const WebAssembly::CxaRethrowFn = "__cxa_rethrow";
const char *const WebAssembly::StdTerminateFn = "_ZSt9terminatev";
const char *const WebAssembly::PersonalityWrapperFn =
    "_Unwind_Wasm_CallPersonality";

bool WebAssembly::isArgument(const MachineInstr &MI) {
  switch (MI.getOpcode()) {
  case WebAssembly::ARGUMENT_i32:
  case WebAssembly::ARGUMENT_i32_S:
  case WebAssembly::ARGUMENT_i64:
  case WebAssembly::ARGUMENT_i64_S:
  case WebAssembly::ARGUMENT_f32:
  case WebAssembly::ARGUMENT_f32_S:
  case WebAssembly::ARGUMENT_f64:
  case WebAssembly::ARGUMENT_f64_S:
  case WebAssembly::ARGUMENT_v16i8:
  case WebAssembly::ARGUMENT_v16i8_S:
  case WebAssembly::ARGUMENT_v8i16:
  case WebAssembly::ARGUMENT_v8i16_S:
  case WebAssembly::ARGUMENT_v4i32:
  case WebAssembly::ARGUMENT_v4i32_S:
  case WebAssembly::ARGUMENT_v2i64:
  case WebAssembly::ARGUMENT_v2i64_S:
  case WebAssembly::ARGUMENT_v4f32:
  case WebAssembly::ARGUMENT_v4f32_S:
  case WebAssembly::ARGUMENT_v2f64:
  case WebAssembly::ARGUMENT_v2f64_S:
    return true;
  default:
    return false;
  }
}

bool WebAssembly::isCopy(const MachineInstr &MI) {
  switch (MI.getOpcode()) {
  case WebAssembly::COPY_I32:
  case WebAssembly::COPY_I32_S:
  case WebAssembly::COPY_I64:
  case WebAssembly::COPY_I64_S:
  case WebAssembly::COPY_F32:
  case WebAssembly::COPY_F32_S:
  case WebAssembly::COPY_F64:
  case WebAssembly::COPY_F64_S:
  case WebAssembly::COPY_V128:
  case WebAssembly::COPY_V128_S:
    return true;
  default:
    return false;
  }
}

bool WebAssembly::isTee(const MachineInstr &MI) {
  switch (MI.getOpcode()) {
  case WebAssembly::TEE_I32:
  case WebAssembly::TEE_I32_S:
  case WebAssembly::TEE_I64:
  case WebAssembly::TEE_I64_S:
  case WebAssembly::TEE_F32:
  case WebAssembly::TEE_F32_S:
  case WebAssembly::TEE_F64:
  case WebAssembly::TEE_F64_S:
  case WebAssembly::TEE_V128:
  case WebAssembly::TEE_V128_S:
    return true;
  default:
    return false;
  }
}

/// Test whether MI is a child of some other node in an expression tree.
bool WebAssembly::isChild(const MachineInstr &MI,
                          const WebAssemblyFunctionInfo &MFI) {
  if (MI.getNumOperands() == 0)
    return false;
  const MachineOperand &MO = MI.getOperand(0);
  if (!MO.isReg() || MO.isImplicit() || !MO.isDef())
    return false;
  unsigned Reg = MO.getReg();
  return TargetRegisterInfo::isVirtualRegister(Reg) &&
         MFI.isVRegStackified(Reg);
}

bool WebAssembly::isCallDirect(const MachineInstr &MI) {
  switch (MI.getOpcode()) {
  case WebAssembly::CALL_VOID:
  case WebAssembly::CALL_VOID_S:
  case WebAssembly::CALL_I32:
  case WebAssembly::CALL_I32_S:
  case WebAssembly::CALL_I64:
  case WebAssembly::CALL_I64_S:
  case WebAssembly::CALL_F32:
  case WebAssembly::CALL_F32_S:
  case WebAssembly::CALL_F64:
  case WebAssembly::CALL_F64_S:
  case WebAssembly::CALL_v16i8:
  case WebAssembly::CALL_v16i8_S:
  case WebAssembly::CALL_v8i16:
  case WebAssembly::CALL_v8i16_S:
  case WebAssembly::CALL_v4i32:
  case WebAssembly::CALL_v4i32_S:
  case WebAssembly::CALL_v2i64:
  case WebAssembly::CALL_v2i64_S:
  case WebAssembly::CALL_v4f32:
  case WebAssembly::CALL_v4f32_S:
  case WebAssembly::CALL_v2f64:
  case WebAssembly::CALL_v2f64_S:
  case WebAssembly::CALL_EXCEPT_REF:
  case WebAssembly::CALL_EXCEPT_REF_S:
    return true;
  default:
    return false;
  }
}

bool WebAssembly::isCallIndirect(const MachineInstr &MI) {
  switch (MI.getOpcode()) {
  case WebAssembly::CALL_INDIRECT_VOID:
  case WebAssembly::CALL_INDIRECT_VOID_S:
  case WebAssembly::CALL_INDIRECT_I32:
  case WebAssembly::CALL_INDIRECT_I32_S:
  case WebAssembly::CALL_INDIRECT_I64:
  case WebAssembly::CALL_INDIRECT_I64_S:
  case WebAssembly::CALL_INDIRECT_F32:
  case WebAssembly::CALL_INDIRECT_F32_S:
  case WebAssembly::CALL_INDIRECT_F64:
  case WebAssembly::CALL_INDIRECT_F64_S:
  case WebAssembly::CALL_INDIRECT_v16i8:
  case WebAssembly::CALL_INDIRECT_v16i8_S:
  case WebAssembly::CALL_INDIRECT_v8i16:
  case WebAssembly::CALL_INDIRECT_v8i16_S:
  case WebAssembly::CALL_INDIRECT_v4i32:
  case WebAssembly::CALL_INDIRECT_v4i32_S:
  case WebAssembly::CALL_INDIRECT_v2i64:
  case WebAssembly::CALL_INDIRECT_v2i64_S:
  case WebAssembly::CALL_INDIRECT_v4f32:
  case WebAssembly::CALL_INDIRECT_v4f32_S:
  case WebAssembly::CALL_INDIRECT_v2f64:
  case WebAssembly::CALL_INDIRECT_v2f64_S:
  case WebAssembly::CALL_INDIRECT_EXCEPT_REF:
  case WebAssembly::CALL_INDIRECT_EXCEPT_REF_S:
    return true;
  default:
    return false;
  }
}

unsigned WebAssembly::getCalleeOpNo(const MachineInstr &MI) {
  switch (MI.getOpcode()) {
  case WebAssembly::CALL_VOID:
  case WebAssembly::CALL_VOID_S:
  case WebAssembly::CALL_INDIRECT_VOID:
  case WebAssembly::CALL_INDIRECT_VOID_S:
    return 0;
  case WebAssembly::CALL_I32:
  case WebAssembly::CALL_I32_S:
  case WebAssembly::CALL_I64:
  case WebAssembly::CALL_I64_S:
  case WebAssembly::CALL_F32:
  case WebAssembly::CALL_F32_S:
  case WebAssembly::CALL_F64:
  case WebAssembly::CALL_F64_S:
  case WebAssembly::CALL_v16i8:
  case WebAssembly::CALL_v16i8_S:
  case WebAssembly::CALL_v8i16:
  case WebAssembly::CALL_v8i16_S:
  case WebAssembly::CALL_v4i32:
  case WebAssembly::CALL_v4i32_S:
  case WebAssembly::CALL_v2i64:
  case WebAssembly::CALL_v2i64_S:
  case WebAssembly::CALL_v4f32:
  case WebAssembly::CALL_v4f32_S:
  case WebAssembly::CALL_v2f64:
  case WebAssembly::CALL_v2f64_S:
  case WebAssembly::CALL_EXCEPT_REF:
  case WebAssembly::CALL_EXCEPT_REF_S:
  case WebAssembly::CALL_INDIRECT_I32:
  case WebAssembly::CALL_INDIRECT_I32_S:
  case WebAssembly::CALL_INDIRECT_I64:
  case WebAssembly::CALL_INDIRECT_I64_S:
  case WebAssembly::CALL_INDIRECT_F32:
  case WebAssembly::CALL_INDIRECT_F32_S:
  case WebAssembly::CALL_INDIRECT_F64:
  case WebAssembly::CALL_INDIRECT_F64_S:
  case WebAssembly::CALL_INDIRECT_v16i8:
  case WebAssembly::CALL_INDIRECT_v16i8_S:
  case WebAssembly::CALL_INDIRECT_v8i16:
  case WebAssembly::CALL_INDIRECT_v8i16_S:
  case WebAssembly::CALL_INDIRECT_v4i32:
  case WebAssembly::CALL_INDIRECT_v4i32_S:
  case WebAssembly::CALL_INDIRECT_v2i64:
  case WebAssembly::CALL_INDIRECT_v2i64_S:
  case WebAssembly::CALL_INDIRECT_v4f32:
  case WebAssembly::CALL_INDIRECT_v4f32_S:
  case WebAssembly::CALL_INDIRECT_v2f64:
  case WebAssembly::CALL_INDIRECT_v2f64_S:
  case WebAssembly::CALL_INDIRECT_EXCEPT_REF:
  case WebAssembly::CALL_INDIRECT_EXCEPT_REF_S:
    return 1;
  default:
    llvm_unreachable("Not a call instruction");
  }
}

bool WebAssembly::isMarker(const MachineInstr &MI) {
  switch (MI.getOpcode()) {
  case WebAssembly::BLOCK:
  case WebAssembly::BLOCK_S:
  case WebAssembly::END_BLOCK:
  case WebAssembly::END_BLOCK_S:
  case WebAssembly::LOOP:
  case WebAssembly::LOOP_S:
  case WebAssembly::END_LOOP:
  case WebAssembly::END_LOOP_S:
  case WebAssembly::TRY:
  case WebAssembly::TRY_S:
  case WebAssembly::END_TRY:
  case WebAssembly::END_TRY_S:
    return true;
  default:
    return false;
  }
}

bool WebAssembly::isThrow(const MachineInstr &MI) {
  switch (MI.getOpcode()) {
  case WebAssembly::THROW_I32:
  case WebAssembly::THROW_I32_S:
  case WebAssembly::THROW_I64:
  case WebAssembly::THROW_I64_S:
    return true;
  default:
    return false;
  }
}

bool WebAssembly::isRethrow(const MachineInstr &MI) {
  switch (MI.getOpcode()) {
  case WebAssembly::RETHROW:
  case WebAssembly::RETHROW_S:
  case WebAssembly::RETHROW_TO_CALLER:
  case WebAssembly::RETHROW_TO_CALLER_S:
    return true;
  default:
    return false;
  }
}

bool WebAssembly::isCatch(const MachineInstr &MI) {
  switch (MI.getOpcode()) {
  case WebAssembly::CATCH_I32:
  case WebAssembly::CATCH_I32_S:
  case WebAssembly::CATCH_I64:
  case WebAssembly::CATCH_I64_S:
  case WebAssembly::CATCH_ALL:
  case WebAssembly::CATCH_ALL_S:
    return true;
  default:
    return false;
  }
}

bool WebAssembly::mayThrow(const MachineInstr &MI) {
  switch (MI.getOpcode()) {
  case WebAssembly::THROW_I32:
  case WebAssembly::THROW_I32_S:
  case WebAssembly::THROW_I64:
  case WebAssembly::THROW_I64_S:
  case WebAssembly::RETHROW:
  case WebAssembly::RETHROW_S:
    return true;
  }
  if (isCallIndirect(MI))
    return true;
  if (!MI.isCall())
    return false;

  const MachineOperand &MO = MI.getOperand(getCalleeOpNo(MI));
  assert(MO.isGlobal());
  const auto *F = dyn_cast<Function>(MO.getGlobal());
  if (!F)
    return true;
  if (F->doesNotThrow())
    return false;
  // These functions never throw
  if (F->getName() == CxaBeginCatchFn || F->getName() == PersonalityWrapperFn ||
      F->getName() == ClangCallTerminateFn || F->getName() == StdTerminateFn)
    return false;
  return true;
}

bool WebAssembly::isCatchTerminatePad(const MachineBasicBlock &MBB) {
  if (!MBB.isEHPad())
    return false;
  bool SeenCatch = false;
  for (auto &MI : MBB) {
    if (MI.getOpcode() == WebAssembly::CATCH_I32 ||
        MI.getOpcode() == WebAssembly::CATCH_I64 ||
        MI.getOpcode() == WebAssembly::CATCH_I32_S ||
        MI.getOpcode() == WebAssembly::CATCH_I64_S)
      SeenCatch = true;
    if (SeenCatch && MI.isCall()) {
      const MachineOperand &CalleeOp = MI.getOperand(getCalleeOpNo(MI));
      if (CalleeOp.isGlobal() &&
          CalleeOp.getGlobal()->getName() == ClangCallTerminateFn)
        return true;
    }
  }
  return false;
}

bool WebAssembly::isCatchAllTerminatePad(const MachineBasicBlock &MBB) {
  if (!MBB.isEHPad())
    return false;
  bool SeenCatchAll = false;
  for (auto &MI : MBB) {
    if (MI.getOpcode() == WebAssembly::CATCH_ALL ||
        MI.getOpcode() == WebAssembly::CATCH_ALL_S)
      SeenCatchAll = true;
    if (SeenCatchAll && MI.isCall()) {
      const MachineOperand &CalleeOp = MI.getOperand(getCalleeOpNo(MI));
      if (CalleeOp.isGlobal() &&
          CalleeOp.getGlobal()->getName() == StdTerminateFn)
        return true;
    }
  }
  return false;
}
