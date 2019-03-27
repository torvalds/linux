//===-- ABIMacOSX_arm.cpp ---------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "ABIMacOSX_arm.h"

#include <vector>

#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/Triple.h"

#include "lldb/Core/Module.h"
#include "lldb/Core/PluginManager.h"
#include "lldb/Core/Value.h"
#include "lldb/Core/ValueObjectConstResult.h"
#include "lldb/Symbol/UnwindPlan.h"
#include "lldb/Target/Process.h"
#include "lldb/Target/RegisterContext.h"
#include "lldb/Target/Target.h"
#include "lldb/Target/Thread.h"
#include "lldb/Utility/ConstString.h"
#include "lldb/Utility/RegisterValue.h"
#include "lldb/Utility/Scalar.h"
#include "lldb/Utility/Status.h"

#include "Plugins/Process/Utility/ARMDefines.h"
#include "Utility/ARM_DWARF_Registers.h"
#include "Utility/ARM_ehframe_Registers.h"

using namespace lldb;
using namespace lldb_private;

static RegisterInfo g_register_infos[] = {
    //  NAME       ALT       SZ OFF ENCODING         FORMAT          EH_FRAME
    //  DWARF               GENERIC                     PROCESS PLUGIN
    //  LLDB NATIVE
    //  ========== =======   == === =============    ============
    //  ======================= =================== ===========================
    //  ======================= ======================
    {"r0",
     "arg1",
     4,
     0,
     eEncodingUint,
     eFormatHex,
     {ehframe_r0, dwarf_r0, LLDB_REGNUM_GENERIC_ARG1, LLDB_INVALID_REGNUM,
      LLDB_INVALID_REGNUM},
     nullptr,
     nullptr,
     nullptr,
     0},
    {"r1",
     "arg2",
     4,
     0,
     eEncodingUint,
     eFormatHex,
     {ehframe_r1, dwarf_r1, LLDB_REGNUM_GENERIC_ARG2, LLDB_INVALID_REGNUM,
      LLDB_INVALID_REGNUM},
     nullptr,
     nullptr,
     nullptr,
     0},
    {"r2",
     "arg3",
     4,
     0,
     eEncodingUint,
     eFormatHex,
     {ehframe_r2, dwarf_r2, LLDB_REGNUM_GENERIC_ARG3, LLDB_INVALID_REGNUM,
      LLDB_INVALID_REGNUM},
     nullptr,
     nullptr,
     nullptr,
     0},
    {"r3",
     "arg4",
     4,
     0,
     eEncodingUint,
     eFormatHex,
     {ehframe_r3, dwarf_r3, LLDB_REGNUM_GENERIC_ARG4, LLDB_INVALID_REGNUM,
      LLDB_INVALID_REGNUM},
     nullptr,
     nullptr,
     nullptr,
     0},
    {"r4",
     nullptr,
     4,
     0,
     eEncodingUint,
     eFormatHex,
     {ehframe_r4, dwarf_r4, LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM,
      LLDB_INVALID_REGNUM},
     nullptr,
     nullptr,
     nullptr,
     0},
    {"r5",
     nullptr,
     4,
     0,
     eEncodingUint,
     eFormatHex,
     {ehframe_r5, dwarf_r5, LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM,
      LLDB_INVALID_REGNUM},
     nullptr,
     nullptr,
     nullptr,
     0},
    {"r6",
     nullptr,
     4,
     0,
     eEncodingUint,
     eFormatHex,
     {ehframe_r6, dwarf_r6, LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM,
      LLDB_INVALID_REGNUM},
     nullptr,
     nullptr,
     nullptr,
     0},
    {"r7",
     nullptr,
     4,
     0,
     eEncodingUint,
     eFormatHex,
     {ehframe_r7, dwarf_r7, LLDB_REGNUM_GENERIC_FP, LLDB_INVALID_REGNUM,
      LLDB_INVALID_REGNUM},
     nullptr,
     nullptr,
     nullptr,
     0},
    {"r8",
     nullptr,
     4,
     0,
     eEncodingUint,
     eFormatHex,
     {ehframe_r8, dwarf_r8, LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM,
      LLDB_INVALID_REGNUM},
     nullptr,
     nullptr,
     nullptr,
     0},
    {"r9",
     nullptr,
     4,
     0,
     eEncodingUint,
     eFormatHex,
     {ehframe_r9, dwarf_r9, LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM,
      LLDB_INVALID_REGNUM},
     nullptr,
     nullptr,
     nullptr,
     0},
    {"r10",
     nullptr,
     4,
     0,
     eEncodingUint,
     eFormatHex,
     {ehframe_r10, dwarf_r10, LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM,
      LLDB_INVALID_REGNUM},
     nullptr,
     nullptr,
     nullptr,
     0},
    {"r11",
     nullptr,
     4,
     0,
     eEncodingUint,
     eFormatHex,
     {ehframe_r11, dwarf_r11, LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM,
      LLDB_INVALID_REGNUM},
     nullptr,
     nullptr,
     nullptr,
     0},
    {"r12",
     nullptr,
     4,
     0,
     eEncodingUint,
     eFormatHex,
     {ehframe_r12, dwarf_r12, LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM,
      LLDB_INVALID_REGNUM},
     nullptr,
     nullptr,
     nullptr,
     0},
    {"sp",
     "r13",
     4,
     0,
     eEncodingUint,
     eFormatHex,
     {ehframe_sp, dwarf_sp, LLDB_REGNUM_GENERIC_SP, LLDB_INVALID_REGNUM,
      LLDB_INVALID_REGNUM},
     nullptr,
     nullptr,
     nullptr,
     0},
    {"lr",
     "r14",
     4,
     0,
     eEncodingUint,
     eFormatHex,
     {ehframe_lr, dwarf_lr, LLDB_REGNUM_GENERIC_RA, LLDB_INVALID_REGNUM,
      LLDB_INVALID_REGNUM},
     nullptr,
     nullptr,
     nullptr,
     0},
    {"pc",
     "r15",
     4,
     0,
     eEncodingUint,
     eFormatHex,
     {ehframe_pc, dwarf_pc, LLDB_REGNUM_GENERIC_PC, LLDB_INVALID_REGNUM,
      LLDB_INVALID_REGNUM},
     nullptr,
     nullptr,
     nullptr,
     0},
    {"cpsr",
     "psr",
     4,
     0,
     eEncodingUint,
     eFormatHex,
     {ehframe_cpsr, dwarf_cpsr, LLDB_REGNUM_GENERIC_FLAGS, LLDB_INVALID_REGNUM,
      LLDB_INVALID_REGNUM},
     nullptr,
     nullptr,
     nullptr,
     0},
    {"s0",
     nullptr,
     4,
     0,
     eEncodingIEEE754,
     eFormatFloat,
     {LLDB_INVALID_REGNUM, dwarf_s0, LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM,
      LLDB_INVALID_REGNUM},
     nullptr,
     nullptr,
     nullptr,
     0},
    {"s1",
     nullptr,
     4,
     0,
     eEncodingIEEE754,
     eFormatFloat,
     {LLDB_INVALID_REGNUM, dwarf_s1, LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM,
      LLDB_INVALID_REGNUM},
     nullptr,
     nullptr,
     nullptr,
     0},
    {"s2",
     nullptr,
     4,
     0,
     eEncodingIEEE754,
     eFormatFloat,
     {LLDB_INVALID_REGNUM, dwarf_s2, LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM,
      LLDB_INVALID_REGNUM},
     nullptr,
     nullptr,
     nullptr,
     0},
    {"s3",
     nullptr,
     4,
     0,
     eEncodingIEEE754,
     eFormatFloat,
     {LLDB_INVALID_REGNUM, dwarf_s3, LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM,
      LLDB_INVALID_REGNUM},
     nullptr,
     nullptr,
     nullptr,
     0},
    {"s4",
     nullptr,
     4,
     0,
     eEncodingIEEE754,
     eFormatFloat,
     {LLDB_INVALID_REGNUM, dwarf_s4, LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM,
      LLDB_INVALID_REGNUM},
     nullptr,
     nullptr,
     nullptr,
     0},
    {"s5",
     nullptr,
     4,
     0,
     eEncodingIEEE754,
     eFormatFloat,
     {LLDB_INVALID_REGNUM, dwarf_s5, LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM,
      LLDB_INVALID_REGNUM},
     nullptr,
     nullptr,
     nullptr,
     0},
    {"s6",
     nullptr,
     4,
     0,
     eEncodingIEEE754,
     eFormatFloat,
     {LLDB_INVALID_REGNUM, dwarf_s6, LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM,
      LLDB_INVALID_REGNUM},
     nullptr,
     nullptr,
     nullptr,
     0},
    {"s7",
     nullptr,
     4,
     0,
     eEncodingIEEE754,
     eFormatFloat,
     {LLDB_INVALID_REGNUM, dwarf_s7, LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM,
      LLDB_INVALID_REGNUM},
     nullptr,
     nullptr,
     nullptr,
     0},
    {"s8",
     nullptr,
     4,
     0,
     eEncodingIEEE754,
     eFormatFloat,
     {LLDB_INVALID_REGNUM, dwarf_s8, LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM,
      LLDB_INVALID_REGNUM},
     nullptr,
     nullptr,
     nullptr,
     0},
    {"s9",
     nullptr,
     4,
     0,
     eEncodingIEEE754,
     eFormatFloat,
     {LLDB_INVALID_REGNUM, dwarf_s9, LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM,
      LLDB_INVALID_REGNUM},
     nullptr,
     nullptr,
     nullptr,
     0},
    {"s10",
     nullptr,
     4,
     0,
     eEncodingIEEE754,
     eFormatFloat,
     {LLDB_INVALID_REGNUM, dwarf_s10, LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM,
      LLDB_INVALID_REGNUM},
     nullptr,
     nullptr,
     nullptr,
     0},
    {"s11",
     nullptr,
     4,
     0,
     eEncodingIEEE754,
     eFormatFloat,
     {LLDB_INVALID_REGNUM, dwarf_s11, LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM,
      LLDB_INVALID_REGNUM},
     nullptr,
     nullptr,
     nullptr,
     0},
    {"s12",
     nullptr,
     4,
     0,
     eEncodingIEEE754,
     eFormatFloat,
     {LLDB_INVALID_REGNUM, dwarf_s12, LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM,
      LLDB_INVALID_REGNUM},
     nullptr,
     nullptr,
     nullptr,
     0},
    {"s13",
     nullptr,
     4,
     0,
     eEncodingIEEE754,
     eFormatFloat,
     {LLDB_INVALID_REGNUM, dwarf_s13, LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM,
      LLDB_INVALID_REGNUM},
     nullptr,
     nullptr,
     nullptr,
     0},
    {"s14",
     nullptr,
     4,
     0,
     eEncodingIEEE754,
     eFormatFloat,
     {LLDB_INVALID_REGNUM, dwarf_s14, LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM,
      LLDB_INVALID_REGNUM},
     nullptr,
     nullptr,
     nullptr,
     0},
    {"s15",
     nullptr,
     4,
     0,
     eEncodingIEEE754,
     eFormatFloat,
     {LLDB_INVALID_REGNUM, dwarf_s15, LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM,
      LLDB_INVALID_REGNUM},
     nullptr,
     nullptr,
     nullptr,
     0},
    {"s16",
     nullptr,
     4,
     0,
     eEncodingIEEE754,
     eFormatFloat,
     {LLDB_INVALID_REGNUM, dwarf_s16, LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM,
      LLDB_INVALID_REGNUM},
     nullptr,
     nullptr,
     nullptr,
     0},
    {"s17",
     nullptr,
     4,
     0,
     eEncodingIEEE754,
     eFormatFloat,
     {LLDB_INVALID_REGNUM, dwarf_s17, LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM,
      LLDB_INVALID_REGNUM},
     nullptr,
     nullptr,
     nullptr,
     0},
    {"s18",
     nullptr,
     4,
     0,
     eEncodingIEEE754,
     eFormatFloat,
     {LLDB_INVALID_REGNUM, dwarf_s18, LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM,
      LLDB_INVALID_REGNUM},
     nullptr,
     nullptr,
     nullptr,
     0},
    {"s19",
     nullptr,
     4,
     0,
     eEncodingIEEE754,
     eFormatFloat,
     {LLDB_INVALID_REGNUM, dwarf_s19, LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM,
      LLDB_INVALID_REGNUM},
     nullptr,
     nullptr,
     nullptr,
     0},
    {"s20",
     nullptr,
     4,
     0,
     eEncodingIEEE754,
     eFormatFloat,
     {LLDB_INVALID_REGNUM, dwarf_s20, LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM,
      LLDB_INVALID_REGNUM},
     nullptr,
     nullptr,
     nullptr,
     0},
    {"s21",
     nullptr,
     4,
     0,
     eEncodingIEEE754,
     eFormatFloat,
     {LLDB_INVALID_REGNUM, dwarf_s21, LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM,
      LLDB_INVALID_REGNUM},
     nullptr,
     nullptr,
     nullptr,
     0},
    {"s22",
     nullptr,
     4,
     0,
     eEncodingIEEE754,
     eFormatFloat,
     {LLDB_INVALID_REGNUM, dwarf_s22, LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM,
      LLDB_INVALID_REGNUM},
     nullptr,
     nullptr,
     nullptr,
     0},
    {"s23",
     nullptr,
     4,
     0,
     eEncodingIEEE754,
     eFormatFloat,
     {LLDB_INVALID_REGNUM, dwarf_s23, LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM,
      LLDB_INVALID_REGNUM},
     nullptr,
     nullptr,
     nullptr,
     0},
    {"s24",
     nullptr,
     4,
     0,
     eEncodingIEEE754,
     eFormatFloat,
     {LLDB_INVALID_REGNUM, dwarf_s24, LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM,
      LLDB_INVALID_REGNUM},
     nullptr,
     nullptr,
     nullptr,
     0},
    {"s25",
     nullptr,
     4,
     0,
     eEncodingIEEE754,
     eFormatFloat,
     {LLDB_INVALID_REGNUM, dwarf_s25, LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM,
      LLDB_INVALID_REGNUM},
     nullptr,
     nullptr,
     nullptr,
     0},
    {"s26",
     nullptr,
     4,
     0,
     eEncodingIEEE754,
     eFormatFloat,
     {LLDB_INVALID_REGNUM, dwarf_s26, LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM,
      LLDB_INVALID_REGNUM},
     nullptr,
     nullptr,
     nullptr,
     0},
    {"s27",
     nullptr,
     4,
     0,
     eEncodingIEEE754,
     eFormatFloat,
     {LLDB_INVALID_REGNUM, dwarf_s27, LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM,
      LLDB_INVALID_REGNUM},
     nullptr,
     nullptr,
     nullptr,
     0},
    {"s28",
     nullptr,
     4,
     0,
     eEncodingIEEE754,
     eFormatFloat,
     {LLDB_INVALID_REGNUM, dwarf_s28, LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM,
      LLDB_INVALID_REGNUM},
     nullptr,
     nullptr,
     nullptr,
     0},
    {"s29",
     nullptr,
     4,
     0,
     eEncodingIEEE754,
     eFormatFloat,
     {LLDB_INVALID_REGNUM, dwarf_s29, LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM,
      LLDB_INVALID_REGNUM},
     nullptr,
     nullptr,
     nullptr,
     0},
    {"s30",
     nullptr,
     4,
     0,
     eEncodingIEEE754,
     eFormatFloat,
     {LLDB_INVALID_REGNUM, dwarf_s30, LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM,
      LLDB_INVALID_REGNUM},
     nullptr,
     nullptr,
     nullptr,
     0},
    {"s31",
     nullptr,
     4,
     0,
     eEncodingIEEE754,
     eFormatFloat,
     {LLDB_INVALID_REGNUM, dwarf_s31, LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM,
      LLDB_INVALID_REGNUM},
     nullptr,
     nullptr,
     nullptr,
     0},
    {"fpscr",
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
    {"d0",
     nullptr,
     8,
     0,
     eEncodingIEEE754,
     eFormatFloat,
     {LLDB_INVALID_REGNUM, dwarf_d0, LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM,
      LLDB_INVALID_REGNUM},
     nullptr,
     nullptr,
     nullptr,
     0},
    {"d1",
     nullptr,
     8,
     0,
     eEncodingIEEE754,
     eFormatFloat,
     {LLDB_INVALID_REGNUM, dwarf_d1, LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM,
      LLDB_INVALID_REGNUM},
     nullptr,
     nullptr,
     nullptr,
     0},
    {"d2",
     nullptr,
     8,
     0,
     eEncodingIEEE754,
     eFormatFloat,
     {LLDB_INVALID_REGNUM, dwarf_d2, LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM,
      LLDB_INVALID_REGNUM},
     nullptr,
     nullptr,
     nullptr,
     0},
    {"d3",
     nullptr,
     8,
     0,
     eEncodingIEEE754,
     eFormatFloat,
     {LLDB_INVALID_REGNUM, dwarf_d3, LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM,
      LLDB_INVALID_REGNUM},
     nullptr,
     nullptr,
     nullptr,
     0},
    {"d4",
     nullptr,
     8,
     0,
     eEncodingIEEE754,
     eFormatFloat,
     {LLDB_INVALID_REGNUM, dwarf_d4, LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM,
      LLDB_INVALID_REGNUM},
     nullptr,
     nullptr,
     nullptr,
     0},
    {"d5",
     nullptr,
     8,
     0,
     eEncodingIEEE754,
     eFormatFloat,
     {LLDB_INVALID_REGNUM, dwarf_d5, LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM,
      LLDB_INVALID_REGNUM},
     nullptr,
     nullptr,
     nullptr,
     0},
    {"d6",
     nullptr,
     8,
     0,
     eEncodingIEEE754,
     eFormatFloat,
     {LLDB_INVALID_REGNUM, dwarf_d6, LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM,
      LLDB_INVALID_REGNUM},
     nullptr,
     nullptr,
     nullptr,
     0},
    {"d7",
     nullptr,
     8,
     0,
     eEncodingIEEE754,
     eFormatFloat,
     {LLDB_INVALID_REGNUM, dwarf_d7, LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM,
      LLDB_INVALID_REGNUM},
     nullptr,
     nullptr,
     nullptr,
     0},
    {"d8",
     nullptr,
     8,
     0,
     eEncodingIEEE754,
     eFormatFloat,
     {LLDB_INVALID_REGNUM, dwarf_d8, LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM,
      LLDB_INVALID_REGNUM},
     nullptr,
     nullptr,
     nullptr,
     0},
    {"d9",
     nullptr,
     8,
     0,
     eEncodingIEEE754,
     eFormatFloat,
     {LLDB_INVALID_REGNUM, dwarf_d9, LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM,
      LLDB_INVALID_REGNUM},
     nullptr,
     nullptr,
     nullptr,
     0},
    {"d10",
     nullptr,
     8,
     0,
     eEncodingIEEE754,
     eFormatFloat,
     {LLDB_INVALID_REGNUM, dwarf_d10, LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM,
      LLDB_INVALID_REGNUM},
     nullptr,
     nullptr,
     nullptr,
     0},
    {"d11",
     nullptr,
     8,
     0,
     eEncodingIEEE754,
     eFormatFloat,
     {LLDB_INVALID_REGNUM, dwarf_d11, LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM,
      LLDB_INVALID_REGNUM},
     nullptr,
     nullptr,
     nullptr,
     0},
    {"d12",
     nullptr,
     8,
     0,
     eEncodingIEEE754,
     eFormatFloat,
     {LLDB_INVALID_REGNUM, dwarf_d12, LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM,
      LLDB_INVALID_REGNUM},
     nullptr,
     nullptr,
     nullptr,
     0},
    {"d13",
     nullptr,
     8,
     0,
     eEncodingIEEE754,
     eFormatFloat,
     {LLDB_INVALID_REGNUM, dwarf_d13, LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM,
      LLDB_INVALID_REGNUM},
     nullptr,
     nullptr,
     nullptr,
     0},
    {"d14",
     nullptr,
     8,
     0,
     eEncodingIEEE754,
     eFormatFloat,
     {LLDB_INVALID_REGNUM, dwarf_d14, LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM,
      LLDB_INVALID_REGNUM},
     nullptr,
     nullptr,
     nullptr,
     0},
    {"d15",
     nullptr,
     8,
     0,
     eEncodingIEEE754,
     eFormatFloat,
     {LLDB_INVALID_REGNUM, dwarf_d15, LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM,
      LLDB_INVALID_REGNUM},
     nullptr,
     nullptr,
     nullptr,
     0},
    {"d16",
     nullptr,
     8,
     0,
     eEncodingIEEE754,
     eFormatFloat,
     {LLDB_INVALID_REGNUM, dwarf_d16, LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM,
      LLDB_INVALID_REGNUM},
     nullptr,
     nullptr,
     nullptr,
     0},
    {"d17",
     nullptr,
     8,
     0,
     eEncodingIEEE754,
     eFormatFloat,
     {LLDB_INVALID_REGNUM, dwarf_d17, LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM,
      LLDB_INVALID_REGNUM},
     nullptr,
     nullptr,
     nullptr,
     0},
    {"d18",
     nullptr,
     8,
     0,
     eEncodingIEEE754,
     eFormatFloat,
     {LLDB_INVALID_REGNUM, dwarf_d18, LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM,
      LLDB_INVALID_REGNUM},
     nullptr,
     nullptr,
     nullptr,
     0},
    {"d19",
     nullptr,
     8,
     0,
     eEncodingIEEE754,
     eFormatFloat,
     {LLDB_INVALID_REGNUM, dwarf_d19, LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM,
      LLDB_INVALID_REGNUM},
     nullptr,
     nullptr,
     nullptr,
     0},
    {"d20",
     nullptr,
     8,
     0,
     eEncodingIEEE754,
     eFormatFloat,
     {LLDB_INVALID_REGNUM, dwarf_d20, LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM,
      LLDB_INVALID_REGNUM},
     nullptr,
     nullptr,
     nullptr,
     0},
    {"d21",
     nullptr,
     8,
     0,
     eEncodingIEEE754,
     eFormatFloat,
     {LLDB_INVALID_REGNUM, dwarf_d21, LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM,
      LLDB_INVALID_REGNUM},
     nullptr,
     nullptr,
     nullptr,
     0},
    {"d22",
     nullptr,
     8,
     0,
     eEncodingIEEE754,
     eFormatFloat,
     {LLDB_INVALID_REGNUM, dwarf_d22, LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM,
      LLDB_INVALID_REGNUM},
     nullptr,
     nullptr,
     nullptr,
     0},
    {"d23",
     nullptr,
     8,
     0,
     eEncodingIEEE754,
     eFormatFloat,
     {LLDB_INVALID_REGNUM, dwarf_d23, LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM,
      LLDB_INVALID_REGNUM},
     nullptr,
     nullptr,
     nullptr,
     0},
    {"d24",
     nullptr,
     8,
     0,
     eEncodingIEEE754,
     eFormatFloat,
     {LLDB_INVALID_REGNUM, dwarf_d24, LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM,
      LLDB_INVALID_REGNUM},
     nullptr,
     nullptr,
     nullptr,
     0},
    {"d25",
     nullptr,
     8,
     0,
     eEncodingIEEE754,
     eFormatFloat,
     {LLDB_INVALID_REGNUM, dwarf_d25, LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM,
      LLDB_INVALID_REGNUM},
     nullptr,
     nullptr,
     nullptr,
     0},
    {"d26",
     nullptr,
     8,
     0,
     eEncodingIEEE754,
     eFormatFloat,
     {LLDB_INVALID_REGNUM, dwarf_d26, LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM,
      LLDB_INVALID_REGNUM},
     nullptr,
     nullptr,
     nullptr,
     0},
    {"d27",
     nullptr,
     8,
     0,
     eEncodingIEEE754,
     eFormatFloat,
     {LLDB_INVALID_REGNUM, dwarf_d27, LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM,
      LLDB_INVALID_REGNUM},
     nullptr,
     nullptr,
     nullptr,
     0},
    {"d28",
     nullptr,
     8,
     0,
     eEncodingIEEE754,
     eFormatFloat,
     {LLDB_INVALID_REGNUM, dwarf_d28, LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM,
      LLDB_INVALID_REGNUM},
     nullptr,
     nullptr,
     nullptr,
     0},
    {"d29",
     nullptr,
     8,
     0,
     eEncodingIEEE754,
     eFormatFloat,
     {LLDB_INVALID_REGNUM, dwarf_d29, LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM,
      LLDB_INVALID_REGNUM},
     nullptr,
     nullptr,
     nullptr,
     0},
    {"d30",
     nullptr,
     8,
     0,
     eEncodingIEEE754,
     eFormatFloat,
     {LLDB_INVALID_REGNUM, dwarf_d30, LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM,
      LLDB_INVALID_REGNUM},
     nullptr,
     nullptr,
     nullptr,
     0},
    {"d31",
     nullptr,
     8,
     0,
     eEncodingIEEE754,
     eFormatFloat,
     {LLDB_INVALID_REGNUM, dwarf_d31, LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM,
      LLDB_INVALID_REGNUM},
     nullptr,
     nullptr,
     nullptr,
     0},
    {"r8_usr",
     nullptr,
     4,
     0,
     eEncodingUint,
     eFormatHex,
     {LLDB_INVALID_REGNUM, dwarf_r8_usr, LLDB_INVALID_REGNUM,
      LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM},
     nullptr,
     nullptr,
     nullptr,
     0},
    {"r9_usr",
     nullptr,
     4,
     0,
     eEncodingUint,
     eFormatHex,
     {LLDB_INVALID_REGNUM, dwarf_r9_usr, LLDB_INVALID_REGNUM,
      LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM},
     nullptr,
     nullptr,
     nullptr,
     0},
    {"r10_usr",
     nullptr,
     4,
     0,
     eEncodingUint,
     eFormatHex,
     {LLDB_INVALID_REGNUM, dwarf_r10_usr, LLDB_INVALID_REGNUM,
      LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM},
     nullptr,
     nullptr,
     nullptr,
     0},
    {"r11_usr",
     nullptr,
     4,
     0,
     eEncodingUint,
     eFormatHex,
     {LLDB_INVALID_REGNUM, dwarf_r11_usr, LLDB_INVALID_REGNUM,
      LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM},
     nullptr,
     nullptr,
     nullptr,
     0},
    {"r12_usr",
     nullptr,
     4,
     0,
     eEncodingUint,
     eFormatHex,
     {LLDB_INVALID_REGNUM, dwarf_r12_usr, LLDB_INVALID_REGNUM,
      LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM},
     nullptr,
     nullptr,
     nullptr,
     0},
    {"r13_usr",
     "sp_usr",
     4,
     0,
     eEncodingUint,
     eFormatHex,
     {LLDB_INVALID_REGNUM, dwarf_r13_usr, LLDB_INVALID_REGNUM,
      LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM},
     nullptr,
     nullptr,
     nullptr,
     0},
    {"r14_usr",
     "lr_usr",
     4,
     0,
     eEncodingUint,
     eFormatHex,
     {LLDB_INVALID_REGNUM, dwarf_r14_usr, LLDB_INVALID_REGNUM,
      LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM},
     nullptr,
     nullptr,
     nullptr,
     0},
    {"r8_fiq",
     nullptr,
     4,
     0,
     eEncodingUint,
     eFormatHex,
     {LLDB_INVALID_REGNUM, dwarf_r8_fiq, LLDB_INVALID_REGNUM,
      LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM},
     nullptr,
     nullptr,
     nullptr,
     0},
    {"r9_fiq",
     nullptr,
     4,
     0,
     eEncodingUint,
     eFormatHex,
     {LLDB_INVALID_REGNUM, dwarf_r9_fiq, LLDB_INVALID_REGNUM,
      LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM},
     nullptr,
     nullptr,
     nullptr,
     0},
    {"r10_fiq",
     nullptr,
     4,
     0,
     eEncodingUint,
     eFormatHex,
     {LLDB_INVALID_REGNUM, dwarf_r10_fiq, LLDB_INVALID_REGNUM,
      LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM},
     nullptr,
     nullptr,
     nullptr,
     0},
    {"r11_fiq",
     nullptr,
     4,
     0,
     eEncodingUint,
     eFormatHex,
     {LLDB_INVALID_REGNUM, dwarf_r11_fiq, LLDB_INVALID_REGNUM,
      LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM},
     nullptr,
     nullptr,
     nullptr,
     0},
    {"r12_fiq",
     nullptr,
     4,
     0,
     eEncodingUint,
     eFormatHex,
     {LLDB_INVALID_REGNUM, dwarf_r12_fiq, LLDB_INVALID_REGNUM,
      LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM},
     nullptr,
     nullptr,
     nullptr,
     0},
    {"r13_fiq",
     "sp_fiq",
     4,
     0,
     eEncodingUint,
     eFormatHex,
     {LLDB_INVALID_REGNUM, dwarf_r13_fiq, LLDB_INVALID_REGNUM,
      LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM},
     nullptr,
     nullptr,
     nullptr,
     0},
    {"r14_fiq",
     "lr_fiq",
     4,
     0,
     eEncodingUint,
     eFormatHex,
     {LLDB_INVALID_REGNUM, dwarf_r14_fiq, LLDB_INVALID_REGNUM,
      LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM},
     nullptr,
     nullptr,
     nullptr,
     0},
    {"r13_irq",
     "sp_irq",
     4,
     0,
     eEncodingUint,
     eFormatHex,
     {LLDB_INVALID_REGNUM, dwarf_r13_irq, LLDB_INVALID_REGNUM,
      LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM},
     nullptr,
     nullptr,
     nullptr,
     0},
    {"r14_irq",
     "lr_irq",
     4,
     0,
     eEncodingUint,
     eFormatHex,
     {LLDB_INVALID_REGNUM, dwarf_r14_irq, LLDB_INVALID_REGNUM,
      LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM},
     nullptr,
     nullptr,
     nullptr,
     0},
    {"r13_abt",
     "sp_abt",
     4,
     0,
     eEncodingUint,
     eFormatHex,
     {LLDB_INVALID_REGNUM, dwarf_r13_abt, LLDB_INVALID_REGNUM,
      LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM},
     nullptr,
     nullptr,
     nullptr,
     0},
    {"r14_abt",
     "lr_abt",
     4,
     0,
     eEncodingUint,
     eFormatHex,
     {LLDB_INVALID_REGNUM, dwarf_r14_abt, LLDB_INVALID_REGNUM,
      LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM},
     nullptr,
     nullptr,
     nullptr,
     0},
    {"r13_und",
     "sp_und",
     4,
     0,
     eEncodingUint,
     eFormatHex,
     {LLDB_INVALID_REGNUM, dwarf_r13_und, LLDB_INVALID_REGNUM,
      LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM},
     nullptr,
     nullptr,
     nullptr,
     0},
    {"r14_und",
     "lr_und",
     4,
     0,
     eEncodingUint,
     eFormatHex,
     {LLDB_INVALID_REGNUM, dwarf_r14_und, LLDB_INVALID_REGNUM,
      LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM},
     nullptr,
     nullptr,
     nullptr,
     0},
    {"r13_svc",
     "sp_svc",
     4,
     0,
     eEncodingUint,
     eFormatHex,
     {LLDB_INVALID_REGNUM, dwarf_r13_svc, LLDB_INVALID_REGNUM,
      LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM},
     nullptr,
     nullptr,
     nullptr,
     0},
    {"r14_svc",
     "lr_svc",
     4,
     0,
     eEncodingUint,
     eFormatHex,
     {LLDB_INVALID_REGNUM, dwarf_r14_svc, LLDB_INVALID_REGNUM,
      LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM},
     nullptr,
     nullptr,
     nullptr,
     0}};

static const uint32_t k_num_register_infos =
    llvm::array_lengthof(g_register_infos);
static bool g_register_info_names_constified = false;

const lldb_private::RegisterInfo *
ABIMacOSX_arm::GetRegisterInfoArray(uint32_t &count) {
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

size_t ABIMacOSX_arm::GetRedZoneSize() const { return 0; }

//------------------------------------------------------------------
// Static Functions
//------------------------------------------------------------------

ABISP
ABIMacOSX_arm::CreateInstance(ProcessSP process_sp, const ArchSpec &arch) {
  const llvm::Triple::ArchType arch_type = arch.GetTriple().getArch();
  const llvm::Triple::VendorType vendor_type = arch.GetTriple().getVendor();

  if (vendor_type == llvm::Triple::Apple) {
    if ((arch_type == llvm::Triple::arm) ||
        (arch_type == llvm::Triple::thumb)) {
      return ABISP(new ABIMacOSX_arm(process_sp));
    }
  }

  return ABISP();
}

bool ABIMacOSX_arm::PrepareTrivialCall(Thread &thread, addr_t sp,
                                       addr_t function_addr, addr_t return_addr,
                                       llvm::ArrayRef<addr_t> args) const {
  RegisterContext *reg_ctx = thread.GetRegisterContext().get();
  if (!reg_ctx)
    return false;

  const uint32_t pc_reg_num = reg_ctx->ConvertRegisterKindToRegisterNumber(
      eRegisterKindGeneric, LLDB_REGNUM_GENERIC_PC);
  const uint32_t sp_reg_num = reg_ctx->ConvertRegisterKindToRegisterNumber(
      eRegisterKindGeneric, LLDB_REGNUM_GENERIC_SP);
  const uint32_t ra_reg_num = reg_ctx->ConvertRegisterKindToRegisterNumber(
      eRegisterKindGeneric, LLDB_REGNUM_GENERIC_RA);

  RegisterValue reg_value;

  const char *reg_names[] = {"r0", "r1", "r2", "r3"};

  llvm::ArrayRef<addr_t>::iterator ai = args.begin(), ae = args.end();

  for (size_t i = 0; i < llvm::array_lengthof(reg_names); ++i) {
    if (ai == ae)
      break;

    reg_value.SetUInt32(*ai);
    if (!reg_ctx->WriteRegister(reg_ctx->GetRegisterInfoByName(reg_names[i]),
                                reg_value))
      return false;

    ++ai;
  }

  if (ai != ae) {
    // Spill onto the stack
    size_t num_stack_regs = ae - ai;

    sp -= (num_stack_regs * 4);
    // Keep the stack 16 byte aligned
    sp &= ~(16ull - 1ull);

    // just using arg1 to get the right size
    const RegisterInfo *reg_info = reg_ctx->GetRegisterInfo(
        eRegisterKindGeneric, LLDB_REGNUM_GENERIC_ARG1);

    addr_t arg_pos = sp;

    for (; ai != ae; ++ai) {
      reg_value.SetUInt32(*ai);
      if (reg_ctx
              ->WriteRegisterValueToMemory(reg_info, arg_pos,
                                           reg_info->byte_size, reg_value)
              .Fail())
        return false;
      arg_pos += reg_info->byte_size;
    }
  }

  TargetSP target_sp(thread.CalculateTarget());
  Address so_addr;

  // Figure out if our return address is ARM or Thumb by using the
  // Address::GetCallableLoadAddress(Target*) which will figure out the ARM
  // thumb-ness and set the correct address bits for us.
  so_addr.SetLoadAddress(return_addr, target_sp.get());
  return_addr = so_addr.GetCallableLoadAddress(target_sp.get());

  // Set "lr" to the return address
  if (!reg_ctx->WriteRegisterFromUnsigned(ra_reg_num, return_addr))
    return false;

  // If bit zero or 1 is set, this must be a thumb function, no need to figure
  // this out from the symbols.
  so_addr.SetLoadAddress(function_addr, target_sp.get());
  function_addr = so_addr.GetCallableLoadAddress(target_sp.get());

  const RegisterInfo *cpsr_reg_info = reg_ctx->GetRegisterInfoByName("cpsr");
  const uint32_t curr_cpsr = reg_ctx->ReadRegisterAsUnsigned(cpsr_reg_info, 0);

  // Make a new CPSR and mask out any Thumb IT (if/then) bits
  uint32_t new_cpsr = curr_cpsr & ~MASK_CPSR_IT_MASK;
  // If bit zero or 1 is set, this must be thumb...
  if (function_addr & 1ull)
    new_cpsr |= MASK_CPSR_T; // Set T bit in CPSR
  else
    new_cpsr &= ~MASK_CPSR_T; // Clear T bit in CPSR

  if (new_cpsr != curr_cpsr) {
    if (!reg_ctx->WriteRegisterFromUnsigned(cpsr_reg_info, new_cpsr))
      return false;
  }

  function_addr &=
      ~1ull; // clear bit zero since the CPSR will take care of the mode for us

  // Update the sp - stack pointer - to be aligned to 16-bytes
  sp &= ~(0xfull);
  if (!reg_ctx->WriteRegisterFromUnsigned(sp_reg_num, sp))
    return false;

  // Set "pc" to the address requested
  if (!reg_ctx->WriteRegisterFromUnsigned(pc_reg_num, function_addr))
    return false;

  return true;
}

bool ABIMacOSX_arm::GetArgumentValues(Thread &thread, ValueList &values) const {
  uint32_t num_values = values.GetSize();

  ExecutionContext exe_ctx(thread.shared_from_this());
  // For now, assume that the types in the AST values come from the Target's
  // scratch AST.

  // Extract the register context so we can read arguments from registers

  RegisterContext *reg_ctx = thread.GetRegisterContext().get();

  if (!reg_ctx)
    return false;

  addr_t sp = 0;

  for (uint32_t value_idx = 0; value_idx < num_values; ++value_idx) {
    // We currently only support extracting values with Clang QualTypes. Do we
    // care about others?
    Value *value = values.GetValueAtIndex(value_idx);

    if (!value)
      return false;

    CompilerType compiler_type = value->GetCompilerType();
    if (compiler_type) {
      bool is_signed = false;
      size_t bit_width = 0;
      llvm::Optional<uint64_t> bit_size = compiler_type.GetBitSize(&thread);
      if (!bit_size)
        return false;
      if (compiler_type.IsIntegerOrEnumerationType(is_signed))
        bit_width = *bit_size;
      else if (compiler_type.IsPointerOrReferenceType())
        bit_width = *bit_size;
      else
        // We only handle integer, pointer and reference types currently...
        return false;

      if (bit_width <= (exe_ctx.GetProcessRef().GetAddressByteSize() * 8)) {
        if (value_idx < 4) {
          // Arguments 1-4 are in r0-r3...
          const RegisterInfo *arg_reg_info = nullptr;
          // Search by generic ID first, then fall back to by name
          uint32_t arg_reg_num = reg_ctx->ConvertRegisterKindToRegisterNumber(
              eRegisterKindGeneric, LLDB_REGNUM_GENERIC_ARG1 + value_idx);
          if (arg_reg_num != LLDB_INVALID_REGNUM) {
            arg_reg_info = reg_ctx->GetRegisterInfoAtIndex(arg_reg_num);
          } else {
            switch (value_idx) {
            case 0:
              arg_reg_info = reg_ctx->GetRegisterInfoByName("r0");
              break;
            case 1:
              arg_reg_info = reg_ctx->GetRegisterInfoByName("r1");
              break;
            case 2:
              arg_reg_info = reg_ctx->GetRegisterInfoByName("r2");
              break;
            case 3:
              arg_reg_info = reg_ctx->GetRegisterInfoByName("r3");
              break;
            }
          }

          if (arg_reg_info) {
            RegisterValue reg_value;

            if (reg_ctx->ReadRegister(arg_reg_info, reg_value)) {
              if (is_signed)
                reg_value.SignExtend(bit_width);
              if (!reg_value.GetScalarValue(value->GetScalar()))
                return false;
              continue;
            }
          }
          return false;
        } else {
          if (sp == 0) {
            // Read the stack pointer if it already hasn't been read
            sp = reg_ctx->GetSP(0);
            if (sp == 0)
              return false;
          }

          // Arguments 5 on up are on the stack
          const uint32_t arg_byte_size = (bit_width + (8 - 1)) / 8;
          Status error;
          if (!exe_ctx.GetProcessRef().ReadScalarIntegerFromMemory(
                  sp, arg_byte_size, is_signed, value->GetScalar(), error))
            return false;

          sp += arg_byte_size;
        }
      }
    }
  }
  return true;
}

bool ABIMacOSX_arm::IsArmv7kProcess() const {
  bool is_armv7k = false;
  ProcessSP process_sp(GetProcessSP());
  if (process_sp) {
    const ArchSpec &arch(process_sp->GetTarget().GetArchitecture());
    const ArchSpec::Core system_core = arch.GetCore();
    if (system_core == ArchSpec::eCore_arm_armv7k) {
      is_armv7k = true;
    }
  }
  return is_armv7k;
}

ValueObjectSP ABIMacOSX_arm::GetReturnValueObjectImpl(
    Thread &thread, lldb_private::CompilerType &compiler_type) const {
  Value value;
  ValueObjectSP return_valobj_sp;

  if (!compiler_type)
    return return_valobj_sp;

  value.SetCompilerType(compiler_type);

  RegisterContext *reg_ctx = thread.GetRegisterContext().get();
  if (!reg_ctx)
    return return_valobj_sp;

  bool is_signed;

  // Get the pointer to the first stack argument so we have a place to start
  // when reading data

  const RegisterInfo *r0_reg_info = reg_ctx->GetRegisterInfoByName("r0", 0);
  if (compiler_type.IsIntegerOrEnumerationType(is_signed)) {
    llvm::Optional<uint64_t> bit_width = compiler_type.GetBitSize(&thread);
    if (!bit_width)
      return return_valobj_sp;

    switch (*bit_width) {
    default:
      return return_valobj_sp;
    case 128:
      if (IsArmv7kProcess()) {
        // "A composite type not larger than 16 bytes is returned in r0-r3. The
        // format is as if the result had been stored in memory at a word-
        // aligned address and then loaded into r0-r3 with an ldm instruction"
        {
          const RegisterInfo *r1_reg_info =
              reg_ctx->GetRegisterInfoByName("r1", 0);
          const RegisterInfo *r2_reg_info =
              reg_ctx->GetRegisterInfoByName("r2", 0);
          const RegisterInfo *r3_reg_info =
              reg_ctx->GetRegisterInfoByName("r3", 0);
          if (r1_reg_info && r2_reg_info && r3_reg_info) {
            llvm::Optional<uint64_t> byte_size =
                compiler_type.GetByteSize(&thread);
            if (!byte_size)
              return return_valobj_sp;
            ProcessSP process_sp(thread.GetProcess());
            if (*byte_size <= r0_reg_info->byte_size + r1_reg_info->byte_size +
                                  r2_reg_info->byte_size +
                                  r3_reg_info->byte_size &&
                process_sp) {
              std::unique_ptr<DataBufferHeap> heap_data_ap(
                  new DataBufferHeap(*byte_size, 0));
              const ByteOrder byte_order = process_sp->GetByteOrder();
              RegisterValue r0_reg_value;
              RegisterValue r1_reg_value;
              RegisterValue r2_reg_value;
              RegisterValue r3_reg_value;
              if (reg_ctx->ReadRegister(r0_reg_info, r0_reg_value) &&
                  reg_ctx->ReadRegister(r1_reg_info, r1_reg_value) &&
                  reg_ctx->ReadRegister(r2_reg_info, r2_reg_value) &&
                  reg_ctx->ReadRegister(r3_reg_info, r3_reg_value)) {
                Status error;
                if (r0_reg_value.GetAsMemoryData(r0_reg_info,
                                                 heap_data_ap->GetBytes() + 0,
                                                 4, byte_order, error) &&
                    r1_reg_value.GetAsMemoryData(r1_reg_info,
                                                 heap_data_ap->GetBytes() + 4,
                                                 4, byte_order, error) &&
                    r2_reg_value.GetAsMemoryData(r2_reg_info,
                                                 heap_data_ap->GetBytes() + 8,
                                                 4, byte_order, error) &&
                    r3_reg_value.GetAsMemoryData(r3_reg_info,
                                                 heap_data_ap->GetBytes() + 12,
                                                 4, byte_order, error)) {
                  DataExtractor data(DataBufferSP(heap_data_ap.release()),
                                     byte_order,
                                     process_sp->GetAddressByteSize());

                  return_valobj_sp = ValueObjectConstResult::Create(
                      &thread, compiler_type, ConstString(""), data);
                  return return_valobj_sp;
                }
              }
            }
          }
        }
      } else {
        return return_valobj_sp;
      }
      break;
    case 64: {
      const RegisterInfo *r1_reg_info = reg_ctx->GetRegisterInfoByName("r1", 0);
      uint64_t raw_value;
      raw_value = reg_ctx->ReadRegisterAsUnsigned(r0_reg_info, 0) & UINT32_MAX;
      raw_value |= ((uint64_t)(reg_ctx->ReadRegisterAsUnsigned(r1_reg_info, 0) &
                               UINT32_MAX))
                   << 32;
      if (is_signed)
        value.GetScalar() = (int64_t)raw_value;
      else
        value.GetScalar() = (uint64_t)raw_value;
    } break;
    case 32:
      if (is_signed)
        value.GetScalar() = (int32_t)(
            reg_ctx->ReadRegisterAsUnsigned(r0_reg_info, 0) & UINT32_MAX);
      else
        value.GetScalar() = (uint32_t)(
            reg_ctx->ReadRegisterAsUnsigned(r0_reg_info, 0) & UINT32_MAX);
      break;
    case 16:
      if (is_signed)
        value.GetScalar() = (int16_t)(
            reg_ctx->ReadRegisterAsUnsigned(r0_reg_info, 0) & UINT16_MAX);
      else
        value.GetScalar() = (uint16_t)(
            reg_ctx->ReadRegisterAsUnsigned(r0_reg_info, 0) & UINT16_MAX);
      break;
    case 8:
      if (is_signed)
        value.GetScalar() = (int8_t)(
            reg_ctx->ReadRegisterAsUnsigned(r0_reg_info, 0) & UINT8_MAX);
      else
        value.GetScalar() = (uint8_t)(
            reg_ctx->ReadRegisterAsUnsigned(r0_reg_info, 0) & UINT8_MAX);
      break;
    }
  } else if (compiler_type.IsPointerType()) {
    uint32_t ptr =
        thread.GetRegisterContext()->ReadRegisterAsUnsigned(r0_reg_info, 0) &
        UINT32_MAX;
    value.GetScalar() = ptr;
  } else {
    // not handled yet
    return return_valobj_sp;
  }

  // If we get here, we have a valid Value, so make our ValueObject out of it:

  return_valobj_sp = ValueObjectConstResult::Create(
      thread.GetStackFrameAtIndex(0).get(), value, ConstString(""));
  return return_valobj_sp;
}

Status ABIMacOSX_arm::SetReturnValueObject(lldb::StackFrameSP &frame_sp,
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
      const RegisterInfo *r0_info = reg_ctx->GetRegisterInfoByName("r0", 0);
      if (num_bytes <= 4) {
        uint32_t raw_value = data.GetMaxU32(&offset, num_bytes);

        if (reg_ctx->WriteRegisterFromUnsigned(r0_info, raw_value))
          set_it_simple = true;
      } else {
        uint32_t raw_value = data.GetMaxU32(&offset, 4);

        if (reg_ctx->WriteRegisterFromUnsigned(r0_info, raw_value)) {
          const RegisterInfo *r1_info = reg_ctx->GetRegisterInfoByName("r1", 0);
          uint32_t raw_value = data.GetMaxU32(&offset, num_bytes - offset);

          if (reg_ctx->WriteRegisterFromUnsigned(r1_info, raw_value))
            set_it_simple = true;
        }
      }
    } else if (num_bytes <= 16 && IsArmv7kProcess()) {
      // "A composite type not larger than 16 bytes is returned in r0-r3. The
      // format is as if the result had been stored in memory at a word-aligned
      // address and then loaded into r0-r3 with an ldm instruction"

      const RegisterInfo *r0_info = reg_ctx->GetRegisterInfoByName("r0", 0);
      const RegisterInfo *r1_info = reg_ctx->GetRegisterInfoByName("r1", 0);
      const RegisterInfo *r2_info = reg_ctx->GetRegisterInfoByName("r2", 0);
      const RegisterInfo *r3_info = reg_ctx->GetRegisterInfoByName("r3", 0);
      lldb::offset_t offset = 0;
      uint32_t bytes_written = 4;
      uint32_t raw_value = data.GetMaxU64(&offset, 4);
      if (reg_ctx->WriteRegisterFromUnsigned(r0_info, raw_value) &&
          bytes_written <= num_bytes) {
        bytes_written += 4;
        raw_value = data.GetMaxU64(&offset, 4);
        if (bytes_written <= num_bytes &&
            reg_ctx->WriteRegisterFromUnsigned(r1_info, raw_value)) {
          bytes_written += 4;
          raw_value = data.GetMaxU64(&offset, 4);
          if (bytes_written <= num_bytes &&
              reg_ctx->WriteRegisterFromUnsigned(r2_info, raw_value)) {
            bytes_written += 4;
            raw_value = data.GetMaxU64(&offset, 4);
            if (bytes_written <= num_bytes &&
                reg_ctx->WriteRegisterFromUnsigned(r3_info, raw_value)) {
              set_it_simple = true;
            }
          }
        }
      }
    } else {
      error.SetErrorString("We don't support returning longer than 64 bit "
                           "integer values at present.");
    }
  } else if (compiler_type.IsFloatingPointType(count, is_complex)) {
    if (is_complex)
      error.SetErrorString(
          "We don't support returning complex values at present");
    else
      error.SetErrorString(
          "We don't support returning float values at present");
  }

  if (!set_it_simple)
    error.SetErrorString(
        "We only support setting simple integer return types at present.");

  return error;
}

bool ABIMacOSX_arm::CreateFunctionEntryUnwindPlan(UnwindPlan &unwind_plan) {
  unwind_plan.Clear();
  unwind_plan.SetRegisterKind(eRegisterKindDWARF);

  uint32_t lr_reg_num = dwarf_lr;
  uint32_t sp_reg_num = dwarf_sp;
  uint32_t pc_reg_num = dwarf_pc;

  UnwindPlan::RowSP row(new UnwindPlan::Row);

  // Our Call Frame Address is the stack pointer value
  row->GetCFAValue().SetIsRegisterPlusOffset(sp_reg_num, 0);

  // The previous PC is in the LR
  row->SetRegisterLocationToRegister(pc_reg_num, lr_reg_num, true);
  unwind_plan.AppendRow(row);

  // All other registers are the same.

  unwind_plan.SetSourceName("arm at-func-entry default");
  unwind_plan.SetSourcedFromCompiler(eLazyBoolNo);

  return true;
}

bool ABIMacOSX_arm::CreateDefaultUnwindPlan(UnwindPlan &unwind_plan) {
  unwind_plan.Clear();
  unwind_plan.SetRegisterKind(eRegisterKindDWARF);

  uint32_t fp_reg_num =
      dwarf_r7; // apple uses r7 for all frames. Normal arm uses r11
  uint32_t pc_reg_num = dwarf_pc;

  UnwindPlan::RowSP row(new UnwindPlan::Row);
  const int32_t ptr_size = 4;

  row->GetCFAValue().SetIsRegisterPlusOffset(fp_reg_num, 2 * ptr_size);
  row->SetOffset(0);

  row->SetRegisterLocationToAtCFAPlusOffset(fp_reg_num, ptr_size * -2, true);
  row->SetRegisterLocationToAtCFAPlusOffset(pc_reg_num, ptr_size * -1, true);

  unwind_plan.AppendRow(row);
  unwind_plan.SetSourceName("arm-apple-ios default unwind plan");
  unwind_plan.SetSourcedFromCompiler(eLazyBoolNo);
  unwind_plan.SetUnwindPlanValidAtAllInstructions(eLazyBoolNo);

  return true;
}

// cf. "ARMv6 Function Calling Conventions"
// https://developer.apple.com/library/ios/documentation/Xcode/Conceptual/iPhoneOSABIReference/Articles/ARMv6FunctionCallingConventions.html
// and "ARMv7 Function Calling Conventions"
// https://developer.apple.com/library/ios/documentation/Xcode/Conceptual/iPhoneOSABIReference/Articles/ARMv7FunctionCallingConventions.html

// ARMv7 on iOS general purpose reg rules:
//    r0-r3 not preserved  (used for argument passing)
//    r4-r6 preserved
//    r7    preserved (frame pointer)
//    r8    preserved
//    r9    not preserved (usable as volatile scratch register with iOS 3.x and
//    later)
//    r10-r11 preserved
//    r12   not presrved
//    r13   preserved (stack pointer)
//    r14   not preserved (link register)
//    r15   preserved (pc)
//    cpsr  not preserved (different rules for different bits)

// ARMv7 on iOS floating point rules:
//    d0-d7   not preserved   (aka s0-s15, q0-q3)
//    d8-d15  preserved       (aka s16-s31, q4-q7)
//    d16-d31 not preserved   (aka q8-q15)

bool ABIMacOSX_arm::RegisterIsVolatile(const RegisterInfo *reg_info) {
  if (reg_info) {
    // Volatile registers are: r0, r1, r2, r3, r9, r12, r13 (aka sp)
    const char *name = reg_info->name;
    if (name[0] == 'r') {
      switch (name[1]) {
      case '0':
        return name[2] == '\0'; // r0
      case '1':
        switch (name[2]) {
        case '\0':
          return true; // r1
        case '2':
        case '3':
          return name[3] == '\0'; // r12, r13 (sp)
        default:
          break;
        }
        break;

      case '2':
        return name[2] == '\0'; // r2
      case '3':
        return name[2] == '\0'; // r3
      case '9':
        return name[2] == '\0'; // r9 (apple-ios only...)

        break;
      }
    } else if (name[0] == 'd') {
      switch (name[1]) {
      case '0':
        return name[2] == '\0'; // d0 is volatile

      case '1':
        switch (name[2]) {
        case '\0':
          return true; // d1 is volatile
        case '6':
        case '7':
        case '8':
        case '9':
          return name[3] == '\0'; // d16 - d19 are volatile
        default:
          break;
        }
        break;

      case '2':
        switch (name[2]) {
        case '\0':
          return true; // d2 is volatile
        case '0':
        case '1':
        case '2':
        case '3':
        case '4':
        case '5':
        case '6':
        case '7':
        case '8':
        case '9':
          return name[3] == '\0'; // d20 - d29 are volatile
        default:
          break;
        }
        break;

      case '3':
        switch (name[2]) {
        case '\0':
          return true; // d3 is volatile
        case '0':
        case '1':
          return name[3] == '\0'; // d30 - d31 are volatile
        default:
          break;
        }
        break;
      case '4':
      case '5':
      case '6':
      case '7':
        return name[2] == '\0'; // d4 - d7 are volatile

      default:
        break;
      }
    } else if (name[0] == 's') {
      switch (name[1]) {
      case '0':
        return name[2] == '\0'; // s0 is volatile

      case '1':
        switch (name[2]) {
        case '\0':
          return true; // s1 is volatile
        case '0':
        case '1':
        case '2':
        case '3':
        case '4':
        case '5':
          return name[3] == '\0'; // s10 - s15 are volatile
        default:
          break;
        }
        break;

      case '2':
      case '3':
      case '4':
      case '5':
      case '6':
      case '7':
      case '8':
      case '9':
        return name[2] == '\0'; // s2 - s9 are volatile

      default:
        break;
      }
    } else if (name[0] == 'q') {
      switch (name[1]) {
      case '1':
        switch (name[2]) {
        case '\0':
          return true; // q1 is volatile
        case '0':
        case '1':
        case '2':
        case '3':
        case '4':
        case '5':
          return true; // q10-q15 are volatile
        default:
          break;
        };
        break;
      case '0':
      case '2':
      case '3':
        return name[2] == '\0'; // q0-q3 are volatile
      case '8':
      case '9':
        return name[2] == '\0'; // q8-q9 are volatile
      default:
        break;
      }
    } else if (name[0] == 's' && name[1] == 'p' && name[2] == '\0')
      return true;
  }
  return false;
}

void ABIMacOSX_arm::Initialize() {
  PluginManager::RegisterPlugin(GetPluginNameStatic(),
                                "Mac OS X ABI for arm targets", CreateInstance);
}

void ABIMacOSX_arm::Terminate() {
  PluginManager::UnregisterPlugin(CreateInstance);
}

lldb_private::ConstString ABIMacOSX_arm::GetPluginNameStatic() {
  static ConstString g_name("macosx-arm");
  return g_name;
}

//------------------------------------------------------------------
// PluginInterface protocol
//------------------------------------------------------------------

lldb_private::ConstString ABIMacOSX_arm::GetPluginName() {
  return GetPluginNameStatic();
}

uint32_t ABIMacOSX_arm::GetPluginVersion() { return 1; }
