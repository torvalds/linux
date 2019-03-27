//===-- RegisterContextLinux_mips64.cpp ------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===---------------------------------------------------------------------===//


#include <stddef.h>
#include <vector>

// For eh_frame and DWARF Register numbers
#include "RegisterContextLinux_mips64.h"

// For GP and FP buffers
#include "RegisterContext_mips.h"

// Internal codes for all mips32 and mips64 registers
#include "lldb-mips-linux-register-enums.h"

using namespace lldb;
using namespace lldb_private;

//---------------------------------------------------------------------------
// Include RegisterInfos_mips64 to declare our g_register_infos_mips64
// structure.
//---------------------------------------------------------------------------
#define DECLARE_REGISTER_INFOS_MIPS64_STRUCT
#define LINUX_MIPS64
#include "RegisterInfos_mips64.h"
#undef LINUX_MIPS64
#undef DECLARE_REGISTER_INFOS_MIPS64_STRUCT

//---------------------------------------------------------------------------
// Include RegisterInfos_mips to declare our g_register_infos_mips structure.
//---------------------------------------------------------------------------
#define DECLARE_REGISTER_INFOS_MIPS_STRUCT
#include "RegisterInfos_mips.h"
#undef DECLARE_REGISTER_INFOS_MIPS_STRUCT

// mips64 general purpose registers.
const uint32_t g_gp_regnums_mips64[] = {
    gpr_zero_mips64,    gpr_r1_mips64,    gpr_r2_mips64,
    gpr_r3_mips64,      gpr_r4_mips64,    gpr_r5_mips64,
    gpr_r6_mips64,      gpr_r7_mips64,    gpr_r8_mips64,
    gpr_r9_mips64,      gpr_r10_mips64,   gpr_r11_mips64,
    gpr_r12_mips64,     gpr_r13_mips64,   gpr_r14_mips64,
    gpr_r15_mips64,     gpr_r16_mips64,   gpr_r17_mips64,
    gpr_r18_mips64,     gpr_r19_mips64,   gpr_r20_mips64,
    gpr_r21_mips64,     gpr_r22_mips64,   gpr_r23_mips64,
    gpr_r24_mips64,     gpr_r25_mips64,   gpr_r26_mips64,
    gpr_r27_mips64,     gpr_gp_mips64,    gpr_sp_mips64,
    gpr_r30_mips64,     gpr_ra_mips64,    gpr_sr_mips64,
    gpr_mullo_mips64,   gpr_mulhi_mips64, gpr_badvaddr_mips64,
    gpr_cause_mips64,   gpr_pc_mips64,    gpr_config5_mips64,
    LLDB_INVALID_REGNUM // register sets need to end with this flag
};

static_assert((sizeof(g_gp_regnums_mips64) / sizeof(g_gp_regnums_mips64[0])) -
                      1 ==
                  k_num_gpr_registers_mips64,
              "g_gp_regnums_mips64 has wrong number of register infos");

// mips64 floating point registers.
const uint32_t g_fp_regnums_mips64[] = {
    fpr_f0_mips64,      fpr_f1_mips64,  fpr_f2_mips64,      fpr_f3_mips64,
    fpr_f4_mips64,      fpr_f5_mips64,  fpr_f6_mips64,      fpr_f7_mips64,
    fpr_f8_mips64,      fpr_f9_mips64,  fpr_f10_mips64,     fpr_f11_mips64,
    fpr_f12_mips64,     fpr_f13_mips64, fpr_f14_mips64,     fpr_f15_mips64,
    fpr_f16_mips64,     fpr_f17_mips64, fpr_f18_mips64,     fpr_f19_mips64,
    fpr_f20_mips64,     fpr_f21_mips64, fpr_f22_mips64,     fpr_f23_mips64,
    fpr_f24_mips64,     fpr_f25_mips64, fpr_f26_mips64,     fpr_f27_mips64,
    fpr_f28_mips64,     fpr_f29_mips64, fpr_f30_mips64,     fpr_f31_mips64,
    fpr_fcsr_mips64,    fpr_fir_mips64, fpr_config5_mips64,
    LLDB_INVALID_REGNUM // register sets need to end with this flag
};

static_assert((sizeof(g_fp_regnums_mips64) / sizeof(g_fp_regnums_mips64[0])) -
                      1 ==
                  k_num_fpr_registers_mips64,
              "g_fp_regnums_mips64 has wrong number of register infos");

// mips64 MSA registers.
const uint32_t g_msa_regnums_mips64[] = {
    msa_w0_mips64,      msa_w1_mips64,  msa_w2_mips64,   msa_w3_mips64,
    msa_w4_mips64,      msa_w5_mips64,  msa_w6_mips64,   msa_w7_mips64,
    msa_w8_mips64,      msa_w9_mips64,  msa_w10_mips64,  msa_w11_mips64,
    msa_w12_mips64,     msa_w13_mips64, msa_w14_mips64,  msa_w15_mips64,
    msa_w16_mips64,     msa_w17_mips64, msa_w18_mips64,  msa_w19_mips64,
    msa_w20_mips64,     msa_w21_mips64, msa_w22_mips64,  msa_w23_mips64,
    msa_w24_mips64,     msa_w25_mips64, msa_w26_mips64,  msa_w27_mips64,
    msa_w28_mips64,     msa_w29_mips64, msa_w30_mips64,  msa_w31_mips64,
    msa_fcsr_mips64,    msa_fir_mips64, msa_mcsr_mips64, msa_mir_mips64,
    msa_config5_mips64,
    LLDB_INVALID_REGNUM // register sets need to end with this flag
};

static_assert((sizeof(g_msa_regnums_mips64) / sizeof(g_msa_regnums_mips64[0])) -
                      1 ==
                  k_num_msa_registers_mips64,
              "g_msa_regnums_mips64 has wrong number of register infos");

// Number of register sets provided by this context.
constexpr size_t k_num_register_sets = 3;

// Register sets for mips64.
static const RegisterSet g_reg_sets_mips64[k_num_register_sets] = {
    {"General Purpose Registers", "gpr", k_num_gpr_registers_mips64,
     g_gp_regnums_mips64},
    {"Floating Point Registers", "fpu", k_num_fpr_registers_mips64,
     g_fp_regnums_mips64},
    {"MSA Registers", "msa", k_num_msa_registers_mips64, g_msa_regnums_mips64},
};

const RegisterSet *
RegisterContextLinux_mips64::GetRegisterSet(size_t set) const {
  if (set >= k_num_register_sets)
    return nullptr;

  switch (m_target_arch.GetMachine()) {
  case llvm::Triple::mips64:
  case llvm::Triple::mips64el:
    return &g_reg_sets_mips64[set];
  default:
    assert(false && "Unhandled target architecture.");
    return nullptr;
  }
  return nullptr;
}

size_t
RegisterContextLinux_mips64::GetRegisterSetCount() const {
  return k_num_register_sets;
}

static const RegisterInfo *GetRegisterInfoPtr(const ArchSpec &target_arch) {
  switch (target_arch.GetMachine()) {
  case llvm::Triple::mips64:
  case llvm::Triple::mips64el:
    return g_register_infos_mips64;
  case llvm::Triple::mips:
  case llvm::Triple::mipsel:
    return g_register_infos_mips;
  default:
    assert(false && "Unhandled target architecture.");
    return nullptr;
  }
}

static uint32_t GetRegisterInfoCount(const ArchSpec &target_arch) {
  switch (target_arch.GetMachine()) {
  case llvm::Triple::mips64:
  case llvm::Triple::mips64el:
    return static_cast<uint32_t>(sizeof(g_register_infos_mips64) /
                                 sizeof(g_register_infos_mips64[0]));
  case llvm::Triple::mips:
  case llvm::Triple::mipsel:
    return static_cast<uint32_t>(sizeof(g_register_infos_mips) /
                                 sizeof(g_register_infos_mips[0]));
  default:
    assert(false && "Unhandled target architecture.");
    return 0;
  }
}

uint32_t GetUserRegisterInfoCount(const ArchSpec &target_arch,
                                  bool msa_present) {
  switch (target_arch.GetMachine()) {
  case llvm::Triple::mips:
  case llvm::Triple::mipsel:
    if (msa_present)
      return static_cast<uint32_t>(k_num_user_registers_mips);
    return static_cast<uint32_t>(k_num_user_registers_mips -
                                 k_num_msa_registers_mips);
  case llvm::Triple::mips64el:
  case llvm::Triple::mips64:
    if (msa_present)
      return static_cast<uint32_t>(k_num_user_registers_mips64);
    return static_cast<uint32_t>(k_num_user_registers_mips64 -
                                 k_num_msa_registers_mips64);
  default:
    assert(false && "Unhandled target architecture.");
    return 0;
  }
}

RegisterContextLinux_mips64::RegisterContextLinux_mips64(
    const ArchSpec &target_arch, bool msa_present)
    : lldb_private::RegisterInfoInterface(target_arch),
      m_register_info_p(GetRegisterInfoPtr(target_arch)),
      m_register_info_count(GetRegisterInfoCount(target_arch)),
      m_user_register_count(
          GetUserRegisterInfoCount(target_arch, msa_present)) {}

size_t RegisterContextLinux_mips64::GetGPRSize() const {
  return sizeof(GPR_linux_mips);
}

const RegisterInfo *RegisterContextLinux_mips64::GetRegisterInfo() const {
  return m_register_info_p;
}

uint32_t RegisterContextLinux_mips64::GetRegisterCount() const {
  return m_register_info_count;
}

uint32_t RegisterContextLinux_mips64::GetUserRegisterCount() const {
  return m_user_register_count;
}

