//===-- RegisterContextDarwin_arm64.cpp ---------------------------*- C++
//-*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "RegisterContextDarwin_arm64.h"
#include "RegisterContextDarwinConstants.h"

#include "lldb/Target/Process.h"
#include "lldb/Target/Thread.h"
#include "lldb/Utility/DataBufferHeap.h"
#include "lldb/Utility/DataExtractor.h"
#include "lldb/Utility/Endian.h"
#include "lldb/Utility/Log.h"
#include "lldb/Utility/RegisterValue.h"
#include "lldb/Utility/Scalar.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/Support/Compiler.h"

#include "Plugins/Process/Utility/InstructionUtils.h"

// Support building against older versions of LLVM, this macro was added
// recently.
#ifndef LLVM_EXTENSION
#define LLVM_EXTENSION
#endif

#include "Utility/ARM64_DWARF_Registers.h"

using namespace lldb;
using namespace lldb_private;

#define GPR_OFFSET(idx) ((idx)*8)
#define GPR_OFFSET_NAME(reg)                                                   \
  (LLVM_EXTENSION offsetof(RegisterContextDarwin_arm64::GPR, reg))

#define FPU_OFFSET(idx) ((idx)*16 + sizeof(RegisterContextDarwin_arm64::GPR))
#define FPU_OFFSET_NAME(reg)                                                   \
  (LLVM_EXTENSION offsetof(RegisterContextDarwin_arm64::FPU, reg))

#define EXC_OFFSET_NAME(reg)                                                   \
  (LLVM_EXTENSION offsetof(RegisterContextDarwin_arm64::EXC, reg) +            \
   sizeof(RegisterContextDarwin_arm64::GPR) +                                  \
   sizeof(RegisterContextDarwin_arm64::FPU))
#define DBG_OFFSET_NAME(reg)                                                   \
  (LLVM_EXTENSION offsetof(RegisterContextDarwin_arm64::DBG, reg) +            \
   sizeof(RegisterContextDarwin_arm64::GPR) +                                  \
   sizeof(RegisterContextDarwin_arm64::FPU) +                                  \
   sizeof(RegisterContextDarwin_arm64::EXC))

#define DEFINE_DBG(reg, i)                                                     \
  #reg, NULL,                                                                  \
      sizeof(((RegisterContextDarwin_arm64::DBG *) NULL)->reg[i]),             \
              DBG_OFFSET_NAME(reg[i]), eEncodingUint, eFormatHex,              \
                              {LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM,       \
                               LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM,       \
                               LLDB_INVALID_REGNUM },                          \
                               NULL, NULL, NULL, 0
#define REG_CONTEXT_SIZE                                                       \
  (sizeof(RegisterContextDarwin_arm64::GPR) +                                  \
   sizeof(RegisterContextDarwin_arm64::FPU) +                                  \
   sizeof(RegisterContextDarwin_arm64::EXC))

//-----------------------------------------------------------------------------
// Include RegisterInfos_arm64 to declare our g_register_infos_arm64 structure.
//-----------------------------------------------------------------------------
#define DECLARE_REGISTER_INFOS_ARM64_STRUCT
#include "RegisterInfos_arm64.h"
#undef DECLARE_REGISTER_INFOS_ARM64_STRUCT

// General purpose registers
static uint32_t g_gpr_regnums[] = {
    gpr_x0,  gpr_x1,  gpr_x2,  gpr_x3,  gpr_x4,  gpr_x5,  gpr_x6,
    gpr_x7,  gpr_x8,  gpr_x9,  gpr_x10, gpr_x11, gpr_x12, gpr_x13,
    gpr_x14, gpr_x15, gpr_x16, gpr_x17, gpr_x18, gpr_x19, gpr_x20,
    gpr_x21, gpr_x22, gpr_x23, gpr_x24, gpr_x25, gpr_x26, gpr_x27,
    gpr_x28, gpr_fp,  gpr_lr,  gpr_sp,  gpr_pc,  gpr_cpsr};

// Floating point registers
static uint32_t g_fpu_regnums[] = {
    fpu_v0,  fpu_v1,  fpu_v2,  fpu_v3,  fpu_v4,   fpu_v5,  fpu_v6,
    fpu_v7,  fpu_v8,  fpu_v9,  fpu_v10, fpu_v11,  fpu_v12, fpu_v13,
    fpu_v14, fpu_v15, fpu_v16, fpu_v17, fpu_v18,  fpu_v19, fpu_v20,
    fpu_v21, fpu_v22, fpu_v23, fpu_v24, fpu_v25,  fpu_v26, fpu_v27,
    fpu_v28, fpu_v29, fpu_v30, fpu_v31, fpu_fpsr, fpu_fpcr};

// Exception registers

static uint32_t g_exc_regnums[] = {exc_far, exc_esr, exc_exception};

static size_t k_num_register_infos =
    llvm::array_lengthof(g_register_infos_arm64_le);

RegisterContextDarwin_arm64::RegisterContextDarwin_arm64(
    Thread &thread, uint32_t concrete_frame_idx)
    : RegisterContext(thread, concrete_frame_idx), gpr(), fpu(), exc() {
  uint32_t i;
  for (i = 0; i < kNumErrors; i++) {
    gpr_errs[i] = -1;
    fpu_errs[i] = -1;
    exc_errs[i] = -1;
  }
}

RegisterContextDarwin_arm64::~RegisterContextDarwin_arm64() {}

void RegisterContextDarwin_arm64::InvalidateAllRegisters() {
  InvalidateAllRegisterStates();
}

size_t RegisterContextDarwin_arm64::GetRegisterCount() {
  assert(k_num_register_infos == k_num_registers);
  return k_num_registers;
}

const RegisterInfo *
RegisterContextDarwin_arm64::GetRegisterInfoAtIndex(size_t reg) {
  assert(k_num_register_infos == k_num_registers);
  if (reg < k_num_registers)
    return &g_register_infos_arm64_le[reg];
  return NULL;
}

size_t RegisterContextDarwin_arm64::GetRegisterInfosCount() {
  return k_num_register_infos;
}

const RegisterInfo *RegisterContextDarwin_arm64::GetRegisterInfos() {
  return g_register_infos_arm64_le;
}

// Number of registers in each register set
const size_t k_num_gpr_registers = llvm::array_lengthof(g_gpr_regnums);
const size_t k_num_fpu_registers = llvm::array_lengthof(g_fpu_regnums);
const size_t k_num_exc_registers = llvm::array_lengthof(g_exc_regnums);

//----------------------------------------------------------------------
// Register set definitions. The first definitions at register set index of
// zero is for all registers, followed by other registers sets. The register
// information for the all register set need not be filled in.
//----------------------------------------------------------------------
static const RegisterSet g_reg_sets[] = {
    {
        "General Purpose Registers", "gpr", k_num_gpr_registers, g_gpr_regnums,
    },
    {"Floating Point Registers", "fpu", k_num_fpu_registers, g_fpu_regnums},
    {"Exception State Registers", "exc", k_num_exc_registers, g_exc_regnums}};

const size_t k_num_regsets = llvm::array_lengthof(g_reg_sets);

size_t RegisterContextDarwin_arm64::GetRegisterSetCount() {
  return k_num_regsets;
}

const RegisterSet *RegisterContextDarwin_arm64::GetRegisterSet(size_t reg_set) {
  if (reg_set < k_num_regsets)
    return &g_reg_sets[reg_set];
  return NULL;
}

//----------------------------------------------------------------------
// Register information definitions for arm64
//----------------------------------------------------------------------
int RegisterContextDarwin_arm64::GetSetForNativeRegNum(int reg) {
  if (reg < fpu_v0)
    return GPRRegSet;
  else if (reg < exc_far)
    return FPURegSet;
  else if (reg < k_num_registers)
    return EXCRegSet;
  return -1;
}

int RegisterContextDarwin_arm64::ReadGPR(bool force) {
  int set = GPRRegSet;
  if (force || !RegisterSetIsCached(set)) {
    SetError(set, Read, DoReadGPR(GetThreadID(), set, gpr));
  }
  return GetError(GPRRegSet, Read);
}

int RegisterContextDarwin_arm64::ReadFPU(bool force) {
  int set = FPURegSet;
  if (force || !RegisterSetIsCached(set)) {
    SetError(set, Read, DoReadFPU(GetThreadID(), set, fpu));
  }
  return GetError(FPURegSet, Read);
}

int RegisterContextDarwin_arm64::ReadEXC(bool force) {
  int set = EXCRegSet;
  if (force || !RegisterSetIsCached(set)) {
    SetError(set, Read, DoReadEXC(GetThreadID(), set, exc));
  }
  return GetError(EXCRegSet, Read);
}

int RegisterContextDarwin_arm64::ReadDBG(bool force) {
  int set = DBGRegSet;
  if (force || !RegisterSetIsCached(set)) {
    SetError(set, Read, DoReadDBG(GetThreadID(), set, dbg));
  }
  return GetError(DBGRegSet, Read);
}

int RegisterContextDarwin_arm64::WriteGPR() {
  int set = GPRRegSet;
  if (!RegisterSetIsCached(set)) {
    SetError(set, Write, -1);
    return KERN_INVALID_ARGUMENT;
  }
  SetError(set, Write, DoWriteGPR(GetThreadID(), set, gpr));
  SetError(set, Read, -1);
  return GetError(GPRRegSet, Write);
}

int RegisterContextDarwin_arm64::WriteFPU() {
  int set = FPURegSet;
  if (!RegisterSetIsCached(set)) {
    SetError(set, Write, -1);
    return KERN_INVALID_ARGUMENT;
  }
  SetError(set, Write, DoWriteFPU(GetThreadID(), set, fpu));
  SetError(set, Read, -1);
  return GetError(FPURegSet, Write);
}

int RegisterContextDarwin_arm64::WriteEXC() {
  int set = EXCRegSet;
  if (!RegisterSetIsCached(set)) {
    SetError(set, Write, -1);
    return KERN_INVALID_ARGUMENT;
  }
  SetError(set, Write, DoWriteEXC(GetThreadID(), set, exc));
  SetError(set, Read, -1);
  return GetError(EXCRegSet, Write);
}

int RegisterContextDarwin_arm64::WriteDBG() {
  int set = DBGRegSet;
  if (!RegisterSetIsCached(set)) {
    SetError(set, Write, -1);
    return KERN_INVALID_ARGUMENT;
  }
  SetError(set, Write, DoWriteDBG(GetThreadID(), set, dbg));
  SetError(set, Read, -1);
  return GetError(DBGRegSet, Write);
}

int RegisterContextDarwin_arm64::ReadRegisterSet(uint32_t set, bool force) {
  switch (set) {
  case GPRRegSet:
    return ReadGPR(force);
  case FPURegSet:
    return ReadFPU(force);
  case EXCRegSet:
    return ReadEXC(force);
  case DBGRegSet:
    return ReadDBG(force);
  default:
    break;
  }
  return KERN_INVALID_ARGUMENT;
}

int RegisterContextDarwin_arm64::WriteRegisterSet(uint32_t set) {
  // Make sure we have a valid context to set.
  if (RegisterSetIsCached(set)) {
    switch (set) {
    case GPRRegSet:
      return WriteGPR();
    case FPURegSet:
      return WriteFPU();
    case EXCRegSet:
      return WriteEXC();
    case DBGRegSet:
      return WriteDBG();
    default:
      break;
    }
  }
  return KERN_INVALID_ARGUMENT;
}

void RegisterContextDarwin_arm64::LogDBGRegisters(Log *log, const DBG &dbg) {
  if (log) {
    for (uint32_t i = 0; i < 16; i++)
      log->Printf("BVR%-2u/BCR%-2u = { 0x%8.8" PRIu64 ", 0x%8.8" PRIu64
                  " } WVR%-2u/WCR%-2u "
                  "= { 0x%8.8" PRIu64 ", 0x%8.8" PRIu64 " }",
                  i, i, dbg.bvr[i], dbg.bcr[i], i, i, dbg.wvr[i], dbg.wcr[i]);
  }
}

bool RegisterContextDarwin_arm64::ReadRegister(const RegisterInfo *reg_info,
                                               RegisterValue &value) {
  const uint32_t reg = reg_info->kinds[eRegisterKindLLDB];
  int set = RegisterContextDarwin_arm64::GetSetForNativeRegNum(reg);

  if (set == -1)
    return false;

  if (ReadRegisterSet(set, false) != KERN_SUCCESS)
    return false;

  switch (reg) {
  case gpr_x0:
  case gpr_x1:
  case gpr_x2:
  case gpr_x3:
  case gpr_x4:
  case gpr_x5:
  case gpr_x6:
  case gpr_x7:
  case gpr_x8:
  case gpr_x9:
  case gpr_x10:
  case gpr_x11:
  case gpr_x12:
  case gpr_x13:
  case gpr_x14:
  case gpr_x15:
  case gpr_x16:
  case gpr_x17:
  case gpr_x18:
  case gpr_x19:
  case gpr_x20:
  case gpr_x21:
  case gpr_x22:
  case gpr_x23:
  case gpr_x24:
  case gpr_x25:
  case gpr_x26:
  case gpr_x27:
  case gpr_x28:
    value.SetUInt64(gpr.x[reg - gpr_x0]);
    break;
  case gpr_fp:
    value.SetUInt64(gpr.fp);
    break;
  case gpr_sp:
    value.SetUInt64(gpr.sp);
    break;
  case gpr_lr:
    value.SetUInt64(gpr.lr);
    break;
  case gpr_pc:
    value.SetUInt64(gpr.pc);
    break;
  case gpr_cpsr:
    value.SetUInt64(gpr.cpsr);
    break;

  case gpr_w0:
  case gpr_w1:
  case gpr_w2:
  case gpr_w3:
  case gpr_w4:
  case gpr_w5:
  case gpr_w6:
  case gpr_w7:
  case gpr_w8:
  case gpr_w9:
  case gpr_w10:
  case gpr_w11:
  case gpr_w12:
  case gpr_w13:
  case gpr_w14:
  case gpr_w15:
  case gpr_w16:
  case gpr_w17:
  case gpr_w18:
  case gpr_w19:
  case gpr_w20:
  case gpr_w21:
  case gpr_w22:
  case gpr_w23:
  case gpr_w24:
  case gpr_w25:
  case gpr_w26:
  case gpr_w27:
  case gpr_w28: {
    ProcessSP process_sp(m_thread.GetProcess());
    if (process_sp.get()) {
      DataExtractor regdata(&gpr.x[reg - gpr_w0], 8, process_sp->GetByteOrder(),
                            process_sp->GetAddressByteSize());
      offset_t offset = 0;
      uint64_t retval = regdata.GetMaxU64(&offset, 8);
      uint32_t retval_lower32 = static_cast<uint32_t>(retval & 0xffffffff);
      value.SetUInt32(retval_lower32);
    }
  } break;

  case fpu_v0:
  case fpu_v1:
  case fpu_v2:
  case fpu_v3:
  case fpu_v4:
  case fpu_v5:
  case fpu_v6:
  case fpu_v7:
  case fpu_v8:
  case fpu_v9:
  case fpu_v10:
  case fpu_v11:
  case fpu_v12:
  case fpu_v13:
  case fpu_v14:
  case fpu_v15:
  case fpu_v16:
  case fpu_v17:
  case fpu_v18:
  case fpu_v19:
  case fpu_v20:
  case fpu_v21:
  case fpu_v22:
  case fpu_v23:
  case fpu_v24:
  case fpu_v25:
  case fpu_v26:
  case fpu_v27:
  case fpu_v28:
  case fpu_v29:
  case fpu_v30:
  case fpu_v31:
    value.SetBytes(fpu.v[reg].bytes.buffer, reg_info->byte_size,
                   endian::InlHostByteOrder());
    break;

  case fpu_s0:
  case fpu_s1:
  case fpu_s2:
  case fpu_s3:
  case fpu_s4:
  case fpu_s5:
  case fpu_s6:
  case fpu_s7:
  case fpu_s8:
  case fpu_s9:
  case fpu_s10:
  case fpu_s11:
  case fpu_s12:
  case fpu_s13:
  case fpu_s14:
  case fpu_s15:
  case fpu_s16:
  case fpu_s17:
  case fpu_s18:
  case fpu_s19:
  case fpu_s20:
  case fpu_s21:
  case fpu_s22:
  case fpu_s23:
  case fpu_s24:
  case fpu_s25:
  case fpu_s26:
  case fpu_s27:
  case fpu_s28:
  case fpu_s29:
  case fpu_s30:
  case fpu_s31: {
    ProcessSP process_sp(m_thread.GetProcess());
    if (process_sp.get()) {
      DataExtractor regdata(&fpu.v[reg - fpu_s0], 4, process_sp->GetByteOrder(),
                            process_sp->GetAddressByteSize());
      offset_t offset = 0;
      value.SetFloat(regdata.GetFloat(&offset));
    }
  } break;

  case fpu_d0:
  case fpu_d1:
  case fpu_d2:
  case fpu_d3:
  case fpu_d4:
  case fpu_d5:
  case fpu_d6:
  case fpu_d7:
  case fpu_d8:
  case fpu_d9:
  case fpu_d10:
  case fpu_d11:
  case fpu_d12:
  case fpu_d13:
  case fpu_d14:
  case fpu_d15:
  case fpu_d16:
  case fpu_d17:
  case fpu_d18:
  case fpu_d19:
  case fpu_d20:
  case fpu_d21:
  case fpu_d22:
  case fpu_d23:
  case fpu_d24:
  case fpu_d25:
  case fpu_d26:
  case fpu_d27:
  case fpu_d28:
  case fpu_d29:
  case fpu_d30:
  case fpu_d31: {
    ProcessSP process_sp(m_thread.GetProcess());
    if (process_sp.get()) {
      DataExtractor regdata(&fpu.v[reg - fpu_s0], 8, process_sp->GetByteOrder(),
                            process_sp->GetAddressByteSize());
      offset_t offset = 0;
      value.SetDouble(regdata.GetDouble(&offset));
    }
  } break;

  case fpu_fpsr:
    value.SetUInt32(fpu.fpsr);
    break;

  case fpu_fpcr:
    value.SetUInt32(fpu.fpcr);
    break;

  case exc_exception:
    value.SetUInt32(exc.exception);
    break;
  case exc_esr:
    value.SetUInt32(exc.esr);
    break;
  case exc_far:
    value.SetUInt64(exc.far);
    break;

  default:
    value.SetValueToInvalid();
    return false;
  }
  return true;
}

bool RegisterContextDarwin_arm64::WriteRegister(const RegisterInfo *reg_info,
                                                const RegisterValue &value) {
  const uint32_t reg = reg_info->kinds[eRegisterKindLLDB];
  int set = GetSetForNativeRegNum(reg);

  if (set == -1)
    return false;

  if (ReadRegisterSet(set, false) != KERN_SUCCESS)
    return false;

  switch (reg) {
  case gpr_x0:
  case gpr_x1:
  case gpr_x2:
  case gpr_x3:
  case gpr_x4:
  case gpr_x5:
  case gpr_x6:
  case gpr_x7:
  case gpr_x8:
  case gpr_x9:
  case gpr_x10:
  case gpr_x11:
  case gpr_x12:
  case gpr_x13:
  case gpr_x14:
  case gpr_x15:
  case gpr_x16:
  case gpr_x17:
  case gpr_x18:
  case gpr_x19:
  case gpr_x20:
  case gpr_x21:
  case gpr_x22:
  case gpr_x23:
  case gpr_x24:
  case gpr_x25:
  case gpr_x26:
  case gpr_x27:
  case gpr_x28:
  case gpr_fp:
  case gpr_sp:
  case gpr_lr:
  case gpr_pc:
  case gpr_cpsr:
    gpr.x[reg - gpr_x0] = value.GetAsUInt64();
    break;

  case fpu_v0:
  case fpu_v1:
  case fpu_v2:
  case fpu_v3:
  case fpu_v4:
  case fpu_v5:
  case fpu_v6:
  case fpu_v7:
  case fpu_v8:
  case fpu_v9:
  case fpu_v10:
  case fpu_v11:
  case fpu_v12:
  case fpu_v13:
  case fpu_v14:
  case fpu_v15:
  case fpu_v16:
  case fpu_v17:
  case fpu_v18:
  case fpu_v19:
  case fpu_v20:
  case fpu_v21:
  case fpu_v22:
  case fpu_v23:
  case fpu_v24:
  case fpu_v25:
  case fpu_v26:
  case fpu_v27:
  case fpu_v28:
  case fpu_v29:
  case fpu_v30:
  case fpu_v31:
    ::memcpy(fpu.v[reg].bytes.buffer, value.GetBytes(), value.GetByteSize());
    break;

  case fpu_fpsr:
    fpu.fpsr = value.GetAsUInt32();
    break;

  case fpu_fpcr:
    fpu.fpcr = value.GetAsUInt32();
    break;

  case exc_exception:
    exc.exception = value.GetAsUInt32();
    break;
  case exc_esr:
    exc.esr = value.GetAsUInt32();
    break;
  case exc_far:
    exc.far = value.GetAsUInt64();
    break;

  default:
    return false;
  }
  return WriteRegisterSet(set) == KERN_SUCCESS;
}

bool RegisterContextDarwin_arm64::ReadAllRegisterValues(
    lldb::DataBufferSP &data_sp) {
  data_sp.reset(new DataBufferHeap(REG_CONTEXT_SIZE, 0));
  if (data_sp && ReadGPR(false) == KERN_SUCCESS &&
      ReadFPU(false) == KERN_SUCCESS && ReadEXC(false) == KERN_SUCCESS) {
    uint8_t *dst = data_sp->GetBytes();
    ::memcpy(dst, &gpr, sizeof(gpr));
    dst += sizeof(gpr);

    ::memcpy(dst, &fpu, sizeof(fpu));
    dst += sizeof(gpr);

    ::memcpy(dst, &exc, sizeof(exc));
    return true;
  }
  return false;
}

bool RegisterContextDarwin_arm64::WriteAllRegisterValues(
    const lldb::DataBufferSP &data_sp) {
  if (data_sp && data_sp->GetByteSize() == REG_CONTEXT_SIZE) {
    const uint8_t *src = data_sp->GetBytes();
    ::memcpy(&gpr, src, sizeof(gpr));
    src += sizeof(gpr);

    ::memcpy(&fpu, src, sizeof(fpu));
    src += sizeof(gpr);

    ::memcpy(&exc, src, sizeof(exc));
    uint32_t success_count = 0;
    if (WriteGPR() == KERN_SUCCESS)
      ++success_count;
    if (WriteFPU() == KERN_SUCCESS)
      ++success_count;
    if (WriteEXC() == KERN_SUCCESS)
      ++success_count;
    return success_count == 3;
  }
  return false;
}

uint32_t RegisterContextDarwin_arm64::ConvertRegisterKindToRegisterNumber(
    RegisterKind kind, uint32_t reg) {
  if (kind == eRegisterKindGeneric) {
    switch (reg) {
    case LLDB_REGNUM_GENERIC_PC:
      return gpr_pc;
    case LLDB_REGNUM_GENERIC_SP:
      return gpr_sp;
    case LLDB_REGNUM_GENERIC_FP:
      return gpr_fp;
    case LLDB_REGNUM_GENERIC_RA:
      return gpr_lr;
    case LLDB_REGNUM_GENERIC_FLAGS:
      return gpr_cpsr;
    default:
      break;
    }
  } else if (kind == eRegisterKindDWARF) {
    switch (reg) {
    case arm64_dwarf::x0:
      return gpr_x0;
    case arm64_dwarf::x1:
      return gpr_x1;
    case arm64_dwarf::x2:
      return gpr_x2;
    case arm64_dwarf::x3:
      return gpr_x3;
    case arm64_dwarf::x4:
      return gpr_x4;
    case arm64_dwarf::x5:
      return gpr_x5;
    case arm64_dwarf::x6:
      return gpr_x6;
    case arm64_dwarf::x7:
      return gpr_x7;
    case arm64_dwarf::x8:
      return gpr_x8;
    case arm64_dwarf::x9:
      return gpr_x9;
    case arm64_dwarf::x10:
      return gpr_x10;
    case arm64_dwarf::x11:
      return gpr_x11;
    case arm64_dwarf::x12:
      return gpr_x12;
    case arm64_dwarf::x13:
      return gpr_x13;
    case arm64_dwarf::x14:
      return gpr_x14;
    case arm64_dwarf::x15:
      return gpr_x15;
    case arm64_dwarf::x16:
      return gpr_x16;
    case arm64_dwarf::x17:
      return gpr_x17;
    case arm64_dwarf::x18:
      return gpr_x18;
    case arm64_dwarf::x19:
      return gpr_x19;
    case arm64_dwarf::x20:
      return gpr_x20;
    case arm64_dwarf::x21:
      return gpr_x21;
    case arm64_dwarf::x22:
      return gpr_x22;
    case arm64_dwarf::x23:
      return gpr_x23;
    case arm64_dwarf::x24:
      return gpr_x24;
    case arm64_dwarf::x25:
      return gpr_x25;
    case arm64_dwarf::x26:
      return gpr_x26;
    case arm64_dwarf::x27:
      return gpr_x27;
    case arm64_dwarf::x28:
      return gpr_x28;

    case arm64_dwarf::fp:
      return gpr_fp;
    case arm64_dwarf::sp:
      return gpr_sp;
    case arm64_dwarf::lr:
      return gpr_lr;
    case arm64_dwarf::pc:
      return gpr_pc;
    case arm64_dwarf::cpsr:
      return gpr_cpsr;

    case arm64_dwarf::v0:
      return fpu_v0;
    case arm64_dwarf::v1:
      return fpu_v1;
    case arm64_dwarf::v2:
      return fpu_v2;
    case arm64_dwarf::v3:
      return fpu_v3;
    case arm64_dwarf::v4:
      return fpu_v4;
    case arm64_dwarf::v5:
      return fpu_v5;
    case arm64_dwarf::v6:
      return fpu_v6;
    case arm64_dwarf::v7:
      return fpu_v7;
    case arm64_dwarf::v8:
      return fpu_v8;
    case arm64_dwarf::v9:
      return fpu_v9;
    case arm64_dwarf::v10:
      return fpu_v10;
    case arm64_dwarf::v11:
      return fpu_v11;
    case arm64_dwarf::v12:
      return fpu_v12;
    case arm64_dwarf::v13:
      return fpu_v13;
    case arm64_dwarf::v14:
      return fpu_v14;
    case arm64_dwarf::v15:
      return fpu_v15;
    case arm64_dwarf::v16:
      return fpu_v16;
    case arm64_dwarf::v17:
      return fpu_v17;
    case arm64_dwarf::v18:
      return fpu_v18;
    case arm64_dwarf::v19:
      return fpu_v19;
    case arm64_dwarf::v20:
      return fpu_v20;
    case arm64_dwarf::v21:
      return fpu_v21;
    case arm64_dwarf::v22:
      return fpu_v22;
    case arm64_dwarf::v23:
      return fpu_v23;
    case arm64_dwarf::v24:
      return fpu_v24;
    case arm64_dwarf::v25:
      return fpu_v25;
    case arm64_dwarf::v26:
      return fpu_v26;
    case arm64_dwarf::v27:
      return fpu_v27;
    case arm64_dwarf::v28:
      return fpu_v28;
    case arm64_dwarf::v29:
      return fpu_v29;
    case arm64_dwarf::v30:
      return fpu_v30;
    case arm64_dwarf::v31:
      return fpu_v31;

    default:
      break;
    }
  } else if (kind == eRegisterKindEHFrame) {
    switch (reg) {
    case arm64_ehframe::x0:
      return gpr_x0;
    case arm64_ehframe::x1:
      return gpr_x1;
    case arm64_ehframe::x2:
      return gpr_x2;
    case arm64_ehframe::x3:
      return gpr_x3;
    case arm64_ehframe::x4:
      return gpr_x4;
    case arm64_ehframe::x5:
      return gpr_x5;
    case arm64_ehframe::x6:
      return gpr_x6;
    case arm64_ehframe::x7:
      return gpr_x7;
    case arm64_ehframe::x8:
      return gpr_x8;
    case arm64_ehframe::x9:
      return gpr_x9;
    case arm64_ehframe::x10:
      return gpr_x10;
    case arm64_ehframe::x11:
      return gpr_x11;
    case arm64_ehframe::x12:
      return gpr_x12;
    case arm64_ehframe::x13:
      return gpr_x13;
    case arm64_ehframe::x14:
      return gpr_x14;
    case arm64_ehframe::x15:
      return gpr_x15;
    case arm64_ehframe::x16:
      return gpr_x16;
    case arm64_ehframe::x17:
      return gpr_x17;
    case arm64_ehframe::x18:
      return gpr_x18;
    case arm64_ehframe::x19:
      return gpr_x19;
    case arm64_ehframe::x20:
      return gpr_x20;
    case arm64_ehframe::x21:
      return gpr_x21;
    case arm64_ehframe::x22:
      return gpr_x22;
    case arm64_ehframe::x23:
      return gpr_x23;
    case arm64_ehframe::x24:
      return gpr_x24;
    case arm64_ehframe::x25:
      return gpr_x25;
    case arm64_ehframe::x26:
      return gpr_x26;
    case arm64_ehframe::x27:
      return gpr_x27;
    case arm64_ehframe::x28:
      return gpr_x28;
    case arm64_ehframe::fp:
      return gpr_fp;
    case arm64_ehframe::sp:
      return gpr_sp;
    case arm64_ehframe::lr:
      return gpr_lr;
    case arm64_ehframe::pc:
      return gpr_pc;
    case arm64_ehframe::cpsr:
      return gpr_cpsr;
    }
  } else if (kind == eRegisterKindLLDB) {
    return reg;
  }
  return LLDB_INVALID_REGNUM;
}

uint32_t RegisterContextDarwin_arm64::NumSupportedHardwareWatchpoints() {
#if defined(__APPLE__) && (defined(__arm64__) || defined(__aarch64__))
  // autodetect how many watchpoints are supported dynamically...
  static uint32_t g_num_supported_hw_watchpoints = UINT32_MAX;
  if (g_num_supported_hw_watchpoints == UINT32_MAX) {
    size_t len;
    uint32_t n = 0;
    len = sizeof(n);
    if (::sysctlbyname("hw.optional.watchpoint", &n, &len, NULL, 0) == 0) {
      g_num_supported_hw_watchpoints = n;
    }
  }
  return g_num_supported_hw_watchpoints;
#else
  // TODO: figure out remote case here!
  return 2;
#endif
}

uint32_t RegisterContextDarwin_arm64::SetHardwareWatchpoint(lldb::addr_t addr,
                                                            size_t size,
                                                            bool read,
                                                            bool write) {
  //    if (log) log->Printf
  //    ("RegisterContextDarwin_arm64::EnableHardwareWatchpoint(addr = %8.8p,
  //    size = %u, read = %u, write = %u)", addr, size, read, write);

  const uint32_t num_hw_watchpoints = NumSupportedHardwareWatchpoints();

  // Can't watch zero bytes
  if (size == 0)
    return LLDB_INVALID_INDEX32;

  // We must watch for either read or write
  if (!read && !write)
    return LLDB_INVALID_INDEX32;

  // Can't watch more than 4 bytes per WVR/WCR pair
  if (size > 4)
    return LLDB_INVALID_INDEX32;

  // We can only watch up to four bytes that follow a 4 byte aligned address
  // per watchpoint register pair. Since we have at most so we can only watch
  // until the next 4 byte boundary and we need to make sure we can properly
  // encode this.
  uint32_t addr_word_offset = addr % 4;
  //    if (log) log->Printf
  //    ("RegisterContextDarwin_arm64::EnableHardwareWatchpoint() -
  //    addr_word_offset = 0x%8.8x", addr_word_offset);

  uint32_t byte_mask = ((1u << size) - 1u) << addr_word_offset;
  //    if (log) log->Printf
  //    ("RegisterContextDarwin_arm64::EnableHardwareWatchpoint() - byte_mask =
  //    0x%8.8x", byte_mask);
  if (byte_mask > 0xfu)
    return LLDB_INVALID_INDEX32;

  // Read the debug state
  int kret = ReadDBG(false);

  if (kret == KERN_SUCCESS) {
    // Check to make sure we have the needed hardware support
    uint32_t i = 0;

    for (i = 0; i < num_hw_watchpoints; ++i) {
      if ((dbg.wcr[i] & WCR_ENABLE) == 0)
        break; // We found an available hw breakpoint slot (in i)
    }

    // See if we found an available hw breakpoint slot above
    if (i < num_hw_watchpoints) {
      // Make the byte_mask into a valid Byte Address Select mask
      uint32_t byte_address_select = byte_mask << 5;
      // Make sure bits 1:0 are clear in our address
      dbg.wvr[i] = addr & ~((lldb::addr_t)3);
      dbg.wcr[i] = byte_address_select |     // Which bytes that follow the IMVA
                                             // that we will watch
                   S_USER |                  // Stop only in user mode
                   (read ? WCR_LOAD : 0) |   // Stop on read access?
                   (write ? WCR_STORE : 0) | // Stop on write access?
                   WCR_ENABLE;               // Enable this watchpoint;

      kret = WriteDBG();
      //            if (log) log->Printf
      //            ("RegisterContextDarwin_arm64::EnableHardwareWatchpoint()
      //            WriteDBG() => 0x%8.8x.", kret);

      if (kret == KERN_SUCCESS)
        return i;
    } else {
      //            if (log) log->Printf
      //            ("RegisterContextDarwin_arm64::EnableHardwareWatchpoint():
      //            All hardware resources (%u) are in use.",
      //            num_hw_watchpoints);
    }
  }
  return LLDB_INVALID_INDEX32;
}

bool RegisterContextDarwin_arm64::ClearHardwareWatchpoint(uint32_t hw_index) {
  int kret = ReadDBG(false);

  const uint32_t num_hw_points = NumSupportedHardwareWatchpoints();
  if (kret == KERN_SUCCESS) {
    if (hw_index < num_hw_points) {
      dbg.wcr[hw_index] = 0;
      //            if (log) log->Printf
      //            ("RegisterContextDarwin_arm64::ClearHardwareWatchpoint( %u )
      //            - WVR%u = 0x%8.8x  WCR%u = 0x%8.8x",
      //                    hw_index,
      //                    hw_index,
      //                    dbg.wvr[hw_index],
      //                    hw_index,
      //                    dbg.wcr[hw_index]);

      kret = WriteDBG();

      if (kret == KERN_SUCCESS)
        return true;
    }
  }
  return false;
}
