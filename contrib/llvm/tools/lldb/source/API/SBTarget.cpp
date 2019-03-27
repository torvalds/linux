//===-- SBTarget.cpp --------------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "lldb/API/SBTarget.h"

#include "lldb/lldb-public.h"

#include "lldb/API/SBBreakpoint.h"
#include "lldb/API/SBDebugger.h"
#include "lldb/API/SBEvent.h"
#include "lldb/API/SBExpressionOptions.h"
#include "lldb/API/SBFileSpec.h"
#include "lldb/API/SBListener.h"
#include "lldb/API/SBModule.h"
#include "lldb/API/SBModuleSpec.h"
#include "lldb/API/SBProcess.h"
#include "lldb/API/SBSourceManager.h"
#include "lldb/API/SBStream.h"
#include "lldb/API/SBStringList.h"
#include "lldb/API/SBStructuredData.h"
#include "lldb/API/SBSymbolContextList.h"
#include "lldb/Breakpoint/BreakpointID.h"
#include "lldb/Breakpoint/BreakpointIDList.h"
#include "lldb/Breakpoint/BreakpointList.h"
#include "lldb/Breakpoint/BreakpointLocation.h"
#include "lldb/Core/Address.h"
#include "lldb/Core/AddressResolver.h"
#include "lldb/Core/AddressResolverName.h"
#include "lldb/Core/Debugger.h"
#include "lldb/Core/Disassembler.h"
#include "lldb/Core/Module.h"
#include "lldb/Core/ModuleSpec.h"
#include "lldb/Core/STLUtils.h"
#include "lldb/Core/SearchFilter.h"
#include "lldb/Core/Section.h"
#include "lldb/Core/StructuredDataImpl.h"
#include "lldb/Core/ValueObjectConstResult.h"
#include "lldb/Core/ValueObjectList.h"
#include "lldb/Core/ValueObjectVariable.h"
#include "lldb/Host/Host.h"
#include "lldb/Symbol/ClangASTContext.h"
#include "lldb/Symbol/DeclVendor.h"
#include "lldb/Symbol/ObjectFile.h"
#include "lldb/Symbol/SymbolFile.h"
#include "lldb/Symbol/SymbolVendor.h"
#include "lldb/Symbol/VariableList.h"
#include "lldb/Target/ABI.h"
#include "lldb/Target/Language.h"
#include "lldb/Target/LanguageRuntime.h"
#include "lldb/Target/ObjCLanguageRuntime.h"
#include "lldb/Target/Process.h"
#include "lldb/Target/StackFrame.h"
#include "lldb/Target/Target.h"
#include "lldb/Target/TargetList.h"
#include "lldb/Utility/ArchSpec.h"
#include "lldb/Utility/Args.h"
#include "lldb/Utility/FileSpec.h"
#include "lldb/Utility/Log.h"
#include "lldb/Utility/RegularExpression.h"

#include "Commands/CommandObjectBreakpoint.h"
#include "lldb/Interpreter/CommandReturnObject.h"
#include "llvm/Support/PrettyStackTrace.h"
#include "llvm/Support/Regex.h"

using namespace lldb;
using namespace lldb_private;

#define DEFAULT_DISASM_BYTE_SIZE 32

namespace {

Status AttachToProcess(ProcessAttachInfo &attach_info, Target &target) {
  std::lock_guard<std::recursive_mutex> guard(target.GetAPIMutex());

  auto process_sp = target.GetProcessSP();
  if (process_sp) {
    const auto state = process_sp->GetState();
    if (process_sp->IsAlive() && state == eStateConnected) {
      // If we are already connected, then we have already specified the
      // listener, so if a valid listener is supplied, we need to error out to
      // let the client know.
      if (attach_info.GetListener())
        return Status("process is connected and already has a listener, pass "
                      "empty listener");
    }
  }

  return target.Attach(attach_info, nullptr);
}

} // namespace

//----------------------------------------------------------------------
// SBTarget constructor
//----------------------------------------------------------------------
SBTarget::SBTarget() : m_opaque_sp() {}

SBTarget::SBTarget(const SBTarget &rhs) : m_opaque_sp(rhs.m_opaque_sp) {}

SBTarget::SBTarget(const TargetSP &target_sp) : m_opaque_sp(target_sp) {}

const SBTarget &SBTarget::operator=(const SBTarget &rhs) {
  if (this != &rhs)
    m_opaque_sp = rhs.m_opaque_sp;
  return *this;
}

//----------------------------------------------------------------------
// Destructor
//----------------------------------------------------------------------
SBTarget::~SBTarget() {}

bool SBTarget::EventIsTargetEvent(const SBEvent &event) {
  return Target::TargetEventData::GetEventDataFromEvent(event.get()) != NULL;
}

SBTarget SBTarget::GetTargetFromEvent(const SBEvent &event) {
  return Target::TargetEventData::GetTargetFromEvent(event.get());
}

uint32_t SBTarget::GetNumModulesFromEvent(const SBEvent &event) {
  const ModuleList module_list =
      Target::TargetEventData::GetModuleListFromEvent(event.get());
  return module_list.GetSize();
}

SBModule SBTarget::GetModuleAtIndexFromEvent(const uint32_t idx,
                                             const SBEvent &event) {
  const ModuleList module_list =
      Target::TargetEventData::GetModuleListFromEvent(event.get());
  return SBModule(module_list.GetModuleAtIndex(idx));
}

const char *SBTarget::GetBroadcasterClassName() {
  return Target::GetStaticBroadcasterClass().AsCString();
}

bool SBTarget::IsValid() const {
  return m_opaque_sp.get() != NULL && m_opaque_sp->IsValid();
}

SBProcess SBTarget::GetProcess() {
  SBProcess sb_process;
  ProcessSP process_sp;
  TargetSP target_sp(GetSP());
  if (target_sp) {
    process_sp = target_sp->GetProcessSP();
    sb_process.SetSP(process_sp);
  }

  Log *log(lldb_private::GetLogIfAllCategoriesSet(LIBLLDB_LOG_API));
  if (log)
    log->Printf("SBTarget(%p)::GetProcess () => SBProcess(%p)",
                static_cast<void *>(target_sp.get()),
                static_cast<void *>(process_sp.get()));

  return sb_process;
}

SBPlatform SBTarget::GetPlatform() {
  TargetSP target_sp(GetSP());
  if (!target_sp)
    return SBPlatform();

  SBPlatform platform;
  platform.m_opaque_sp = target_sp->GetPlatform();

  return platform;
}

SBDebugger SBTarget::GetDebugger() const {
  SBDebugger debugger;
  TargetSP target_sp(GetSP());
  if (target_sp)
    debugger.reset(target_sp->GetDebugger().shared_from_this());
  return debugger;
}

SBStructuredData SBTarget::GetStatistics() {
  SBStructuredData data;
  TargetSP target_sp(GetSP());
  if (!target_sp)
    return data;

  auto stats_up = llvm::make_unique<StructuredData::Dictionary>();
  int i = 0;
  for (auto &Entry : target_sp->GetStatistics()) {
    std::string Desc = lldb_private::GetStatDescription(
        static_cast<lldb_private::StatisticKind>(i));
    stats_up->AddIntegerItem(Desc, Entry);
    i += 1;
  }

  data.m_impl_up->SetObjectSP(std::move(stats_up));
  return data;
}

void SBTarget::SetCollectingStats(bool v) {
  TargetSP target_sp(GetSP());
  if (!target_sp)
    return;
  return target_sp->SetCollectingStats(v);
}

bool SBTarget::GetCollectingStats() {
  TargetSP target_sp(GetSP());
  if (!target_sp)
    return false;
  return target_sp->GetCollectingStats();
}


SBProcess SBTarget::LoadCore(const char *core_file) {
  lldb::SBError error; // Ignored
  return LoadCore(core_file, error);
}

SBProcess SBTarget::LoadCore(const char *core_file, lldb::SBError &error) {
  SBProcess sb_process;
  TargetSP target_sp(GetSP());
  if (target_sp) {
    FileSpec filespec(core_file);
    FileSystem::Instance().Resolve(filespec);
    ProcessSP process_sp(target_sp->CreateProcess(
        target_sp->GetDebugger().GetListener(), "", &filespec));
    if (process_sp) {
      error.SetError(process_sp->LoadCore());
      if (error.Success())
        sb_process.SetSP(process_sp);
    } else {
      error.SetErrorString("Failed to create the process");
    }
  } else {
    error.SetErrorString("SBTarget is invalid");
  }
  return sb_process;
}

SBProcess SBTarget::LaunchSimple(char const **argv, char const **envp,
                                 const char *working_directory) {
  char *stdin_path = NULL;
  char *stdout_path = NULL;
  char *stderr_path = NULL;
  uint32_t launch_flags = 0;
  bool stop_at_entry = false;
  SBError error;
  SBListener listener = GetDebugger().GetListener();
  return Launch(listener, argv, envp, stdin_path, stdout_path, stderr_path,
                working_directory, launch_flags, stop_at_entry, error);
}

SBError SBTarget::Install() {
  SBError sb_error;
  TargetSP target_sp(GetSP());
  if (target_sp) {
    std::lock_guard<std::recursive_mutex> guard(target_sp->GetAPIMutex());
    sb_error.ref() = target_sp->Install(NULL);
  }
  return sb_error;
}

SBProcess SBTarget::Launch(SBListener &listener, char const **argv,
                           char const **envp, const char *stdin_path,
                           const char *stdout_path, const char *stderr_path,
                           const char *working_directory,
                           uint32_t launch_flags, // See LaunchFlags
                           bool stop_at_entry, lldb::SBError &error) {
  Log *log(lldb_private::GetLogIfAllCategoriesSet(LIBLLDB_LOG_API));

  SBProcess sb_process;
  ProcessSP process_sp;
  TargetSP target_sp(GetSP());

  if (log)
    log->Printf("SBTarget(%p)::Launch (argv=%p, envp=%p, stdin=%s, stdout=%s, "
                "stderr=%s, working-dir=%s, launch_flags=0x%x, "
                "stop_at_entry=%i, &error (%p))...",
                static_cast<void *>(target_sp.get()), static_cast<void *>(argv),
                static_cast<void *>(envp), stdin_path ? stdin_path : "NULL",
                stdout_path ? stdout_path : "NULL",
                stderr_path ? stderr_path : "NULL",
                working_directory ? working_directory : "NULL", launch_flags,
                stop_at_entry, static_cast<void *>(error.get()));

  if (target_sp) {
    std::lock_guard<std::recursive_mutex> guard(target_sp->GetAPIMutex());

    if (stop_at_entry)
      launch_flags |= eLaunchFlagStopAtEntry;

    if (getenv("LLDB_LAUNCH_FLAG_DISABLE_ASLR"))
      launch_flags |= eLaunchFlagDisableASLR;

    StateType state = eStateInvalid;
    process_sp = target_sp->GetProcessSP();
    if (process_sp) {
      state = process_sp->GetState();

      if (process_sp->IsAlive() && state != eStateConnected) {
        if (state == eStateAttaching)
          error.SetErrorString("process attach is in progress");
        else
          error.SetErrorString("a process is already being debugged");
        return sb_process;
      }
    }

    if (state == eStateConnected) {
      // If we are already connected, then we have already specified the
      // listener, so if a valid listener is supplied, we need to error out to
      // let the client know.
      if (listener.IsValid()) {
        error.SetErrorString("process is connected and already has a listener, "
                             "pass empty listener");
        return sb_process;
      }
    }

    if (getenv("LLDB_LAUNCH_FLAG_DISABLE_STDIO"))
      launch_flags |= eLaunchFlagDisableSTDIO;

    ProcessLaunchInfo launch_info(FileSpec(stdin_path), FileSpec(stdout_path),
                                  FileSpec(stderr_path),
                                  FileSpec(working_directory), launch_flags);

    Module *exe_module = target_sp->GetExecutableModulePointer();
    if (exe_module)
      launch_info.SetExecutableFile(exe_module->GetPlatformFileSpec(), true);
    if (argv)
      launch_info.GetArguments().AppendArguments(argv);
    if (envp)
      launch_info.GetEnvironment() = Environment(envp);

    if (listener.IsValid())
      launch_info.SetListener(listener.GetSP());

    error.SetError(target_sp->Launch(launch_info, NULL));

    sb_process.SetSP(target_sp->GetProcessSP());
  } else {
    error.SetErrorString("SBTarget is invalid");
  }

  log = lldb_private::GetLogIfAllCategoriesSet(LIBLLDB_LOG_API);
  if (log)
    log->Printf("SBTarget(%p)::Launch (...) => SBProcess(%p), SBError(%s)",
                static_cast<void *>(target_sp.get()),
                static_cast<void *>(sb_process.GetSP().get()),
                error.GetCString());

  return sb_process;
}

SBProcess SBTarget::Launch(SBLaunchInfo &sb_launch_info, SBError &error) {
  Log *log(lldb_private::GetLogIfAllCategoriesSet(LIBLLDB_LOG_API));

  SBProcess sb_process;
  TargetSP target_sp(GetSP());

  if (log)
    log->Printf("SBTarget(%p)::Launch (launch_info, error)...",
                static_cast<void *>(target_sp.get()));

  if (target_sp) {
    std::lock_guard<std::recursive_mutex> guard(target_sp->GetAPIMutex());
    StateType state = eStateInvalid;
    {
      ProcessSP process_sp = target_sp->GetProcessSP();
      if (process_sp) {
        state = process_sp->GetState();

        if (process_sp->IsAlive() && state != eStateConnected) {
          if (state == eStateAttaching)
            error.SetErrorString("process attach is in progress");
          else
            error.SetErrorString("a process is already being debugged");
          return sb_process;
        }
      }
    }

    lldb_private::ProcessLaunchInfo launch_info = sb_launch_info.ref();

    if (!launch_info.GetExecutableFile()) {
      Module *exe_module = target_sp->GetExecutableModulePointer();
      if (exe_module)
        launch_info.SetExecutableFile(exe_module->GetPlatformFileSpec(), true);
    }

    const ArchSpec &arch_spec = target_sp->GetArchitecture();
    if (arch_spec.IsValid())
      launch_info.GetArchitecture() = arch_spec;

    error.SetError(target_sp->Launch(launch_info, NULL));
    sb_launch_info.set_ref(launch_info);
    sb_process.SetSP(target_sp->GetProcessSP());
  } else {
    error.SetErrorString("SBTarget is invalid");
  }

  log = lldb_private::GetLogIfAllCategoriesSet(LIBLLDB_LOG_API);
  if (log)
    log->Printf("SBTarget(%p)::Launch (...) => SBProcess(%p)",
                static_cast<void *>(target_sp.get()),
                static_cast<void *>(sb_process.GetSP().get()));

  return sb_process;
}

lldb::SBProcess SBTarget::Attach(SBAttachInfo &sb_attach_info, SBError &error) {
  Log *log(lldb_private::GetLogIfAllCategoriesSet(LIBLLDB_LOG_API));

  SBProcess sb_process;
  TargetSP target_sp(GetSP());

  if (log)
    log->Printf("SBTarget(%p)::Attach (sb_attach_info, error)...",
                static_cast<void *>(target_sp.get()));

  if (target_sp) {
    ProcessAttachInfo &attach_info = sb_attach_info.ref();
    if (attach_info.ProcessIDIsValid() && !attach_info.UserIDIsValid()) {
      PlatformSP platform_sp = target_sp->GetPlatform();
      // See if we can pre-verify if a process exists or not
      if (platform_sp && platform_sp->IsConnected()) {
        lldb::pid_t attach_pid = attach_info.GetProcessID();
        ProcessInstanceInfo instance_info;
        if (platform_sp->GetProcessInfo(attach_pid, instance_info)) {
          attach_info.SetUserID(instance_info.GetEffectiveUserID());
        } else {
          error.ref().SetErrorStringWithFormat(
              "no process found with process ID %" PRIu64, attach_pid);
          if (log) {
            log->Printf("SBTarget(%p)::Attach (...) => error %s",
                        static_cast<void *>(target_sp.get()),
                        error.GetCString());
          }
          return sb_process;
        }
      }
    }
    error.SetError(AttachToProcess(attach_info, *target_sp));
    if (error.Success())
      sb_process.SetSP(target_sp->GetProcessSP());
  } else {
    error.SetErrorString("SBTarget is invalid");
  }

  if (log)
    log->Printf("SBTarget(%p)::Attach (...) => SBProcess(%p)",
                static_cast<void *>(target_sp.get()),
                static_cast<void *>(sb_process.GetSP().get()));

  return sb_process;
}

lldb::SBProcess SBTarget::AttachToProcessWithID(
    SBListener &listener,
    lldb::pid_t pid, // The process ID to attach to
    SBError &error   // An error explaining what went wrong if attach fails
    ) {
  Log *log(lldb_private::GetLogIfAllCategoriesSet(LIBLLDB_LOG_API));

  SBProcess sb_process;
  TargetSP target_sp(GetSP());

  if (log)
    log->Printf("SBTarget(%p)::%s (listener, pid=%" PRId64 ", error)...",
                static_cast<void *>(target_sp.get()), __FUNCTION__, pid);

  if (target_sp) {
    ProcessAttachInfo attach_info;
    attach_info.SetProcessID(pid);
    if (listener.IsValid())
      attach_info.SetListener(listener.GetSP());

    ProcessInstanceInfo instance_info;
    if (target_sp->GetPlatform()->GetProcessInfo(pid, instance_info))
      attach_info.SetUserID(instance_info.GetEffectiveUserID());

    error.SetError(AttachToProcess(attach_info, *target_sp));
    if (error.Success())
      sb_process.SetSP(target_sp->GetProcessSP());
  } else
    error.SetErrorString("SBTarget is invalid");

  if (log)
    log->Printf("SBTarget(%p)::%s (...) => SBProcess(%p)",
                static_cast<void *>(target_sp.get()), __FUNCTION__,
                static_cast<void *>(sb_process.GetSP().get()));
  return sb_process;
}

lldb::SBProcess SBTarget::AttachToProcessWithName(
    SBListener &listener,
    const char *name, // basename of process to attach to
    bool wait_for, // if true wait for a new instance of "name" to be launched
    SBError &error // An error explaining what went wrong if attach fails
    ) {
  Log *log(lldb_private::GetLogIfAllCategoriesSet(LIBLLDB_LOG_API));

  SBProcess sb_process;
  TargetSP target_sp(GetSP());

  if (log)
    log->Printf("SBTarget(%p)::%s (listener, name=%s, wait_for=%s, error)...",
                static_cast<void *>(target_sp.get()), __FUNCTION__, name,
                wait_for ? "true" : "false");

  if (name && target_sp) {
    ProcessAttachInfo attach_info;
    attach_info.GetExecutableFile().SetFile(name, FileSpec::Style::native);
    attach_info.SetWaitForLaunch(wait_for);
    if (listener.IsValid())
      attach_info.SetListener(listener.GetSP());

    error.SetError(AttachToProcess(attach_info, *target_sp));
    if (error.Success())
      sb_process.SetSP(target_sp->GetProcessSP());
  } else
    error.SetErrorString("SBTarget is invalid");

  if (log)
    log->Printf("SBTarget(%p)::%s (...) => SBProcess(%p)",
                static_cast<void *>(target_sp.get()), __FUNCTION__,
                static_cast<void *>(sb_process.GetSP().get()));
  return sb_process;
}

lldb::SBProcess SBTarget::ConnectRemote(SBListener &listener, const char *url,
                                        const char *plugin_name,
                                        SBError &error) {
  Log *log(lldb_private::GetLogIfAllCategoriesSet(LIBLLDB_LOG_API));

  SBProcess sb_process;
  ProcessSP process_sp;
  TargetSP target_sp(GetSP());

  if (log)
    log->Printf("SBTarget(%p)::ConnectRemote (listener, url=%s, "
                "plugin_name=%s, error)...",
                static_cast<void *>(target_sp.get()), url, plugin_name);

  if (target_sp) {
    std::lock_guard<std::recursive_mutex> guard(target_sp->GetAPIMutex());
    if (listener.IsValid())
      process_sp =
          target_sp->CreateProcess(listener.m_opaque_sp, plugin_name, NULL);
    else
      process_sp = target_sp->CreateProcess(
          target_sp->GetDebugger().GetListener(), plugin_name, NULL);

    if (process_sp) {
      sb_process.SetSP(process_sp);
      error.SetError(process_sp->ConnectRemote(NULL, url));
    } else {
      error.SetErrorString("unable to create lldb_private::Process");
    }
  } else {
    error.SetErrorString("SBTarget is invalid");
  }

  if (log)
    log->Printf("SBTarget(%p)::ConnectRemote (...) => SBProcess(%p)",
                static_cast<void *>(target_sp.get()),
                static_cast<void *>(process_sp.get()));
  return sb_process;
}

SBFileSpec SBTarget::GetExecutable() {

  SBFileSpec exe_file_spec;
  TargetSP target_sp(GetSP());
  if (target_sp) {
    Module *exe_module = target_sp->GetExecutableModulePointer();
    if (exe_module)
      exe_file_spec.SetFileSpec(exe_module->GetFileSpec());
  }

  Log *log(lldb_private::GetLogIfAllCategoriesSet(LIBLLDB_LOG_API));
  if (log) {
    log->Printf("SBTarget(%p)::GetExecutable () => SBFileSpec(%p)",
                static_cast<void *>(target_sp.get()),
                static_cast<const void *>(exe_file_spec.get()));
  }

  return exe_file_spec;
}

bool SBTarget::operator==(const SBTarget &rhs) const {
  return m_opaque_sp.get() == rhs.m_opaque_sp.get();
}

bool SBTarget::operator!=(const SBTarget &rhs) const {
  return m_opaque_sp.get() != rhs.m_opaque_sp.get();
}

lldb::TargetSP SBTarget::GetSP() const { return m_opaque_sp; }

void SBTarget::SetSP(const lldb::TargetSP &target_sp) {
  m_opaque_sp = target_sp;
}

lldb::SBAddress SBTarget::ResolveLoadAddress(lldb::addr_t vm_addr) {
  lldb::SBAddress sb_addr;
  Address &addr = sb_addr.ref();
  TargetSP target_sp(GetSP());
  if (target_sp) {
    std::lock_guard<std::recursive_mutex> guard(target_sp->GetAPIMutex());
    if (target_sp->ResolveLoadAddress(vm_addr, addr))
      return sb_addr;
  }

  // We have a load address that isn't in a section, just return an address
  // with the offset filled in (the address) and the section set to NULL
  addr.SetRawAddress(vm_addr);
  return sb_addr;
}

lldb::SBAddress SBTarget::ResolveFileAddress(lldb::addr_t file_addr) {
  lldb::SBAddress sb_addr;
  Address &addr = sb_addr.ref();
  TargetSP target_sp(GetSP());
  if (target_sp) {
    std::lock_guard<std::recursive_mutex> guard(target_sp->GetAPIMutex());
    if (target_sp->ResolveFileAddress(file_addr, addr))
      return sb_addr;
  }

  addr.SetRawAddress(file_addr);
  return sb_addr;
}

lldb::SBAddress SBTarget::ResolvePastLoadAddress(uint32_t stop_id,
                                                 lldb::addr_t vm_addr) {
  lldb::SBAddress sb_addr;
  Address &addr = sb_addr.ref();
  TargetSP target_sp(GetSP());
  if (target_sp) {
    std::lock_guard<std::recursive_mutex> guard(target_sp->GetAPIMutex());
    if (target_sp->ResolveLoadAddress(vm_addr, addr))
      return sb_addr;
  }

  // We have a load address that isn't in a section, just return an address
  // with the offset filled in (the address) and the section set to NULL
  addr.SetRawAddress(vm_addr);
  return sb_addr;
}

SBSymbolContext
SBTarget::ResolveSymbolContextForAddress(const SBAddress &addr,
                                         uint32_t resolve_scope) {
  SBSymbolContext sc;
  SymbolContextItem scope = static_cast<SymbolContextItem>(resolve_scope);
  if (addr.IsValid()) {
    TargetSP target_sp(GetSP());
    if (target_sp)
      target_sp->GetImages().ResolveSymbolContextForAddress(addr.ref(), scope,
                                                            sc.ref());
  }
  return sc;
}

size_t SBTarget::ReadMemory(const SBAddress addr, void *buf, size_t size,
                            lldb::SBError &error) {
  SBError sb_error;
  size_t bytes_read = 0;
  TargetSP target_sp(GetSP());
  if (target_sp) {
    std::lock_guard<std::recursive_mutex> guard(target_sp->GetAPIMutex());
    bytes_read =
        target_sp->ReadMemory(addr.ref(), false, buf, size, sb_error.ref());
  } else {
    sb_error.SetErrorString("invalid target");
  }

  return bytes_read;
}

SBBreakpoint SBTarget::BreakpointCreateByLocation(const char *file,
                                                  uint32_t line) {
  return SBBreakpoint(
      BreakpointCreateByLocation(SBFileSpec(file, false), line));
}

SBBreakpoint
SBTarget::BreakpointCreateByLocation(const SBFileSpec &sb_file_spec,
                                     uint32_t line) {
  return BreakpointCreateByLocation(sb_file_spec, line, 0);
}

SBBreakpoint
SBTarget::BreakpointCreateByLocation(const SBFileSpec &sb_file_spec,
                                     uint32_t line, lldb::addr_t offset) {
  SBFileSpecList empty_list;
  return BreakpointCreateByLocation(sb_file_spec, line, offset, empty_list);
}

SBBreakpoint
SBTarget::BreakpointCreateByLocation(const SBFileSpec &sb_file_spec,
                                     uint32_t line, lldb::addr_t offset,
                                     SBFileSpecList &sb_module_list) {
  return BreakpointCreateByLocation(sb_file_spec, line, 0, offset,
                                    sb_module_list);
}

SBBreakpoint SBTarget::BreakpointCreateByLocation(
    const SBFileSpec &sb_file_spec, uint32_t line, uint32_t column,
    lldb::addr_t offset, SBFileSpecList &sb_module_list) {
  Log *log(lldb_private::GetLogIfAllCategoriesSet(LIBLLDB_LOG_API));

  SBBreakpoint sb_bp;
  TargetSP target_sp(GetSP());
  if (target_sp && line != 0) {
    std::lock_guard<std::recursive_mutex> guard(target_sp->GetAPIMutex());

    const LazyBool check_inlines = eLazyBoolCalculate;
    const LazyBool skip_prologue = eLazyBoolCalculate;
    const bool internal = false;
    const bool hardware = false;
    const LazyBool move_to_nearest_code = eLazyBoolCalculate;
    const FileSpecList *module_list = nullptr;
    if (sb_module_list.GetSize() > 0) {
      module_list = sb_module_list.get();
    }
    sb_bp = target_sp->CreateBreakpoint(
        module_list, *sb_file_spec, line, column, offset, check_inlines,
        skip_prologue, internal, hardware, move_to_nearest_code);
  }

  if (log) {
    SBStream sstr;
    sb_bp.GetDescription(sstr);
    char path[PATH_MAX];
    sb_file_spec->GetPath(path, sizeof(path));
    log->Printf("SBTarget(%p)::BreakpointCreateByLocation ( %s:%u ) => "
                "SBBreakpoint(%p): %s",
                static_cast<void *>(target_sp.get()), path, line,
                static_cast<void *>(sb_bp.GetSP().get()), sstr.GetData());
  }

  return sb_bp;
}

SBBreakpoint SBTarget::BreakpointCreateByName(const char *symbol_name,
                                              const char *module_name) {
  Log *log(lldb_private::GetLogIfAllCategoriesSet(LIBLLDB_LOG_API));

  SBBreakpoint sb_bp;
  TargetSP target_sp(GetSP());
  if (target_sp.get()) {
    std::lock_guard<std::recursive_mutex> guard(target_sp->GetAPIMutex());

    const bool internal = false;
    const bool hardware = false;
    const LazyBool skip_prologue = eLazyBoolCalculate;
    const lldb::addr_t offset = 0;
    if (module_name && module_name[0]) {
      FileSpecList module_spec_list;
      module_spec_list.Append(FileSpec(module_name));
      sb_bp = target_sp->CreateBreakpoint(
          &module_spec_list, NULL, symbol_name, eFunctionNameTypeAuto,
          eLanguageTypeUnknown, offset, skip_prologue, internal, hardware);
    } else {
      sb_bp = target_sp->CreateBreakpoint(
          NULL, NULL, symbol_name, eFunctionNameTypeAuto, eLanguageTypeUnknown,
          offset, skip_prologue, internal, hardware);
    }
  }

  if (log)
    log->Printf("SBTarget(%p)::BreakpointCreateByName (symbol=\"%s\", "
                "module=\"%s\") => SBBreakpoint(%p)",
                static_cast<void *>(target_sp.get()), symbol_name, module_name,
                static_cast<void *>(sb_bp.GetSP().get()));

  return sb_bp;
}

lldb::SBBreakpoint
SBTarget::BreakpointCreateByName(const char *symbol_name,
                                 const SBFileSpecList &module_list,
                                 const SBFileSpecList &comp_unit_list) {
  lldb::FunctionNameType name_type_mask = eFunctionNameTypeAuto;
  return BreakpointCreateByName(symbol_name, name_type_mask,
                                eLanguageTypeUnknown, module_list,
                                comp_unit_list);
}

lldb::SBBreakpoint SBTarget::BreakpointCreateByName(
    const char *symbol_name, uint32_t name_type_mask,
    const SBFileSpecList &module_list, const SBFileSpecList &comp_unit_list) {
  return BreakpointCreateByName(symbol_name, name_type_mask,
                                eLanguageTypeUnknown, module_list,
                                comp_unit_list);
}

lldb::SBBreakpoint SBTarget::BreakpointCreateByName(
    const char *symbol_name, uint32_t name_type_mask,
    LanguageType symbol_language, const SBFileSpecList &module_list,
    const SBFileSpecList &comp_unit_list) {
  Log *log(lldb_private::GetLogIfAllCategoriesSet(LIBLLDB_LOG_API));

  SBBreakpoint sb_bp;
  TargetSP target_sp(GetSP());
  if (target_sp && symbol_name && symbol_name[0]) {
    const bool internal = false;
    const bool hardware = false;
    const LazyBool skip_prologue = eLazyBoolCalculate;
    std::lock_guard<std::recursive_mutex> guard(target_sp->GetAPIMutex());
    FunctionNameType mask = static_cast<FunctionNameType>(name_type_mask);
    sb_bp = target_sp->CreateBreakpoint(module_list.get(), comp_unit_list.get(),
                                        symbol_name, mask, symbol_language, 0,
                                        skip_prologue, internal, hardware);
  }

  if (log)
    log->Printf("SBTarget(%p)::BreakpointCreateByName (symbol=\"%s\", "
                "name_type: %d) => SBBreakpoint(%p)",
                static_cast<void *>(target_sp.get()), symbol_name,
                name_type_mask, static_cast<void *>(sb_bp.GetSP().get()));

  return sb_bp;
}

lldb::SBBreakpoint SBTarget::BreakpointCreateByNames(
    const char *symbol_names[], uint32_t num_names, uint32_t name_type_mask,
    const SBFileSpecList &module_list, const SBFileSpecList &comp_unit_list) {
  return BreakpointCreateByNames(symbol_names, num_names, name_type_mask,
                                 eLanguageTypeUnknown, module_list,
                                 comp_unit_list);
}

lldb::SBBreakpoint SBTarget::BreakpointCreateByNames(
    const char *symbol_names[], uint32_t num_names, uint32_t name_type_mask,
    LanguageType symbol_language, const SBFileSpecList &module_list,
    const SBFileSpecList &comp_unit_list) {
  return BreakpointCreateByNames(symbol_names, num_names, name_type_mask,
                                 eLanguageTypeUnknown, 0, module_list,
                                 comp_unit_list);
}

lldb::SBBreakpoint SBTarget::BreakpointCreateByNames(
    const char *symbol_names[], uint32_t num_names, uint32_t name_type_mask,
    LanguageType symbol_language, lldb::addr_t offset,
    const SBFileSpecList &module_list, const SBFileSpecList &comp_unit_list) {
  Log *log(lldb_private::GetLogIfAllCategoriesSet(LIBLLDB_LOG_API));

  SBBreakpoint sb_bp;
  TargetSP target_sp(GetSP());
  if (target_sp && num_names > 0) {
    std::lock_guard<std::recursive_mutex> guard(target_sp->GetAPIMutex());
    const bool internal = false;
    const bool hardware = false;
    FunctionNameType mask = static_cast<FunctionNameType>(name_type_mask);
    const LazyBool skip_prologue = eLazyBoolCalculate;
    sb_bp = target_sp->CreateBreakpoint(
        module_list.get(), comp_unit_list.get(), symbol_names, num_names, mask,
        symbol_language, offset, skip_prologue, internal, hardware);
  }

  if (log) {
    log->Printf("SBTarget(%p)::BreakpointCreateByName (symbols={",
                static_cast<void *>(target_sp.get()));
    for (uint32_t i = 0; i < num_names; i++) {
      char sep;
      if (i < num_names - 1)
        sep = ',';
      else
        sep = '}';
      if (symbol_names[i] != NULL)
        log->Printf("\"%s\"%c ", symbol_names[i], sep);
      else
        log->Printf("\"<NULL>\"%c ", sep);
    }
    log->Printf("name_type: %d) => SBBreakpoint(%p)", name_type_mask,
                static_cast<void *>(sb_bp.GetSP().get()));
  }

  return sb_bp;
}

SBBreakpoint SBTarget::BreakpointCreateByRegex(const char *symbol_name_regex,
                                               const char *module_name) {
  SBFileSpecList module_spec_list;
  SBFileSpecList comp_unit_list;
  if (module_name && module_name[0]) {
    module_spec_list.Append(FileSpec(module_name));
  }
  return BreakpointCreateByRegex(symbol_name_regex, eLanguageTypeUnknown,
                                 module_spec_list, comp_unit_list);
}

lldb::SBBreakpoint
SBTarget::BreakpointCreateByRegex(const char *symbol_name_regex,
                                  const SBFileSpecList &module_list,
                                  const SBFileSpecList &comp_unit_list) {
  return BreakpointCreateByRegex(symbol_name_regex, eLanguageTypeUnknown,
                                 module_list, comp_unit_list);
}

lldb::SBBreakpoint SBTarget::BreakpointCreateByRegex(
    const char *symbol_name_regex, LanguageType symbol_language,
    const SBFileSpecList &module_list, const SBFileSpecList &comp_unit_list) {
  Log *log(lldb_private::GetLogIfAllCategoriesSet(LIBLLDB_LOG_API));

  SBBreakpoint sb_bp;
  TargetSP target_sp(GetSP());
  if (target_sp && symbol_name_regex && symbol_name_regex[0]) {
    std::lock_guard<std::recursive_mutex> guard(target_sp->GetAPIMutex());
    RegularExpression regexp((llvm::StringRef(symbol_name_regex)));
    const bool internal = false;
    const bool hardware = false;
    const LazyBool skip_prologue = eLazyBoolCalculate;

    sb_bp = target_sp->CreateFuncRegexBreakpoint(
        module_list.get(), comp_unit_list.get(), regexp, symbol_language,
        skip_prologue, internal, hardware);
  }

  if (log)
    log->Printf("SBTarget(%p)::BreakpointCreateByRegex (symbol_regex=\"%s\") "
                "=> SBBreakpoint(%p)",
                static_cast<void *>(target_sp.get()), symbol_name_regex,
                static_cast<void *>(sb_bp.GetSP().get()));

  return sb_bp;
}

SBBreakpoint SBTarget::BreakpointCreateByAddress(addr_t address) {
  Log *log(lldb_private::GetLogIfAllCategoriesSet(LIBLLDB_LOG_API));

  SBBreakpoint sb_bp;
  TargetSP target_sp(GetSP());
  if (target_sp) {
    std::lock_guard<std::recursive_mutex> guard(target_sp->GetAPIMutex());
    const bool hardware = false;
    sb_bp = target_sp->CreateBreakpoint(address, false, hardware);
  }

  if (log)
    log->Printf("SBTarget(%p)::BreakpointCreateByAddress (address=%" PRIu64
                ") => SBBreakpoint(%p)",
                static_cast<void *>(target_sp.get()),
                static_cast<uint64_t>(address),
                static_cast<void *>(sb_bp.GetSP().get()));

  return sb_bp;
}

SBBreakpoint SBTarget::BreakpointCreateBySBAddress(SBAddress &sb_address) {
  Log *log(lldb_private::GetLogIfAllCategoriesSet(LIBLLDB_LOG_API));

  SBBreakpoint sb_bp;
  TargetSP target_sp(GetSP());
  if (!sb_address.IsValid()) {
    if (log)
      log->Printf("SBTarget(%p)::BreakpointCreateBySBAddress called with "
                  "invalid address",
                  static_cast<void *>(target_sp.get()));
    return sb_bp;
  }

  if (target_sp) {
    std::lock_guard<std::recursive_mutex> guard(target_sp->GetAPIMutex());
    const bool hardware = false;
    sb_bp = target_sp->CreateBreakpoint(sb_address.ref(), false, hardware);
  }

  if (log) {
    SBStream s;
    sb_address.GetDescription(s);
    log->Printf("SBTarget(%p)::BreakpointCreateBySBAddress (address=%s) => "
                "SBBreakpoint(%p)",
                static_cast<void *>(target_sp.get()), s.GetData(),
                static_cast<void *>(sb_bp.GetSP().get()));
  }

  return sb_bp;
}

lldb::SBBreakpoint
SBTarget::BreakpointCreateBySourceRegex(const char *source_regex,
                                        const lldb::SBFileSpec &source_file,
                                        const char *module_name) {
  SBFileSpecList module_spec_list;

  if (module_name && module_name[0]) {
    module_spec_list.Append(FileSpec(module_name));
  }

  SBFileSpecList source_file_list;
  if (source_file.IsValid()) {
    source_file_list.Append(source_file);
  }

  return BreakpointCreateBySourceRegex(source_regex, module_spec_list,
                                       source_file_list);
}

lldb::SBBreakpoint SBTarget::BreakpointCreateBySourceRegex(
    const char *source_regex, const SBFileSpecList &module_list,
    const lldb::SBFileSpecList &source_file_list) {
  return BreakpointCreateBySourceRegex(source_regex, module_list,
                                       source_file_list, SBStringList());
}

lldb::SBBreakpoint SBTarget::BreakpointCreateBySourceRegex(
    const char *source_regex, const SBFileSpecList &module_list,
    const lldb::SBFileSpecList &source_file_list,
    const SBStringList &func_names) {
  Log *log(lldb_private::GetLogIfAllCategoriesSet(LIBLLDB_LOG_API));

  SBBreakpoint sb_bp;
  TargetSP target_sp(GetSP());
  if (target_sp && source_regex && source_regex[0]) {
    std::lock_guard<std::recursive_mutex> guard(target_sp->GetAPIMutex());
    const bool hardware = false;
    const LazyBool move_to_nearest_code = eLazyBoolCalculate;
    RegularExpression regexp((llvm::StringRef(source_regex)));
    std::unordered_set<std::string> func_names_set;
    for (size_t i = 0; i < func_names.GetSize(); i++) {
      func_names_set.insert(func_names.GetStringAtIndex(i));
    }

    sb_bp = target_sp->CreateSourceRegexBreakpoint(
        module_list.get(), source_file_list.get(), func_names_set, regexp,
        false, hardware, move_to_nearest_code);
  }

  if (log)
    log->Printf("SBTarget(%p)::BreakpointCreateByRegex (source_regex=\"%s\") "
                "=> SBBreakpoint(%p)",
                static_cast<void *>(target_sp.get()), source_regex,
                static_cast<void *>(sb_bp.GetSP().get()));

  return sb_bp;
}

lldb::SBBreakpoint
SBTarget::BreakpointCreateForException(lldb::LanguageType language,
                                       bool catch_bp, bool throw_bp) {
  Log *log(lldb_private::GetLogIfAllCategoriesSet(LIBLLDB_LOG_API));

  SBBreakpoint sb_bp;
  TargetSP target_sp(GetSP());
  if (target_sp) {
    std::lock_guard<std::recursive_mutex> guard(target_sp->GetAPIMutex());
    const bool hardware = false;
    sb_bp = target_sp->CreateExceptionBreakpoint(language, catch_bp, throw_bp,
                                                  hardware);
  }

  if (log)
    log->Printf("SBTarget(%p)::BreakpointCreateForException (Language: %s, catch: "
                "%s throw: %s) => SBBreakpoint(%p)",
                static_cast<void *>(target_sp.get()),
                Language::GetNameForLanguageType(language),
                catch_bp ? "on" : "off", throw_bp ? "on" : "off",
                static_cast<void *>(sb_bp.GetSP().get()));

  return sb_bp;
}

lldb::SBBreakpoint
SBTarget::BreakpointCreateFromScript(const char *class_name,
                                     SBStructuredData &extra_args,
                                     const SBFileSpecList &module_list,
                                     const SBFileSpecList &file_list,
                                     bool request_hardware)
{
  Log *log(lldb_private::GetLogIfAllCategoriesSet(LIBLLDB_LOG_API));

  SBBreakpoint sb_bp;
  TargetSP target_sp(GetSP());
  if (target_sp) {
    std::lock_guard<std::recursive_mutex> guard(target_sp->GetAPIMutex());
    Status error;
    
    StructuredData::ObjectSP obj_sp = extra_args.m_impl_up->GetObjectSP();
    sb_bp =
        target_sp->CreateScriptedBreakpoint(class_name,
                                            module_list.get(),
                                            file_list.get(),
                                            false, /* internal */
                                            request_hardware,
                                            obj_sp,
                                            &error);
  }
  if (log)
    log->Printf("SBTarget(%p)::BreakpointCreateFromScript (class name: %s) "
                " => SBBreakpoint(%p)",
                static_cast<void *>(target_sp.get()),
                class_name,
                static_cast<void *>(sb_bp.GetSP().get()));

  return sb_bp;
}


uint32_t SBTarget::GetNumBreakpoints() const {
  TargetSP target_sp(GetSP());
  if (target_sp) {
    // The breakpoint list is thread safe, no need to lock
    return target_sp->GetBreakpointList().GetSize();
  }
  return 0;
}

SBBreakpoint SBTarget::GetBreakpointAtIndex(uint32_t idx) const {
  SBBreakpoint sb_breakpoint;
  TargetSP target_sp(GetSP());
  if (target_sp) {
    // The breakpoint list is thread safe, no need to lock
    sb_breakpoint = target_sp->GetBreakpointList().GetBreakpointAtIndex(idx);
  }
  return sb_breakpoint;
}

bool SBTarget::BreakpointDelete(break_id_t bp_id) {
  Log *log(lldb_private::GetLogIfAllCategoriesSet(LIBLLDB_LOG_API));

  bool result = false;
  TargetSP target_sp(GetSP());
  if (target_sp) {
    std::lock_guard<std::recursive_mutex> guard(target_sp->GetAPIMutex());
    result = target_sp->RemoveBreakpointByID(bp_id);
  }

  if (log)
    log->Printf("SBTarget(%p)::BreakpointDelete (bp_id=%d) => %i",
                static_cast<void *>(target_sp.get()),
                static_cast<uint32_t>(bp_id), result);

  return result;
}

SBBreakpoint SBTarget::FindBreakpointByID(break_id_t bp_id) {
  Log *log(lldb_private::GetLogIfAllCategoriesSet(LIBLLDB_LOG_API));

  SBBreakpoint sb_breakpoint;
  TargetSP target_sp(GetSP());
  if (target_sp && bp_id != LLDB_INVALID_BREAK_ID) {
    std::lock_guard<std::recursive_mutex> guard(target_sp->GetAPIMutex());
    sb_breakpoint = target_sp->GetBreakpointByID(bp_id);
  }

  if (log)
    log->Printf(
        "SBTarget(%p)::FindBreakpointByID (bp_id=%d) => SBBreakpoint(%p)",
        static_cast<void *>(target_sp.get()), static_cast<uint32_t>(bp_id),
        static_cast<void *>(sb_breakpoint.GetSP().get()));

  return sb_breakpoint;
}

bool SBTarget::FindBreakpointsByName(const char *name,
                                     SBBreakpointList &bkpts) {
  TargetSP target_sp(GetSP());
  if (target_sp) {
    std::lock_guard<std::recursive_mutex> guard(target_sp->GetAPIMutex());
    BreakpointList bkpt_list(false);
    bool is_valid =
        target_sp->GetBreakpointList().FindBreakpointsByName(name, bkpt_list);
    if (!is_valid)
      return false;
    for (BreakpointSP bkpt_sp : bkpt_list.Breakpoints()) {
      bkpts.AppendByID(bkpt_sp->GetID());
    }
  }
  return true;
}

void SBTarget::GetBreakpointNames(SBStringList &names)
{
  names.Clear();

  TargetSP target_sp(GetSP());
  if (target_sp) {
    std::lock_guard<std::recursive_mutex> guard(target_sp->GetAPIMutex());

    std::vector<std::string> name_vec;
    target_sp->GetBreakpointNames(name_vec);
    for (auto name : name_vec)
      names.AppendString(name.c_str());
  }
}

void SBTarget::DeleteBreakpointName(const char *name)
{
  TargetSP target_sp(GetSP());
  if (target_sp) {
    std::lock_guard<std::recursive_mutex> guard(target_sp->GetAPIMutex());
    target_sp->DeleteBreakpointName(ConstString(name));
  }
}

bool SBTarget::EnableAllBreakpoints() {
  TargetSP target_sp(GetSP());
  if (target_sp) {
    std::lock_guard<std::recursive_mutex> guard(target_sp->GetAPIMutex());
    target_sp->EnableAllowedBreakpoints();
    return true;
  }
  return false;
}

bool SBTarget::DisableAllBreakpoints() {
  TargetSP target_sp(GetSP());
  if (target_sp) {
    std::lock_guard<std::recursive_mutex> guard(target_sp->GetAPIMutex());
    target_sp->DisableAllowedBreakpoints();
    return true;
  }
  return false;
}

bool SBTarget::DeleteAllBreakpoints() {
  TargetSP target_sp(GetSP());
  if (target_sp) {
    std::lock_guard<std::recursive_mutex> guard(target_sp->GetAPIMutex());
    target_sp->RemoveAllowedBreakpoints();
    return true;
  }
  return false;
}

lldb::SBError SBTarget::BreakpointsCreateFromFile(SBFileSpec &source_file,
                                                  SBBreakpointList &new_bps) {
  SBStringList empty_name_list;
  return BreakpointsCreateFromFile(source_file, empty_name_list, new_bps);
}

lldb::SBError SBTarget::BreakpointsCreateFromFile(SBFileSpec &source_file,
                                                  SBStringList &matching_names,
                                                  SBBreakpointList &new_bps) {
  SBError sberr;
  TargetSP target_sp(GetSP());
  if (!target_sp) {
    sberr.SetErrorString(
        "BreakpointCreateFromFile called with invalid target.");
    return sberr;
  }
  std::lock_guard<std::recursive_mutex> guard(target_sp->GetAPIMutex());

  BreakpointIDList bp_ids;

  std::vector<std::string> name_vector;
  size_t num_names = matching_names.GetSize();
  for (size_t i = 0; i < num_names; i++)
    name_vector.push_back(matching_names.GetStringAtIndex(i));

  sberr.ref() = target_sp->CreateBreakpointsFromFile(source_file.ref(),
                                                     name_vector, bp_ids);
  if (sberr.Fail())
    return sberr;

  size_t num_bkpts = bp_ids.GetSize();
  for (size_t i = 0; i < num_bkpts; i++) {
    BreakpointID bp_id = bp_ids.GetBreakpointIDAtIndex(i);
    new_bps.AppendByID(bp_id.GetBreakpointID());
  }
  return sberr;
}

lldb::SBError SBTarget::BreakpointsWriteToFile(SBFileSpec &dest_file) {
  SBError sberr;
  TargetSP target_sp(GetSP());
  if (!target_sp) {
    sberr.SetErrorString("BreakpointWriteToFile called with invalid target.");
    return sberr;
  }
  SBBreakpointList bkpt_list(*this);
  return BreakpointsWriteToFile(dest_file, bkpt_list);
}

lldb::SBError SBTarget::BreakpointsWriteToFile(SBFileSpec &dest_file,
                                               SBBreakpointList &bkpt_list,
                                               bool append) {
  SBError sberr;
  TargetSP target_sp(GetSP());
  if (!target_sp) {
    sberr.SetErrorString("BreakpointWriteToFile called with invalid target.");
    return sberr;
  }

  std::lock_guard<std::recursive_mutex> guard(target_sp->GetAPIMutex());
  BreakpointIDList bp_id_list;
  bkpt_list.CopyToBreakpointIDList(bp_id_list);
  sberr.ref() = target_sp->SerializeBreakpointsToFile(dest_file.ref(),
                                                      bp_id_list, append);
  return sberr;
}

uint32_t SBTarget::GetNumWatchpoints() const {
  TargetSP target_sp(GetSP());
  if (target_sp) {
    // The watchpoint list is thread safe, no need to lock
    return target_sp->GetWatchpointList().GetSize();
  }
  return 0;
}

SBWatchpoint SBTarget::GetWatchpointAtIndex(uint32_t idx) const {
  SBWatchpoint sb_watchpoint;
  TargetSP target_sp(GetSP());
  if (target_sp) {
    // The watchpoint list is thread safe, no need to lock
    sb_watchpoint.SetSP(target_sp->GetWatchpointList().GetByIndex(idx));
  }
  return sb_watchpoint;
}

bool SBTarget::DeleteWatchpoint(watch_id_t wp_id) {
  Log *log(lldb_private::GetLogIfAllCategoriesSet(LIBLLDB_LOG_API));

  bool result = false;
  TargetSP target_sp(GetSP());
  if (target_sp) {
    std::lock_guard<std::recursive_mutex> guard(target_sp->GetAPIMutex());
    std::unique_lock<std::recursive_mutex> lock;
    target_sp->GetWatchpointList().GetListMutex(lock);
    result = target_sp->RemoveWatchpointByID(wp_id);
  }

  if (log)
    log->Printf("SBTarget(%p)::WatchpointDelete (wp_id=%d) => %i",
                static_cast<void *>(target_sp.get()),
                static_cast<uint32_t>(wp_id), result);

  return result;
}

SBWatchpoint SBTarget::FindWatchpointByID(lldb::watch_id_t wp_id) {
  Log *log(lldb_private::GetLogIfAllCategoriesSet(LIBLLDB_LOG_API));

  SBWatchpoint sb_watchpoint;
  lldb::WatchpointSP watchpoint_sp;
  TargetSP target_sp(GetSP());
  if (target_sp && wp_id != LLDB_INVALID_WATCH_ID) {
    std::lock_guard<std::recursive_mutex> guard(target_sp->GetAPIMutex());
    std::unique_lock<std::recursive_mutex> lock;
    target_sp->GetWatchpointList().GetListMutex(lock);
    watchpoint_sp = target_sp->GetWatchpointList().FindByID(wp_id);
    sb_watchpoint.SetSP(watchpoint_sp);
  }

  if (log)
    log->Printf(
        "SBTarget(%p)::FindWatchpointByID (bp_id=%d) => SBWatchpoint(%p)",
        static_cast<void *>(target_sp.get()), static_cast<uint32_t>(wp_id),
        static_cast<void *>(watchpoint_sp.get()));

  return sb_watchpoint;
}

lldb::SBWatchpoint SBTarget::WatchAddress(lldb::addr_t addr, size_t size,
                                          bool read, bool write,
                                          SBError &error) {
  Log *log(lldb_private::GetLogIfAllCategoriesSet(LIBLLDB_LOG_API));

  SBWatchpoint sb_watchpoint;
  lldb::WatchpointSP watchpoint_sp;
  TargetSP target_sp(GetSP());
  if (target_sp && (read || write) && addr != LLDB_INVALID_ADDRESS &&
      size > 0) {
    std::lock_guard<std::recursive_mutex> guard(target_sp->GetAPIMutex());
    uint32_t watch_type = 0;
    if (read)
      watch_type |= LLDB_WATCH_TYPE_READ;
    if (write)
      watch_type |= LLDB_WATCH_TYPE_WRITE;
    if (watch_type == 0) {
      error.SetErrorString(
          "Can't create a watchpoint that is neither read nor write.");
      return sb_watchpoint;
    }

    // Target::CreateWatchpoint() is thread safe.
    Status cw_error;
    // This API doesn't take in a type, so we can't figure out what it is.
    CompilerType *type = NULL;
    watchpoint_sp =
        target_sp->CreateWatchpoint(addr, size, type, watch_type, cw_error);
    error.SetError(cw_error);
    sb_watchpoint.SetSP(watchpoint_sp);
  }

  if (log)
    log->Printf("SBTarget(%p)::WatchAddress (addr=0x%" PRIx64
                ", 0x%u) => SBWatchpoint(%p)",
                static_cast<void *>(target_sp.get()), addr,
                static_cast<uint32_t>(size),
                static_cast<void *>(watchpoint_sp.get()));

  return sb_watchpoint;
}

bool SBTarget::EnableAllWatchpoints() {
  TargetSP target_sp(GetSP());
  if (target_sp) {
    std::lock_guard<std::recursive_mutex> guard(target_sp->GetAPIMutex());
    std::unique_lock<std::recursive_mutex> lock;
    target_sp->GetWatchpointList().GetListMutex(lock);
    target_sp->EnableAllWatchpoints();
    return true;
  }
  return false;
}

bool SBTarget::DisableAllWatchpoints() {
  TargetSP target_sp(GetSP());
  if (target_sp) {
    std::lock_guard<std::recursive_mutex> guard(target_sp->GetAPIMutex());
    std::unique_lock<std::recursive_mutex> lock;
    target_sp->GetWatchpointList().GetListMutex(lock);
    target_sp->DisableAllWatchpoints();
    return true;
  }
  return false;
}

SBValue SBTarget::CreateValueFromAddress(const char *name, SBAddress addr,
                                         SBType type) {
  SBValue sb_value;
  lldb::ValueObjectSP new_value_sp;
  if (IsValid() && name && *name && addr.IsValid() && type.IsValid()) {
    lldb::addr_t load_addr(addr.GetLoadAddress(*this));
    ExecutionContext exe_ctx(
        ExecutionContextRef(ExecutionContext(m_opaque_sp.get(), false)));
    CompilerType ast_type(type.GetSP()->GetCompilerType(true));
    new_value_sp = ValueObject::CreateValueObjectFromAddress(name, load_addr,
                                                             exe_ctx, ast_type);
  }
  sb_value.SetSP(new_value_sp);
  Log *log(lldb_private::GetLogIfAllCategoriesSet(LIBLLDB_LOG_API));
  if (log) {
    if (new_value_sp)
      log->Printf("SBTarget(%p)::CreateValueFromAddress => \"%s\"",
                  static_cast<void *>(m_opaque_sp.get()),
                  new_value_sp->GetName().AsCString());
    else
      log->Printf("SBTarget(%p)::CreateValueFromAddress => NULL",
                  static_cast<void *>(m_opaque_sp.get()));
  }
  return sb_value;
}

lldb::SBValue SBTarget::CreateValueFromData(const char *name, lldb::SBData data,
                                            lldb::SBType type) {
  SBValue sb_value;
  lldb::ValueObjectSP new_value_sp;
  if (IsValid() && name && *name && data.IsValid() && type.IsValid()) {
    DataExtractorSP extractor(*data);
    ExecutionContext exe_ctx(
        ExecutionContextRef(ExecutionContext(m_opaque_sp.get(), false)));
    CompilerType ast_type(type.GetSP()->GetCompilerType(true));
    new_value_sp = ValueObject::CreateValueObjectFromData(name, *extractor,
                                                          exe_ctx, ast_type);
  }
  sb_value.SetSP(new_value_sp);
  Log *log(lldb_private::GetLogIfAllCategoriesSet(LIBLLDB_LOG_API));
  if (log) {
    if (new_value_sp)
      log->Printf("SBTarget(%p)::CreateValueFromData => \"%s\"",
                  static_cast<void *>(m_opaque_sp.get()),
                  new_value_sp->GetName().AsCString());
    else
      log->Printf("SBTarget(%p)::CreateValueFromData => NULL",
                  static_cast<void *>(m_opaque_sp.get()));
  }
  return sb_value;
}

lldb::SBValue SBTarget::CreateValueFromExpression(const char *name,
                                                  const char *expr) {
  SBValue sb_value;
  lldb::ValueObjectSP new_value_sp;
  if (IsValid() && name && *name && expr && *expr) {
    ExecutionContext exe_ctx(
        ExecutionContextRef(ExecutionContext(m_opaque_sp.get(), false)));
    new_value_sp =
        ValueObject::CreateValueObjectFromExpression(name, expr, exe_ctx);
  }
  sb_value.SetSP(new_value_sp);
  Log *log(lldb_private::GetLogIfAllCategoriesSet(LIBLLDB_LOG_API));
  if (log) {
    if (new_value_sp)
      log->Printf("SBTarget(%p)::CreateValueFromExpression => \"%s\"",
                  static_cast<void *>(m_opaque_sp.get()),
                  new_value_sp->GetName().AsCString());
    else
      log->Printf("SBTarget(%p)::CreateValueFromExpression => NULL",
                  static_cast<void *>(m_opaque_sp.get()));
  }
  return sb_value;
}

bool SBTarget::DeleteAllWatchpoints() {
  TargetSP target_sp(GetSP());
  if (target_sp) {
    std::lock_guard<std::recursive_mutex> guard(target_sp->GetAPIMutex());
    std::unique_lock<std::recursive_mutex> lock;
    target_sp->GetWatchpointList().GetListMutex(lock);
    target_sp->RemoveAllWatchpoints();
    return true;
  }
  return false;
}

void SBTarget::AppendImageSearchPath(const char *from, const char *to,
                                     lldb::SBError &error) {
  TargetSP target_sp(GetSP());
  if (!target_sp)
    return error.SetErrorString("invalid target");

  const ConstString csFrom(from), csTo(to);
  if (!csFrom)
    return error.SetErrorString("<from> path can't be empty");
  if (!csTo)
    return error.SetErrorString("<to> path can't be empty");

  Log *log(lldb_private::GetLogIfAllCategoriesSet(LIBLLDB_LOG_API));
  if (log)
    log->Printf("SBTarget(%p)::%s: '%s' -> '%s'",
                static_cast<void *>(target_sp.get()),  __FUNCTION__,
                from, to);
  target_sp->GetImageSearchPathList().Append(csFrom, csTo, true);
}

lldb::SBModule SBTarget::AddModule(const char *path, const char *triple,
                                   const char *uuid_cstr) {
  return AddModule(path, triple, uuid_cstr, NULL);
}

lldb::SBModule SBTarget::AddModule(const char *path, const char *triple,
                                   const char *uuid_cstr, const char *symfile) {
  lldb::SBModule sb_module;
  TargetSP target_sp(GetSP());
  if (target_sp) {
    ModuleSpec module_spec;
    if (path)
      module_spec.GetFileSpec().SetFile(path, FileSpec::Style::native);

    if (uuid_cstr)
      module_spec.GetUUID().SetFromStringRef(uuid_cstr);

    if (triple)
      module_spec.GetArchitecture() = Platform::GetAugmentedArchSpec(
          target_sp->GetPlatform().get(), triple);
    else
      module_spec.GetArchitecture() = target_sp->GetArchitecture();

    if (symfile)
      module_spec.GetSymbolFileSpec().SetFile(symfile, FileSpec::Style::native);

    sb_module.SetSP(target_sp->GetSharedModule(module_spec));
  }
  return sb_module;
}

lldb::SBModule SBTarget::AddModule(const SBModuleSpec &module_spec) {
  lldb::SBModule sb_module;
  TargetSP target_sp(GetSP());
  if (target_sp)
    sb_module.SetSP(target_sp->GetSharedModule(*module_spec.m_opaque_ap));
  return sb_module;
}

bool SBTarget::AddModule(lldb::SBModule &module) {
  TargetSP target_sp(GetSP());
  if (target_sp) {
    target_sp->GetImages().AppendIfNeeded(module.GetSP());
    return true;
  }
  return false;
}

uint32_t SBTarget::GetNumModules() const {
  Log *log(lldb_private::GetLogIfAllCategoriesSet(LIBLLDB_LOG_API));

  uint32_t num = 0;
  TargetSP target_sp(GetSP());
  if (target_sp) {
    // The module list is thread safe, no need to lock
    num = target_sp->GetImages().GetSize();
  }

  if (log)
    log->Printf("SBTarget(%p)::GetNumModules () => %d",
                static_cast<void *>(target_sp.get()), num);

  return num;
}

void SBTarget::Clear() {
  Log *log(lldb_private::GetLogIfAllCategoriesSet(LIBLLDB_LOG_API));

  if (log)
    log->Printf("SBTarget(%p)::Clear ()",
                static_cast<void *>(m_opaque_sp.get()));

  m_opaque_sp.reset();
}

SBModule SBTarget::FindModule(const SBFileSpec &sb_file_spec) {
  SBModule sb_module;
  TargetSP target_sp(GetSP());
  if (target_sp && sb_file_spec.IsValid()) {
    ModuleSpec module_spec(*sb_file_spec);
    // The module list is thread safe, no need to lock
    sb_module.SetSP(target_sp->GetImages().FindFirstModule(module_spec));
  }
  return sb_module;
}

SBSymbolContextList
SBTarget::FindCompileUnits(const SBFileSpec &sb_file_spec) {
  SBSymbolContextList sb_sc_list;
  const TargetSP target_sp(GetSP());
  if (target_sp && sb_file_spec.IsValid()) {
    const bool append = true;
    target_sp->GetImages().FindCompileUnits(*sb_file_spec,
                                            append, *sb_sc_list);
  }
  return sb_sc_list;
}

lldb::ByteOrder SBTarget::GetByteOrder() {
  TargetSP target_sp(GetSP());
  if (target_sp)
    return target_sp->GetArchitecture().GetByteOrder();
  return eByteOrderInvalid;
}

const char *SBTarget::GetTriple() {
  TargetSP target_sp(GetSP());
  if (target_sp) {
    std::string triple(target_sp->GetArchitecture().GetTriple().str());
    // Unique the string so we don't run into ownership issues since the const
    // strings put the string into the string pool once and the strings never
    // comes out
    ConstString const_triple(triple.c_str());
    return const_triple.GetCString();
  }
  return NULL;
}

uint32_t SBTarget::GetDataByteSize() {
  TargetSP target_sp(GetSP());
  if (target_sp) {
    return target_sp->GetArchitecture().GetDataByteSize();
  }
  return 0;
}

uint32_t SBTarget::GetCodeByteSize() {
  TargetSP target_sp(GetSP());
  if (target_sp) {
    return target_sp->GetArchitecture().GetCodeByteSize();
  }
  return 0;
}

uint32_t SBTarget::GetAddressByteSize() {
  TargetSP target_sp(GetSP());
  if (target_sp)
    return target_sp->GetArchitecture().GetAddressByteSize();
  return sizeof(void *);
}

SBModule SBTarget::GetModuleAtIndex(uint32_t idx) {
  Log *log(lldb_private::GetLogIfAllCategoriesSet(LIBLLDB_LOG_API));

  SBModule sb_module;
  ModuleSP module_sp;
  TargetSP target_sp(GetSP());
  if (target_sp) {
    // The module list is thread safe, no need to lock
    module_sp = target_sp->GetImages().GetModuleAtIndex(idx);
    sb_module.SetSP(module_sp);
  }

  if (log)
    log->Printf("SBTarget(%p)::GetModuleAtIndex (idx=%d) => SBModule(%p)",
                static_cast<void *>(target_sp.get()), idx,
                static_cast<void *>(module_sp.get()));

  return sb_module;
}

bool SBTarget::RemoveModule(lldb::SBModule module) {
  TargetSP target_sp(GetSP());
  if (target_sp)
    return target_sp->GetImages().Remove(module.GetSP());
  return false;
}

SBBroadcaster SBTarget::GetBroadcaster() const {
  Log *log(lldb_private::GetLogIfAllCategoriesSet(LIBLLDB_LOG_API));

  TargetSP target_sp(GetSP());
  SBBroadcaster broadcaster(target_sp.get(), false);

  if (log)
    log->Printf("SBTarget(%p)::GetBroadcaster () => SBBroadcaster(%p)",
                static_cast<void *>(target_sp.get()),
                static_cast<void *>(broadcaster.get()));

  return broadcaster;
}

bool SBTarget::GetDescription(SBStream &description,
                              lldb::DescriptionLevel description_level) {
  Stream &strm = description.ref();

  TargetSP target_sp(GetSP());
  if (target_sp) {
    target_sp->Dump(&strm, description_level);
  } else
    strm.PutCString("No value");

  return true;
}

lldb::SBSymbolContextList SBTarget::FindFunctions(const char *name,
                                                  uint32_t name_type_mask) {
  lldb::SBSymbolContextList sb_sc_list;
  if (!name | !name[0])
    return sb_sc_list;

  TargetSP target_sp(GetSP());
  if (!target_sp)
    return sb_sc_list;

  const bool symbols_ok = true;
  const bool inlines_ok = true;
  const bool append = true;
  FunctionNameType mask = static_cast<FunctionNameType>(name_type_mask);
  target_sp->GetImages().FindFunctions(ConstString(name), mask, symbols_ok,
                                       inlines_ok, append, *sb_sc_list);
  return sb_sc_list;
}

lldb::SBSymbolContextList SBTarget::FindGlobalFunctions(const char *name,
                                                        uint32_t max_matches,
                                                        MatchType matchtype) {
  lldb::SBSymbolContextList sb_sc_list;
  if (name && name[0]) {
    llvm::StringRef name_ref(name);
    TargetSP target_sp(GetSP());
    if (target_sp) {
      std::string regexstr;
      switch (matchtype) {
      case eMatchTypeRegex:
        target_sp->GetImages().FindFunctions(RegularExpression(name_ref), true,
                                             true, true, *sb_sc_list);
        break;
      case eMatchTypeStartsWith:
        regexstr = llvm::Regex::escape(name) + ".*";
        target_sp->GetImages().FindFunctions(RegularExpression(regexstr), true,
                                             true, true, *sb_sc_list);
        break;
      default:
        target_sp->GetImages().FindFunctions(ConstString(name),
                                             eFunctionNameTypeAny, true, true,
                                             true, *sb_sc_list);
        break;
      }
    }
  }
  return sb_sc_list;
}

lldb::SBType SBTarget::FindFirstType(const char *typename_cstr) {
  TargetSP target_sp(GetSP());
  if (typename_cstr && typename_cstr[0] && target_sp) {
    ConstString const_typename(typename_cstr);
    SymbolContext sc;
    const bool exact_match = false;

    const ModuleList &module_list = target_sp->GetImages();
    size_t count = module_list.GetSize();
    for (size_t idx = 0; idx < count; idx++) {
      ModuleSP module_sp(module_list.GetModuleAtIndex(idx));
      if (module_sp) {
        TypeSP type_sp(
            module_sp->FindFirstType(sc, const_typename, exact_match));
        if (type_sp)
          return SBType(type_sp);
      }
    }

    // Didn't find the type in the symbols; try the Objective-C runtime if one
    // is installed

    ProcessSP process_sp(target_sp->GetProcessSP());

    if (process_sp) {
      ObjCLanguageRuntime *objc_language_runtime =
          process_sp->GetObjCLanguageRuntime();

      if (objc_language_runtime) {
        DeclVendor *objc_decl_vendor = objc_language_runtime->GetDeclVendor();

        if (objc_decl_vendor) {
          std::vector<clang::NamedDecl *> decls;

          if (objc_decl_vendor->FindDecls(const_typename, true, 1, decls) > 0) {
            if (CompilerType type = ClangASTContext::GetTypeForDecl(decls[0])) {
              return SBType(type);
            }
          }
        }
      }
    }

    // No matches, search for basic typename matches
    ClangASTContext *clang_ast = target_sp->GetScratchClangASTContext();
    if (clang_ast)
      return SBType(ClangASTContext::GetBasicType(clang_ast->getASTContext(),
                                                  const_typename));
  }
  return SBType();
}

SBType SBTarget::GetBasicType(lldb::BasicType type) {
  TargetSP target_sp(GetSP());
  if (target_sp) {
    ClangASTContext *clang_ast = target_sp->GetScratchClangASTContext();
    if (clang_ast)
      return SBType(
          ClangASTContext::GetBasicType(clang_ast->getASTContext(), type));
  }
  return SBType();
}

lldb::SBTypeList SBTarget::FindTypes(const char *typename_cstr) {
  SBTypeList sb_type_list;
  TargetSP target_sp(GetSP());
  if (typename_cstr && typename_cstr[0] && target_sp) {
    ModuleList &images = target_sp->GetImages();
    ConstString const_typename(typename_cstr);
    bool exact_match = false;
    TypeList type_list;
    llvm::DenseSet<SymbolFile *> searched_symbol_files;
    uint32_t num_matches =
        images.FindTypes(nullptr, const_typename, exact_match, UINT32_MAX,
                         searched_symbol_files, type_list);

    if (num_matches > 0) {
      for (size_t idx = 0; idx < num_matches; idx++) {
        TypeSP type_sp(type_list.GetTypeAtIndex(idx));
        if (type_sp)
          sb_type_list.Append(SBType(type_sp));
      }
    }

    // Try the Objective-C runtime if one is installed

    ProcessSP process_sp(target_sp->GetProcessSP());

    if (process_sp) {
      ObjCLanguageRuntime *objc_language_runtime =
          process_sp->GetObjCLanguageRuntime();

      if (objc_language_runtime) {
        DeclVendor *objc_decl_vendor = objc_language_runtime->GetDeclVendor();

        if (objc_decl_vendor) {
          std::vector<clang::NamedDecl *> decls;

          if (objc_decl_vendor->FindDecls(const_typename, true, 1, decls) > 0) {
            for (clang::NamedDecl *decl : decls) {
              if (CompilerType type = ClangASTContext::GetTypeForDecl(decl)) {
                sb_type_list.Append(SBType(type));
              }
            }
          }
        }
      }
    }

    if (sb_type_list.GetSize() == 0) {
      // No matches, search for basic typename matches
      ClangASTContext *clang_ast = target_sp->GetScratchClangASTContext();
      if (clang_ast)
        sb_type_list.Append(SBType(ClangASTContext::GetBasicType(
            clang_ast->getASTContext(), const_typename)));
    }
  }
  return sb_type_list;
}

SBValueList SBTarget::FindGlobalVariables(const char *name,
                                          uint32_t max_matches) {
  SBValueList sb_value_list;

  TargetSP target_sp(GetSP());
  if (name && target_sp) {
    VariableList variable_list;
    const uint32_t match_count = target_sp->GetImages().FindGlobalVariables(
        ConstString(name), max_matches, variable_list);

    if (match_count > 0) {
      ExecutionContextScope *exe_scope = target_sp->GetProcessSP().get();
      if (exe_scope == NULL)
        exe_scope = target_sp.get();
      for (uint32_t i = 0; i < match_count; ++i) {
        lldb::ValueObjectSP valobj_sp(ValueObjectVariable::Create(
            exe_scope, variable_list.GetVariableAtIndex(i)));
        if (valobj_sp)
          sb_value_list.Append(SBValue(valobj_sp));
      }
    }
  }

  return sb_value_list;
}

SBValueList SBTarget::FindGlobalVariables(const char *name,
                                          uint32_t max_matches,
                                          MatchType matchtype) {
  SBValueList sb_value_list;

  TargetSP target_sp(GetSP());
  if (name && target_sp) {
    llvm::StringRef name_ref(name);
    VariableList variable_list;

    std::string regexstr;
    uint32_t match_count;
    switch (matchtype) {
    case eMatchTypeNormal:
      match_count = target_sp->GetImages().FindGlobalVariables(
          ConstString(name), max_matches, variable_list);
      break;
    case eMatchTypeRegex:
      match_count = target_sp->GetImages().FindGlobalVariables(
          RegularExpression(name_ref), max_matches, variable_list);
      break;
    case eMatchTypeStartsWith:
      regexstr = llvm::Regex::escape(name) + ".*";
      match_count = target_sp->GetImages().FindGlobalVariables(
          RegularExpression(regexstr), max_matches, variable_list);
      break;
    }

    if (match_count > 0) {
      ExecutionContextScope *exe_scope = target_sp->GetProcessSP().get();
      if (exe_scope == NULL)
        exe_scope = target_sp.get();
      for (uint32_t i = 0; i < match_count; ++i) {
        lldb::ValueObjectSP valobj_sp(ValueObjectVariable::Create(
            exe_scope, variable_list.GetVariableAtIndex(i)));
        if (valobj_sp)
          sb_value_list.Append(SBValue(valobj_sp));
      }
    }
  }

  return sb_value_list;
}

lldb::SBValue SBTarget::FindFirstGlobalVariable(const char *name) {
  SBValueList sb_value_list(FindGlobalVariables(name, 1));
  if (sb_value_list.IsValid() && sb_value_list.GetSize() > 0)
    return sb_value_list.GetValueAtIndex(0);
  return SBValue();
}

SBSourceManager SBTarget::GetSourceManager() {
  SBSourceManager source_manager(*this);
  return source_manager;
}

lldb::SBInstructionList SBTarget::ReadInstructions(lldb::SBAddress base_addr,
                                                   uint32_t count) {
  return ReadInstructions(base_addr, count, NULL);
}

lldb::SBInstructionList SBTarget::ReadInstructions(lldb::SBAddress base_addr,
                                                   uint32_t count,
                                                   const char *flavor_string) {
  SBInstructionList sb_instructions;

  TargetSP target_sp(GetSP());
  if (target_sp) {
    Address *addr_ptr = base_addr.get();

    if (addr_ptr) {
      DataBufferHeap data(
          target_sp->GetArchitecture().GetMaximumOpcodeByteSize() * count, 0);
      bool prefer_file_cache = false;
      lldb_private::Status error;
      lldb::addr_t load_addr = LLDB_INVALID_ADDRESS;
      const size_t bytes_read =
          target_sp->ReadMemory(*addr_ptr, prefer_file_cache, data.GetBytes(),
                                data.GetByteSize(), error, &load_addr);
      const bool data_from_file = load_addr == LLDB_INVALID_ADDRESS;
      sb_instructions.SetDisassembler(Disassembler::DisassembleBytes(
          target_sp->GetArchitecture(), NULL, flavor_string, *addr_ptr,
          data.GetBytes(), bytes_read, count, data_from_file));
    }
  }

  return sb_instructions;
}

lldb::SBInstructionList SBTarget::GetInstructions(lldb::SBAddress base_addr,
                                                  const void *buf,
                                                  size_t size) {
  return GetInstructionsWithFlavor(base_addr, NULL, buf, size);
}

lldb::SBInstructionList
SBTarget::GetInstructionsWithFlavor(lldb::SBAddress base_addr,
                                    const char *flavor_string, const void *buf,
                                    size_t size) {
  SBInstructionList sb_instructions;

  TargetSP target_sp(GetSP());
  if (target_sp) {
    Address addr;

    if (base_addr.get())
      addr = *base_addr.get();

    const bool data_from_file = true;

    sb_instructions.SetDisassembler(Disassembler::DisassembleBytes(
        target_sp->GetArchitecture(), NULL, flavor_string, addr, buf, size,
        UINT32_MAX, data_from_file));
  }

  return sb_instructions;
}

lldb::SBInstructionList SBTarget::GetInstructions(lldb::addr_t base_addr,
                                                  const void *buf,
                                                  size_t size) {
  return GetInstructionsWithFlavor(ResolveLoadAddress(base_addr), NULL, buf,
                                   size);
}

lldb::SBInstructionList
SBTarget::GetInstructionsWithFlavor(lldb::addr_t base_addr,
                                    const char *flavor_string, const void *buf,
                                    size_t size) {
  return GetInstructionsWithFlavor(ResolveLoadAddress(base_addr), flavor_string,
                                   buf, size);
}

SBError SBTarget::SetSectionLoadAddress(lldb::SBSection section,
                                        lldb::addr_t section_base_addr) {
  SBError sb_error;
  TargetSP target_sp(GetSP());
  if (target_sp) {
    if (!section.IsValid()) {
      sb_error.SetErrorStringWithFormat("invalid section");
    } else {
      SectionSP section_sp(section.GetSP());
      if (section_sp) {
        if (section_sp->IsThreadSpecific()) {
          sb_error.SetErrorString(
              "thread specific sections are not yet supported");
        } else {
          ProcessSP process_sp(target_sp->GetProcessSP());
          if (target_sp->SetSectionLoadAddress(section_sp, section_base_addr)) {
            ModuleSP module_sp(section_sp->GetModule());
            if (module_sp) {
              ModuleList module_list;
              module_list.Append(module_sp);
              target_sp->ModulesDidLoad(module_list);
            }
            // Flush info in the process (stack frames, etc)
            if (process_sp)
              process_sp->Flush();
          }
        }
      }
    }
  } else {
    sb_error.SetErrorString("invalid target");
  }
  return sb_error;
}

SBError SBTarget::ClearSectionLoadAddress(lldb::SBSection section) {
  SBError sb_error;

  TargetSP target_sp(GetSP());
  if (target_sp) {
    if (!section.IsValid()) {
      sb_error.SetErrorStringWithFormat("invalid section");
    } else {
      SectionSP section_sp(section.GetSP());
      if (section_sp) {
        ProcessSP process_sp(target_sp->GetProcessSP());
        if (target_sp->SetSectionUnloaded(section_sp)) {
          ModuleSP module_sp(section_sp->GetModule());
          if (module_sp) {
            ModuleList module_list;
            module_list.Append(module_sp);
            target_sp->ModulesDidUnload(module_list, false);
          }
          // Flush info in the process (stack frames, etc)
          if (process_sp)
            process_sp->Flush();
        }
      } else {
        sb_error.SetErrorStringWithFormat("invalid section");
      }
    }
  } else {
    sb_error.SetErrorStringWithFormat("invalid target");
  }
  return sb_error;
}

SBError SBTarget::SetModuleLoadAddress(lldb::SBModule module,
                                       int64_t slide_offset) {
  SBError sb_error;

  TargetSP target_sp(GetSP());
  if (target_sp) {
    ModuleSP module_sp(module.GetSP());
    if (module_sp) {
      bool changed = false;
      if (module_sp->SetLoadAddress(*target_sp, slide_offset, true, changed)) {
        // The load was successful, make sure that at least some sections
        // changed before we notify that our module was loaded.
        if (changed) {
          ModuleList module_list;
          module_list.Append(module_sp);
          target_sp->ModulesDidLoad(module_list);
          // Flush info in the process (stack frames, etc)
          ProcessSP process_sp(target_sp->GetProcessSP());
          if (process_sp)
            process_sp->Flush();
        }
      }
    } else {
      sb_error.SetErrorStringWithFormat("invalid module");
    }

  } else {
    sb_error.SetErrorStringWithFormat("invalid target");
  }
  return sb_error;
}

SBError SBTarget::ClearModuleLoadAddress(lldb::SBModule module) {
  SBError sb_error;

  char path[PATH_MAX];
  TargetSP target_sp(GetSP());
  if (target_sp) {
    ModuleSP module_sp(module.GetSP());
    if (module_sp) {
      ObjectFile *objfile = module_sp->GetObjectFile();
      if (objfile) {
        SectionList *section_list = objfile->GetSectionList();
        if (section_list) {
          ProcessSP process_sp(target_sp->GetProcessSP());

          bool changed = false;
          const size_t num_sections = section_list->GetSize();
          for (size_t sect_idx = 0; sect_idx < num_sections; ++sect_idx) {
            SectionSP section_sp(section_list->GetSectionAtIndex(sect_idx));
            if (section_sp)
              changed |= target_sp->SetSectionUnloaded(section_sp);
          }
          if (changed) {
            ModuleList module_list;
            module_list.Append(module_sp);
            target_sp->ModulesDidUnload(module_list, false);
            // Flush info in the process (stack frames, etc)
            ProcessSP process_sp(target_sp->GetProcessSP());
            if (process_sp)
              process_sp->Flush();
          }
        } else {
          module_sp->GetFileSpec().GetPath(path, sizeof(path));
          sb_error.SetErrorStringWithFormat("no sections in object file '%s'",
                                            path);
        }
      } else {
        module_sp->GetFileSpec().GetPath(path, sizeof(path));
        sb_error.SetErrorStringWithFormat("no object file for module '%s'",
                                          path);
      }
    } else {
      sb_error.SetErrorStringWithFormat("invalid module");
    }
  } else {
    sb_error.SetErrorStringWithFormat("invalid target");
  }
  return sb_error;
}

lldb::SBSymbolContextList SBTarget::FindSymbols(const char *name,
                                                lldb::SymbolType symbol_type) {
  SBSymbolContextList sb_sc_list;
  if (name && name[0]) {
    TargetSP target_sp(GetSP());
    if (target_sp) {
      bool append = true;
      target_sp->GetImages().FindSymbolsWithNameAndType(
          ConstString(name), symbol_type, *sb_sc_list, append);
    }
  }
  return sb_sc_list;
}

lldb::SBValue SBTarget::EvaluateExpression(const char *expr) {
  TargetSP target_sp(GetSP());
  if (!target_sp)
    return SBValue();

  SBExpressionOptions options;
  lldb::DynamicValueType fetch_dynamic_value =
      target_sp->GetPreferDynamicValue();
  options.SetFetchDynamicValue(fetch_dynamic_value);
  options.SetUnwindOnError(true);
  return EvaluateExpression(expr, options);
}

lldb::SBValue SBTarget::EvaluateExpression(const char *expr,
                                           const SBExpressionOptions &options) {
  Log *log(GetLogIfAllCategoriesSet(LIBLLDB_LOG_API));
#if !defined(LLDB_DISABLE_PYTHON)
  Log *expr_log(GetLogIfAllCategoriesSet(LIBLLDB_LOG_EXPRESSIONS));
#endif
  SBValue expr_result;
  ExpressionResults exe_results = eExpressionSetupError;
  ValueObjectSP expr_value_sp;
  TargetSP target_sp(GetSP());
  StackFrame *frame = NULL;
  if (target_sp) {
    if (expr == NULL || expr[0] == '\0') {
      if (log)
        log->Printf(
            "SBTarget::EvaluateExpression called with an empty expression");
      return expr_result;
    }

    std::lock_guard<std::recursive_mutex> guard(target_sp->GetAPIMutex());
    ExecutionContext exe_ctx(m_opaque_sp.get());

    if (log)
      log->Printf("SBTarget()::EvaluateExpression (expr=\"%s\")...", expr);

    frame = exe_ctx.GetFramePtr();
    Target *target = exe_ctx.GetTargetPtr();

    if (target) {
#ifdef LLDB_CONFIGURATION_DEBUG
      StreamString frame_description;
      if (frame)
        frame->DumpUsingSettingsFormat(&frame_description);
      llvm::PrettyStackTraceFormat stack_trace(
          "SBTarget::EvaluateExpression (expr = \"%s\", fetch_dynamic_value = "
          "%u) %s",
          expr, options.GetFetchDynamicValue(),
          frame_description.GetString().str().c_str());
#endif
      exe_results =
          target->EvaluateExpression(expr, frame, expr_value_sp, options.ref());

      expr_result.SetSP(expr_value_sp, options.GetFetchDynamicValue());
    } else {
      if (log)
        log->Printf("SBTarget::EvaluateExpression () => error: could not "
                    "reconstruct frame object for this SBTarget.");
    }
  }
#ifndef LLDB_DISABLE_PYTHON
  if (expr_log)
    expr_log->Printf("** [SBTarget::EvaluateExpression] Expression result is "
                     "%s, summary %s **",
                     expr_result.GetValue(), expr_result.GetSummary());

  if (log)
    log->Printf("SBTarget(%p)::EvaluateExpression (expr=\"%s\") => SBValue(%p) "
                "(execution result=%d)",
                static_cast<void *>(frame), expr,
                static_cast<void *>(expr_value_sp.get()), exe_results);
#endif

  return expr_result;
}

lldb::addr_t SBTarget::GetStackRedZoneSize() {
  TargetSP target_sp(GetSP());
  if (target_sp) {
    ABISP abi_sp;
    ProcessSP process_sp(target_sp->GetProcessSP());
    if (process_sp)
      abi_sp = process_sp->GetABI();
    else
      abi_sp = ABI::FindPlugin(ProcessSP(), target_sp->GetArchitecture());
    if (abi_sp)
      return abi_sp->GetRedZoneSize();
  }
  return 0;
}

lldb::SBLaunchInfo SBTarget::GetLaunchInfo() const {
  lldb::SBLaunchInfo launch_info(NULL);
  TargetSP target_sp(GetSP());
  if (target_sp)
    launch_info.set_ref(m_opaque_sp->GetProcessLaunchInfo());
  return launch_info;
}

void SBTarget::SetLaunchInfo(const lldb::SBLaunchInfo &launch_info) {
  TargetSP target_sp(GetSP());
  if (target_sp)
    m_opaque_sp->SetProcessLaunchInfo(launch_info.ref());
}
