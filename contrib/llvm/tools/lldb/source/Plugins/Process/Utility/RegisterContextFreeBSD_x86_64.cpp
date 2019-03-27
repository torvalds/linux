//===-- RegisterContextFreeBSD_x86_64.cpp ----------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===---------------------------------------------------------------------===//

#include "RegisterContextFreeBSD_x86_64.h"
#include "RegisterContextFreeBSD_i386.h"
#include "RegisterContextPOSIX_x86.h"
#include <vector>

using namespace lldb_private;
using namespace lldb;

// http://svnweb.freebsd.org/base/head/sys/x86/include/reg.h
typedef struct _GPR {
  uint64_t r15;
  uint64_t r14;
  uint64_t r13;
  uint64_t r12;
  uint64_t r11;
  uint64_t r10;
  uint64_t r9;
  uint64_t r8;
  uint64_t rdi;
  uint64_t rsi;
  uint64_t rbp;
  uint64_t rbx;
  uint64_t rdx;
  uint64_t rcx;
  uint64_t rax;
  uint32_t trapno;
  uint16_t fs;
  uint16_t gs;
  uint32_t err;
  uint16_t es;
  uint16_t ds;
  uint64_t rip;
  uint64_t cs;
  uint64_t rflags;
  uint64_t rsp;
  uint64_t ss;
} GPR;

struct DBG {
  uint64_t dr[16]; /* debug registers */
                   /* Index 0-3: debug address registers */
                   /* Index 4-5: reserved */
                   /* Index 6: debug status */
                   /* Index 7: debug control */
                   /* Index 8-15: reserved */
};

struct UserArea {
  GPR gpr;
  FPR fpr;
  DBG dbg;
};

#define DR_OFFSET(reg_index) (LLVM_EXTENSION offsetof(DBG, dr[reg_index]))

//---------------------------------------------------------------------------
// Include RegisterInfos_x86_64 to declare our g_register_infos_x86_64
// structure.
//---------------------------------------------------------------------------
#define DECLARE_REGISTER_INFOS_X86_64_STRUCT
#include "RegisterInfos_x86_64.h"
#undef DECLARE_REGISTER_INFOS_X86_64_STRUCT

static std::vector<lldb_private::RegisterInfo> &GetSharedRegisterInfoVector() {
  static std::vector<lldb_private::RegisterInfo> register_infos;
  return register_infos;
}

static const RegisterInfo *
GetRegisterInfo_i386(const lldb_private::ArchSpec &arch) {
  static std::vector<lldb_private::RegisterInfo> g_register_infos(
      GetSharedRegisterInfoVector());

  // Allocate RegisterInfo only once
  if (g_register_infos.empty()) {
    // Copy the register information from base class
    std::unique_ptr<RegisterContextFreeBSD_i386> reg_interface(
        new RegisterContextFreeBSD_i386(arch));
    const RegisterInfo *base_info = reg_interface->GetRegisterInfo();
    g_register_infos.insert(g_register_infos.end(), &base_info[0],
                            &base_info[k_num_registers_i386]);

//---------------------------------------------------------------------------
// Include RegisterInfos_x86_64 to update the g_register_infos structure
//  with x86_64 offsets.
//---------------------------------------------------------------------------
#define UPDATE_REGISTER_INFOS_I386_STRUCT_WITH_X86_64_OFFSETS
#include "RegisterInfos_x86_64.h"
#undef UPDATE_REGISTER_INFOS_I386_STRUCT_WITH_X86_64_OFFSETS
  }

  return &g_register_infos[0];
}

static const RegisterInfo *
PrivateGetRegisterInfoPtr(const lldb_private::ArchSpec &target_arch) {
  switch (target_arch.GetMachine()) {
  case llvm::Triple::x86:
    return GetRegisterInfo_i386(target_arch);
  case llvm::Triple::x86_64:
    return g_register_infos_x86_64;
  default:
    assert(false && "Unhandled target architecture.");
    return nullptr;
  }
}

static uint32_t
PrivateGetRegisterCount(const lldb_private::ArchSpec &target_arch) {
  switch (target_arch.GetMachine()) {
  case llvm::Triple::x86:
    // This vector should have already been filled.
    assert(!GetSharedRegisterInfoVector().empty() &&
           "i386 register info vector not filled.");
    return static_cast<uint32_t>(GetSharedRegisterInfoVector().size());
  case llvm::Triple::x86_64:
    return static_cast<uint32_t>(sizeof(g_register_infos_x86_64) /
                                 sizeof(g_register_infos_x86_64[0]));
  default:
    assert(false && "Unhandled target architecture.");
    return 0;
  }
}

RegisterContextFreeBSD_x86_64::RegisterContextFreeBSD_x86_64(
    const ArchSpec &target_arch)
    : lldb_private::RegisterInfoInterface(target_arch),
      m_register_info_p(PrivateGetRegisterInfoPtr(target_arch)),
      m_register_count(PrivateGetRegisterCount(target_arch)) {}

size_t RegisterContextFreeBSD_x86_64::GetGPRSize() const { return sizeof(GPR); }

const RegisterInfo *RegisterContextFreeBSD_x86_64::GetRegisterInfo() const {
  return m_register_info_p;
}

uint32_t RegisterContextFreeBSD_x86_64::GetRegisterCount() const {
  return m_register_count;
}
