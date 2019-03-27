//===-- ABISysV_mips.cpp ----------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "ABISysV_mips.h"

#include "llvm/ADT/STLExtras.h"
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
  dwarf_r0 = 0,
  dwarf_r1,
  dwarf_r2,
  dwarf_r3,
  dwarf_r4,
  dwarf_r5,
  dwarf_r6,
  dwarf_r7,
  dwarf_r8,
  dwarf_r9,
  dwarf_r10,
  dwarf_r11,
  dwarf_r12,
  dwarf_r13,
  dwarf_r14,
  dwarf_r15,
  dwarf_r16,
  dwarf_r17,
  dwarf_r18,
  dwarf_r19,
  dwarf_r20,
  dwarf_r21,
  dwarf_r22,
  dwarf_r23,
  dwarf_r24,
  dwarf_r25,
  dwarf_r26,
  dwarf_r27,
  dwarf_r28,
  dwarf_r29,
  dwarf_r30,
  dwarf_r31,
  dwarf_sr,
  dwarf_lo,
  dwarf_hi,
  dwarf_bad,
  dwarf_cause,
  dwarf_pc
};

static const RegisterInfo g_register_infos[] = {
    //  NAME      ALT    SZ OFF ENCODING        FORMAT         EH_FRAME
    //  DWARF                   GENERIC                     PROCESS PLUGINS
    //  LLDB NATIVE            VALUE REGS  INVALIDATE REGS
    //  ========  ======  == === =============  ===========    ============
    //  ==============          ============                =================
    //  ===================     ========== =================
    {"r0",
     "zero",
     4,
     0,
     eEncodingUint,
     eFormatHex,
     {dwarf_r0, dwarf_r0, LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM,
      LLDB_INVALID_REGNUM},
     nullptr,
     nullptr,
     nullptr,
     0},
    {"r1",
     "AT",
     4,
     0,
     eEncodingUint,
     eFormatHex,
     {dwarf_r1, dwarf_r1, LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM,
      LLDB_INVALID_REGNUM},
     nullptr,
     nullptr,
     nullptr,
     0},
    {"r2",
     "v0",
     4,
     0,
     eEncodingUint,
     eFormatHex,
     {dwarf_r2, dwarf_r2, LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM,
      LLDB_INVALID_REGNUM},
     nullptr,
     nullptr,
     nullptr,
     0},
    {"r3",
     "v1",
     4,
     0,
     eEncodingUint,
     eFormatHex,
     {dwarf_r3, dwarf_r3, LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM,
      LLDB_INVALID_REGNUM},
     nullptr,
     nullptr,
     nullptr,
     0},
    {"r4",
     "arg1",
     4,
     0,
     eEncodingUint,
     eFormatHex,
     {dwarf_r4, dwarf_r4, LLDB_REGNUM_GENERIC_ARG1, LLDB_INVALID_REGNUM,
      LLDB_INVALID_REGNUM},
     nullptr,
     nullptr,
     nullptr,
     0},
    {"r5",
     "arg2",
     4,
     0,
     eEncodingUint,
     eFormatHex,
     {dwarf_r5, dwarf_r5, LLDB_REGNUM_GENERIC_ARG2, LLDB_INVALID_REGNUM,
      LLDB_INVALID_REGNUM},
     nullptr,
     nullptr,
     nullptr,
     0},
    {"r6",
     "arg3",
     4,
     0,
     eEncodingUint,
     eFormatHex,
     {dwarf_r6, dwarf_r6, LLDB_REGNUM_GENERIC_ARG3, LLDB_INVALID_REGNUM,
      LLDB_INVALID_REGNUM},
     nullptr,
     nullptr,
     nullptr,
     0},
    {"r7",
     "arg4",
     4,
     0,
     eEncodingUint,
     eFormatHex,
     {dwarf_r7, dwarf_r7, LLDB_REGNUM_GENERIC_ARG4, LLDB_INVALID_REGNUM,
      LLDB_INVALID_REGNUM},
     nullptr,
     nullptr,
     nullptr,
     0},
    {"r8",
     "arg5",
     4,
     0,
     eEncodingUint,
     eFormatHex,
     {dwarf_r8, dwarf_r8, LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM,
      LLDB_INVALID_REGNUM},
     nullptr,
     nullptr,
     nullptr,
     0},
    {"r9",
     "arg6",
     4,
     0,
     eEncodingUint,
     eFormatHex,
     {dwarf_r9, dwarf_r9, LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM,
      LLDB_INVALID_REGNUM},
     nullptr,
     nullptr,
     nullptr,
     0},
    {"r10",
     "arg7",
     4,
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
     "arg8",
     4,
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
     4,
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
     4,
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
     4,
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
     4,
     0,
     eEncodingUint,
     eFormatHex,
     {dwarf_r15, dwarf_r15, LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM,
      LLDB_INVALID_REGNUM},
     nullptr,
     nullptr,
     nullptr,
     0},
    {"r16",
     nullptr,
     4,
     0,
     eEncodingUint,
     eFormatHex,
     {dwarf_r16, dwarf_r16, LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM,
      LLDB_INVALID_REGNUM},
     nullptr,
     nullptr,
     nullptr,
     0},
    {"r17",
     nullptr,
     4,
     0,
     eEncodingUint,
     eFormatHex,
     {dwarf_r17, dwarf_r17, LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM,
      LLDB_INVALID_REGNUM},
     nullptr,
     nullptr,
     nullptr,
     0},
    {"r18",
     nullptr,
     4,
     0,
     eEncodingUint,
     eFormatHex,
     {dwarf_r18, dwarf_r18, LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM,
      LLDB_INVALID_REGNUM},
     nullptr,
     nullptr,
     nullptr,
     0},
    {"r19",
     nullptr,
     4,
     0,
     eEncodingUint,
     eFormatHex,
     {dwarf_r19, dwarf_r19, LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM,
      LLDB_INVALID_REGNUM},
     nullptr,
     nullptr,
     nullptr,
     0},
    {"r20",
     nullptr,
     4,
     0,
     eEncodingUint,
     eFormatHex,
     {dwarf_r20, dwarf_r20, LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM,
      LLDB_INVALID_REGNUM},
     nullptr,
     nullptr,
     nullptr,
     0},
    {"r21",
     nullptr,
     4,
     0,
     eEncodingUint,
     eFormatHex,
     {dwarf_r21, dwarf_r21, LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM,
      LLDB_INVALID_REGNUM},
     nullptr,
     nullptr,
     nullptr,
     0},
    {"r22",
     nullptr,
     4,
     0,
     eEncodingUint,
     eFormatHex,
     {dwarf_r22, dwarf_r22, LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM,
      LLDB_INVALID_REGNUM},
     nullptr,
     nullptr,
     nullptr,
     0},
    {"r23",
     nullptr,
     4,
     0,
     eEncodingUint,
     eFormatHex,
     {dwarf_r23, dwarf_r23, LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM,
      LLDB_INVALID_REGNUM},
     nullptr,
     nullptr,
     nullptr,
     0},
    {"r24",
     nullptr,
     4,
     0,
     eEncodingUint,
     eFormatHex,
     {dwarf_r24, dwarf_r24, LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM,
      LLDB_INVALID_REGNUM},
     nullptr,
     nullptr,
     nullptr,
     0},
    {"r25",
     nullptr,
     4,
     0,
     eEncodingUint,
     eFormatHex,
     {dwarf_r25, dwarf_r25, LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM,
      LLDB_INVALID_REGNUM},
     nullptr,
     nullptr,
     nullptr,
     0},
    {"r26",
     nullptr,
     4,
     0,
     eEncodingUint,
     eFormatHex,
     {dwarf_r26, dwarf_r26, LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM,
      LLDB_INVALID_REGNUM},
     nullptr,
     nullptr,
     nullptr,
     0},
    {"r27",
     nullptr,
     4,
     0,
     eEncodingUint,
     eFormatHex,
     {dwarf_r27, dwarf_r27, LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM,
      LLDB_INVALID_REGNUM},
     nullptr,
     nullptr,
     nullptr,
     0},
    {"r28",
     "gp",
     4,
     0,
     eEncodingUint,
     eFormatHex,
     {dwarf_r28, dwarf_r28, LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM,
      LLDB_INVALID_REGNUM},
     nullptr,
     nullptr,
     nullptr,
     0},
    {"r29",
     "sp",
     4,
     0,
     eEncodingUint,
     eFormatHex,
     {dwarf_r29, dwarf_r29, LLDB_REGNUM_GENERIC_SP, LLDB_INVALID_REGNUM,
      LLDB_INVALID_REGNUM},
     nullptr,
     nullptr,
     nullptr,
     0},
    {"r30",
     "fp",
     4,
     0,
     eEncodingUint,
     eFormatHex,
     {dwarf_r30, dwarf_r30, LLDB_REGNUM_GENERIC_FP, LLDB_INVALID_REGNUM,
      LLDB_INVALID_REGNUM},
     nullptr,
     nullptr,
     nullptr,
     0},
    {"r31",
     "ra",
     4,
     0,
     eEncodingUint,
     eFormatHex,
     {dwarf_r31, dwarf_r31, LLDB_REGNUM_GENERIC_RA, LLDB_INVALID_REGNUM,
      LLDB_INVALID_REGNUM},
     nullptr,
     nullptr,
     nullptr,
     0},
    {"sr",
     nullptr,
     4,
     0,
     eEncodingUint,
     eFormatHex,
     {dwarf_sr, dwarf_sr, LLDB_REGNUM_GENERIC_FLAGS, LLDB_INVALID_REGNUM,
      LLDB_INVALID_REGNUM},
     nullptr,
     nullptr,
     nullptr,
     0},
    {"lo",
     nullptr,
     4,
     0,
     eEncodingUint,
     eFormatHex,
     {dwarf_lo, dwarf_lo, LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM,
      LLDB_INVALID_REGNUM},
     nullptr,
     nullptr,
     nullptr,
     0},
    {"hi",
     nullptr,
     4,
     0,
     eEncodingUint,
     eFormatHex,
     {dwarf_hi, dwarf_hi, LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM,
      LLDB_INVALID_REGNUM},
     nullptr,
     nullptr,
     nullptr,
     0},
    {"bad",
     nullptr,
     4,
     0,
     eEncodingUint,
     eFormatHex,
     {dwarf_bad, dwarf_bad, LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM,
      LLDB_INVALID_REGNUM},
     nullptr,
     nullptr,
     nullptr,
     0},
    {"cause",
     nullptr,
     4,
     0,
     eEncodingUint,
     eFormatHex,
     {dwarf_cause, dwarf_cause, LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM,
      LLDB_INVALID_REGNUM},
     nullptr,
     nullptr,
     nullptr,
     0},
    {"pc",
     nullptr,
     4,
     0,
     eEncodingUint,
     eFormatHex,
     {dwarf_pc, dwarf_pc, LLDB_REGNUM_GENERIC_PC, LLDB_INVALID_REGNUM,
      LLDB_INVALID_REGNUM},
     nullptr,
     nullptr,
     nullptr,
     0},
};

static const uint32_t k_num_register_infos =
    llvm::array_lengthof(g_register_infos);

const lldb_private::RegisterInfo *
ABISysV_mips::GetRegisterInfoArray(uint32_t &count) {
  count = k_num_register_infos;
  return g_register_infos;
}

size_t ABISysV_mips::GetRedZoneSize() const { return 0; }

//------------------------------------------------------------------
// Static Functions
//------------------------------------------------------------------

ABISP
ABISysV_mips::CreateInstance(lldb::ProcessSP process_sp, const ArchSpec &arch) {
  const llvm::Triple::ArchType arch_type = arch.GetTriple().getArch();
  if ((arch_type == llvm::Triple::mips) ||
      (arch_type == llvm::Triple::mipsel)) {
    return ABISP(new ABISysV_mips(process_sp));
  }
  return ABISP();
}

bool ABISysV_mips::PrepareTrivialCall(Thread &thread, addr_t sp,
                                      addr_t func_addr, addr_t return_addr,
                                      llvm::ArrayRef<addr_t> args) const {
  Log *log(lldb_private::GetLogIfAllCategoriesSet(LIBLLDB_LOG_EXPRESSIONS));

  if (log) {
    StreamString s;
    s.Printf("ABISysV_mips::PrepareTrivialCall (tid = 0x%" PRIx64
             ", sp = 0x%" PRIx64 ", func_addr = 0x%" PRIx64
             ", return_addr = 0x%" PRIx64,
             thread.GetID(), (uint64_t)sp, (uint64_t)func_addr,
             (uint64_t)return_addr);

    for (size_t i = 0; i < args.size(); ++i)
      s.Printf(", arg%zd = 0x%" PRIx64, i + 1, args[i]);
    s.PutCString(")");
    log->PutString(s.GetString());
  }

  RegisterContext *reg_ctx = thread.GetRegisterContext().get();
  if (!reg_ctx)
    return false;

  const RegisterInfo *reg_info = nullptr;

  RegisterValue reg_value;

  // Argument registers
  const char *reg_names[] = {"r4", "r5", "r6", "r7"};

  llvm::ArrayRef<addr_t>::iterator ai = args.begin(), ae = args.end();

  // Write arguments to registers
  for (size_t i = 0; i < llvm::array_lengthof(reg_names); ++i) {
    if (ai == ae)
      break;

    reg_info = reg_ctx->GetRegisterInfo(eRegisterKindGeneric,
                                        LLDB_REGNUM_GENERIC_ARG1 + i);
    if (log)
      log->Printf("About to write arg%zd (0x%" PRIx64 ") into %s", i + 1,
                  args[i], reg_info->name);

    if (!reg_ctx->WriteRegisterFromUnsigned(reg_info, args[i]))
      return false;

    ++ai;
  }

  // If we have more than 4 arguments --Spill onto the stack
  if (ai != ae) {
    // No of arguments to go on stack
    size_t num_stack_regs = args.size();

    // Allocate needed space for args on the stack
    sp -= (num_stack_regs * 4);

    // Keep the stack 8 byte aligned
    sp &= ~(8ull - 1ull);

    // just using arg1 to get the right size
    const RegisterInfo *reg_info = reg_ctx->GetRegisterInfo(
        eRegisterKindGeneric, LLDB_REGNUM_GENERIC_ARG1);

    addr_t arg_pos = sp + 16;

    size_t i = 4;
    for (; ai != ae; ++ai) {
      reg_value.SetUInt32(*ai);
      if (log)
        log->Printf("About to write arg%zd (0x%" PRIx64 ") at  0x%" PRIx64 "",
                    i + 1, args[i], arg_pos);

      if (reg_ctx
              ->WriteRegisterValueToMemory(reg_info, arg_pos,
                                           reg_info->byte_size, reg_value)
              .Fail())
        return false;
      arg_pos += reg_info->byte_size;
      i++;
    }
  }

  Status error;
  const RegisterInfo *pc_reg_info =
      reg_ctx->GetRegisterInfo(eRegisterKindGeneric, LLDB_REGNUM_GENERIC_PC);
  const RegisterInfo *sp_reg_info =
      reg_ctx->GetRegisterInfo(eRegisterKindGeneric, LLDB_REGNUM_GENERIC_SP);
  const RegisterInfo *ra_reg_info =
      reg_ctx->GetRegisterInfo(eRegisterKindGeneric, LLDB_REGNUM_GENERIC_RA);
  const RegisterInfo *r25_info = reg_ctx->GetRegisterInfoByName("r25", 0);
  const RegisterInfo *r0_info = reg_ctx->GetRegisterInfoByName("zero", 0);

  if (log)
    log->Printf("Writing R0: 0x%" PRIx64, (uint64_t)0);

  /* Write r0 with 0, in case we are stopped in syscall,
   * such setting prevents automatic decrement of the PC.
   * This clears the bug 23659 for MIPS.
  */
  if (!reg_ctx->WriteRegisterFromUnsigned(r0_info, (uint64_t)0))
    return false;

  if (log)
    log->Printf("Writing SP: 0x%" PRIx64, (uint64_t)sp);

  // Set "sp" to the requested value
  if (!reg_ctx->WriteRegisterFromUnsigned(sp_reg_info, sp))
    return false;

  if (log)
    log->Printf("Writing RA: 0x%" PRIx64, (uint64_t)return_addr);

  // Set "ra" to the return address
  if (!reg_ctx->WriteRegisterFromUnsigned(ra_reg_info, return_addr))
    return false;

  if (log)
    log->Printf("Writing PC: 0x%" PRIx64, (uint64_t)func_addr);

  // Set pc to the address of the called function.
  if (!reg_ctx->WriteRegisterFromUnsigned(pc_reg_info, func_addr))
    return false;

  if (log)
    log->Printf("Writing r25: 0x%" PRIx64, (uint64_t)func_addr);

  // All callers of position independent functions must place the address of
  // the called function in t9 (r25)
  if (!reg_ctx->WriteRegisterFromUnsigned(r25_info, func_addr))
    return false;

  return true;
}

bool ABISysV_mips::GetArgumentValues(Thread &thread, ValueList &values) const {
  return false;
}

Status ABISysV_mips::SetReturnValueObject(lldb::StackFrameSP &frame_sp,
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
      const RegisterInfo *r2_info = reg_ctx->GetRegisterInfoByName("r2", 0);
      if (num_bytes <= 4) {
        uint32_t raw_value = data.GetMaxU32(&offset, num_bytes);

        if (reg_ctx->WriteRegisterFromUnsigned(r2_info, raw_value))
          set_it_simple = true;
      } else {
        uint32_t raw_value = data.GetMaxU32(&offset, 4);

        if (reg_ctx->WriteRegisterFromUnsigned(r2_info, raw_value)) {
          const RegisterInfo *r3_info = reg_ctx->GetRegisterInfoByName("r3", 0);
          uint32_t raw_value = data.GetMaxU32(&offset, num_bytes - offset);

          if (reg_ctx->WriteRegisterFromUnsigned(r3_info, raw_value))
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

ValueObjectSP ABISysV_mips::GetReturnValueObjectSimple(
    Thread &thread, CompilerType &return_compiler_type) const {
  ValueObjectSP return_valobj_sp;
  return return_valobj_sp;
}

ValueObjectSP ABISysV_mips::GetReturnValueObjectImpl(
    Thread &thread, CompilerType &return_compiler_type) const {
  ValueObjectSP return_valobj_sp;
  Value value;

  if (!return_compiler_type)
    return return_valobj_sp;

  ExecutionContext exe_ctx(thread.shared_from_this());
  if (exe_ctx.GetTargetPtr() == nullptr || exe_ctx.GetProcessPtr() == nullptr)
    return return_valobj_sp;

  Target *target = exe_ctx.GetTargetPtr();
  const ArchSpec target_arch = target->GetArchitecture();
  ByteOrder target_byte_order = target_arch.GetByteOrder();
  value.SetCompilerType(return_compiler_type);
  uint32_t fp_flag =
      target_arch.GetFlags() & lldb_private::ArchSpec::eMIPS_ABI_FP_mask;

  RegisterContext *reg_ctx = thread.GetRegisterContext().get();
  if (!reg_ctx)
    return return_valobj_sp;

  bool is_signed = false;
  bool is_complex = false;
  uint32_t count = 0;

  // In MIPS register "r2" (v0) holds the integer function return values
  const RegisterInfo *r2_reg_info = reg_ctx->GetRegisterInfoByName("r2", 0);
  llvm::Optional<uint64_t> bit_width = return_compiler_type.GetBitSize(&thread);
  if (!bit_width)
    return return_valobj_sp;
  if (return_compiler_type.IsIntegerOrEnumerationType(is_signed)) {
    switch (*bit_width) {
    default:
      return return_valobj_sp;
    case 64: {
      const RegisterInfo *r3_reg_info = reg_ctx->GetRegisterInfoByName("r3", 0);
      uint64_t raw_value;
      raw_value = reg_ctx->ReadRegisterAsUnsigned(r2_reg_info, 0) & UINT32_MAX;
      raw_value |= ((uint64_t)(reg_ctx->ReadRegisterAsUnsigned(r3_reg_info, 0) &
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
            reg_ctx->ReadRegisterAsUnsigned(r2_reg_info, 0) & UINT32_MAX);
      else
        value.GetScalar() = (uint32_t)(
            reg_ctx->ReadRegisterAsUnsigned(r2_reg_info, 0) & UINT32_MAX);
      break;
    case 16:
      if (is_signed)
        value.GetScalar() = (int16_t)(
            reg_ctx->ReadRegisterAsUnsigned(r2_reg_info, 0) & UINT16_MAX);
      else
        value.GetScalar() = (uint16_t)(
            reg_ctx->ReadRegisterAsUnsigned(r2_reg_info, 0) & UINT16_MAX);
      break;
    case 8:
      if (is_signed)
        value.GetScalar() = (int8_t)(
            reg_ctx->ReadRegisterAsUnsigned(r2_reg_info, 0) & UINT8_MAX);
      else
        value.GetScalar() = (uint8_t)(
            reg_ctx->ReadRegisterAsUnsigned(r2_reg_info, 0) & UINT8_MAX);
      break;
    }
  } else if (return_compiler_type.IsPointerType()) {
    uint32_t ptr =
        thread.GetRegisterContext()->ReadRegisterAsUnsigned(r2_reg_info, 0) &
        UINT32_MAX;
    value.GetScalar() = ptr;
  } else if (return_compiler_type.IsAggregateType()) {
    // Structure/Vector is always passed in memory and pointer to that memory
    // is passed in r2.
    uint64_t mem_address = reg_ctx->ReadRegisterAsUnsigned(
        reg_ctx->GetRegisterInfoByName("r2", 0), 0);
    // We have got the address. Create a memory object out of it
    return_valobj_sp = ValueObjectMemory::Create(
        &thread, "", Address(mem_address, nullptr), return_compiler_type);
    return return_valobj_sp;
  } else if (return_compiler_type.IsFloatingPointType(count, is_complex)) {
    if (IsSoftFloat(fp_flag)) {
      uint64_t raw_value = reg_ctx->ReadRegisterAsUnsigned(r2_reg_info, 0);
      if (count != 1 && is_complex)
        return return_valobj_sp;
      switch (*bit_width) {
      default:
        return return_valobj_sp;
      case 32:
        static_assert(sizeof(float) == sizeof(uint32_t), "");
        value.GetScalar() = *((float *)(&raw_value));
        break;
      case 64:
        static_assert(sizeof(double) == sizeof(uint64_t), "");
        const RegisterInfo *r3_reg_info =
            reg_ctx->GetRegisterInfoByName("r3", 0);
        if (target_byte_order == eByteOrderLittle)
          raw_value =
              ((reg_ctx->ReadRegisterAsUnsigned(r3_reg_info, 0)) << 32) |
              raw_value;
        else
          raw_value = (raw_value << 32) |
                      reg_ctx->ReadRegisterAsUnsigned(r3_reg_info, 0);
        value.GetScalar() = *((double *)(&raw_value));
        break;
      }
    }

    else {
      const RegisterInfo *f0_info = reg_ctx->GetRegisterInfoByName("f0", 0);
      RegisterValue f0_value;
      DataExtractor f0_data;
      reg_ctx->ReadRegister(f0_info, f0_value);
      f0_value.GetData(f0_data);
      lldb::offset_t offset = 0;

      if (count == 1 && !is_complex) {
        switch (*bit_width) {
        default:
          return return_valobj_sp;
        case 64: {
          static_assert(sizeof(double) == sizeof(uint64_t), "");
          const RegisterInfo *f1_info = reg_ctx->GetRegisterInfoByName("f1", 0);
          RegisterValue f1_value;
          DataExtractor f1_data;
          reg_ctx->ReadRegister(f1_info, f1_value);
          DataExtractor *copy_from_extractor = nullptr;
          DataBufferSP data_sp(new DataBufferHeap(8, 0));
          DataExtractor return_ext(
              data_sp, target_byte_order,
              target->GetArchitecture().GetAddressByteSize());

          if (target_byte_order == eByteOrderLittle) {
            copy_from_extractor = &f0_data;
            copy_from_extractor->CopyByteOrderedData(
                offset, 4, data_sp->GetBytes(), 4, target_byte_order);
            f1_value.GetData(f1_data);
            copy_from_extractor = &f1_data;
            copy_from_extractor->CopyByteOrderedData(
                offset, 4, data_sp->GetBytes() + 4, 4, target_byte_order);
          } else {
            copy_from_extractor = &f0_data;
            copy_from_extractor->CopyByteOrderedData(
                offset, 4, data_sp->GetBytes() + 4, 4, target_byte_order);
            f1_value.GetData(f1_data);
            copy_from_extractor = &f1_data;
            copy_from_extractor->CopyByteOrderedData(
                offset, 4, data_sp->GetBytes(), 4, target_byte_order);
          }
          value.GetScalar() = (double)return_ext.GetDouble(&offset);
          break;
        }
        case 32: {
          static_assert(sizeof(float) == sizeof(uint32_t), "");
          value.GetScalar() = (float)f0_data.GetFloat(&offset);
          break;
        }
        }
      } else {
        // not handled yet
        return return_valobj_sp;
      }
    }
  } else {
    // not handled yet
    return return_valobj_sp;
  }

  // If we get here, we have a valid Value, so make our ValueObject out of it:

  return_valobj_sp = ValueObjectConstResult::Create(
      thread.GetStackFrameAtIndex(0).get(), value, ConstString(""));
  return return_valobj_sp;
}

bool ABISysV_mips::CreateFunctionEntryUnwindPlan(UnwindPlan &unwind_plan) {
  unwind_plan.Clear();
  unwind_plan.SetRegisterKind(eRegisterKindDWARF);

  UnwindPlan::RowSP row(new UnwindPlan::Row);

  // Our Call Frame Address is the stack pointer value
  row->GetCFAValue().SetIsRegisterPlusOffset(dwarf_r29, 0);

  // The previous PC is in the RA
  row->SetRegisterLocationToRegister(dwarf_pc, dwarf_r31, true);
  unwind_plan.AppendRow(row);

  // All other registers are the same.

  unwind_plan.SetSourceName("mips at-func-entry default");
  unwind_plan.SetSourcedFromCompiler(eLazyBoolNo);
  unwind_plan.SetReturnAddressRegister(dwarf_r31);
  return true;
}

bool ABISysV_mips::CreateDefaultUnwindPlan(UnwindPlan &unwind_plan) {
  unwind_plan.Clear();
  unwind_plan.SetRegisterKind(eRegisterKindDWARF);

  UnwindPlan::RowSP row(new UnwindPlan::Row);

  row->GetCFAValue().SetIsRegisterPlusOffset(dwarf_r29, 0);

  row->SetRegisterLocationToRegister(dwarf_pc, dwarf_r31, true);

  unwind_plan.AppendRow(row);
  unwind_plan.SetSourceName("mips default unwind plan");
  unwind_plan.SetSourcedFromCompiler(eLazyBoolNo);
  unwind_plan.SetUnwindPlanValidAtAllInstructions(eLazyBoolNo);
  return true;
}

bool ABISysV_mips::RegisterIsVolatile(const RegisterInfo *reg_info) {
  return !RegisterIsCalleeSaved(reg_info);
}

bool ABISysV_mips::IsSoftFloat(uint32_t fp_flags) const {
  return (fp_flags == lldb_private::ArchSpec::eMIPS_ABI_FP_SOFT);
}

bool ABISysV_mips::RegisterIsCalleeSaved(const RegisterInfo *reg_info) {
  if (reg_info) {
    // Preserved registers are :
    // r16-r23, r28, r29, r30, r31
    const char *name = reg_info->name;

    if (name[0] == 'r') {
      switch (name[1]) {
      case '1':
        if (name[2] == '6' || name[2] == '7' || name[2] == '8' ||
            name[2] == '9') // r16-r19
          return name[3] == '\0';
        break;
      case '2':
        if (name[2] == '0' || name[2] == '1' || name[2] == '2' ||
            name[2] == '3'                       // r20-r23
            || name[2] == '8' || name[2] == '9') // r28 and r29
          return name[3] == '\0';
        break;
      case '3':
        if (name[2] == '0' || name[2] == '1') // r30 and r31
          return name[3] == '\0';
        break;
      }

      if (name[0] == 'g' && name[1] == 'p' && name[2] == '\0') // gp (r28)
        return true;
      if (name[0] == 's' && name[1] == 'p' && name[2] == '\0') // sp (r29)
        return true;
      if (name[0] == 'f' && name[1] == 'p' && name[2] == '\0') // fp (r30)
        return true;
      if (name[0] == 'r' && name[1] == 'a' && name[2] == '\0') // ra (r31)
        return true;
    }
  }
  return false;
}

void ABISysV_mips::Initialize() {
  PluginManager::RegisterPlugin(
      GetPluginNameStatic(), "System V ABI for mips targets", CreateInstance);
}

void ABISysV_mips::Terminate() {
  PluginManager::UnregisterPlugin(CreateInstance);
}

lldb_private::ConstString ABISysV_mips::GetPluginNameStatic() {
  static ConstString g_name("sysv-mips");
  return g_name;
}

//------------------------------------------------------------------
// PluginInterface protocol
//------------------------------------------------------------------

lldb_private::ConstString ABISysV_mips::GetPluginName() {
  return GetPluginNameStatic();
}

uint32_t ABISysV_mips::GetPluginVersion() { return 1; }
