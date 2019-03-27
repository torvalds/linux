//===-- SBDebugger.cpp ------------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//


#include "SystemInitializerFull.h"

#include "lldb/API/SBDebugger.h"

#include "lldb/lldb-private.h"

#include "lldb/API/SBBroadcaster.h"
#include "lldb/API/SBCommandInterpreter.h"
#include "lldb/API/SBCommandReturnObject.h"
#include "lldb/API/SBError.h"
#include "lldb/API/SBEvent.h"
#include "lldb/API/SBFrame.h"
#include "lldb/API/SBListener.h"
#include "lldb/API/SBProcess.h"
#include "lldb/API/SBSourceManager.h"
#include "lldb/API/SBStream.h"
#include "lldb/API/SBStringList.h"
#include "lldb/API/SBStructuredData.h"
#include "lldb/API/SBTarget.h"
#include "lldb/API/SBThread.h"
#include "lldb/API/SBTypeCategory.h"
#include "lldb/API/SBTypeFilter.h"
#include "lldb/API/SBTypeFormat.h"
#include "lldb/API/SBTypeNameSpecifier.h"
#include "lldb/API/SBTypeSummary.h"
#include "lldb/API/SBTypeSynthetic.h"

#include "lldb/Core/Debugger.h"
#include "lldb/Core/PluginManager.h"
#include "lldb/Core/StreamFile.h"
#include "lldb/Core/StructuredDataImpl.h"
#include "lldb/DataFormatters/DataVisualization.h"
#include "lldb/Host/XML.h"
#include "lldb/Initialization/SystemLifetimeManager.h"
#include "lldb/Interpreter/CommandInterpreter.h"
#include "lldb/Interpreter/OptionArgParser.h"
#include "lldb/Interpreter/OptionGroupPlatform.h"
#include "lldb/Target/Process.h"
#include "lldb/Target/TargetList.h"
#include "lldb/Utility/Args.h"
#include "lldb/Utility/State.h"

#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/DynamicLibrary.h"
#include "llvm/Support/ManagedStatic.h"

using namespace lldb;
using namespace lldb_private;

static llvm::sys::DynamicLibrary LoadPlugin(const lldb::DebuggerSP &debugger_sp,
                                            const FileSpec &spec,
                                            Status &error) {
  llvm::sys::DynamicLibrary dynlib =
      llvm::sys::DynamicLibrary::getPermanentLibrary(spec.GetPath().c_str());
  if (dynlib.isValid()) {
    typedef bool (*LLDBCommandPluginInit)(lldb::SBDebugger & debugger);

    lldb::SBDebugger debugger_sb(debugger_sp);
    // This calls the bool lldb::PluginInitialize(lldb::SBDebugger debugger)
    // function.
    // TODO: mangle this differently for your system - on OSX, the first
    // underscore needs to be removed and the second one stays
    LLDBCommandPluginInit init_func =
        (LLDBCommandPluginInit)(uintptr_t)dynlib.getAddressOfSymbol(
            "_ZN4lldb16PluginInitializeENS_10SBDebuggerE");
    if (init_func) {
      if (init_func(debugger_sb))
        return dynlib;
      else
        error.SetErrorString("plug-in refused to load "
                             "(lldb::PluginInitialize(lldb::SBDebugger) "
                             "returned false)");
    } else {
      error.SetErrorString("plug-in is missing the required initialization: "
                           "lldb::PluginInitialize(lldb::SBDebugger)");
    }
  } else {
    if (FileSystem::Instance().Exists(spec))
      error.SetErrorString("this file does not represent a loadable dylib");
    else
      error.SetErrorString("no such file");
  }
  return llvm::sys::DynamicLibrary();
}

static llvm::ManagedStatic<SystemLifetimeManager> g_debugger_lifetime;

SBError SBInputReader::Initialize(
    lldb::SBDebugger &sb_debugger,
    unsigned long (*)(void *, lldb::SBInputReader *, lldb::InputReaderAction,
                      char const *, unsigned long),
    void *, lldb::InputReaderGranularity, char const *, char const *, bool) {
  return SBError();
}

void SBInputReader::SetIsDone(bool) {}

bool SBInputReader::IsActive() const { return false; }

SBDebugger::SBDebugger() = default;

SBDebugger::SBDebugger(const lldb::DebuggerSP &debugger_sp)
    : m_opaque_sp(debugger_sp) {}

SBDebugger::SBDebugger(const SBDebugger &rhs) : m_opaque_sp(rhs.m_opaque_sp) {}

SBDebugger::~SBDebugger() = default;

SBDebugger &SBDebugger::operator=(const SBDebugger &rhs) {
  if (this != &rhs) {
    m_opaque_sp = rhs.m_opaque_sp;
  }
  return *this;
}

void SBDebugger::Initialize() {
  SBInitializerOptions options;
  SBDebugger::Initialize(options);
}

lldb::SBError SBDebugger::Initialize(SBInitializerOptions &options) {
  Log *log(GetLogIfAllCategoriesSet(LIBLLDB_LOG_API));

  if (log)
    log->Printf("SBDebugger::Initialize ()");

  SBError error;
  if (auto e = g_debugger_lifetime->Initialize(
          llvm::make_unique<SystemInitializerFull>(), *options.m_opaque_up,
          LoadPlugin)) {
    error.SetError(Status(std::move(e)));
  }
  return error;
}

void SBDebugger::Terminate() { g_debugger_lifetime->Terminate(); }

void SBDebugger::Clear() {
  Log *log(GetLogIfAllCategoriesSet(LIBLLDB_LOG_API));

  if (log)
    log->Printf("SBDebugger(%p)::Clear ()",
                static_cast<void *>(m_opaque_sp.get()));

  if (m_opaque_sp)
    m_opaque_sp->ClearIOHandlers();

  m_opaque_sp.reset();
}

SBDebugger SBDebugger::Create() {
  return SBDebugger::Create(false, nullptr, nullptr);
}

SBDebugger SBDebugger::Create(bool source_init_files) {
  return SBDebugger::Create(source_init_files, nullptr, nullptr);
}

SBDebugger SBDebugger::Create(bool source_init_files,
                              lldb::LogOutputCallback callback, void *baton)

{
  Log *log(GetLogIfAllCategoriesSet(LIBLLDB_LOG_API));

  SBDebugger debugger;

  // Currently we have issues if this function is called simultaneously on two
  // different threads. The issues mainly revolve around the fact that the
  // lldb_private::FormatManager uses global collections and having two threads
  // parsing the .lldbinit files can cause mayhem. So to get around this for
  // now we need to use a mutex to prevent bad things from happening.
  static std::recursive_mutex g_mutex;
  std::lock_guard<std::recursive_mutex> guard(g_mutex);

  debugger.reset(Debugger::CreateInstance(callback, baton));

  if (log) {
    SBStream sstr;
    debugger.GetDescription(sstr);
    log->Printf("SBDebugger::Create () => SBDebugger(%p): %s",
                static_cast<void *>(debugger.m_opaque_sp.get()),
                sstr.GetData());
  }

  SBCommandInterpreter interp = debugger.GetCommandInterpreter();
  if (source_init_files) {
    interp.get()->SkipLLDBInitFiles(false);
    interp.get()->SkipAppInitFiles(false);
    SBCommandReturnObject result;
    interp.SourceInitFileInHomeDirectory(result);
  } else {
    interp.get()->SkipLLDBInitFiles(true);
    interp.get()->SkipAppInitFiles(true);
  }
  return debugger;
}

void SBDebugger::Destroy(SBDebugger &debugger) {
  Log *log(GetLogIfAllCategoriesSet(LIBLLDB_LOG_API));

  if (log) {
    SBStream sstr;
    debugger.GetDescription(sstr);
    log->Printf("SBDebugger::Destroy () => SBDebugger(%p): %s",
                static_cast<void *>(debugger.m_opaque_sp.get()),
                sstr.GetData());
  }

  Debugger::Destroy(debugger.m_opaque_sp);

  if (debugger.m_opaque_sp.get() != nullptr)
    debugger.m_opaque_sp.reset();
}

void SBDebugger::MemoryPressureDetected() {
  // Since this function can be call asynchronously, we allow it to be non-
  // mandatory. We have seen deadlocks with this function when called so we
  // need to safeguard against this until we can determine what is causing the
  // deadlocks.
  Log *log(GetLogIfAllCategoriesSet(LIBLLDB_LOG_API));

  const bool mandatory = false;
  if (log) {
    log->Printf("SBDebugger::MemoryPressureDetected (), mandatory = %d",
                mandatory);
  }

  ModuleList::RemoveOrphanSharedModules(mandatory);
}

bool SBDebugger::IsValid() const { return m_opaque_sp.get() != nullptr; }

void SBDebugger::SetAsync(bool b) {
  if (m_opaque_sp)
    m_opaque_sp->SetAsyncExecution(b);
}

bool SBDebugger::GetAsync() {
  return (m_opaque_sp ? m_opaque_sp->GetAsyncExecution() : false);
}

void SBDebugger::SkipLLDBInitFiles(bool b) {
  if (m_opaque_sp)
    m_opaque_sp->GetCommandInterpreter().SkipLLDBInitFiles(b);
}

void SBDebugger::SkipAppInitFiles(bool b) {
  if (m_opaque_sp)
    m_opaque_sp->GetCommandInterpreter().SkipAppInitFiles(b);
}

// Shouldn't really be settable after initialization as this could cause lots
// of problems; don't want users trying to switch modes in the middle of a
// debugging session.
void SBDebugger::SetInputFileHandle(FILE *fh, bool transfer_ownership) {
  Log *log(GetLogIfAllCategoriesSet(LIBLLDB_LOG_API));

  if (log)
    log->Printf(
        "SBDebugger(%p)::SetInputFileHandle (fh=%p, transfer_ownership=%i)",
        static_cast<void *>(m_opaque_sp.get()), static_cast<void *>(fh),
        transfer_ownership);

  if (m_opaque_sp)
    m_opaque_sp->SetInputFileHandle(fh, transfer_ownership);
}

void SBDebugger::SetOutputFileHandle(FILE *fh, bool transfer_ownership) {
  Log *log(GetLogIfAllCategoriesSet(LIBLLDB_LOG_API));

  if (log)
    log->Printf(
        "SBDebugger(%p)::SetOutputFileHandle (fh=%p, transfer_ownership=%i)",
        static_cast<void *>(m_opaque_sp.get()), static_cast<void *>(fh),
        transfer_ownership);

  if (m_opaque_sp)
    m_opaque_sp->SetOutputFileHandle(fh, transfer_ownership);
}

void SBDebugger::SetErrorFileHandle(FILE *fh, bool transfer_ownership) {
  Log *log(GetLogIfAllCategoriesSet(LIBLLDB_LOG_API));

  if (log)
    log->Printf(
        "SBDebugger(%p)::SetErrorFileHandle (fh=%p, transfer_ownership=%i)",
        static_cast<void *>(m_opaque_sp.get()), static_cast<void *>(fh),
        transfer_ownership);

  if (m_opaque_sp)
    m_opaque_sp->SetErrorFileHandle(fh, transfer_ownership);
}

FILE *SBDebugger::GetInputFileHandle() {
  if (m_opaque_sp) {
    StreamFileSP stream_file_sp(m_opaque_sp->GetInputFile());
    if (stream_file_sp)
      return stream_file_sp->GetFile().GetStream();
  }
  return nullptr;
}

FILE *SBDebugger::GetOutputFileHandle() {
  if (m_opaque_sp) {
    StreamFileSP stream_file_sp(m_opaque_sp->GetOutputFile());
    if (stream_file_sp)
      return stream_file_sp->GetFile().GetStream();
  }
  return nullptr;
}

FILE *SBDebugger::GetErrorFileHandle() {
  if (m_opaque_sp) {
    StreamFileSP stream_file_sp(m_opaque_sp->GetErrorFile());
    if (stream_file_sp)
      return stream_file_sp->GetFile().GetStream();
  }
  return nullptr;
}

void SBDebugger::SaveInputTerminalState() {
  if (m_opaque_sp)
    m_opaque_sp->SaveInputTerminalState();
}

void SBDebugger::RestoreInputTerminalState() {
  if (m_opaque_sp)
    m_opaque_sp->RestoreInputTerminalState();
}
SBCommandInterpreter SBDebugger::GetCommandInterpreter() {
  Log *log(GetLogIfAllCategoriesSet(LIBLLDB_LOG_API));

  SBCommandInterpreter sb_interpreter;
  if (m_opaque_sp)
    sb_interpreter.reset(&m_opaque_sp->GetCommandInterpreter());

  if (log)
    log->Printf(
        "SBDebugger(%p)::GetCommandInterpreter () => SBCommandInterpreter(%p)",
        static_cast<void *>(m_opaque_sp.get()),
        static_cast<void *>(sb_interpreter.get()));

  return sb_interpreter;
}

void SBDebugger::HandleCommand(const char *command) {
  if (m_opaque_sp) {
    TargetSP target_sp(m_opaque_sp->GetSelectedTarget());
    std::unique_lock<std::recursive_mutex> lock;
    if (target_sp)
      lock = std::unique_lock<std::recursive_mutex>(target_sp->GetAPIMutex());

    SBCommandInterpreter sb_interpreter(GetCommandInterpreter());
    SBCommandReturnObject result;

    sb_interpreter.HandleCommand(command, result, false);

    if (GetErrorFileHandle() != nullptr)
      result.PutError(GetErrorFileHandle());
    if (GetOutputFileHandle() != nullptr)
      result.PutOutput(GetOutputFileHandle());

    if (!m_opaque_sp->GetAsyncExecution()) {
      SBProcess process(GetCommandInterpreter().GetProcess());
      ProcessSP process_sp(process.GetSP());
      if (process_sp) {
        EventSP event_sp;
        ListenerSP lldb_listener_sp = m_opaque_sp->GetListener();
        while (lldb_listener_sp->GetEventForBroadcaster(
            process_sp.get(), event_sp, std::chrono::seconds(0))) {
          SBEvent event(event_sp);
          HandleProcessEvent(process, event, GetOutputFileHandle(),
                             GetErrorFileHandle());
        }
      }
    }
  }
}

SBListener SBDebugger::GetListener() {
  Log *log(GetLogIfAllCategoriesSet(LIBLLDB_LOG_API));

  SBListener sb_listener;
  if (m_opaque_sp)
    sb_listener.reset(m_opaque_sp->GetListener());

  if (log)
    log->Printf("SBDebugger(%p)::GetListener () => SBListener(%p)",
                static_cast<void *>(m_opaque_sp.get()),
                static_cast<void *>(sb_listener.get()));

  return sb_listener;
}

void SBDebugger::HandleProcessEvent(const SBProcess &process,
                                    const SBEvent &event, FILE *out,
                                    FILE *err) {
  if (!process.IsValid())
    return;

  TargetSP target_sp(process.GetTarget().GetSP());
  if (!target_sp)
    return;

  const uint32_t event_type = event.GetType();
  char stdio_buffer[1024];
  size_t len;

  std::lock_guard<std::recursive_mutex> guard(target_sp->GetAPIMutex());

  if (event_type &
      (Process::eBroadcastBitSTDOUT | Process::eBroadcastBitStateChanged)) {
    // Drain stdout when we stop just in case we have any bytes
    while ((len = process.GetSTDOUT(stdio_buffer, sizeof(stdio_buffer))) > 0)
      if (out != nullptr)
        ::fwrite(stdio_buffer, 1, len, out);
  }

  if (event_type &
      (Process::eBroadcastBitSTDERR | Process::eBroadcastBitStateChanged)) {
    // Drain stderr when we stop just in case we have any bytes
    while ((len = process.GetSTDERR(stdio_buffer, sizeof(stdio_buffer))) > 0)
      if (err != nullptr)
        ::fwrite(stdio_buffer, 1, len, err);
  }

  if (event_type & Process::eBroadcastBitStateChanged) {
    StateType event_state = SBProcess::GetStateFromEvent(event);

    if (event_state == eStateInvalid)
      return;

    bool is_stopped = StateIsStoppedState(event_state);
    if (!is_stopped)
      process.ReportEventState(event, out);
  }
}

SBSourceManager SBDebugger::GetSourceManager() {
  SBSourceManager sb_source_manager(*this);
  return sb_source_manager;
}

bool SBDebugger::GetDefaultArchitecture(char *arch_name, size_t arch_name_len) {
  if (arch_name && arch_name_len) {
    ArchSpec default_arch = Target::GetDefaultArchitecture();

    if (default_arch.IsValid()) {
      const std::string &triple_str = default_arch.GetTriple().str();
      if (!triple_str.empty())
        ::snprintf(arch_name, arch_name_len, "%s", triple_str.c_str());
      else
        ::snprintf(arch_name, arch_name_len, "%s",
                   default_arch.GetArchitectureName());
      return true;
    }
  }
  if (arch_name && arch_name_len)
    arch_name[0] = '\0';
  return false;
}

bool SBDebugger::SetDefaultArchitecture(const char *arch_name) {
  if (arch_name) {
    ArchSpec arch(arch_name);
    if (arch.IsValid()) {
      Target::SetDefaultArchitecture(arch);
      return true;
    }
  }
  return false;
}

ScriptLanguage
SBDebugger::GetScriptingLanguage(const char *script_language_name) {
  if (!script_language_name) return eScriptLanguageDefault;
  return OptionArgParser::ToScriptLanguage(
      llvm::StringRef(script_language_name), eScriptLanguageDefault, nullptr);
}

const char *SBDebugger::GetVersionString() {
  return lldb_private::GetVersion();
}

const char *SBDebugger::StateAsCString(StateType state) {
  return lldb_private::StateAsCString(state);
}

static void AddBoolConfigEntry(StructuredData::Dictionary &dict,
                               llvm::StringRef name, bool value,
                               llvm::StringRef description) {
  auto entry_up = llvm::make_unique<StructuredData::Dictionary>();
  entry_up->AddBooleanItem("value", value);
  entry_up->AddStringItem("description", description);
  dict.AddItem(name, std::move(entry_up));
}

static void AddLLVMTargets(StructuredData::Dictionary &dict) {
  auto array_up = llvm::make_unique<StructuredData::Array>();
#define LLVM_TARGET(target)                                                    \
  array_up->AddItem(llvm::make_unique<StructuredData::String>(#target));
#include "llvm/Config/Targets.def"
  auto entry_up = llvm::make_unique<StructuredData::Dictionary>();
  entry_up->AddItem("value", std::move(array_up));
  entry_up->AddStringItem("description", "A list of configured LLVM targets.");
  dict.AddItem("targets", std::move(entry_up));
}

SBStructuredData SBDebugger::GetBuildConfiguration() {
  auto config_up = llvm::make_unique<StructuredData::Dictionary>();
  AddBoolConfigEntry(
      *config_up, "xml", XMLDocument::XMLEnabled(),
      "A boolean value that indicates if XML support is enabled in LLDB");
  AddLLVMTargets(*config_up);

  SBStructuredData data;
  data.m_impl_up->SetObjectSP(std::move(config_up));
  return data;
}

bool SBDebugger::StateIsRunningState(StateType state) {
  Log *log(GetLogIfAllCategoriesSet(LIBLLDB_LOG_API));

  const bool result = lldb_private::StateIsRunningState(state);
  if (log)
    log->Printf("SBDebugger::StateIsRunningState (state=%s) => %i",
                StateAsCString(state), result);

  return result;
}

bool SBDebugger::StateIsStoppedState(StateType state) {
  Log *log(GetLogIfAllCategoriesSet(LIBLLDB_LOG_API));

  const bool result = lldb_private::StateIsStoppedState(state, false);
  if (log)
    log->Printf("SBDebugger::StateIsStoppedState (state=%s) => %i",
                StateAsCString(state), result);

  return result;
}

lldb::SBTarget SBDebugger::CreateTarget(const char *filename,
                                        const char *target_triple,
                                        const char *platform_name,
                                        bool add_dependent_modules,
                                        lldb::SBError &sb_error) {
  SBTarget sb_target;
  TargetSP target_sp;
  if (m_opaque_sp) {
    sb_error.Clear();
    OptionGroupPlatform platform_options(false);
    platform_options.SetPlatformName(platform_name);

    sb_error.ref() = m_opaque_sp->GetTargetList().CreateTarget(
        *m_opaque_sp, filename, target_triple,
        add_dependent_modules ? eLoadDependentsYes : eLoadDependentsNo,
        &platform_options, target_sp);

    if (sb_error.Success())
      sb_target.SetSP(target_sp);
  } else {
    sb_error.SetErrorString("invalid debugger");
  }

  Log *log(GetLogIfAllCategoriesSet(LIBLLDB_LOG_API));
  if (log)
    log->Printf("SBDebugger(%p)::CreateTarget (filename=\"%s\", triple=%s, "
                "platform_name=%s, add_dependent_modules=%u, error=%s) => "
                "SBTarget(%p)",
                static_cast<void *>(m_opaque_sp.get()), filename, target_triple,
                platform_name, add_dependent_modules, sb_error.GetCString(),
                static_cast<void *>(target_sp.get()));

  return sb_target;
}

SBTarget
SBDebugger::CreateTargetWithFileAndTargetTriple(const char *filename,
                                                const char *target_triple) {
  SBTarget sb_target;
  TargetSP target_sp;
  if (m_opaque_sp) {
    const bool add_dependent_modules = true;
    Status error(m_opaque_sp->GetTargetList().CreateTarget(
        *m_opaque_sp, filename, target_triple,
        add_dependent_modules ? eLoadDependentsYes : eLoadDependentsNo, nullptr,
        target_sp));
    sb_target.SetSP(target_sp);
  }

  Log *log(GetLogIfAllCategoriesSet(LIBLLDB_LOG_API));
  if (log)
    log->Printf("SBDebugger(%p)::CreateTargetWithFileAndTargetTriple "
                "(filename=\"%s\", triple=%s) => SBTarget(%p)",
                static_cast<void *>(m_opaque_sp.get()), filename, target_triple,
                static_cast<void *>(target_sp.get()));

  return sb_target;
}

SBTarget SBDebugger::CreateTargetWithFileAndArch(const char *filename,
                                                 const char *arch_cstr) {
  Log *log(GetLogIfAllCategoriesSet(LIBLLDB_LOG_API));

  SBTarget sb_target;
  TargetSP target_sp;
  if (m_opaque_sp) {
    Status error;
    const bool add_dependent_modules = true;

    error = m_opaque_sp->GetTargetList().CreateTarget(
        *m_opaque_sp, filename, arch_cstr,
        add_dependent_modules ? eLoadDependentsYes : eLoadDependentsNo, nullptr,
        target_sp);

    if (error.Success()) {
      m_opaque_sp->GetTargetList().SetSelectedTarget(target_sp.get());
      sb_target.SetSP(target_sp);
    }
  }

  if (log)
    log->Printf("SBDebugger(%p)::CreateTargetWithFileAndArch (filename=\"%s\", "
                "arch=%s) => SBTarget(%p)",
                static_cast<void *>(m_opaque_sp.get()), filename, arch_cstr,
                static_cast<void *>(target_sp.get()));

  return sb_target;
}

SBTarget SBDebugger::CreateTarget(const char *filename) {
  SBTarget sb_target;
  TargetSP target_sp;
  if (m_opaque_sp) {
    Status error;
    const bool add_dependent_modules = true;
    error = m_opaque_sp->GetTargetList().CreateTarget(
        *m_opaque_sp, filename, "",
        add_dependent_modules ? eLoadDependentsYes : eLoadDependentsNo, nullptr,
        target_sp);

    if (error.Success()) {
      m_opaque_sp->GetTargetList().SetSelectedTarget(target_sp.get());
      sb_target.SetSP(target_sp);
    }
  }
  Log *log(GetLogIfAllCategoriesSet(LIBLLDB_LOG_API));
  if (log)
    log->Printf(
        "SBDebugger(%p)::CreateTarget (filename=\"%s\") => SBTarget(%p)",
        static_cast<void *>(m_opaque_sp.get()), filename,
        static_cast<void *>(target_sp.get()));
  return sb_target;
}

SBTarget SBDebugger::GetDummyTarget() {
  SBTarget sb_target;
  if (m_opaque_sp) {
      sb_target.SetSP(m_opaque_sp->GetDummyTarget()->shared_from_this());
  }
  Log *log(GetLogIfAllCategoriesSet(LIBLLDB_LOG_API));
  if (log)
    log->Printf(
        "SBDebugger(%p)::GetDummyTarget() => SBTarget(%p)",
        static_cast<void *>(m_opaque_sp.get()),
        static_cast<void *>(sb_target.GetSP().get()));
  return sb_target;
}

bool SBDebugger::DeleteTarget(lldb::SBTarget &target) {
  bool result = false;
  if (m_opaque_sp) {
    TargetSP target_sp(target.GetSP());
    if (target_sp) {
      // No need to lock, the target list is thread safe
      result = m_opaque_sp->GetTargetList().DeleteTarget(target_sp);
      target_sp->Destroy();
      target.Clear();
      const bool mandatory = true;
      ModuleList::RemoveOrphanSharedModules(mandatory);
    }
  }

  Log *log(GetLogIfAllCategoriesSet(LIBLLDB_LOG_API));
  if (log)
    log->Printf("SBDebugger(%p)::DeleteTarget (SBTarget(%p)) => %i",
                static_cast<void *>(m_opaque_sp.get()),
                static_cast<void *>(target.m_opaque_sp.get()), result);

  return result;
}

SBTarget SBDebugger::GetTargetAtIndex(uint32_t idx) {
  SBTarget sb_target;
  if (m_opaque_sp) {
    // No need to lock, the target list is thread safe
    sb_target.SetSP(m_opaque_sp->GetTargetList().GetTargetAtIndex(idx));
  }
  return sb_target;
}

uint32_t SBDebugger::GetIndexOfTarget(lldb::SBTarget target) {

  lldb::TargetSP target_sp = target.GetSP();
  if (!target_sp)
    return UINT32_MAX;

  if (!m_opaque_sp)
    return UINT32_MAX;

  return m_opaque_sp->GetTargetList().GetIndexOfTarget(target.GetSP());
}

SBTarget SBDebugger::FindTargetWithProcessID(lldb::pid_t pid) {
  SBTarget sb_target;
  if (m_opaque_sp) {
    // No need to lock, the target list is thread safe
    sb_target.SetSP(m_opaque_sp->GetTargetList().FindTargetWithProcessID(pid));
  }
  return sb_target;
}

SBTarget SBDebugger::FindTargetWithFileAndArch(const char *filename,
                                               const char *arch_name) {
  SBTarget sb_target;
  if (m_opaque_sp && filename && filename[0]) {
    // No need to lock, the target list is thread safe
    ArchSpec arch = Platform::GetAugmentedArchSpec(
        m_opaque_sp->GetPlatformList().GetSelectedPlatform().get(), arch_name);
    TargetSP target_sp(
        m_opaque_sp->GetTargetList().FindTargetWithExecutableAndArchitecture(
            FileSpec(filename), arch_name ? &arch : nullptr));
    sb_target.SetSP(target_sp);
  }
  return sb_target;
}

SBTarget SBDebugger::FindTargetWithLLDBProcess(const ProcessSP &process_sp) {
  SBTarget sb_target;
  if (m_opaque_sp) {
    // No need to lock, the target list is thread safe
    sb_target.SetSP(
        m_opaque_sp->GetTargetList().FindTargetWithProcess(process_sp.get()));
  }
  return sb_target;
}

uint32_t SBDebugger::GetNumTargets() {
  if (m_opaque_sp) {
    // No need to lock, the target list is thread safe
    return m_opaque_sp->GetTargetList().GetNumTargets();
  }
  return 0;
}

SBTarget SBDebugger::GetSelectedTarget() {
  Log *log(GetLogIfAllCategoriesSet(LIBLLDB_LOG_API));

  SBTarget sb_target;
  TargetSP target_sp;
  if (m_opaque_sp) {
    // No need to lock, the target list is thread safe
    target_sp = m_opaque_sp->GetTargetList().GetSelectedTarget();
    sb_target.SetSP(target_sp);
  }

  if (log) {
    SBStream sstr;
    sb_target.GetDescription(sstr, eDescriptionLevelBrief);
    log->Printf("SBDebugger(%p)::GetSelectedTarget () => SBTarget(%p): %s",
                static_cast<void *>(m_opaque_sp.get()),
                static_cast<void *>(target_sp.get()), sstr.GetData());
  }

  return sb_target;
}

void SBDebugger::SetSelectedTarget(SBTarget &sb_target) {
  Log *log(GetLogIfAllCategoriesSet(LIBLLDB_LOG_API));

  TargetSP target_sp(sb_target.GetSP());
  if (m_opaque_sp) {
    m_opaque_sp->GetTargetList().SetSelectedTarget(target_sp.get());
  }
  if (log) {
    SBStream sstr;
    sb_target.GetDescription(sstr, eDescriptionLevelBrief);
    log->Printf("SBDebugger(%p)::SetSelectedTarget () => SBTarget(%p): %s",
                static_cast<void *>(m_opaque_sp.get()),
                static_cast<void *>(target_sp.get()), sstr.GetData());
  }
}

SBPlatform SBDebugger::GetSelectedPlatform() {
  Log *log(GetLogIfAllCategoriesSet(LIBLLDB_LOG_API));

  SBPlatform sb_platform;
  DebuggerSP debugger_sp(m_opaque_sp);
  if (debugger_sp) {
    sb_platform.SetSP(debugger_sp->GetPlatformList().GetSelectedPlatform());
  }
  if (log)
    log->Printf("SBDebugger(%p)::GetSelectedPlatform () => SBPlatform(%p): %s",
                static_cast<void *>(m_opaque_sp.get()),
                static_cast<void *>(sb_platform.GetSP().get()),
                sb_platform.GetName());
  return sb_platform;
}

void SBDebugger::SetSelectedPlatform(SBPlatform &sb_platform) {
  Log *log(GetLogIfAllCategoriesSet(LIBLLDB_LOG_API));

  DebuggerSP debugger_sp(m_opaque_sp);
  if (debugger_sp) {
    debugger_sp->GetPlatformList().SetSelectedPlatform(sb_platform.GetSP());
  }

  if (log)
    log->Printf("SBDebugger(%p)::SetSelectedPlatform (SBPlatform(%p) %s)",
                static_cast<void *>(m_opaque_sp.get()),
                static_cast<void *>(sb_platform.GetSP().get()),
                sb_platform.GetName());
}

uint32_t SBDebugger::GetNumPlatforms() {
  if (m_opaque_sp) {
    // No need to lock, the platform list is thread safe
    return m_opaque_sp->GetPlatformList().GetSize();
  }
  return 0;
}

SBPlatform SBDebugger::GetPlatformAtIndex(uint32_t idx) {
  SBPlatform sb_platform;
  if (m_opaque_sp) {
    // No need to lock, the platform list is thread safe
    sb_platform.SetSP(m_opaque_sp->GetPlatformList().GetAtIndex(idx));
  }
  return sb_platform;
}

uint32_t SBDebugger::GetNumAvailablePlatforms() {
  uint32_t idx = 0;
  while (true) {
    if (!PluginManager::GetPlatformPluginNameAtIndex(idx)) {
      break;
    }
    ++idx;
  }
  // +1 for the host platform, which should always appear first in the list.
  return idx + 1;
}

SBStructuredData SBDebugger::GetAvailablePlatformInfoAtIndex(uint32_t idx) {
  SBStructuredData data;
  auto platform_dict = llvm::make_unique<StructuredData::Dictionary>();
  llvm::StringRef name_str("name"), desc_str("description");

  if (idx == 0) {
    PlatformSP host_platform_sp(Platform::GetHostPlatform());
    platform_dict->AddStringItem(
        name_str, host_platform_sp->GetPluginName().GetStringRef());
    platform_dict->AddStringItem(
        desc_str, llvm::StringRef(host_platform_sp->GetDescription()));
  } else if (idx > 0) {
    const char *plugin_name =
        PluginManager::GetPlatformPluginNameAtIndex(idx - 1);
    if (!plugin_name) {
      return data;
    }
    platform_dict->AddStringItem(name_str, llvm::StringRef(plugin_name));

    const char *plugin_desc =
        PluginManager::GetPlatformPluginDescriptionAtIndex(idx - 1);
    if (!plugin_desc) {
      return data;
    }
    platform_dict->AddStringItem(desc_str, llvm::StringRef(plugin_desc));
  }

  data.m_impl_up->SetObjectSP(
      StructuredData::ObjectSP(platform_dict.release()));
  return data;
}

void SBDebugger::DispatchInput(void *baton, const void *data, size_t data_len) {
  DispatchInput(data, data_len);
}

void SBDebugger::DispatchInput(const void *data, size_t data_len) {
  //    Log *log(GetLogIfAllCategoriesSet (LIBLLDB_LOG_API));
  //
  //    if (log)
  //        log->Printf ("SBDebugger(%p)::DispatchInput (data=\"%.*s\",
  //        size_t=%" PRIu64 ")",
  //                     m_opaque_sp.get(),
  //                     (int) data_len,
  //                     (const char *) data,
  //                     (uint64_t)data_len);
  //
  //    if (m_opaque_sp)
  //        m_opaque_sp->DispatchInput ((const char *) data, data_len);
}

void SBDebugger::DispatchInputInterrupt() {
  if (m_opaque_sp)
    m_opaque_sp->DispatchInputInterrupt();
}

void SBDebugger::DispatchInputEndOfFile() {
  if (m_opaque_sp)
    m_opaque_sp->DispatchInputEndOfFile();
}

void SBDebugger::PushInputReader(SBInputReader &reader) {}

void SBDebugger::RunCommandInterpreter(bool auto_handle_events,
                                       bool spawn_thread) {
  if (m_opaque_sp) {
    CommandInterpreterRunOptions options;

    m_opaque_sp->GetCommandInterpreter().RunCommandInterpreter(
        auto_handle_events, spawn_thread, options);
  }
}

void SBDebugger::RunCommandInterpreter(bool auto_handle_events,
                                       bool spawn_thread,
                                       SBCommandInterpreterRunOptions &options,
                                       int &num_errors, bool &quit_requested,
                                       bool &stopped_for_crash)

{
  if (m_opaque_sp) {
    CommandInterpreter &interp = m_opaque_sp->GetCommandInterpreter();
    interp.RunCommandInterpreter(auto_handle_events, spawn_thread,
                                 options.ref());
    num_errors = interp.GetNumErrors();
    quit_requested = interp.GetQuitRequested();
    stopped_for_crash = interp.GetStoppedForCrash();
  }
}

SBError SBDebugger::RunREPL(lldb::LanguageType language,
                            const char *repl_options) {
  SBError error;
  if (m_opaque_sp)
    error.ref() = m_opaque_sp->RunREPL(language, repl_options);
  else
    error.SetErrorString("invalid debugger");
  return error;
}

void SBDebugger::reset(const DebuggerSP &debugger_sp) {
  m_opaque_sp = debugger_sp;
}

Debugger *SBDebugger::get() const { return m_opaque_sp.get(); }

Debugger &SBDebugger::ref() const {
  assert(m_opaque_sp.get());
  return *m_opaque_sp;
}

const lldb::DebuggerSP &SBDebugger::get_sp() const { return m_opaque_sp; }

SBDebugger SBDebugger::FindDebuggerWithID(int id) {
  // No need to lock, the debugger list is thread safe
  SBDebugger sb_debugger;
  DebuggerSP debugger_sp = Debugger::FindDebuggerWithID(id);
  if (debugger_sp)
    sb_debugger.reset(debugger_sp);
  return sb_debugger;
}

const char *SBDebugger::GetInstanceName() {
  return (m_opaque_sp ? m_opaque_sp->GetInstanceName().AsCString() : nullptr);
}

SBError SBDebugger::SetInternalVariable(const char *var_name, const char *value,
                                        const char *debugger_instance_name) {
  SBError sb_error;
  DebuggerSP debugger_sp(Debugger::FindDebuggerWithInstanceName(
      ConstString(debugger_instance_name)));
  Status error;
  if (debugger_sp) {
    ExecutionContext exe_ctx(
        debugger_sp->GetCommandInterpreter().GetExecutionContext());
    error = debugger_sp->SetPropertyValue(&exe_ctx, eVarSetOperationAssign,
                                          var_name, value);
  } else {
    error.SetErrorStringWithFormat("invalid debugger instance name '%s'",
                                   debugger_instance_name);
  }
  if (error.Fail())
    sb_error.SetError(error);
  return sb_error;
}

SBStringList
SBDebugger::GetInternalVariableValue(const char *var_name,
                                     const char *debugger_instance_name) {
  SBStringList ret_value;
  DebuggerSP debugger_sp(Debugger::FindDebuggerWithInstanceName(
      ConstString(debugger_instance_name)));
  Status error;
  if (debugger_sp) {
    ExecutionContext exe_ctx(
        debugger_sp->GetCommandInterpreter().GetExecutionContext());
    lldb::OptionValueSP value_sp(
        debugger_sp->GetPropertyValue(&exe_ctx, var_name, false, error));
    if (value_sp) {
      StreamString value_strm;
      value_sp->DumpValue(&exe_ctx, value_strm, OptionValue::eDumpOptionValue);
      const std::string &value_str = value_strm.GetString();
      if (!value_str.empty()) {
        StringList string_list;
        string_list.SplitIntoLines(value_str);
        return SBStringList(&string_list);
      }
    }
  }
  return SBStringList();
}

uint32_t SBDebugger::GetTerminalWidth() const {
  return (m_opaque_sp ? m_opaque_sp->GetTerminalWidth() : 0);
}

void SBDebugger::SetTerminalWidth(uint32_t term_width) {
  if (m_opaque_sp)
    m_opaque_sp->SetTerminalWidth(term_width);
}

const char *SBDebugger::GetPrompt() const {
  Log *log(GetLogIfAllCategoriesSet(LIBLLDB_LOG_API));

  if (log)
    log->Printf("SBDebugger(%p)::GetPrompt () => \"%s\"",
                static_cast<void *>(m_opaque_sp.get()),
                (m_opaque_sp ? m_opaque_sp->GetPrompt().str().c_str() : ""));

  return (m_opaque_sp ? ConstString(m_opaque_sp->GetPrompt()).GetCString()
                      : nullptr);
}

void SBDebugger::SetPrompt(const char *prompt) {
  if (m_opaque_sp)
    m_opaque_sp->SetPrompt(llvm::StringRef::withNullAsEmpty(prompt));
}

const char *SBDebugger::GetReproducerPath() const {
  return (m_opaque_sp
              ? ConstString(m_opaque_sp->GetReproducerPath()).GetCString()
              : nullptr);
}

ScriptLanguage SBDebugger::GetScriptLanguage() const {
  return (m_opaque_sp ? m_opaque_sp->GetScriptLanguage() : eScriptLanguageNone);
}

void SBDebugger::SetScriptLanguage(ScriptLanguage script_lang) {
  if (m_opaque_sp) {
    m_opaque_sp->SetScriptLanguage(script_lang);
  }
}

bool SBDebugger::SetUseExternalEditor(bool value) {
  return (m_opaque_sp ? m_opaque_sp->SetUseExternalEditor(value) : false);
}

bool SBDebugger::GetUseExternalEditor() {
  return (m_opaque_sp ? m_opaque_sp->GetUseExternalEditor() : false);
}

bool SBDebugger::SetUseColor(bool value) {
  return (m_opaque_sp ? m_opaque_sp->SetUseColor(value) : false);
}

bool SBDebugger::GetUseColor() const {
  return (m_opaque_sp ? m_opaque_sp->GetUseColor() : false);
}

bool SBDebugger::GetDescription(SBStream &description) {
  Stream &strm = description.ref();

  if (m_opaque_sp) {
    const char *name = m_opaque_sp->GetInstanceName().AsCString();
    user_id_t id = m_opaque_sp->GetID();
    strm.Printf("Debugger (instance: \"%s\", id: %" PRIu64 ")", name, id);
  } else
    strm.PutCString("No value");

  return true;
}

user_id_t SBDebugger::GetID() {
  return (m_opaque_sp ? m_opaque_sp->GetID() : LLDB_INVALID_UID);
}

SBError SBDebugger::SetCurrentPlatform(const char *platform_name_cstr) {
  SBError sb_error;
  if (m_opaque_sp) {
    if (platform_name_cstr && platform_name_cstr[0]) {
      ConstString platform_name(platform_name_cstr);
      PlatformSP platform_sp(Platform::Find(platform_name));

      if (platform_sp) {
        // Already have a platform with this name, just select it
        m_opaque_sp->GetPlatformList().SetSelectedPlatform(platform_sp);
      } else {
        // We don't have a platform by this name yet, create one
        platform_sp = Platform::Create(platform_name, sb_error.ref());
        if (platform_sp) {
          // We created the platform, now append and select it
          bool make_selected = true;
          m_opaque_sp->GetPlatformList().Append(platform_sp, make_selected);
        }
      }
    } else {
      sb_error.ref().SetErrorString("invalid platform name");
    }
  } else {
    sb_error.ref().SetErrorString("invalid debugger");
  }
  return sb_error;
}

bool SBDebugger::SetCurrentPlatformSDKRoot(const char *sysroot) {
  Log *log(GetLogIfAllCategoriesSet(LIBLLDB_LOG_API));
  if (m_opaque_sp) {
    PlatformSP platform_sp(
        m_opaque_sp->GetPlatformList().GetSelectedPlatform());

    if (platform_sp) {
      if (log && sysroot)
        log->Printf("SBDebugger::SetCurrentPlatformSDKRoot (\"%s\")", sysroot);
      platform_sp->SetSDKRootDirectory(ConstString(sysroot));
      return true;
    }
  }
  return false;
}

bool SBDebugger::GetCloseInputOnEOF() const {
  return (m_opaque_sp ? m_opaque_sp->GetCloseInputOnEOF() : false);
}

void SBDebugger::SetCloseInputOnEOF(bool b) {
  if (m_opaque_sp)
    m_opaque_sp->SetCloseInputOnEOF(b);
}

SBTypeCategory SBDebugger::GetCategory(const char *category_name) {
  if (!category_name || *category_name == 0)
    return SBTypeCategory();

  TypeCategoryImplSP category_sp;

  if (DataVisualization::Categories::GetCategory(ConstString(category_name),
                                                 category_sp, false))
    return SBTypeCategory(category_sp);
  else
    return SBTypeCategory();
}

SBTypeCategory SBDebugger::GetCategory(lldb::LanguageType lang_type) {
  TypeCategoryImplSP category_sp;
  if (DataVisualization::Categories::GetCategory(lang_type, category_sp))
    return SBTypeCategory(category_sp);
  else
    return SBTypeCategory();
}

SBTypeCategory SBDebugger::CreateCategory(const char *category_name) {
  if (!category_name || *category_name == 0)
    return SBTypeCategory();

  TypeCategoryImplSP category_sp;

  if (DataVisualization::Categories::GetCategory(ConstString(category_name),
                                                 category_sp, true))
    return SBTypeCategory(category_sp);
  else
    return SBTypeCategory();
}

bool SBDebugger::DeleteCategory(const char *category_name) {
  if (!category_name || *category_name == 0)
    return false;

  return DataVisualization::Categories::Delete(ConstString(category_name));
}

uint32_t SBDebugger::GetNumCategories() {
  return DataVisualization::Categories::GetCount();
}

SBTypeCategory SBDebugger::GetCategoryAtIndex(uint32_t index) {
  return SBTypeCategory(
      DataVisualization::Categories::GetCategoryAtIndex(index));
}

SBTypeCategory SBDebugger::GetDefaultCategory() {
  return GetCategory("default");
}

SBTypeFormat SBDebugger::GetFormatForType(SBTypeNameSpecifier type_name) {
  SBTypeCategory default_category_sb = GetDefaultCategory();
  if (default_category_sb.GetEnabled())
    return default_category_sb.GetFormatForType(type_name);
  return SBTypeFormat();
}

#ifndef LLDB_DISABLE_PYTHON
SBTypeSummary SBDebugger::GetSummaryForType(SBTypeNameSpecifier type_name) {
  if (!type_name.IsValid())
    return SBTypeSummary();
  return SBTypeSummary(DataVisualization::GetSummaryForType(type_name.GetSP()));
}
#endif // LLDB_DISABLE_PYTHON

SBTypeFilter SBDebugger::GetFilterForType(SBTypeNameSpecifier type_name) {
  if (!type_name.IsValid())
    return SBTypeFilter();
  return SBTypeFilter(DataVisualization::GetFilterForType(type_name.GetSP()));
}

#ifndef LLDB_DISABLE_PYTHON
SBTypeSynthetic SBDebugger::GetSyntheticForType(SBTypeNameSpecifier type_name) {
  if (!type_name.IsValid())
    return SBTypeSynthetic();
  return SBTypeSynthetic(
      DataVisualization::GetSyntheticForType(type_name.GetSP()));
}
#endif // LLDB_DISABLE_PYTHON

static llvm::ArrayRef<const char *> GetCategoryArray(const char **categories) {
  if (categories == nullptr)
    return {};
  size_t len = 0;
  while (categories[len] != nullptr)
    ++len;
  return llvm::makeArrayRef(categories, len);
}

bool SBDebugger::EnableLog(const char *channel, const char **categories) {
  if (m_opaque_sp) {
    uint32_t log_options =
        LLDB_LOG_OPTION_PREPEND_TIMESTAMP | LLDB_LOG_OPTION_PREPEND_THREAD_NAME;
    std::string error;
    llvm::raw_string_ostream error_stream(error);
    return m_opaque_sp->EnableLog(channel, GetCategoryArray(categories), "",
                                  log_options, error_stream);
  } else
    return false;
}

void SBDebugger::SetLoggingCallback(lldb::LogOutputCallback log_callback,
                                    void *baton) {
  if (m_opaque_sp) {
    return m_opaque_sp->SetLoggingCallback(log_callback, baton);
  }
}
