//===-- RegisterContextLinux_mips.cpp ------------------------*- C++ -*-===//
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
#include "RegisterContextLinux_mips.h"

// Internal codes for mips registers
#include "lldb-mips-linux-register-enums.h"

// For GP and FP buffers
#include "RegisterContext_mips.h"

using namespace lldb_private;
using namespace lldb;

//---------------------------------------------------------------------------
// Include RegisterInfos_mips to declare our g_register_infos_mips structure.
//---------------------------------------------------------------------------
#define DECLARE_REGISTER_INFOS_MIPS_STRUCT
#include "RegisterInfos_mips.h"
#undef DECLARE_REGISTER_INFOS_MIPS_STRUCT

// mips general purpose registers.
const uint32_t g_gp_regnums_mips[] = {
    gpr_zero_mips,      gpr_r1_mips,    gpr_r2_mips,      gpr_r3_mips,
    gpr_r4_mips,        gpr_r5_mips,    gpr_r6_mips,      gpr_r7_mips,
    gpr_r8_mips,        gpr_r9_mips,    gpr_r10_mips,     gpr_r11_mips,
    gpr_r12_mips,       gpr_r13_mips,   gpr_r14_mips,     gpr_r15_mips,
    gpr_r16_mips,       gpr_r17_mips,   gpr_r18_mips,     gpr_r19_mips,
    gpr_r20_mips,       gpr_r21_mips,   gpr_r22_mips,     gpr_r23_mips,
    gpr_r24_mips,       gpr_r25_mips,   gpr_r26_mips,     gpr_r27_mips,
    gpr_gp_mips,        gpr_sp_mips,    gpr_r30_mips,     gpr_ra_mips,
    gpr_sr_mips,        gpr_mullo_mips, gpr_mulhi_mips,   gpr_badvaddr_mips,
    gpr_cause_mips,     gpr_pc_mips,    gpr_config5_mips,
    LLDB_INVALID_REGNUM // register sets need to end with this flag
};

static_assert((sizeof(g_gp_regnums_mips) / sizeof(g_gp_regnums_mips[0])) - 1 ==
                  k_num_gpr_registers_mips,
              "g_gp_regnums_mips has wrong number of register infos");
// mips floating point registers.
const uint32_t g_fp_regnums_mips[] = {
    fpr_f0_mips,        fpr_f1_mips,  fpr_f2_mips,      fpr_f3_mips,
    fpr_f4_mips,        fpr_f5_mips,  fpr_f6_mips,      fpr_f7_mips,
    fpr_f8_mips,        fpr_f9_mips,  fpr_f10_mips,     fpr_f11_mips,
    fpr_f12_mips,       fpr_f13_mips, fpr_f14_mips,     fpr_f15_mips,
    fpr_f16_mips,       fpr_f17_mips, fpr_f18_mips,     fpr_f19_mips,
    fpr_f20_mips,       fpr_f21_mips, fpr_f22_mips,     fpr_f23_mips,
    fpr_f24_mips,       fpr_f25_mips, fpr_f26_mips,     fpr_f27_mips,
    fpr_f28_mips,       fpr_f29_mips, fpr_f30_mips,     fpr_f31_mips,
    fpr_fcsr_mips,      fpr_fir_mips, fpr_config5_mips,
    LLDB_INVALID_REGNUM // register sets need to end with this flag
};

static_assert((sizeof(g_fp_regnums_mips) / sizeof(g_fp_regnums_mips[0])) - 1 ==
                  k_num_fpr_registers_mips,
              "g_fp_regnums_mips has wrong number of register infos");

// mips MSA registers.
const uint32_t g_msa_regnums_mips[] = {
    msa_w0_mips,        msa_w1_mips,  msa_w2_mips,   msa_w3_mips,
    msa_w4_mips,        msa_w5_mips,  msa_w6_mips,   msa_w7_mips,
    msa_w8_mips,        msa_w9_mips,  msa_w10_mips,  msa_w11_mips,
    msa_w12_mips,       msa_w13_mips, msa_w14_mips,  msa_w15_mips,
    msa_w16_mips,       msa_w17_mips, msa_w18_mips,  msa_w19_mips,
    msa_w20_mips,       msa_w21_mips, msa_w22_mips,  msa_w23_mips,
    msa_w24_mips,       msa_w25_mips, msa_w26_mips,  msa_w27_mips,
    msa_w28_mips,       msa_w29_mips, msa_w30_mips,  msa_w31_mips,
    msa_fcsr_mips,      msa_fir_mips, msa_mcsr_mips, msa_mir_mips,
    msa_config5_mips,
    LLDB_INVALID_REGNUM // register sets need to end with this flag
};

static_assert((sizeof(g_msa_regnums_mips) / sizeof(g_msa_regnums_mips[0])) -
                      1 ==
                  k_num_msa_registers_mips,
              "g_msa_regnums_mips has wrong number of register infos");

// Number of register sets provided by this context.
constexpr size_t k_num_register_sets = 3;

// Register sets for mips.
static const RegisterSet g_reg_sets_mips[k_num_register_sets] = {
    {"General Purpose Registers", "gpr", k_num_gpr_registers_mips,
     g_gp_regnums_mips},
    {"Floating Point Registers", "fpu", k_num_fpr_registers_mips,
     g_fp_regnums_mips},
    {"MSA Registers", "msa", k_num_msa_registers_mips, g_msa_regnums_mips}};

uint32_t GetUserRegisterInfoCount(bool msa_present) {
  if (msa_present)
    return static_cast<uint32_t>(k_num_user_registers_mips);
  return static_cast<uint32_t>(k_num_user_registers_mips -
                               k_num_msa_registers_mips);
}

RegisterContextLinux_mips::RegisterContextLinux_mips(
    const ArchSpec &target_arch, bool msa_present)
    : RegisterInfoInterface(target_arch),
      m_user_register_count(GetUserRegisterInfoCount(msa_present)) {}

size_t RegisterContextLinux_mips::GetGPRSize() const {
  return sizeof(GPR_linux_mips);
}

const RegisterInfo *RegisterContextLinux_mips::GetRegisterInfo() const {
  switch (m_target_arch.GetMachine()) {
  case llvm::Triple::mips:
  case llvm::Triple::mipsel:
    return g_register_infos_mips;
  default:
    assert(false && "Unhandled target architecture.");
    return NULL;
  }
}

const RegisterSet * 
RegisterContextLinux_mips::GetRegisterSet(size_t set) const {
  if (set >= k_num_register_sets)
    return nullptr;
  switch (m_target_arch.GetMachine()) {
    case llvm::Triple::mips:
    case llvm::Triple::mipsel:
      return &g_reg_sets_mips[set];
    default:
      assert(false && "Unhandled target architecture.");
      return nullptr;
  }
}

size_t
RegisterContextLinux_mips::GetRegisterSetCount() const {
  return k_num_register_sets;
}

uint32_t RegisterContextLinux_mips::GetRegisterCount() const {
  return static_cast<uint32_t>(sizeof(g_register_infos_mips) /
                               sizeof(g_register_infos_mips[0]));
}

uint32_t RegisterContextLinux_mips::GetUserRegisterCount() const {
  return static_cast<uint32_t>(m_user_register_count);
}
