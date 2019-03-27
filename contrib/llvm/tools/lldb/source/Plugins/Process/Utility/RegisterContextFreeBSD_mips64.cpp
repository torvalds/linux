//===-- RegisterContextFreeBSD_mips64.cpp ----------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===---------------------------------------------------------------------===//

#include "RegisterContextFreeBSD_mips64.h"
#include "RegisterContextPOSIX_mips64.h"
#include "lldb-mips-freebsd-register-enums.h"
#include <vector>

using namespace lldb_private;
using namespace lldb;

static const uint32_t g_gpr_regnums[] = {
    gpr_zero_mips64,  gpr_r1_mips64,    gpr_r2_mips64,    gpr_r3_mips64,
    gpr_r4_mips64,    gpr_r5_mips64,    gpr_r6_mips64,    gpr_r7_mips64,
    gpr_r8_mips64,    gpr_r9_mips64,    gpr_r10_mips64,   gpr_r11_mips64,
    gpr_r12_mips64,   gpr_r13_mips64,   gpr_r14_mips64,   gpr_r15_mips64,
    gpr_r16_mips64,   gpr_r17_mips64,   gpr_r18_mips64,   gpr_r19_mips64,
    gpr_r20_mips64,   gpr_r21_mips64,   gpr_r22_mips64,   gpr_r23_mips64,
    gpr_r24_mips64,   gpr_r25_mips64,   gpr_r26_mips64,   gpr_r27_mips64,
    gpr_gp_mips64,    gpr_sp_mips64,    gpr_r30_mips64,   gpr_ra_mips64,
    gpr_sr_mips64,    gpr_mullo_mips64, gpr_mulhi_mips64, gpr_badvaddr_mips64,
    gpr_cause_mips64, gpr_pc_mips64,    gpr_ic_mips64,    gpr_dummy_mips64};

// Number of register sets provided by this context.
constexpr size_t k_num_register_sets = 1;

static const RegisterSet g_reg_sets_mips64[k_num_register_sets] = {
    {"General Purpose Registers", "gpr", k_num_gpr_registers_mips64,
     g_gpr_regnums},
};


// http://svnweb.freebsd.org/base/head/sys/mips/include/regnum.h
typedef struct _GPR {
  uint64_t zero;
  uint64_t r1;
  uint64_t r2;
  uint64_t r3;
  uint64_t r4;
  uint64_t r5;
  uint64_t r6;
  uint64_t r7;
  uint64_t r8;
  uint64_t r9;
  uint64_t r10;
  uint64_t r11;
  uint64_t r12;
  uint64_t r13;
  uint64_t r14;
  uint64_t r15;
  uint64_t r16;
  uint64_t r17;
  uint64_t r18;
  uint64_t r19;
  uint64_t r20;
  uint64_t r21;
  uint64_t r22;
  uint64_t r23;
  uint64_t r24;
  uint64_t r25;
  uint64_t r26;
  uint64_t r27;
  uint64_t gp;
  uint64_t sp;
  uint64_t r30;
  uint64_t ra;
  uint64_t sr;
  uint64_t mullo;
  uint64_t mulhi;
  uint64_t badvaddr;
  uint64_t cause;
  uint64_t pc;
  uint64_t ic;
  uint64_t dummy;
} GPR_freebsd_mips;

//---------------------------------------------------------------------------
// Include RegisterInfos_mips64 to declare our g_register_infos_mips64
// structure.
//---------------------------------------------------------------------------
#define DECLARE_REGISTER_INFOS_MIPS64_STRUCT
#include "RegisterInfos_mips64.h"
#undef DECLARE_REGISTER_INFOS_MIPS64_STRUCT

RegisterContextFreeBSD_mips64::RegisterContextFreeBSD_mips64(
    const ArchSpec &target_arch)
    : RegisterInfoInterface(target_arch) {}

size_t RegisterContextFreeBSD_mips64::GetGPRSize() const {
  return sizeof(GPR_freebsd_mips);
}

const RegisterSet *
RegisterContextFreeBSD_mips64::GetRegisterSet(size_t set) const {
   // Check if RegisterSet is available
   if (set < k_num_register_sets)
     return &g_reg_sets_mips64[set];
   return nullptr;
}

size_t
RegisterContextFreeBSD_mips64::GetRegisterSetCount() const {
  return k_num_register_sets;
}

const RegisterInfo *RegisterContextFreeBSD_mips64::GetRegisterInfo() const {
  assert(m_target_arch.GetCore() == ArchSpec::eCore_mips64);
  return g_register_infos_mips64;
}

uint32_t RegisterContextFreeBSD_mips64::GetRegisterCount() const {
  return static_cast<uint32_t>(sizeof(g_register_infos_mips64) /
                               sizeof(g_register_infos_mips64[0]));
}
