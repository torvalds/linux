//===-- ABISysV_x86_64.cpp --------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "ABISysV_x86_64.h"

#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/StringSwitch.h"
#include "llvm/ADT/Triple.h"

#include "lldb/Core/Module.h"
#include "lldb/Core/PluginManager.h"
#include "lldb/Core/Value.h"
#include "lldb/Core/ValueObjectConstResult.h"
#include "lldb/Core/ValueObjectMemory.h"
#include "lldb/Core/ValueObjectRegister.h"
#include "lldb/Symbol/UnwindPlan.h"
#include "lldb/Target/Process.h"
#include "lldb/Target/RegisterContext.h"
#include "lldb/Target/StackFrame.h"
#include "lldb/Target/Target.h"
#include "lldb/Target/Thread.h"
#include "lldb/Utility/ConstString.h"
#include "lldb/Utility/DataExtractor.h"
#include "lldb/Utility/Log.h"
#include "lldb/Utility/RegisterValue.h"
#include "lldb/Utility/Status.h"

using namespace lldb;
using namespace lldb_private;

enum dwarf_regnums {
  dwarf_rax = 0,
  dwarf_rdx,
  dwarf_rcx,
  dwarf_rbx,
  dwarf_rsi,
  dwarf_rdi,
  dwarf_rbp,
  dwarf_rsp,
  dwarf_r8,
  dwarf_r9,
  dwarf_r10,
  dwarf_r11,
  dwarf_r12,
  dwarf_r13,
  dwarf_r14,
  dwarf_r15,
  dwarf_rip,
  dwarf_xmm0,
  dwarf_xmm1,
  dwarf_xmm2,
  dwarf_xmm3,
  dwarf_xmm4,
  dwarf_xmm5,
  dwarf_xmm6,
  dwarf_xmm7,
  dwarf_xmm8,
  dwarf_xmm9,
  dwarf_xmm10,
  dwarf_xmm11,
  dwarf_xmm12,
  dwarf_xmm13,
  dwarf_xmm14,
  dwarf_xmm15,
  dwarf_stmm0,
  dwarf_stmm1,
  dwarf_stmm2,
  dwarf_stmm3,
  dwarf_stmm4,
  dwarf_stmm5,
  dwarf_stmm6,
  dwarf_stmm7,
  dwarf_ymm0,
  dwarf_ymm1,
  dwarf_ymm2,
  dwarf_ymm3,
  dwarf_ymm4,
  dwarf_ymm5,
  dwarf_ymm6,
  dwarf_ymm7,
  dwarf_ymm8,
  dwarf_ymm9,
  dwarf_ymm10,
  dwarf_ymm11,
  dwarf_ymm12,
  dwarf_ymm13,
  dwarf_ymm14,
  dwarf_ymm15,
  dwarf_bnd0 = 126,
  dwarf_bnd1,
  dwarf_bnd2,
  dwarf_bnd3
};

static RegisterInfo g_register_infos[] = {
    //  NAME      ALT      SZ OFF ENCODING         FORMAT              EH_FRAME
    //  DWARF                 GENERIC                     PROCESS PLUGIN
    //  LLDB NATIVE
    //  ========  =======  == === =============    ===================
    //  ======================= =====================
    //  =========================== ===================== ======================
    {"rax",
     nullptr,
     8,
     0,
     eEncodingUint,
     eFormatHex,
     {dwarf_rax, dwarf_rax, LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM,
      LLDB_INVALID_REGNUM},
     nullptr,
     nullptr,
     nullptr,
     0},
    {"rbx",
     nullptr,
     8,
     0,
     eEncodingUint,
     eFormatHex,
     {dwarf_rbx, dwarf_rbx, LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM,
      LLDB_INVALID_REGNUM},
     nullptr,
     nullptr,
     nullptr,
     0},
    {"rcx",
     "arg4",
     8,
     0,
     eEncodingUint,
     eFormatHex,
     {dwarf_rcx, dwarf_rcx, LLDB_REGNUM_GENERIC_ARG4, LLDB_INVALID_REGNUM,
      LLDB_INVALID_REGNUM},
     nullptr,
     nullptr,
     nullptr,
     0},
    {"rdx",
     "arg3",
     8,
     0,
     eEncodingUint,
     eFormatHex,
     {dwarf_rdx, dwarf_rdx, LLDB_REGNUM_GENERIC_ARG3, LLDB_INVALID_REGNUM,
      LLDB_INVALID_REGNUM},
     nullptr,
     nullptr,
     nullptr,
     0},
    {"rsi",
     "arg2",
     8,
     0,
     eEncodingUint,
     eFormatHex,
     {dwarf_rsi, dwarf_rsi, LLDB_REGNUM_GENERIC_ARG2, LLDB_INVALID_REGNUM,
      LLDB_INVALID_REGNUM},
     nullptr,
     nullptr,
     nullptr,
     0},
    {"rdi",
     "arg1",
     8,
     0,
     eEncodingUint,
     eFormatHex,
     {dwarf_rdi, dwarf_rdi, LLDB_REGNUM_GENERIC_ARG1, LLDB_INVALID_REGNUM,
      LLDB_INVALID_REGNUM},
     nullptr,
     nullptr,
     nullptr,
     0},
    {"rbp",
     "fp",
     8,
     0,
     eEncodingUint,
     eFormatHex,
     {dwarf_rbp, dwarf_rbp, LLDB_REGNUM_GENERIC_FP, LLDB_INVALID_REGNUM,
      LLDB_INVALID_REGNUM},
     nullptr,
     nullptr,
     nullptr,
     0},
    {"rsp",
     "sp",
     8,
     0,
     eEncodingUint,
     eFormatHex,
     {dwarf_rsp, dwarf_rsp, LLDB_REGNUM_GENERIC_SP, LLDB_INVALID_REGNUM,
      LLDB_INVALID_REGNUM},
     nullptr,
     nullptr,
     nullptr,
     0},
    {"r8",
     "arg5",
     8,
     0,
     eEncodingUint,
     eFormatHex,
     {dwarf_r8, dwarf_r8, LLDB_REGNUM_GENERIC_ARG5, LLDB_INVALID_REGNUM,
      LLDB_INVALID_REGNUM},
     nullptr,
     nullptr,
     nullptr,
     0},
    {"r9",
     "arg6",
     8,
     0,
     eEncodingUint,
     eFormatHex,
     {dwarf_r9, dwarf_r9, LLDB_REGNUM_GENERIC_ARG6, LLDB_INVALID_REGNUM,
      LLDB_INVALID_REGNUM},
     nullptr,
     nullptr,
     nullptr,
     0},
    {"r10",
     nullptr,
     8,
     0,
     eEncodingUint,
     eFormatHex,
     {dwarf_r10, dwarf_r10, LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM,
      LLDB_INVALID_REGNUM},
     nullptr,
     nullptr,
     nullptr,
     0},
    {"r11",
     nullptr,
     8,
     0,
     eEncodingUint,
     eFormatHex,
     {dwarf_r11, dwarf_r11, LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM,
      LLDB_INVALID_REGNUM},
     nullptr,
     nullptr,
     nullptr,
     0},
    {"r12",
     nullptr,
     8,
     0,
     eEncodingUint,
     eFormatHex,
     {dwarf_r12, dwarf_r12, LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM,
      LLDB_INVALID_REGNUM},
     nullptr,
     nullptr,
     nullptr,
     0},
    {"r13",
     nullptr,
     8,
     0,
     eEncodingUint,
     eFormatHex,
     {dwarf_r13, dwarf_r13, LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM,
      LLDB_INVALID_REGNUM},
     nullptr,
     nullptr,
     nullptr,
     0},
    {"r14",
     nullptr,
     8,
     0,
     eEncodingUint,
     eFormatHex,
     {dwarf_r14, dwarf_r14, LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM,
      LLDB_INVALID_REGNUM},
     nullptr,
     nullptr,
     nullptr,
     0},
    {"r15",
     nullptr,
     8,
     0,
     eEncodingUint,
     eFormatHex,
     {dwarf_r15, dwarf_r15, LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM,
      LLDB_INVALID_REGNUM},
     nullptr,
     nullptr,
     nullptr,
     0},
    {"rip",
     "pc",
     8,
     0,
     eEncodingUint,
     eFormatHex,
     {dwarf_rip, dwarf_rip, LLDB_REGNUM_GENERIC_PC, LLDB_INVALID_REGNUM,
      LLDB_INVALID_REGNUM},
     nullptr,
     nullptr,
     nullptr,
     0},
    {"rflags",
     nullptr,
     4,
     0,
     eEncodingUint,
     eFormatHex,
     {LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM, LLDB_REGNUM_GENERIC_FLAGS,
      LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM},
     nullptr,
     nullptr,
     nullptr,
     0},
    {"cs",
     nullptr,
     4,
     0,
     eEncodingUint,
     eFormatHex,
     {LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM,
      LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM},
     nullptr,
     nullptr,
     nullptr,
     0},
    {"ss",
     nullptr,
     4,
     0,
     eEncodingUint,
     eFormatHex,
     {LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM,
      LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM},
     nullptr,
     nullptr,
     nullptr,
     0},
    {"ds",
     nullptr,
     4,
     0,
     eEncodingUint,
     eFormatHex,
     {LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM,
      LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM},
     nullptr,
     nullptr,
     nullptr,
     0},
    {"es",
     nullptr,
     4,
     0,
     eEncodingUint,
     eFormatHex,
     {LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM,
      LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM},
     nullptr,
     nullptr,
     nullptr,
     0},
    {"fs",
     nullptr,
     4,
     0,
     eEncodingUint,
     eFormatHex,
     {LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM,
      LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM},
     nullptr,
     nullptr,
     nullptr,
     0},
    {"gs",
     nullptr,
     4,
     0,
     eEncodingUint,
     eFormatHex,
     {LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM,
      LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM},
     nullptr,
     nullptr,
     nullptr,
     0},
    {"stmm0",
     nullptr,
     10,
     0,
     eEncodingVector,
     eFormatVectorOfUInt8,
     {dwarf_stmm0, dwarf_stmm0, LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM,
      LLDB_INVALID_REGNUM},
     nullptr,
     nullptr,
     nullptr,
     0},
    {"stmm1",
     nullptr,
     10,
     0,
     eEncodingVector,
     eFormatVectorOfUInt8,
     {dwarf_stmm1, dwarf_stmm1, LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM,
      LLDB_INVALID_REGNUM},
     nullptr,
     nullptr,
     nullptr,
     0},
    {"stmm2",
     nullptr,
     10,
     0,
     eEncodingVector,
     eFormatVectorOfUInt8,
     {dwarf_stmm2, dwarf_stmm2, LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM,
      LLDB_INVALID_REGNUM},
     nullptr,
     nullptr,
     nullptr,
     0},
    {"stmm3",
     nullptr,
     10,
     0,
     eEncodingVector,
     eFormatVectorOfUInt8,
     {dwarf_stmm3, dwarf_stmm3, LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM,
      LLDB_INVALID_REGNUM},
     nullptr,
     nullptr,
     nullptr,
     0},
    {"stmm4",
     nullptr,
     10,
     0,
     eEncodingVector,
     eFormatVectorOfUInt8,
     {dwarf_stmm4, dwarf_stmm4, LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM,
      LLDB_INVALID_REGNUM},
     nullptr,
     nullptr,
     nullptr,
     0},
    {"stmm5",
     nullptr,
     10,
     0,
     eEncodingVector,
     eFormatVectorOfUInt8,
     {dwarf_stmm5, dwarf_stmm5, LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM,
      LLDB_INVALID_REGNUM},
     nullptr,
     nullptr,
     nullptr,
     0},
    {"stmm6",
     nullptr,
     10,
     0,
     eEncodingVector,
     eFormatVectorOfUInt8,
     {dwarf_stmm6, dwarf_stmm6, LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM,
      LLDB_INVALID_REGNUM},
     nullptr,
     nullptr,
     nullptr,
     0},
    {"stmm7",
     nullptr,
     10,
     0,
     eEncodingVector,
     eFormatVectorOfUInt8,
     {dwarf_stmm7, dwarf_stmm7, LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM,
      LLDB_INVALID_REGNUM},
     nullptr,
     nullptr,
     nullptr,
     0},
    {"fctrl",
     nullptr,
     4,
     0,
     eEncodingUint,
     eFormatHex,
     {LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM,
      LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM},
     nullptr,
     nullptr,
     nullptr,
     0},
    {"fstat",
     nullptr,
     4,
     0,
     eEncodingUint,
     eFormatHex,
     {LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM,
      LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM},
     nullptr,
     nullptr,
     nullptr,
     0},
    {"ftag",
     nullptr,
     4,
     0,
     eEncodingUint,
     eFormatHex,
     {LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM,
      LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM},
     nullptr,
     nullptr,
     nullptr,
     0},
    {"fiseg",
     nullptr,
     4,
     0,
     eEncodingUint,
     eFormatHex,
     {LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM,
      LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM},
     nullptr,
     nullptr,
     nullptr,
     0},
    {"fioff",
     nullptr,
     4,
     0,
     eEncodingUint,
     eFormatHex,
     {LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM,
      LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM},
     nullptr,
     nullptr,
     nullptr,
     0},
    {"foseg",
     nullptr,
     4,
     0,
     eEncodingUint,
     eFormatHex,
     {LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM,
      LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM},
     nullptr,
     nullptr,
     nullptr,
     0},
    {"fooff",
     nullptr,
     4,
     0,
     eEncodingUint,
     eFormatHex,
     {LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM,
      LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM},
     nullptr,
     nullptr,
     nullptr,
     0},
    {"fop",
     nullptr,
     4,
     0,
     eEncodingUint,
     eFormatHex,
     {LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM,
      LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM},
     nullptr,
     nullptr,
     nullptr,
     0},
    {"xmm0",
     nullptr,
     16,
     0,
     eEncodingVector,
     eFormatVectorOfUInt8,
     {dwarf_xmm0, dwarf_xmm0, LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM,
      LLDB_INVALID_REGNUM},
     nullptr,
     nullptr,
     nullptr,
     0},
    {"xmm1",
     nullptr,
     16,
     0,
     eEncodingVector,
     eFormatVectorOfUInt8,
     {dwarf_xmm1, dwarf_xmm1, LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM,
      LLDB_INVALID_REGNUM},
     nullptr,
     nullptr,
     nullptr,
     0},
    {"xmm2",
     nullptr,
     16,
     0,
     eEncodingVector,
     eFormatVectorOfUInt8,
     {dwarf_xmm2, dwarf_xmm2, LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM,
      LLDB_INVALID_REGNUM},
     nullptr,
     nullptr,
     nullptr,
     0},
    {"xmm3",
     nullptr,
     16,
     0,
     eEncodingVector,
     eFormatVectorOfUInt8,
     {dwarf_xmm3, dwarf_xmm3, LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM,
      LLDB_INVALID_REGNUM},
     nullptr,
     nullptr,
     nullptr,
     0},
    {"xmm4",
     nullptr,
     16,
     0,
     eEncodingVector,
     eFormatVectorOfUInt8,
     {dwarf_xmm4, dwarf_xmm4, LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM,
      LLDB_INVALID_REGNUM},
     nullptr,
     nullptr,
     nullptr,
     0},
    {"xmm5",
     nullptr,
     16,
     0,
     eEncodingVector,
     eFormatVectorOfUInt8,
     {dwarf_xmm5, dwarf_xmm5, LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM,
      LLDB_INVALID_REGNUM},
     nullptr,
     nullptr,
     nullptr,
     0},
    {"xmm6",
     nullptr,
     16,
     0,
     eEncodingVector,
     eFormatVectorOfUInt8,
     {dwarf_xmm6, dwarf_xmm6, LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM,
      LLDB_INVALID_REGNUM},
     nullptr,
     nullptr,
     nullptr,
     0},
    {"xmm7",
     nullptr,
     16,
     0,
     eEncodingVector,
     eFormatVectorOfUInt8,
     {dwarf_xmm7, dwarf_xmm7, LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM,
      LLDB_INVALID_REGNUM},
     nullptr,
     nullptr,
     nullptr,
     0},
    {"xmm8",
     nullptr,
     16,
     0,
     eEncodingVector,
     eFormatVectorOfUInt8,
     {dwarf_xmm8, dwarf_xmm8, LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM,
      LLDB_INVALID_REGNUM},
     nullptr,
     nullptr,
     nullptr,
     0},
    {"xmm9",
     nullptr,
     16,
     0,
     eEncodingVector,
     eFormatVectorOfUInt8,
     {dwarf_xmm9, dwarf_xmm9, LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM,
      LLDB_INVALID_REGNUM},
     nullptr,
     nullptr,
     nullptr,
     0},
    {"xmm10",
     nullptr,
     16,
     0,
     eEncodingVector,
     eFormatVectorOfUInt8,
     {dwarf_xmm10, dwarf_xmm10, LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM,
      LLDB_INVALID_REGNUM},
     nullptr,
     nullptr,
     nullptr,
     0},
    {"xmm11",
     nullptr,
     16,
     0,
     eEncodingVector,
     eFormatVectorOfUInt8,
     {dwarf_xmm11, dwarf_xmm11, LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM,
      LLDB_INVALID_REGNUM},
     nullptr,
     nullptr,
     nullptr,
     0},
    {"xmm12",
     nullptr,
     16,
     0,
     eEncodingVector,
     eFormatVectorOfUInt8,
     {dwarf_xmm12, dwarf_xmm12, LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM,
      LLDB_INVALID_REGNUM},
     nullptr,
     nullptr,
     nullptr,
     0},
    {"xmm13",
     nullptr,
     16,
     0,
     eEncodingVector,
     eFormatVectorOfUInt8,
     {dwarf_xmm13, dwarf_xmm13, LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM,
      LLDB_INVALID_REGNUM},
     nullptr,
     nullptr,
     nullptr,
     0},
    {"xmm14",
     nullptr,
     16,
     0,
     eEncodingVector,
     eFormatVectorOfUInt8,
     {dwarf_xmm14, dwarf_xmm14, LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM,
      LLDB_INVALID_REGNUM},
     nullptr,
     nullptr,
     nullptr,
     0},
    {"xmm15",
     nullptr,
     16,
     0,
     eEncodingVector,
     eFormatVectorOfUInt8,
     {dwarf_xmm15, dwarf_xmm15, LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM,
      LLDB_INVALID_REGNUM},
     nullptr,
     nullptr,
     nullptr,
     0},
    {"mxcsr",
     nullptr,
     4,
     0,
     eEncodingUint,
     eFormatHex,
     {LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM,
      LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM},
     nullptr,
     nullptr,
     nullptr,
     0},
    {"ymm0",
     nullptr,
     32,
     0,
     eEncodingVector,
     eFormatVectorOfUInt8,
     {dwarf_ymm0, dwarf_ymm0, LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM,
      LLDB_INVALID_REGNUM},
     nullptr,
     nullptr,
     nullptr,
     0},
    {"ymm1",
     nullptr,
     32,
     0,
     eEncodingVector,
     eFormatVectorOfUInt8,
     {dwarf_ymm1, dwarf_ymm1, LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM,
      LLDB_INVALID_REGNUM},
     nullptr,
     nullptr,
     nullptr,
     0},
    {"ymm2",
     nullptr,
     32,
     0,
     eEncodingVector,
     eFormatVectorOfUInt8,
     {dwarf_ymm2, dwarf_ymm2, LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM,
      LLDB_INVALID_REGNUM},
     nullptr,
     nullptr,
     nullptr,
     0},
    {"ymm3",
     nullptr,
     32,
     0,
     eEncodingVector,
     eFormatVectorOfUInt8,
     {dwarf_ymm3, dwarf_ymm3, LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM,
      LLDB_INVALID_REGNUM},
     nullptr,
     nullptr,
     nullptr,
     0},
    {"ymm4",
     nullptr,
     32,
     0,
     eEncodingVector,
     eFormatVectorOfUInt8,
     {dwarf_ymm4, dwarf_ymm4, LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM,
      LLDB_INVALID_REGNUM},
     nullptr,
     nullptr,
     nullptr,
     0},
    {"ymm5",
     nullptr,
     32,
     0,
     eEncodingVector,
     eFormatVectorOfUInt8,
     {dwarf_ymm5, dwarf_ymm5, LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM,
      LLDB_INVALID_REGNUM},
     nullptr,
     nullptr,
     nullptr,
     0},
    {"ymm6",
     nullptr,
     32,
     0,
     eEncodingVector,
     eFormatVectorOfUInt8,
     {dwarf_ymm6, dwarf_ymm6, LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM,
      LLDB_INVALID_REGNUM},
     nullptr,
     nullptr,
     nullptr,
     0},
    {"ymm7",
     nullptr,
     32,
     0,
     eEncodingVector,
     eFormatVectorOfUInt8,
     {dwarf_ymm7, dwarf_ymm7, LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM,
      LLDB_INVALID_REGNUM},
     nullptr,
     nullptr,
     nullptr,
     0},
    {"ymm8",
     nullptr,
     32,
     0,
     eEncodingVector,
     eFormatVectorOfUInt8,
     {dwarf_ymm8, dwarf_ymm8, LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM,
      LLDB_INVALID_REGNUM},
     nullptr,
     nullptr,
     nullptr,
     0},
    {"ymm9",
     nullptr,
     32,
     0,
     eEncodingVector,
     eFormatVectorOfUInt8,
     {dwarf_ymm9, dwarf_ymm9, LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM,
      LLDB_INVALID_REGNUM},
     nullptr,
     nullptr,
     nullptr,
     0},
    {"ymm10",
     nullptr,
     32,
     0,
     eEncodingVector,
     eFormatVectorOfUInt8,
     {dwarf_ymm10, dwarf_ymm10, LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM,
      LLDB_INVALID_REGNUM},
     nullptr,
     nullptr,
     nullptr,
     0},
    {"ymm11",
     nullptr,
     32,
     0,
     eEncodingVector,
     eFormatVectorOfUInt8,
     {dwarf_ymm11, dwarf_ymm11, LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM,
      LLDB_INVALID_REGNUM},
     nullptr,
     nullptr,
     nullptr,
     0},
    {"ymm12",
     nullptr,
     32,
     0,
     eEncodingVector,
     eFormatVectorOfUInt8,
     {dwarf_ymm12, dwarf_ymm12, LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM,
      LLDB_INVALID_REGNUM},
     nullptr,
     nullptr,
     nullptr,
     0},
    {"ymm13",
     nullptr,
     32,
     0,
     eEncodingVector,
     eFormatVectorOfUInt8,
     {dwarf_ymm13, dwarf_ymm13, LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM,
      LLDB_INVALID_REGNUM},
     nullptr,
     nullptr,
     nullptr,
     0},
    {"ymm14",
     nullptr,
     32,
     0,
     eEncodingVector,
     eFormatVectorOfUInt8,
     {dwarf_ymm14, dwarf_ymm14, LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM,
      LLDB_INVALID_REGNUM},
     nullptr,
     nullptr,
     nullptr,
     0},
    {"ymm15",
     nullptr,
     32,
     0,
     eEncodingVector,
     eFormatVectorOfUInt8,
     {dwarf_ymm15, dwarf_ymm15, LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM,
      LLDB_INVALID_REGNUM},
     nullptr,
     nullptr,
     nullptr,
     0},
    {"bnd0",
     nullptr,
     16,
     0,
     eEncodingVector,
     eFormatVectorOfUInt64,
     {dwarf_bnd0, dwarf_bnd0, LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM,
      LLDB_INVALID_REGNUM},
     nullptr,
     nullptr,
     nullptr,
     0},
    {"bnd1",
     nullptr,
     16,
     0,
     eEncodingVector,
     eFormatVectorOfUInt64,
     {dwarf_bnd1, dwarf_bnd1, LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM,
      LLDB_INVALID_REGNUM},
     nullptr,
     nullptr,
     nullptr,
     0},
    {"bnd2",
     nullptr,
     16,
     0,
     eEncodingVector,
     eFormatVectorOfUInt64,
     {dwarf_bnd2, dwarf_bnd2, LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM,
      LLDB_INVALID_REGNUM},
     nullptr,
     nullptr,
     nullptr,
     0},
    {"bnd3",
     nullptr,
     16,
     0,
     eEncodingVector,
     eFormatVectorOfUInt64,
     {dwarf_bnd3, dwarf_bnd3, LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM,
      LLDB_INVALID_REGNUM},
     nullptr,
     nullptr,
     nullptr,
     0},
    {"bndcfgu",
     nullptr,
     8,
     0,
     eEncodingVector,
     eFormatVectorOfUInt8,
     {LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM,
      LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM},
     nullptr,
     nullptr,
     nullptr,
     0},
    {"bndstatus",
     nullptr,
     8,
     0,
     eEncodingVector,
     eFormatVectorOfUInt8,
     {LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM,
      LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM},
     nullptr,
     nullptr,
     nullptr,
     0}};

static const uint32_t k_num_register_infos =
    llvm::array_lengthof(g_register_infos);
static bool g_register_info_names_constified = false;

const lldb_private::RegisterInfo *
ABISysV_x86_64::GetRegisterInfoArray(uint32_t &count) {
  // Make the C-string names and alt_names for the register infos into const
  // C-string values by having the ConstString unique the names in the global
  // constant C-string pool.
  if (!g_register_info_names_constified) {
    g_register_info_names_constified = true;
    for (uint32_t i = 0; i < k_num_register_infos; ++i) {
      if (g_register_infos[i].name)
        g_register_infos[i].name =
            ConstString(g_register_infos[i].name).GetCString();
      if (g_register_infos[i].alt_name)
        g_register_infos[i].alt_name =
            ConstString(g_register_infos[i].alt_name).GetCString();
    }
  }
  count = k_num_register_infos;
  return g_register_infos;
}

bool ABISysV_x86_64::GetPointerReturnRegister(const char *&name) {
  name = "rax";
  return true;
}

size_t ABISysV_x86_64::GetRedZoneSize() const { return 128; }

//------------------------------------------------------------------
// Static Functions
//------------------------------------------------------------------

ABISP
ABISysV_x86_64::CreateInstance(lldb::ProcessSP process_sp, const ArchSpec &arch) {
  if (arch.GetTriple().getArch() == llvm::Triple::x86_64) {
    return ABISP(new ABISysV_x86_64(process_sp));
  }
  return ABISP();
}

bool ABISysV_x86_64::PrepareTrivialCall(Thread &thread, addr_t sp,
                                        addr_t func_addr, addr_t return_addr,
                                        llvm::ArrayRef<addr_t> args) const {
  Log *log(lldb_private::GetLogIfAllCategoriesSet(LIBLLDB_LOG_EXPRESSIONS));

  if (log) {
    StreamString s;
    s.Printf("ABISysV_x86_64::PrepareTrivialCall (tid = 0x%" PRIx64
             ", sp = 0x%" PRIx64 ", func_addr = 0x%" PRIx64
             ", return_addr = 0x%" PRIx64,
             thread.GetID(), (uint64_t)sp, (uint64_t)func_addr,
             (uint64_t)return_addr);

    for (size_t i = 0; i < args.size(); ++i)
      s.Printf(", arg%" PRIu64 " = 0x%" PRIx64, static_cast<uint64_t>(i + 1),
               args[i]);
    s.PutCString(")");
    log->PutString(s.GetString());
  }

  RegisterContext *reg_ctx = thread.GetRegisterContext().get();
  if (!reg_ctx)
    return false;

  const RegisterInfo *reg_info = nullptr;

  if (args.size() > 6) // TODO handle more than 6 arguments
    return false;

  for (size_t i = 0; i < args.size(); ++i) {
    reg_info = reg_ctx->GetRegisterInfo(eRegisterKindGeneric,
                                        LLDB_REGNUM_GENERIC_ARG1 + i);
    if (log)
      log->Printf("About to write arg%" PRIu64 " (0x%" PRIx64 ") into %s",
                  static_cast<uint64_t>(i + 1), args[i], reg_info->name);
    if (!reg_ctx->WriteRegisterFromUnsigned(reg_info, args[i]))
      return false;
  }

  // First, align the SP

  if (log)
    log->Printf("16-byte aligning SP: 0x%" PRIx64 " to 0x%" PRIx64,
                (uint64_t)sp, (uint64_t)(sp & ~0xfull));

  sp &= ~(0xfull); // 16-byte alignment

  sp -= 8;

  Status error;
  const RegisterInfo *pc_reg_info =
      reg_ctx->GetRegisterInfo(eRegisterKindGeneric, LLDB_REGNUM_GENERIC_PC);
  const RegisterInfo *sp_reg_info =
      reg_ctx->GetRegisterInfo(eRegisterKindGeneric, LLDB_REGNUM_GENERIC_SP);
  ProcessSP process_sp(thread.GetProcess());

  RegisterValue reg_value;
  if (log)
    log->Printf("Pushing the return address onto the stack: 0x%" PRIx64
                ": 0x%" PRIx64,
                (uint64_t)sp, (uint64_t)return_addr);

  // Save return address onto the stack
  if (!process_sp->WritePointerToMemory(sp, return_addr, error))
    return false;

  // %rsp is set to the actual stack value.

  if (log)
    log->Printf("Writing SP: 0x%" PRIx64, (uint64_t)sp);

  if (!reg_ctx->WriteRegisterFromUnsigned(sp_reg_info, sp))
    return false;

  // %rip is set to the address of the called function.

  if (log)
    log->Printf("Writing IP: 0x%" PRIx64, (uint64_t)func_addr);

  if (!reg_ctx->WriteRegisterFromUnsigned(pc_reg_info, func_addr))
    return false;

  return true;
}

static bool ReadIntegerArgument(Scalar &scalar, unsigned int bit_width,
                                bool is_signed, Thread &thread,
                                uint32_t *argument_register_ids,
                                unsigned int &current_argument_register,
                                addr_t &current_stack_argument) {
  if (bit_width > 64)
    return false; // Scalar can't hold large integer arguments

  if (current_argument_register < 6) {
    scalar = thread.GetRegisterContext()->ReadRegisterAsUnsigned(
        argument_register_ids[current_argument_register], 0);
    current_argument_register++;
    if (is_signed)
      scalar.SignExtend(bit_width);
  } else {
    uint32_t byte_size = (bit_width + (8 - 1)) / 8;
    Status error;
    if (thread.GetProcess()->ReadScalarIntegerFromMemory(
            current_stack_argument, byte_size, is_signed, scalar, error)) {
      current_stack_argument += byte_size;
      return true;
    }
    return false;
  }
  return true;
}

bool ABISysV_x86_64::GetArgumentValues(Thread &thread,
                                       ValueList &values) const {
  unsigned int num_values = values.GetSize();
  unsigned int value_index;

  // Extract the register context so we can read arguments from registers

  RegisterContext *reg_ctx = thread.GetRegisterContext().get();

  if (!reg_ctx)
    return false;

  // Get the pointer to the first stack argument so we have a place to start
  // when reading data

  addr_t sp = reg_ctx->GetSP(0);

  if (!sp)
    return false;

  addr_t current_stack_argument = sp + 8; // jump over return address

  uint32_t argument_register_ids[6];

  argument_register_ids[0] =
      reg_ctx->GetRegisterInfo(eRegisterKindGeneric, LLDB_REGNUM_GENERIC_ARG1)
          ->kinds[eRegisterKindLLDB];
  argument_register_ids[1] =
      reg_ctx->GetRegisterInfo(eRegisterKindGeneric, LLDB_REGNUM_GENERIC_ARG2)
          ->kinds[eRegisterKindLLDB];
  argument_register_ids[2] =
      reg_ctx->GetRegisterInfo(eRegisterKindGeneric, LLDB_REGNUM_GENERIC_ARG3)
          ->kinds[eRegisterKindLLDB];
  argument_register_ids[3] =
      reg_ctx->GetRegisterInfo(eRegisterKindGeneric, LLDB_REGNUM_GENERIC_ARG4)
          ->kinds[eRegisterKindLLDB];
  argument_register_ids[4] =
      reg_ctx->GetRegisterInfo(eRegisterKindGeneric, LLDB_REGNUM_GENERIC_ARG5)
          ->kinds[eRegisterKindLLDB];
  argument_register_ids[5] =
      reg_ctx->GetRegisterInfo(eRegisterKindGeneric, LLDB_REGNUM_GENERIC_ARG6)
          ->kinds[eRegisterKindLLDB];

  unsigned int current_argument_register = 0;

  for (value_index = 0; value_index < num_values; ++value_index) {
    Value *value = values.GetValueAtIndex(value_index);

    if (!value)
      return false;

    // We currently only support extracting values with Clang QualTypes. Do we
    // care about others?
    CompilerType compiler_type = value->GetCompilerType();
    llvm::Optional<uint64_t> bit_size = compiler_type.GetBitSize(&thread);
    if (!bit_size)
      return false;
    bool is_signed;

    if (compiler_type.IsIntegerOrEnumerationType(is_signed)) {
      ReadIntegerArgument(value->GetScalar(), *bit_size, is_signed, thread,
                          argument_register_ids, current_argument_register,
                          current_stack_argument);
    } else if (compiler_type.IsPointerType()) {
      ReadIntegerArgument(value->GetScalar(), *bit_size, false, thread,
                          argument_register_ids, current_argument_register,
                          current_stack_argument);
    }
  }

  return true;
}

Status ABISysV_x86_64::SetReturnValueObject(lldb::StackFrameSP &frame_sp,
                                            lldb::ValueObjectSP &new_value_sp) {
  Status error;
  if (!new_value_sp) {
    error.SetErrorString("Empty value object for return value.");
    return error;
  }

  CompilerType compiler_type = new_value_sp->GetCompilerType();
  if (!compiler_type) {
    error.SetErrorString("Null clang type for return value.");
    return error;
  }

  Thread *thread = frame_sp->GetThread().get();

  bool is_signed;
  uint32_t count;
  bool is_complex;

  RegisterContext *reg_ctx = thread->GetRegisterContext().get();

  bool set_it_simple = false;
  if (compiler_type.IsIntegerOrEnumerationType(is_signed) ||
      compiler_type.IsPointerType()) {
    const RegisterInfo *reg_info = reg_ctx->GetRegisterInfoByName("rax", 0);

    DataExtractor data;
    Status data_error;
    size_t num_bytes = new_value_sp->GetData(data, data_error);
    if (data_error.Fail()) {
      error.SetErrorStringWithFormat(
          "Couldn't convert return value to raw data: %s",
          data_error.AsCString());
      return error;
    }
    lldb::offset_t offset = 0;
    if (num_bytes <= 8) {
      uint64_t raw_value = data.GetMaxU64(&offset, num_bytes);

      if (reg_ctx->WriteRegisterFromUnsigned(reg_info, raw_value))
        set_it_simple = true;
    } else {
      error.SetErrorString("We don't support returning longer than 64 bit "
                           "integer values at present.");
    }
  } else if (compiler_type.IsFloatingPointType(count, is_complex)) {
    if (is_complex)
      error.SetErrorString(
          "We don't support returning complex values at present");
    else {
      llvm::Optional<uint64_t> bit_width =
          compiler_type.GetBitSize(frame_sp.get());
      if (!bit_width) {
        error.SetErrorString("can't get type size");
        return error;
      }
      if (*bit_width <= 64) {
        const RegisterInfo *xmm0_info =
            reg_ctx->GetRegisterInfoByName("xmm0", 0);
        RegisterValue xmm0_value;
        DataExtractor data;
        Status data_error;
        size_t num_bytes = new_value_sp->GetData(data, data_error);
        if (data_error.Fail()) {
          error.SetErrorStringWithFormat(
              "Couldn't convert return value to raw data: %s",
              data_error.AsCString());
          return error;
        }

        unsigned char buffer[16];
        ByteOrder byte_order = data.GetByteOrder();

        data.CopyByteOrderedData(0, num_bytes, buffer, 16, byte_order);
        xmm0_value.SetBytes(buffer, 16, byte_order);
        reg_ctx->WriteRegister(xmm0_info, xmm0_value);
        set_it_simple = true;
      } else {
        // FIXME - don't know how to do 80 bit long doubles yet.
        error.SetErrorString(
            "We don't support returning float values > 64 bits at present");
      }
    }
  }

  if (!set_it_simple) {
    // Okay we've got a structure or something that doesn't fit in a simple
    // register. We should figure out where it really goes, but we don't
    // support this yet.
    error.SetErrorString("We only support setting simple integer and float "
                         "return types at present.");
  }

  return error;
}

ValueObjectSP ABISysV_x86_64::GetReturnValueObjectSimple(
    Thread &thread, CompilerType &return_compiler_type) const {
  ValueObjectSP return_valobj_sp;
  Value value;

  if (!return_compiler_type)
    return return_valobj_sp;

  // value.SetContext (Value::eContextTypeClangType, return_value_type);
  value.SetCompilerType(return_compiler_type);

  RegisterContext *reg_ctx = thread.GetRegisterContext().get();
  if (!reg_ctx)
    return return_valobj_sp;

  const uint32_t type_flags = return_compiler_type.GetTypeInfo();
  if (type_flags & eTypeIsScalar) {
    value.SetValueType(Value::eValueTypeScalar);

    bool success = false;
    if (type_flags & eTypeIsInteger) {
      // Extract the register context so we can read arguments from registers

      llvm::Optional<uint64_t> byte_size =
          return_compiler_type.GetByteSize(nullptr);
      if (!byte_size)
        return return_valobj_sp;
      uint64_t raw_value = thread.GetRegisterContext()->ReadRegisterAsUnsigned(
          reg_ctx->GetRegisterInfoByName("rax", 0), 0);
      const bool is_signed = (type_flags & eTypeIsSigned) != 0;
      switch (*byte_size) {
      default:
        break;

      case sizeof(uint64_t):
        if (is_signed)
          value.GetScalar() = (int64_t)(raw_value);
        else
          value.GetScalar() = (uint64_t)(raw_value);
        success = true;
        break;

      case sizeof(uint32_t):
        if (is_signed)
          value.GetScalar() = (int32_t)(raw_value & UINT32_MAX);
        else
          value.GetScalar() = (uint32_t)(raw_value & UINT32_MAX);
        success = true;
        break;

      case sizeof(uint16_t):
        if (is_signed)
          value.GetScalar() = (int16_t)(raw_value & UINT16_MAX);
        else
          value.GetScalar() = (uint16_t)(raw_value & UINT16_MAX);
        success = true;
        break;

      case sizeof(uint8_t):
        if (is_signed)
          value.GetScalar() = (int8_t)(raw_value & UINT8_MAX);
        else
          value.GetScalar() = (uint8_t)(raw_value & UINT8_MAX);
        success = true;
        break;
      }
    } else if (type_flags & eTypeIsFloat) {
      if (type_flags & eTypeIsComplex) {
        // Don't handle complex yet.
      } else {
        llvm::Optional<uint64_t> byte_size =
            return_compiler_type.GetByteSize(nullptr);
        if (byte_size && *byte_size <= sizeof(long double)) {
          const RegisterInfo *xmm0_info =
              reg_ctx->GetRegisterInfoByName("xmm0", 0);
          RegisterValue xmm0_value;
          if (reg_ctx->ReadRegister(xmm0_info, xmm0_value)) {
            DataExtractor data;
            if (xmm0_value.GetData(data)) {
              lldb::offset_t offset = 0;
              if (*byte_size == sizeof(float)) {
                value.GetScalar() = (float)data.GetFloat(&offset);
                success = true;
              } else if (*byte_size == sizeof(double)) {
                value.GetScalar() = (double)data.GetDouble(&offset);
                success = true;
              } else if (*byte_size == sizeof(long double)) {
                // Don't handle long double since that can be encoded as 80 bit
                // floats...
              }
            }
          }
        }
      }
    }

    if (success)
      return_valobj_sp = ValueObjectConstResult::Create(
          thread.GetStackFrameAtIndex(0).get(), value, ConstString(""));
  } else if (type_flags & eTypeIsPointer) {
    unsigned rax_id =
        reg_ctx->GetRegisterInfoByName("rax", 0)->kinds[eRegisterKindLLDB];
    value.GetScalar() =
        (uint64_t)thread.GetRegisterContext()->ReadRegisterAsUnsigned(rax_id,
                                                                      0);
    value.SetValueType(Value::eValueTypeScalar);
    return_valobj_sp = ValueObjectConstResult::Create(
        thread.GetStackFrameAtIndex(0).get(), value, ConstString(""));
  } else if (type_flags & eTypeIsVector) {
    llvm::Optional<uint64_t> byte_size =
        return_compiler_type.GetByteSize(nullptr);
    if (byte_size && *byte_size > 0) {
      const RegisterInfo *altivec_reg =
          reg_ctx->GetRegisterInfoByName("xmm0", 0);
      if (altivec_reg == nullptr)
        altivec_reg = reg_ctx->GetRegisterInfoByName("mm0", 0);

      if (altivec_reg) {
        if (*byte_size <= altivec_reg->byte_size) {
          ProcessSP process_sp(thread.GetProcess());
          if (process_sp) {
            std::unique_ptr<DataBufferHeap> heap_data_ap(
                new DataBufferHeap(*byte_size, 0));
            const ByteOrder byte_order = process_sp->GetByteOrder();
            RegisterValue reg_value;
            if (reg_ctx->ReadRegister(altivec_reg, reg_value)) {
              Status error;
              if (reg_value.GetAsMemoryData(
                      altivec_reg, heap_data_ap->GetBytes(),
                      heap_data_ap->GetByteSize(), byte_order, error)) {
                DataExtractor data(DataBufferSP(heap_data_ap.release()),
                                   byte_order, process_sp->GetTarget()
                                                   .GetArchitecture()
                                                   .GetAddressByteSize());
                return_valobj_sp = ValueObjectConstResult::Create(
                    &thread, return_compiler_type, ConstString(""), data);
              }
            }
          }
        } else if (*byte_size <= altivec_reg->byte_size * 2) {
          const RegisterInfo *altivec_reg2 =
              reg_ctx->GetRegisterInfoByName("xmm1", 0);
          if (altivec_reg2) {
            ProcessSP process_sp(thread.GetProcess());
            if (process_sp) {
              std::unique_ptr<DataBufferHeap> heap_data_ap(
                  new DataBufferHeap(*byte_size, 0));
              const ByteOrder byte_order = process_sp->GetByteOrder();
              RegisterValue reg_value;
              RegisterValue reg_value2;
              if (reg_ctx->ReadRegister(altivec_reg, reg_value) &&
                  reg_ctx->ReadRegister(altivec_reg2, reg_value2)) {

                Status error;
                if (reg_value.GetAsMemoryData(
                        altivec_reg, heap_data_ap->GetBytes(),
                        altivec_reg->byte_size, byte_order, error) &&
                    reg_value2.GetAsMemoryData(
                        altivec_reg2,
                        heap_data_ap->GetBytes() + altivec_reg->byte_size,
                        heap_data_ap->GetByteSize() - altivec_reg->byte_size,
                        byte_order, error)) {
                  DataExtractor data(DataBufferSP(heap_data_ap.release()),
                                     byte_order, process_sp->GetTarget()
                                                     .GetArchitecture()
                                                     .GetAddressByteSize());
                  return_valobj_sp = ValueObjectConstResult::Create(
                      &thread, return_compiler_type, ConstString(""), data);
                }
              }
            }
          }
        }
      }
    }
  }

  return return_valobj_sp;
}

ValueObjectSP ABISysV_x86_64::GetReturnValueObjectImpl(
    Thread &thread, CompilerType &return_compiler_type) const {
  ValueObjectSP return_valobj_sp;

  if (!return_compiler_type)
    return return_valobj_sp;

  ExecutionContext exe_ctx(thread.shared_from_this());
  return_valobj_sp = GetReturnValueObjectSimple(thread, return_compiler_type);
  if (return_valobj_sp)
    return return_valobj_sp;

  RegisterContextSP reg_ctx_sp = thread.GetRegisterContext();
  if (!reg_ctx_sp)
    return return_valobj_sp;

  llvm::Optional<uint64_t> bit_width = return_compiler_type.GetBitSize(&thread);
  if (!bit_width)
    return return_valobj_sp;
  if (return_compiler_type.IsAggregateType()) {
    Target *target = exe_ctx.GetTargetPtr();
    bool is_memory = true;
    if (*bit_width <= 128) {
      ByteOrder target_byte_order = target->GetArchitecture().GetByteOrder();
      DataBufferSP data_sp(new DataBufferHeap(16, 0));
      DataExtractor return_ext(data_sp, target_byte_order,
                               target->GetArchitecture().GetAddressByteSize());

      const RegisterInfo *rax_info =
          reg_ctx_sp->GetRegisterInfoByName("rax", 0);
      const RegisterInfo *rdx_info =
          reg_ctx_sp->GetRegisterInfoByName("rdx", 0);
      const RegisterInfo *xmm0_info =
          reg_ctx_sp->GetRegisterInfoByName("xmm0", 0);
      const RegisterInfo *xmm1_info =
          reg_ctx_sp->GetRegisterInfoByName("xmm1", 0);

      RegisterValue rax_value, rdx_value, xmm0_value, xmm1_value;
      reg_ctx_sp->ReadRegister(rax_info, rax_value);
      reg_ctx_sp->ReadRegister(rdx_info, rdx_value);
      reg_ctx_sp->ReadRegister(xmm0_info, xmm0_value);
      reg_ctx_sp->ReadRegister(xmm1_info, xmm1_value);

      DataExtractor rax_data, rdx_data, xmm0_data, xmm1_data;

      rax_value.GetData(rax_data);
      rdx_value.GetData(rdx_data);
      xmm0_value.GetData(xmm0_data);
      xmm1_value.GetData(xmm1_data);

      uint32_t fp_bytes =
          0; // Tracks how much of the xmm registers we've consumed so far
      uint32_t integer_bytes =
          0; // Tracks how much of the rax/rds registers we've consumed so far

      const uint32_t num_children = return_compiler_type.GetNumFields();

      // Since we are in the small struct regime, assume we are not in memory.
      is_memory = false;

      for (uint32_t idx = 0; idx < num_children; idx++) {
        std::string name;
        uint64_t field_bit_offset = 0;
        bool is_signed;
        bool is_complex;
        uint32_t count;

        CompilerType field_compiler_type = return_compiler_type.GetFieldAtIndex(
            idx, name, &field_bit_offset, nullptr, nullptr);
        llvm::Optional<uint64_t> field_bit_width =
            field_compiler_type.GetBitSize(&thread);

        // if we don't know the size of the field (e.g. invalid type), just
        // bail out
        if (!field_bit_width || *field_bit_width == 0)
          break;

        // If there are any unaligned fields, this is stored in memory.
        if (field_bit_offset % *field_bit_width != 0) {
          is_memory = true;
          break;
        }

        uint32_t field_byte_width = *field_bit_width / 8;
        uint32_t field_byte_offset = field_bit_offset / 8;

        DataExtractor *copy_from_extractor = nullptr;
        uint32_t copy_from_offset = 0;

        if (field_compiler_type.IsIntegerOrEnumerationType(is_signed) ||
            field_compiler_type.IsPointerType()) {
          if (integer_bytes < 8) {
            if (integer_bytes + field_byte_width <= 8) {
              // This is in RAX, copy from register to our result structure:
              copy_from_extractor = &rax_data;
              copy_from_offset = integer_bytes;
              integer_bytes += field_byte_width;
            } else {
              // The next field wouldn't fit in the remaining space, so we
              // pushed it to rdx.
              copy_from_extractor = &rdx_data;
              copy_from_offset = 0;
              integer_bytes = 8 + field_byte_width;
            }
          } else if (integer_bytes + field_byte_width <= 16) {
            copy_from_extractor = &rdx_data;
            copy_from_offset = integer_bytes - 8;
            integer_bytes += field_byte_width;
          } else {
            // The last field didn't fit.  I can't see how that would happen
            // w/o the overall size being greater than 16 bytes.  For now,
            // return a nullptr return value object.
            return return_valobj_sp;
          }
        } else if (field_compiler_type.IsFloatingPointType(count, is_complex)) {
          // Structs with long doubles are always passed in memory.
          if (*field_bit_width == 128) {
            is_memory = true;
            break;
          } else if (*field_bit_width == 64) {
            // These have to be in a single xmm register.
            if (fp_bytes == 0)
              copy_from_extractor = &xmm0_data;
            else
              copy_from_extractor = &xmm1_data;

            copy_from_offset = 0;
            fp_bytes += field_byte_width;
          } else if (*field_bit_width == 32) {
            // This one is kind of complicated.  If we are in an "eightbyte"
            // with another float, we'll be stuffed into an xmm register with
            // it.  If we are in an "eightbyte" with one or more ints, then we
            // will be stuffed into the appropriate GPR with them.
            bool in_gpr;
            if (field_byte_offset % 8 == 0) {
              // We are at the beginning of one of the eightbytes, so check the
              // next element (if any)
              if (idx == num_children - 1)
                in_gpr = false;
              else {
                uint64_t next_field_bit_offset = 0;
                CompilerType next_field_compiler_type =
                    return_compiler_type.GetFieldAtIndex(idx + 1, name,
                                                         &next_field_bit_offset,
                                                         nullptr, nullptr);
                if (next_field_compiler_type.IsIntegerOrEnumerationType(
                        is_signed))
                  in_gpr = true;
                else {
                  copy_from_offset = 0;
                  in_gpr = false;
                }
              }
            } else if (field_byte_offset % 4 == 0) {
              // We are inside of an eightbyte, so see if the field before us
              // is floating point: This could happen if somebody put padding
              // in the structure.
              if (idx == 0)
                in_gpr = false;
              else {
                uint64_t prev_field_bit_offset = 0;
                CompilerType prev_field_compiler_type =
                    return_compiler_type.GetFieldAtIndex(idx - 1, name,
                                                         &prev_field_bit_offset,
                                                         nullptr, nullptr);
                if (prev_field_compiler_type.IsIntegerOrEnumerationType(
                        is_signed))
                  in_gpr = true;
                else {
                  copy_from_offset = 4;
                  in_gpr = false;
                }
              }
            } else {
              is_memory = true;
              continue;
            }

            // Okay, we've figured out whether we are in GPR or XMM, now figure
            // out which one.
            if (in_gpr) {
              if (integer_bytes < 8) {
                // This is in RAX, copy from register to our result structure:
                copy_from_extractor = &rax_data;
                copy_from_offset = integer_bytes;
                integer_bytes += field_byte_width;
              } else {
                copy_from_extractor = &rdx_data;
                copy_from_offset = integer_bytes - 8;
                integer_bytes += field_byte_width;
              }
            } else {
              if (fp_bytes < 8)
                copy_from_extractor = &xmm0_data;
              else
                copy_from_extractor = &xmm1_data;

              fp_bytes += field_byte_width;
            }
          }
        }

        // These two tests are just sanity checks.  If I somehow get the type
        // calculation wrong above it is better to just return nothing than to
        // assert or crash.
        if (!copy_from_extractor)
          return return_valobj_sp;
        if (copy_from_offset + field_byte_width >
            copy_from_extractor->GetByteSize())
          return return_valobj_sp;

        copy_from_extractor->CopyByteOrderedData(
            copy_from_offset, field_byte_width,
            data_sp->GetBytes() + field_byte_offset, field_byte_width,
            target_byte_order);
      }

      if (!is_memory) {
        // The result is in our data buffer.  Let's make a variable object out
        // of it:
        return_valobj_sp = ValueObjectConstResult::Create(
            &thread, return_compiler_type, ConstString(""), return_ext);
      }
    }

    // FIXME: This is just taking a guess, rax may very well no longer hold the
    // return storage location.
    // If we are going to do this right, when we make a new frame we should
    // check to see if it uses a memory return, and if we are at the first
    // instruction and if so stash away the return location.  Then we would
    // only return the memory return value if we know it is valid.

    if (is_memory) {
      unsigned rax_id =
          reg_ctx_sp->GetRegisterInfoByName("rax", 0)->kinds[eRegisterKindLLDB];
      lldb::addr_t storage_addr =
          (uint64_t)thread.GetRegisterContext()->ReadRegisterAsUnsigned(rax_id,
                                                                        0);
      return_valobj_sp = ValueObjectMemory::Create(
          &thread, "", Address(storage_addr, nullptr), return_compiler_type);
    }
  }

  return return_valobj_sp;
}

// This defines the CFA as rsp+8
// the saved pc is at CFA-8 (i.e. rsp+0)
// The saved rsp is CFA+0

bool ABISysV_x86_64::CreateFunctionEntryUnwindPlan(UnwindPlan &unwind_plan) {
  unwind_plan.Clear();
  unwind_plan.SetRegisterKind(eRegisterKindDWARF);

  uint32_t sp_reg_num = dwarf_rsp;
  uint32_t pc_reg_num = dwarf_rip;

  UnwindPlan::RowSP row(new UnwindPlan::Row);
  row->GetCFAValue().SetIsRegisterPlusOffset(sp_reg_num, 8);
  row->SetRegisterLocationToAtCFAPlusOffset(pc_reg_num, -8, false);
  row->SetRegisterLocationToIsCFAPlusOffset(sp_reg_num, 0, true);
  unwind_plan.AppendRow(row);
  unwind_plan.SetSourceName("x86_64 at-func-entry default");
  unwind_plan.SetSourcedFromCompiler(eLazyBoolNo);
  return true;
}

// This defines the CFA as rbp+16
// The saved pc is at CFA-8 (i.e. rbp+8)
// The saved rbp is at CFA-16 (i.e. rbp+0)
// The saved rsp is CFA+0

bool ABISysV_x86_64::CreateDefaultUnwindPlan(UnwindPlan &unwind_plan) {
  unwind_plan.Clear();
  unwind_plan.SetRegisterKind(eRegisterKindDWARF);

  uint32_t fp_reg_num = dwarf_rbp;
  uint32_t sp_reg_num = dwarf_rsp;
  uint32_t pc_reg_num = dwarf_rip;

  UnwindPlan::RowSP row(new UnwindPlan::Row);

  const int32_t ptr_size = 8;
  row->GetCFAValue().SetIsRegisterPlusOffset(dwarf_rbp, 2 * ptr_size);
  row->SetOffset(0);

  row->SetRegisterLocationToAtCFAPlusOffset(fp_reg_num, ptr_size * -2, true);
  row->SetRegisterLocationToAtCFAPlusOffset(pc_reg_num, ptr_size * -1, true);
  row->SetRegisterLocationToIsCFAPlusOffset(sp_reg_num, 0, true);

  unwind_plan.AppendRow(row);
  unwind_plan.SetSourceName("x86_64 default unwind plan");
  unwind_plan.SetSourcedFromCompiler(eLazyBoolNo);
  unwind_plan.SetUnwindPlanValidAtAllInstructions(eLazyBoolNo);
  return true;
}

bool ABISysV_x86_64::RegisterIsVolatile(const RegisterInfo *reg_info) {
  return !RegisterIsCalleeSaved(reg_info);
}

// See "Register Usage" in the
// "System V Application Binary Interface"
// "AMD64 Architecture Processor Supplement" (or "x86-64(tm) Architecture
// Processor Supplement" in earlier revisions) (this doc is also commonly
// referred to as the x86-64/AMD64 psABI) Edited by Michael Matz, Jan Hubicka,
// Andreas Jaeger, and Mark Mitchell current version is 0.99.6 released
// 2012-07-02 at http://refspecs.linuxfoundation.org/elf/x86-64-abi-0.99.pdf
// It's being revised & updated at https://github.com/hjl-tools/x86-psABI/

bool ABISysV_x86_64::RegisterIsCalleeSaved(const RegisterInfo *reg_info) {
  if (!reg_info)
    return false;
  assert(reg_info->name != nullptr && "unnamed register?");
  std::string Name = std::string(reg_info->name);
  bool IsCalleeSaved =
      llvm::StringSwitch<bool>(Name)
          .Cases("r12", "r13", "r14", "r15", "rbp", "ebp", "rbx", "ebx", true)
          .Cases("rip", "eip", "rsp", "esp", "sp", "fp", "pc", true)
          .Default(false);
  return IsCalleeSaved;
}

void ABISysV_x86_64::Initialize() {
  PluginManager::RegisterPlugin(
      GetPluginNameStatic(), "System V ABI for x86_64 targets", CreateInstance);
}

void ABISysV_x86_64::Terminate() {
  PluginManager::UnregisterPlugin(CreateInstance);
}

lldb_private::ConstString ABISysV_x86_64::GetPluginNameStatic() {
  static ConstString g_name("sysv-x86_64");
  return g_name;
}

//------------------------------------------------------------------
// PluginInterface protocol
//------------------------------------------------------------------

lldb_private::ConstString ABISysV_x86_64::GetPluginName() {
  return GetPluginNameStatic();
}

uint32_t ABISysV_x86_64::GetPluginVersion() { return 1; }
