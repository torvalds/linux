//===-- ProcessMinidump.cpp -------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "ProcessMinidump.h"
#include "ThreadMinidump.h"

#include "lldb/Core/DumpDataExtractor.h"
#include "lldb/Core/Module.h"
#include "lldb/Core/ModuleSpec.h"
#include "lldb/Core/PluginManager.h"
#include "lldb/Core/Section.h"
#include "lldb/Interpreter/CommandInterpreter.h"
#include "lldb/Interpreter/CommandObject.h"
#include "lldb/Interpreter/CommandObjectMultiword.h"
#include "lldb/Interpreter/CommandReturnObject.h"
#include "lldb/Interpreter/OptionArgParser.h"
#include "lldb/Interpreter/OptionGroupBoolean.h"
#include "lldb/Target/JITLoaderList.h"
#include "lldb/Target/MemoryRegionInfo.h"
#include "lldb/Target/SectionLoadList.h"
#include "lldb/Target/Target.h"
#include "lldb/Target/UnixSignals.h"
#include "lldb/Utility/LLDBAssert.h"
#include "lldb/Utility/Log.h"
#include "lldb/Utility/State.h"

#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/Threading.h"

#include "Plugins/Process/Utility/StopInfoMachException.h"

// C includes
// C++ includes

using namespace lldb;
using namespace lldb_private;
using namespace minidump;

//------------------------------------------------------------------
/// A placeholder module used for minidumps, where the original
/// object files may not be available (so we can't parse the object
/// files to extract the set of sections/segments)
///
/// This placeholder module has a single synthetic section (.module_image)
/// which represents the module memory range covering the whole module.
//------------------------------------------------------------------
class PlaceholderModule : public Module {
public:
  PlaceholderModule(const ModuleSpec &module_spec) :
    Module(module_spec.GetFileSpec(), module_spec.GetArchitecture()) {
    if (module_spec.GetUUID().IsValid())
      SetUUID(module_spec.GetUUID());
  }

  // Creates a synthetic module section covering the whole module image (and
  // sets the section load address as well)
  void CreateImageSection(const MinidumpModule *module, Target& target) {
    const ConstString section_name(".module_image");
    lldb::SectionSP section_sp(new Section(
        shared_from_this(),     // Module to which this section belongs.
        nullptr,                // ObjectFile
        0,                      // Section ID.
        section_name,           // Section name.
        eSectionTypeContainer,  // Section type.
        module->base_of_image,  // VM address.
        module->size_of_image,  // VM size in bytes of this section.
        0,                      // Offset of this section in the file.
        module->size_of_image,  // Size of the section as found in the file.
        12,                     // Alignment of the section (log2)
        0,                      // Flags for this section.
        1));                    // Number of host bytes per target byte
    section_sp->SetPermissions(ePermissionsExecutable | ePermissionsReadable);
    GetSectionList()->AddSection(section_sp);
    target.GetSectionLoadList().SetSectionLoadAddress(
        section_sp, module->base_of_image);
  }

ObjectFile *GetObjectFile() override { return nullptr; }

  SectionList *GetSectionList() override {
    return Module::GetUnifiedSectionList();
  }
};

ConstString ProcessMinidump::GetPluginNameStatic() {
  static ConstString g_name("minidump");
  return g_name;
}

const char *ProcessMinidump::GetPluginDescriptionStatic() {
  return "Minidump plug-in.";
}

lldb::ProcessSP ProcessMinidump::CreateInstance(lldb::TargetSP target_sp,
                                                lldb::ListenerSP listener_sp,
                                                const FileSpec *crash_file) {
  if (!crash_file)
    return nullptr;

  lldb::ProcessSP process_sp;
  // Read enough data for the Minidump header
  constexpr size_t header_size = sizeof(MinidumpHeader);
  auto DataPtr = FileSystem::Instance().CreateDataBuffer(crash_file->GetPath(),
                                                         header_size, 0);
  if (!DataPtr)
    return nullptr;

  lldbassert(DataPtr->GetByteSize() == header_size);

  // first, only try to parse the header, beacuse we need to be fast
  llvm::ArrayRef<uint8_t> HeaderBytes = DataPtr->GetData();
  const MinidumpHeader *header = MinidumpHeader::Parse(HeaderBytes);
  if (header == nullptr)
    return nullptr;

  auto AllData =
      FileSystem::Instance().CreateDataBuffer(crash_file->GetPath(), -1, 0);
  if (!AllData)
    return nullptr;

  auto minidump_parser = MinidumpParser::Create(AllData);
  // check if the parser object is valid
  if (!minidump_parser)
    return nullptr;

  return std::make_shared<ProcessMinidump>(target_sp, listener_sp, *crash_file,
                                           minidump_parser.getValue());
}

bool ProcessMinidump::CanDebug(lldb::TargetSP target_sp,
                               bool plugin_specified_by_name) {
  return true;
}

ProcessMinidump::ProcessMinidump(lldb::TargetSP target_sp,
                                 lldb::ListenerSP listener_sp,
                                 const FileSpec &core_file,
                                 MinidumpParser minidump_parser)
    : Process(target_sp, listener_sp), m_minidump_parser(minidump_parser),
      m_core_file(core_file), m_is_wow64(false) {}

ProcessMinidump::~ProcessMinidump() {
  Clear();
  // We need to call finalize on the process before destroying ourselves to
  // make sure all of the broadcaster cleanup goes as planned. If we destruct
  // this class, then Process::~Process() might have problems trying to fully
  // destroy the broadcaster.
  Finalize();
}

void ProcessMinidump::Initialize() {
  static llvm::once_flag g_once_flag;

  llvm::call_once(g_once_flag, []() {
    PluginManager::RegisterPlugin(GetPluginNameStatic(),
                                  GetPluginDescriptionStatic(),
                                  ProcessMinidump::CreateInstance);
  });
}

void ProcessMinidump::Terminate() {
  PluginManager::UnregisterPlugin(ProcessMinidump::CreateInstance);
}

Status ProcessMinidump::DoLoadCore() {
  Status error;

  // Minidump parser initialization & consistency checks
  error = m_minidump_parser.Initialize();
  if (error.Fail())
    return error;

  // Do we support the minidump's architecture?
  ArchSpec arch = GetArchitecture();
  switch (arch.GetMachine()) {
  case llvm::Triple::x86:
  case llvm::Triple::x86_64:
  case llvm::Triple::arm:
  case llvm::Triple::aarch64:
    // Any supported architectures must be listed here and also supported in
    // ThreadMinidump::CreateRegisterContextForFrame().
    break;
  default:
    error.SetErrorStringWithFormat("unsupported minidump architecture: %s",
                                   arch.GetArchitectureName());
    return error;
  }
  GetTarget().SetArchitecture(arch, true /*set_platform*/);

  m_thread_list = m_minidump_parser.GetThreads();
  m_active_exception = m_minidump_parser.GetExceptionStream();
  ReadModuleList();

  llvm::Optional<lldb::pid_t> pid = m_minidump_parser.GetPid();
  if (!pid) {
    error.SetErrorString("failed to parse PID");
    return error;
  }
  SetID(pid.getValue());

  return error;
}

ConstString ProcessMinidump::GetPluginName() { return GetPluginNameStatic(); }

uint32_t ProcessMinidump::GetPluginVersion() { return 1; }

Status ProcessMinidump::DoDestroy() { return Status(); }

void ProcessMinidump::RefreshStateAfterStop() {
  if (!m_active_exception)
    return;

  if (m_active_exception->exception_record.exception_code ==
      MinidumpException::DumpRequested) {
    return;
  }

  lldb::StopInfoSP stop_info;
  lldb::ThreadSP stop_thread;

  Process::m_thread_list.SetSelectedThreadByID(m_active_exception->thread_id);
  stop_thread = Process::m_thread_list.GetSelectedThread();
  ArchSpec arch = GetArchitecture();

  if (arch.GetTriple().getOS() == llvm::Triple::Linux) {
    stop_info = StopInfo::CreateStopReasonWithSignal(
        *stop_thread, m_active_exception->exception_record.exception_code);
  } else if (arch.GetTriple().getVendor() == llvm::Triple::Apple) {
    stop_info = StopInfoMachException::CreateStopReasonWithMachException(
        *stop_thread, m_active_exception->exception_record.exception_code, 2,
        m_active_exception->exception_record.exception_flags,
        m_active_exception->exception_record.exception_address, 0);
  } else {
    std::string desc;
    llvm::raw_string_ostream desc_stream(desc);
    desc_stream << "Exception "
                << llvm::format_hex(
                       m_active_exception->exception_record.exception_code, 8)
                << " encountered at address "
                << llvm::format_hex(
                       m_active_exception->exception_record.exception_address,
                       8);
    stop_info = StopInfo::CreateStopReasonWithException(
        *stop_thread, desc_stream.str().c_str());
  }

  stop_thread->SetStopInfo(stop_info);
}

bool ProcessMinidump::IsAlive() { return true; }

bool ProcessMinidump::WarnBeforeDetach() const { return false; }

size_t ProcessMinidump::ReadMemory(lldb::addr_t addr, void *buf, size_t size,
                                   Status &error) {
  // Don't allow the caching that lldb_private::Process::ReadMemory does since
  // we have it all cached in our dump file anyway.
  return DoReadMemory(addr, buf, size, error);
}

size_t ProcessMinidump::DoReadMemory(lldb::addr_t addr, void *buf, size_t size,
                                     Status &error) {

  llvm::ArrayRef<uint8_t> mem = m_minidump_parser.GetMemory(addr, size);
  if (mem.empty()) {
    error.SetErrorString("could not parse memory info");
    return 0;
  }

  std::memcpy(buf, mem.data(), mem.size());
  return mem.size();
}

ArchSpec ProcessMinidump::GetArchitecture() {
  if (!m_is_wow64) {
    return m_minidump_parser.GetArchitecture();
  }

  llvm::Triple triple;
  triple.setVendor(llvm::Triple::VendorType::UnknownVendor);
  triple.setArch(llvm::Triple::ArchType::x86);
  triple.setOS(llvm::Triple::OSType::Win32);
  return ArchSpec(triple);
}

Status ProcessMinidump::GetMemoryRegionInfo(lldb::addr_t load_addr,
                                            MemoryRegionInfo &range_info) {
  range_info = m_minidump_parser.GetMemoryRegionInfo(load_addr);
  return Status();
}

Status ProcessMinidump::GetMemoryRegions(
    lldb_private::MemoryRegionInfos &region_list) {
  region_list = m_minidump_parser.GetMemoryRegions();
  return Status();
}

void ProcessMinidump::Clear() { Process::m_thread_list.Clear(); }

bool ProcessMinidump::UpdateThreadList(ThreadList &old_thread_list,
                                       ThreadList &new_thread_list) {
  for (const MinidumpThread& thread : m_thread_list) {
    MinidumpLocationDescriptor context_location = thread.thread_context;

    // If the minidump contains an exception context, use it
    if (m_active_exception != nullptr &&
        m_active_exception->thread_id == thread.thread_id) {
      context_location = m_active_exception->thread_context;
    }

    llvm::ArrayRef<uint8_t> context;
    if (!m_is_wow64)
      context = m_minidump_parser.GetThreadContext(context_location);
    else
      context = m_minidump_parser.GetThreadContextWow64(thread);

    lldb::ThreadSP thread_sp(new ThreadMinidump(*this, thread, context));
    new_thread_list.AddThread(thread_sp);
  }
  return new_thread_list.GetSize(false) > 0;
}

void ProcessMinidump::ReadModuleList() {
  std::vector<const MinidumpModule *> filtered_modules =
      m_minidump_parser.GetFilteredModuleList();

  for (auto module : filtered_modules) {
    llvm::Optional<std::string> name =
        m_minidump_parser.GetMinidumpString(module->module_name_rva);

    if (!name)
      continue;

    Log *log(GetLogIfAllCategoriesSet(LIBLLDB_LOG_MODULES));
    if (log) {
      log->Printf("ProcessMinidump::%s found module: name: %s %#010" PRIx64
                  "-%#010" PRIx64 " size: %" PRIu32,
                  __FUNCTION__, name.getValue().c_str(),
                  uint64_t(module->base_of_image),
                  module->base_of_image + module->size_of_image,
                  uint32_t(module->size_of_image));
    }

    // check if the process is wow64 - a 32 bit windows process running on a
    // 64 bit windows
    if (llvm::StringRef(name.getValue()).endswith_lower("wow64.dll")) {
      m_is_wow64 = true;
    }

    const auto uuid = m_minidump_parser.GetModuleUUID(module);
    auto file_spec = FileSpec(name.getValue(), GetArchitecture().GetTriple());
    FileSystem::Instance().Resolve(file_spec);
    ModuleSpec module_spec(file_spec, uuid);
    Status error;
    lldb::ModuleSP module_sp = GetTarget().GetSharedModule(module_spec, &error);
    if (!module_sp || error.Fail()) {
      // We failed to locate a matching local object file. Fortunately, the
      // minidump format encodes enough information about each module's memory
      // range to allow us to create placeholder modules.
      //
      // This enables most LLDB functionality involving address-to-module
      // translations (ex. identifing the module for a stack frame PC) and
      // modules/sections commands (ex. target modules list, ...)
      if (log) {
        log->Printf("Unable to locate the matching object file, creating a "
                    "placeholder module for: %s",
                    name.getValue().c_str());
      }

      auto placeholder_module =
          std::make_shared<PlaceholderModule>(module_spec);
      placeholder_module->CreateImageSection(module, GetTarget());
      module_sp = placeholder_module;
      GetTarget().GetImages().Append(module_sp);
    }

    if (log) {
      log->Printf("ProcessMinidump::%s load module: name: %s", __FUNCTION__,
                  name.getValue().c_str());
    }

    bool load_addr_changed = false;
    module_sp->SetLoadAddress(GetTarget(), module->base_of_image, false,
                              load_addr_changed);
  }
}

bool ProcessMinidump::GetProcessInfo(ProcessInstanceInfo &info) {
  info.Clear();
  info.SetProcessID(GetID());
  info.SetArchitecture(GetArchitecture());
  lldb::ModuleSP module_sp = GetTarget().GetExecutableModule();
  if (module_sp) {
    const bool add_exe_file_as_first_arg = false;
    info.SetExecutableFile(GetTarget().GetExecutableModule()->GetFileSpec(),
                           add_exe_file_as_first_arg);
  }
  return true;
}

// For minidumps there's no runtime generated code so we don't need JITLoader(s)
// Avoiding them will also speed up minidump loading since JITLoaders normally
// try to set up symbolic breakpoints, which in turn may force loading more
// debug information than needed.
JITLoaderList &ProcessMinidump::GetJITLoaders() {
  if (!m_jit_loaders_ap) {
    m_jit_loaders_ap = llvm::make_unique<JITLoaderList>();
  }
  return *m_jit_loaders_ap;
}

#define INIT_BOOL(VAR, LONG, SHORT, DESC) \
    VAR(LLDB_OPT_SET_1, false, LONG, SHORT, DESC, false, true)
#define APPEND_OPT(VAR) \
    m_option_group.Append(&VAR, LLDB_OPT_SET_ALL, LLDB_OPT_SET_1)

class CommandObjectProcessMinidumpDump : public CommandObjectParsed {
private:
  OptionGroupOptions m_option_group;
  OptionGroupBoolean m_dump_all;
  OptionGroupBoolean m_dump_directory;
  OptionGroupBoolean m_dump_linux_cpuinfo;
  OptionGroupBoolean m_dump_linux_proc_status;
  OptionGroupBoolean m_dump_linux_lsb_release;
  OptionGroupBoolean m_dump_linux_cmdline;
  OptionGroupBoolean m_dump_linux_environ;
  OptionGroupBoolean m_dump_linux_auxv;
  OptionGroupBoolean m_dump_linux_maps;
  OptionGroupBoolean m_dump_linux_proc_stat;
  OptionGroupBoolean m_dump_linux_proc_uptime;
  OptionGroupBoolean m_dump_linux_proc_fd;
  OptionGroupBoolean m_dump_linux_all;

  void SetDefaultOptionsIfNoneAreSet() {
    if (m_dump_all.GetOptionValue().GetCurrentValue() ||
        m_dump_linux_all.GetOptionValue().GetCurrentValue() ||
        m_dump_directory.GetOptionValue().GetCurrentValue() ||
        m_dump_linux_cpuinfo.GetOptionValue().GetCurrentValue() ||
        m_dump_linux_proc_status.GetOptionValue().GetCurrentValue() ||
        m_dump_linux_lsb_release.GetOptionValue().GetCurrentValue() ||
        m_dump_linux_cmdline.GetOptionValue().GetCurrentValue() ||
        m_dump_linux_environ.GetOptionValue().GetCurrentValue() ||
        m_dump_linux_auxv.GetOptionValue().GetCurrentValue() ||
        m_dump_linux_maps.GetOptionValue().GetCurrentValue() ||
        m_dump_linux_proc_stat.GetOptionValue().GetCurrentValue() ||
        m_dump_linux_proc_uptime.GetOptionValue().GetCurrentValue() ||
        m_dump_linux_proc_fd.GetOptionValue().GetCurrentValue())
      return;
    // If no options were set, then dump everything
    m_dump_all.GetOptionValue().SetCurrentValue(true);
  }
  bool DumpAll() const {
    return m_dump_all.GetOptionValue().GetCurrentValue();
  }
  bool DumpDirectory() const {
    return DumpAll() ||
        m_dump_directory.GetOptionValue().GetCurrentValue();
  }
  bool DumpLinux() const {
    return DumpAll() || m_dump_linux_all.GetOptionValue().GetCurrentValue();
  }
  bool DumpLinuxCPUInfo() const {
    return DumpLinux() ||
        m_dump_linux_cpuinfo.GetOptionValue().GetCurrentValue();
  }
  bool DumpLinuxProcStatus() const {
    return DumpLinux() ||
        m_dump_linux_proc_status.GetOptionValue().GetCurrentValue();
  }
  bool DumpLinuxProcStat() const {
    return DumpLinux() ||
        m_dump_linux_proc_stat.GetOptionValue().GetCurrentValue();
  }
  bool DumpLinuxLSBRelease() const {
    return DumpLinux() ||
        m_dump_linux_lsb_release.GetOptionValue().GetCurrentValue();
  }
  bool DumpLinuxCMDLine() const {
    return DumpLinux() ||
        m_dump_linux_cmdline.GetOptionValue().GetCurrentValue();
  }
  bool DumpLinuxEnviron() const {
    return DumpLinux() ||
        m_dump_linux_environ.GetOptionValue().GetCurrentValue();
  }
  bool DumpLinuxAuxv() const {
    return DumpLinux() ||
        m_dump_linux_auxv.GetOptionValue().GetCurrentValue();
  }
  bool DumpLinuxMaps() const {
    return DumpLinux() ||
        m_dump_linux_maps.GetOptionValue().GetCurrentValue();
  }
  bool DumpLinuxProcUptime() const {
    return DumpLinux() ||
        m_dump_linux_proc_uptime.GetOptionValue().GetCurrentValue();
  }
  bool DumpLinuxProcFD() const {
    return DumpLinux() ||
        m_dump_linux_proc_fd.GetOptionValue().GetCurrentValue();
  }
public:

  CommandObjectProcessMinidumpDump(CommandInterpreter &interpreter)
  : CommandObjectParsed(interpreter, "process plugin dump",
      "Dump information from the minidump file.", NULL),
    m_option_group(),
    INIT_BOOL(m_dump_all, "all", 'a',
              "Dump the everything in the minidump."),
    INIT_BOOL(m_dump_directory, "directory", 'd',
              "Dump the minidump directory map."),
    INIT_BOOL(m_dump_linux_cpuinfo, "cpuinfo", 'C',
              "Dump linux /proc/cpuinfo."),
    INIT_BOOL(m_dump_linux_proc_status, "status", 's',
              "Dump linux /proc/<pid>/status."),
    INIT_BOOL(m_dump_linux_lsb_release, "lsb-release", 'r',
              "Dump linux /etc/lsb-release."),
    INIT_BOOL(m_dump_linux_cmdline, "cmdline", 'c',
              "Dump linux /proc/<pid>/cmdline."),
    INIT_BOOL(m_dump_linux_environ, "environ", 'e',
              "Dump linux /proc/<pid>/environ."),
    INIT_BOOL(m_dump_linux_auxv, "auxv", 'x',
              "Dump linux /proc/<pid>/auxv."),
    INIT_BOOL(m_dump_linux_maps, "maps", 'm',
              "Dump linux /proc/<pid>/maps."),
    INIT_BOOL(m_dump_linux_proc_stat, "stat", 'S',
              "Dump linux /proc/<pid>/stat."),
    INIT_BOOL(m_dump_linux_proc_uptime, "uptime", 'u',
              "Dump linux process uptime."),
    INIT_BOOL(m_dump_linux_proc_fd, "fd", 'f',
              "Dump linux /proc/<pid>/fd."),
    INIT_BOOL(m_dump_linux_all, "linux", 'l',
              "Dump all linux streams.") {
    APPEND_OPT(m_dump_all);
    APPEND_OPT(m_dump_directory);
    APPEND_OPT(m_dump_linux_cpuinfo);
    APPEND_OPT(m_dump_linux_proc_status);
    APPEND_OPT(m_dump_linux_lsb_release);
    APPEND_OPT(m_dump_linux_cmdline);
    APPEND_OPT(m_dump_linux_environ);
    APPEND_OPT(m_dump_linux_auxv);
    APPEND_OPT(m_dump_linux_maps);
    APPEND_OPT(m_dump_linux_proc_stat);
    APPEND_OPT(m_dump_linux_proc_uptime);
    APPEND_OPT(m_dump_linux_proc_fd);
    APPEND_OPT(m_dump_linux_all);
    m_option_group.Finalize();
  }

  ~CommandObjectProcessMinidumpDump() {}

  Options *GetOptions() override { return &m_option_group; }

  bool DoExecute(Args &command, CommandReturnObject &result) override {
    const size_t argc = command.GetArgumentCount();
    if (argc > 0) {
      result.AppendErrorWithFormat("'%s' take no arguments, only options",
                                   m_cmd_name.c_str());
      result.SetStatus(eReturnStatusFailed);
      return false;
    }
    SetDefaultOptionsIfNoneAreSet();

    ProcessMinidump *process = static_cast<ProcessMinidump *>(
        m_interpreter.GetExecutionContext().GetProcessPtr());
    result.SetStatus(eReturnStatusSuccessFinishResult);
    Stream &s = result.GetOutputStream();
    MinidumpParser &minidump = process->m_minidump_parser;
    if (DumpDirectory()) {
      s.Printf("RVA        SIZE       TYPE       MinidumpStreamType\n");
      s.Printf("---------- ---------- ---------- --------------------------\n");
      for (const auto &pair: minidump.GetDirectoryMap())
        s.Printf("0x%8.8x 0x%8.8x 0x%8.8x %s\n", (uint32_t)pair.second.rva,
                 (uint32_t)pair.second.data_size, pair.first,
                 MinidumpParser::GetStreamTypeAsString(pair.first).data());
      s.Printf("\n");
    }
    auto DumpTextStream = [&](MinidumpStreamType stream_type,
                              llvm::StringRef label) -> void {
      auto bytes = minidump.GetStream(stream_type);
      if (!bytes.empty()) {
        if (label.empty())
          label = MinidumpParser::GetStreamTypeAsString((uint32_t)stream_type);
        s.Printf("%s:\n%s\n\n", label.data(), bytes.data());
      }
    };
    auto DumpBinaryStream = [&](MinidumpStreamType stream_type,
                                llvm::StringRef label) -> void {
      auto bytes = minidump.GetStream(stream_type);
      if (!bytes.empty()) {
        if (label.empty())
          label = MinidumpParser::GetStreamTypeAsString((uint32_t)stream_type);
        s.Printf("%s:\n", label.data());
        DataExtractor data(bytes.data(), bytes.size(), eByteOrderLittle,
                           process->GetAddressByteSize());
        DumpDataExtractor(data, &s, 0, lldb::eFormatBytesWithASCII, 1,
                          bytes.size(), 16, 0, 0, 0);
        s.Printf("\n\n");
      }
    };

    if (DumpLinuxCPUInfo())
      DumpTextStream(MinidumpStreamType::LinuxCPUInfo, "/proc/cpuinfo");
    if (DumpLinuxProcStatus())
      DumpTextStream(MinidumpStreamType::LinuxProcStatus, "/proc/PID/status");
    if (DumpLinuxLSBRelease())
      DumpTextStream(MinidumpStreamType::LinuxLSBRelease, "/etc/lsb-release");
    if (DumpLinuxCMDLine())
      DumpTextStream(MinidumpStreamType::LinuxCMDLine, "/proc/PID/cmdline");
    if (DumpLinuxEnviron())
      DumpTextStream(MinidumpStreamType::LinuxEnviron, "/proc/PID/environ");
    if (DumpLinuxAuxv())
      DumpBinaryStream(MinidumpStreamType::LinuxAuxv, "/proc/PID/auxv");
    if (DumpLinuxMaps())
      DumpTextStream(MinidumpStreamType::LinuxMaps, "/proc/PID/maps");
    if (DumpLinuxProcStat())
      DumpTextStream(MinidumpStreamType::LinuxProcStat, "/proc/PID/stat");
    if (DumpLinuxProcUptime())
      DumpTextStream(MinidumpStreamType::LinuxProcUptime, "uptime");
    if (DumpLinuxProcFD())
      DumpTextStream(MinidumpStreamType::LinuxProcFD, "/proc/PID/fd");
    return true;
  }
};

class CommandObjectMultiwordProcessMinidump : public CommandObjectMultiword {
public:
  CommandObjectMultiwordProcessMinidump(CommandInterpreter &interpreter)
    : CommandObjectMultiword(interpreter, "process plugin",
          "Commands for operating on a ProcessMinidump process.",
          "process plugin <subcommand> [<subcommand-options>]") {
    LoadSubCommand("dump",
        CommandObjectSP(new CommandObjectProcessMinidumpDump(interpreter)));
  }

  ~CommandObjectMultiwordProcessMinidump() {}
};

CommandObject *ProcessMinidump::GetPluginCommandObject() {
  if (!m_command_sp)
    m_command_sp.reset(new CommandObjectMultiwordProcessMinidump(
        GetTarget().GetDebugger().GetCommandInterpreter()));
  return m_command_sp.get();
}
