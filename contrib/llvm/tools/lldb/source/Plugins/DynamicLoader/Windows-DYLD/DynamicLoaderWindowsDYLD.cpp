//===-- DynamicLoaderWindowsDYLD.cpp --------------------------------*- C++
//-*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "DynamicLoaderWindowsDYLD.h"

#include "lldb/Core/Module.h"
#include "lldb/Core/PluginManager.h"
#include "lldb/Target/ExecutionContext.h"
#include "lldb/Target/Process.h"
#include "lldb/Target/RegisterContext.h"
#include "lldb/Target/Target.h"
#include "lldb/Target/ThreadPlanStepInstruction.h"
#include "lldb/Utility/Log.h"

#include "llvm/ADT/Triple.h"

using namespace lldb;
using namespace lldb_private;

DynamicLoaderWindowsDYLD::DynamicLoaderWindowsDYLD(Process *process)
    : DynamicLoader(process) {}

DynamicLoaderWindowsDYLD::~DynamicLoaderWindowsDYLD() {}

void DynamicLoaderWindowsDYLD::Initialize() {
  PluginManager::RegisterPlugin(GetPluginNameStatic(),
                                GetPluginDescriptionStatic(), CreateInstance);
}

void DynamicLoaderWindowsDYLD::Terminate() {}

ConstString DynamicLoaderWindowsDYLD::GetPluginNameStatic() {
  static ConstString g_plugin_name("windows-dyld");
  return g_plugin_name;
}

const char *DynamicLoaderWindowsDYLD::GetPluginDescriptionStatic() {
  return "Dynamic loader plug-in that watches for shared library "
         "loads/unloads in Windows processes.";
}

DynamicLoader *DynamicLoaderWindowsDYLD::CreateInstance(Process *process,
                                                        bool force) {
  bool should_create = force;
  if (!should_create) {
    const llvm::Triple &triple_ref =
        process->GetTarget().GetArchitecture().GetTriple();
    if (triple_ref.getOS() == llvm::Triple::Win32)
      should_create = true;
  }

  if (should_create)
    return new DynamicLoaderWindowsDYLD(process);

  return nullptr;
}

void DynamicLoaderWindowsDYLD::DidAttach() {
    Log *log(GetLogIfAnyCategoriesSet(LIBLLDB_LOG_DYNAMIC_LOADER));
  if (log)
    log->Printf("DynamicLoaderWindowsDYLD::%s()", __FUNCTION__);

  ModuleSP executable = GetTargetExecutable();

  if (!executable.get())
    return;

  // Try to fetch the load address of the file from the process, since there
  // could be randomization of the load address.

  // It might happen that the remote has a different dir for the file, so we
  // only send the basename of the executable in the query. I think this is safe
  // because I doubt that two executables with the same basenames are loaded in
  // memory...
  FileSpec file_spec(
      executable->GetPlatformFileSpec().GetFilename().GetCString());
  bool is_loaded;
  addr_t base_addr = 0;
  lldb::addr_t load_addr;
  Status error = m_process->GetFileLoadAddress(file_spec, is_loaded, load_addr);
  if (error.Success() && is_loaded) {
    base_addr = load_addr;
    UpdateLoadedSections(executable, LLDB_INVALID_ADDRESS, base_addr, false);
  }

  ModuleList module_list;
  module_list.Append(executable);
  m_process->GetTarget().ModulesDidLoad(module_list);
  m_process->LoadModules();
}

void DynamicLoaderWindowsDYLD::DidLaunch() {}

Status DynamicLoaderWindowsDYLD::CanLoadImage() { return Status(); }

ConstString DynamicLoaderWindowsDYLD::GetPluginName() {
  return GetPluginNameStatic();
}

uint32_t DynamicLoaderWindowsDYLD::GetPluginVersion() { return 1; }

ThreadPlanSP
DynamicLoaderWindowsDYLD::GetStepThroughTrampolinePlan(Thread &thread,
                                                       bool stop) {
  auto arch = m_process->GetTarget().GetArchitecture();
  if (arch.GetMachine() != llvm::Triple::x86) {
    return ThreadPlanSP();
  }

  uint64_t pc = thread.GetRegisterContext()->GetPC();
  // Max size of an instruction in x86 is 15 bytes.
  AddressRange range(pc, 2 * 15);

  ExecutionContext exe_ctx(m_process->GetTarget());
  DisassemblerSP disassembler_sp = Disassembler::DisassembleRange(
      arch, nullptr, nullptr, exe_ctx, range, true);
  if (!disassembler_sp) {
    return ThreadPlanSP();
  }

  InstructionList *insn_list = &disassembler_sp->GetInstructionList();
  if (insn_list == nullptr) {
    return ThreadPlanSP();
  }

  // First instruction in a x86 Windows trampoline is going to be an indirect
  // jump through the IAT and the next one will be a nop (usually there for
  // alignment purposes). e.g.:
  //     0x70ff4cfc <+956>: jmpl   *0x7100c2a8
  //     0x70ff4d02 <+962>: nop

  auto first_insn = insn_list->GetInstructionAtIndex(0);
  auto second_insn = insn_list->GetInstructionAtIndex(1);

  if (first_insn == nullptr || second_insn == nullptr ||
      strcmp(first_insn->GetMnemonic(&exe_ctx), "jmpl") != 0 ||
      strcmp(second_insn->GetMnemonic(&exe_ctx), "nop") != 0) {
    return ThreadPlanSP();
  }

  assert(first_insn->DoesBranch() && !second_insn->DoesBranch());

  return ThreadPlanSP(new ThreadPlanStepInstruction(
      thread, false, false, eVoteNoOpinion, eVoteNoOpinion));
}
