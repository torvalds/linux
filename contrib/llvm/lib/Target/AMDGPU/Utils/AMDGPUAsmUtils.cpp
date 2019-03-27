//===-- AMDGPUAsmUtils.cpp - AsmParser/InstPrinter common -----------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
#include "AMDGPUAsmUtils.h"

namespace llvm {
namespace AMDGPU {
namespace SendMsg {

// This must be in sync with llvm::AMDGPU::SendMsg::Id enum members, see SIDefines.h.
const char* const IdSymbolic[] = {
  nullptr,
  "MSG_INTERRUPT",
  "MSG_GS",
  "MSG_GS_DONE",
  nullptr,
  nullptr,
  nullptr,
  nullptr,
  nullptr,
  nullptr,
  nullptr,
  nullptr,
  nullptr,
  nullptr,
  nullptr,
  "MSG_SYSMSG"
};

// These two must be in sync with llvm::AMDGPU::SendMsg::Op enum members, see SIDefines.h.
const char* const OpSysSymbolic[] = {
  nullptr,
  "SYSMSG_OP_ECC_ERR_INTERRUPT",
  "SYSMSG_OP_REG_RD",
  "SYSMSG_OP_HOST_TRAP_ACK",
  "SYSMSG_OP_TTRACE_PC"
};

const char* const OpGsSymbolic[] = {
  "GS_OP_NOP",
  "GS_OP_CUT",
  "GS_OP_EMIT",
  "GS_OP_EMIT_CUT"
};

} // namespace SendMsg

namespace Hwreg {

// This must be in sync with llvm::AMDGPU::Hwreg::ID_SYMBOLIC_FIRST_/LAST_, see SIDefines.h.
const char* const IdSymbolic[] = {
  nullptr,
  "HW_REG_MODE",
  "HW_REG_STATUS",
  "HW_REG_TRAPSTS",
  "HW_REG_HW_ID",
  "HW_REG_GPR_ALLOC",
  "HW_REG_LDS_ALLOC",
  "HW_REG_IB_STS",
  nullptr,
  nullptr,
  nullptr,
  nullptr,
  nullptr,
  nullptr,
  nullptr,
  "HW_REG_SH_MEM_BASES"
};

} // namespace Hwreg

namespace Swizzle {

// This must be in sync with llvm::AMDGPU::Swizzle::Id enum members, see SIDefines.h.
const char* const IdSymbolic[] = {
  "QUAD_PERM",
  "BITMASK_PERM",
  "SWAP",
  "REVERSE",
  "BROADCAST",
};

} // namespace Swizzle
} // namespace AMDGPU
} // namespace llvm
