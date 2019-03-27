//===-- ABISysV_hexagon.cpp -------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "ABISysV_hexagon.h"

#include "llvm/ADT/Triple.h"
#include "llvm/IR/DerivedTypes.h"

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

static RegisterInfo g_register_infos[] = {
    // hexagon-core.xml
    {"r00",
     "",
     4,
     0,
     eEncodingUint,
     eFormatAddressInfo,
     {0, 0, LLDB_INVALID_REGNUM, 0, 0},
     nullptr,
     nullptr,
     nullptr,
     0},
    {"r01",
     "",
     4,
     0,
     eEncodingUint,
     eFormatAddressInfo,
     {1, 1, LLDB_INVALID_REGNUM, 1, 1},
     nullptr,
     nullptr,
     nullptr,
     0},
    {"r02",
     "",
     4,
     0,
     eEncodingUint,
     eFormatAddressInfo,
     {2, 2, LLDB_INVALID_REGNUM, 2, 2},
     nullptr,
     nullptr,
     nullptr,
     0},
    {"r03",
     "",
     4,
     0,
     eEncodingUint,
     eFormatAddressInfo,
     {3, 3, LLDB_INVALID_REGNUM, 3, 3},
     nullptr,
     nullptr,
     nullptr,
     0},
    {"r04",
     "",
     4,
     0,
     eEncodingUint,
     eFormatAddressInfo,
     {4, 4, LLDB_INVALID_REGNUM, 4, 4},
     nullptr,
     nullptr,
     nullptr,
     0},
    {"r05",
     "",
     4,
     0,
     eEncodingUint,
     eFormatAddressInfo,
     {5, 5, LLDB_INVALID_REGNUM, 5, 5},
     nullptr,
     nullptr,
     nullptr,
     0},
    {"r06",
     "",
     4,
     0,
     eEncodingUint,
     eFormatAddressInfo,
     {6, 6, LLDB_INVALID_REGNUM, 6, 6},
     nullptr,
     nullptr,
     nullptr,
     0},
    {"r07",
     "",
     4,
     0,
     eEncodingUint,
     eFormatAddressInfo,
     {7, 7, LLDB_INVALID_REGNUM, 7, 7},
     nullptr,
     nullptr,
     nullptr,
     0},
    {"r08",
     "",
     4,
     0,
     eEncodingUint,
     eFormatAddressInfo,
     {8, 8, LLDB_INVALID_REGNUM, 8, 8},
     nullptr,
     nullptr,
     nullptr,
     0},
    {"r09",
     "",
     4,
     0,
     eEncodingUint,
     eFormatAddressInfo,
     {9, 9, LLDB_INVALID_REGNUM, 9, 9},
     nullptr,
     nullptr,
     nullptr,
     0},
    {"r10",
     "",
     4,
     0,
     eEncodingUint,
     eFormatAddressInfo,
     {10, 10, LLDB_INVALID_REGNUM, 10, 10},
     nullptr,
     nullptr,
     nullptr,
     0},
    {"r11",
     "",
     4,
     0,
     eEncodingUint,
     eFormatAddressInfo,
     {11, 11, LLDB_INVALID_REGNUM, 11, 11},
     nullptr,
     nullptr,
     nullptr,
     0},
    {"r12",
     "",
     4,
     0,
     eEncodingUint,
     eFormatAddressInfo,
     {12, 12, LLDB_INVALID_REGNUM, 12, 12},
     nullptr,
     nullptr,
     nullptr,
     0},
    {"r13",
     "",
     4,
     0,
     eEncodingUint,
     eFormatAddressInfo,
     {13, 13, LLDB_INVALID_REGNUM, 13, 13},
     nullptr,
     nullptr,
     nullptr,
     0},
    {"r14",
     "",
     4,
     0,
     eEncodingUint,
     eFormatAddressInfo,
     {14, 14, LLDB_INVALID_REGNUM, 14, 14},
     nullptr,
     nullptr,
     nullptr,
     0},
    {"r15",
     "",
     4,
     0,
     eEncodingUint,
     eFormatAddressInfo,
     {15, 15, LLDB_INVALID_REGNUM, 15, 15},
     nullptr,
     nullptr,
     nullptr,
     0},
    {"r16",
     "",
     4,
     0,
     eEncodingUint,
     eFormatAddressInfo,
     {16, 16, LLDB_INVALID_REGNUM, 16, 16},
     nullptr,
     nullptr,
     nullptr,
     0},
    {"r17",
     "",
     4,
     0,
     eEncodingUint,
     eFormatAddressInfo,
     {17, 17, LLDB_INVALID_REGNUM, 17, 17},
     nullptr,
     nullptr,
     nullptr,
     0},
    {"r18",
     "",
     4,
     0,
     eEncodingUint,
     eFormatAddressInfo,
     {18, 18, LLDB_INVALID_REGNUM, 18, 18},
     nullptr,
     nullptr,
     nullptr,
     0},
    {"r19",
     "",
     4,
     0,
     eEncodingUint,
     eFormatAddressInfo,
     {19, 19, LLDB_INVALID_REGNUM, 19, 19},
     nullptr,
     nullptr,
     nullptr,
     0},
    {"r20",
     "",
     4,
     0,
     eEncodingUint,
     eFormatAddressInfo,
     {20, 20, LLDB_INVALID_REGNUM, 20, 20},
     nullptr,
     nullptr,
     nullptr,
     0},
    {"r21",
     "",
     4,
     0,
     eEncodingUint,
     eFormatAddressInfo,
     {21, 21, LLDB_INVALID_REGNUM, 21, 21},
     nullptr,
     nullptr,
     nullptr,
     0},
    {"r22",
     "",
     4,
     0,
     eEncodingUint,
     eFormatAddressInfo,
     {22, 22, LLDB_INVALID_REGNUM, 22, 22},
     nullptr,
     nullptr,
     nullptr,
     0},
    {"r23",
     "",
     4,
     0,
     eEncodingUint,
     eFormatAddressInfo,
     {23, 23, LLDB_INVALID_REGNUM, 23, 23},
     nullptr,
     nullptr,
     nullptr,
     0},
    {"r24",
     "",
     4,
     0,
     eEncodingUint,
     eFormatAddressInfo,
     {24, 24, LLDB_INVALID_REGNUM, 24, 24},
     nullptr,
     nullptr,
     nullptr,
     0},
    {"r25",
     "",
     4,
     0,
     eEncodingUint,
     eFormatAddressInfo,
     {25, 25, LLDB_INVALID_REGNUM, 25, 25},
     nullptr,
     nullptr,
     nullptr,
     0},
    {"r26",
     "",
     4,
     0,
     eEncodingUint,
     eFormatAddressInfo,
     {26, 26, LLDB_INVALID_REGNUM, 26, 26},
     nullptr,
     nullptr,
     nullptr,
     0},
    {"r27",
     "",
     4,
     0,
     eEncodingUint,
     eFormatAddressInfo,
     {27, 27, LLDB_INVALID_REGNUM, 27, 27},
     nullptr,
     nullptr,
     nullptr,
     0},
    {"r28",
     "",
     4,
     0,
     eEncodingUint,
     eFormatAddressInfo,
     {28, 28, LLDB_INVALID_REGNUM, 28, 28},
     nullptr,
     nullptr,
     nullptr,
     0},
    {"sp",
     "r29",
     4,
     0,
     eEncodingUint,
     eFormatAddressInfo,
     {29, 29, LLDB_REGNUM_GENERIC_SP, 29, 29},
     nullptr,
     nullptr,
     nullptr,
     0},
    {"fp",
     "r30",
     4,
     0,
     eEncodingUint,
     eFormatAddressInfo,
     {30, 30, LLDB_REGNUM_GENERIC_FP, 30, 30},
     nullptr,
     nullptr,
     nullptr,
     0},
    {"lr",
     "r31",
     4,
     0,
     eEncodingUint,
     eFormatAddressInfo,
     {31, 31, LLDB_REGNUM_GENERIC_RA, 31, 31},
     nullptr,
     nullptr,
     nullptr,
     0},
    {"sa0",
     "",
     4,
     0,
     eEncodingUint,
     eFormatAddressInfo,
     {32, 32, LLDB_INVALID_REGNUM, 32, 32},
     nullptr,
     nullptr,
     nullptr,
     0},
    {"lc0",
     "",
     4,
     0,
     eEncodingUint,
     eFormatAddressInfo,
     {33, 33, LLDB_INVALID_REGNUM, 33, 33},
     nullptr,
     nullptr,
     nullptr,
     0},
    {"sa1",
     "",
     4,
     0,
     eEncodingUint,
     eFormatAddressInfo,
     {34, 34, LLDB_INVALID_REGNUM, 34, 34},
     nullptr,
     nullptr,
     nullptr,
     0},
    {"lc1",
     "",
     4,
     0,
     eEncodingUint,
     eFormatAddressInfo,
     {35, 35, LLDB_INVALID_REGNUM, 35, 35},
     nullptr,
     nullptr,
     nullptr,
     0},
    // --> hexagon-v4/5/55/56-sim.xml
    {"p3_0",
     "",
     4,
     0,
     eEncodingUint,
     eFormatAddressInfo,
     {36, 36, LLDB_INVALID_REGNUM, 36, 36},
     nullptr,
     nullptr,
     nullptr,
     0},
    // PADDING {
    {"p00",
     "",
     4,
     0,
     eEncodingInvalid,
     eFormatInvalid,
     {37, 37, LLDB_INVALID_REGNUM, 37, 37},
     nullptr,
     nullptr,
     nullptr,
     0},
    // }
    {"m0",
     "",
     4,
     0,
     eEncodingUint,
     eFormatAddressInfo,
     {38, 38, LLDB_INVALID_REGNUM, 38, 38},
     nullptr,
     nullptr,
     nullptr,
     0},
    {"m1",
     "",
     4,
     0,
     eEncodingUint,
     eFormatAddressInfo,
     {39, 39, LLDB_INVALID_REGNUM, 39, 39},
     nullptr,
     nullptr,
     nullptr,
     0},
    {"usr",
     "",
     4,
     0,
     eEncodingUint,
     eFormatAddressInfo,
     {40, 40, LLDB_INVALID_REGNUM, 40, 40},
     nullptr,
     nullptr,
     nullptr,
     0},
    {"pc",
     "",
     4,
     0,
     eEncodingUint,
     eFormatAddressInfo,
     {41, 41, LLDB_REGNUM_GENERIC_PC, 41, 41},
     nullptr,
     nullptr,
     nullptr,
     0},
    {"ugp",
     "",
     4,
     0,
     eEncodingUint,
     eFormatAddressInfo,
     {42, 42, LLDB_INVALID_REGNUM, 42, 42},
     nullptr,
     nullptr,
     nullptr,
     0},
    {"gp",
     "",
     4,
     0,
     eEncodingUint,
     eFormatAddressInfo,
     {43, 43, LLDB_INVALID_REGNUM, 43, 43},
     nullptr,
     nullptr,
     nullptr,
     0},
    {"cs0",
     "",
     4,
     0,
     eEncodingUint,
     eFormatAddressInfo,
     {44, 44, LLDB_INVALID_REGNUM, 44, 44},
     nullptr,
     nullptr,
     nullptr,
     0},
    {"cs1",
     "",
     4,
     0,
     eEncodingUint,
     eFormatAddressInfo,
     {45, 45, LLDB_INVALID_REGNUM, 45, 45},
     nullptr,
     nullptr,
     nullptr,
     0},
    // PADDING {
    {"p01",
     "",
     4,
     0,
     eEncodingInvalid,
     eFormatInvalid,
     {46, 46, LLDB_INVALID_REGNUM, 46, 46},
     nullptr,
     nullptr,
     nullptr,
     0},
    {"p02",
     "",
     4,
     0,
     eEncodingInvalid,
     eFormatInvalid,
     {47, 47, LLDB_INVALID_REGNUM, 47, 47},
     nullptr,
     nullptr,
     nullptr,
     0},
    {"p03",
     "",
     4,
     0,
     eEncodingInvalid,
     eFormatInvalid,
     {48, 48, LLDB_INVALID_REGNUM, 48, 48},
     nullptr,
     nullptr,
     nullptr,
     0},
    {"p04",
     "",
     4,
     0,
     eEncodingInvalid,
     eFormatInvalid,
     {49, 49, LLDB_INVALID_REGNUM, 49, 49},
     nullptr,
     nullptr,
     nullptr,
     0},
    {"p05",
     "",
     4,
     0,
     eEncodingInvalid,
     eFormatInvalid,
     {50, 50, LLDB_INVALID_REGNUM, 50, 50},
     nullptr,
     nullptr,
     nullptr,
     0},
    {"p06",
     "",
     4,
     0,
     eEncodingInvalid,
     eFormatInvalid,
     {51, 51, LLDB_INVALID_REGNUM, 51, 51},
     nullptr,
     nullptr,
     nullptr,
     0},
    {"p07",
     "",
     4,
     0,
     eEncodingInvalid,
     eFormatInvalid,
     {52, 52, LLDB_INVALID_REGNUM, 52, 52},
     nullptr,
     nullptr,
     nullptr,
     0},
    {"p08",
     "",
     4,
     0,
     eEncodingInvalid,
     eFormatInvalid,
     {53, 53, LLDB_INVALID_REGNUM, 53, 53},
     nullptr,
     nullptr,
     nullptr,
     0},
    {"p09",
     "",
     4,
     0,
     eEncodingInvalid,
     eFormatInvalid,
     {54, 54, LLDB_INVALID_REGNUM, 54, 54},
     nullptr,
     nullptr,
     nullptr,
     0},
    {"p10",
     "",
     4,
     0,
     eEncodingInvalid,
     eFormatInvalid,
     {55, 55, LLDB_INVALID_REGNUM, 55, 55},
     nullptr,
     nullptr,
     nullptr,
     0},
    {"p11",
     "",
     4,
     0,
     eEncodingInvalid,
     eFormatInvalid,
     {56, 56, LLDB_INVALID_REGNUM, 56, 56},
     nullptr,
     nullptr,
     nullptr,
     0},
    {"p12",
     "",
     4,
     0,
     eEncodingInvalid,
     eFormatInvalid,
     {57, 57, LLDB_INVALID_REGNUM, 57, 57},
     nullptr,
     nullptr,
     nullptr,
     0},
    {"p13",
     "",
     4,
     0,
     eEncodingInvalid,
     eFormatInvalid,
     {58, 58, LLDB_INVALID_REGNUM, 58, 58},
     nullptr,
     nullptr,
     nullptr,
     0},
    {"p14",
     "",
     4,
     0,
     eEncodingInvalid,
     eFormatInvalid,
     {59, 59, LLDB_INVALID_REGNUM, 59, 59},
     nullptr,
     nullptr,
     nullptr,
     0},
    {"p15",
     "",
     4,
     0,
     eEncodingInvalid,
     eFormatInvalid,
     {60, 60, LLDB_INVALID_REGNUM, 60, 60},
     nullptr,
     nullptr,
     nullptr,
     0},
    {"p16",
     "",
     4,
     0,
     eEncodingInvalid,
     eFormatInvalid,
     {61, 61, LLDB_INVALID_REGNUM, 61, 61},
     nullptr,
     nullptr,
     nullptr,
     0},
    {"p17",
     "",
     4,
     0,
     eEncodingInvalid,
     eFormatInvalid,
     {62, 62, LLDB_INVALID_REGNUM, 62, 62},
     nullptr,
     nullptr,
     nullptr,
     0},
    {"p18",
     "",
     4,
     0,
     eEncodingInvalid,
     eFormatInvalid,
     {63, 63, LLDB_INVALID_REGNUM, 63, 63},
     nullptr,
     nullptr,
     nullptr,
     0},
    // }
    {"sgp0",
     "",
     4,
     0,
     eEncodingUint,
     eFormatAddressInfo,
     {64, 64, LLDB_INVALID_REGNUM, 64, 64},
     nullptr,
     nullptr,
     nullptr,
     0},
    // PADDING {
    {"p19",
     "",
     4,
     0,
     eEncodingInvalid,
     eFormatInvalid,
     {65, 65, LLDB_INVALID_REGNUM, 65, 65},
     nullptr,
     nullptr,
     nullptr,
     0},
    // }
    {"stid",
     "",
     4,
     0,
     eEncodingUint,
     eFormatAddressInfo,
     {66, 66, LLDB_INVALID_REGNUM, 66, 66},
     nullptr,
     nullptr,
     nullptr,
     0},
    {"elr",
     "",
     4,
     0,
     eEncodingUint,
     eFormatAddressInfo,
     {67, 67, LLDB_INVALID_REGNUM, 67, 67},
     nullptr,
     nullptr,
     nullptr,
     0},
    {"badva0",
     "",
     4,
     0,
     eEncodingUint,
     eFormatAddressInfo,
     {68, 68, LLDB_INVALID_REGNUM, 68, 68},
     nullptr,
     nullptr,
     nullptr,
     0},
    {"badva1",
     "",
     4,
     0,
     eEncodingUint,
     eFormatAddressInfo,
     {69, 69, LLDB_INVALID_REGNUM, 69, 69},
     nullptr,
     nullptr,
     nullptr,
     0},
    {"ssr",
     "",
     4,
     0,
     eEncodingUint,
     eFormatAddressInfo,
     {70, 70, LLDB_INVALID_REGNUM, 70, 70},
     nullptr,
     nullptr,
     nullptr,
     0},
    {"ccr",
     "",
     4,
     0,
     eEncodingUint,
     eFormatAddressInfo,
     {71, 71, LLDB_INVALID_REGNUM, 71, 71},
     nullptr,
     nullptr,
     nullptr,
     0},
    {"htid",
     "",
     4,
     0,
     eEncodingUint,
     eFormatAddressInfo,
     {72, 72, LLDB_INVALID_REGNUM, 72, 72},
     nullptr,
     nullptr,
     nullptr,
     0},
    // PADDING {
    {"p20",
     "",
     4,
     0,
     eEncodingInvalid,
     eFormatInvalid,
     {73, 73, LLDB_INVALID_REGNUM, 73, 73},
     nullptr,
     nullptr,
     nullptr,
     0},
    // }
    {"imask",
     "",
     4,
     0,
     eEncodingUint,
     eFormatAddressInfo,
     {74, 74, LLDB_INVALID_REGNUM, 74, 74},
     nullptr,
     nullptr,
     nullptr,
     0},
    // PADDING {
    {"p21",
     "",
     4,
     0,
     eEncodingInvalid,
     eFormatInvalid,
     {75, 75, LLDB_INVALID_REGNUM, 75, 75},
     nullptr,
     nullptr,
     nullptr,
     0},
    {"p22",
     "",
     4,
     0,
     eEncodingInvalid,
     eFormatInvalid,
     {76, 76, LLDB_INVALID_REGNUM, 76, 76},
     nullptr,
     nullptr,
     nullptr,
     0},
    {"p23",
     "",
     4,
     0,
     eEncodingInvalid,
     eFormatInvalid,
     {77, 77, LLDB_INVALID_REGNUM, 77, 77},
     nullptr,
     nullptr,
     nullptr,
     0},
    {"p24",
     "",
     4,
     0,
     eEncodingInvalid,
     eFormatInvalid,
     {78, 78, LLDB_INVALID_REGNUM, 78, 78},
     nullptr,
     nullptr,
     nullptr,
     0},
    {"p25",
     "",
     4,
     0,
     eEncodingInvalid,
     eFormatInvalid,
     {79, 79, LLDB_INVALID_REGNUM, 79, 79},
     nullptr,
     nullptr,
     nullptr,
     0},
    // }
    {"g0",
     "",
     4,
     0,
     eEncodingUint,
     eFormatAddressInfo,
     {80, 80, LLDB_INVALID_REGNUM, 80, 80},
     nullptr,
     nullptr,
     nullptr,
     0},
    {"g1",
     "",
     4,
     0,
     eEncodingUint,
     eFormatAddressInfo,
     {81, 81, LLDB_INVALID_REGNUM, 81, 81},
     nullptr,
     nullptr,
     nullptr,
     0},
    {"g2",
     "",
     4,
     0,
     eEncodingUint,
     eFormatAddressInfo,
     {82, 82, LLDB_INVALID_REGNUM, 82, 82},
     nullptr,
     nullptr,
     nullptr,
     0},
    {"g3",
     "",
     4,
     0,
     eEncodingUint,
     eFormatAddressInfo,
     {83, 83, LLDB_INVALID_REGNUM, 83, 83},
     nullptr,
     nullptr,
     nullptr,
     0}};

static const uint32_t k_num_register_infos =
    sizeof(g_register_infos) / sizeof(RegisterInfo);
static bool g_register_info_names_constified = false;

const lldb_private::RegisterInfo *
ABISysV_hexagon::GetRegisterInfoArray(uint32_t &count) {
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

/*
    http://en.wikipedia.org/wiki/Red_zone_%28computing%29

    In computing, a red zone is a fixed size area in memory beyond the stack
   pointer that has not been
    "allocated". This region of memory is not to be modified by
   interrupt/exception/signal handlers.
    This allows the space to be used for temporary data without the extra
   overhead of modifying the
    stack pointer. The x86-64 ABI mandates a 128 byte red zone.[1] The OpenRISC
   toolchain assumes a
    128 byte red zone though it is not documented.
*/
size_t ABISysV_hexagon::GetRedZoneSize() const { return 0; }

//------------------------------------------------------------------
// Static Functions
//------------------------------------------------------------------

ABISP
ABISysV_hexagon::CreateInstance(lldb::ProcessSP process_sp, const ArchSpec &arch) {
  if (arch.GetTriple().getArch() == llvm::Triple::hexagon) {
    return ABISP(new ABISysV_hexagon(process_sp));
  }
  return ABISP();
}

bool ABISysV_hexagon::PrepareTrivialCall(Thread &thread, lldb::addr_t sp,
                                         lldb::addr_t pc, lldb::addr_t ra,
                                         llvm::ArrayRef<addr_t> args) const {
  // we don't use the traditional trivial call specialized for jit
  return false;
}

/*

// AD:
//  . safeguard the current stack
//  . how can we know that the called function will create its own frame
properly?
//  . we could manually make a new stack first:
//      2. push RA
//      3. push FP
//      4. FP = SP
//      5. SP = SP ( since no locals in our temp frame )

// AD 6/05/2014
//  . variable argument list parameters are not passed via registers, they are
passed on
//    the stack.  This presents us with a problem, since we need to know when
the valist
//    starts.  Currently I can find out if a function is varg, but not how many
//    real parameters it takes.  Thus I don't know when to start spilling the
vargs.  For
//    the time being, to progress, I will assume that it takes on real parameter
before
//    the vargs list starts.

// AD 06/05/2014
//  . how do we adhere to the stack alignment requirements

// AD 06/05/2014
//  . handle 64bit values and their register / stack requirements

*/
#define HEX_ABI_DEBUG 0
bool ABISysV_hexagon::PrepareTrivialCall(
    Thread &thread, lldb::addr_t sp, lldb::addr_t pc, lldb::addr_t ra,
    llvm::Type &prototype, llvm::ArrayRef<ABI::CallArgument> args) const {
  // default number of register passed arguments for varg functions
  const int nVArgRegParams = 1;
  Status error;

  // grab the process so we have access to the memory for spilling
  lldb::ProcessSP proc = thread.GetProcess();

  // get the register context for modifying all of the registers
  RegisterContext *reg_ctx = thread.GetRegisterContext().get();
  if (!reg_ctx)
    return false;

  uint32_t pc_reg = reg_ctx->ConvertRegisterKindToRegisterNumber(
      eRegisterKindGeneric, LLDB_REGNUM_GENERIC_PC);
  if (pc_reg == LLDB_INVALID_REGNUM)
    return false;

  uint32_t ra_reg = reg_ctx->ConvertRegisterKindToRegisterNumber(
      eRegisterKindGeneric, LLDB_REGNUM_GENERIC_RA);
  if (ra_reg == LLDB_INVALID_REGNUM)
    return false;

  uint32_t sp_reg = reg_ctx->ConvertRegisterKindToRegisterNumber(
      eRegisterKindGeneric, LLDB_REGNUM_GENERIC_SP);
  if (sp_reg == LLDB_INVALID_REGNUM)
    return false;

  // push host data onto target
  for (size_t i = 0; i < args.size(); i++) {
    const ABI::CallArgument &arg = args[i];
    // skip over target values
    if (arg.type == ABI::CallArgument::TargetValue)
      continue;
    // round up to 8 byte multiple
    size_t argSize = (arg.size | 0x7) + 1;

    // create space on the stack for this data
    sp -= argSize;

    // write this argument onto the stack of the host process
    proc->WriteMemory(sp, arg.data_ap.get(), arg.size, error);
    if (error.Fail())
      return false;

    // update the argument with the target pointer
    // XXX: This is a gross hack for getting around the const
    *const_cast<lldb::addr_t *>(&arg.value) = sp;
  }

#if HEX_ABI_DEBUG
  // print the original stack pointer
  printf("sp : %04" PRIx64 " \n", sp);
#endif

  // make sure number of parameters matches prototype
  assert(prototype.getFunctionNumParams() == args.size());

  // check if this is a variable argument function
  bool isVArg = prototype.isFunctionVarArg();

  // number of arguments passed by register
  int nRegArgs = nVArgRegParams;
  if (!isVArg) {
    // number of arguments is limited by [R0 : R5] space
    nRegArgs = args.size();
    if (nRegArgs > 6)
      nRegArgs = 6;
  }

  // pass arguments that are passed via registers
  for (int i = 0; i < nRegArgs; i++) {
    // get the parameter as a u32
    uint32_t param = (uint32_t)args[i].value;
    // write argument into register
    if (!reg_ctx->WriteRegisterFromUnsigned(i, param))
      return false;
  }

  // number of arguments to spill onto stack
  int nSpillArgs = args.size() - nRegArgs;
  // make space on the stack for arguments
  sp -= 4 * nSpillArgs;
  // align stack on an 8 byte boundary
  if (sp & 7)
    sp -= 4;

  // arguments that are passed on the stack
  for (size_t i = nRegArgs, offs = 0; i < args.size(); i++) {
    // get the parameter as a u32
    uint32_t param = (uint32_t)args[i].value;
    // write argument to stack
    proc->WriteMemory(sp + offs, (void *)&param, sizeof(param), error);
    if (!error.Success())
      return false;
    //
    offs += 4;
  }

  // update registers with current function call state
  reg_ctx->WriteRegisterFromUnsigned(pc_reg, pc);
  reg_ctx->WriteRegisterFromUnsigned(ra_reg, ra);
  reg_ctx->WriteRegisterFromUnsigned(sp_reg, sp);

#if HEX_ABI_DEBUG
  // quick and dirty stack dumper for debugging
  for (int i = -8; i < 8; i++) {
    uint32_t data = 0;
    lldb::addr_t addr = sp + i * 4;
    proc->ReadMemory(addr, (void *)&data, sizeof(data), error);
    printf("\n0x%04" PRIx64 " 0x%08x ", addr, data);
    if (i == 0)
      printf("<<-- sp");
  }
  printf("\n");
#endif

  return true;
}

bool ABISysV_hexagon::GetArgumentValues(Thread &thread,
                                        ValueList &values) const {
  return false;
}

Status
ABISysV_hexagon::SetReturnValueObject(lldb::StackFrameSP &frame_sp,
                                      lldb::ValueObjectSP &new_value_sp) {
  Status error;
  return error;
}

ValueObjectSP ABISysV_hexagon::GetReturnValueObjectSimple(
    Thread &thread, CompilerType &return_compiler_type) const {
  ValueObjectSP return_valobj_sp;
  return return_valobj_sp;
}

ValueObjectSP ABISysV_hexagon::GetReturnValueObjectImpl(
    Thread &thread, CompilerType &return_compiler_type) const {
  ValueObjectSP return_valobj_sp;
  return return_valobj_sp;
}

// called when we are on the first instruction of a new function for hexagon
// the return address is in RA (R31)
bool ABISysV_hexagon::CreateFunctionEntryUnwindPlan(UnwindPlan &unwind_plan) {
  unwind_plan.Clear();
  unwind_plan.SetRegisterKind(eRegisterKindGeneric);
  unwind_plan.SetReturnAddressRegister(LLDB_REGNUM_GENERIC_RA);

  UnwindPlan::RowSP row(new UnwindPlan::Row);

  // Our Call Frame Address is the stack pointer value
  row->GetCFAValue().SetIsRegisterPlusOffset(LLDB_REGNUM_GENERIC_SP, 4);
  row->SetOffset(0);

  // The previous PC is in the LR
  row->SetRegisterLocationToRegister(LLDB_REGNUM_GENERIC_PC,
                                     LLDB_REGNUM_GENERIC_RA, true);
  unwind_plan.AppendRow(row);

  unwind_plan.SetSourceName("hexagon at-func-entry default");
  unwind_plan.SetSourcedFromCompiler(eLazyBoolNo);
  return true;
}

bool ABISysV_hexagon::CreateDefaultUnwindPlan(UnwindPlan &unwind_plan) {
  unwind_plan.Clear();
  unwind_plan.SetRegisterKind(eRegisterKindGeneric);

  uint32_t fp_reg_num = LLDB_REGNUM_GENERIC_FP;
  uint32_t sp_reg_num = LLDB_REGNUM_GENERIC_SP;
  uint32_t pc_reg_num = LLDB_REGNUM_GENERIC_PC;

  UnwindPlan::RowSP row(new UnwindPlan::Row);

  row->GetCFAValue().SetIsRegisterPlusOffset(LLDB_REGNUM_GENERIC_FP, 8);

  row->SetRegisterLocationToAtCFAPlusOffset(fp_reg_num, -8, true);
  row->SetRegisterLocationToAtCFAPlusOffset(pc_reg_num, -4, true);
  row->SetRegisterLocationToIsCFAPlusOffset(sp_reg_num, 0, true);

  unwind_plan.AppendRow(row);
  unwind_plan.SetSourceName("hexagon default unwind plan");
  unwind_plan.SetSourcedFromCompiler(eLazyBoolNo);
  unwind_plan.SetUnwindPlanValidAtAllInstructions(eLazyBoolNo);
  return true;
}

/*
    Register		Usage					Saved By

    R0  - R5		parameters(a)			-
    R6  - R15		Scratch(b)				Caller
    R16 - R27		Scratch					Callee
    R28				Scratch(b)				Caller
    R29 - R31		Stack Frames			Callee(c)
    P3:0			Processor State			Caller

    a = the caller can change parameter values
    b = R14 - R15 and R28 are used by the procedure linkage table
    c = R29 - R31 are saved and restored by allocframe() and deallocframe()
*/
bool ABISysV_hexagon::RegisterIsVolatile(const RegisterInfo *reg_info) {
  return !RegisterIsCalleeSaved(reg_info);
}

bool ABISysV_hexagon::RegisterIsCalleeSaved(const RegisterInfo *reg_info) {
  int reg = ((reg_info->byte_offset) / 4);

  bool save = (reg >= 16) && (reg <= 27);
  save |= (reg >= 29) && (reg <= 32);

  return save;
}

void ABISysV_hexagon::Initialize() {
  PluginManager::RegisterPlugin(GetPluginNameStatic(),
                                "System V ABI for hexagon targets",
                                CreateInstance);
}

void ABISysV_hexagon::Terminate() {
  PluginManager::UnregisterPlugin(CreateInstance);
}

lldb_private::ConstString ABISysV_hexagon::GetPluginNameStatic() {
  static ConstString g_name("sysv-hexagon");
  return g_name;
}

//------------------------------------------------------------------
// PluginInterface protocol
//------------------------------------------------------------------

lldb_private::ConstString ABISysV_hexagon::GetPluginName() {
  return GetPluginNameStatic();
}

uint32_t ABISysV_hexagon::GetPluginVersion() { return 1; }

// get value object specialized to work with llvm IR types
lldb::ValueObjectSP
ABISysV_hexagon::GetReturnValueObjectImpl(lldb_private::Thread &thread,
                                          llvm::Type &retType) const {
  Value value;
  ValueObjectSP vObjSP;

  // get the current register context
  RegisterContext *reg_ctx = thread.GetRegisterContext().get();
  if (!reg_ctx)
    return vObjSP;

  // for now just pop R0 to find the return value
  const lldb_private::RegisterInfo *r0_info =
      reg_ctx->GetRegisterInfoAtIndex(0);
  if (r0_info == nullptr)
    return vObjSP;

  // void return type
  if (retType.isVoidTy()) {
    value.GetScalar() = 0;
  }
  // integer / pointer return type
  else if (retType.isIntegerTy() || retType.isPointerTy()) {
    // read r0 register value
    lldb_private::RegisterValue r0_value;
    if (!reg_ctx->ReadRegister(r0_info, r0_value))
      return vObjSP;

    // push r0 into value
    uint32_t r0_u32 = r0_value.GetAsUInt32();

    // account for integer size
    if (retType.isIntegerTy() && retType.isSized()) {
      uint64_t size = retType.getScalarSizeInBits();
      uint64_t mask = (1ull << size) - 1;
      // mask out higher order bits then the type we expect
      r0_u32 &= mask;
    }

    value.GetScalar() = r0_u32;
  }
  // unsupported return type
  else
    return vObjSP;

  // pack the value into a ValueObjectSP
  vObjSP = ValueObjectConstResult::Create(thread.GetStackFrameAtIndex(0).get(),
                                          value, ConstString(""));
  return vObjSP;
}
