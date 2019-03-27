//===-- EmulateInstructionPPC64.cpp ------------------------------*- C++-*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "EmulateInstructionPPC64.h"

#include <stdlib.h>

#include "lldb/Core/PluginManager.h"
#include "lldb/Symbol/UnwindPlan.h"
#include "lldb/Utility/ArchSpec.h"
#include "lldb/Utility/ConstString.h"

#include "Plugins/Process/Utility/lldb-ppc64le-register-enums.h"

#define DECLARE_REGISTER_INFOS_PPC64LE_STRUCT
#include "Plugins/Process/Utility/RegisterInfos_ppc64le.h"

#include "Plugins/Process/Utility/InstructionUtils.h"

using namespace lldb;
using namespace lldb_private;

EmulateInstructionPPC64::EmulateInstructionPPC64(const ArchSpec &arch)
    : EmulateInstruction(arch) {}

void EmulateInstructionPPC64::Initialize() {
  PluginManager::RegisterPlugin(GetPluginNameStatic(),
                                GetPluginDescriptionStatic(), CreateInstance);
}

void EmulateInstructionPPC64::Terminate() {
  PluginManager::UnregisterPlugin(CreateInstance);
}

ConstString EmulateInstructionPPC64::GetPluginNameStatic() {
  ConstString g_plugin_name("lldb.emulate-instruction.ppc64");
  return g_plugin_name;
}

ConstString EmulateInstructionPPC64::GetPluginName() {
  static ConstString g_plugin_name("EmulateInstructionPPC64");
  return g_plugin_name;
}

const char *EmulateInstructionPPC64::GetPluginDescriptionStatic() {
  return "Emulate instructions for the PPC64 architecture.";
}

EmulateInstruction *
EmulateInstructionPPC64::CreateInstance(const ArchSpec &arch,
                                        InstructionType inst_type) {
  if (EmulateInstructionPPC64::SupportsEmulatingInstructionsOfTypeStatic(
          inst_type)) {
    if (arch.GetTriple().getArch() == llvm::Triple::ppc64 ||
        arch.GetTriple().getArch() == llvm::Triple::ppc64le) {
      return new EmulateInstructionPPC64(arch);
    }
  }

  return nullptr;
}

bool EmulateInstructionPPC64::SetTargetTriple(const ArchSpec &arch) {
  if (arch.GetTriple().getArch() == llvm::Triple::ppc64)
    return true;
  else if (arch.GetTriple().getArch() == llvm::Triple::ppc64le)
    return true;

  return false;
}

static bool LLDBTableGetRegisterInfo(uint32_t reg_num, RegisterInfo &reg_info) {
  if (reg_num >= llvm::array_lengthof(g_register_infos_ppc64le))
    return false;
  reg_info = g_register_infos_ppc64le[reg_num];
  return true;
}

bool EmulateInstructionPPC64::GetRegisterInfo(RegisterKind reg_kind,
                                              uint32_t reg_num,
                                              RegisterInfo &reg_info) {
  if (reg_kind == eRegisterKindGeneric) {
    switch (reg_num) {
    case LLDB_REGNUM_GENERIC_PC:
      reg_kind = eRegisterKindLLDB;
      reg_num = gpr_pc_ppc64le;
      break;
    case LLDB_REGNUM_GENERIC_SP:
      reg_kind = eRegisterKindLLDB;
      reg_num = gpr_r1_ppc64le;
      break;
    case LLDB_REGNUM_GENERIC_RA:
      reg_kind = eRegisterKindLLDB;
      reg_num = gpr_lr_ppc64le;
      break;
    case LLDB_REGNUM_GENERIC_FLAGS:
      reg_kind = eRegisterKindLLDB;
      reg_num = gpr_cr_ppc64le;
      break;

    default:
      return false;
    }
  }

  if (reg_kind == eRegisterKindLLDB)
    return LLDBTableGetRegisterInfo(reg_num, reg_info);
  return false;
}

bool EmulateInstructionPPC64::ReadInstruction() {
  bool success = false;
  m_addr = ReadRegisterUnsigned(eRegisterKindGeneric, LLDB_REGNUM_GENERIC_PC,
                                LLDB_INVALID_ADDRESS, &success);
  if (success) {
    Context ctx;
    ctx.type = eContextReadOpcode;
    ctx.SetNoArgs();
    m_opcode.SetOpcode32(ReadMemoryUnsigned(ctx, m_addr, 4, 0, &success),
                         GetByteOrder());
  }
  if (!success)
    m_addr = LLDB_INVALID_ADDRESS;
  return success;
}

bool EmulateInstructionPPC64::CreateFunctionEntryUnwind(
    UnwindPlan &unwind_plan) {
  unwind_plan.Clear();
  unwind_plan.SetRegisterKind(eRegisterKindLLDB);

  UnwindPlan::RowSP row(new UnwindPlan::Row);

  // Our previous Call Frame Address is the stack pointer
  row->GetCFAValue().SetIsRegisterPlusOffset(gpr_r1_ppc64le, 0);

  unwind_plan.AppendRow(row);
  unwind_plan.SetSourceName("EmulateInstructionPPC64");
  unwind_plan.SetSourcedFromCompiler(eLazyBoolNo);
  unwind_plan.SetUnwindPlanValidAtAllInstructions(eLazyBoolYes);
  unwind_plan.SetReturnAddressRegister(gpr_lr_ppc64le);
  return true;
}

EmulateInstructionPPC64::Opcode *
EmulateInstructionPPC64::GetOpcodeForInstruction(uint32_t opcode) {
  static EmulateInstructionPPC64::Opcode g_opcodes[] = {
      {0xfc0007ff, 0x7c0002a6, &EmulateInstructionPPC64::EmulateMFSPR,
       "mfspr RT, SPR"},
      {0xfc000003, 0xf8000000, &EmulateInstructionPPC64::EmulateSTD,
       "std RS, DS(RA)"},
      {0xfc000003, 0xf8000001, &EmulateInstructionPPC64::EmulateSTD,
       "stdu RS, DS(RA)"},
      {0xfc0007fe, 0x7c000378, &EmulateInstructionPPC64::EmulateOR,
       "or RA, RS, RB"},
      {0xfc000000, 0x38000000, &EmulateInstructionPPC64::EmulateADDI,
       "addi RT, RA, SI"},
      {0xfc000003, 0xe8000000, &EmulateInstructionPPC64::EmulateLD,
       "ld RT, DS(RA)"}};
  static const size_t k_num_ppc_opcodes = llvm::array_lengthof(g_opcodes);

  for (size_t i = 0; i < k_num_ppc_opcodes; ++i) {
    if ((g_opcodes[i].mask & opcode) == g_opcodes[i].value)
      return &g_opcodes[i];
  }
  return nullptr;
}

bool EmulateInstructionPPC64::EvaluateInstruction(uint32_t evaluate_options) {
  const uint32_t opcode = m_opcode.GetOpcode32();
  // LLDB_LOG(log, "PPC64::EvaluateInstruction: opcode={0:X+8}", opcode);
  Opcode *opcode_data = GetOpcodeForInstruction(opcode);
  if (!opcode_data)
    return false;

  // LLDB_LOG(log, "PPC64::EvaluateInstruction: {0}", opcode_data->name);
  const bool auto_advance_pc =
      evaluate_options & eEmulateInstructionOptionAutoAdvancePC;

  bool success = false;

  uint32_t orig_pc_value = 0;
  if (auto_advance_pc) {
    orig_pc_value =
        ReadRegisterUnsigned(eRegisterKindLLDB, gpr_pc_ppc64le, 0, &success);
    if (!success)
      return false;
  }

  // Call the Emulate... function.
  success = (this->*opcode_data->callback)(opcode);
  if (!success)
    return false;

  if (auto_advance_pc) {
    uint32_t new_pc_value =
        ReadRegisterUnsigned(eRegisterKindLLDB, gpr_pc_ppc64le, 0, &success);
    if (!success)
      return false;

    if (auto_advance_pc && (new_pc_value == orig_pc_value)) {
      EmulateInstruction::Context context;
      context.type = eContextAdvancePC;
      context.SetNoArgs();
      if (!WriteRegisterUnsigned(context, eRegisterKindLLDB, gpr_pc_ppc64le,
                                 orig_pc_value + 4))
        return false;
    }
  }
  return true;
}

bool EmulateInstructionPPC64::EmulateMFSPR(uint32_t opcode) {
  uint32_t rt = Bits32(opcode, 25, 21);
  uint32_t spr = Bits32(opcode, 20, 11);

  enum { SPR_LR = 0x100 };

  // For now, we're only insterested in 'mfspr r0, lr'
  if (rt != gpr_r0_ppc64le || spr != SPR_LR)
    return false;

  Log *log(GetLogIfAllCategoriesSet(LIBLLDB_LOG_UNWIND));
  LLDB_LOG(log, "EmulateMFSPR: {0:X+8}: mfspr r0, lr", m_addr);

  bool success;
  uint64_t lr =
      ReadRegisterUnsigned(eRegisterKindLLDB, gpr_lr_ppc64le, 0, &success);
  if (!success)
    return false;
  Context context;
  context.type = eContextWriteRegisterRandomBits;
  WriteRegisterUnsigned(context, eRegisterKindLLDB, gpr_r0_ppc64le, lr);
  LLDB_LOG(log, "EmulateMFSPR: success!");
  return true;
}

bool EmulateInstructionPPC64::EmulateLD(uint32_t opcode) {
  uint32_t rt = Bits32(opcode, 25, 21);
  uint32_t ra = Bits32(opcode, 20, 16);
  uint32_t ds = Bits32(opcode, 15, 2);

  int32_t ids = llvm::SignExtend32<16>(ds << 2);

  // For now, tracking only loads from 0(r1) to r1 (0(r1) is the ABI defined
  // location to save previous SP)
  if (ra != gpr_r1_ppc64le || rt != gpr_r1_ppc64le || ids != 0)
    return false;

  Log *log(GetLogIfAllCategoriesSet(LIBLLDB_LOG_UNWIND));
  LLDB_LOG(log, "EmulateLD: {0:X+8}: ld r{1}, {2}(r{3})", m_addr, rt, ids, ra);

  RegisterInfo r1_info;
  if (!GetRegisterInfo(eRegisterKindLLDB, gpr_r1_ppc64le, r1_info))
    return false;

  // restore SP
  Context ctx;
  ctx.type = eContextRestoreStackPointer;
  ctx.SetRegisterToRegisterPlusOffset(r1_info, r1_info, 0);

  WriteRegisterUnsigned(ctx, eRegisterKindLLDB, gpr_r1_ppc64le, 0);
  LLDB_LOG(log, "EmulateLD: success!");
  return true;
}

bool EmulateInstructionPPC64::EmulateSTD(uint32_t opcode) {
  uint32_t rs = Bits32(opcode, 25, 21);
  uint32_t ra = Bits32(opcode, 20, 16);
  uint32_t ds = Bits32(opcode, 15, 2);
  uint32_t u = Bits32(opcode, 1, 0);

  // For now, tracking only stores to r1
  if (ra != gpr_r1_ppc64le)
    return false;
  // ... and only stores of SP, FP and LR (moved into r0 by a previous mfspr)
  if (rs != gpr_r1_ppc64le && rs != gpr_r31_ppc64le && rs != gpr_r30_ppc64le &&
      rs != gpr_r0_ppc64le)
    return false;

  bool success;
  uint64_t rs_val = ReadRegisterUnsigned(eRegisterKindLLDB, rs, 0, &success);
  if (!success)
    return false;

  int32_t ids = llvm::SignExtend32<16>(ds << 2);
  Log *log(GetLogIfAllCategoriesSet(LIBLLDB_LOG_UNWIND));
  LLDB_LOG(log, "EmulateSTD: {0:X+8}: std{1} r{2}, {3}(r{4})", m_addr,
           u ? "u" : "", rs, ids, ra);

  // Make sure that r0 is really holding LR value (this won't catch unlikely
  // cases, such as r0 being overwritten after mfspr)
  uint32_t rs_num = rs;
  if (rs == gpr_r0_ppc64le) {
    uint64_t lr =
        ReadRegisterUnsigned(eRegisterKindLLDB, gpr_lr_ppc64le, 0, &success);
    if (!success || lr != rs_val)
      return false;
    rs_num = gpr_lr_ppc64le;
  }

  // set context
  RegisterInfo rs_info;
  if (!GetRegisterInfo(eRegisterKindLLDB, rs_num, rs_info))
    return false;
  RegisterInfo ra_info;
  if (!GetRegisterInfo(eRegisterKindLLDB, ra, ra_info))
    return false;

  Context ctx;
  ctx.type = eContextPushRegisterOnStack;
  ctx.SetRegisterToRegisterPlusOffset(rs_info, ra_info, ids);

  // store
  uint64_t ra_val = ReadRegisterUnsigned(eRegisterKindLLDB, ra, 0, &success);
  if (!success)
    return false;

  lldb::addr_t addr = ra_val + ids;
  WriteMemory(ctx, addr, &rs_val, sizeof(rs_val));

  // update RA?
  if (u) {
    Context ctx;
    // NOTE Currently, RA will always be equal to SP(r1)
    ctx.type = eContextAdjustStackPointer;
    WriteRegisterUnsigned(ctx, eRegisterKindLLDB, ra, addr);
  }

  LLDB_LOG(log, "EmulateSTD: success!");
  return true;
}

bool EmulateInstructionPPC64::EmulateOR(uint32_t opcode) {
  uint32_t rs = Bits32(opcode, 25, 21);
  uint32_t ra = Bits32(opcode, 20, 16);
  uint32_t rb = Bits32(opcode, 15, 11);

  // to be safe, process only the known 'mr r31/r30, r1' prologue instructions
  if (m_fp != LLDB_INVALID_REGNUM || rs != rb ||
      (ra != gpr_r30_ppc64le && ra != gpr_r31_ppc64le) || rb != gpr_r1_ppc64le)
    return false;

  Log *log(GetLogIfAllCategoriesSet(LIBLLDB_LOG_UNWIND));
  LLDB_LOG(log, "EmulateOR: {0:X+8}: mr r{1}, r{2}", m_addr, ra, rb);

  // set context
  RegisterInfo ra_info;
  if (!GetRegisterInfo(eRegisterKindLLDB, ra, ra_info))
    return false;

  Context ctx;
  ctx.type = eContextSetFramePointer;
  ctx.SetRegister(ra_info);

  // move
  bool success;
  uint64_t rb_val = ReadRegisterUnsigned(eRegisterKindLLDB, rb, 0, &success);
  if (!success)
    return false;
  WriteRegisterUnsigned(ctx, eRegisterKindLLDB, ra, rb_val);
  m_fp = ra;
  LLDB_LOG(log, "EmulateOR: success!");
  return true;
}

bool EmulateInstructionPPC64::EmulateADDI(uint32_t opcode) {
  uint32_t rt = Bits32(opcode, 25, 21);
  uint32_t ra = Bits32(opcode, 20, 16);
  uint32_t si = Bits32(opcode, 15, 0);

  // handle stack adjustments only
  // (this is a typical epilogue operation, with ra == r1. If it's
  //  something else, then we won't know the correct value of ra)
  if (rt != gpr_r1_ppc64le || ra != gpr_r1_ppc64le)
    return false;

  int32_t si_val = llvm::SignExtend32<16>(si);
  Log *log(GetLogIfAllCategoriesSet(LIBLLDB_LOG_UNWIND));
  LLDB_LOG(log, "EmulateADDI: {0:X+8}: addi r1, r1, {1}", m_addr, si_val);

  // set context
  RegisterInfo r1_info;
  if (!GetRegisterInfo(eRegisterKindLLDB, gpr_r1_ppc64le, r1_info))
    return false;

  Context ctx;
  ctx.type = eContextRestoreStackPointer;
  ctx.SetRegisterToRegisterPlusOffset(r1_info, r1_info, 0);

  // adjust SP
  bool success;
  uint64_t r1 =
      ReadRegisterUnsigned(eRegisterKindLLDB, gpr_r1_ppc64le, 0, &success);
  if (!success)
    return false;
  WriteRegisterUnsigned(ctx, eRegisterKindLLDB, gpr_r1_ppc64le, r1 + si_val);
  LLDB_LOG(log, "EmulateADDI: success!");
  return true;
}
