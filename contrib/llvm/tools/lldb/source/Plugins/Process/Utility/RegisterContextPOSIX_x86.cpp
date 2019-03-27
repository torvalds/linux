//===-- RegisterContextPOSIX_x86.cpp ----------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include <cstring>
#include <errno.h>
#include <stdint.h>

#include "lldb/Target/Process.h"
#include "lldb/Target/Target.h"
#include "lldb/Target/Thread.h"
#include "lldb/Utility/DataBufferHeap.h"
#include "lldb/Utility/DataExtractor.h"
#include "lldb/Utility/Endian.h"
#include "lldb/Utility/RegisterValue.h"
#include "lldb/Utility/Scalar.h"
#include "llvm/Support/Compiler.h"

#include "RegisterContextPOSIX_x86.h"
#include "RegisterContext_x86.h"

using namespace lldb_private;
using namespace lldb;

const uint32_t g_gpr_regnums_i386[] = {
    lldb_eax_i386,       lldb_ebx_i386,    lldb_ecx_i386, lldb_edx_i386,
    lldb_edi_i386,       lldb_esi_i386,    lldb_ebp_i386, lldb_esp_i386,
    lldb_eip_i386,       lldb_eflags_i386, lldb_cs_i386,  lldb_fs_i386,
    lldb_gs_i386,        lldb_ss_i386,     lldb_ds_i386,  lldb_es_i386,
    lldb_ax_i386,        lldb_bx_i386,     lldb_cx_i386,  lldb_dx_i386,
    lldb_di_i386,        lldb_si_i386,     lldb_bp_i386,  lldb_sp_i386,
    lldb_ah_i386,        lldb_bh_i386,     lldb_ch_i386,  lldb_dh_i386,
    lldb_al_i386,        lldb_bl_i386,     lldb_cl_i386,  lldb_dl_i386,
    LLDB_INVALID_REGNUM, // Register sets must be terminated with
                         // LLDB_INVALID_REGNUM.
};
static_assert((sizeof(g_gpr_regnums_i386) / sizeof(g_gpr_regnums_i386[0])) -
                      1 ==
                  k_num_gpr_registers_i386,
              "g_gpr_regnums_i386 has wrong number of register infos");

const uint32_t g_lldb_regnums_i386[] = {
    lldb_fctrl_i386,    lldb_fstat_i386,     lldb_ftag_i386,  lldb_fop_i386,
    lldb_fiseg_i386,    lldb_fioff_i386,     lldb_foseg_i386, lldb_fooff_i386,
    lldb_mxcsr_i386,    lldb_mxcsrmask_i386, lldb_st0_i386,   lldb_st1_i386,
    lldb_st2_i386,      lldb_st3_i386,       lldb_st4_i386,   lldb_st5_i386,
    lldb_st6_i386,      lldb_st7_i386,       lldb_mm0_i386,   lldb_mm1_i386,
    lldb_mm2_i386,      lldb_mm3_i386,       lldb_mm4_i386,   lldb_mm5_i386,
    lldb_mm6_i386,      lldb_mm7_i386,       lldb_xmm0_i386,  lldb_xmm1_i386,
    lldb_xmm2_i386,     lldb_xmm3_i386,      lldb_xmm4_i386,  lldb_xmm5_i386,
    lldb_xmm6_i386,     lldb_xmm7_i386,
    LLDB_INVALID_REGNUM // Register sets must be terminated with
                        // LLDB_INVALID_REGNUM.
};
static_assert((sizeof(g_lldb_regnums_i386) / sizeof(g_lldb_regnums_i386[0])) -
                      1 ==
                  k_num_fpr_registers_i386,
              "g_lldb_regnums_i386 has wrong number of register infos");

const uint32_t g_avx_regnums_i386[] = {
    lldb_ymm0_i386,     lldb_ymm1_i386, lldb_ymm2_i386, lldb_ymm3_i386,
    lldb_ymm4_i386,     lldb_ymm5_i386, lldb_ymm6_i386, lldb_ymm7_i386,
    LLDB_INVALID_REGNUM // Register sets must be terminated with
                        // LLDB_INVALID_REGNUM.
};
static_assert((sizeof(g_avx_regnums_i386) / sizeof(g_avx_regnums_i386[0])) -
                      1 ==
                  k_num_avx_registers_i386,
              " g_avx_regnums_i386 has wrong number of register infos");

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
    LLDB_INVALID_REGNUM // Register sets must be terminated with
                        // LLDB_INVALID_REGNUM.
};
static_assert((sizeof(g_gpr_regnums_x86_64) / sizeof(g_gpr_regnums_x86_64[0])) -
                      1 ==
                  k_num_gpr_registers_x86_64,
              "g_gpr_regnums_x86_64 has wrong number of register infos");

static const uint32_t g_lldb_regnums_x86_64[] = {
    lldb_fctrl_x86_64,     lldb_fstat_x86_64, lldb_ftag_x86_64,
    lldb_fop_x86_64,       lldb_fiseg_x86_64, lldb_fioff_x86_64,
    lldb_foseg_x86_64,     lldb_fooff_x86_64, lldb_mxcsr_x86_64,
    lldb_mxcsrmask_x86_64, lldb_st0_x86_64,   lldb_st1_x86_64,
    lldb_st2_x86_64,       lldb_st3_x86_64,   lldb_st4_x86_64,
    lldb_st5_x86_64,       lldb_st6_x86_64,   lldb_st7_x86_64,
    lldb_mm0_x86_64,       lldb_mm1_x86_64,   lldb_mm2_x86_64,
    lldb_mm3_x86_64,       lldb_mm4_x86_64,   lldb_mm5_x86_64,
    lldb_mm6_x86_64,       lldb_mm7_x86_64,   lldb_xmm0_x86_64,
    lldb_xmm1_x86_64,      lldb_xmm2_x86_64,  lldb_xmm3_x86_64,
    lldb_xmm4_x86_64,      lldb_xmm5_x86_64,  lldb_xmm6_x86_64,
    lldb_xmm7_x86_64,      lldb_xmm8_x86_64,  lldb_xmm9_x86_64,
    lldb_xmm10_x86_64,     lldb_xmm11_x86_64, lldb_xmm12_x86_64,
    lldb_xmm13_x86_64,     lldb_xmm14_x86_64, lldb_xmm15_x86_64,
    LLDB_INVALID_REGNUM // Register sets must be terminated with
                        // LLDB_INVALID_REGNUM.
};
static_assert((sizeof(g_lldb_regnums_x86_64) /
               sizeof(g_lldb_regnums_x86_64[0])) -
                      1 ==
                  k_num_fpr_registers_x86_64,
              "g_lldb_regnums_x86_64 has wrong number of register infos");

static const uint32_t g_avx_regnums_x86_64[] = {
    lldb_ymm0_x86_64,   lldb_ymm1_x86_64,  lldb_ymm2_x86_64,  lldb_ymm3_x86_64,
    lldb_ymm4_x86_64,   lldb_ymm5_x86_64,  lldb_ymm6_x86_64,  lldb_ymm7_x86_64,
    lldb_ymm8_x86_64,   lldb_ymm9_x86_64,  lldb_ymm10_x86_64, lldb_ymm11_x86_64,
    lldb_ymm12_x86_64,  lldb_ymm13_x86_64, lldb_ymm14_x86_64, lldb_ymm15_x86_64,
    LLDB_INVALID_REGNUM // Register sets must be terminated with
                        // LLDB_INVALID_REGNUM.
};
static_assert((sizeof(g_avx_regnums_x86_64) / sizeof(g_avx_regnums_x86_64[0])) -
                      1 ==
                  k_num_avx_registers_x86_64,
              "g_avx_regnums_x86_64 has wrong number of register infos");

uint32_t RegisterContextPOSIX_x86::g_contained_eax[] = {lldb_eax_i386,
                                                        LLDB_INVALID_REGNUM};
uint32_t RegisterContextPOSIX_x86::g_contained_ebx[] = {lldb_ebx_i386,
                                                        LLDB_INVALID_REGNUM};
uint32_t RegisterContextPOSIX_x86::g_contained_ecx[] = {lldb_ecx_i386,
                                                        LLDB_INVALID_REGNUM};
uint32_t RegisterContextPOSIX_x86::g_contained_edx[] = {lldb_edx_i386,
                                                        LLDB_INVALID_REGNUM};
uint32_t RegisterContextPOSIX_x86::g_contained_edi[] = {lldb_edi_i386,
                                                        LLDB_INVALID_REGNUM};
uint32_t RegisterContextPOSIX_x86::g_contained_esi[] = {lldb_esi_i386,
                                                        LLDB_INVALID_REGNUM};
uint32_t RegisterContextPOSIX_x86::g_contained_ebp[] = {lldb_ebp_i386,
                                                        LLDB_INVALID_REGNUM};
uint32_t RegisterContextPOSIX_x86::g_contained_esp[] = {lldb_esp_i386,
                                                        LLDB_INVALID_REGNUM};

uint32_t RegisterContextPOSIX_x86::g_invalidate_eax[] = {
    lldb_eax_i386, lldb_ax_i386, lldb_ah_i386, lldb_al_i386,
    LLDB_INVALID_REGNUM};
uint32_t RegisterContextPOSIX_x86::g_invalidate_ebx[] = {
    lldb_ebx_i386, lldb_bx_i386, lldb_bh_i386, lldb_bl_i386,
    LLDB_INVALID_REGNUM};
uint32_t RegisterContextPOSIX_x86::g_invalidate_ecx[] = {
    lldb_ecx_i386, lldb_cx_i386, lldb_ch_i386, lldb_cl_i386,
    LLDB_INVALID_REGNUM};
uint32_t RegisterContextPOSIX_x86::g_invalidate_edx[] = {
    lldb_edx_i386, lldb_dx_i386, lldb_dh_i386, lldb_dl_i386,
    LLDB_INVALID_REGNUM};
uint32_t RegisterContextPOSIX_x86::g_invalidate_edi[] = {
    lldb_edi_i386, lldb_di_i386, LLDB_INVALID_REGNUM};
uint32_t RegisterContextPOSIX_x86::g_invalidate_esi[] = {
    lldb_esi_i386, lldb_si_i386, LLDB_INVALID_REGNUM};
uint32_t RegisterContextPOSIX_x86::g_invalidate_ebp[] = {
    lldb_ebp_i386, lldb_bp_i386, LLDB_INVALID_REGNUM};
uint32_t RegisterContextPOSIX_x86::g_invalidate_esp[] = {
    lldb_esp_i386, lldb_sp_i386, LLDB_INVALID_REGNUM};

uint32_t RegisterContextPOSIX_x86::g_contained_rax[] = {lldb_rax_x86_64,
                                                        LLDB_INVALID_REGNUM};
uint32_t RegisterContextPOSIX_x86::g_contained_rbx[] = {lldb_rbx_x86_64,
                                                        LLDB_INVALID_REGNUM};
uint32_t RegisterContextPOSIX_x86::g_contained_rcx[] = {lldb_rcx_x86_64,
                                                        LLDB_INVALID_REGNUM};
uint32_t RegisterContextPOSIX_x86::g_contained_rdx[] = {lldb_rdx_x86_64,
                                                        LLDB_INVALID_REGNUM};
uint32_t RegisterContextPOSIX_x86::g_contained_rdi[] = {lldb_rdi_x86_64,
                                                        LLDB_INVALID_REGNUM};
uint32_t RegisterContextPOSIX_x86::g_contained_rsi[] = {lldb_rsi_x86_64,
                                                        LLDB_INVALID_REGNUM};
uint32_t RegisterContextPOSIX_x86::g_contained_rbp[] = {lldb_rbp_x86_64,
                                                        LLDB_INVALID_REGNUM};
uint32_t RegisterContextPOSIX_x86::g_contained_rsp[] = {lldb_rsp_x86_64,
                                                        LLDB_INVALID_REGNUM};
uint32_t RegisterContextPOSIX_x86::g_contained_r8[] = {lldb_r8_x86_64,
                                                       LLDB_INVALID_REGNUM};
uint32_t RegisterContextPOSIX_x86::g_contained_r9[] = {lldb_r9_x86_64,
                                                       LLDB_INVALID_REGNUM};
uint32_t RegisterContextPOSIX_x86::g_contained_r10[] = {lldb_r10_x86_64,
                                                        LLDB_INVALID_REGNUM};
uint32_t RegisterContextPOSIX_x86::g_contained_r11[] = {lldb_r11_x86_64,
                                                        LLDB_INVALID_REGNUM};
uint32_t RegisterContextPOSIX_x86::g_contained_r12[] = {lldb_r12_x86_64,
                                                        LLDB_INVALID_REGNUM};
uint32_t RegisterContextPOSIX_x86::g_contained_r13[] = {lldb_r13_x86_64,
                                                        LLDB_INVALID_REGNUM};
uint32_t RegisterContextPOSIX_x86::g_contained_r14[] = {lldb_r14_x86_64,
                                                        LLDB_INVALID_REGNUM};
uint32_t RegisterContextPOSIX_x86::g_contained_r15[] = {lldb_r15_x86_64,
                                                        LLDB_INVALID_REGNUM};

uint32_t RegisterContextPOSIX_x86::g_invalidate_rax[] = {
    lldb_rax_x86_64, lldb_eax_x86_64, lldb_ax_x86_64,
    lldb_ah_x86_64,  lldb_al_x86_64,  LLDB_INVALID_REGNUM};
uint32_t RegisterContextPOSIX_x86::g_invalidate_rbx[] = {
    lldb_rbx_x86_64, lldb_ebx_x86_64, lldb_bx_x86_64,
    lldb_bh_x86_64,  lldb_bl_x86_64,  LLDB_INVALID_REGNUM};
uint32_t RegisterContextPOSIX_x86::g_invalidate_rcx[] = {
    lldb_rcx_x86_64, lldb_ecx_x86_64, lldb_cx_x86_64,
    lldb_ch_x86_64,  lldb_cl_x86_64,  LLDB_INVALID_REGNUM};
uint32_t RegisterContextPOSIX_x86::g_invalidate_rdx[] = {
    lldb_rdx_x86_64, lldb_edx_x86_64, lldb_dx_x86_64,
    lldb_dh_x86_64,  lldb_dl_x86_64,  LLDB_INVALID_REGNUM};
uint32_t RegisterContextPOSIX_x86::g_invalidate_rdi[] = {
    lldb_rdi_x86_64, lldb_edi_x86_64, lldb_di_x86_64, lldb_dil_x86_64,
    LLDB_INVALID_REGNUM};
uint32_t RegisterContextPOSIX_x86::g_invalidate_rsi[] = {
    lldb_rsi_x86_64, lldb_esi_x86_64, lldb_si_x86_64, lldb_sil_x86_64,
    LLDB_INVALID_REGNUM};
uint32_t RegisterContextPOSIX_x86::g_invalidate_rbp[] = {
    lldb_rbp_x86_64, lldb_ebp_x86_64, lldb_bp_x86_64, lldb_bpl_x86_64,
    LLDB_INVALID_REGNUM};
uint32_t RegisterContextPOSIX_x86::g_invalidate_rsp[] = {
    lldb_rsp_x86_64, lldb_esp_x86_64, lldb_sp_x86_64, lldb_spl_x86_64,
    LLDB_INVALID_REGNUM};
uint32_t RegisterContextPOSIX_x86::g_invalidate_r8[] = {
    lldb_r8_x86_64, lldb_r8d_x86_64, lldb_r8w_x86_64, lldb_r8l_x86_64,
    LLDB_INVALID_REGNUM};
uint32_t RegisterContextPOSIX_x86::g_invalidate_r9[] = {
    lldb_r9_x86_64, lldb_r9d_x86_64, lldb_r9w_x86_64, lldb_r9l_x86_64,
    LLDB_INVALID_REGNUM};
uint32_t RegisterContextPOSIX_x86::g_invalidate_r10[] = {
    lldb_r10_x86_64, lldb_r10d_x86_64, lldb_r10w_x86_64, lldb_r10l_x86_64,
    LLDB_INVALID_REGNUM};
uint32_t RegisterContextPOSIX_x86::g_invalidate_r11[] = {
    lldb_r11_x86_64, lldb_r11d_x86_64, lldb_r11w_x86_64, lldb_r11l_x86_64,
    LLDB_INVALID_REGNUM};
uint32_t RegisterContextPOSIX_x86::g_invalidate_r12[] = {
    lldb_r12_x86_64, lldb_r12d_x86_64, lldb_r12w_x86_64, lldb_r12l_x86_64,
    LLDB_INVALID_REGNUM};
uint32_t RegisterContextPOSIX_x86::g_invalidate_r13[] = {
    lldb_r13_x86_64, lldb_r13d_x86_64, lldb_r13w_x86_64, lldb_r13l_x86_64,
    LLDB_INVALID_REGNUM};
uint32_t RegisterContextPOSIX_x86::g_invalidate_r14[] = {
    lldb_r14_x86_64, lldb_r14d_x86_64, lldb_r14w_x86_64, lldb_r14l_x86_64,
    LLDB_INVALID_REGNUM};
uint32_t RegisterContextPOSIX_x86::g_invalidate_r15[] = {
    lldb_r15_x86_64, lldb_r15d_x86_64, lldb_r15w_x86_64, lldb_r15l_x86_64,
    LLDB_INVALID_REGNUM};

// Number of register sets provided by this context.
enum { k_num_extended_register_sets = 1, k_num_register_sets = 3 };

static const RegisterSet g_reg_sets_i386[k_num_register_sets] = {
    {"General Purpose Registers", "gpr", k_num_gpr_registers_i386,
     g_gpr_regnums_i386},
    {"Floating Point Registers", "fpu", k_num_fpr_registers_i386,
     g_lldb_regnums_i386},
    {"Advanced Vector Extensions", "avx", k_num_avx_registers_i386,
     g_avx_regnums_i386}};

static const RegisterSet g_reg_sets_x86_64[k_num_register_sets] = {
    {"General Purpose Registers", "gpr", k_num_gpr_registers_x86_64,
     g_gpr_regnums_x86_64},
    {"Floating Point Registers", "fpu", k_num_fpr_registers_x86_64,
     g_lldb_regnums_x86_64},
    {"Advanced Vector Extensions", "avx", k_num_avx_registers_x86_64,
     g_avx_regnums_x86_64}};

bool RegisterContextPOSIX_x86::IsGPR(unsigned reg) {
  return reg <= m_reg_info.last_gpr; // GPR's come first.
}

bool RegisterContextPOSIX_x86::IsFPR(unsigned reg) {
  return (m_reg_info.first_fpr <= reg && reg <= m_reg_info.last_fpr);
}

bool RegisterContextPOSIX_x86::IsAVX(unsigned reg) {
  return (m_reg_info.first_ymm <= reg && reg <= m_reg_info.last_ymm);
}

bool RegisterContextPOSIX_x86::IsFPR(unsigned reg, FPRType fpr_type) {
  bool generic_fpr = IsFPR(reg);

  if (fpr_type == eXSAVE)
    return generic_fpr || IsAVX(reg);
  return generic_fpr;
}

RegisterContextPOSIX_x86::RegisterContextPOSIX_x86(
    Thread &thread, uint32_t concrete_frame_idx,
    RegisterInfoInterface *register_info)
    : RegisterContext(thread, concrete_frame_idx) {
  m_register_info_ap.reset(register_info);

  switch (register_info->m_target_arch.GetMachine()) {
  case llvm::Triple::x86:
    m_reg_info.num_registers = k_num_registers_i386;
    m_reg_info.num_gpr_registers = k_num_gpr_registers_i386;
    m_reg_info.num_fpr_registers = k_num_fpr_registers_i386;
    m_reg_info.num_avx_registers = k_num_avx_registers_i386;
    m_reg_info.last_gpr = k_last_gpr_i386;
    m_reg_info.first_fpr = k_first_fpr_i386;
    m_reg_info.last_fpr = k_last_fpr_i386;
    m_reg_info.first_st = lldb_st0_i386;
    m_reg_info.last_st = lldb_st7_i386;
    m_reg_info.first_mm = lldb_mm0_i386;
    m_reg_info.last_mm = lldb_mm7_i386;
    m_reg_info.first_xmm = lldb_xmm0_i386;
    m_reg_info.last_xmm = lldb_xmm7_i386;
    m_reg_info.first_ymm = lldb_ymm0_i386;
    m_reg_info.last_ymm = lldb_ymm7_i386;
    m_reg_info.first_dr = lldb_dr0_i386;
    m_reg_info.gpr_flags = lldb_eflags_i386;
    break;
  case llvm::Triple::x86_64:
    m_reg_info.num_registers = k_num_registers_x86_64;
    m_reg_info.num_gpr_registers = k_num_gpr_registers_x86_64;
    m_reg_info.num_fpr_registers = k_num_fpr_registers_x86_64;
    m_reg_info.num_avx_registers = k_num_avx_registers_x86_64;
    m_reg_info.last_gpr = k_last_gpr_x86_64;
    m_reg_info.first_fpr = k_first_fpr_x86_64;
    m_reg_info.last_fpr = k_last_fpr_x86_64;
    m_reg_info.first_st = lldb_st0_x86_64;
    m_reg_info.last_st = lldb_st7_x86_64;
    m_reg_info.first_mm = lldb_mm0_x86_64;
    m_reg_info.last_mm = lldb_mm7_x86_64;
    m_reg_info.first_xmm = lldb_xmm0_x86_64;
    m_reg_info.last_xmm = lldb_xmm15_x86_64;
    m_reg_info.first_ymm = lldb_ymm0_x86_64;
    m_reg_info.last_ymm = lldb_ymm15_x86_64;
    m_reg_info.first_dr = lldb_dr0_x86_64;
    m_reg_info.gpr_flags = lldb_rflags_x86_64;
    break;
  default:
    assert(false && "Unhandled target architecture.");
    break;
  }

  ::memset(&m_fpr, 0, sizeof(FPR));

  m_fpr_type = eNotValid;
}

RegisterContextPOSIX_x86::~RegisterContextPOSIX_x86() {}

RegisterContextPOSIX_x86::FPRType RegisterContextPOSIX_x86::GetFPRType() {
  if (m_fpr_type == eNotValid) {
    // TODO: Use assembly to call cpuid on the inferior and query ebx or ecx
    m_fpr_type = eXSAVE; // extended floating-point registers, if available
    if (!ReadFPR())
      m_fpr_type = eFXSAVE; // assume generic floating-point registers
  }
  return m_fpr_type;
}

void RegisterContextPOSIX_x86::Invalidate() {}

void RegisterContextPOSIX_x86::InvalidateAllRegisters() {}

unsigned RegisterContextPOSIX_x86::GetRegisterOffset(unsigned reg) {
  assert(reg < m_reg_info.num_registers && "Invalid register number.");
  return GetRegisterInfo()[reg].byte_offset;
}

unsigned RegisterContextPOSIX_x86::GetRegisterSize(unsigned reg) {
  assert(reg < m_reg_info.num_registers && "Invalid register number.");
  return GetRegisterInfo()[reg].byte_size;
}

size_t RegisterContextPOSIX_x86::GetRegisterCount() {
  size_t num_registers =
      m_reg_info.num_gpr_registers + m_reg_info.num_fpr_registers;
  if (GetFPRType() == eXSAVE)
    return num_registers + m_reg_info.num_avx_registers;
  return num_registers;
}

size_t RegisterContextPOSIX_x86::GetGPRSize() {
  return m_register_info_ap->GetGPRSize();
}

size_t RegisterContextPOSIX_x86::GetFXSAVEOffset() {
  return GetRegisterInfo()[m_reg_info.first_fpr].byte_offset;
}

const RegisterInfo *RegisterContextPOSIX_x86::GetRegisterInfo() {
  // Commonly, this method is overridden and g_register_infos is copied and
  // specialized. So, use GetRegisterInfo() rather than g_register_infos in
  // this scope.
  return m_register_info_ap->GetRegisterInfo();
}

const RegisterInfo *
RegisterContextPOSIX_x86::GetRegisterInfoAtIndex(size_t reg) {
  if (reg < m_reg_info.num_registers)
    return &GetRegisterInfo()[reg];
  else
    return NULL;
}

size_t RegisterContextPOSIX_x86::GetRegisterSetCount() {
  size_t sets = 0;
  for (size_t set = 0; set < k_num_register_sets; ++set) {
    if (IsRegisterSetAvailable(set))
      ++sets;
  }

  return sets;
}

const RegisterSet *RegisterContextPOSIX_x86::GetRegisterSet(size_t set) {
  if (IsRegisterSetAvailable(set)) {
    switch (m_register_info_ap->m_target_arch.GetMachine()) {
    case llvm::Triple::x86:
      return &g_reg_sets_i386[set];
    case llvm::Triple::x86_64:
      return &g_reg_sets_x86_64[set];
    default:
      assert(false && "Unhandled target architecture.");
      return NULL;
    }
  }
  return NULL;
}

const char *RegisterContextPOSIX_x86::GetRegisterName(unsigned reg) {
  assert(reg < m_reg_info.num_registers && "Invalid register offset.");
  return GetRegisterInfo()[reg].name;
}

lldb::ByteOrder RegisterContextPOSIX_x86::GetByteOrder() {
  // Get the target process whose privileged thread was used for the register
  // read.
  lldb::ByteOrder byte_order = eByteOrderInvalid;
  Process *process = CalculateProcess().get();

  if (process)
    byte_order = process->GetByteOrder();
  return byte_order;
}

// Parse ymm registers and into xmm.bytes and ymmh.bytes.
bool RegisterContextPOSIX_x86::CopyYMMtoXSTATE(uint32_t reg,
                                               lldb::ByteOrder byte_order) {
  if (!IsAVX(reg))
    return false;

  if (byte_order == eByteOrderLittle) {
    ::memcpy(m_fpr.fxsave.xmm[reg - m_reg_info.first_ymm].bytes,
             m_ymm_set.ymm[reg - m_reg_info.first_ymm].bytes, sizeof(XMMReg));
    ::memcpy(m_fpr.xsave.ymmh[reg - m_reg_info.first_ymm].bytes,
             m_ymm_set.ymm[reg - m_reg_info.first_ymm].bytes + sizeof(XMMReg),
             sizeof(YMMHReg));
    return true;
  }

  if (byte_order == eByteOrderBig) {
    ::memcpy(m_fpr.fxsave.xmm[reg - m_reg_info.first_ymm].bytes,
             m_ymm_set.ymm[reg - m_reg_info.first_ymm].bytes + sizeof(XMMReg),
             sizeof(XMMReg));
    ::memcpy(m_fpr.xsave.ymmh[reg - m_reg_info.first_ymm].bytes,
             m_ymm_set.ymm[reg - m_reg_info.first_ymm].bytes, sizeof(YMMHReg));
    return true;
  }
  return false; // unsupported or invalid byte order
}

// Concatenate xmm.bytes with ymmh.bytes
bool RegisterContextPOSIX_x86::CopyXSTATEtoYMM(uint32_t reg,
                                               lldb::ByteOrder byte_order) {
  if (!IsAVX(reg))
    return false;

  if (byte_order == eByteOrderLittle) {
    ::memcpy(m_ymm_set.ymm[reg - m_reg_info.first_ymm].bytes,
             m_fpr.fxsave.xmm[reg - m_reg_info.first_ymm].bytes,
             sizeof(XMMReg));
    ::memcpy(m_ymm_set.ymm[reg - m_reg_info.first_ymm].bytes + sizeof(XMMReg),
             m_fpr.xsave.ymmh[reg - m_reg_info.first_ymm].bytes,
             sizeof(YMMHReg));
    return true;
  }

  if (byte_order == eByteOrderBig) {
    ::memcpy(m_ymm_set.ymm[reg - m_reg_info.first_ymm].bytes + sizeof(XMMReg),
             m_fpr.fxsave.xmm[reg - m_reg_info.first_ymm].bytes,
             sizeof(XMMReg));
    ::memcpy(m_ymm_set.ymm[reg - m_reg_info.first_ymm].bytes,
             m_fpr.xsave.ymmh[reg - m_reg_info.first_ymm].bytes,
             sizeof(YMMHReg));
    return true;
  }
  return false; // unsupported or invalid byte order
}

bool RegisterContextPOSIX_x86::IsRegisterSetAvailable(size_t set_index) {
  // Note: Extended register sets are assumed to be at the end of g_reg_sets...
  size_t num_sets = k_num_register_sets - k_num_extended_register_sets;

  if (GetFPRType() == eXSAVE) // ...and to start with AVX registers.
    ++num_sets;
  return (set_index < num_sets);
}

// Used when parsing DWARF and EH frame information and any other object file
// sections that contain register numbers in them.
uint32_t RegisterContextPOSIX_x86::ConvertRegisterKindToRegisterNumber(
    lldb::RegisterKind kind, uint32_t num) {
  const uint32_t num_regs = GetRegisterCount();

  assert(kind < kNumRegisterKinds);
  for (uint32_t reg_idx = 0; reg_idx < num_regs; ++reg_idx) {
    const RegisterInfo *reg_info = GetRegisterInfoAtIndex(reg_idx);

    if (reg_info->kinds[kind] == num)
      return reg_idx;
  }

  return LLDB_INVALID_REGNUM;
}
