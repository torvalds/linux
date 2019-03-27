//===-- ABIMacOSX_i386.cpp --------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "ABIMacOSX_i386.h"

#include <vector>

#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/Triple.h"

#include "lldb/Core/Module.h"
#include "lldb/Core/PluginManager.h"
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

using namespace lldb;
using namespace lldb_private;

enum {
  ehframe_eax = 0,
  ehframe_ecx,
  ehframe_edx,
  ehframe_ebx,
  ehframe_ebp, // Different from DWARF the regnums - eh_frame esp/ebp had their
               // regnums switched on i386 darwin
  ehframe_esp, // Different from DWARF the regnums - eh_frame esp/ebp had their
               // regnums switched on i386 darwin
  ehframe_esi,
  ehframe_edi,
  ehframe_eip,
  ehframe_eflags
};

enum {
  dwarf_eax = 0,
  dwarf_ecx,
  dwarf_edx,
  dwarf_ebx,
  dwarf_esp,
  dwarf_ebp,
  dwarf_esi,
  dwarf_edi,
  dwarf_eip,
  dwarf_eflags,
  dwarf_stmm0 = 11,
  dwarf_stmm1,
  dwarf_stmm2,
  dwarf_stmm3,
  dwarf_stmm4,
  dwarf_stmm5,
  dwarf_stmm6,
  dwarf_stmm7,
  dwarf_xmm0 = 21,
  dwarf_xmm1,
  dwarf_xmm2,
  dwarf_xmm3,
  dwarf_xmm4,
  dwarf_xmm5,
  dwarf_xmm6,
  dwarf_xmm7,
  dwarf_ymm0 = dwarf_xmm0,
  dwarf_ymm1 = dwarf_xmm1,
  dwarf_ymm2 = dwarf_xmm2,
  dwarf_ymm3 = dwarf_xmm3,
  dwarf_ymm4 = dwarf_xmm4,
  dwarf_ymm5 = dwarf_xmm5,
  dwarf_ymm6 = dwarf_xmm6,
  dwarf_ymm7 = dwarf_xmm7
};

static RegisterInfo g_register_infos[] = {
    //  NAME      ALT      SZ OFF ENCODING         FORMAT
    //  EH_FRAME              DWARF                 GENERIC
    //  PROCESS PLUGIN        LLDB NATIVE
    //  ======    =======  == === =============    ============
    //  ===================== ===================== ============================
    //  ====================  ======================
    {"eax",
     nullptr,
     4,
     0,
     eEncodingUint,
     eFormatHex,
     {ehframe_eax, dwarf_eax, LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM,
      LLDB_INVALID_REGNUM},
     nullptr,
     nullptr,
     nullptr,
     0},
    {"ebx",
     nullptr,
     4,
     0,
     eEncodingUint,
     eFormatHex,
     {ehframe_ebx, dwarf_ebx, LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM,
      LLDB_INVALID_REGNUM},
     nullptr,
     nullptr,
     nullptr,
     0},
    {"ecx",
     nullptr,
     4,
     0,
     eEncodingUint,
     eFormatHex,
     {ehframe_ecx, dwarf_ecx, LLDB_REGNUM_GENERIC_ARG4, LLDB_INVALID_REGNUM,
      LLDB_INVALID_REGNUM},
     nullptr,
     nullptr,
     nullptr,
     0},
    {"edx",
     nullptr,
     4,
     0,
     eEncodingUint,
     eFormatHex,
     {ehframe_edx, dwarf_edx, LLDB_REGNUM_GENERIC_ARG3, LLDB_INVALID_REGNUM,
      LLDB_INVALID_REGNUM},
     nullptr,
     nullptr,
     nullptr,
     0},
    {"esi",
     nullptr,
     4,
     0,
     eEncodingUint,
     eFormatHex,
     {ehframe_esi, dwarf_esi, LLDB_REGNUM_GENERIC_ARG2, LLDB_INVALID_REGNUM,
      LLDB_INVALID_REGNUM},
     nullptr,
     nullptr,
     nullptr,
     0},
    {"edi",
     nullptr,
     4,
     0,
     eEncodingUint,
     eFormatHex,
     {ehframe_edi, dwarf_edi, LLDB_REGNUM_GENERIC_ARG1, LLDB_INVALID_REGNUM,
      LLDB_INVALID_REGNUM},
     nullptr,
     nullptr,
     nullptr,
     0},
    {"ebp",
     "fp",
     4,
     0,
     eEncodingUint,
     eFormatHex,
     {ehframe_ebp, dwarf_ebp, LLDB_REGNUM_GENERIC_FP, LLDB_INVALID_REGNUM,
      LLDB_INVALID_REGNUM},
     nullptr,
     nullptr,
     nullptr,
     0},
    {"esp",
     "sp",
     4,
     0,
     eEncodingUint,
     eFormatHex,
     {ehframe_esp, dwarf_esp, LLDB_REGNUM_GENERIC_SP, LLDB_INVALID_REGNUM,
      LLDB_INVALID_REGNUM},
     nullptr,
     nullptr,
     nullptr,
     0},
    {"eip",
     "pc",
     4,
     0,
     eEncodingUint,
     eFormatHex,
     {ehframe_eip, dwarf_eip, LLDB_REGNUM_GENERIC_PC, LLDB_INVALID_REGNUM,
      LLDB_INVALID_REGNUM},
     nullptr,
     nullptr,
     nullptr,
     0},
    {"eflags",
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
     {LLDB_INVALID_REGNUM, dwarf_stmm0, LLDB_INVALID_REGNUM,
      LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM},
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
     {LLDB_INVALID_REGNUM, dwarf_stmm1, LLDB_INVALID_REGNUM,
      LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM},
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
     {LLDB_INVALID_REGNUM, dwarf_stmm2, LLDB_INVALID_REGNUM,
      LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM},
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
     {LLDB_INVALID_REGNUM, dwarf_stmm3, LLDB_INVALID_REGNUM,
      LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM},
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
     {LLDB_INVALID_REGNUM, dwarf_stmm4, LLDB_INVALID_REGNUM,
      LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM},
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
     {LLDB_INVALID_REGNUM, dwarf_stmm5, LLDB_INVALID_REGNUM,
      LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM},
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
     {LLDB_INVALID_REGNUM, dwarf_stmm6, LLDB_INVALID_REGNUM,
      LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM},
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
     {LLDB_INVALID_REGNUM, dwarf_stmm7, LLDB_INVALID_REGNUM,
      LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM},
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
     {LLDB_INVALID_REGNUM, dwarf_xmm0, LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM,
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
     {LLDB_INVALID_REGNUM, dwarf_xmm1, LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM,
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
     {LLDB_INVALID_REGNUM, dwarf_xmm2, LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM,
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
     {LLDB_INVALID_REGNUM, dwarf_xmm3, LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM,
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
     {LLDB_INVALID_REGNUM, dwarf_xmm4, LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM,
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
     {LLDB_INVALID_REGNUM, dwarf_xmm5, LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM,
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
     {LLDB_INVALID_REGNUM, dwarf_xmm6, LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM,
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
     {LLDB_INVALID_REGNUM, dwarf_xmm7, LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM,
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
     {LLDB_INVALID_REGNUM, dwarf_ymm0, LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM,
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
     {LLDB_INVALID_REGNUM, dwarf_ymm1, LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM,
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
     {LLDB_INVALID_REGNUM, dwarf_ymm2, LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM,
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
     {LLDB_INVALID_REGNUM, dwarf_ymm3, LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM,
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
     {LLDB_INVALID_REGNUM, dwarf_ymm4, LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM,
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
     {LLDB_INVALID_REGNUM, dwarf_ymm5, LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM,
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
     {LLDB_INVALID_REGNUM, dwarf_ymm6, LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM,
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
     {LLDB_INVALID_REGNUM, dwarf_ymm7, LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM,
      LLDB_INVALID_REGNUM},
     nullptr,
     nullptr,
     nullptr,
     0}};

static const uint32_t k_num_register_infos =
    llvm::array_lengthof(g_register_infos);
static bool g_register_info_names_constified = false;

const lldb_private::RegisterInfo *
ABIMacOSX_i386::GetRegisterInfoArray(uint32_t &count) {
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

size_t ABIMacOSX_i386::GetRedZoneSize() const { return 0; }

//------------------------------------------------------------------
// Static Functions
//------------------------------------------------------------------

ABISP
ABIMacOSX_i386::CreateInstance(lldb::ProcessSP process_sp, const ArchSpec &arch) {
  if ((arch.GetTriple().getArch() == llvm::Triple::x86) &&
      (arch.GetTriple().isMacOSX() || arch.GetTriple().isiOS() ||
       arch.GetTriple().isWatchOS())) {
    return ABISP(new ABIMacOSX_i386(process_sp));
  }
  return ABISP();
}

bool ABIMacOSX_i386::PrepareTrivialCall(Thread &thread, addr_t sp,
                                        addr_t func_addr, addr_t return_addr,
                                        llvm::ArrayRef<addr_t> args) const {
  RegisterContext *reg_ctx = thread.GetRegisterContext().get();
  if (!reg_ctx)
    return false;
  uint32_t pc_reg_num = reg_ctx->ConvertRegisterKindToRegisterNumber(
      eRegisterKindGeneric, LLDB_REGNUM_GENERIC_PC);
  uint32_t sp_reg_num = reg_ctx->ConvertRegisterKindToRegisterNumber(
      eRegisterKindGeneric, LLDB_REGNUM_GENERIC_SP);

  // When writing a register value down to memory, the register info used to
  // write memory just needs to have the correct size of a 32 bit register, the
  // actual register it pertains to is not important, just the size needs to be
  // correct. Here we use "eax"...
  const RegisterInfo *reg_info_32 = reg_ctx->GetRegisterInfoByName("eax");
  if (!reg_info_32)
    return false; // TODO this should actually never happen

  // Make room for the argument(s) on the stack

  Status error;
  RegisterValue reg_value;

  // Write any arguments onto the stack
  sp -= 4 * args.size();

  // Align the SP
  sp &= ~(16ull - 1ull); // 16-byte alignment

  addr_t arg_pos = sp;

  for (addr_t arg : args) {
    reg_value.SetUInt32(arg);
    error = reg_ctx->WriteRegisterValueToMemory(
        reg_info_32, arg_pos, reg_info_32->byte_size, reg_value);
    if (error.Fail())
      return false;
    arg_pos += 4;
  }

  // The return address is pushed onto the stack (yes after we just set the
  // alignment above!).
  sp -= 4;
  reg_value.SetUInt32(return_addr);
  error = reg_ctx->WriteRegisterValueToMemory(
      reg_info_32, sp, reg_info_32->byte_size, reg_value);
  if (error.Fail())
    return false;

  // %esp is set to the actual stack value.

  if (!reg_ctx->WriteRegisterFromUnsigned(sp_reg_num, sp))
    return false;

  // %eip is set to the address of the called function.

  if (!reg_ctx->WriteRegisterFromUnsigned(pc_reg_num, func_addr))
    return false;

  return true;
}

static bool ReadIntegerArgument(Scalar &scalar, unsigned int bit_width,
                                bool is_signed, Process *process,
                                addr_t &current_stack_argument) {

  uint32_t byte_size = (bit_width + (8 - 1)) / 8;
  Status error;
  if (process->ReadScalarIntegerFromMemory(current_stack_argument, byte_size,
                                           is_signed, scalar, error)) {
    current_stack_argument += byte_size;
    return true;
  }
  return false;
}

bool ABIMacOSX_i386::GetArgumentValues(Thread &thread,
                                       ValueList &values) const {
  unsigned int num_values = values.GetSize();
  unsigned int value_index;

  // Get the pointer to the first stack argument so we have a place to start
  // when reading data

  RegisterContext *reg_ctx = thread.GetRegisterContext().get();

  if (!reg_ctx)
    return false;

  addr_t sp = reg_ctx->GetSP(0);

  if (!sp)
    return false;

  addr_t current_stack_argument = sp + 4; // jump over return address

  for (value_index = 0; value_index < num_values; ++value_index) {
    Value *value = values.GetValueAtIndex(value_index);

    if (!value)
      return false;

    // We currently only support extracting values with Clang QualTypes. Do we
    // care about others?
    CompilerType compiler_type(value->GetCompilerType());
    llvm::Optional<uint64_t> bit_size = compiler_type.GetBitSize(&thread);
    if (bit_size) {
      bool is_signed;
      if (compiler_type.IsIntegerOrEnumerationType(is_signed))
        ReadIntegerArgument(value->GetScalar(), *bit_size, is_signed,
                            thread.GetProcess().get(), current_stack_argument);
      else if (compiler_type.IsPointerType())
        ReadIntegerArgument(value->GetScalar(), *bit_size, false,
                            thread.GetProcess().get(), current_stack_argument);
    }
  }

  return true;
}

Status ABIMacOSX_i386::SetReturnValueObject(lldb::StackFrameSP &frame_sp,
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
      const RegisterInfo *eax_info = reg_ctx->GetRegisterInfoByName("eax", 0);
      if (num_bytes <= 4) {
        uint32_t raw_value = data.GetMaxU32(&offset, num_bytes);

        if (reg_ctx->WriteRegisterFromUnsigned(eax_info, raw_value))
          set_it_simple = true;
      } else {
        uint32_t raw_value = data.GetMaxU32(&offset, 4);

        if (reg_ctx->WriteRegisterFromUnsigned(eax_info, raw_value)) {
          const RegisterInfo *edx_info =
              reg_ctx->GetRegisterInfoByName("edx", 0);
          uint32_t raw_value = data.GetMaxU32(&offset, num_bytes - offset);

          if (reg_ctx->WriteRegisterFromUnsigned(edx_info, raw_value))
            set_it_simple = true;
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

ValueObjectSP
ABIMacOSX_i386::GetReturnValueObjectImpl(Thread &thread,
                                         CompilerType &compiler_type) const {
  Value value;
  ValueObjectSP return_valobj_sp;

  if (!compiler_type)
    return return_valobj_sp;

  // value.SetContext (Value::eContextTypeClangType,
  // compiler_type.GetOpaqueQualType());
  value.SetCompilerType(compiler_type);

  RegisterContext *reg_ctx = thread.GetRegisterContext().get();
  if (!reg_ctx)
    return return_valobj_sp;

  bool is_signed;

  if (compiler_type.IsIntegerOrEnumerationType(is_signed)) {
    llvm::Optional<uint64_t> bit_width = compiler_type.GetBitSize(&thread);
    if (!bit_width)
      return return_valobj_sp;
    unsigned eax_id =
        reg_ctx->GetRegisterInfoByName("eax", 0)->kinds[eRegisterKindLLDB];
    unsigned edx_id =
        reg_ctx->GetRegisterInfoByName("edx", 0)->kinds[eRegisterKindLLDB];

    switch (*bit_width) {
    default:
    case 128:
      // Scalar can't hold 128-bit literals, so we don't handle this
      return return_valobj_sp;
    case 64:
      uint64_t raw_value;
      raw_value =
          thread.GetRegisterContext()->ReadRegisterAsUnsigned(eax_id, 0) &
          0xffffffff;
      raw_value |=
          (thread.GetRegisterContext()->ReadRegisterAsUnsigned(edx_id, 0) &
           0xffffffff)
          << 32;
      if (is_signed)
        value.GetScalar() = (int64_t)raw_value;
      else
        value.GetScalar() = (uint64_t)raw_value;
      break;
    case 32:
      if (is_signed)
        value.GetScalar() = (int32_t)(
            thread.GetRegisterContext()->ReadRegisterAsUnsigned(eax_id, 0) &
            0xffffffff);
      else
        value.GetScalar() = (uint32_t)(
            thread.GetRegisterContext()->ReadRegisterAsUnsigned(eax_id, 0) &
            0xffffffff);
      break;
    case 16:
      if (is_signed)
        value.GetScalar() = (int16_t)(
            thread.GetRegisterContext()->ReadRegisterAsUnsigned(eax_id, 0) &
            0xffff);
      else
        value.GetScalar() = (uint16_t)(
            thread.GetRegisterContext()->ReadRegisterAsUnsigned(eax_id, 0) &
            0xffff);
      break;
    case 8:
      if (is_signed)
        value.GetScalar() = (int8_t)(
            thread.GetRegisterContext()->ReadRegisterAsUnsigned(eax_id, 0) &
            0xff);
      else
        value.GetScalar() = (uint8_t)(
            thread.GetRegisterContext()->ReadRegisterAsUnsigned(eax_id, 0) &
            0xff);
      break;
    }
  } else if (compiler_type.IsPointerType()) {
    unsigned eax_id =
        reg_ctx->GetRegisterInfoByName("eax", 0)->kinds[eRegisterKindLLDB];
    uint32_t ptr =
        thread.GetRegisterContext()->ReadRegisterAsUnsigned(eax_id, 0) &
        0xffffffff;
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

// This defines the CFA as esp+4
// the saved pc is at CFA-4 (i.e. esp+0)
// The saved esp is CFA+0

bool ABIMacOSX_i386::CreateFunctionEntryUnwindPlan(UnwindPlan &unwind_plan) {
  unwind_plan.Clear();
  unwind_plan.SetRegisterKind(eRegisterKindDWARF);

  uint32_t sp_reg_num = dwarf_esp;
  uint32_t pc_reg_num = dwarf_eip;

  UnwindPlan::RowSP row(new UnwindPlan::Row);
  row->GetCFAValue().SetIsRegisterPlusOffset(sp_reg_num, 4);
  row->SetRegisterLocationToAtCFAPlusOffset(pc_reg_num, -4, false);
  row->SetRegisterLocationToIsCFAPlusOffset(sp_reg_num, 0, true);
  unwind_plan.AppendRow(row);
  unwind_plan.SetSourceName("i386 at-func-entry default");
  unwind_plan.SetSourcedFromCompiler(eLazyBoolNo);
  return true;
}

// This defines the CFA as ebp+8
// The saved pc is at CFA-4 (i.e. ebp+4)
// The saved ebp is at CFA-8 (i.e. ebp+0)
// The saved esp is CFA+0

bool ABIMacOSX_i386::CreateDefaultUnwindPlan(UnwindPlan &unwind_plan) {
  unwind_plan.Clear();
  unwind_plan.SetRegisterKind(eRegisterKindDWARF);

  uint32_t fp_reg_num = dwarf_ebp;
  uint32_t sp_reg_num = dwarf_esp;
  uint32_t pc_reg_num = dwarf_eip;

  UnwindPlan::RowSP row(new UnwindPlan::Row);
  const int32_t ptr_size = 4;

  row->GetCFAValue().SetIsRegisterPlusOffset(fp_reg_num, 2 * ptr_size);
  row->SetOffset(0);

  row->SetRegisterLocationToAtCFAPlusOffset(fp_reg_num, ptr_size * -2, true);
  row->SetRegisterLocationToAtCFAPlusOffset(pc_reg_num, ptr_size * -1, true);
  row->SetRegisterLocationToIsCFAPlusOffset(sp_reg_num, 0, true);

  unwind_plan.AppendRow(row);
  unwind_plan.SetSourceName("i386 default unwind plan");
  unwind_plan.SetSourcedFromCompiler(eLazyBoolNo);
  unwind_plan.SetUnwindPlanValidAtAllInstructions(eLazyBoolNo);
  return true;
}

bool ABIMacOSX_i386::RegisterIsVolatile(const RegisterInfo *reg_info) {
  return !RegisterIsCalleeSaved(reg_info);
}

// v.
// http://developer.apple.com/library/mac/#documentation/developertools/Conceptual/LowLevelABI/130
// -IA-
// 32_Function_Calling_Conventions/IA32.html#//apple_ref/doc/uid/TP40002492-SW4
//
// This document ("OS X ABI Function Call Guide", chapter "IA-32 Function
// Calling Conventions") says that the following registers on i386 are
// preserved aka non-volatile aka callee-saved:
//
// ebx, ebp, esi, edi, esp

bool ABIMacOSX_i386::RegisterIsCalleeSaved(const RegisterInfo *reg_info) {
  if (reg_info) {
    // Saved registers are ebx, ebp, esi, edi, esp, eip
    const char *name = reg_info->name;
    if (name[0] == 'e') {
      switch (name[1]) {
      case 'b':
        if (name[2] == 'x' || name[2] == 'p')
          return name[3] == '\0';
        break;
      case 'd':
        if (name[2] == 'i')
          return name[3] == '\0';
        break;
      case 'i':
        if (name[2] == 'p')
          return name[3] == '\0';
        break;
      case 's':
        if (name[2] == 'i' || name[2] == 'p')
          return name[3] == '\0';
        break;
      }
    }
    if (name[0] == 's' && name[1] == 'p' && name[2] == '\0') // sp
      return true;
    if (name[0] == 'f' && name[1] == 'p' && name[2] == '\0') // fp
      return true;
    if (name[0] == 'p' && name[1] == 'c' && name[2] == '\0') // pc
      return true;
  }
  return false;
}

void ABIMacOSX_i386::Initialize() {
  PluginManager::RegisterPlugin(
      GetPluginNameStatic(), "Mac OS X ABI for i386 targets", CreateInstance);
}

void ABIMacOSX_i386::Terminate() {
  PluginManager::UnregisterPlugin(CreateInstance);
}

lldb_private::ConstString ABIMacOSX_i386::GetPluginNameStatic() {
  static ConstString g_short_name("abi.macosx-i386");
  return g_short_name;
}

//------------------------------------------------------------------
// PluginInterface protocol
//------------------------------------------------------------------

lldb_private::ConstString ABIMacOSX_i386::GetPluginName() {
  return GetPluginNameStatic();
}

uint32_t ABIMacOSX_i386::GetPluginVersion() { return 1; }
