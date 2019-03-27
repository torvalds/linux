//===-- UnwindAssembly-x86.cpp ----------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "UnwindAssembly-x86.h"
#include "x86AssemblyInspectionEngine.h"

#include "llvm-c/Disassembler.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/Support/TargetSelect.h"

#include "lldb/Core/Address.h"
#include "lldb/Core/PluginManager.h"
#include "lldb/Symbol/UnwindPlan.h"
#include "lldb/Target/ABI.h"
#include "lldb/Target/ExecutionContext.h"
#include "lldb/Target/Process.h"
#include "lldb/Target/RegisterContext.h"
#include "lldb/Target/RegisterNumber.h"
#include "lldb/Target/Target.h"
#include "lldb/Target/Thread.h"
#include "lldb/Target/UnwindAssembly.h"
#include "lldb/Utility/ArchSpec.h"
#include "lldb/Utility/Status.h"

using namespace lldb;
using namespace lldb_private;

//-----------------------------------------------------------------------------------------------
//  UnwindAssemblyParser_x86 method definitions
//-----------------------------------------------------------------------------------------------

UnwindAssembly_x86::UnwindAssembly_x86(const ArchSpec &arch)
    : lldb_private::UnwindAssembly(arch),
      m_assembly_inspection_engine(new x86AssemblyInspectionEngine(arch)) {}

UnwindAssembly_x86::~UnwindAssembly_x86() {
  delete m_assembly_inspection_engine;
}

bool UnwindAssembly_x86::GetNonCallSiteUnwindPlanFromAssembly(
    AddressRange &func, Thread &thread, UnwindPlan &unwind_plan) {
  if (!func.GetBaseAddress().IsValid() || func.GetByteSize() == 0)
    return false;
  if (m_assembly_inspection_engine == nullptr)
    return false;
  ProcessSP process_sp(thread.GetProcess());
  if (process_sp.get() == nullptr)
    return false;
  const bool prefer_file_cache = true;
  std::vector<uint8_t> function_text(func.GetByteSize());
  Status error;
  if (process_sp->GetTarget().ReadMemory(
          func.GetBaseAddress(), prefer_file_cache, function_text.data(),
          func.GetByteSize(), error) == func.GetByteSize()) {
    RegisterContextSP reg_ctx(thread.GetRegisterContext());
    m_assembly_inspection_engine->Initialize(reg_ctx);
    return m_assembly_inspection_engine->GetNonCallSiteUnwindPlanFromAssembly(
        function_text.data(), func.GetByteSize(), func, unwind_plan);
  }
  return false;
}

bool UnwindAssembly_x86::AugmentUnwindPlanFromCallSite(
    AddressRange &func, Thread &thread, UnwindPlan &unwind_plan) {
  bool do_augment_unwindplan = true;

  UnwindPlan::RowSP first_row = unwind_plan.GetRowForFunctionOffset(0);
  UnwindPlan::RowSP last_row = unwind_plan.GetRowForFunctionOffset(-1);

  int wordsize = 8;
  ProcessSP process_sp(thread.GetProcess());
  if (process_sp.get() == nullptr)
    return false;

  wordsize = process_sp->GetTarget().GetArchitecture().GetAddressByteSize();

  RegisterNumber sp_regnum(thread, eRegisterKindGeneric,
                           LLDB_REGNUM_GENERIC_SP);
  RegisterNumber pc_regnum(thread, eRegisterKindGeneric,
                           LLDB_REGNUM_GENERIC_PC);

  // Does this UnwindPlan describe the prologue?  I want to see that the CFA is
  // set in terms of the stack pointer plus an offset, and I want to see that
  // rip is retrieved at the CFA-wordsize. If there is no description of the
  // prologue, don't try to augment this eh_frame unwinder code, fall back to
  // assembly parsing instead.

  if (first_row->GetCFAValue().GetValueType() !=
          UnwindPlan::Row::FAValue::isRegisterPlusOffset ||
      RegisterNumber(thread, unwind_plan.GetRegisterKind(),
                     first_row->GetCFAValue().GetRegisterNumber()) !=
          sp_regnum ||
      first_row->GetCFAValue().GetOffset() != wordsize) {
    return false;
  }
  UnwindPlan::Row::RegisterLocation first_row_pc_loc;
  if (!first_row->GetRegisterInfo(
          pc_regnum.GetAsKind(unwind_plan.GetRegisterKind()),
          first_row_pc_loc) ||
      !first_row_pc_loc.IsAtCFAPlusOffset() ||
      first_row_pc_loc.GetOffset() != -wordsize) {
    return false;
  }

  // It looks like the prologue is described. Is the epilogue described?  If it
  // is, no need to do any augmentation.

  if (first_row != last_row &&
      first_row->GetOffset() != last_row->GetOffset()) {
    // The first & last row have the same CFA register and the same CFA offset
    // value and the CFA register is esp/rsp (the stack pointer).

    // We're checking that both of them have an unwind rule like "CFA=esp+4" or
    // CFA+rsp+8".

    if (first_row->GetCFAValue().GetValueType() ==
            last_row->GetCFAValue().GetValueType() &&
        first_row->GetCFAValue().GetRegisterNumber() ==
            last_row->GetCFAValue().GetRegisterNumber() &&
        first_row->GetCFAValue().GetOffset() ==
            last_row->GetCFAValue().GetOffset()) {
      // Get the register locations for eip/rip from the first & last rows. Are
      // they both CFA plus an offset?  Is it the same offset?

      UnwindPlan::Row::RegisterLocation last_row_pc_loc;
      if (last_row->GetRegisterInfo(
              pc_regnum.GetAsKind(unwind_plan.GetRegisterKind()),
              last_row_pc_loc)) {
        if (last_row_pc_loc.IsAtCFAPlusOffset() &&
            first_row_pc_loc.GetOffset() == last_row_pc_loc.GetOffset()) {

          // One last sanity check:  Is the unwind rule for getting the caller
          // pc value "deref the CFA-4" or "deref the CFA-8"?

          // If so, we have an UnwindPlan that already describes the epilogue
          // and we don't need to modify it at all.

          if (first_row_pc_loc.GetOffset() == -wordsize) {
            do_augment_unwindplan = false;
          }
        }
      }
    }
  }

  if (do_augment_unwindplan) {
    if (!func.GetBaseAddress().IsValid() || func.GetByteSize() == 0)
      return false;
    if (m_assembly_inspection_engine == nullptr)
      return false;
    const bool prefer_file_cache = true;
    std::vector<uint8_t> function_text(func.GetByteSize());
    Status error;
    if (process_sp->GetTarget().ReadMemory(
            func.GetBaseAddress(), prefer_file_cache, function_text.data(),
            func.GetByteSize(), error) == func.GetByteSize()) {
      RegisterContextSP reg_ctx(thread.GetRegisterContext());
      m_assembly_inspection_engine->Initialize(reg_ctx);
      return m_assembly_inspection_engine->AugmentUnwindPlanFromCallSite(
          function_text.data(), func.GetByteSize(), func, unwind_plan, reg_ctx);
    }
  }

  return false;
}

bool UnwindAssembly_x86::GetFastUnwindPlan(AddressRange &func, Thread &thread,
                                           UnwindPlan &unwind_plan) {
  // if prologue is
  //   55     pushl %ebp
  //   89 e5  movl %esp, %ebp
  //  or
  //   55        pushq %rbp
  //   48 89 e5  movq %rsp, %rbp

  // We should pull in the ABI architecture default unwind plan and return that

  llvm::SmallVector<uint8_t, 4> opcode_data;

  ProcessSP process_sp = thread.GetProcess();
  if (process_sp) {
    Target &target(process_sp->GetTarget());
    const bool prefer_file_cache = true;
    Status error;
    if (target.ReadMemory(func.GetBaseAddress(), prefer_file_cache,
                          opcode_data.data(), 4, error) == 4) {
      uint8_t i386_push_mov[] = {0x55, 0x89, 0xe5};
      uint8_t x86_64_push_mov[] = {0x55, 0x48, 0x89, 0xe5};

      if (memcmp(opcode_data.data(), i386_push_mov, sizeof(i386_push_mov)) ==
              0 ||
          memcmp(opcode_data.data(), x86_64_push_mov,
                 sizeof(x86_64_push_mov)) == 0) {
        ABISP abi_sp = process_sp->GetABI();
        if (abi_sp) {
          return abi_sp->CreateDefaultUnwindPlan(unwind_plan);
        }
      }
    }
  }
  return false;
}

bool UnwindAssembly_x86::FirstNonPrologueInsn(
    AddressRange &func, const ExecutionContext &exe_ctx,
    Address &first_non_prologue_insn) {

  if (!func.GetBaseAddress().IsValid())
    return false;

  Target *target = exe_ctx.GetTargetPtr();
  if (target == nullptr)
    return false;

  if (m_assembly_inspection_engine == nullptr)
    return false;

  const bool prefer_file_cache = true;
  std::vector<uint8_t> function_text(func.GetByteSize());
  Status error;
  if (target->ReadMemory(func.GetBaseAddress(), prefer_file_cache,
                         function_text.data(), func.GetByteSize(),
                         error) == func.GetByteSize()) {
    size_t offset;
    if (m_assembly_inspection_engine->FindFirstNonPrologueInstruction(
            function_text.data(), func.GetByteSize(), offset)) {
      first_non_prologue_insn = func.GetBaseAddress();
      first_non_prologue_insn.Slide(offset);
    }
    return true;
  }
  return false;
}

UnwindAssembly *UnwindAssembly_x86::CreateInstance(const ArchSpec &arch) {
  const llvm::Triple::ArchType cpu = arch.GetMachine();
  if (cpu == llvm::Triple::x86 || cpu == llvm::Triple::x86_64)
    return new UnwindAssembly_x86(arch);
  return NULL;
}

//------------------------------------------------------------------
// PluginInterface protocol in UnwindAssemblyParser_x86
//------------------------------------------------------------------

ConstString UnwindAssembly_x86::GetPluginName() {
  return GetPluginNameStatic();
}

uint32_t UnwindAssembly_x86::GetPluginVersion() { return 1; }

void UnwindAssembly_x86::Initialize() {
  PluginManager::RegisterPlugin(GetPluginNameStatic(),
                                GetPluginDescriptionStatic(), CreateInstance);
}

void UnwindAssembly_x86::Terminate() {
  PluginManager::UnregisterPlugin(CreateInstance);
}

lldb_private::ConstString UnwindAssembly_x86::GetPluginNameStatic() {
  static ConstString g_name("x86");
  return g_name;
}

const char *UnwindAssembly_x86::GetPluginDescriptionStatic() {
  return "i386 and x86_64 assembly language profiler plugin.";
}
