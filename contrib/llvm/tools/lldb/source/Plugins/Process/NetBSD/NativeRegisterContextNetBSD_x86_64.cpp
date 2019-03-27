//===-- NativeRegisterContextNetBSD_x86_64.cpp ---------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#if defined(__x86_64__)

#include "NativeRegisterContextNetBSD_x86_64.h"

#include "lldb/Host/HostInfo.h"
#include "lldb/Utility/DataBufferHeap.h"
#include "lldb/Utility/Log.h"
#include "lldb/Utility/RegisterValue.h"
#include "lldb/Utility/Status.h"

#include "Plugins/Process/Utility/RegisterContextNetBSD_x86_64.h"

// clang-format off
#include <sys/types.h>
#include <sys/sysctl.h>
#include <x86/cpu.h>
#include <elf.h>
#include <err.h>
#include <stdint.h>
#include <stdlib.h>
// clang-format on

using namespace lldb_private;
using namespace lldb_private::process_netbsd;

// ----------------------------------------------------------------------------
// Private namespace.
// ----------------------------------------------------------------------------

namespace {
// x86 64-bit general purpose registers.
static const uint32_t g_gpr_regnums_x86_64[] = {
    lldb_rax_x86_64,    lldb_rbx_x86_64,    lldb_rcx_x86_64, lldb_rdx_x86_64,
    lldb_rdi_x86_64,    lldb_rsi_x86_64,    lldb_rbp_x86_64, lldb_rsp_x86_64,
    lldb_r8_x86_64,     lldb_r9_x86_64,     lldb_r10_x86_64, lldb_r11_x86_64,
    lldb_r12_x86_64,    lldb_r13_x86_64,    lldb_r14_x86_64, lldb_r15_x86_64,
    lldb_rip_x86_64,    lldb_rflags_x86_64, lldb_cs_x86_64,  lldb_fs_x86_64,
    lldb_gs_x86_64,     lldb_ss_x86_64,     lldb_ds_x86_64,  lldb_es_x86_64,
    lldb_eax_x86_64,    lldb_ebx_x86_64,    lldb_ecx_x86_64, lldb_edx_x86_64,
    lldb_edi_x86_64,    lldb_esi_x86_64,    lldb_ebp_x86_64, lldb_esp_x86_64,
    lldb_r8d_x86_64,  // Low 32 bits or r8
    lldb_r9d_x86_64,  // Low 32 bits or r9
    lldb_r10d_x86_64, // Low 32 bits or r10
    lldb_r11d_x86_64, // Low 32 bits or r11
    lldb_r12d_x86_64, // Low 32 bits or r12
    lldb_r13d_x86_64, // Low 32 bits or r13
    lldb_r14d_x86_64, // Low 32 bits or r14
    lldb_r15d_x86_64, // Low 32 bits or r15
    lldb_ax_x86_64,     lldb_bx_x86_64,     lldb_cx_x86_64,  lldb_dx_x86_64,
    lldb_di_x86_64,     lldb_si_x86_64,     lldb_bp_x86_64,  lldb_sp_x86_64,
    lldb_r8w_x86_64,  // Low 16 bits or r8
    lldb_r9w_x86_64,  // Low 16 bits or r9
    lldb_r10w_x86_64, // Low 16 bits or r10
    lldb_r11w_x86_64, // Low 16 bits or r11
    lldb_r12w_x86_64, // Low 16 bits or r12
    lldb_r13w_x86_64, // Low 16 bits or r13
    lldb_r14w_x86_64, // Low 16 bits or r14
    lldb_r15w_x86_64, // Low 16 bits or r15
    lldb_ah_x86_64,     lldb_bh_x86_64,     lldb_ch_x86_64,  lldb_dh_x86_64,
    lldb_al_x86_64,     lldb_bl_x86_64,     lldb_cl_x86_64,  lldb_dl_x86_64,
    lldb_dil_x86_64,    lldb_sil_x86_64,    lldb_bpl_x86_64, lldb_spl_x86_64,
    lldb_r8l_x86_64,    // Low 8 bits or r8
    lldb_r9l_x86_64,    // Low 8 bits or r9
    lldb_r10l_x86_64,   // Low 8 bits or r10
    lldb_r11l_x86_64,   // Low 8 bits or r11
    lldb_r12l_x86_64,   // Low 8 bits or r12
    lldb_r13l_x86_64,   // Low 8 bits or r13
    lldb_r14l_x86_64,   // Low 8 bits or r14
    lldb_r15l_x86_64,   // Low 8 bits or r15
    LLDB_INVALID_REGNUM // register sets need to end with this flag
};
static_assert((sizeof(g_gpr_regnums_x86_64) / sizeof(g_gpr_regnums_x86_64[0])) -
                      1 ==
                  k_num_gpr_registers_x86_64,
              "g_gpr_regnums_x86_64 has wrong number of register infos");

// Number of register sets provided by this context.
enum { k_num_extended_register_sets = 2, k_num_register_sets = 4 };

// Register sets for x86 64-bit.
static const RegisterSet g_reg_sets_x86_64[k_num_register_sets] = {
    {"General Purpose Registers", "gpr", k_num_gpr_registers_x86_64,
     g_gpr_regnums_x86_64},
};

#define REG_CONTEXT_SIZE (GetRegisterInfoInterface().GetGPRSize())

const int fpu_present = []() -> int {
  int mib[2];
  int error;
  size_t len;
  int val;

  len = sizeof(val);
  mib[0] = CTL_MACHDEP;
  mib[1] = CPU_FPU_PRESENT;

  error = sysctl(mib, __arraycount(mib), &val, &len, NULL, 0);
  if (error)
    errx(EXIT_FAILURE, "sysctl");

  return val;
}();

const int osfxsr = []() -> int {
  int mib[2];
  int error;
  size_t len;
  int val;

  len = sizeof(val);
  mib[0] = CTL_MACHDEP;
  mib[1] = CPU_OSFXSR;

  error = sysctl(mib, __arraycount(mib), &val, &len, NULL, 0);
  if (error)
    errx(EXIT_FAILURE, "sysctl");

  return val;
}();

const int fpu_save = []() -> int {
  int mib[2];
  int error;
  size_t len;
  int val;

  len = sizeof(val);
  mib[0] = CTL_MACHDEP;
  mib[1] = CPU_FPU_SAVE;

  error = sysctl(mib, __arraycount(mib), &val, &len, NULL, 0);
  if (error)
    errx(EXIT_FAILURE, "sysctl");

  return val;
}();

} // namespace

NativeRegisterContextNetBSD *
NativeRegisterContextNetBSD::CreateHostNativeRegisterContextNetBSD(
    const ArchSpec &target_arch, NativeThreadProtocol &native_thread) {
  return new NativeRegisterContextNetBSD_x86_64(target_arch, native_thread);
}

// ----------------------------------------------------------------------------
// NativeRegisterContextNetBSD_x86_64 members.
// ----------------------------------------------------------------------------

static RegisterInfoInterface *
CreateRegisterInfoInterface(const ArchSpec &target_arch) {
  assert((HostInfo::GetArchitecture().GetAddressByteSize() == 8) &&
         "Register setting path assumes this is a 64-bit host");
  // X86_64 hosts know how to work with 64-bit and 32-bit EXEs using the x86_64
  // register context.
  return new RegisterContextNetBSD_x86_64(target_arch);
}

NativeRegisterContextNetBSD_x86_64::NativeRegisterContextNetBSD_x86_64(
    const ArchSpec &target_arch, NativeThreadProtocol &native_thread)
    : NativeRegisterContextNetBSD(native_thread,
                                  CreateRegisterInfoInterface(target_arch)),
      m_gpr_x86_64(), m_fpr_x86_64(), m_dbr_x86_64() {}

// CONSIDER after local and llgs debugging are merged, register set support can
// be moved into a base x86-64 class with IsRegisterSetAvailable made virtual.
uint32_t NativeRegisterContextNetBSD_x86_64::GetRegisterSetCount() const {
  uint32_t sets = 0;
  for (uint32_t set_index = 0; set_index < k_num_register_sets; ++set_index) {
    if (GetSetForNativeRegNum(set_index) != -1)
      ++sets;
  }

  return sets;
}

const RegisterSet *
NativeRegisterContextNetBSD_x86_64::GetRegisterSet(uint32_t set_index) const {
  switch (GetRegisterInfoInterface().GetTargetArchitecture().GetMachine()) {
  case llvm::Triple::x86_64:
    return &g_reg_sets_x86_64[set_index];
  default:
    assert(false && "Unhandled target architecture.");
    return nullptr;
  }

  return nullptr;
}

int NativeRegisterContextNetBSD_x86_64::GetSetForNativeRegNum(
    int reg_num) const {
  if (reg_num <= k_last_gpr_x86_64)
    return GPRegSet;
  else if (reg_num <= k_last_fpr_x86_64)
    return (fpu_present == 1 && osfxsr == 1 && fpu_save >= 1) ? FPRegSet : -1;
  else if (reg_num <= k_last_avx_x86_64)
    return -1; // AVX
  else if (reg_num <= k_last_mpxr_x86_64)
    return -1; // MPXR
  else if (reg_num <= k_last_mpxc_x86_64)
    return -1; // MPXC
  else if (reg_num <= lldb_dr7_x86_64)
    return DBRegSet; // DBR
  else
    return -1;
}

int NativeRegisterContextNetBSD_x86_64::ReadRegisterSet(uint32_t set) {
  switch (set) {
  case GPRegSet:
    ReadGPR();
    return 0;
  case FPRegSet:
    ReadFPR();
    return 0;
  case DBRegSet:
    ReadDBR();
    return 0;
  default:
    break;
  }
  return -1;
}
int NativeRegisterContextNetBSD_x86_64::WriteRegisterSet(uint32_t set) {
  switch (set) {
  case GPRegSet:
    WriteGPR();
    return 0;
  case FPRegSet:
    WriteFPR();
    return 0;
  case DBRegSet:
    WriteDBR();
    return 0;
  default:
    break;
  }
  return -1;
}

Status
NativeRegisterContextNetBSD_x86_64::ReadRegister(const RegisterInfo *reg_info,
                                                 RegisterValue &reg_value) {
  Status error;

  if (!reg_info) {
    error.SetErrorString("reg_info NULL");
    return error;
  }

  const uint32_t reg = reg_info->kinds[lldb::eRegisterKindLLDB];
  if (reg == LLDB_INVALID_REGNUM) {
    // This is likely an internal register for lldb use only and should not be
    // directly queried.
    error.SetErrorStringWithFormat("register \"%s\" is an internal-only lldb "
                                   "register, cannot read directly",
                                   reg_info->name);
    return error;
  }

  int set = GetSetForNativeRegNum(reg);
  if (set == -1) {
    // This is likely an internal register for lldb use only and should not be
    // directly queried.
    error.SetErrorStringWithFormat("register \"%s\" is in unrecognized set",
                                   reg_info->name);
    return error;
  }

  if (ReadRegisterSet(set) != 0) {
    // This is likely an internal register for lldb use only and should not be
    // directly queried.
    error.SetErrorStringWithFormat(
        "reading register set for register \"%s\" failed", reg_info->name);
    return error;
  }

  switch (reg) {
  case lldb_rax_x86_64:
    reg_value = (uint64_t)m_gpr_x86_64.regs[_REG_RAX];
    break;
  case lldb_rbx_x86_64:
    reg_value = (uint64_t)m_gpr_x86_64.regs[_REG_RBX];
    break;
  case lldb_rcx_x86_64:
    reg_value = (uint64_t)m_gpr_x86_64.regs[_REG_RCX];
    break;
  case lldb_rdx_x86_64:
    reg_value = (uint64_t)m_gpr_x86_64.regs[_REG_RDX];
    break;
  case lldb_rdi_x86_64:
    reg_value = (uint64_t)m_gpr_x86_64.regs[_REG_RDI];
    break;
  case lldb_rsi_x86_64:
    reg_value = (uint64_t)m_gpr_x86_64.regs[_REG_RSI];
    break;
  case lldb_rbp_x86_64:
    reg_value = (uint64_t)m_gpr_x86_64.regs[_REG_RBP];
    break;
  case lldb_rsp_x86_64:
    reg_value = (uint64_t)m_gpr_x86_64.regs[_REG_RSP];
    break;
  case lldb_r8_x86_64:
    reg_value = (uint64_t)m_gpr_x86_64.regs[_REG_R8];
    break;
  case lldb_r9_x86_64:
    reg_value = (uint64_t)m_gpr_x86_64.regs[_REG_R9];
    break;
  case lldb_r10_x86_64:
    reg_value = (uint64_t)m_gpr_x86_64.regs[_REG_R10];
    break;
  case lldb_r11_x86_64:
    reg_value = (uint64_t)m_gpr_x86_64.regs[_REG_R11];
    break;
  case lldb_r12_x86_64:
    reg_value = (uint64_t)m_gpr_x86_64.regs[_REG_R12];
    break;
  case lldb_r13_x86_64:
    reg_value = (uint64_t)m_gpr_x86_64.regs[_REG_R13];
    break;
  case lldb_r14_x86_64:
    reg_value = (uint64_t)m_gpr_x86_64.regs[_REG_R14];
    break;
  case lldb_r15_x86_64:
    reg_value = (uint64_t)m_gpr_x86_64.regs[_REG_R15];
    break;
  case lldb_rip_x86_64:
    reg_value = (uint64_t)m_gpr_x86_64.regs[_REG_RIP];
    break;
  case lldb_rflags_x86_64:
    reg_value = (uint64_t)m_gpr_x86_64.regs[_REG_RFLAGS];
    break;
  case lldb_cs_x86_64:
    reg_value = (uint64_t)m_gpr_x86_64.regs[_REG_CS];
    break;
  case lldb_fs_x86_64:
    reg_value = (uint64_t)m_gpr_x86_64.regs[_REG_FS];
    break;
  case lldb_gs_x86_64:
    reg_value = (uint64_t)m_gpr_x86_64.regs[_REG_GS];
    break;
  case lldb_ss_x86_64:
    reg_value = (uint64_t)m_gpr_x86_64.regs[_REG_SS];
    break;
  case lldb_ds_x86_64:
    reg_value = (uint64_t)m_gpr_x86_64.regs[_REG_DS];
    break;
  case lldb_es_x86_64:
    reg_value = (uint64_t)m_gpr_x86_64.regs[_REG_ES];
    break;
  case lldb_fctrl_x86_64:
    reg_value = (uint16_t)m_fpr_x86_64.fxstate.fx_cw;
    break;
  case lldb_fstat_x86_64:
    reg_value = (uint16_t)m_fpr_x86_64.fxstate.fx_sw;
    break;
  case lldb_ftag_x86_64:
    reg_value = (uint8_t)m_fpr_x86_64.fxstate.fx_tw;
    break;
  case lldb_fop_x86_64:
    reg_value = (uint64_t)m_fpr_x86_64.fxstate.fx_opcode;
    break;
  case lldb_fiseg_x86_64:
    reg_value = (uint64_t)m_fpr_x86_64.fxstate.fx_ip.fa_64;
    break;
  case lldb_fioff_x86_64:
    reg_value = (uint32_t)m_fpr_x86_64.fxstate.fx_ip.fa_32.fa_off;
    break;
  case lldb_foseg_x86_64:
    reg_value = (uint64_t)m_fpr_x86_64.fxstate.fx_dp.fa_64;
    break;
  case lldb_fooff_x86_64:
    reg_value = (uint32_t)m_fpr_x86_64.fxstate.fx_dp.fa_32.fa_off;
    break;
  case lldb_mxcsr_x86_64:
    reg_value = (uint32_t)m_fpr_x86_64.fxstate.fx_mxcsr;
    break;
  case lldb_mxcsrmask_x86_64:
    reg_value = (uint32_t)m_fpr_x86_64.fxstate.fx_mxcsr_mask;
    break;
  case lldb_st0_x86_64:
  case lldb_st1_x86_64:
  case lldb_st2_x86_64:
  case lldb_st3_x86_64:
  case lldb_st4_x86_64:
  case lldb_st5_x86_64:
  case lldb_st6_x86_64:
  case lldb_st7_x86_64:
    reg_value.SetBytes(&m_fpr_x86_64.fxstate.fx_87_ac[reg - lldb_st0_x86_64],
                       reg_info->byte_size, endian::InlHostByteOrder());
    break;
  case lldb_mm0_x86_64:
  case lldb_mm1_x86_64:
  case lldb_mm2_x86_64:
  case lldb_mm3_x86_64:
  case lldb_mm4_x86_64:
  case lldb_mm5_x86_64:
  case lldb_mm6_x86_64:
  case lldb_mm7_x86_64:
    reg_value.SetBytes(&m_fpr_x86_64.fxstate.fx_xmm[reg - lldb_mm0_x86_64],
                       reg_info->byte_size, endian::InlHostByteOrder());
    break;
  case lldb_xmm0_x86_64:
  case lldb_xmm1_x86_64:
  case lldb_xmm2_x86_64:
  case lldb_xmm3_x86_64:
  case lldb_xmm4_x86_64:
  case lldb_xmm5_x86_64:
  case lldb_xmm6_x86_64:
  case lldb_xmm7_x86_64:
  case lldb_xmm8_x86_64:
  case lldb_xmm9_x86_64:
  case lldb_xmm10_x86_64:
  case lldb_xmm11_x86_64:
  case lldb_xmm12_x86_64:
  case lldb_xmm13_x86_64:
  case lldb_xmm14_x86_64:
  case lldb_xmm15_x86_64:
    reg_value.SetBytes(&m_fpr_x86_64.fxstate.fx_xmm[reg - lldb_xmm0_x86_64],
                       reg_info->byte_size, endian::InlHostByteOrder());
    break;
  case lldb_dr0_x86_64:
  case lldb_dr1_x86_64:
  case lldb_dr2_x86_64:
  case lldb_dr3_x86_64:
  case lldb_dr4_x86_64:
  case lldb_dr5_x86_64:
  case lldb_dr6_x86_64:
  case lldb_dr7_x86_64:
    reg_value = (uint64_t)m_dbr_x86_64.dr[reg - lldb_dr0_x86_64];
    break;
  }

  return error;
}

Status NativeRegisterContextNetBSD_x86_64::WriteRegister(
    const RegisterInfo *reg_info, const RegisterValue &reg_value) {

  Status error;

  if (!reg_info) {
    error.SetErrorString("reg_info NULL");
    return error;
  }

  const uint32_t reg = reg_info->kinds[lldb::eRegisterKindLLDB];
  if (reg == LLDB_INVALID_REGNUM) {
    // This is likely an internal register for lldb use only and should not be
    // directly queried.
    error.SetErrorStringWithFormat("register \"%s\" is an internal-only lldb "
                                   "register, cannot read directly",
                                   reg_info->name);
    return error;
  }

  int set = GetSetForNativeRegNum(reg);
  if (set == -1) {
    // This is likely an internal register for lldb use only and should not be
    // directly queried.
    error.SetErrorStringWithFormat("register \"%s\" is in unrecognized set",
                                   reg_info->name);
    return error;
  }

  if (ReadRegisterSet(set) != 0) {
    // This is likely an internal register for lldb use only and should not be
    // directly queried.
    error.SetErrorStringWithFormat(
        "reading register set for register \"%s\" failed", reg_info->name);
    return error;
  }

  switch (reg) {
  case lldb_rax_x86_64:
    m_gpr_x86_64.regs[_REG_RAX] = reg_value.GetAsUInt64();
    break;
  case lldb_rbx_x86_64:
    m_gpr_x86_64.regs[_REG_RBX] = reg_value.GetAsUInt64();
    break;
  case lldb_rcx_x86_64:
    m_gpr_x86_64.regs[_REG_RCX] = reg_value.GetAsUInt64();
    break;
  case lldb_rdx_x86_64:
    m_gpr_x86_64.regs[_REG_RDX] = reg_value.GetAsUInt64();
    break;
  case lldb_rdi_x86_64:
    m_gpr_x86_64.regs[_REG_RDI] = reg_value.GetAsUInt64();
    break;
  case lldb_rsi_x86_64:
    m_gpr_x86_64.regs[_REG_RSI] = reg_value.GetAsUInt64();
    break;
  case lldb_rbp_x86_64:
    m_gpr_x86_64.regs[_REG_RBP] = reg_value.GetAsUInt64();
    break;
  case lldb_rsp_x86_64:
    m_gpr_x86_64.regs[_REG_RSP] = reg_value.GetAsUInt64();
    break;
  case lldb_r8_x86_64:
    m_gpr_x86_64.regs[_REG_R8] = reg_value.GetAsUInt64();
    break;
  case lldb_r9_x86_64:
    m_gpr_x86_64.regs[_REG_R9] = reg_value.GetAsUInt64();
    break;
  case lldb_r10_x86_64:
    m_gpr_x86_64.regs[_REG_R10] = reg_value.GetAsUInt64();
    break;
  case lldb_r11_x86_64:
    m_gpr_x86_64.regs[_REG_R11] = reg_value.GetAsUInt64();
    break;
  case lldb_r12_x86_64:
    m_gpr_x86_64.regs[_REG_R12] = reg_value.GetAsUInt64();
    break;
  case lldb_r13_x86_64:
    m_gpr_x86_64.regs[_REG_R13] = reg_value.GetAsUInt64();
    break;
  case lldb_r14_x86_64:
    m_gpr_x86_64.regs[_REG_R14] = reg_value.GetAsUInt64();
    break;
  case lldb_r15_x86_64:
    m_gpr_x86_64.regs[_REG_R15] = reg_value.GetAsUInt64();
    break;
  case lldb_rip_x86_64:
    m_gpr_x86_64.regs[_REG_RIP] = reg_value.GetAsUInt64();
    break;
  case lldb_rflags_x86_64:
    m_gpr_x86_64.regs[_REG_RFLAGS] = reg_value.GetAsUInt64();
    break;
  case lldb_cs_x86_64:
    m_gpr_x86_64.regs[_REG_CS] = reg_value.GetAsUInt64();
    break;
  case lldb_fs_x86_64:
    m_gpr_x86_64.regs[_REG_FS] = reg_value.GetAsUInt64();
    break;
  case lldb_gs_x86_64:
    m_gpr_x86_64.regs[_REG_GS] = reg_value.GetAsUInt64();
    break;
  case lldb_ss_x86_64:
    m_gpr_x86_64.regs[_REG_SS] = reg_value.GetAsUInt64();
    break;
  case lldb_ds_x86_64:
    m_gpr_x86_64.regs[_REG_DS] = reg_value.GetAsUInt64();
    break;
  case lldb_es_x86_64:
    m_gpr_x86_64.regs[_REG_ES] = reg_value.GetAsUInt64();
    break;
  case lldb_fctrl_x86_64:
    m_fpr_x86_64.fxstate.fx_cw = reg_value.GetAsUInt16();
    break;
  case lldb_fstat_x86_64:
    m_fpr_x86_64.fxstate.fx_sw = reg_value.GetAsUInt16();
    break;
  case lldb_ftag_x86_64:
    m_fpr_x86_64.fxstate.fx_tw = reg_value.GetAsUInt8();
    break;
  case lldb_fop_x86_64:
    m_fpr_x86_64.fxstate.fx_opcode = reg_value.GetAsUInt16();
    break;
  case lldb_fiseg_x86_64:
    m_fpr_x86_64.fxstate.fx_ip.fa_64 = reg_value.GetAsUInt64();
    break;
  case lldb_fioff_x86_64:
    m_fpr_x86_64.fxstate.fx_ip.fa_32.fa_off = reg_value.GetAsUInt32();
    break;
  case lldb_foseg_x86_64:
    m_fpr_x86_64.fxstate.fx_dp.fa_64 = reg_value.GetAsUInt64();
    break;
  case lldb_fooff_x86_64:
    m_fpr_x86_64.fxstate.fx_dp.fa_32.fa_off = reg_value.GetAsUInt32();
    break;
  case lldb_mxcsr_x86_64:
    m_fpr_x86_64.fxstate.fx_mxcsr = reg_value.GetAsUInt32();
    break;
  case lldb_mxcsrmask_x86_64:
    m_fpr_x86_64.fxstate.fx_mxcsr_mask = reg_value.GetAsUInt32();
    break;
  case lldb_st0_x86_64:
  case lldb_st1_x86_64:
  case lldb_st2_x86_64:
  case lldb_st3_x86_64:
  case lldb_st4_x86_64:
  case lldb_st5_x86_64:
  case lldb_st6_x86_64:
  case lldb_st7_x86_64:
    ::memcpy(&m_fpr_x86_64.fxstate.fx_87_ac[reg - lldb_st0_x86_64],
             reg_value.GetBytes(), reg_value.GetByteSize());
    break;
  case lldb_mm0_x86_64:
  case lldb_mm1_x86_64:
  case lldb_mm2_x86_64:
  case lldb_mm3_x86_64:
  case lldb_mm4_x86_64:
  case lldb_mm5_x86_64:
  case lldb_mm6_x86_64:
  case lldb_mm7_x86_64:
    ::memcpy(&m_fpr_x86_64.fxstate.fx_xmm[reg - lldb_mm0_x86_64],
             reg_value.GetBytes(), reg_value.GetByteSize());
    break;
  case lldb_xmm0_x86_64:
  case lldb_xmm1_x86_64:
  case lldb_xmm2_x86_64:
  case lldb_xmm3_x86_64:
  case lldb_xmm4_x86_64:
  case lldb_xmm5_x86_64:
  case lldb_xmm6_x86_64:
  case lldb_xmm7_x86_64:
  case lldb_xmm8_x86_64:
  case lldb_xmm9_x86_64:
  case lldb_xmm10_x86_64:
  case lldb_xmm11_x86_64:
  case lldb_xmm12_x86_64:
  case lldb_xmm13_x86_64:
  case lldb_xmm14_x86_64:
  case lldb_xmm15_x86_64:
    ::memcpy(&m_fpr_x86_64.fxstate.fx_xmm[reg - lldb_xmm0_x86_64],
             reg_value.GetBytes(), reg_value.GetByteSize());
    break;
  case lldb_dr0_x86_64:
  case lldb_dr1_x86_64:
  case lldb_dr2_x86_64:
  case lldb_dr3_x86_64:
  case lldb_dr4_x86_64:
  case lldb_dr5_x86_64:
  case lldb_dr6_x86_64:
  case lldb_dr7_x86_64:
    m_dbr_x86_64.dr[reg - lldb_dr0_x86_64] = reg_value.GetAsUInt64();
    break;
  }

  if (WriteRegisterSet(set) != 0)
    error.SetErrorStringWithFormat("failed to write register set");

  return error;
}

Status NativeRegisterContextNetBSD_x86_64::ReadAllRegisterValues(
    lldb::DataBufferSP &data_sp) {
  Status error;

  data_sp.reset(new DataBufferHeap(REG_CONTEXT_SIZE, 0));
  if (!data_sp) {
    error.SetErrorStringWithFormat(
        "failed to allocate DataBufferHeap instance of size %" PRIu64,
        REG_CONTEXT_SIZE);
    return error;
  }

  error = ReadGPR();
  if (error.Fail())
    return error;

  uint8_t *dst = data_sp->GetBytes();
  if (dst == nullptr) {
    error.SetErrorStringWithFormat("DataBufferHeap instance of size %" PRIu64
                                   " returned a null pointer",
                                   REG_CONTEXT_SIZE);
    return error;
  }

  ::memcpy(dst, &m_gpr_x86_64, GetRegisterInfoInterface().GetGPRSize());
  dst += GetRegisterInfoInterface().GetGPRSize();

  RegisterValue value((uint64_t)-1);
  const RegisterInfo *reg_info =
      GetRegisterInfoInterface().GetDynamicRegisterInfo("orig_eax");
  if (reg_info == nullptr)
    reg_info = GetRegisterInfoInterface().GetDynamicRegisterInfo("orig_rax");
  return error;
}

Status NativeRegisterContextNetBSD_x86_64::WriteAllRegisterValues(
    const lldb::DataBufferSP &data_sp) {
  Status error;

  if (!data_sp) {
    error.SetErrorStringWithFormat(
        "NativeRegisterContextNetBSD_x86_64::%s invalid data_sp provided",
        __FUNCTION__);
    return error;
  }

  if (data_sp->GetByteSize() != REG_CONTEXT_SIZE) {
    error.SetErrorStringWithFormat(
        "NativeRegisterContextNetBSD_x86_64::%s data_sp contained mismatched "
        "data size, expected %" PRIu64 ", actual %" PRIu64,
        __FUNCTION__, REG_CONTEXT_SIZE, data_sp->GetByteSize());
    return error;
  }

  uint8_t *src = data_sp->GetBytes();
  if (src == nullptr) {
    error.SetErrorStringWithFormat("NativeRegisterContextNetBSD_x86_64::%s "
                                   "DataBuffer::GetBytes() returned a null "
                                   "pointer",
                                   __FUNCTION__);
    return error;
  }
  ::memcpy(&m_gpr_x86_64, src, GetRegisterInfoInterface().GetGPRSize());

  error = WriteGPR();
  if (error.Fail())
    return error;
  src += GetRegisterInfoInterface().GetGPRSize();

  return error;
}

Status NativeRegisterContextNetBSD_x86_64::IsWatchpointHit(uint32_t wp_index,
                                                           bool &is_hit) {
  if (wp_index >= NumSupportedHardwareWatchpoints())
    return Status("Watchpoint index out of range");

  RegisterValue reg_value;
  const RegisterInfo *const reg_info = GetRegisterInfoAtIndex(lldb_dr6_x86_64);
  Status error = ReadRegister(reg_info, reg_value);
  if (error.Fail()) {
    is_hit = false;
    return error;
  }

  uint64_t status_bits = reg_value.GetAsUInt64();

  is_hit = status_bits & (1 << wp_index);

  return error;
}

Status NativeRegisterContextNetBSD_x86_64::GetWatchpointHitIndex(
    uint32_t &wp_index, lldb::addr_t trap_addr) {
  uint32_t num_hw_wps = NumSupportedHardwareWatchpoints();
  for (wp_index = 0; wp_index < num_hw_wps; ++wp_index) {
    bool is_hit;
    Status error = IsWatchpointHit(wp_index, is_hit);
    if (error.Fail()) {
      wp_index = LLDB_INVALID_INDEX32;
      return error;
    } else if (is_hit) {
      return error;
    }
  }
  wp_index = LLDB_INVALID_INDEX32;
  return Status();
}

Status NativeRegisterContextNetBSD_x86_64::IsWatchpointVacant(uint32_t wp_index,
                                                              bool &is_vacant) {
  if (wp_index >= NumSupportedHardwareWatchpoints())
    return Status("Watchpoint index out of range");

  RegisterValue reg_value;
  const RegisterInfo *const reg_info = GetRegisterInfoAtIndex(lldb_dr7_x86_64);
  Status error = ReadRegister(reg_info, reg_value);
  if (error.Fail()) {
    is_vacant = false;
    return error;
  }

  uint64_t control_bits = reg_value.GetAsUInt64();

  is_vacant = !(control_bits & (1 << (2 * wp_index)));

  return error;
}

Status NativeRegisterContextNetBSD_x86_64::SetHardwareWatchpointWithIndex(
    lldb::addr_t addr, size_t size, uint32_t watch_flags, uint32_t wp_index) {

  if (wp_index >= NumSupportedHardwareWatchpoints())
    return Status("Watchpoint index out of range");

  // Read only watchpoints aren't supported on x86_64. Fall back to read/write
  // waitchpoints instead.
  // TODO: Add logic to detect when a write happens and ignore that watchpoint
  // hit.
  if (watch_flags == 0x2)
    watch_flags = 0x3;

  if (watch_flags != 0x1 && watch_flags != 0x3)
    return Status("Invalid read/write bits for watchpoint");

  if (size != 1 && size != 2 && size != 4 && size != 8)
    return Status("Invalid size for watchpoint");

  bool is_vacant;
  Status error = IsWatchpointVacant(wp_index, is_vacant);
  if (error.Fail())
    return error;
  if (!is_vacant)
    return Status("Watchpoint index not vacant");

  RegisterValue reg_value;
  const RegisterInfo *const reg_info_dr7 =
      GetRegisterInfoAtIndex(lldb_dr7_x86_64);
  error = ReadRegister(reg_info_dr7, reg_value);
  if (error.Fail())
    return error;

  // for watchpoints 0, 1, 2, or 3, respectively, set bits 1, 3, 5, or 7
  uint64_t enable_bit = 1 << (2 * wp_index);

  // set bits 16-17, 20-21, 24-25, or 28-29
  // with 0b01 for write, and 0b11 for read/write
  uint64_t rw_bits = watch_flags << (16 + 4 * wp_index);

  // set bits 18-19, 22-23, 26-27, or 30-31
  // with 0b00, 0b01, 0b10, or 0b11
  // for 1, 2, 8 (if supported), or 4 bytes, respectively
  uint64_t size_bits = (size == 8 ? 0x2 : size - 1) << (18 + 4 * wp_index);

  uint64_t bit_mask = (0x3 << (2 * wp_index)) | (0xF << (16 + 4 * wp_index));

  uint64_t control_bits = reg_value.GetAsUInt64() & ~bit_mask;

  control_bits |= enable_bit | rw_bits | size_bits;

  const RegisterInfo *const reg_info_drN =
      GetRegisterInfoAtIndex(lldb_dr0_x86_64 + wp_index);
  error = WriteRegister(reg_info_drN, RegisterValue(addr));
  if (error.Fail())
    return error;

  error = WriteRegister(reg_info_dr7, RegisterValue(control_bits));
  if (error.Fail())
    return error;

  error.Clear();
  return error;
}

bool NativeRegisterContextNetBSD_x86_64::ClearHardwareWatchpoint(
    uint32_t wp_index) {
  if (wp_index >= NumSupportedHardwareWatchpoints())
    return false;

  RegisterValue reg_value;

  // for watchpoints 0, 1, 2, or 3, respectively, clear bits 0, 1, 2, or 3 of
  // the debug status register (DR6)
  const RegisterInfo *const reg_info_dr6 =
      GetRegisterInfoAtIndex(lldb_dr6_x86_64);
  Status error = ReadRegister(reg_info_dr6, reg_value);
  if (error.Fail())
    return false;
  uint64_t bit_mask = 1 << wp_index;
  uint64_t status_bits = reg_value.GetAsUInt64() & ~bit_mask;
  error = WriteRegister(reg_info_dr6, RegisterValue(status_bits));
  if (error.Fail())
    return false;

  // for watchpoints 0, 1, 2, or 3, respectively, clear bits {0-1,16-19},
  // {2-3,20-23}, {4-5,24-27}, or {6-7,28-31} of the debug control register
  // (DR7)
  const RegisterInfo *const reg_info_dr7 =
      GetRegisterInfoAtIndex(lldb_dr7_x86_64);
  error = ReadRegister(reg_info_dr7, reg_value);
  if (error.Fail())
    return false;
  bit_mask = (0x3 << (2 * wp_index)) | (0xF << (16 + 4 * wp_index));
  uint64_t control_bits = reg_value.GetAsUInt64() & ~bit_mask;
  return WriteRegister(reg_info_dr7, RegisterValue(control_bits)).Success();
}

Status NativeRegisterContextNetBSD_x86_64::ClearAllHardwareWatchpoints() {
  RegisterValue reg_value;

  // clear bits {0-4} of the debug status register (DR6)
  const RegisterInfo *const reg_info_dr6 =
      GetRegisterInfoAtIndex(lldb_dr6_x86_64);
  Status error = ReadRegister(reg_info_dr6, reg_value);
  if (error.Fail())
    return error;
  uint64_t bit_mask = 0xF;
  uint64_t status_bits = reg_value.GetAsUInt64() & ~bit_mask;
  error = WriteRegister(reg_info_dr6, RegisterValue(status_bits));
  if (error.Fail())
    return error;

  // clear bits {0-7,16-31} of the debug control register (DR7)
  const RegisterInfo *const reg_info_dr7 =
      GetRegisterInfoAtIndex(lldb_dr7_x86_64);
  error = ReadRegister(reg_info_dr7, reg_value);
  if (error.Fail())
    return error;
  bit_mask = 0xFF | (0xFFFF << 16);
  uint64_t control_bits = reg_value.GetAsUInt64() & ~bit_mask;
  return WriteRegister(reg_info_dr7, RegisterValue(control_bits));
}

uint32_t NativeRegisterContextNetBSD_x86_64::SetHardwareWatchpoint(
    lldb::addr_t addr, size_t size, uint32_t watch_flags) {
  Log *log(GetLogIfAllCategoriesSet(LIBLLDB_LOG_WATCHPOINTS));
  const uint32_t num_hw_watchpoints = NumSupportedHardwareWatchpoints();
  for (uint32_t wp_index = 0; wp_index < num_hw_watchpoints; ++wp_index) {
    bool is_vacant;
    Status error = IsWatchpointVacant(wp_index, is_vacant);
    if (is_vacant) {
      error = SetHardwareWatchpointWithIndex(addr, size, watch_flags, wp_index);
      if (error.Success())
        return wp_index;
    }
    if (error.Fail() && log) {
      log->Printf("NativeRegisterContextNetBSD_x86_64::%s Error: %s",
                  __FUNCTION__, error.AsCString());
    }
  }
  return LLDB_INVALID_INDEX32;
}

lldb::addr_t
NativeRegisterContextNetBSD_x86_64::GetWatchpointAddress(uint32_t wp_index) {
  if (wp_index >= NumSupportedHardwareWatchpoints())
    return LLDB_INVALID_ADDRESS;
  RegisterValue reg_value;
  const RegisterInfo *const reg_info_drN =
      GetRegisterInfoAtIndex(lldb_dr0_x86_64 + wp_index);
  if (ReadRegister(reg_info_drN, reg_value).Fail())
    return LLDB_INVALID_ADDRESS;
  return reg_value.GetAsUInt64();
}

uint32_t NativeRegisterContextNetBSD_x86_64::NumSupportedHardwareWatchpoints() {
  // Available debug address registers: dr0, dr1, dr2, dr3
  return 4;
}

#endif // defined(__x86_64__)
