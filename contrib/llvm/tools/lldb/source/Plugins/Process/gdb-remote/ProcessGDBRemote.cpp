//===-- ProcessGDBRemote.cpp ------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "lldb/Host/Config.h"

#include <errno.h>
#include <stdlib.h>
#ifndef LLDB_DISABLE_POSIX
#include <netinet/in.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <unistd.h>
#endif
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>

#include <algorithm>
#include <csignal>
#include <map>
#include <mutex>
#include <sstream>

#include "lldb/Breakpoint/Watchpoint.h"
#include "lldb/Core/Debugger.h"
#include "lldb/Core/Module.h"
#include "lldb/Core/ModuleSpec.h"
#include "lldb/Core/PluginManager.h"
#include "lldb/Core/StreamFile.h"
#include "lldb/Core/Value.h"
#include "lldb/DataFormatters/FormatManager.h"
#include "lldb/Host/ConnectionFileDescriptor.h"
#include "lldb/Host/FileSystem.h"
#include "lldb/Host/HostThread.h"
#include "lldb/Host/PosixApi.h"
#include "lldb/Host/PseudoTerminal.h"
#include "lldb/Host/StringConvert.h"
#include "lldb/Host/Symbols.h"
#include "lldb/Host/ThreadLauncher.h"
#include "lldb/Host/XML.h"
#include "lldb/Interpreter/CommandInterpreter.h"
#include "lldb/Interpreter/CommandObject.h"
#include "lldb/Interpreter/CommandObjectMultiword.h"
#include "lldb/Interpreter/CommandReturnObject.h"
#include "lldb/Interpreter/OptionArgParser.h"
#include "lldb/Interpreter/OptionGroupBoolean.h"
#include "lldb/Interpreter/OptionGroupUInt64.h"
#include "lldb/Interpreter/OptionValueProperties.h"
#include "lldb/Interpreter/Options.h"
#include "lldb/Interpreter/Property.h"
#include "lldb/Symbol/ObjectFile.h"
#include "lldb/Target/ABI.h"
#include "lldb/Target/DynamicLoader.h"
#include "lldb/Target/MemoryRegionInfo.h"
#include "lldb/Target/SystemRuntime.h"
#include "lldb/Target/Target.h"
#include "lldb/Target/TargetList.h"
#include "lldb/Target/ThreadPlanCallFunction.h"
#include "lldb/Utility/Args.h"
#include "lldb/Utility/CleanUp.h"
#include "lldb/Utility/FileSpec.h"
#include "lldb/Utility/Reproducer.h"
#include "lldb/Utility/State.h"
#include "lldb/Utility/StreamString.h"
#include "lldb/Utility/Timer.h"

#include "GDBRemoteRegisterContext.h"
#ifdef LLDB_ENABLE_ALL
#include "Plugins/Platform/MacOSX/PlatformRemoteiOS.h"
#endif // LLDB_ENABLE_ALL
#include "Plugins/Process/Utility/GDBRemoteSignals.h"
#include "Plugins/Process/Utility/InferiorCallPOSIX.h"
#include "Plugins/Process/Utility/StopInfoMachException.h"
#include "ProcessGDBRemote.h"
#include "ProcessGDBRemoteLog.h"
#include "ThreadGDBRemote.h"
#include "lldb/Host/Host.h"
#include "lldb/Utility/StringExtractorGDBRemote.h"

#include "llvm/ADT/StringSwitch.h"
#include "llvm/Support/Threading.h"
#include "llvm/Support/raw_ostream.h"

#define DEBUGSERVER_BASENAME "debugserver"
using namespace llvm;
using namespace lldb;
using namespace lldb_private;
using namespace lldb_private::process_gdb_remote;

namespace lldb {
// Provide a function that can easily dump the packet history if we know a
// ProcessGDBRemote * value (which we can get from logs or from debugging). We
// need the function in the lldb namespace so it makes it into the final
// executable since the LLDB shared library only exports stuff in the lldb
// namespace. This allows you to attach with a debugger and call this function
// and get the packet history dumped to a file.
void DumpProcessGDBRemotePacketHistory(void *p, const char *path) {
  StreamFile strm;
  Status error = FileSystem::Instance().Open(strm.GetFile(), FileSpec(path),
                                             File::eOpenOptionWrite |
                                                 File::eOpenOptionCanCreate);
  if (error.Success())
    ((ProcessGDBRemote *)p)->GetGDBRemote().DumpHistory(strm);
}
} // namespace lldb

namespace {

static constexpr PropertyDefinition g_properties[] = {
    {"packet-timeout", OptionValue::eTypeUInt64, true, 1, NULL, {},
     "Specify the default packet timeout in seconds."},
    {"target-definition-file", OptionValue::eTypeFileSpec, true, 0, NULL, {},
     "The file that provides the description for remote target registers."}};

enum { ePropertyPacketTimeout, ePropertyTargetDefinitionFile };

class PluginProperties : public Properties {
public:
  static ConstString GetSettingName() {
    return ProcessGDBRemote::GetPluginNameStatic();
  }

  PluginProperties() : Properties() {
    m_collection_sp.reset(new OptionValueProperties(GetSettingName()));
    m_collection_sp->Initialize(g_properties);
  }

  virtual ~PluginProperties() {}

  uint64_t GetPacketTimeout() {
    const uint32_t idx = ePropertyPacketTimeout;
    return m_collection_sp->GetPropertyAtIndexAsUInt64(
        NULL, idx, g_properties[idx].default_uint_value);
  }

  bool SetPacketTimeout(uint64_t timeout) {
    const uint32_t idx = ePropertyPacketTimeout;
    return m_collection_sp->SetPropertyAtIndexAsUInt64(NULL, idx, timeout);
  }

  FileSpec GetTargetDefinitionFile() const {
    const uint32_t idx = ePropertyTargetDefinitionFile;
    return m_collection_sp->GetPropertyAtIndexAsFileSpec(NULL, idx);
  }
};

typedef std::shared_ptr<PluginProperties> ProcessKDPPropertiesSP;

static const ProcessKDPPropertiesSP &GetGlobalPluginProperties() {
  static ProcessKDPPropertiesSP g_settings_sp;
  if (!g_settings_sp)
    g_settings_sp.reset(new PluginProperties());
  return g_settings_sp;
}

class ProcessGDBRemoteProvider
    : public repro::Provider<ProcessGDBRemoteProvider> {
public:
  ProcessGDBRemoteProvider(const FileSpec &directory) : Provider(directory) {
    m_info.name = "gdb-remote";
    m_info.files.push_back("gdb-remote.yaml");
  }

  raw_ostream *GetHistoryStream() {
    FileSpec history_file =
        GetRoot().CopyByAppendingPathComponent("gdb-remote.yaml");

    std::error_code EC;
    m_stream_up = llvm::make_unique<raw_fd_ostream>(history_file.GetPath(), EC,
                                                    sys::fs::OpenFlags::F_None);
    return m_stream_up.get();
  }

  void SetCallback(std::function<void()> callback) {
    m_callback = std::move(callback);
  }

  void Keep() override { m_callback(); }

  void Discard() override { m_callback(); }

  static char ID;

private:
  std::function<void()> m_callback;
  std::unique_ptr<raw_fd_ostream> m_stream_up;
};

char ProcessGDBRemoteProvider::ID = 0;

} // namespace

// TODO Randomly assigning a port is unsafe.  We should get an unused
// ephemeral port from the kernel and make sure we reserve it before passing it
// to debugserver.

#if defined(__APPLE__)
#define LOW_PORT (IPPORT_RESERVED)
#define HIGH_PORT (IPPORT_HIFIRSTAUTO)
#else
#define LOW_PORT (1024u)
#define HIGH_PORT (49151u)
#endif

#if defined(__APPLE__) &&                                                      \
    (defined(__arm__) || defined(__arm64__) || defined(__aarch64__))
static bool rand_initialized = false;

static inline uint16_t get_random_port() {
  if (!rand_initialized) {
    time_t seed = time(NULL);

    rand_initialized = true;
    srand(seed);
  }
  return (rand() % (HIGH_PORT - LOW_PORT)) + LOW_PORT;
}
#endif

ConstString ProcessGDBRemote::GetPluginNameStatic() {
  static ConstString g_name("gdb-remote");
  return g_name;
}

const char *ProcessGDBRemote::GetPluginDescriptionStatic() {
  return "GDB Remote protocol based debugging plug-in.";
}

void ProcessGDBRemote::Terminate() {
  PluginManager::UnregisterPlugin(ProcessGDBRemote::CreateInstance);
}

lldb::ProcessSP
ProcessGDBRemote::CreateInstance(lldb::TargetSP target_sp,
                                 ListenerSP listener_sp,
                                 const FileSpec *crash_file_path) {
  lldb::ProcessSP process_sp;
  if (crash_file_path == NULL)
    process_sp.reset(new ProcessGDBRemote(target_sp, listener_sp));
  return process_sp;
}

bool ProcessGDBRemote::CanDebug(lldb::TargetSP target_sp,
                                bool plugin_specified_by_name) {
  if (plugin_specified_by_name)
    return true;

  // For now we are just making sure the file exists for a given module
  Module *exe_module = target_sp->GetExecutableModulePointer();
  if (exe_module) {
    ObjectFile *exe_objfile = exe_module->GetObjectFile();
    // We can't debug core files...
    switch (exe_objfile->GetType()) {
    case ObjectFile::eTypeInvalid:
    case ObjectFile::eTypeCoreFile:
    case ObjectFile::eTypeDebugInfo:
    case ObjectFile::eTypeObjectFile:
    case ObjectFile::eTypeSharedLibrary:
    case ObjectFile::eTypeStubLibrary:
    case ObjectFile::eTypeJIT:
      return false;
    case ObjectFile::eTypeExecutable:
    case ObjectFile::eTypeDynamicLinker:
    case ObjectFile::eTypeUnknown:
      break;
    }
    return FileSystem::Instance().Exists(exe_module->GetFileSpec());
  }
  // However, if there is no executable module, we return true since we might
  // be preparing to attach.
  return true;
}

//----------------------------------------------------------------------
// ProcessGDBRemote constructor
//----------------------------------------------------------------------
ProcessGDBRemote::ProcessGDBRemote(lldb::TargetSP target_sp,
                                   ListenerSP listener_sp)
    : Process(target_sp, listener_sp),
      m_debugserver_pid(LLDB_INVALID_PROCESS_ID), m_last_stop_packet_mutex(),
      m_register_info(),
      m_async_broadcaster(NULL, "lldb.process.gdb-remote.async-broadcaster"),
      m_async_listener_sp(
          Listener::MakeListener("lldb.process.gdb-remote.async-listener")),
      m_async_thread_state_mutex(), m_thread_ids(), m_thread_pcs(),
      m_jstopinfo_sp(), m_jthreadsinfo_sp(), m_continue_c_tids(),
      m_continue_C_tids(), m_continue_s_tids(), m_continue_S_tids(),
      m_max_memory_size(0), m_remote_stub_max_memory_size(0),
      m_addr_to_mmap_size(), m_thread_create_bp_sp(),
      m_waiting_for_attach(false), m_destroy_tried_resuming(false),
      m_command_sp(), m_breakpoint_pc_offset(0),
      m_initial_tid(LLDB_INVALID_THREAD_ID), m_replay_mode(false),
      m_allow_flash_writes(false), m_erased_flash_ranges() {
  m_async_broadcaster.SetEventName(eBroadcastBitAsyncThreadShouldExit,
                                   "async thread should exit");
  m_async_broadcaster.SetEventName(eBroadcastBitAsyncContinue,
                                   "async thread continue");
  m_async_broadcaster.SetEventName(eBroadcastBitAsyncThreadDidExit,
                                   "async thread did exit");

  if (repro::Generator *g = repro::Reproducer::Instance().GetGenerator()) {
    ProcessGDBRemoteProvider &provider =
        g->GetOrCreate<ProcessGDBRemoteProvider>();
    // Set the history stream to the stream owned by the provider.
    m_gdb_comm.SetHistoryStream(provider.GetHistoryStream());
    // Make sure to clear the stream again when we're finished.
    provider.SetCallback([&]() { m_gdb_comm.SetHistoryStream(nullptr); });
  }

  Log *log(ProcessGDBRemoteLog::GetLogIfAllCategoriesSet(GDBR_LOG_ASYNC));

  const uint32_t async_event_mask =
      eBroadcastBitAsyncContinue | eBroadcastBitAsyncThreadShouldExit;

  if (m_async_listener_sp->StartListeningForEvents(
          &m_async_broadcaster, async_event_mask) != async_event_mask) {
    if (log)
      log->Printf("ProcessGDBRemote::%s failed to listen for "
                  "m_async_broadcaster events",
                  __FUNCTION__);
  }

  const uint32_t gdb_event_mask =
      Communication::eBroadcastBitReadThreadDidExit |
      GDBRemoteCommunication::eBroadcastBitGdbReadThreadGotNotify;
  if (m_async_listener_sp->StartListeningForEvents(
          &m_gdb_comm, gdb_event_mask) != gdb_event_mask) {
    if (log)
      log->Printf("ProcessGDBRemote::%s failed to listen for m_gdb_comm events",
                  __FUNCTION__);
  }

  const uint64_t timeout_seconds =
      GetGlobalPluginProperties()->GetPacketTimeout();
  if (timeout_seconds > 0)
    m_gdb_comm.SetPacketTimeout(std::chrono::seconds(timeout_seconds));
}

//----------------------------------------------------------------------
// Destructor
//----------------------------------------------------------------------
ProcessGDBRemote::~ProcessGDBRemote() {
  //  m_mach_process.UnregisterNotificationCallbacks (this);
  Clear();
  // We need to call finalize on the process before destroying ourselves to
  // make sure all of the broadcaster cleanup goes as planned. If we destruct
  // this class, then Process::~Process() might have problems trying to fully
  // destroy the broadcaster.
  Finalize();

  // The general Finalize is going to try to destroy the process and that
  // SHOULD shut down the async thread.  However, if we don't kill it it will
  // get stranded and its connection will go away so when it wakes up it will
  // crash.  So kill it for sure here.
  StopAsyncThread();
  KillDebugserverProcess();
}

//----------------------------------------------------------------------
// PluginInterface
//----------------------------------------------------------------------
ConstString ProcessGDBRemote::GetPluginName() { return GetPluginNameStatic(); }

uint32_t ProcessGDBRemote::GetPluginVersion() { return 1; }

bool ProcessGDBRemote::ParsePythonTargetDefinition(
    const FileSpec &target_definition_fspec) {
  ScriptInterpreter *interpreter =
      GetTarget().GetDebugger().GetCommandInterpreter().GetScriptInterpreter();
  Status error;
  StructuredData::ObjectSP module_object_sp(
      interpreter->LoadPluginModule(target_definition_fspec, error));
  if (module_object_sp) {
    StructuredData::DictionarySP target_definition_sp(
        interpreter->GetDynamicSettings(module_object_sp, &GetTarget(),
                                        "gdb-server-target-definition", error));

    if (target_definition_sp) {
      StructuredData::ObjectSP target_object(
          target_definition_sp->GetValueForKey("host-info"));
      if (target_object) {
        if (auto host_info_dict = target_object->GetAsDictionary()) {
          StructuredData::ObjectSP triple_value =
              host_info_dict->GetValueForKey("triple");
          if (auto triple_string_value = triple_value->GetAsString()) {
            std::string triple_string = triple_string_value->GetValue();
            ArchSpec host_arch(triple_string.c_str());
            if (!host_arch.IsCompatibleMatch(GetTarget().GetArchitecture())) {
              GetTarget().SetArchitecture(host_arch);
            }
          }
        }
      }
      m_breakpoint_pc_offset = 0;
      StructuredData::ObjectSP breakpoint_pc_offset_value =
          target_definition_sp->GetValueForKey("breakpoint-pc-offset");
      if (breakpoint_pc_offset_value) {
        if (auto breakpoint_pc_int_value =
                breakpoint_pc_offset_value->GetAsInteger())
          m_breakpoint_pc_offset = breakpoint_pc_int_value->GetValue();
      }

      if (m_register_info.SetRegisterInfo(*target_definition_sp,
                                          GetTarget().GetArchitecture()) > 0) {
        return true;
      }
    }
  }
  return false;
}

// If the remote stub didn't give us eh_frame or DWARF register numbers for a
// register, see if the ABI can provide them.
// DWARF and eh_frame register numbers are defined as a part of the ABI.
static void AugmentRegisterInfoViaABI(RegisterInfo &reg_info,
                                      ConstString reg_name, ABISP abi_sp) {
  if (reg_info.kinds[eRegisterKindEHFrame] == LLDB_INVALID_REGNUM ||
      reg_info.kinds[eRegisterKindDWARF] == LLDB_INVALID_REGNUM) {
    if (abi_sp) {
      RegisterInfo abi_reg_info;
      if (abi_sp->GetRegisterInfoByName(reg_name, abi_reg_info)) {
        if (reg_info.kinds[eRegisterKindEHFrame] == LLDB_INVALID_REGNUM &&
            abi_reg_info.kinds[eRegisterKindEHFrame] != LLDB_INVALID_REGNUM) {
          reg_info.kinds[eRegisterKindEHFrame] =
              abi_reg_info.kinds[eRegisterKindEHFrame];
        }
        if (reg_info.kinds[eRegisterKindDWARF] == LLDB_INVALID_REGNUM &&
            abi_reg_info.kinds[eRegisterKindDWARF] != LLDB_INVALID_REGNUM) {
          reg_info.kinds[eRegisterKindDWARF] =
              abi_reg_info.kinds[eRegisterKindDWARF];
        }
        if (reg_info.kinds[eRegisterKindGeneric] == LLDB_INVALID_REGNUM &&
            abi_reg_info.kinds[eRegisterKindGeneric] != LLDB_INVALID_REGNUM) {
          reg_info.kinds[eRegisterKindGeneric] =
              abi_reg_info.kinds[eRegisterKindGeneric];
        }
      }
    }
  }
}

static size_t SplitCommaSeparatedRegisterNumberString(
    const llvm::StringRef &comma_separated_regiter_numbers,
    std::vector<uint32_t> &regnums, int base) {
  regnums.clear();
  std::pair<llvm::StringRef, llvm::StringRef> value_pair;
  value_pair.second = comma_separated_regiter_numbers;
  do {
    value_pair = value_pair.second.split(',');
    if (!value_pair.first.empty()) {
      uint32_t reg = StringConvert::ToUInt32(value_pair.first.str().c_str(),
                                             LLDB_INVALID_REGNUM, base);
      if (reg != LLDB_INVALID_REGNUM)
        regnums.push_back(reg);
    }
  } while (!value_pair.second.empty());
  return regnums.size();
}

void ProcessGDBRemote::BuildDynamicRegisterInfo(bool force) {
  if (!force && m_register_info.GetNumRegisters() > 0)
    return;

  m_register_info.Clear();

  // Check if qHostInfo specified a specific packet timeout for this
  // connection. If so then lets update our setting so the user knows what the
  // timeout is and can see it.
  const auto host_packet_timeout = m_gdb_comm.GetHostDefaultPacketTimeout();
  if (host_packet_timeout > std::chrono::seconds(0)) {
    GetGlobalPluginProperties()->SetPacketTimeout(host_packet_timeout.count());
  }

  // Register info search order:
  //     1 - Use the target definition python file if one is specified.
  //     2 - If the target definition doesn't have any of the info from the
  //     target.xml (registers) then proceed to read the target.xml.
  //     3 - Fall back on the qRegisterInfo packets.

  FileSpec target_definition_fspec =
      GetGlobalPluginProperties()->GetTargetDefinitionFile();
  if (!FileSystem::Instance().Exists(target_definition_fspec)) {
    // If the filename doesn't exist, it may be a ~ not having been expanded -
    // try to resolve it.
    FileSystem::Instance().Resolve(target_definition_fspec);
  }
  if (target_definition_fspec) {
    // See if we can get register definitions from a python file
    if (ParsePythonTargetDefinition(target_definition_fspec)) {
      return;
    } else {
      StreamSP stream_sp = GetTarget().GetDebugger().GetAsyncOutputStream();
      stream_sp->Printf("ERROR: target description file %s failed to parse.\n",
                        target_definition_fspec.GetPath().c_str());
    }
  }

  const ArchSpec &target_arch = GetTarget().GetArchitecture();
  const ArchSpec &remote_host_arch = m_gdb_comm.GetHostArchitecture();
  const ArchSpec &remote_process_arch = m_gdb_comm.GetProcessArchitecture();

  // Use the process' architecture instead of the host arch, if available
  ArchSpec arch_to_use;
  if (remote_process_arch.IsValid())
    arch_to_use = remote_process_arch;
  else
    arch_to_use = remote_host_arch;

  if (!arch_to_use.IsValid())
    arch_to_use = target_arch;

  if (GetGDBServerRegisterInfo(arch_to_use))
    return;

  char packet[128];
  uint32_t reg_offset = 0;
  uint32_t reg_num = 0;
  for (StringExtractorGDBRemote::ResponseType response_type =
           StringExtractorGDBRemote::eResponse;
       response_type == StringExtractorGDBRemote::eResponse; ++reg_num) {
    const int packet_len =
        ::snprintf(packet, sizeof(packet), "qRegisterInfo%x", reg_num);
    assert(packet_len < (int)sizeof(packet));
    UNUSED_IF_ASSERT_DISABLED(packet_len);
    StringExtractorGDBRemote response;
    if (m_gdb_comm.SendPacketAndWaitForResponse(packet, response, false) ==
        GDBRemoteCommunication::PacketResult::Success) {
      response_type = response.GetResponseType();
      if (response_type == StringExtractorGDBRemote::eResponse) {
        llvm::StringRef name;
        llvm::StringRef value;
        ConstString reg_name;
        ConstString alt_name;
        ConstString set_name;
        std::vector<uint32_t> value_regs;
        std::vector<uint32_t> invalidate_regs;
        std::vector<uint8_t> dwarf_opcode_bytes;
        RegisterInfo reg_info = {
            NULL,          // Name
            NULL,          // Alt name
            0,             // byte size
            reg_offset,    // offset
            eEncodingUint, // encoding
            eFormatHex,    // format
            {
                LLDB_INVALID_REGNUM, // eh_frame reg num
                LLDB_INVALID_REGNUM, // DWARF reg num
                LLDB_INVALID_REGNUM, // generic reg num
                reg_num,             // process plugin reg num
                reg_num              // native register number
            },
            NULL,
            NULL,
            NULL, // Dwarf expression opcode bytes pointer
            0     // Dwarf expression opcode bytes length
        };

        while (response.GetNameColonValue(name, value)) {
          if (name.equals("name")) {
            reg_name.SetString(value);
          } else if (name.equals("alt-name")) {
            alt_name.SetString(value);
          } else if (name.equals("bitsize")) {
            value.getAsInteger(0, reg_info.byte_size);
            reg_info.byte_size /= CHAR_BIT;
          } else if (name.equals("offset")) {
            if (value.getAsInteger(0, reg_offset))
              reg_offset = UINT32_MAX;
          } else if (name.equals("encoding")) {
            const Encoding encoding = Args::StringToEncoding(value);
            if (encoding != eEncodingInvalid)
              reg_info.encoding = encoding;
          } else if (name.equals("format")) {
            Format format = eFormatInvalid;
            if (OptionArgParser::ToFormat(value.str().c_str(), format, NULL)
                    .Success())
              reg_info.format = format;
            else {
              reg_info.format =
                  llvm::StringSwitch<Format>(value)
                      .Case("binary", eFormatBinary)
                      .Case("decimal", eFormatDecimal)
                      .Case("hex", eFormatHex)
                      .Case("float", eFormatFloat)
                      .Case("vector-sint8", eFormatVectorOfSInt8)
                      .Case("vector-uint8", eFormatVectorOfUInt8)
                      .Case("vector-sint16", eFormatVectorOfSInt16)
                      .Case("vector-uint16", eFormatVectorOfUInt16)
                      .Case("vector-sint32", eFormatVectorOfSInt32)
                      .Case("vector-uint32", eFormatVectorOfUInt32)
                      .Case("vector-float32", eFormatVectorOfFloat32)
                      .Case("vector-uint64", eFormatVectorOfUInt64)
                      .Case("vector-uint128", eFormatVectorOfUInt128)
                      .Default(eFormatInvalid);
            }
          } else if (name.equals("set")) {
            set_name.SetString(value);
          } else if (name.equals("gcc") || name.equals("ehframe")) {
            if (value.getAsInteger(0, reg_info.kinds[eRegisterKindEHFrame]))
              reg_info.kinds[eRegisterKindEHFrame] = LLDB_INVALID_REGNUM;
          } else if (name.equals("dwarf")) {
            if (value.getAsInteger(0, reg_info.kinds[eRegisterKindDWARF]))
              reg_info.kinds[eRegisterKindDWARF] = LLDB_INVALID_REGNUM;
          } else if (name.equals("generic")) {
            reg_info.kinds[eRegisterKindGeneric] =
                Args::StringToGenericRegister(value);
          } else if (name.equals("container-regs")) {
            SplitCommaSeparatedRegisterNumberString(value, value_regs, 16);
          } else if (name.equals("invalidate-regs")) {
            SplitCommaSeparatedRegisterNumberString(value, invalidate_regs, 16);
          } else if (name.equals("dynamic_size_dwarf_expr_bytes")) {
            size_t dwarf_opcode_len = value.size() / 2;
            assert(dwarf_opcode_len > 0);

            dwarf_opcode_bytes.resize(dwarf_opcode_len);
            reg_info.dynamic_size_dwarf_len = dwarf_opcode_len;

            StringExtractor opcode_extractor(value);
            uint32_t ret_val =
                opcode_extractor.GetHexBytesAvail(dwarf_opcode_bytes);
            assert(dwarf_opcode_len == ret_val);
            UNUSED_IF_ASSERT_DISABLED(ret_val);
            reg_info.dynamic_size_dwarf_expr_bytes = dwarf_opcode_bytes.data();
          }
        }

        reg_info.byte_offset = reg_offset;
        assert(reg_info.byte_size != 0);
        reg_offset += reg_info.byte_size;
        if (!value_regs.empty()) {
          value_regs.push_back(LLDB_INVALID_REGNUM);
          reg_info.value_regs = value_regs.data();
        }
        if (!invalidate_regs.empty()) {
          invalidate_regs.push_back(LLDB_INVALID_REGNUM);
          reg_info.invalidate_regs = invalidate_regs.data();
        }

        // We have to make a temporary ABI here, and not use the GetABI because
        // this code gets called in DidAttach, when the target architecture
        // (and consequently the ABI we'll get from the process) may be wrong.
        ABISP abi_to_use = ABI::FindPlugin(shared_from_this(), arch_to_use);

        AugmentRegisterInfoViaABI(reg_info, reg_name, abi_to_use);

        m_register_info.AddRegister(reg_info, reg_name, alt_name, set_name);
      } else {
        break; // ensure exit before reg_num is incremented
      }
    } else {
      break;
    }
  }

  if (m_register_info.GetNumRegisters() > 0) {
    m_register_info.Finalize(GetTarget().GetArchitecture());
    return;
  }

  // We didn't get anything if the accumulated reg_num is zero.  See if we are
  // debugging ARM and fill with a hard coded register set until we can get an
  // updated debugserver down on the devices. On the other hand, if the
  // accumulated reg_num is positive, see if we can add composite registers to
  // the existing primordial ones.
  bool from_scratch = (m_register_info.GetNumRegisters() == 0);

  if (!target_arch.IsValid()) {
    if (arch_to_use.IsValid() &&
        (arch_to_use.GetMachine() == llvm::Triple::arm ||
         arch_to_use.GetMachine() == llvm::Triple::thumb) &&
        arch_to_use.GetTriple().getVendor() == llvm::Triple::Apple)
      m_register_info.HardcodeARMRegisters(from_scratch);
  } else if (target_arch.GetMachine() == llvm::Triple::arm ||
             target_arch.GetMachine() == llvm::Triple::thumb) {
    m_register_info.HardcodeARMRegisters(from_scratch);
  }

  // At this point, we can finalize our register info.
  m_register_info.Finalize(GetTarget().GetArchitecture());
}

Status ProcessGDBRemote::WillLaunch(lldb_private::Module *module) {
  return WillLaunchOrAttach();
}

Status ProcessGDBRemote::WillAttachToProcessWithID(lldb::pid_t pid) {
  return WillLaunchOrAttach();
}

Status ProcessGDBRemote::WillAttachToProcessWithName(const char *process_name,
                                                     bool wait_for_launch) {
  return WillLaunchOrAttach();
}

Status ProcessGDBRemote::DoConnectRemote(Stream *strm,
                                         llvm::StringRef remote_url) {
  Log *log(ProcessGDBRemoteLog::GetLogIfAllCategoriesSet(GDBR_LOG_PROCESS));
  Status error(WillLaunchOrAttach());

  if (error.Fail())
    return error;

  error = ConnectToDebugserver(remote_url);

  if (error.Fail())
    return error;
  StartAsyncThread();

  lldb::pid_t pid = m_gdb_comm.GetCurrentProcessID();
  if (pid == LLDB_INVALID_PROCESS_ID) {
    // We don't have a valid process ID, so note that we are connected and
    // could now request to launch or attach, or get remote process listings...
    SetPrivateState(eStateConnected);
  } else {
    // We have a valid process
    SetID(pid);
    GetThreadList();
    StringExtractorGDBRemote response;
    if (m_gdb_comm.GetStopReply(response)) {
      SetLastStopPacket(response);

      // '?' Packets must be handled differently in non-stop mode
      if (GetTarget().GetNonStopModeEnabled())
        HandleStopReplySequence();

      Target &target = GetTarget();
      if (!target.GetArchitecture().IsValid()) {
        if (m_gdb_comm.GetProcessArchitecture().IsValid()) {
          target.SetArchitecture(m_gdb_comm.GetProcessArchitecture());
        } else {
          if (m_gdb_comm.GetHostArchitecture().IsValid()) {
            target.SetArchitecture(m_gdb_comm.GetHostArchitecture());
          }
        }
      }

      const StateType state = SetThreadStopInfo(response);
      if (state != eStateInvalid) {
        SetPrivateState(state);
      } else
        error.SetErrorStringWithFormat(
            "Process %" PRIu64 " was reported after connecting to "
            "'%s', but state was not stopped: %s",
            pid, remote_url.str().c_str(), StateAsCString(state));
    } else
      error.SetErrorStringWithFormat("Process %" PRIu64
                                     " was reported after connecting to '%s', "
                                     "but no stop reply packet was received",
                                     pid, remote_url.str().c_str());
  }

  if (log)
    log->Printf("ProcessGDBRemote::%s pid %" PRIu64
                ": normalizing target architecture initial triple: %s "
                "(GetTarget().GetArchitecture().IsValid() %s, "
                "m_gdb_comm.GetHostArchitecture().IsValid(): %s)",
                __FUNCTION__, GetID(),
                GetTarget().GetArchitecture().GetTriple().getTriple().c_str(),
                GetTarget().GetArchitecture().IsValid() ? "true" : "false",
                m_gdb_comm.GetHostArchitecture().IsValid() ? "true" : "false");

  if (error.Success() && !GetTarget().GetArchitecture().IsValid() &&
      m_gdb_comm.GetHostArchitecture().IsValid()) {
    // Prefer the *process'* architecture over that of the *host*, if
    // available.
    if (m_gdb_comm.GetProcessArchitecture().IsValid())
      GetTarget().SetArchitecture(m_gdb_comm.GetProcessArchitecture());
    else
      GetTarget().SetArchitecture(m_gdb_comm.GetHostArchitecture());
  }

  if (log)
    log->Printf("ProcessGDBRemote::%s pid %" PRIu64
                ": normalized target architecture triple: %s",
                __FUNCTION__, GetID(),
                GetTarget().GetArchitecture().GetTriple().getTriple().c_str());

  if (error.Success()) {
    PlatformSP platform_sp = GetTarget().GetPlatform();
    if (platform_sp && platform_sp->IsConnected())
      SetUnixSignals(platform_sp->GetUnixSignals());
    else
      SetUnixSignals(UnixSignals::Create(GetTarget().GetArchitecture()));
  }

  return error;
}

Status ProcessGDBRemote::WillLaunchOrAttach() {
  Status error;
  m_stdio_communication.Clear();
  return error;
}

//----------------------------------------------------------------------
// Process Control
//----------------------------------------------------------------------
Status ProcessGDBRemote::DoLaunch(lldb_private::Module *exe_module,
                                  ProcessLaunchInfo &launch_info) {
  Log *log(ProcessGDBRemoteLog::GetLogIfAllCategoriesSet(GDBR_LOG_PROCESS));
  Status error;

  if (log)
    log->Printf("ProcessGDBRemote::%s() entered", __FUNCTION__);

  uint32_t launch_flags = launch_info.GetFlags().Get();
  FileSpec stdin_file_spec{};
  FileSpec stdout_file_spec{};
  FileSpec stderr_file_spec{};
  FileSpec working_dir = launch_info.GetWorkingDirectory();

  const FileAction *file_action;
  file_action = launch_info.GetFileActionForFD(STDIN_FILENO);
  if (file_action) {
    if (file_action->GetAction() == FileAction::eFileActionOpen)
      stdin_file_spec = file_action->GetFileSpec();
  }
  file_action = launch_info.GetFileActionForFD(STDOUT_FILENO);
  if (file_action) {
    if (file_action->GetAction() == FileAction::eFileActionOpen)
      stdout_file_spec = file_action->GetFileSpec();
  }
  file_action = launch_info.GetFileActionForFD(STDERR_FILENO);
  if (file_action) {
    if (file_action->GetAction() == FileAction::eFileActionOpen)
      stderr_file_spec = file_action->GetFileSpec();
  }

  if (log) {
    if (stdin_file_spec || stdout_file_spec || stderr_file_spec)
      log->Printf("ProcessGDBRemote::%s provided with STDIO paths via "
                  "launch_info: stdin=%s, stdout=%s, stderr=%s",
                  __FUNCTION__,
                  stdin_file_spec ? stdin_file_spec.GetCString() : "<null>",
                  stdout_file_spec ? stdout_file_spec.GetCString() : "<null>",
                  stderr_file_spec ? stderr_file_spec.GetCString() : "<null>");
    else
      log->Printf("ProcessGDBRemote::%s no STDIO paths given via launch_info",
                  __FUNCTION__);
  }

  const bool disable_stdio = (launch_flags & eLaunchFlagDisableSTDIO) != 0;
  if (stdin_file_spec || disable_stdio) {
    // the inferior will be reading stdin from the specified file or stdio is
    // completely disabled
    m_stdin_forward = false;
  } else {
    m_stdin_forward = true;
  }

  //  ::LogSetBitMask (GDBR_LOG_DEFAULT);
  //  ::LogSetOptions (LLDB_LOG_OPTION_THREADSAFE |
  //  LLDB_LOG_OPTION_PREPEND_TIMESTAMP |
  //  LLDB_LOG_OPTION_PREPEND_PROC_AND_THREAD);
  //  ::LogSetLogFile ("/dev/stdout");

  ObjectFile *object_file = exe_module->GetObjectFile();
  if (object_file) {
    error = EstablishConnectionIfNeeded(launch_info);
    if (error.Success()) {
      PseudoTerminal pty;
      const bool disable_stdio = (launch_flags & eLaunchFlagDisableSTDIO) != 0;

      PlatformSP platform_sp(GetTarget().GetPlatform());
      if (disable_stdio) {
        // set to /dev/null unless redirected to a file above
        if (!stdin_file_spec)
          stdin_file_spec.SetFile(FileSystem::DEV_NULL,
                                  FileSpec::Style::native);
        if (!stdout_file_spec)
          stdout_file_spec.SetFile(FileSystem::DEV_NULL,
                                   FileSpec::Style::native);
        if (!stderr_file_spec)
          stderr_file_spec.SetFile(FileSystem::DEV_NULL,
                                   FileSpec::Style::native);
      } else if (platform_sp && platform_sp->IsHost()) {
        // If the debugserver is local and we aren't disabling STDIO, lets use
        // a pseudo terminal to instead of relying on the 'O' packets for stdio
        // since 'O' packets can really slow down debugging if the inferior
        // does a lot of output.
        if ((!stdin_file_spec || !stdout_file_spec || !stderr_file_spec) &&
            pty.OpenFirstAvailableMaster(O_RDWR | O_NOCTTY, NULL, 0)) {
          FileSpec slave_name{pty.GetSlaveName(NULL, 0)};

          if (!stdin_file_spec)
            stdin_file_spec = slave_name;

          if (!stdout_file_spec)
            stdout_file_spec = slave_name;

          if (!stderr_file_spec)
            stderr_file_spec = slave_name;
        }
        if (log)
          log->Printf(
              "ProcessGDBRemote::%s adjusted STDIO paths for local platform "
              "(IsHost() is true) using slave: stdin=%s, stdout=%s, stderr=%s",
              __FUNCTION__,
              stdin_file_spec ? stdin_file_spec.GetCString() : "<null>",
              stdout_file_spec ? stdout_file_spec.GetCString() : "<null>",
              stderr_file_spec ? stderr_file_spec.GetCString() : "<null>");
      }

      if (log)
        log->Printf("ProcessGDBRemote::%s final STDIO paths after all "
                    "adjustments: stdin=%s, stdout=%s, stderr=%s",
                    __FUNCTION__,
                    stdin_file_spec ? stdin_file_spec.GetCString() : "<null>",
                    stdout_file_spec ? stdout_file_spec.GetCString() : "<null>",
                    stderr_file_spec ? stderr_file_spec.GetCString()
                                     : "<null>");

      if (stdin_file_spec)
        m_gdb_comm.SetSTDIN(stdin_file_spec);
      if (stdout_file_spec)
        m_gdb_comm.SetSTDOUT(stdout_file_spec);
      if (stderr_file_spec)
        m_gdb_comm.SetSTDERR(stderr_file_spec);

      m_gdb_comm.SetDisableASLR(launch_flags & eLaunchFlagDisableASLR);
      m_gdb_comm.SetDetachOnError(launch_flags & eLaunchFlagDetachOnError);

      m_gdb_comm.SendLaunchArchPacket(
          GetTarget().GetArchitecture().GetArchitectureName());

      const char *launch_event_data = launch_info.GetLaunchEventData();
      if (launch_event_data != NULL && *launch_event_data != '\0')
        m_gdb_comm.SendLaunchEventDataPacket(launch_event_data);

      if (working_dir) {
        m_gdb_comm.SetWorkingDir(working_dir);
      }

      // Send the environment and the program + arguments after we connect
      m_gdb_comm.SendEnvironment(launch_info.GetEnvironment());

      {
        // Scope for the scoped timeout object
        GDBRemoteCommunication::ScopedTimeout timeout(m_gdb_comm,
                                                      std::chrono::seconds(10));

        int arg_packet_err = m_gdb_comm.SendArgumentsPacket(launch_info);
        if (arg_packet_err == 0) {
          std::string error_str;
          if (m_gdb_comm.GetLaunchSuccess(error_str)) {
            SetID(m_gdb_comm.GetCurrentProcessID());
          } else {
            error.SetErrorString(error_str.c_str());
          }
        } else {
          error.SetErrorStringWithFormat("'A' packet returned an error: %i",
                                         arg_packet_err);
        }
      }

      if (GetID() == LLDB_INVALID_PROCESS_ID) {
        if (log)
          log->Printf("failed to connect to debugserver: %s",
                      error.AsCString());
        KillDebugserverProcess();
        return error;
      }

      StringExtractorGDBRemote response;
      if (m_gdb_comm.GetStopReply(response)) {
        SetLastStopPacket(response);
        // '?' Packets must be handled differently in non-stop mode
        if (GetTarget().GetNonStopModeEnabled())
          HandleStopReplySequence();

        const ArchSpec &process_arch = m_gdb_comm.GetProcessArchitecture();

        if (process_arch.IsValid()) {
          GetTarget().MergeArchitecture(process_arch);
        } else {
          const ArchSpec &host_arch = m_gdb_comm.GetHostArchitecture();
          if (host_arch.IsValid())
            GetTarget().MergeArchitecture(host_arch);
        }

        SetPrivateState(SetThreadStopInfo(response));

        if (!disable_stdio) {
          if (pty.GetMasterFileDescriptor() != PseudoTerminal::invalid_fd)
            SetSTDIOFileDescriptor(pty.ReleaseMasterFileDescriptor());
        }
      }
    } else {
      if (log)
        log->Printf("failed to connect to debugserver: %s", error.AsCString());
    }
  } else {
    // Set our user ID to an invalid process ID.
    SetID(LLDB_INVALID_PROCESS_ID);
    error.SetErrorStringWithFormat(
        "failed to get object file from '%s' for arch %s",
        exe_module->GetFileSpec().GetFilename().AsCString(),
        exe_module->GetArchitecture().GetArchitectureName());
  }
  return error;
}

Status ProcessGDBRemote::ConnectToDebugserver(llvm::StringRef connect_url) {
  Status error;
  // Only connect if we have a valid connect URL
  Log *log(ProcessGDBRemoteLog::GetLogIfAllCategoriesSet(GDBR_LOG_PROCESS));

  if (!connect_url.empty()) {
    if (log)
      log->Printf("ProcessGDBRemote::%s Connecting to %s", __FUNCTION__,
                  connect_url.str().c_str());
    std::unique_ptr<ConnectionFileDescriptor> conn_ap(
        new ConnectionFileDescriptor());
    if (conn_ap.get()) {
      const uint32_t max_retry_count = 50;
      uint32_t retry_count = 0;
      while (!m_gdb_comm.IsConnected()) {
        if (conn_ap->Connect(connect_url, &error) == eConnectionStatusSuccess) {
          m_gdb_comm.SetConnection(conn_ap.release());
          break;
        } else if (error.WasInterrupted()) {
          // If we were interrupted, don't keep retrying.
          break;
        }

        retry_count++;

        if (retry_count >= max_retry_count)
          break;

        usleep(100000);
      }
    }
  }

  if (!m_gdb_comm.IsConnected()) {
    if (error.Success())
      error.SetErrorString("not connected to remote gdb server");
    return error;
  }

  // Start the communications read thread so all incoming data can be parsed
  // into packets and queued as they arrive.
  if (GetTarget().GetNonStopModeEnabled())
    m_gdb_comm.StartReadThread();

  // We always seem to be able to open a connection to a local port so we need
  // to make sure we can then send data to it. If we can't then we aren't
  // actually connected to anything, so try and do the handshake with the
  // remote GDB server and make sure that goes alright.
  if (!m_gdb_comm.HandshakeWithServer(&error)) {
    m_gdb_comm.Disconnect();
    if (error.Success())
      error.SetErrorString("not connected to remote gdb server");
    return error;
  }

  // Send $QNonStop:1 packet on startup if required
  if (GetTarget().GetNonStopModeEnabled())
    GetTarget().SetNonStopModeEnabled(m_gdb_comm.SetNonStopMode(true));

  m_gdb_comm.GetEchoSupported();
  m_gdb_comm.GetThreadSuffixSupported();
  m_gdb_comm.GetListThreadsInStopReplySupported();
  m_gdb_comm.GetHostInfo();
  m_gdb_comm.GetVContSupported('c');
  m_gdb_comm.GetVAttachOrWaitSupported();
  m_gdb_comm.EnableErrorStringInPacket();

  // Ask the remote server for the default thread id
  if (GetTarget().GetNonStopModeEnabled())
    m_gdb_comm.GetDefaultThreadId(m_initial_tid);

  size_t num_cmds = GetExtraStartupCommands().GetArgumentCount();
  for (size_t idx = 0; idx < num_cmds; idx++) {
    StringExtractorGDBRemote response;
    m_gdb_comm.SendPacketAndWaitForResponse(
        GetExtraStartupCommands().GetArgumentAtIndex(idx), response, false);
  }
  return error;
}

void ProcessGDBRemote::DidLaunchOrAttach(ArchSpec &process_arch) {
  Log *log(ProcessGDBRemoteLog::GetLogIfAllCategoriesSet(GDBR_LOG_PROCESS));
  if (log)
    log->Printf("ProcessGDBRemote::%s()", __FUNCTION__);
  if (GetID() != LLDB_INVALID_PROCESS_ID) {
    BuildDynamicRegisterInfo(false);

    // See if the GDB server supports the qHostInfo information

    // See if the GDB server supports the qProcessInfo packet, if so prefer
    // that over the Host information as it will be more specific to our
    // process.

    const ArchSpec &remote_process_arch = m_gdb_comm.GetProcessArchitecture();
    if (remote_process_arch.IsValid()) {
      process_arch = remote_process_arch;
      if (log)
        log->Printf("ProcessGDBRemote::%s gdb-remote had process architecture, "
                    "using %s %s",
                    __FUNCTION__,
                    process_arch.GetArchitectureName()
                        ? process_arch.GetArchitectureName()
                        : "<null>",
                    process_arch.GetTriple().getTriple().c_str()
                        ? process_arch.GetTriple().getTriple().c_str()
                        : "<null>");
    } else {
      process_arch = m_gdb_comm.GetHostArchitecture();
      if (log)
        log->Printf("ProcessGDBRemote::%s gdb-remote did not have process "
                    "architecture, using gdb-remote host architecture %s %s",
                    __FUNCTION__,
                    process_arch.GetArchitectureName()
                        ? process_arch.GetArchitectureName()
                        : "<null>",
                    process_arch.GetTriple().getTriple().c_str()
                        ? process_arch.GetTriple().getTriple().c_str()
                        : "<null>");
    }

    if (process_arch.IsValid()) {
      const ArchSpec &target_arch = GetTarget().GetArchitecture();
      if (target_arch.IsValid()) {
        if (log)
          log->Printf(
              "ProcessGDBRemote::%s analyzing target arch, currently %s %s",
              __FUNCTION__,
              target_arch.GetArchitectureName()
                  ? target_arch.GetArchitectureName()
                  : "<null>",
              target_arch.GetTriple().getTriple().c_str()
                  ? target_arch.GetTriple().getTriple().c_str()
                  : "<null>");

        // If the remote host is ARM and we have apple as the vendor, then
        // ARM executables and shared libraries can have mixed ARM
        // architectures.
        // You can have an armv6 executable, and if the host is armv7, then the
        // system will load the best possible architecture for all shared
        // libraries it has, so we really need to take the remote host
        // architecture as our defacto architecture in this case.

        if ((process_arch.GetMachine() == llvm::Triple::arm ||
             process_arch.GetMachine() == llvm::Triple::thumb) &&
            process_arch.GetTriple().getVendor() == llvm::Triple::Apple) {
          GetTarget().SetArchitecture(process_arch);
          if (log)
            log->Printf("ProcessGDBRemote::%s remote process is ARM/Apple, "
                        "setting target arch to %s %s",
                        __FUNCTION__,
                        process_arch.GetArchitectureName()
                            ? process_arch.GetArchitectureName()
                            : "<null>",
                        process_arch.GetTriple().getTriple().c_str()
                            ? process_arch.GetTriple().getTriple().c_str()
                            : "<null>");
        } else {
          // Fill in what is missing in the triple
          const llvm::Triple &remote_triple = process_arch.GetTriple();
          llvm::Triple new_target_triple = target_arch.GetTriple();
          if (new_target_triple.getVendorName().size() == 0) {
            new_target_triple.setVendor(remote_triple.getVendor());

            if (new_target_triple.getOSName().size() == 0) {
              new_target_triple.setOS(remote_triple.getOS());

              if (new_target_triple.getEnvironmentName().size() == 0)
                new_target_triple.setEnvironment(
                    remote_triple.getEnvironment());
            }

            ArchSpec new_target_arch = target_arch;
            new_target_arch.SetTriple(new_target_triple);
            GetTarget().SetArchitecture(new_target_arch);
          }
        }

        if (log)
          log->Printf("ProcessGDBRemote::%s final target arch after "
                      "adjustments for remote architecture: %s %s",
                      __FUNCTION__,
                      target_arch.GetArchitectureName()
                          ? target_arch.GetArchitectureName()
                          : "<null>",
                      target_arch.GetTriple().getTriple().c_str()
                          ? target_arch.GetTriple().getTriple().c_str()
                          : "<null>");
      } else {
        // The target doesn't have a valid architecture yet, set it from the
        // architecture we got from the remote GDB server
        GetTarget().SetArchitecture(process_arch);
      }
    }

    // Find out which StructuredDataPlugins are supported by the debug monitor.
    // These plugins transmit data over async $J packets.
    auto supported_packets_array =
        m_gdb_comm.GetSupportedStructuredDataPlugins();
    if (supported_packets_array)
      MapSupportedStructuredDataPlugins(*supported_packets_array);
  }
}

void ProcessGDBRemote::DidLaunch() {
  ArchSpec process_arch;
  DidLaunchOrAttach(process_arch);
}

Status ProcessGDBRemote::DoAttachToProcessWithID(
    lldb::pid_t attach_pid, const ProcessAttachInfo &attach_info) {
  Log *log(ProcessGDBRemoteLog::GetLogIfAllCategoriesSet(GDBR_LOG_PROCESS));
  Status error;

  if (log)
    log->Printf("ProcessGDBRemote::%s()", __FUNCTION__);

  // Clear out and clean up from any current state
  Clear();
  if (attach_pid != LLDB_INVALID_PROCESS_ID) {
    error = EstablishConnectionIfNeeded(attach_info);
    if (error.Success()) {
      m_gdb_comm.SetDetachOnError(attach_info.GetDetachOnError());

      char packet[64];
      const int packet_len =
          ::snprintf(packet, sizeof(packet), "vAttach;%" PRIx64, attach_pid);
      SetID(attach_pid);
      m_async_broadcaster.BroadcastEvent(
          eBroadcastBitAsyncContinue, new EventDataBytes(packet, packet_len));
    } else
      SetExitStatus(-1, error.AsCString());
  }

  return error;
}

Status ProcessGDBRemote::DoAttachToProcessWithName(
    const char *process_name, const ProcessAttachInfo &attach_info) {
  Status error;
  // Clear out and clean up from any current state
  Clear();

  if (process_name && process_name[0]) {
    error = EstablishConnectionIfNeeded(attach_info);
    if (error.Success()) {
      StreamString packet;

      m_gdb_comm.SetDetachOnError(attach_info.GetDetachOnError());

      if (attach_info.GetWaitForLaunch()) {
        if (!m_gdb_comm.GetVAttachOrWaitSupported()) {
          packet.PutCString("vAttachWait");
        } else {
          if (attach_info.GetIgnoreExisting())
            packet.PutCString("vAttachWait");
          else
            packet.PutCString("vAttachOrWait");
        }
      } else
        packet.PutCString("vAttachName");
      packet.PutChar(';');
      packet.PutBytesAsRawHex8(process_name, strlen(process_name),
                               endian::InlHostByteOrder(),
                               endian::InlHostByteOrder());

      m_async_broadcaster.BroadcastEvent(
          eBroadcastBitAsyncContinue,
          new EventDataBytes(packet.GetString().data(), packet.GetSize()));

    } else
      SetExitStatus(-1, error.AsCString());
  }
  return error;
}

lldb::user_id_t ProcessGDBRemote::StartTrace(const TraceOptions &options,
                                             Status &error) {
  return m_gdb_comm.SendStartTracePacket(options, error);
}

Status ProcessGDBRemote::StopTrace(lldb::user_id_t uid, lldb::tid_t thread_id) {
  return m_gdb_comm.SendStopTracePacket(uid, thread_id);
}

Status ProcessGDBRemote::GetData(lldb::user_id_t uid, lldb::tid_t thread_id,
                                 llvm::MutableArrayRef<uint8_t> &buffer,
                                 size_t offset) {
  return m_gdb_comm.SendGetDataPacket(uid, thread_id, buffer, offset);
}

Status ProcessGDBRemote::GetMetaData(lldb::user_id_t uid, lldb::tid_t thread_id,
                                     llvm::MutableArrayRef<uint8_t> &buffer,
                                     size_t offset) {
  return m_gdb_comm.SendGetMetaDataPacket(uid, thread_id, buffer, offset);
}

Status ProcessGDBRemote::GetTraceConfig(lldb::user_id_t uid,
                                        TraceOptions &options) {
  return m_gdb_comm.SendGetTraceConfigPacket(uid, options);
}

void ProcessGDBRemote::DidExit() {
  // When we exit, disconnect from the GDB server communications
  m_gdb_comm.Disconnect();
}

void ProcessGDBRemote::DidAttach(ArchSpec &process_arch) {
  // If you can figure out what the architecture is, fill it in here.
  process_arch.Clear();
  DidLaunchOrAttach(process_arch);
}

Status ProcessGDBRemote::WillResume() {
  m_continue_c_tids.clear();
  m_continue_C_tids.clear();
  m_continue_s_tids.clear();
  m_continue_S_tids.clear();
  m_jstopinfo_sp.reset();
  m_jthreadsinfo_sp.reset();
  return Status();
}

Status ProcessGDBRemote::DoResume() {
  Status error;
  Log *log(ProcessGDBRemoteLog::GetLogIfAllCategoriesSet(GDBR_LOG_PROCESS));
  if (log)
    log->Printf("ProcessGDBRemote::Resume()");

  ListenerSP listener_sp(
      Listener::MakeListener("gdb-remote.resume-packet-sent"));
  if (listener_sp->StartListeningForEvents(
          &m_gdb_comm, GDBRemoteCommunication::eBroadcastBitRunPacketSent)) {
    listener_sp->StartListeningForEvents(
        &m_async_broadcaster,
        ProcessGDBRemote::eBroadcastBitAsyncThreadDidExit);

    const size_t num_threads = GetThreadList().GetSize();

    StreamString continue_packet;
    bool continue_packet_error = false;
    if (m_gdb_comm.HasAnyVContSupport()) {
      if (!GetTarget().GetNonStopModeEnabled() &&
          (m_continue_c_tids.size() == num_threads ||
           (m_continue_c_tids.empty() && m_continue_C_tids.empty() &&
            m_continue_s_tids.empty() && m_continue_S_tids.empty()))) {
        // All threads are continuing, just send a "c" packet
        continue_packet.PutCString("c");
      } else {
        continue_packet.PutCString("vCont");

        if (!m_continue_c_tids.empty()) {
          if (m_gdb_comm.GetVContSupported('c')) {
            for (tid_collection::const_iterator
                     t_pos = m_continue_c_tids.begin(),
                     t_end = m_continue_c_tids.end();
                 t_pos != t_end; ++t_pos)
              continue_packet.Printf(";c:%4.4" PRIx64, *t_pos);
          } else
            continue_packet_error = true;
        }

        if (!continue_packet_error && !m_continue_C_tids.empty()) {
          if (m_gdb_comm.GetVContSupported('C')) {
            for (tid_sig_collection::const_iterator
                     s_pos = m_continue_C_tids.begin(),
                     s_end = m_continue_C_tids.end();
                 s_pos != s_end; ++s_pos)
              continue_packet.Printf(";C%2.2x:%4.4" PRIx64, s_pos->second,
                                     s_pos->first);
          } else
            continue_packet_error = true;
        }

        if (!continue_packet_error && !m_continue_s_tids.empty()) {
          if (m_gdb_comm.GetVContSupported('s')) {
            for (tid_collection::const_iterator
                     t_pos = m_continue_s_tids.begin(),
                     t_end = m_continue_s_tids.end();
                 t_pos != t_end; ++t_pos)
              continue_packet.Printf(";s:%4.4" PRIx64, *t_pos);
          } else
            continue_packet_error = true;
        }

        if (!continue_packet_error && !m_continue_S_tids.empty()) {
          if (m_gdb_comm.GetVContSupported('S')) {
            for (tid_sig_collection::const_iterator
                     s_pos = m_continue_S_tids.begin(),
                     s_end = m_continue_S_tids.end();
                 s_pos != s_end; ++s_pos)
              continue_packet.Printf(";S%2.2x:%4.4" PRIx64, s_pos->second,
                                     s_pos->first);
          } else
            continue_packet_error = true;
        }

        if (continue_packet_error)
          continue_packet.Clear();
      }
    } else
      continue_packet_error = true;

    if (continue_packet_error) {
      // Either no vCont support, or we tried to use part of the vCont packet
      // that wasn't supported by the remote GDB server. We need to try and
      // make a simple packet that can do our continue
      const size_t num_continue_c_tids = m_continue_c_tids.size();
      const size_t num_continue_C_tids = m_continue_C_tids.size();
      const size_t num_continue_s_tids = m_continue_s_tids.size();
      const size_t num_continue_S_tids = m_continue_S_tids.size();
      if (num_continue_c_tids > 0) {
        if (num_continue_c_tids == num_threads) {
          // All threads are resuming...
          m_gdb_comm.SetCurrentThreadForRun(-1);
          continue_packet.PutChar('c');
          continue_packet_error = false;
        } else if (num_continue_c_tids == 1 && num_continue_C_tids == 0 &&
                   num_continue_s_tids == 0 && num_continue_S_tids == 0) {
          // Only one thread is continuing
          m_gdb_comm.SetCurrentThreadForRun(m_continue_c_tids.front());
          continue_packet.PutChar('c');
          continue_packet_error = false;
        }
      }

      if (continue_packet_error && num_continue_C_tids > 0) {
        if ((num_continue_C_tids + num_continue_c_tids) == num_threads &&
            num_continue_C_tids > 0 && num_continue_s_tids == 0 &&
            num_continue_S_tids == 0) {
          const int continue_signo = m_continue_C_tids.front().second;
          // Only one thread is continuing
          if (num_continue_C_tids > 1) {
            // More that one thread with a signal, yet we don't have vCont
            // support and we are being asked to resume each thread with a
            // signal, we need to make sure they are all the same signal, or we
            // can't issue the continue accurately with the current support...
            if (num_continue_C_tids > 1) {
              continue_packet_error = false;
              for (size_t i = 1; i < m_continue_C_tids.size(); ++i) {
                if (m_continue_C_tids[i].second != continue_signo)
                  continue_packet_error = true;
              }
            }
            if (!continue_packet_error)
              m_gdb_comm.SetCurrentThreadForRun(-1);
          } else {
            // Set the continue thread ID
            continue_packet_error = false;
            m_gdb_comm.SetCurrentThreadForRun(m_continue_C_tids.front().first);
          }
          if (!continue_packet_error) {
            // Add threads continuing with the same signo...
            continue_packet.Printf("C%2.2x", continue_signo);
          }
        }
      }

      if (continue_packet_error && num_continue_s_tids > 0) {
        if (num_continue_s_tids == num_threads) {
          // All threads are resuming...
          m_gdb_comm.SetCurrentThreadForRun(-1);

          // If in Non-Stop-Mode use vCont when stepping
          if (GetTarget().GetNonStopModeEnabled()) {
            if (m_gdb_comm.GetVContSupported('s'))
              continue_packet.PutCString("vCont;s");
            else
              continue_packet.PutChar('s');
          } else
            continue_packet.PutChar('s');

          continue_packet_error = false;
        } else if (num_continue_c_tids == 0 && num_continue_C_tids == 0 &&
                   num_continue_s_tids == 1 && num_continue_S_tids == 0) {
          // Only one thread is stepping
          m_gdb_comm.SetCurrentThreadForRun(m_continue_s_tids.front());
          continue_packet.PutChar('s');
          continue_packet_error = false;
        }
      }

      if (!continue_packet_error && num_continue_S_tids > 0) {
        if (num_continue_S_tids == num_threads) {
          const int step_signo = m_continue_S_tids.front().second;
          // Are all threads trying to step with the same signal?
          continue_packet_error = false;
          if (num_continue_S_tids > 1) {
            for (size_t i = 1; i < num_threads; ++i) {
              if (m_continue_S_tids[i].second != step_signo)
                continue_packet_error = true;
            }
          }
          if (!continue_packet_error) {
            // Add threads stepping with the same signo...
            m_gdb_comm.SetCurrentThreadForRun(-1);
            continue_packet.Printf("S%2.2x", step_signo);
          }
        } else if (num_continue_c_tids == 0 && num_continue_C_tids == 0 &&
                   num_continue_s_tids == 0 && num_continue_S_tids == 1) {
          // Only one thread is stepping with signal
          m_gdb_comm.SetCurrentThreadForRun(m_continue_S_tids.front().first);
          continue_packet.Printf("S%2.2x", m_continue_S_tids.front().second);
          continue_packet_error = false;
        }
      }
    }

    if (continue_packet_error) {
      error.SetErrorString("can't make continue packet for this resume");
    } else {
      EventSP event_sp;
      if (!m_async_thread.IsJoinable()) {
        error.SetErrorString("Trying to resume but the async thread is dead.");
        if (log)
          log->Printf("ProcessGDBRemote::DoResume: Trying to resume but the "
                      "async thread is dead.");
        return error;
      }

      m_async_broadcaster.BroadcastEvent(
          eBroadcastBitAsyncContinue,
          new EventDataBytes(continue_packet.GetString().data(),
                             continue_packet.GetSize()));

      if (!listener_sp->GetEvent(event_sp, std::chrono::seconds(5))) {
        error.SetErrorString("Resume timed out.");
        if (log)
          log->Printf("ProcessGDBRemote::DoResume: Resume timed out.");
      } else if (event_sp->BroadcasterIs(&m_async_broadcaster)) {
        error.SetErrorString("Broadcast continue, but the async thread was "
                             "killed before we got an ack back.");
        if (log)
          log->Printf("ProcessGDBRemote::DoResume: Broadcast continue, but the "
                      "async thread was killed before we got an ack back.");
        return error;
      }
    }
  }

  return error;
}

void ProcessGDBRemote::HandleStopReplySequence() {
  while (true) {
    // Send vStopped
    StringExtractorGDBRemote response;
    m_gdb_comm.SendPacketAndWaitForResponse("vStopped", response, false);

    // OK represents end of signal list
    if (response.IsOKResponse())
      break;

    // If not OK or a normal packet we have a problem
    if (!response.IsNormalResponse())
      break;

    SetLastStopPacket(response);
  }
}

void ProcessGDBRemote::ClearThreadIDList() {
  std::lock_guard<std::recursive_mutex> guard(m_thread_list_real.GetMutex());
  m_thread_ids.clear();
  m_thread_pcs.clear();
}

size_t
ProcessGDBRemote::UpdateThreadIDsFromStopReplyThreadsValue(std::string &value) {
  m_thread_ids.clear();
  size_t comma_pos;
  lldb::tid_t tid;
  while ((comma_pos = value.find(',')) != std::string::npos) {
    value[comma_pos] = '\0';
    // thread in big endian hex
    tid = StringConvert::ToUInt64(value.c_str(), LLDB_INVALID_THREAD_ID, 16);
    if (tid != LLDB_INVALID_THREAD_ID)
      m_thread_ids.push_back(tid);
    value.erase(0, comma_pos + 1);
  }
  tid = StringConvert::ToUInt64(value.c_str(), LLDB_INVALID_THREAD_ID, 16);
  if (tid != LLDB_INVALID_THREAD_ID)
    m_thread_ids.push_back(tid);
  return m_thread_ids.size();
}

size_t
ProcessGDBRemote::UpdateThreadPCsFromStopReplyThreadsValue(std::string &value) {
  m_thread_pcs.clear();
  size_t comma_pos;
  lldb::addr_t pc;
  while ((comma_pos = value.find(',')) != std::string::npos) {
    value[comma_pos] = '\0';
    pc = StringConvert::ToUInt64(value.c_str(), LLDB_INVALID_ADDRESS, 16);
    if (pc != LLDB_INVALID_ADDRESS)
      m_thread_pcs.push_back(pc);
    value.erase(0, comma_pos + 1);
  }
  pc = StringConvert::ToUInt64(value.c_str(), LLDB_INVALID_ADDRESS, 16);
  if (pc != LLDB_INVALID_THREAD_ID)
    m_thread_pcs.push_back(pc);
  return m_thread_pcs.size();
}

bool ProcessGDBRemote::UpdateThreadIDList() {
  std::lock_guard<std::recursive_mutex> guard(m_thread_list_real.GetMutex());

  if (m_jthreadsinfo_sp) {
    // If we have the JSON threads info, we can get the thread list from that
    StructuredData::Array *thread_infos = m_jthreadsinfo_sp->GetAsArray();
    if (thread_infos && thread_infos->GetSize() > 0) {
      m_thread_ids.clear();
      m_thread_pcs.clear();
      thread_infos->ForEach([this](StructuredData::Object *object) -> bool {
        StructuredData::Dictionary *thread_dict = object->GetAsDictionary();
        if (thread_dict) {
          // Set the thread stop info from the JSON dictionary
          SetThreadStopInfo(thread_dict);
          lldb::tid_t tid = LLDB_INVALID_THREAD_ID;
          if (thread_dict->GetValueForKeyAsInteger<lldb::tid_t>("tid", tid))
            m_thread_ids.push_back(tid);
        }
        return true; // Keep iterating through all thread_info objects
      });
    }
    if (!m_thread_ids.empty())
      return true;
  } else {
    // See if we can get the thread IDs from the current stop reply packets
    // that might contain a "threads" key/value pair

    // Lock the thread stack while we access it
    // Mutex::Locker stop_stack_lock(m_last_stop_packet_mutex);
    std::unique_lock<std::recursive_mutex> stop_stack_lock(
        m_last_stop_packet_mutex, std::defer_lock);
    if (stop_stack_lock.try_lock()) {
      // Get the number of stop packets on the stack
      int nItems = m_stop_packet_stack.size();
      // Iterate over them
      for (int i = 0; i < nItems; i++) {
        // Get the thread stop info
        StringExtractorGDBRemote &stop_info = m_stop_packet_stack[i];
        const std::string &stop_info_str = stop_info.GetStringRef();

        m_thread_pcs.clear();
        const size_t thread_pcs_pos = stop_info_str.find(";thread-pcs:");
        if (thread_pcs_pos != std::string::npos) {
          const size_t start = thread_pcs_pos + strlen(";thread-pcs:");
          const size_t end = stop_info_str.find(';', start);
          if (end != std::string::npos) {
            std::string value = stop_info_str.substr(start, end - start);
            UpdateThreadPCsFromStopReplyThreadsValue(value);
          }
        }

        const size_t threads_pos = stop_info_str.find(";threads:");
        if (threads_pos != std::string::npos) {
          const size_t start = threads_pos + strlen(";threads:");
          const size_t end = stop_info_str.find(';', start);
          if (end != std::string::npos) {
            std::string value = stop_info_str.substr(start, end - start);
            if (UpdateThreadIDsFromStopReplyThreadsValue(value))
              return true;
          }
        }
      }
    }
  }

  bool sequence_mutex_unavailable = false;
  m_gdb_comm.GetCurrentThreadIDs(m_thread_ids, sequence_mutex_unavailable);
  if (sequence_mutex_unavailable) {
    return false; // We just didn't get the list
  }
  return true;
}

bool ProcessGDBRemote::UpdateThreadList(ThreadList &old_thread_list,
                                        ThreadList &new_thread_list) {
  // locker will keep a mutex locked until it goes out of scope
  Log *log(ProcessGDBRemoteLog::GetLogIfAllCategoriesSet(GDBR_LOG_THREAD));
  LLDB_LOGV(log, "pid = {0}", GetID());

  size_t num_thread_ids = m_thread_ids.size();
  // The "m_thread_ids" thread ID list should always be updated after each stop
  // reply packet, but in case it isn't, update it here.
  if (num_thread_ids == 0) {
    if (!UpdateThreadIDList())
      return false;
    num_thread_ids = m_thread_ids.size();
  }

  ThreadList old_thread_list_copy(old_thread_list);
  if (num_thread_ids > 0) {
    for (size_t i = 0; i < num_thread_ids; ++i) {
      tid_t tid = m_thread_ids[i];
      ThreadSP thread_sp(
          old_thread_list_copy.RemoveThreadByProtocolID(tid, false));
      if (!thread_sp) {
        thread_sp.reset(new ThreadGDBRemote(*this, tid));
        LLDB_LOGV(log, "Making new thread: {0} for thread ID: {1:x}.",
                  thread_sp.get(), thread_sp->GetID());
      } else {
        LLDB_LOGV(log, "Found old thread: {0} for thread ID: {1:x}.",
                  thread_sp.get(), thread_sp->GetID());
      }

      SetThreadPc(thread_sp, i);
      new_thread_list.AddThreadSortedByIndexID(thread_sp);
    }
  }

  // Whatever that is left in old_thread_list_copy are not present in
  // new_thread_list. Remove non-existent threads from internal id table.
  size_t old_num_thread_ids = old_thread_list_copy.GetSize(false);
  for (size_t i = 0; i < old_num_thread_ids; i++) {
    ThreadSP old_thread_sp(old_thread_list_copy.GetThreadAtIndex(i, false));
    if (old_thread_sp) {
      lldb::tid_t old_thread_id = old_thread_sp->GetProtocolID();
      m_thread_id_to_index_id_map.erase(old_thread_id);
    }
  }

  return true;
}

void ProcessGDBRemote::SetThreadPc(const ThreadSP &thread_sp, uint64_t index) {
  if (m_thread_ids.size() == m_thread_pcs.size() && thread_sp.get() &&
      GetByteOrder() != eByteOrderInvalid) {
    ThreadGDBRemote *gdb_thread =
        static_cast<ThreadGDBRemote *>(thread_sp.get());
    RegisterContextSP reg_ctx_sp(thread_sp->GetRegisterContext());
    if (reg_ctx_sp) {
      uint32_t pc_regnum = reg_ctx_sp->ConvertRegisterKindToRegisterNumber(
          eRegisterKindGeneric, LLDB_REGNUM_GENERIC_PC);
      if (pc_regnum != LLDB_INVALID_REGNUM) {
        gdb_thread->PrivateSetRegisterValue(pc_regnum, m_thread_pcs[index]);
      }
    }
  }
}

bool ProcessGDBRemote::GetThreadStopInfoFromJSON(
    ThreadGDBRemote *thread, const StructuredData::ObjectSP &thread_infos_sp) {
  // See if we got thread stop infos for all threads via the "jThreadsInfo"
  // packet
  if (thread_infos_sp) {
    StructuredData::Array *thread_infos = thread_infos_sp->GetAsArray();
    if (thread_infos) {
      lldb::tid_t tid;
      const size_t n = thread_infos->GetSize();
      for (size_t i = 0; i < n; ++i) {
        StructuredData::Dictionary *thread_dict =
            thread_infos->GetItemAtIndex(i)->GetAsDictionary();
        if (thread_dict) {
          if (thread_dict->GetValueForKeyAsInteger<lldb::tid_t>(
                  "tid", tid, LLDB_INVALID_THREAD_ID)) {
            if (tid == thread->GetID())
              return (bool)SetThreadStopInfo(thread_dict);
          }
        }
      }
    }
  }
  return false;
}

bool ProcessGDBRemote::CalculateThreadStopInfo(ThreadGDBRemote *thread) {
  // See if we got thread stop infos for all threads via the "jThreadsInfo"
  // packet
  if (GetThreadStopInfoFromJSON(thread, m_jthreadsinfo_sp))
    return true;

  // See if we got thread stop info for any threads valid stop info reasons
  // threads via the "jstopinfo" packet stop reply packet key/value pair?
  if (m_jstopinfo_sp) {
    // If we have "jstopinfo" then we have stop descriptions for all threads
    // that have stop reasons, and if there is no entry for a thread, then it
    // has no stop reason.
    thread->GetRegisterContext()->InvalidateIfNeeded(true);
    if (!GetThreadStopInfoFromJSON(thread, m_jstopinfo_sp)) {
      thread->SetStopInfo(StopInfoSP());
    }
    return true;
  }

  // Fall back to using the qThreadStopInfo packet
  StringExtractorGDBRemote stop_packet;
  if (GetGDBRemote().GetThreadStopInfo(thread->GetProtocolID(), stop_packet))
    return SetThreadStopInfo(stop_packet) == eStateStopped;
  return false;
}

ThreadSP ProcessGDBRemote::SetThreadStopInfo(
    lldb::tid_t tid, ExpeditedRegisterMap &expedited_register_map,
    uint8_t signo, const std::string &thread_name, const std::string &reason,
    const std::string &description, uint32_t exc_type,
    const std::vector<addr_t> &exc_data, addr_t thread_dispatch_qaddr,
    bool queue_vars_valid, // Set to true if queue_name, queue_kind and
                           // queue_serial are valid
    LazyBool associated_with_dispatch_queue, addr_t dispatch_queue_t,
    std::string &queue_name, QueueKind queue_kind, uint64_t queue_serial) {
  ThreadSP thread_sp;
  if (tid != LLDB_INVALID_THREAD_ID) {
    // Scope for "locker" below
    {
      // m_thread_list_real does have its own mutex, but we need to hold onto
      // the mutex between the call to m_thread_list_real.FindThreadByID(...)
      // and the m_thread_list_real.AddThread(...) so it doesn't change on us
      std::lock_guard<std::recursive_mutex> guard(
          m_thread_list_real.GetMutex());
      thread_sp = m_thread_list_real.FindThreadByProtocolID(tid, false);

      if (!thread_sp) {
        // Create the thread if we need to
        thread_sp.reset(new ThreadGDBRemote(*this, tid));
        m_thread_list_real.AddThread(thread_sp);
      }
    }

    if (thread_sp) {
      ThreadGDBRemote *gdb_thread =
          static_cast<ThreadGDBRemote *>(thread_sp.get());
      gdb_thread->GetRegisterContext()->InvalidateIfNeeded(true);

      auto iter = std::find(m_thread_ids.begin(), m_thread_ids.end(), tid);
      if (iter != m_thread_ids.end()) {
        SetThreadPc(thread_sp, iter - m_thread_ids.begin());
      }

      for (const auto &pair : expedited_register_map) {
        StringExtractor reg_value_extractor;
        reg_value_extractor.GetStringRef() = pair.second;
        DataBufferSP buffer_sp(new DataBufferHeap(
            reg_value_extractor.GetStringRef().size() / 2, 0));
        reg_value_extractor.GetHexBytes(buffer_sp->GetData(), '\xcc');
        gdb_thread->PrivateSetRegisterValue(pair.first, buffer_sp->GetData());
      }

      thread_sp->SetName(thread_name.empty() ? NULL : thread_name.c_str());

      gdb_thread->SetThreadDispatchQAddr(thread_dispatch_qaddr);
      // Check if the GDB server was able to provide the queue name, kind and
      // serial number
      if (queue_vars_valid)
        gdb_thread->SetQueueInfo(std::move(queue_name), queue_kind,
                                 queue_serial, dispatch_queue_t,
                                 associated_with_dispatch_queue);
      else
        gdb_thread->ClearQueueInfo();

      gdb_thread->SetAssociatedWithLibdispatchQueue(
          associated_with_dispatch_queue);

      if (dispatch_queue_t != LLDB_INVALID_ADDRESS)
        gdb_thread->SetQueueLibdispatchQueueAddress(dispatch_queue_t);

      // Make sure we update our thread stop reason just once
      if (!thread_sp->StopInfoIsUpToDate()) {
        thread_sp->SetStopInfo(StopInfoSP());
        // If there's a memory thread backed by this thread, we need to use it
        // to calculate StopInfo.
        if (ThreadSP memory_thread_sp =
                m_thread_list.GetBackingThread(thread_sp))
          thread_sp = memory_thread_sp;

        if (exc_type != 0) {
          const size_t exc_data_size = exc_data.size();

          thread_sp->SetStopInfo(
              StopInfoMachException::CreateStopReasonWithMachException(
                  *thread_sp, exc_type, exc_data_size,
                  exc_data_size >= 1 ? exc_data[0] : 0,
                  exc_data_size >= 2 ? exc_data[1] : 0,
                  exc_data_size >= 3 ? exc_data[2] : 0));
        } else {
          bool handled = false;
          bool did_exec = false;
          if (!reason.empty()) {
            if (reason == "trace") {
              addr_t pc = thread_sp->GetRegisterContext()->GetPC();
              lldb::BreakpointSiteSP bp_site_sp = thread_sp->GetProcess()
                                                      ->GetBreakpointSiteList()
                                                      .FindByAddress(pc);

              // If the current pc is a breakpoint site then the StopInfo
              // should be set to Breakpoint Otherwise, it will be set to
              // Trace.
              if (bp_site_sp &&
                  bp_site_sp->ValidForThisThread(thread_sp.get())) {
                thread_sp->SetStopInfo(
                    StopInfo::CreateStopReasonWithBreakpointSiteID(
                        *thread_sp, bp_site_sp->GetID()));
              } else
                thread_sp->SetStopInfo(
                    StopInfo::CreateStopReasonToTrace(*thread_sp));
              handled = true;
            } else if (reason == "breakpoint") {
              addr_t pc = thread_sp->GetRegisterContext()->GetPC();
              lldb::BreakpointSiteSP bp_site_sp = thread_sp->GetProcess()
                                                      ->GetBreakpointSiteList()
                                                      .FindByAddress(pc);
              if (bp_site_sp) {
                // If the breakpoint is for this thread, then we'll report the
                // hit, but if it is for another thread, we can just report no
                // reason.  We don't need to worry about stepping over the
                // breakpoint here, that will be taken care of when the thread
                // resumes and notices that there's a breakpoint under the pc.
                handled = true;
                if (bp_site_sp->ValidForThisThread(thread_sp.get())) {
                  thread_sp->SetStopInfo(
                      StopInfo::CreateStopReasonWithBreakpointSiteID(
                          *thread_sp, bp_site_sp->GetID()));
                } else {
                  StopInfoSP invalid_stop_info_sp;
                  thread_sp->SetStopInfo(invalid_stop_info_sp);
                }
              }
            } else if (reason == "trap") {
              // Let the trap just use the standard signal stop reason below...
            } else if (reason == "watchpoint") {
              StringExtractor desc_extractor(description.c_str());
              addr_t wp_addr = desc_extractor.GetU64(LLDB_INVALID_ADDRESS);
              uint32_t wp_index = desc_extractor.GetU32(LLDB_INVALID_INDEX32);
              addr_t wp_hit_addr = desc_extractor.GetU64(LLDB_INVALID_ADDRESS);
              watch_id_t watch_id = LLDB_INVALID_WATCH_ID;
              if (wp_addr != LLDB_INVALID_ADDRESS) {
                WatchpointSP wp_sp;
                ArchSpec::Core core = GetTarget().GetArchitecture().GetCore();
                if ((core >= ArchSpec::kCore_mips_first &&
                     core <= ArchSpec::kCore_mips_last) ||
                    (core >= ArchSpec::eCore_arm_generic &&
                     core <= ArchSpec::eCore_arm_aarch64))
                  wp_sp = GetTarget().GetWatchpointList().FindByAddress(
                      wp_hit_addr);
                if (!wp_sp)
                  wp_sp =
                      GetTarget().GetWatchpointList().FindByAddress(wp_addr);
                if (wp_sp) {
                  wp_sp->SetHardwareIndex(wp_index);
                  watch_id = wp_sp->GetID();
                }
              }
              if (watch_id == LLDB_INVALID_WATCH_ID) {
                Log *log(ProcessGDBRemoteLog::GetLogIfAllCategoriesSet(
                    GDBR_LOG_WATCHPOINTS));
                if (log)
                  log->Printf("failed to find watchpoint");
              }
              thread_sp->SetStopInfo(StopInfo::CreateStopReasonWithWatchpointID(
                  *thread_sp, watch_id, wp_hit_addr));
              handled = true;
            } else if (reason == "exception") {
              thread_sp->SetStopInfo(StopInfo::CreateStopReasonWithException(
                  *thread_sp, description.c_str()));
              handled = true;
            } else if (reason == "exec") {
              did_exec = true;
              thread_sp->SetStopInfo(
                  StopInfo::CreateStopReasonWithExec(*thread_sp));
              handled = true;
            }
          } else if (!signo) {
            addr_t pc = thread_sp->GetRegisterContext()->GetPC();
            lldb::BreakpointSiteSP bp_site_sp =
                thread_sp->GetProcess()->GetBreakpointSiteList().FindByAddress(
                    pc);

            // If the current pc is a breakpoint site then the StopInfo should
            // be set to Breakpoint even though the remote stub did not set it
            // as such. This can happen when the thread is involuntarily
            // interrupted (e.g. due to stops on other threads) just as it is
            // about to execute the breakpoint instruction.
            if (bp_site_sp && bp_site_sp->ValidForThisThread(thread_sp.get())) {
              thread_sp->SetStopInfo(
                  StopInfo::CreateStopReasonWithBreakpointSiteID(
                      *thread_sp, bp_site_sp->GetID()));
              handled = true;
            }
          }

          if (!handled && signo && !did_exec) {
            if (signo == SIGTRAP) {
              // Currently we are going to assume SIGTRAP means we are either
              // hitting a breakpoint or hardware single stepping.
              handled = true;
              addr_t pc = thread_sp->GetRegisterContext()->GetPC() +
                          m_breakpoint_pc_offset;
              lldb::BreakpointSiteSP bp_site_sp = thread_sp->GetProcess()
                                                      ->GetBreakpointSiteList()
                                                      .FindByAddress(pc);

              if (bp_site_sp) {
                // If the breakpoint is for this thread, then we'll report the
                // hit, but if it is for another thread, we can just report no
                // reason.  We don't need to worry about stepping over the
                // breakpoint here, that will be taken care of when the thread
                // resumes and notices that there's a breakpoint under the pc.
                if (bp_site_sp->ValidForThisThread(thread_sp.get())) {
                  if (m_breakpoint_pc_offset != 0)
                    thread_sp->GetRegisterContext()->SetPC(pc);
                  thread_sp->SetStopInfo(
                      StopInfo::CreateStopReasonWithBreakpointSiteID(
                          *thread_sp, bp_site_sp->GetID()));
                } else {
                  StopInfoSP invalid_stop_info_sp;
                  thread_sp->SetStopInfo(invalid_stop_info_sp);
                }
              } else {
                // If we were stepping then assume the stop was the result of
                // the trace.  If we were not stepping then report the SIGTRAP.
                // FIXME: We are still missing the case where we single step
                // over a trap instruction.
                if (thread_sp->GetTemporaryResumeState() == eStateStepping)
                  thread_sp->SetStopInfo(
                      StopInfo::CreateStopReasonToTrace(*thread_sp));
                else
                  thread_sp->SetStopInfo(StopInfo::CreateStopReasonWithSignal(
                      *thread_sp, signo, description.c_str()));
              }
            }
            if (!handled)
              thread_sp->SetStopInfo(StopInfo::CreateStopReasonWithSignal(
                  *thread_sp, signo, description.c_str()));
          }

          if (!description.empty()) {
            lldb::StopInfoSP stop_info_sp(thread_sp->GetStopInfo());
            if (stop_info_sp) {
              const char *stop_info_desc = stop_info_sp->GetDescription();
              if (!stop_info_desc || !stop_info_desc[0])
                stop_info_sp->SetDescription(description.c_str());
            } else {
              thread_sp->SetStopInfo(StopInfo::CreateStopReasonWithException(
                  *thread_sp, description.c_str()));
            }
          }
        }
      }
    }
  }
  return thread_sp;
}

lldb::ThreadSP
ProcessGDBRemote::SetThreadStopInfo(StructuredData::Dictionary *thread_dict) {
  static ConstString g_key_tid("tid");
  static ConstString g_key_name("name");
  static ConstString g_key_reason("reason");
  static ConstString g_key_metype("metype");
  static ConstString g_key_medata("medata");
  static ConstString g_key_qaddr("qaddr");
  static ConstString g_key_dispatch_queue_t("dispatch_queue_t");
  static ConstString g_key_associated_with_dispatch_queue(
      "associated_with_dispatch_queue");
  static ConstString g_key_queue_name("qname");
  static ConstString g_key_queue_kind("qkind");
  static ConstString g_key_queue_serial_number("qserialnum");
  static ConstString g_key_registers("registers");
  static ConstString g_key_memory("memory");
  static ConstString g_key_address("address");
  static ConstString g_key_bytes("bytes");
  static ConstString g_key_description("description");
  static ConstString g_key_signal("signal");

  // Stop with signal and thread info
  lldb::tid_t tid = LLDB_INVALID_THREAD_ID;
  uint8_t signo = 0;
  std::string value;
  std::string thread_name;
  std::string reason;
  std::string description;
  uint32_t exc_type = 0;
  std::vector<addr_t> exc_data;
  addr_t thread_dispatch_qaddr = LLDB_INVALID_ADDRESS;
  ExpeditedRegisterMap expedited_register_map;
  bool queue_vars_valid = false;
  addr_t dispatch_queue_t = LLDB_INVALID_ADDRESS;
  LazyBool associated_with_dispatch_queue = eLazyBoolCalculate;
  std::string queue_name;
  QueueKind queue_kind = eQueueKindUnknown;
  uint64_t queue_serial_number = 0;
  // Iterate through all of the thread dictionary key/value pairs from the
  // structured data dictionary

  thread_dict->ForEach([this, &tid, &expedited_register_map, &thread_name,
                        &signo, &reason, &description, &exc_type, &exc_data,
                        &thread_dispatch_qaddr, &queue_vars_valid,
                        &associated_with_dispatch_queue, &dispatch_queue_t,
                        &queue_name, &queue_kind, &queue_serial_number](
                           ConstString key,
                           StructuredData::Object *object) -> bool {
    if (key == g_key_tid) {
      // thread in big endian hex
      tid = object->GetIntegerValue(LLDB_INVALID_THREAD_ID);
    } else if (key == g_key_metype) {
      // exception type in big endian hex
      exc_type = object->GetIntegerValue(0);
    } else if (key == g_key_medata) {
      // exception data in big endian hex
      StructuredData::Array *array = object->GetAsArray();
      if (array) {
        array->ForEach([&exc_data](StructuredData::Object *object) -> bool {
          exc_data.push_back(object->GetIntegerValue());
          return true; // Keep iterating through all array items
        });
      }
    } else if (key == g_key_name) {
      thread_name = object->GetStringValue();
    } else if (key == g_key_qaddr) {
      thread_dispatch_qaddr = object->GetIntegerValue(LLDB_INVALID_ADDRESS);
    } else if (key == g_key_queue_name) {
      queue_vars_valid = true;
      queue_name = object->GetStringValue();
    } else if (key == g_key_queue_kind) {
      std::string queue_kind_str = object->GetStringValue();
      if (queue_kind_str == "serial") {
        queue_vars_valid = true;
        queue_kind = eQueueKindSerial;
      } else if (queue_kind_str == "concurrent") {
        queue_vars_valid = true;
        queue_kind = eQueueKindConcurrent;
      }
    } else if (key == g_key_queue_serial_number) {
      queue_serial_number = object->GetIntegerValue(0);
      if (queue_serial_number != 0)
        queue_vars_valid = true;
    } else if (key == g_key_dispatch_queue_t) {
      dispatch_queue_t = object->GetIntegerValue(0);
      if (dispatch_queue_t != 0 && dispatch_queue_t != LLDB_INVALID_ADDRESS)
        queue_vars_valid = true;
    } else if (key == g_key_associated_with_dispatch_queue) {
      queue_vars_valid = true;
      bool associated = object->GetBooleanValue();
      if (associated)
        associated_with_dispatch_queue = eLazyBoolYes;
      else
        associated_with_dispatch_queue = eLazyBoolNo;
    } else if (key == g_key_reason) {
      reason = object->GetStringValue();
    } else if (key == g_key_description) {
      description = object->GetStringValue();
    } else if (key == g_key_registers) {
      StructuredData::Dictionary *registers_dict = object->GetAsDictionary();

      if (registers_dict) {
        registers_dict->ForEach(
            [&expedited_register_map](ConstString key,
                                      StructuredData::Object *object) -> bool {
              const uint32_t reg =
                  StringConvert::ToUInt32(key.GetCString(), UINT32_MAX, 10);
              if (reg != UINT32_MAX)
                expedited_register_map[reg] = object->GetStringValue();
              return true; // Keep iterating through all array items
            });
      }
    } else if (key == g_key_memory) {
      StructuredData::Array *array = object->GetAsArray();
      if (array) {
        array->ForEach([this](StructuredData::Object *object) -> bool {
          StructuredData::Dictionary *mem_cache_dict =
              object->GetAsDictionary();
          if (mem_cache_dict) {
            lldb::addr_t mem_cache_addr = LLDB_INVALID_ADDRESS;
            if (mem_cache_dict->GetValueForKeyAsInteger<lldb::addr_t>(
                    "address", mem_cache_addr)) {
              if (mem_cache_addr != LLDB_INVALID_ADDRESS) {
                llvm::StringRef str;
                if (mem_cache_dict->GetValueForKeyAsString("bytes", str)) {
                  StringExtractor bytes(str);
                  bytes.SetFilePos(0);

                  const size_t byte_size = bytes.GetStringRef().size() / 2;
                  DataBufferSP data_buffer_sp(new DataBufferHeap(byte_size, 0));
                  const size_t bytes_copied =
                      bytes.GetHexBytes(data_buffer_sp->GetData(), 0);
                  if (bytes_copied == byte_size)
                    m_memory_cache.AddL1CacheData(mem_cache_addr,
                                                  data_buffer_sp);
                }
              }
            }
          }
          return true; // Keep iterating through all array items
        });
      }

    } else if (key == g_key_signal)
      signo = object->GetIntegerValue(LLDB_INVALID_SIGNAL_NUMBER);
    return true; // Keep iterating through all dictionary key/value pairs
  });

  return SetThreadStopInfo(tid, expedited_register_map, signo, thread_name,
                           reason, description, exc_type, exc_data,
                           thread_dispatch_qaddr, queue_vars_valid,
                           associated_with_dispatch_queue, dispatch_queue_t,
                           queue_name, queue_kind, queue_serial_number);
}

StateType ProcessGDBRemote::SetThreadStopInfo(StringExtractor &stop_packet) {
  stop_packet.SetFilePos(0);
  const char stop_type = stop_packet.GetChar();
  switch (stop_type) {
  case 'T':
  case 'S': {
    // This is a bit of a hack, but is is required. If we did exec, we need to
    // clear our thread lists and also know to rebuild our dynamic register
    // info before we lookup and threads and populate the expedited register
    // values so we need to know this right away so we can cleanup and update
    // our registers.
    const uint32_t stop_id = GetStopID();
    if (stop_id == 0) {
      // Our first stop, make sure we have a process ID, and also make sure we
      // know about our registers
      if (GetID() == LLDB_INVALID_PROCESS_ID) {
        lldb::pid_t pid = m_gdb_comm.GetCurrentProcessID();
        if (pid != LLDB_INVALID_PROCESS_ID)
          SetID(pid);
      }
      BuildDynamicRegisterInfo(true);
    }
    // Stop with signal and thread info
    lldb::tid_t tid = LLDB_INVALID_THREAD_ID;
    const uint8_t signo = stop_packet.GetHexU8();
    llvm::StringRef key;
    llvm::StringRef value;
    std::string thread_name;
    std::string reason;
    std::string description;
    uint32_t exc_type = 0;
    std::vector<addr_t> exc_data;
    addr_t thread_dispatch_qaddr = LLDB_INVALID_ADDRESS;
    bool queue_vars_valid =
        false; // says if locals below that start with "queue_" are valid
    addr_t dispatch_queue_t = LLDB_INVALID_ADDRESS;
    LazyBool associated_with_dispatch_queue = eLazyBoolCalculate;
    std::string queue_name;
    QueueKind queue_kind = eQueueKindUnknown;
    uint64_t queue_serial_number = 0;
    ExpeditedRegisterMap expedited_register_map;
    while (stop_packet.GetNameColonValue(key, value)) {
      if (key.compare("metype") == 0) {
        // exception type in big endian hex
        value.getAsInteger(16, exc_type);
      } else if (key.compare("medata") == 0) {
        // exception data in big endian hex
        uint64_t x;
        value.getAsInteger(16, x);
        exc_data.push_back(x);
      } else if (key.compare("thread") == 0) {
        // thread in big endian hex
        if (value.getAsInteger(16, tid))
          tid = LLDB_INVALID_THREAD_ID;
      } else if (key.compare("threads") == 0) {
        std::lock_guard<std::recursive_mutex> guard(
            m_thread_list_real.GetMutex());

        m_thread_ids.clear();
        // A comma separated list of all threads in the current
        // process that includes the thread for this stop reply packet
        lldb::tid_t tid;
        while (!value.empty()) {
          llvm::StringRef tid_str;
          std::tie(tid_str, value) = value.split(',');
          if (tid_str.getAsInteger(16, tid))
            tid = LLDB_INVALID_THREAD_ID;
          m_thread_ids.push_back(tid);
        }
      } else if (key.compare("thread-pcs") == 0) {
        m_thread_pcs.clear();
        // A comma separated list of all threads in the current
        // process that includes the thread for this stop reply packet
        lldb::addr_t pc;
        while (!value.empty()) {
          llvm::StringRef pc_str;
          std::tie(pc_str, value) = value.split(',');
          if (pc_str.getAsInteger(16, pc))
            pc = LLDB_INVALID_ADDRESS;
          m_thread_pcs.push_back(pc);
        }
      } else if (key.compare("jstopinfo") == 0) {
        StringExtractor json_extractor(value);
        std::string json;
        // Now convert the HEX bytes into a string value
        json_extractor.GetHexByteString(json);

        // This JSON contains thread IDs and thread stop info for all threads.
        // It doesn't contain expedited registers, memory or queue info.
        m_jstopinfo_sp = StructuredData::ParseJSON(json);
      } else if (key.compare("hexname") == 0) {
        StringExtractor name_extractor(value);
        std::string name;
        // Now convert the HEX bytes into a string value
        name_extractor.GetHexByteString(thread_name);
      } else if (key.compare("name") == 0) {
        thread_name = value;
      } else if (key.compare("qaddr") == 0) {
        value.getAsInteger(16, thread_dispatch_qaddr);
      } else if (key.compare("dispatch_queue_t") == 0) {
        queue_vars_valid = true;
        value.getAsInteger(16, dispatch_queue_t);
      } else if (key.compare("qname") == 0) {
        queue_vars_valid = true;
        StringExtractor name_extractor(value);
        // Now convert the HEX bytes into a string value
        name_extractor.GetHexByteString(queue_name);
      } else if (key.compare("qkind") == 0) {
        queue_kind = llvm::StringSwitch<QueueKind>(value)
                         .Case("serial", eQueueKindSerial)
                         .Case("concurrent", eQueueKindConcurrent)
                         .Default(eQueueKindUnknown);
        queue_vars_valid = queue_kind != eQueueKindUnknown;
      } else if (key.compare("qserialnum") == 0) {
        if (!value.getAsInteger(0, queue_serial_number))
          queue_vars_valid = true;
      } else if (key.compare("reason") == 0) {
        reason = value;
      } else if (key.compare("description") == 0) {
        StringExtractor desc_extractor(value);
        // Now convert the HEX bytes into a string value
        desc_extractor.GetHexByteString(description);
      } else if (key.compare("memory") == 0) {
        // Expedited memory. GDB servers can choose to send back expedited
        // memory that can populate the L1 memory cache in the process so that
        // things like the frame pointer backchain can be expedited. This will
        // help stack backtracing be more efficient by not having to send as
        // many memory read requests down the remote GDB server.

        // Key/value pair format: memory:<addr>=<bytes>;
        // <addr> is a number whose base will be interpreted by the prefix:
        //      "0x[0-9a-fA-F]+" for hex
        //      "0[0-7]+" for octal
        //      "[1-9]+" for decimal
        // <bytes> is native endian ASCII hex bytes just like the register
        // values
        llvm::StringRef addr_str, bytes_str;
        std::tie(addr_str, bytes_str) = value.split('=');
        if (!addr_str.empty() && !bytes_str.empty()) {
          lldb::addr_t mem_cache_addr = LLDB_INVALID_ADDRESS;
          if (!addr_str.getAsInteger(0, mem_cache_addr)) {
            StringExtractor bytes(bytes_str);
            const size_t byte_size = bytes.GetBytesLeft() / 2;
            DataBufferSP data_buffer_sp(new DataBufferHeap(byte_size, 0));
            const size_t bytes_copied =
                bytes.GetHexBytes(data_buffer_sp->GetData(), 0);
            if (bytes_copied == byte_size)
              m_memory_cache.AddL1CacheData(mem_cache_addr, data_buffer_sp);
          }
        }
      } else if (key.compare("watch") == 0 || key.compare("rwatch") == 0 ||
                 key.compare("awatch") == 0) {
        // Support standard GDB remote stop reply packet 'TAAwatch:addr'
        lldb::addr_t wp_addr = LLDB_INVALID_ADDRESS;
        value.getAsInteger(16, wp_addr);

        WatchpointSP wp_sp =
            GetTarget().GetWatchpointList().FindByAddress(wp_addr);
        uint32_t wp_index = LLDB_INVALID_INDEX32;

        if (wp_sp)
          wp_index = wp_sp->GetHardwareIndex();

        reason = "watchpoint";
        StreamString ostr;
        ostr.Printf("%" PRIu64 " %" PRIu32, wp_addr, wp_index);
        description = ostr.GetString();
      } else if (key.compare("library") == 0) {
        LoadModules();
      } else if (key.size() == 2 && ::isxdigit(key[0]) && ::isxdigit(key[1])) {
        uint32_t reg = UINT32_MAX;
        if (!key.getAsInteger(16, reg))
          expedited_register_map[reg] = std::move(value);
      }
    }

    if (tid == LLDB_INVALID_THREAD_ID) {
      // A thread id may be invalid if the response is old style 'S' packet
      // which does not provide the
      // thread information. So update the thread list and choose the first
      // one.
      UpdateThreadIDList();

      if (!m_thread_ids.empty()) {
        tid = m_thread_ids.front();
      }
    }

    ThreadSP thread_sp = SetThreadStopInfo(
        tid, expedited_register_map, signo, thread_name, reason, description,
        exc_type, exc_data, thread_dispatch_qaddr, queue_vars_valid,
        associated_with_dispatch_queue, dispatch_queue_t, queue_name,
        queue_kind, queue_serial_number);

    return eStateStopped;
  } break;

  case 'W':
  case 'X':
    // process exited
    return eStateExited;

  default:
    break;
  }
  return eStateInvalid;
}

void ProcessGDBRemote::RefreshStateAfterStop() {
  std::lock_guard<std::recursive_mutex> guard(m_thread_list_real.GetMutex());

  m_thread_ids.clear();
  m_thread_pcs.clear();
  // Set the thread stop info. It might have a "threads" key whose value is a
  // list of all thread IDs in the current process, so m_thread_ids might get
  // set.

  // Scope for the lock
  {
    // Lock the thread stack while we access it
    std::lock_guard<std::recursive_mutex> guard(m_last_stop_packet_mutex);
    // Get the number of stop packets on the stack
    int nItems = m_stop_packet_stack.size();
    // Iterate over them
    for (int i = 0; i < nItems; i++) {
      // Get the thread stop info
      StringExtractorGDBRemote stop_info = m_stop_packet_stack[i];
      // Process thread stop info
      SetThreadStopInfo(stop_info);
    }
    // Clear the thread stop stack
    m_stop_packet_stack.clear();
  }

  // Check to see if SetThreadStopInfo() filled in m_thread_ids?
  if (m_thread_ids.empty()) {
    // No, we need to fetch the thread list manually
    UpdateThreadIDList();
  }

  // If we have queried for a default thread id
  if (m_initial_tid != LLDB_INVALID_THREAD_ID) {
    m_thread_list.SetSelectedThreadByID(m_initial_tid);
    m_initial_tid = LLDB_INVALID_THREAD_ID;
  }

  // Let all threads recover from stopping and do any clean up based on the
  // previous thread state (if any).
  m_thread_list_real.RefreshStateAfterStop();
}

Status ProcessGDBRemote::DoHalt(bool &caused_stop) {
  Status error;

  if (m_public_state.GetValue() == eStateAttaching) {
    // We are being asked to halt during an attach. We need to just close our
    // file handle and debugserver will go away, and we can be done...
    m_gdb_comm.Disconnect();
  } else
    caused_stop = m_gdb_comm.Interrupt();
  return error;
}

Status ProcessGDBRemote::DoDetach(bool keep_stopped) {
  Status error;
  Log *log(ProcessGDBRemoteLog::GetLogIfAllCategoriesSet(GDBR_LOG_PROCESS));
  if (log)
    log->Printf("ProcessGDBRemote::DoDetach(keep_stopped: %i)", keep_stopped);

  error = m_gdb_comm.Detach(keep_stopped);
  if (log) {
    if (error.Success())
      log->PutCString(
          "ProcessGDBRemote::DoDetach() detach packet sent successfully");
    else
      log->Printf("ProcessGDBRemote::DoDetach() detach packet send failed: %s",
                  error.AsCString() ? error.AsCString() : "<unknown error>");
  }

  if (!error.Success())
    return error;

  // Sleep for one second to let the process get all detached...
  StopAsyncThread();

  SetPrivateState(eStateDetached);
  ResumePrivateStateThread();

  // KillDebugserverProcess ();
  return error;
}

Status ProcessGDBRemote::DoDestroy() {
  Status error;
  Log *log(ProcessGDBRemoteLog::GetLogIfAllCategoriesSet(GDBR_LOG_PROCESS));
  if (log)
    log->Printf("ProcessGDBRemote::DoDestroy()");

#ifdef LLDB_ENABLE_ALL // XXX Currently no iOS target support on FreeBSD
  // There is a bug in older iOS debugservers where they don't shut down the
  // process they are debugging properly.  If the process is sitting at a
  // breakpoint or an exception, this can cause problems with restarting.  So
  // we check to see if any of our threads are stopped at a breakpoint, and if
  // so we remove all the breakpoints, resume the process, and THEN destroy it
  // again.
  //
  // Note, we don't have a good way to test the version of debugserver, but I
  // happen to know that the set of all the iOS debugservers which don't
  // support GetThreadSuffixSupported() and that of the debugservers with this
  // bug are equal.  There really should be a better way to test this!
  //
  // We also use m_destroy_tried_resuming to make sure we only do this once, if
  // we resume and then halt and get called here to destroy again and we're
  // still at a breakpoint or exception, then we should just do the straight-
  // forward kill.
  //
  // And of course, if we weren't able to stop the process by the time we get
  // here, it isn't necessary (or helpful) to do any of this.

  if (!m_gdb_comm.GetThreadSuffixSupported() &&
      m_public_state.GetValue() != eStateRunning) {
    PlatformSP platform_sp = GetTarget().GetPlatform();

    // FIXME: These should be ConstStrings so we aren't doing strcmp'ing.
    if (platform_sp && platform_sp->GetName() &&
        platform_sp->GetName() == PlatformRemoteiOS::GetPluginNameStatic()) {
      if (m_destroy_tried_resuming) {
        if (log)
          log->PutCString("ProcessGDBRemote::DoDestroy() - Tried resuming to "
                          "destroy once already, not doing it again.");
      } else {
        // At present, the plans are discarded and the breakpoints disabled
        // Process::Destroy, but we really need it to happen here and it
        // doesn't matter if we do it twice.
        m_thread_list.DiscardThreadPlans();
        DisableAllBreakpointSites();

        bool stop_looks_like_crash = false;
        ThreadList &threads = GetThreadList();

        {
          std::lock_guard<std::recursive_mutex> guard(threads.GetMutex());

          size_t num_threads = threads.GetSize();
          for (size_t i = 0; i < num_threads; i++) {
            ThreadSP thread_sp = threads.GetThreadAtIndex(i);
            StopInfoSP stop_info_sp = thread_sp->GetPrivateStopInfo();
            StopReason reason = eStopReasonInvalid;
            if (stop_info_sp)
              reason = stop_info_sp->GetStopReason();
            if (reason == eStopReasonBreakpoint ||
                reason == eStopReasonException) {
              if (log)
                log->Printf(
                    "ProcessGDBRemote::DoDestroy() - thread: 0x%4.4" PRIx64
                    " stopped with reason: %s.",
                    thread_sp->GetProtocolID(), stop_info_sp->GetDescription());
              stop_looks_like_crash = true;
              break;
            }
          }
        }

        if (stop_looks_like_crash) {
          if (log)
            log->PutCString("ProcessGDBRemote::DoDestroy() - Stopped at a "
                            "breakpoint, continue and then kill.");
          m_destroy_tried_resuming = true;

          // If we are going to run again before killing, it would be good to
          // suspend all the threads before resuming so they won't get into
          // more trouble.  Sadly, for the threads stopped with the breakpoint
          // or exception, the exception doesn't get cleared if it is
          // suspended, so we do have to run the risk of letting those threads
          // proceed a bit.

          {
            std::lock_guard<std::recursive_mutex> guard(threads.GetMutex());

            size_t num_threads = threads.GetSize();
            for (size_t i = 0; i < num_threads; i++) {
              ThreadSP thread_sp = threads.GetThreadAtIndex(i);
              StopInfoSP stop_info_sp = thread_sp->GetPrivateStopInfo();
              StopReason reason = eStopReasonInvalid;
              if (stop_info_sp)
                reason = stop_info_sp->GetStopReason();
              if (reason != eStopReasonBreakpoint &&
                  reason != eStopReasonException) {
                if (log)
                  log->Printf("ProcessGDBRemote::DoDestroy() - Suspending "
                              "thread: 0x%4.4" PRIx64 " before running.",
                              thread_sp->GetProtocolID());
                thread_sp->SetResumeState(eStateSuspended);
              }
            }
          }
          Resume();
          return Destroy(false);
        }
      }
    }
  }
#endif // LLDB_ENABLE_ALL

  // Interrupt if our inferior is running...
  int exit_status = SIGABRT;
  std::string exit_string;

  if (m_gdb_comm.IsConnected()) {
    if (m_public_state.GetValue() != eStateAttaching) {
      StringExtractorGDBRemote response;
      bool send_async = true;
      GDBRemoteCommunication::ScopedTimeout(m_gdb_comm,
                                            std::chrono::seconds(3));

      if (m_gdb_comm.SendPacketAndWaitForResponse("k", response, send_async) ==
          GDBRemoteCommunication::PacketResult::Success) {
        char packet_cmd = response.GetChar(0);

        if (packet_cmd == 'W' || packet_cmd == 'X') {
#if defined(__APPLE__)
          // For Native processes on Mac OS X, we launch through the Host
          // Platform, then hand the process off to debugserver, which becomes
          // the parent process through "PT_ATTACH".  Then when we go to kill
          // the process on Mac OS X we call ptrace(PT_KILL) to kill it, then
          // we call waitpid which returns with no error and the correct
          // status.  But amusingly enough that doesn't seem to actually reap
          // the process, but instead it is left around as a Zombie.  Probably
          // the kernel is in the process of switching ownership back to lldb
          // which was the original parent, and gets confused in the handoff.
          // Anyway, so call waitpid here to finally reap it.
          PlatformSP platform_sp(GetTarget().GetPlatform());
          if (platform_sp && platform_sp->IsHost()) {
            int status;
            ::pid_t reap_pid;
            reap_pid = waitpid(GetID(), &status, WNOHANG);
            if (log)
              log->Printf("Reaped pid: %d, status: %d.\n", reap_pid, status);
          }
#endif
          SetLastStopPacket(response);
          ClearThreadIDList();
          exit_status = response.GetHexU8();
        } else {
          if (log)
            log->Printf("ProcessGDBRemote::DoDestroy - got unexpected response "
                        "to k packet: %s",
                        response.GetStringRef().c_str());
          exit_string.assign("got unexpected response to k packet: ");
          exit_string.append(response.GetStringRef());
        }
      } else {
        if (log)
          log->Printf("ProcessGDBRemote::DoDestroy - failed to send k packet");
        exit_string.assign("failed to send the k packet");
      }
    } else {
      if (log)
        log->Printf("ProcessGDBRemote::DoDestroy - killed or interrupted while "
                    "attaching");
      exit_string.assign("killed or interrupted while attaching.");
    }
  } else {
    // If we missed setting the exit status on the way out, do it here.
    // NB set exit status can be called multiple times, the first one sets the
    // status.
    exit_string.assign("destroying when not connected to debugserver");
  }

  SetExitStatus(exit_status, exit_string.c_str());

  StopAsyncThread();
  KillDebugserverProcess();
  return error;
}

void ProcessGDBRemote::SetLastStopPacket(
    const StringExtractorGDBRemote &response) {
  const bool did_exec =
      response.GetStringRef().find(";reason:exec;") != std::string::npos;
  if (did_exec) {
    Log *log(ProcessGDBRemoteLog::GetLogIfAllCategoriesSet(GDBR_LOG_PROCESS));
    if (log)
      log->Printf("ProcessGDBRemote::SetLastStopPacket () - detected exec");

    m_thread_list_real.Clear();
    m_thread_list.Clear();
    BuildDynamicRegisterInfo(true);
    m_gdb_comm.ResetDiscoverableSettings(did_exec);
  }

  // Scope the lock
  {
    // Lock the thread stack while we access it
    std::lock_guard<std::recursive_mutex> guard(m_last_stop_packet_mutex);

    // We are are not using non-stop mode, there can only be one last stop
    // reply packet, so clear the list.
    if (!GetTarget().GetNonStopModeEnabled())
      m_stop_packet_stack.clear();

    // Add this stop packet to the stop packet stack This stack will get popped
    // and examined when we switch to the Stopped state
    m_stop_packet_stack.push_back(response);
  }
}

void ProcessGDBRemote::SetUnixSignals(const UnixSignalsSP &signals_sp) {
  Process::SetUnixSignals(std::make_shared<GDBRemoteSignals>(signals_sp));
}

//------------------------------------------------------------------
// Process Queries
//------------------------------------------------------------------

bool ProcessGDBRemote::IsAlive() {
  return m_gdb_comm.IsConnected() && Process::IsAlive();
}

addr_t ProcessGDBRemote::GetImageInfoAddress() {
  // request the link map address via the $qShlibInfoAddr packet
  lldb::addr_t addr = m_gdb_comm.GetShlibInfoAddr();

  // the loaded module list can also provides a link map address
  if (addr == LLDB_INVALID_ADDRESS) {
    LoadedModuleInfoList list;
    if (GetLoadedModuleList(list).Success())
      addr = list.m_link_map;
  }

  return addr;
}

void ProcessGDBRemote::WillPublicStop() {
  // See if the GDB remote client supports the JSON threads info. If so, we
  // gather stop info for all threads, expedited registers, expedited memory,
  // runtime queue information (iOS and MacOSX only), and more. Expediting
  // memory will help stack backtracing be much faster. Expediting registers
  // will make sure we don't have to read the thread registers for GPRs.
  m_jthreadsinfo_sp = m_gdb_comm.GetThreadsInfo();

  if (m_jthreadsinfo_sp) {
    // Now set the stop info for each thread and also expedite any registers
    // and memory that was in the jThreadsInfo response.
    StructuredData::Array *thread_infos = m_jthreadsinfo_sp->GetAsArray();
    if (thread_infos) {
      const size_t n = thread_infos->GetSize();
      for (size_t i = 0; i < n; ++i) {
        StructuredData::Dictionary *thread_dict =
            thread_infos->GetItemAtIndex(i)->GetAsDictionary();
        if (thread_dict)
          SetThreadStopInfo(thread_dict);
      }
    }
  }
}

//------------------------------------------------------------------
// Process Memory
//------------------------------------------------------------------
size_t ProcessGDBRemote::DoReadMemory(addr_t addr, void *buf, size_t size,
                                      Status &error) {
  GetMaxMemorySize();
  bool binary_memory_read = m_gdb_comm.GetxPacketSupported();
  // M and m packets take 2 bytes for 1 byte of memory
  size_t max_memory_size =
      binary_memory_read ? m_max_memory_size : m_max_memory_size / 2;
  if (size > max_memory_size) {
    // Keep memory read sizes down to a sane limit. This function will be
    // called multiple times in order to complete the task by
    // lldb_private::Process so it is ok to do this.
    size = max_memory_size;
  }

  char packet[64];
  int packet_len;
  packet_len = ::snprintf(packet, sizeof(packet), "%c%" PRIx64 ",%" PRIx64,
                          binary_memory_read ? 'x' : 'm', (uint64_t)addr,
                          (uint64_t)size);
  assert(packet_len + 1 < (int)sizeof(packet));
  UNUSED_IF_ASSERT_DISABLED(packet_len);
  StringExtractorGDBRemote response;
  if (m_gdb_comm.SendPacketAndWaitForResponse(packet, response, true) ==
      GDBRemoteCommunication::PacketResult::Success) {
    if (response.IsNormalResponse()) {
      error.Clear();
      if (binary_memory_read) {
        // The lower level GDBRemoteCommunication packet receive layer has
        // already de-quoted any 0x7d character escaping that was present in
        // the packet

        size_t data_received_size = response.GetBytesLeft();
        if (data_received_size > size) {
          // Don't write past the end of BUF if the remote debug server gave us
          // too much data for some reason.
          data_received_size = size;
        }
        memcpy(buf, response.GetStringRef().data(), data_received_size);
        return data_received_size;
      } else {
        return response.GetHexBytes(
            llvm::MutableArrayRef<uint8_t>((uint8_t *)buf, size), '\xdd');
      }
    } else if (response.IsErrorResponse())
      error.SetErrorStringWithFormat("memory read failed for 0x%" PRIx64, addr);
    else if (response.IsUnsupportedResponse())
      error.SetErrorStringWithFormat(
          "GDB server does not support reading memory");
    else
      error.SetErrorStringWithFormat(
          "unexpected response to GDB server memory read packet '%s': '%s'",
          packet, response.GetStringRef().c_str());
  } else {
    error.SetErrorStringWithFormat("failed to send packet: '%s'", packet);
  }
  return 0;
}

Status ProcessGDBRemote::WriteObjectFile(
    std::vector<ObjectFile::LoadableData> entries) {
  Status error;
  // Sort the entries by address because some writes, like those to flash
  // memory, must happen in order of increasing address.
  std::stable_sort(
      std::begin(entries), std::end(entries),
      [](const ObjectFile::LoadableData a, const ObjectFile::LoadableData b) {
        return a.Dest < b.Dest;
      });
  m_allow_flash_writes = true;
  error = Process::WriteObjectFile(entries);
  if (error.Success())
    error = FlashDone();
  else
    // Even though some of the writing failed, try to send a flash done if some
    // of the writing succeeded so the flash state is reset to normal, but
    // don't stomp on the error status that was set in the write failure since
    // that's the one we want to report back.
    FlashDone();
  m_allow_flash_writes = false;
  return error;
}

bool ProcessGDBRemote::HasErased(FlashRange range) {
  auto size = m_erased_flash_ranges.GetSize();
  for (size_t i = 0; i < size; ++i)
    if (m_erased_flash_ranges.GetEntryAtIndex(i)->Contains(range))
      return true;
  return false;
}

Status ProcessGDBRemote::FlashErase(lldb::addr_t addr, size_t size) {
  Status status;

  MemoryRegionInfo region;
  status = GetMemoryRegionInfo(addr, region);
  if (!status.Success())
    return status;

  // The gdb spec doesn't say if erasures are allowed across multiple regions,
  // but we'll disallow it to be safe and to keep the logic simple by worring
  // about only one region's block size.  DoMemoryWrite is this function's
  // primary user, and it can easily keep writes within a single memory region
  if (addr + size > region.GetRange().GetRangeEnd()) {
    status.SetErrorString("Unable to erase flash in multiple regions");
    return status;
  }

  uint64_t blocksize = region.GetBlocksize();
  if (blocksize == 0) {
    status.SetErrorString("Unable to erase flash because blocksize is 0");
    return status;
  }

  // Erasures can only be done on block boundary adresses, so round down addr
  // and round up size
  lldb::addr_t block_start_addr = addr - (addr % blocksize);
  size += (addr - block_start_addr);
  if ((size % blocksize) != 0)
    size += (blocksize - size % blocksize);

  FlashRange range(block_start_addr, size);

  if (HasErased(range))
    return status;

  // We haven't erased the entire range, but we may have erased part of it.
  // (e.g., block A is already erased and range starts in A and ends in B). So,
  // adjust range if necessary to exclude already erased blocks.
  if (!m_erased_flash_ranges.IsEmpty()) {
    // Assuming that writes and erasures are done in increasing addr order,
    // because that is a requirement of the vFlashWrite command.  Therefore, we
    // only need to look at the last range in the list for overlap.
    const auto &last_range = *m_erased_flash_ranges.Back();
    if (range.GetRangeBase() < last_range.GetRangeEnd()) {
      auto overlap = last_range.GetRangeEnd() - range.GetRangeBase();
      // overlap will be less than range.GetByteSize() or else HasErased()
      // would have been true
      range.SetByteSize(range.GetByteSize() - overlap);
      range.SetRangeBase(range.GetRangeBase() + overlap);
    }
  }

  StreamString packet;
  packet.Printf("vFlashErase:%" PRIx64 ",%" PRIx64, range.GetRangeBase(),
                (uint64_t)range.GetByteSize());

  StringExtractorGDBRemote response;
  if (m_gdb_comm.SendPacketAndWaitForResponse(packet.GetString(), response,
                                              true) ==
      GDBRemoteCommunication::PacketResult::Success) {
    if (response.IsOKResponse()) {
      m_erased_flash_ranges.Insert(range, true);
    } else {
      if (response.IsErrorResponse())
        status.SetErrorStringWithFormat("flash erase failed for 0x%" PRIx64,
                                        addr);
      else if (response.IsUnsupportedResponse())
        status.SetErrorStringWithFormat("GDB server does not support flashing");
      else
        status.SetErrorStringWithFormat(
            "unexpected response to GDB server flash erase packet '%s': '%s'",
            packet.GetData(), response.GetStringRef().c_str());
    }
  } else {
    status.SetErrorStringWithFormat("failed to send packet: '%s'",
                                    packet.GetData());
  }
  return status;
}

Status ProcessGDBRemote::FlashDone() {
  Status status;
  // If we haven't erased any blocks, then we must not have written anything
  // either, so there is no need to actually send a vFlashDone command
  if (m_erased_flash_ranges.IsEmpty())
    return status;
  StringExtractorGDBRemote response;
  if (m_gdb_comm.SendPacketAndWaitForResponse("vFlashDone", response, true) ==
      GDBRemoteCommunication::PacketResult::Success) {
    if (response.IsOKResponse()) {
      m_erased_flash_ranges.Clear();
    } else {
      if (response.IsErrorResponse())
        status.SetErrorStringWithFormat("flash done failed");
      else if (response.IsUnsupportedResponse())
        status.SetErrorStringWithFormat("GDB server does not support flashing");
      else
        status.SetErrorStringWithFormat(
            "unexpected response to GDB server flash done packet: '%s'",
            response.GetStringRef().c_str());
    }
  } else {
    status.SetErrorStringWithFormat("failed to send flash done packet");
  }
  return status;
}

size_t ProcessGDBRemote::DoWriteMemory(addr_t addr, const void *buf,
                                       size_t size, Status &error) {
  GetMaxMemorySize();
  // M and m packets take 2 bytes for 1 byte of memory
  size_t max_memory_size = m_max_memory_size / 2;
  if (size > max_memory_size) {
    // Keep memory read sizes down to a sane limit. This function will be
    // called multiple times in order to complete the task by
    // lldb_private::Process so it is ok to do this.
    size = max_memory_size;
  }

  StreamGDBRemote packet;

  MemoryRegionInfo region;
  Status region_status = GetMemoryRegionInfo(addr, region);

  bool is_flash =
      region_status.Success() && region.GetFlash() == MemoryRegionInfo::eYes;

  if (is_flash) {
    if (!m_allow_flash_writes) {
      error.SetErrorString("Writing to flash memory is not allowed");
      return 0;
    }
    // Keep the write within a flash memory region
    if (addr + size > region.GetRange().GetRangeEnd())
      size = region.GetRange().GetRangeEnd() - addr;
    // Flash memory must be erased before it can be written
    error = FlashErase(addr, size);
    if (!error.Success())
      return 0;
    packet.Printf("vFlashWrite:%" PRIx64 ":", addr);
    packet.PutEscapedBytes(buf, size);
  } else {
    packet.Printf("M%" PRIx64 ",%" PRIx64 ":", addr, (uint64_t)size);
    packet.PutBytesAsRawHex8(buf, size, endian::InlHostByteOrder(),
                             endian::InlHostByteOrder());
  }
  StringExtractorGDBRemote response;
  if (m_gdb_comm.SendPacketAndWaitForResponse(packet.GetString(), response,
                                              true) ==
      GDBRemoteCommunication::PacketResult::Success) {
    if (response.IsOKResponse()) {
      error.Clear();
      return size;
    } else if (response.IsErrorResponse())
      error.SetErrorStringWithFormat("memory write failed for 0x%" PRIx64,
                                     addr);
    else if (response.IsUnsupportedResponse())
      error.SetErrorStringWithFormat(
          "GDB server does not support writing memory");
    else
      error.SetErrorStringWithFormat(
          "unexpected response to GDB server memory write packet '%s': '%s'",
          packet.GetData(), response.GetStringRef().c_str());
  } else {
    error.SetErrorStringWithFormat("failed to send packet: '%s'",
                                   packet.GetData());
  }
  return 0;
}

lldb::addr_t ProcessGDBRemote::DoAllocateMemory(size_t size,
                                                uint32_t permissions,
                                                Status &error) {
  Log *log(
      GetLogIfAnyCategoriesSet(LIBLLDB_LOG_PROCESS | LIBLLDB_LOG_EXPRESSIONS));
  addr_t allocated_addr = LLDB_INVALID_ADDRESS;

  if (m_gdb_comm.SupportsAllocDeallocMemory() != eLazyBoolNo) {
    allocated_addr = m_gdb_comm.AllocateMemory(size, permissions);
    if (allocated_addr != LLDB_INVALID_ADDRESS ||
        m_gdb_comm.SupportsAllocDeallocMemory() == eLazyBoolYes)
      return allocated_addr;
  }

  if (m_gdb_comm.SupportsAllocDeallocMemory() == eLazyBoolNo) {
    // Call mmap() to create memory in the inferior..
    unsigned prot = 0;
    if (permissions & lldb::ePermissionsReadable)
      prot |= eMmapProtRead;
    if (permissions & lldb::ePermissionsWritable)
      prot |= eMmapProtWrite;
    if (permissions & lldb::ePermissionsExecutable)
      prot |= eMmapProtExec;

    if (InferiorCallMmap(this, allocated_addr, 0, size, prot,
                         eMmapFlagsAnon | eMmapFlagsPrivate, -1, 0))
      m_addr_to_mmap_size[allocated_addr] = size;
    else {
      allocated_addr = LLDB_INVALID_ADDRESS;
      if (log)
        log->Printf("ProcessGDBRemote::%s no direct stub support for memory "
                    "allocation, and InferiorCallMmap also failed - is stub "
                    "missing register context save/restore capability?",
                    __FUNCTION__);
    }
  }

  if (allocated_addr == LLDB_INVALID_ADDRESS)
    error.SetErrorStringWithFormat(
        "unable to allocate %" PRIu64 " bytes of memory with permissions %s",
        (uint64_t)size, GetPermissionsAsCString(permissions));
  else
    error.Clear();
  return allocated_addr;
}

Status ProcessGDBRemote::GetMemoryRegionInfo(addr_t load_addr,
                                             MemoryRegionInfo &region_info) {

  Status error(m_gdb_comm.GetMemoryRegionInfo(load_addr, region_info));
  return error;
}

Status ProcessGDBRemote::GetWatchpointSupportInfo(uint32_t &num) {

  Status error(m_gdb_comm.GetWatchpointSupportInfo(num));
  return error;
}

Status ProcessGDBRemote::GetWatchpointSupportInfo(uint32_t &num, bool &after) {
  Status error(m_gdb_comm.GetWatchpointSupportInfo(
      num, after, GetTarget().GetArchitecture()));
  return error;
}

Status ProcessGDBRemote::DoDeallocateMemory(lldb::addr_t addr) {
  Status error;
  LazyBool supported = m_gdb_comm.SupportsAllocDeallocMemory();

  switch (supported) {
  case eLazyBoolCalculate:
    // We should never be deallocating memory without allocating memory first
    // so we should never get eLazyBoolCalculate
    error.SetErrorString(
        "tried to deallocate memory without ever allocating memory");
    break;

  case eLazyBoolYes:
    if (!m_gdb_comm.DeallocateMemory(addr))
      error.SetErrorStringWithFormat(
          "unable to deallocate memory at 0x%" PRIx64, addr);
    break;

  case eLazyBoolNo:
    // Call munmap() to deallocate memory in the inferior..
    {
      MMapMap::iterator pos = m_addr_to_mmap_size.find(addr);
      if (pos != m_addr_to_mmap_size.end() &&
          InferiorCallMunmap(this, addr, pos->second))
        m_addr_to_mmap_size.erase(pos);
      else
        error.SetErrorStringWithFormat(
            "unable to deallocate memory at 0x%" PRIx64, addr);
    }
    break;
  }

  return error;
}

//------------------------------------------------------------------
// Process STDIO
//------------------------------------------------------------------
size_t ProcessGDBRemote::PutSTDIN(const char *src, size_t src_len,
                                  Status &error) {
  if (m_stdio_communication.IsConnected()) {
    ConnectionStatus status;
    m_stdio_communication.Write(src, src_len, status, NULL);
  } else if (m_stdin_forward) {
    m_gdb_comm.SendStdinNotification(src, src_len);
  }
  return 0;
}

Status ProcessGDBRemote::EnableBreakpointSite(BreakpointSite *bp_site) {
  Status error;
  assert(bp_site != NULL);

  // Get logging info
  Log *log(ProcessGDBRemoteLog::GetLogIfAllCategoriesSet(GDBR_LOG_BREAKPOINTS));
  user_id_t site_id = bp_site->GetID();

  // Get the breakpoint address
  const addr_t addr = bp_site->GetLoadAddress();

  // Log that a breakpoint was requested
  if (log)
    log->Printf("ProcessGDBRemote::EnableBreakpointSite (size_id = %" PRIu64
                ") address = 0x%" PRIx64,
                site_id, (uint64_t)addr);

  // Breakpoint already exists and is enabled
  if (bp_site->IsEnabled()) {
    if (log)
      log->Printf("ProcessGDBRemote::EnableBreakpointSite (size_id = %" PRIu64
                  ") address = 0x%" PRIx64 " -- SUCCESS (already enabled)",
                  site_id, (uint64_t)addr);
    return error;
  }

  // Get the software breakpoint trap opcode size
  const size_t bp_op_size = GetSoftwareBreakpointTrapOpcode(bp_site);

  // SupportsGDBStoppointPacket() simply checks a boolean, indicating if this
  // breakpoint type is supported by the remote stub. These are set to true by
  // default, and later set to false only after we receive an unimplemented
  // response when sending a breakpoint packet. This means initially that
  // unless we were specifically instructed to use a hardware breakpoint, LLDB
  // will attempt to set a software breakpoint. HardwareRequired() also queries
  // a boolean variable which indicates if the user specifically asked for
  // hardware breakpoints.  If true then we will skip over software
  // breakpoints.
  if (m_gdb_comm.SupportsGDBStoppointPacket(eBreakpointSoftware) &&
      (!bp_site->HardwareRequired())) {
    // Try to send off a software breakpoint packet ($Z0)
    uint8_t error_no = m_gdb_comm.SendGDBStoppointTypePacket(
        eBreakpointSoftware, true, addr, bp_op_size);
    if (error_no == 0) {
      // The breakpoint was placed successfully
      bp_site->SetEnabled(true);
      bp_site->SetType(BreakpointSite::eExternal);
      return error;
    }

    // SendGDBStoppointTypePacket() will return an error if it was unable to
    // set this breakpoint. We need to differentiate between a error specific
    // to placing this breakpoint or if we have learned that this breakpoint
    // type is unsupported. To do this, we must test the support boolean for
    // this breakpoint type to see if it now indicates that this breakpoint
    // type is unsupported.  If they are still supported then we should return
    // with the error code.  If they are now unsupported, then we would like to
    // fall through and try another form of breakpoint.
    if (m_gdb_comm.SupportsGDBStoppointPacket(eBreakpointSoftware)) {
      if (error_no != UINT8_MAX)
        error.SetErrorStringWithFormat(
            "error: %d sending the breakpoint request", errno);
      else
        error.SetErrorString("error sending the breakpoint request");
      return error;
    }

    // We reach here when software breakpoints have been found to be
    // unsupported. For future calls to set a breakpoint, we will not attempt
    // to set a breakpoint with a type that is known not to be supported.
    if (log)
      log->Printf("Software breakpoints are unsupported");

    // So we will fall through and try a hardware breakpoint
  }

  // The process of setting a hardware breakpoint is much the same as above.
  // We check the supported boolean for this breakpoint type, and if it is
  // thought to be supported then we will try to set this breakpoint with a
  // hardware breakpoint.
  if (m_gdb_comm.SupportsGDBStoppointPacket(eBreakpointHardware)) {
    // Try to send off a hardware breakpoint packet ($Z1)
    uint8_t error_no = m_gdb_comm.SendGDBStoppointTypePacket(
        eBreakpointHardware, true, addr, bp_op_size);
    if (error_no == 0) {
      // The breakpoint was placed successfully
      bp_site->SetEnabled(true);
      bp_site->SetType(BreakpointSite::eHardware);
      return error;
    }

    // Check if the error was something other then an unsupported breakpoint
    // type
    if (m_gdb_comm.SupportsGDBStoppointPacket(eBreakpointHardware)) {
      // Unable to set this hardware breakpoint
      if (error_no != UINT8_MAX)
        error.SetErrorStringWithFormat(
            "error: %d sending the hardware breakpoint request "
            "(hardware breakpoint resources might be exhausted or unavailable)",
            error_no);
      else
        error.SetErrorString("error sending the hardware breakpoint request "
                             "(hardware breakpoint resources "
                             "might be exhausted or unavailable)");
      return error;
    }

    // We will reach here when the stub gives an unsupported response to a
    // hardware breakpoint
    if (log)
      log->Printf("Hardware breakpoints are unsupported");

    // Finally we will falling through to a #trap style breakpoint
  }

  // Don't fall through when hardware breakpoints were specifically requested
  if (bp_site->HardwareRequired()) {
    error.SetErrorString("hardware breakpoints are not supported");
    return error;
  }

  // As a last resort we want to place a manual breakpoint. An instruction is
  // placed into the process memory using memory write packets.
  return EnableSoftwareBreakpoint(bp_site);
}

Status ProcessGDBRemote::DisableBreakpointSite(BreakpointSite *bp_site) {
  Status error;
  assert(bp_site != NULL);
  addr_t addr = bp_site->GetLoadAddress();
  user_id_t site_id = bp_site->GetID();
  Log *log(ProcessGDBRemoteLog::GetLogIfAllCategoriesSet(GDBR_LOG_BREAKPOINTS));
  if (log)
    log->Printf("ProcessGDBRemote::DisableBreakpointSite (site_id = %" PRIu64
                ") addr = 0x%8.8" PRIx64,
                site_id, (uint64_t)addr);

  if (bp_site->IsEnabled()) {
    const size_t bp_op_size = GetSoftwareBreakpointTrapOpcode(bp_site);

    BreakpointSite::Type bp_type = bp_site->GetType();
    switch (bp_type) {
    case BreakpointSite::eSoftware:
      error = DisableSoftwareBreakpoint(bp_site);
      break;

    case BreakpointSite::eHardware:
      if (m_gdb_comm.SendGDBStoppointTypePacket(eBreakpointHardware, false,
                                                addr, bp_op_size))
        error.SetErrorToGenericError();
      break;

    case BreakpointSite::eExternal: {
      GDBStoppointType stoppoint_type;
      if (bp_site->IsHardware())
        stoppoint_type = eBreakpointHardware;
      else
        stoppoint_type = eBreakpointSoftware;

      if (m_gdb_comm.SendGDBStoppointTypePacket(stoppoint_type, false, addr,
                                                bp_op_size))
        error.SetErrorToGenericError();
    } break;
    }
    if (error.Success())
      bp_site->SetEnabled(false);
  } else {
    if (log)
      log->Printf("ProcessGDBRemote::DisableBreakpointSite (site_id = %" PRIu64
                  ") addr = 0x%8.8" PRIx64 " -- SUCCESS (already disabled)",
                  site_id, (uint64_t)addr);
    return error;
  }

  if (error.Success())
    error.SetErrorToGenericError();
  return error;
}

// Pre-requisite: wp != NULL.
static GDBStoppointType GetGDBStoppointType(Watchpoint *wp) {
  assert(wp);
  bool watch_read = wp->WatchpointRead();
  bool watch_write = wp->WatchpointWrite();

  // watch_read and watch_write cannot both be false.
  assert(watch_read || watch_write);
  if (watch_read && watch_write)
    return eWatchpointReadWrite;
  else if (watch_read)
    return eWatchpointRead;
  else // Must be watch_write, then.
    return eWatchpointWrite;
}

Status ProcessGDBRemote::EnableWatchpoint(Watchpoint *wp, bool notify) {
  Status error;
  if (wp) {
    user_id_t watchID = wp->GetID();
    addr_t addr = wp->GetLoadAddress();
    Log *log(
        ProcessGDBRemoteLog::GetLogIfAllCategoriesSet(GDBR_LOG_WATCHPOINTS));
    if (log)
      log->Printf("ProcessGDBRemote::EnableWatchpoint(watchID = %" PRIu64 ")",
                  watchID);
    if (wp->IsEnabled()) {
      if (log)
        log->Printf("ProcessGDBRemote::EnableWatchpoint(watchID = %" PRIu64
                    ") addr = 0x%8.8" PRIx64 ": watchpoint already enabled.",
                    watchID, (uint64_t)addr);
      return error;
    }

    GDBStoppointType type = GetGDBStoppointType(wp);
    // Pass down an appropriate z/Z packet...
    if (m_gdb_comm.SupportsGDBStoppointPacket(type)) {
      if (m_gdb_comm.SendGDBStoppointTypePacket(type, true, addr,
                                                wp->GetByteSize()) == 0) {
        wp->SetEnabled(true, notify);
        return error;
      } else
        error.SetErrorString("sending gdb watchpoint packet failed");
    } else
      error.SetErrorString("watchpoints not supported");
  } else {
    error.SetErrorString("Watchpoint argument was NULL.");
  }
  if (error.Success())
    error.SetErrorToGenericError();
  return error;
}

Status ProcessGDBRemote::DisableWatchpoint(Watchpoint *wp, bool notify) {
  Status error;
  if (wp) {
    user_id_t watchID = wp->GetID();

    Log *log(
        ProcessGDBRemoteLog::GetLogIfAllCategoriesSet(GDBR_LOG_WATCHPOINTS));

    addr_t addr = wp->GetLoadAddress();

    if (log)
      log->Printf("ProcessGDBRemote::DisableWatchpoint (watchID = %" PRIu64
                  ") addr = 0x%8.8" PRIx64,
                  watchID, (uint64_t)addr);

    if (!wp->IsEnabled()) {
      if (log)
        log->Printf("ProcessGDBRemote::DisableWatchpoint (watchID = %" PRIu64
                    ") addr = 0x%8.8" PRIx64 " -- SUCCESS (already disabled)",
                    watchID, (uint64_t)addr);
      // See also 'class WatchpointSentry' within StopInfo.cpp. This disabling
      // attempt might come from the user-supplied actions, we'll route it in
      // order for the watchpoint object to intelligently process this action.
      wp->SetEnabled(false, notify);
      return error;
    }

    if (wp->IsHardware()) {
      GDBStoppointType type = GetGDBStoppointType(wp);
      // Pass down an appropriate z/Z packet...
      if (m_gdb_comm.SendGDBStoppointTypePacket(type, false, addr,
                                                wp->GetByteSize()) == 0) {
        wp->SetEnabled(false, notify);
        return error;
      } else
        error.SetErrorString("sending gdb watchpoint packet failed");
    }
    // TODO: clear software watchpoints if we implement them
  } else {
    error.SetErrorString("Watchpoint argument was NULL.");
  }
  if (error.Success())
    error.SetErrorToGenericError();
  return error;
}

void ProcessGDBRemote::Clear() {
  m_thread_list_real.Clear();
  m_thread_list.Clear();
}

Status ProcessGDBRemote::DoSignal(int signo) {
  Status error;
  Log *log(ProcessGDBRemoteLog::GetLogIfAllCategoriesSet(GDBR_LOG_PROCESS));
  if (log)
    log->Printf("ProcessGDBRemote::DoSignal (signal = %d)", signo);

  if (!m_gdb_comm.SendAsyncSignal(signo))
    error.SetErrorStringWithFormat("failed to send signal %i", signo);
  return error;
}

Status ProcessGDBRemote::ConnectToReplayServer(repro::Loader *loader) {
  if (!loader)
    return Status("No loader provided.");

  auto provider_info = loader->GetProviderInfo("gdb-remote");
  if (!provider_info)
    return Status("No provider for gdb-remote.");

  if (provider_info->files.empty())
    return Status("Provider for  gdb-remote contains no files.");

  // Construct replay history path.
  FileSpec history_file = loader->GetRoot().CopyByAppendingPathComponent(
      provider_info->files.front());

  // Enable replay mode.
  m_replay_mode = true;

  // Load replay history.
  if (auto error = m_gdb_replay_server.LoadReplayHistory(history_file))
    return Status("Unable to load replay history");

  // Make a local connection.
  if (auto error = GDBRemoteCommunication::ConnectLocally(m_gdb_comm,
                                                          m_gdb_replay_server))
    return Status("Unable to connect to replay server");

  // Start server thread.
  m_gdb_replay_server.StartAsyncThread();

  // Start client thread.
  StartAsyncThread();

  // Do the usual setup.
  return ConnectToDebugserver("");
}

Status
ProcessGDBRemote::EstablishConnectionIfNeeded(const ProcessInfo &process_info) {
  // Make sure we aren't already connected?
  if (m_gdb_comm.IsConnected())
    return Status();

  PlatformSP platform_sp(GetTarget().GetPlatform());
  if (platform_sp && !platform_sp->IsHost())
    return Status("Lost debug server connection");

  if (repro::Loader *loader = repro::Reproducer::Instance().GetLoader())
    return ConnectToReplayServer(loader);

  auto error = LaunchAndConnectToDebugserver(process_info);
  if (error.Fail()) {
    const char *error_string = error.AsCString();
    if (error_string == nullptr)
      error_string = "unable to launch " DEBUGSERVER_BASENAME;
  }
  return error;
}
#if !defined(_WIN32)
#define USE_SOCKETPAIR_FOR_LOCAL_CONNECTION 1
#endif

#ifdef USE_SOCKETPAIR_FOR_LOCAL_CONNECTION
static bool SetCloexecFlag(int fd) {
#if defined(FD_CLOEXEC)
  int flags = ::fcntl(fd, F_GETFD);
  if (flags == -1)
    return false;
  return (::fcntl(fd, F_SETFD, flags | FD_CLOEXEC) == 0);
#else
  return false;
#endif
}
#endif

Status ProcessGDBRemote::LaunchAndConnectToDebugserver(
    const ProcessInfo &process_info) {
  using namespace std::placeholders; // For _1, _2, etc.

  Status error;
  if (m_debugserver_pid == LLDB_INVALID_PROCESS_ID) {
    // If we locate debugserver, keep that located version around
    static FileSpec g_debugserver_file_spec;

    ProcessLaunchInfo debugserver_launch_info;
    // Make debugserver run in its own session so signals generated by special
    // terminal key sequences (^C) don't affect debugserver.
    debugserver_launch_info.SetLaunchInSeparateProcessGroup(true);

    const std::weak_ptr<ProcessGDBRemote> this_wp =
        std::static_pointer_cast<ProcessGDBRemote>(shared_from_this());
    debugserver_launch_info.SetMonitorProcessCallback(
        std::bind(MonitorDebugserverProcess, this_wp, _1, _2, _3, _4), false);
    debugserver_launch_info.SetUserID(process_info.GetUserID());

    int communication_fd = -1;
#ifdef USE_SOCKETPAIR_FOR_LOCAL_CONNECTION
    // Use a socketpair on non-Windows systems for security and performance
    // reasons.
    int sockets[2]; /* the pair of socket descriptors */
    if (socketpair(AF_UNIX, SOCK_STREAM, 0, sockets) == -1) {
      error.SetErrorToErrno();
      return error;
    }

    int our_socket = sockets[0];
    int gdb_socket = sockets[1];
    CleanUp cleanup_our(close, our_socket);
    CleanUp cleanup_gdb(close, gdb_socket);

    // Don't let any child processes inherit our communication socket
    SetCloexecFlag(our_socket);
    communication_fd = gdb_socket;
#endif

    error = m_gdb_comm.StartDebugserverProcess(
        nullptr, GetTarget().GetPlatform().get(), debugserver_launch_info,
        nullptr, nullptr, communication_fd);

    if (error.Success())
      m_debugserver_pid = debugserver_launch_info.GetProcessID();
    else
      m_debugserver_pid = LLDB_INVALID_PROCESS_ID;

    if (m_debugserver_pid != LLDB_INVALID_PROCESS_ID) {
#ifdef USE_SOCKETPAIR_FOR_LOCAL_CONNECTION
      // Our process spawned correctly, we can now set our connection to use
      // our end of the socket pair
      cleanup_our.disable();
      m_gdb_comm.SetConnection(new ConnectionFileDescriptor(our_socket, true));
#endif
      StartAsyncThread();
    }

    if (error.Fail()) {
      Log *log(ProcessGDBRemoteLog::GetLogIfAllCategoriesSet(GDBR_LOG_PROCESS));

      if (log)
        log->Printf("failed to start debugserver process: %s",
                    error.AsCString());
      return error;
    }

    if (m_gdb_comm.IsConnected()) {
      // Finish the connection process by doing the handshake without
      // connecting (send NULL URL)
      error = ConnectToDebugserver("");
    } else {
      error.SetErrorString("connection failed");
    }
  }
  return error;
}

bool ProcessGDBRemote::MonitorDebugserverProcess(
    std::weak_ptr<ProcessGDBRemote> process_wp, lldb::pid_t debugserver_pid,
    bool exited,    // True if the process did exit
    int signo,      // Zero for no signal
    int exit_status // Exit value of process if signal is zero
) {
  // "debugserver_pid" argument passed in is the process ID for debugserver
  // that we are tracking...
  Log *log(ProcessGDBRemoteLog::GetLogIfAllCategoriesSet(GDBR_LOG_PROCESS));
  const bool handled = true;

  if (log)
    log->Printf("ProcessGDBRemote::%s(process_wp, pid=%" PRIu64
                ", signo=%i (0x%x), exit_status=%i)",
                __FUNCTION__, debugserver_pid, signo, signo, exit_status);

  std::shared_ptr<ProcessGDBRemote> process_sp = process_wp.lock();
  if (log)
    log->Printf("ProcessGDBRemote::%s(process = %p)", __FUNCTION__,
                static_cast<void *>(process_sp.get()));
  if (!process_sp || process_sp->m_debugserver_pid != debugserver_pid)
    return handled;

  // Sleep for a half a second to make sure our inferior process has time to
  // set its exit status before we set it incorrectly when both the debugserver
  // and the inferior process shut down.
  usleep(500000);
  // If our process hasn't yet exited, debugserver might have died. If the
  // process did exit, then we are reaping it.
  const StateType state = process_sp->GetState();

  if (state != eStateInvalid && state != eStateUnloaded &&
      state != eStateExited && state != eStateDetached) {
    char error_str[1024];
    if (signo) {
      const char *signal_cstr =
          process_sp->GetUnixSignals()->GetSignalAsCString(signo);
      if (signal_cstr)
        ::snprintf(error_str, sizeof(error_str),
                   DEBUGSERVER_BASENAME " died with signal %s", signal_cstr);
      else
        ::snprintf(error_str, sizeof(error_str),
                   DEBUGSERVER_BASENAME " died with signal %i", signo);
    } else {
      ::snprintf(error_str, sizeof(error_str),
                 DEBUGSERVER_BASENAME " died with an exit status of 0x%8.8x",
                 exit_status);
    }

    process_sp->SetExitStatus(-1, error_str);
  }
  // Debugserver has exited we need to let our ProcessGDBRemote know that it no
  // longer has a debugserver instance
  process_sp->m_debugserver_pid = LLDB_INVALID_PROCESS_ID;
  return handled;
}

void ProcessGDBRemote::KillDebugserverProcess() {
  m_gdb_comm.Disconnect();
  if (m_debugserver_pid != LLDB_INVALID_PROCESS_ID) {
    Host::Kill(m_debugserver_pid, SIGINT);
    m_debugserver_pid = LLDB_INVALID_PROCESS_ID;
  }
}

void ProcessGDBRemote::Initialize() {
  static llvm::once_flag g_once_flag;

  llvm::call_once(g_once_flag, []() {
    PluginManager::RegisterPlugin(GetPluginNameStatic(),
                                  GetPluginDescriptionStatic(), CreateInstance,
                                  DebuggerInitialize);
  });
}

void ProcessGDBRemote::DebuggerInitialize(Debugger &debugger) {
  if (!PluginManager::GetSettingForProcessPlugin(
          debugger, PluginProperties::GetSettingName())) {
    const bool is_global_setting = true;
    PluginManager::CreateSettingForProcessPlugin(
        debugger, GetGlobalPluginProperties()->GetValueProperties(),
        ConstString("Properties for the gdb-remote process plug-in."),
        is_global_setting);
  }
}

bool ProcessGDBRemote::StartAsyncThread() {
  Log *log(ProcessGDBRemoteLog::GetLogIfAllCategoriesSet(GDBR_LOG_PROCESS));

  if (log)
    log->Printf("ProcessGDBRemote::%s ()", __FUNCTION__);

  std::lock_guard<std::recursive_mutex> guard(m_async_thread_state_mutex);
  if (!m_async_thread.IsJoinable()) {
    // Create a thread that watches our internal state and controls which
    // events make it to clients (into the DCProcess event queue).

    m_async_thread =
        ThreadLauncher::LaunchThread("<lldb.process.gdb-remote.async>",
                                     ProcessGDBRemote::AsyncThread, this, NULL);
  } else if (log)
    log->Printf("ProcessGDBRemote::%s () - Called when Async thread was "
                "already running.",
                __FUNCTION__);

  return m_async_thread.IsJoinable();
}

void ProcessGDBRemote::StopAsyncThread() {
  Log *log(ProcessGDBRemoteLog::GetLogIfAllCategoriesSet(GDBR_LOG_PROCESS));

  if (log)
    log->Printf("ProcessGDBRemote::%s ()", __FUNCTION__);

  std::lock_guard<std::recursive_mutex> guard(m_async_thread_state_mutex);
  if (m_async_thread.IsJoinable()) {
    m_async_broadcaster.BroadcastEvent(eBroadcastBitAsyncThreadShouldExit);

    //  This will shut down the async thread.
    m_gdb_comm.Disconnect(); // Disconnect from the debug server.

    // Stop the stdio thread
    m_async_thread.Join(nullptr);
    m_async_thread.Reset();
  } else if (log)
    log->Printf(
        "ProcessGDBRemote::%s () - Called when Async thread was not running.",
        __FUNCTION__);
}

bool ProcessGDBRemote::HandleNotifyPacket(StringExtractorGDBRemote &packet) {
  // get the packet at a string
  const std::string &pkt = packet.GetStringRef();
  // skip %stop:
  StringExtractorGDBRemote stop_info(pkt.c_str() + 5);

  // pass as a thread stop info packet
  SetLastStopPacket(stop_info);

  // check for more stop reasons
  HandleStopReplySequence();

  // if the process is stopped then we need to fake a resume so that we can
  // stop properly with the new break. This is possible due to
  // SetPrivateState() broadcasting the state change as a side effect.
  if (GetPrivateState() == lldb::StateType::eStateStopped) {
    SetPrivateState(lldb::StateType::eStateRunning);
  }

  // since we have some stopped packets we can halt the process
  SetPrivateState(lldb::StateType::eStateStopped);

  return true;
}

thread_result_t ProcessGDBRemote::AsyncThread(void *arg) {
  ProcessGDBRemote *process = (ProcessGDBRemote *)arg;

  Log *log(ProcessGDBRemoteLog::GetLogIfAllCategoriesSet(GDBR_LOG_PROCESS));
  if (log)
    log->Printf("ProcessGDBRemote::%s (arg = %p, pid = %" PRIu64
                ") thread starting...",
                __FUNCTION__, arg, process->GetID());

  EventSP event_sp;
  bool done = false;
  while (!done) {
    if (log)
      log->Printf("ProcessGDBRemote::%s (arg = %p, pid = %" PRIu64
                  ") listener.WaitForEvent (NULL, event_sp)...",
                  __FUNCTION__, arg, process->GetID());
    if (process->m_async_listener_sp->GetEvent(event_sp, llvm::None)) {
      const uint32_t event_type = event_sp->GetType();
      if (event_sp->BroadcasterIs(&process->m_async_broadcaster)) {
        if (log)
          log->Printf("ProcessGDBRemote::%s (arg = %p, pid = %" PRIu64
                      ") Got an event of type: %d...",
                      __FUNCTION__, arg, process->GetID(), event_type);

        switch (event_type) {
        case eBroadcastBitAsyncContinue: {
          const EventDataBytes *continue_packet =
              EventDataBytes::GetEventDataFromEvent(event_sp.get());

          if (continue_packet) {
            const char *continue_cstr =
                (const char *)continue_packet->GetBytes();
            const size_t continue_cstr_len = continue_packet->GetByteSize();
            if (log)
              log->Printf("ProcessGDBRemote::%s (arg = %p, pid = %" PRIu64
                          ") got eBroadcastBitAsyncContinue: %s",
                          __FUNCTION__, arg, process->GetID(), continue_cstr);

            if (::strstr(continue_cstr, "vAttach") == NULL)
              process->SetPrivateState(eStateRunning);
            StringExtractorGDBRemote response;

            // If in Non-Stop-Mode
            if (process->GetTarget().GetNonStopModeEnabled()) {
              // send the vCont packet
              if (!process->GetGDBRemote().SendvContPacket(
                      llvm::StringRef(continue_cstr, continue_cstr_len),
                      response)) {
                // Something went wrong
                done = true;
                break;
              }
            }
            // If in All-Stop-Mode
            else {
              StateType stop_state =
                  process->GetGDBRemote().SendContinuePacketAndWaitForResponse(
                      *process, *process->GetUnixSignals(),
                      llvm::StringRef(continue_cstr, continue_cstr_len),
                      response);

              // We need to immediately clear the thread ID list so we are sure
              // to get a valid list of threads. The thread ID list might be
              // contained within the "response", or the stop reply packet that
              // caused the stop. So clear it now before we give the stop reply
              // packet to the process using the
              // process->SetLastStopPacket()...
              process->ClearThreadIDList();

              switch (stop_state) {
              case eStateStopped:
              case eStateCrashed:
              case eStateSuspended:
                process->SetLastStopPacket(response);
                process->SetPrivateState(stop_state);
                break;

              case eStateExited: {
                process->SetLastStopPacket(response);
                process->ClearThreadIDList();
                response.SetFilePos(1);

                int exit_status = response.GetHexU8();
                std::string desc_string;
                if (response.GetBytesLeft() > 0 &&
                    response.GetChar('-') == ';') {
                  llvm::StringRef desc_str;
                  llvm::StringRef desc_token;
                  while (response.GetNameColonValue(desc_token, desc_str)) {
                    if (desc_token != "description")
                      continue;
                    StringExtractor extractor(desc_str);
                    extractor.GetHexByteString(desc_string);
                  }
                }
                process->SetExitStatus(exit_status, desc_string.c_str());
                done = true;
                break;
              }
              case eStateInvalid: {
                // Check to see if we were trying to attach and if we got back
                // the "E87" error code from debugserver -- this indicates that
                // the process is not debuggable.  Return a slightly more
                // helpful error message about why the attach failed.
                if (::strstr(continue_cstr, "vAttach") != NULL &&
                    response.GetError() == 0x87) {
                  process->SetExitStatus(-1, "cannot attach to process due to "
                                             "System Integrity Protection");
                } else if (::strstr(continue_cstr, "vAttach") != NULL &&
                           response.GetStatus().Fail()) {
                  process->SetExitStatus(-1, response.GetStatus().AsCString());
                } else {
                  process->SetExitStatus(-1, "lost connection");
                }
                break;
              }

              default:
                process->SetPrivateState(stop_state);
                break;
              } // switch(stop_state)
            }   // else // if in All-stop-mode
          }     // if (continue_packet)
        }       // case eBroadcastBitAysncContinue
        break;

        case eBroadcastBitAsyncThreadShouldExit:
          if (log)
            log->Printf("ProcessGDBRemote::%s (arg = %p, pid = %" PRIu64
                        ") got eBroadcastBitAsyncThreadShouldExit...",
                        __FUNCTION__, arg, process->GetID());
          done = true;
          break;

        default:
          if (log)
            log->Printf("ProcessGDBRemote::%s (arg = %p, pid = %" PRIu64
                        ") got unknown event 0x%8.8x",
                        __FUNCTION__, arg, process->GetID(), event_type);
          done = true;
          break;
        }
      } else if (event_sp->BroadcasterIs(&process->m_gdb_comm)) {
        switch (event_type) {
        case Communication::eBroadcastBitReadThreadDidExit:
          process->SetExitStatus(-1, "lost connection");
          done = true;
          break;

        case GDBRemoteCommunication::eBroadcastBitGdbReadThreadGotNotify: {
          lldb_private::Event *event = event_sp.get();
          const EventDataBytes *continue_packet =
              EventDataBytes::GetEventDataFromEvent(event);
          StringExtractorGDBRemote notify(
              (const char *)continue_packet->GetBytes());
          // Hand this over to the process to handle
          process->HandleNotifyPacket(notify);
          break;
        }

        default:
          if (log)
            log->Printf("ProcessGDBRemote::%s (arg = %p, pid = %" PRIu64
                        ") got unknown event 0x%8.8x",
                        __FUNCTION__, arg, process->GetID(), event_type);
          done = true;
          break;
        }
      }
    } else {
      if (log)
        log->Printf("ProcessGDBRemote::%s (arg = %p, pid = %" PRIu64
                    ") listener.WaitForEvent (NULL, event_sp) => false",
                    __FUNCTION__, arg, process->GetID());
      done = true;
    }
  }

  if (log)
    log->Printf("ProcessGDBRemote::%s (arg = %p, pid = %" PRIu64
                ") thread exiting...",
                __FUNCTION__, arg, process->GetID());

  return NULL;
}

// uint32_t
// ProcessGDBRemote::ListProcessesMatchingName (const char *name, StringList
// &matches, std::vector<lldb::pid_t> &pids)
//{
//    // If we are planning to launch the debugserver remotely, then we need to
//    fire up a debugserver
//    // process and ask it for the list of processes. But if we are local, we
//    can let the Host do it.
//    if (m_local_debugserver)
//    {
//        return Host::ListProcessesMatchingName (name, matches, pids);
//    }
//    else
//    {
//        // FIXME: Implement talking to the remote debugserver.
//        return 0;
//    }
//
//}
//
bool ProcessGDBRemote::NewThreadNotifyBreakpointHit(
    void *baton, StoppointCallbackContext *context, lldb::user_id_t break_id,
    lldb::user_id_t break_loc_id) {
  // I don't think I have to do anything here, just make sure I notice the new
  // thread when it starts to
  // run so I can stop it if that's what I want to do.
  Log *log(GetLogIfAllCategoriesSet(LIBLLDB_LOG_STEP));
  if (log)
    log->Printf("Hit New Thread Notification breakpoint.");
  return false;
}

Status ProcessGDBRemote::UpdateAutomaticSignalFiltering() {
  Log *log(ProcessGDBRemoteLog::GetLogIfAllCategoriesSet(GDBR_LOG_PROCESS));
  LLDB_LOG(log, "Check if need to update ignored signals");

  // QPassSignals package is not supported by the server, there is no way we
  // can ignore any signals on server side.
  if (!m_gdb_comm.GetQPassSignalsSupported())
    return Status();

  // No signals, nothing to send.
  if (m_unix_signals_sp == nullptr)
    return Status();

  // Signals' version hasn't changed, no need to send anything.
  uint64_t new_signals_version = m_unix_signals_sp->GetVersion();
  if (new_signals_version == m_last_signals_version) {
    LLDB_LOG(log, "Signals' version hasn't changed. version={0}",
             m_last_signals_version);
    return Status();
  }

  auto signals_to_ignore =
      m_unix_signals_sp->GetFilteredSignals(false, false, false);
  Status error = m_gdb_comm.SendSignalsToIgnore(signals_to_ignore);

  LLDB_LOG(log,
           "Signals' version changed. old version={0}, new version={1}, "
           "signals ignored={2}, update result={3}",
           m_last_signals_version, new_signals_version,
           signals_to_ignore.size(), error);

  if (error.Success())
    m_last_signals_version = new_signals_version;

  return error;
}

bool ProcessGDBRemote::StartNoticingNewThreads() {
  Log *log(GetLogIfAllCategoriesSet(LIBLLDB_LOG_STEP));
  if (m_thread_create_bp_sp) {
    if (log && log->GetVerbose())
      log->Printf("Enabled noticing new thread breakpoint.");
    m_thread_create_bp_sp->SetEnabled(true);
  } else {
    PlatformSP platform_sp(GetTarget().GetPlatform());
    if (platform_sp) {
      m_thread_create_bp_sp =
          platform_sp->SetThreadCreationBreakpoint(GetTarget());
      if (m_thread_create_bp_sp) {
        if (log && log->GetVerbose())
          log->Printf(
              "Successfully created new thread notification breakpoint %i",
              m_thread_create_bp_sp->GetID());
        m_thread_create_bp_sp->SetCallback(
            ProcessGDBRemote::NewThreadNotifyBreakpointHit, this, true);
      } else {
        if (log)
          log->Printf("Failed to create new thread notification breakpoint.");
      }
    }
  }
  return m_thread_create_bp_sp.get() != NULL;
}

bool ProcessGDBRemote::StopNoticingNewThreads() {
  Log *log(GetLogIfAllCategoriesSet(LIBLLDB_LOG_STEP));
  if (log && log->GetVerbose())
    log->Printf("Disabling new thread notification breakpoint.");

  if (m_thread_create_bp_sp)
    m_thread_create_bp_sp->SetEnabled(false);

  return true;
}

DynamicLoader *ProcessGDBRemote::GetDynamicLoader() {
  if (m_dyld_ap.get() == NULL)
    m_dyld_ap.reset(DynamicLoader::FindPlugin(this, NULL));
  return m_dyld_ap.get();
}

Status ProcessGDBRemote::SendEventData(const char *data) {
  int return_value;
  bool was_supported;

  Status error;

  return_value = m_gdb_comm.SendLaunchEventDataPacket(data, &was_supported);
  if (return_value != 0) {
    if (!was_supported)
      error.SetErrorString("Sending events is not supported for this process.");
    else
      error.SetErrorStringWithFormat("Error sending event data: %d.",
                                     return_value);
  }
  return error;
}

const DataBufferSP ProcessGDBRemote::GetAuxvData() {
  DataBufferSP buf;
  if (m_gdb_comm.GetQXferAuxvReadSupported()) {
    std::string response_string;
    if (m_gdb_comm.SendPacketsAndConcatenateResponses("qXfer:auxv:read::",
                                                      response_string) ==
        GDBRemoteCommunication::PacketResult::Success)
      buf.reset(new DataBufferHeap(response_string.c_str(),
                                   response_string.length()));
  }
  return buf;
}

StructuredData::ObjectSP
ProcessGDBRemote::GetExtendedInfoForThread(lldb::tid_t tid) {
  StructuredData::ObjectSP object_sp;

  if (m_gdb_comm.GetThreadExtendedInfoSupported()) {
    StructuredData::ObjectSP args_dict(new StructuredData::Dictionary());
    SystemRuntime *runtime = GetSystemRuntime();
    if (runtime) {
      runtime->AddThreadExtendedInfoPacketHints(args_dict);
    }
    args_dict->GetAsDictionary()->AddIntegerItem("thread", tid);

    StreamString packet;
    packet << "jThreadExtendedInfo:";
    args_dict->Dump(packet, false);

    // FIXME the final character of a JSON dictionary, '}', is the escape
    // character in gdb-remote binary mode.  lldb currently doesn't escape
    // these characters in its packet output -- so we add the quoted version of
    // the } character here manually in case we talk to a debugserver which un-
    // escapes the characters at packet read time.
    packet << (char)(0x7d ^ 0x20);

    StringExtractorGDBRemote response;
    response.SetResponseValidatorToJSON();
    if (m_gdb_comm.SendPacketAndWaitForResponse(packet.GetString(), response,
                                                false) ==
        GDBRemoteCommunication::PacketResult::Success) {
      StringExtractorGDBRemote::ResponseType response_type =
          response.GetResponseType();
      if (response_type == StringExtractorGDBRemote::eResponse) {
        if (!response.Empty()) {
          object_sp = StructuredData::ParseJSON(response.GetStringRef());
        }
      }
    }
  }
  return object_sp;
}

StructuredData::ObjectSP ProcessGDBRemote::GetLoadedDynamicLibrariesInfos(
    lldb::addr_t image_list_address, lldb::addr_t image_count) {

  StructuredData::ObjectSP args_dict(new StructuredData::Dictionary());
  args_dict->GetAsDictionary()->AddIntegerItem("image_list_address",
                                               image_list_address);
  args_dict->GetAsDictionary()->AddIntegerItem("image_count", image_count);

  return GetLoadedDynamicLibrariesInfos_sender(args_dict);
}

StructuredData::ObjectSP ProcessGDBRemote::GetLoadedDynamicLibrariesInfos() {
  StructuredData::ObjectSP args_dict(new StructuredData::Dictionary());

  args_dict->GetAsDictionary()->AddBooleanItem("fetch_all_solibs", true);

  return GetLoadedDynamicLibrariesInfos_sender(args_dict);
}

StructuredData::ObjectSP ProcessGDBRemote::GetLoadedDynamicLibrariesInfos(
    const std::vector<lldb::addr_t> &load_addresses) {
  StructuredData::ObjectSP args_dict(new StructuredData::Dictionary());
  StructuredData::ArraySP addresses(new StructuredData::Array);

  for (auto addr : load_addresses) {
    StructuredData::ObjectSP addr_sp(new StructuredData::Integer(addr));
    addresses->AddItem(addr_sp);
  }

  args_dict->GetAsDictionary()->AddItem("solib_addresses", addresses);

  return GetLoadedDynamicLibrariesInfos_sender(args_dict);
}

StructuredData::ObjectSP
ProcessGDBRemote::GetLoadedDynamicLibrariesInfos_sender(
    StructuredData::ObjectSP args_dict) {
  StructuredData::ObjectSP object_sp;

  if (m_gdb_comm.GetLoadedDynamicLibrariesInfosSupported()) {
    // Scope for the scoped timeout object
    GDBRemoteCommunication::ScopedTimeout timeout(m_gdb_comm,
                                                  std::chrono::seconds(10));

    StreamString packet;
    packet << "jGetLoadedDynamicLibrariesInfos:";
    args_dict->Dump(packet, false);

    // FIXME the final character of a JSON dictionary, '}', is the escape
    // character in gdb-remote binary mode.  lldb currently doesn't escape
    // these characters in its packet output -- so we add the quoted version of
    // the } character here manually in case we talk to a debugserver which un-
    // escapes the characters at packet read time.
    packet << (char)(0x7d ^ 0x20);

    StringExtractorGDBRemote response;
    response.SetResponseValidatorToJSON();
    if (m_gdb_comm.SendPacketAndWaitForResponse(packet.GetString(), response,
                                                false) ==
        GDBRemoteCommunication::PacketResult::Success) {
      StringExtractorGDBRemote::ResponseType response_type =
          response.GetResponseType();
      if (response_type == StringExtractorGDBRemote::eResponse) {
        if (!response.Empty()) {
          object_sp = StructuredData::ParseJSON(response.GetStringRef());
        }
      }
    }
  }
  return object_sp;
}

StructuredData::ObjectSP ProcessGDBRemote::GetSharedCacheInfo() {
  StructuredData::ObjectSP object_sp;
  StructuredData::ObjectSP args_dict(new StructuredData::Dictionary());

  if (m_gdb_comm.GetSharedCacheInfoSupported()) {
    StreamString packet;
    packet << "jGetSharedCacheInfo:";
    args_dict->Dump(packet, false);

    // FIXME the final character of a JSON dictionary, '}', is the escape
    // character in gdb-remote binary mode.  lldb currently doesn't escape
    // these characters in its packet output -- so we add the quoted version of
    // the } character here manually in case we talk to a debugserver which un-
    // escapes the characters at packet read time.
    packet << (char)(0x7d ^ 0x20);

    StringExtractorGDBRemote response;
    response.SetResponseValidatorToJSON();
    if (m_gdb_comm.SendPacketAndWaitForResponse(packet.GetString(), response,
                                                false) ==
        GDBRemoteCommunication::PacketResult::Success) {
      StringExtractorGDBRemote::ResponseType response_type =
          response.GetResponseType();
      if (response_type == StringExtractorGDBRemote::eResponse) {
        if (!response.Empty()) {
          object_sp = StructuredData::ParseJSON(response.GetStringRef());
        }
      }
    }
  }
  return object_sp;
}

Status ProcessGDBRemote::ConfigureStructuredData(
    const ConstString &type_name, const StructuredData::ObjectSP &config_sp) {
  return m_gdb_comm.ConfigureRemoteStructuredData(type_name, config_sp);
}

// Establish the largest memory read/write payloads we should use. If the
// remote stub has a max packet size, stay under that size.
//
// If the remote stub's max packet size is crazy large, use a reasonable
// largeish default.
//
// If the remote stub doesn't advertise a max packet size, use a conservative
// default.

void ProcessGDBRemote::GetMaxMemorySize() {
  const uint64_t reasonable_largeish_default = 128 * 1024;
  const uint64_t conservative_default = 512;

  if (m_max_memory_size == 0) {
    uint64_t stub_max_size = m_gdb_comm.GetRemoteMaxPacketSize();
    if (stub_max_size != UINT64_MAX && stub_max_size != 0) {
      // Save the stub's claimed maximum packet size
      m_remote_stub_max_memory_size = stub_max_size;

      // Even if the stub says it can support ginormous packets, don't exceed
      // our reasonable largeish default packet size.
      if (stub_max_size > reasonable_largeish_default) {
        stub_max_size = reasonable_largeish_default;
      }

      // Memory packet have other overheads too like Maddr,size:#NN Instead of
      // calculating the bytes taken by size and addr every time, we take a
      // maximum guess here.
      if (stub_max_size > 70)
        stub_max_size -= 32 + 32 + 6;
      else {
        // In unlikely scenario that max packet size is less then 70, we will
        // hope that data being written is small enough to fit.
        Log *log(ProcessGDBRemoteLog::GetLogIfAnyCategoryIsSet(
            GDBR_LOG_COMM | GDBR_LOG_MEMORY));
        if (log)
          log->Warning("Packet size is too small. "
                       "LLDB may face problems while writing memory");
      }

      m_max_memory_size = stub_max_size;
    } else {
      m_max_memory_size = conservative_default;
    }
  }
}

void ProcessGDBRemote::SetUserSpecifiedMaxMemoryTransferSize(
    uint64_t user_specified_max) {
  if (user_specified_max != 0) {
    GetMaxMemorySize();

    if (m_remote_stub_max_memory_size != 0) {
      if (m_remote_stub_max_memory_size < user_specified_max) {
        m_max_memory_size = m_remote_stub_max_memory_size; // user specified a
                                                           // packet size too
                                                           // big, go as big
        // as the remote stub says we can go.
      } else {
        m_max_memory_size = user_specified_max; // user's packet size is good
      }
    } else {
      m_max_memory_size =
          user_specified_max; // user's packet size is probably fine
    }
  }
}

bool ProcessGDBRemote::GetModuleSpec(const FileSpec &module_file_spec,
                                     const ArchSpec &arch,
                                     ModuleSpec &module_spec) {
  Log *log = GetLogIfAnyCategoriesSet(LIBLLDB_LOG_PLATFORM);

  const ModuleCacheKey key(module_file_spec.GetPath(),
                           arch.GetTriple().getTriple());
  auto cached = m_cached_module_specs.find(key);
  if (cached != m_cached_module_specs.end()) {
    module_spec = cached->second;
    return bool(module_spec);
  }

  if (!m_gdb_comm.GetModuleInfo(module_file_spec, arch, module_spec)) {
    if (log)
      log->Printf("ProcessGDBRemote::%s - failed to get module info for %s:%s",
                  __FUNCTION__, module_file_spec.GetPath().c_str(),
                  arch.GetTriple().getTriple().c_str());
    return false;
  }

  if (log) {
    StreamString stream;
    module_spec.Dump(stream);
    log->Printf("ProcessGDBRemote::%s - got module info for (%s:%s) : %s",
                __FUNCTION__, module_file_spec.GetPath().c_str(),
                arch.GetTriple().getTriple().c_str(), stream.GetData());
  }

  m_cached_module_specs[key] = module_spec;
  return true;
}

void ProcessGDBRemote::PrefetchModuleSpecs(
    llvm::ArrayRef<FileSpec> module_file_specs, const llvm::Triple &triple) {
  auto module_specs = m_gdb_comm.GetModulesInfo(module_file_specs, triple);
  if (module_specs) {
    for (const FileSpec &spec : module_file_specs)
      m_cached_module_specs[ModuleCacheKey(spec.GetPath(),
                                           triple.getTriple())] = ModuleSpec();
    for (const ModuleSpec &spec : *module_specs)
      m_cached_module_specs[ModuleCacheKey(spec.GetFileSpec().GetPath(),
                                           triple.getTriple())] = spec;
  }
}

llvm::VersionTuple ProcessGDBRemote::GetHostOSVersion() {
  return m_gdb_comm.GetOSVersion();
}

namespace {

typedef std::vector<std::string> stringVec;

typedef std::vector<struct GdbServerRegisterInfo> GDBServerRegisterVec;
struct RegisterSetInfo {
  ConstString name;
};

typedef std::map<uint32_t, RegisterSetInfo> RegisterSetMap;

struct GdbServerTargetInfo {
  std::string arch;
  std::string osabi;
  stringVec includes;
  RegisterSetMap reg_set_map;
};

bool ParseRegisters(XMLNode feature_node, GdbServerTargetInfo &target_info,
                    GDBRemoteDynamicRegisterInfo &dyn_reg_info, ABISP abi_sp,
                    uint32_t &cur_reg_num, uint32_t &reg_offset) {
  if (!feature_node)
    return false;

  feature_node.ForEachChildElementWithName(
      "reg",
      [&target_info, &dyn_reg_info, &cur_reg_num, &reg_offset,
       &abi_sp](const XMLNode &reg_node) -> bool {
        std::string gdb_group;
        std::string gdb_type;
        ConstString reg_name;
        ConstString alt_name;
        ConstString set_name;
        std::vector<uint32_t> value_regs;
        std::vector<uint32_t> invalidate_regs;
        std::vector<uint8_t> dwarf_opcode_bytes;
        bool encoding_set = false;
        bool format_set = false;
        RegisterInfo reg_info = {
            NULL,          // Name
            NULL,          // Alt name
            0,             // byte size
            reg_offset,    // offset
            eEncodingUint, // encoding
            eFormatHex,    // format
            {
                LLDB_INVALID_REGNUM, // eh_frame reg num
                LLDB_INVALID_REGNUM, // DWARF reg num
                LLDB_INVALID_REGNUM, // generic reg num
                cur_reg_num,         // process plugin reg num
                cur_reg_num          // native register number
            },
            NULL,
            NULL,
            NULL, // Dwarf Expression opcode bytes pointer
            0     // Dwarf Expression opcode bytes length
        };

        reg_node.ForEachAttribute([&target_info, &gdb_group, &gdb_type,
                                   &reg_name, &alt_name, &set_name, &value_regs,
                                   &invalidate_regs, &encoding_set, &format_set,
                                   &reg_info, &reg_offset, &dwarf_opcode_bytes](
                                      const llvm::StringRef &name,
                                      const llvm::StringRef &value) -> bool {
          if (name == "name") {
            reg_name.SetString(value);
          } else if (name == "bitsize") {
            reg_info.byte_size =
                StringConvert::ToUInt32(value.data(), 0, 0) / CHAR_BIT;
          } else if (name == "type") {
            gdb_type = value.str();
          } else if (name == "group") {
            gdb_group = value.str();
          } else if (name == "regnum") {
            const uint32_t regnum =
                StringConvert::ToUInt32(value.data(), LLDB_INVALID_REGNUM, 0);
            if (regnum != LLDB_INVALID_REGNUM) {
              reg_info.kinds[eRegisterKindProcessPlugin] = regnum;
            }
          } else if (name == "offset") {
            reg_offset = StringConvert::ToUInt32(value.data(), UINT32_MAX, 0);
          } else if (name == "altname") {
            alt_name.SetString(value);
          } else if (name == "encoding") {
            encoding_set = true;
            reg_info.encoding = Args::StringToEncoding(value, eEncodingUint);
          } else if (name == "format") {
            format_set = true;
            Format format = eFormatInvalid;
            if (OptionArgParser::ToFormat(value.data(), format, NULL).Success())
              reg_info.format = format;
            else if (value == "vector-sint8")
              reg_info.format = eFormatVectorOfSInt8;
            else if (value == "vector-uint8")
              reg_info.format = eFormatVectorOfUInt8;
            else if (value == "vector-sint16")
              reg_info.format = eFormatVectorOfSInt16;
            else if (value == "vector-uint16")
              reg_info.format = eFormatVectorOfUInt16;
            else if (value == "vector-sint32")
              reg_info.format = eFormatVectorOfSInt32;
            else if (value == "vector-uint32")
              reg_info.format = eFormatVectorOfUInt32;
            else if (value == "vector-float32")
              reg_info.format = eFormatVectorOfFloat32;
            else if (value == "vector-uint64")
              reg_info.format = eFormatVectorOfUInt64;
            else if (value == "vector-uint128")
              reg_info.format = eFormatVectorOfUInt128;
          } else if (name == "group_id") {
            const uint32_t set_id =
                StringConvert::ToUInt32(value.data(), UINT32_MAX, 0);
            RegisterSetMap::const_iterator pos =
                target_info.reg_set_map.find(set_id);
            if (pos != target_info.reg_set_map.end())
              set_name = pos->second.name;
          } else if (name == "gcc_regnum" || name == "ehframe_regnum") {
            reg_info.kinds[eRegisterKindEHFrame] =
                StringConvert::ToUInt32(value.data(), LLDB_INVALID_REGNUM, 0);
          } else if (name == "dwarf_regnum") {
            reg_info.kinds[eRegisterKindDWARF] =
                StringConvert::ToUInt32(value.data(), LLDB_INVALID_REGNUM, 0);
          } else if (name == "generic") {
            reg_info.kinds[eRegisterKindGeneric] =
                Args::StringToGenericRegister(value);
          } else if (name == "value_regnums") {
            SplitCommaSeparatedRegisterNumberString(value, value_regs, 0);
          } else if (name == "invalidate_regnums") {
            SplitCommaSeparatedRegisterNumberString(value, invalidate_regs, 0);
          } else if (name == "dynamic_size_dwarf_expr_bytes") {
            StringExtractor opcode_extractor;
            std::string opcode_string = value.str();
            size_t dwarf_opcode_len = opcode_string.length() / 2;
            assert(dwarf_opcode_len > 0);

            dwarf_opcode_bytes.resize(dwarf_opcode_len);
            reg_info.dynamic_size_dwarf_len = dwarf_opcode_len;
            opcode_extractor.GetStringRef().swap(opcode_string);
            uint32_t ret_val =
                opcode_extractor.GetHexBytesAvail(dwarf_opcode_bytes);
            assert(dwarf_opcode_len == ret_val);
            UNUSED_IF_ASSERT_DISABLED(ret_val);
            reg_info.dynamic_size_dwarf_expr_bytes = dwarf_opcode_bytes.data();
          } else {
            printf("unhandled attribute %s = %s\n", name.data(), value.data());
          }
          return true; // Keep iterating through all attributes
        });

        if (!gdb_type.empty() && !(encoding_set || format_set)) {
          if (gdb_type.find("int") == 0) {
            reg_info.format = eFormatHex;
            reg_info.encoding = eEncodingUint;
          } else if (gdb_type == "data_ptr" || gdb_type == "code_ptr") {
            reg_info.format = eFormatAddressInfo;
            reg_info.encoding = eEncodingUint;
          } else if (gdb_type == "i387_ext" || gdb_type == "float") {
            reg_info.format = eFormatFloat;
            reg_info.encoding = eEncodingIEEE754;
          }
        }

        // Only update the register set name if we didn't get a "reg_set"
        // attribute. "set_name" will be empty if we didn't have a "reg_set"
        // attribute.
        if (!set_name && !gdb_group.empty())
          set_name.SetCString(gdb_group.c_str());

        reg_info.byte_offset = reg_offset;
        assert(reg_info.byte_size != 0);
        reg_offset += reg_info.byte_size;
        if (!value_regs.empty()) {
          value_regs.push_back(LLDB_INVALID_REGNUM);
          reg_info.value_regs = value_regs.data();
        }
        if (!invalidate_regs.empty()) {
          invalidate_regs.push_back(LLDB_INVALID_REGNUM);
          reg_info.invalidate_regs = invalidate_regs.data();
        }

        ++cur_reg_num;
        AugmentRegisterInfoViaABI(reg_info, reg_name, abi_sp);
        dyn_reg_info.AddRegister(reg_info, reg_name, alt_name, set_name);

        return true; // Keep iterating through all "reg" elements
      });
  return true;
}

} // namespace

// query the target of gdb-remote for extended target information return:
// 'true'  on success
//          'false' on failure
bool ProcessGDBRemote::GetGDBServerRegisterInfo(ArchSpec &arch_to_use) {
  // Make sure LLDB has an XML parser it can use first
  if (!XMLDocument::XMLEnabled())
    return false;

  // redirect libxml2's error handler since the default prints to stdout

  GDBRemoteCommunicationClient &comm = m_gdb_comm;

  // check that we have extended feature read support
  if (!comm.GetQXferFeaturesReadSupported())
    return false;

  // request the target xml file
  std::string raw;
  lldb_private::Status lldberr;
  if (!comm.ReadExtFeature(ConstString("features"), ConstString("target.xml"),
                           raw, lldberr)) {
    return false;
  }

  XMLDocument xml_document;

  if (xml_document.ParseMemory(raw.c_str(), raw.size(), "target.xml")) {
    GdbServerTargetInfo target_info;

    XMLNode target_node = xml_document.GetRootElement("target");
    if (target_node) {
      std::vector<XMLNode> feature_nodes;
      target_node.ForEachChildElement([&target_info, &feature_nodes](
                                          const XMLNode &node) -> bool {
        llvm::StringRef name = node.GetName();
        if (name == "architecture") {
          node.GetElementText(target_info.arch);
        } else if (name == "osabi") {
          node.GetElementText(target_info.osabi);
        } else if (name == "xi:include" || name == "include") {
          llvm::StringRef href = node.GetAttributeValue("href");
          if (!href.empty())
            target_info.includes.push_back(href.str());
        } else if (name == "feature") {
          feature_nodes.push_back(node);
        } else if (name == "groups") {
          node.ForEachChildElementWithName(
              "group", [&target_info](const XMLNode &node) -> bool {
                uint32_t set_id = UINT32_MAX;
                RegisterSetInfo set_info;

                node.ForEachAttribute(
                    [&set_id, &set_info](const llvm::StringRef &name,
                                         const llvm::StringRef &value) -> bool {
                      if (name == "id")
                        set_id = StringConvert::ToUInt32(value.data(),
                                                         UINT32_MAX, 0);
                      if (name == "name")
                        set_info.name = ConstString(value);
                      return true; // Keep iterating through all attributes
                    });

                if (set_id != UINT32_MAX)
                  target_info.reg_set_map[set_id] = set_info;
                return true; // Keep iterating through all "group" elements
              });
        }
        return true; // Keep iterating through all children of the target_node
      });

      // If the target.xml includes an architecture entry like
      //   <architecture>i386:x86-64</architecture> (seen from VMWare ESXi)
      //   <architecture>arm</architecture> (seen from Segger JLink on unspecified arm board)
      // use that if we don't have anything better.
      if (!arch_to_use.IsValid() && !target_info.arch.empty()) {
        if (target_info.arch == "i386:x86-64") {
          // We don't have any information about vendor or OS.
          arch_to_use.SetTriple("x86_64--");
          GetTarget().MergeArchitecture(arch_to_use);
        }

        // SEGGER J-Link jtag boards send this very-generic arch name,
        // we'll need to use this if we have absolutely nothing better
        // to work with or the register definitions won't be accepted.
        if (target_info.arch == "arm") {
          arch_to_use.SetTriple("arm--");
          GetTarget().MergeArchitecture(arch_to_use);
        }
      }

      // Initialize these outside of ParseRegisters, since they should not be
      // reset inside each include feature
      uint32_t cur_reg_num = 0;
      uint32_t reg_offset = 0;

      // Don't use Process::GetABI, this code gets called from DidAttach, and
      // in that context we haven't set the Target's architecture yet, so the
      // ABI is also potentially incorrect.
      ABISP abi_to_use_sp = ABI::FindPlugin(shared_from_this(), arch_to_use);
      for (auto &feature_node : feature_nodes) {
        ParseRegisters(feature_node, target_info, this->m_register_info,
                       abi_to_use_sp, cur_reg_num, reg_offset);
      }

      for (const auto &include : target_info.includes) {
        // request register file
        std::string xml_data;
        if (!comm.ReadExtFeature(ConstString("features"), ConstString(include),
                                 xml_data, lldberr))
          continue;

        XMLDocument include_xml_document;
        include_xml_document.ParseMemory(xml_data.data(), xml_data.size(),
                                         include.c_str());
        XMLNode include_feature_node =
            include_xml_document.GetRootElement("feature");
        if (include_feature_node) {
          ParseRegisters(include_feature_node, target_info,
                         this->m_register_info, abi_to_use_sp, cur_reg_num,
                         reg_offset);
        }
      }
      this->m_register_info.Finalize(arch_to_use);
    }
  }

  return m_register_info.GetNumRegisters() > 0;
}

Status ProcessGDBRemote::GetLoadedModuleList(LoadedModuleInfoList &list) {
  // Make sure LLDB has an XML parser it can use first
  if (!XMLDocument::XMLEnabled())
    return Status(0, ErrorType::eErrorTypeGeneric);

  Log *log = GetLogIfAnyCategoriesSet(LIBLLDB_LOG_PROCESS);
  if (log)
    log->Printf("ProcessGDBRemote::%s", __FUNCTION__);

  GDBRemoteCommunicationClient &comm = m_gdb_comm;

  // check that we have extended feature read support
  if (comm.GetQXferLibrariesSVR4ReadSupported()) {
    list.clear();

    // request the loaded library list
    std::string raw;
    lldb_private::Status lldberr;

    if (!comm.ReadExtFeature(ConstString("libraries-svr4"), ConstString(""),
                             raw, lldberr))
      return Status(0, ErrorType::eErrorTypeGeneric);

    // parse the xml file in memory
    if (log)
      log->Printf("parsing: %s", raw.c_str());
    XMLDocument doc;

    if (!doc.ParseMemory(raw.c_str(), raw.size(), "noname.xml"))
      return Status(0, ErrorType::eErrorTypeGeneric);

    XMLNode root_element = doc.GetRootElement("library-list-svr4");
    if (!root_element)
      return Status();

    // main link map structure
    llvm::StringRef main_lm = root_element.GetAttributeValue("main-lm");
    if (!main_lm.empty()) {
      list.m_link_map =
          StringConvert::ToUInt64(main_lm.data(), LLDB_INVALID_ADDRESS, 0);
    }

    root_element.ForEachChildElementWithName(
        "library", [log, &list](const XMLNode &library) -> bool {

          LoadedModuleInfoList::LoadedModuleInfo module;

          library.ForEachAttribute(
              [&module](const llvm::StringRef &name,
                        const llvm::StringRef &value) -> bool {

                if (name == "name")
                  module.set_name(value.str());
                else if (name == "lm") {
                  // the address of the link_map struct.
                  module.set_link_map(StringConvert::ToUInt64(
                      value.data(), LLDB_INVALID_ADDRESS, 0));
                } else if (name == "l_addr") {
                  // the displacement as read from the field 'l_addr' of the
                  // link_map struct.
                  module.set_base(StringConvert::ToUInt64(
                      value.data(), LLDB_INVALID_ADDRESS, 0));
                  // base address is always a displacement, not an absolute
                  // value.
                  module.set_base_is_offset(true);
                } else if (name == "l_ld") {
                  // the memory address of the libraries PT_DYAMIC section.
                  module.set_dynamic(StringConvert::ToUInt64(
                      value.data(), LLDB_INVALID_ADDRESS, 0));
                }

                return true; // Keep iterating over all properties of "library"
              });

          if (log) {
            std::string name;
            lldb::addr_t lm = 0, base = 0, ld = 0;
            bool base_is_offset;

            module.get_name(name);
            module.get_link_map(lm);
            module.get_base(base);
            module.get_base_is_offset(base_is_offset);
            module.get_dynamic(ld);

            log->Printf("found (link_map:0x%08" PRIx64 ", base:0x%08" PRIx64
                        "[%s], ld:0x%08" PRIx64 ", name:'%s')",
                        lm, base, (base_is_offset ? "offset" : "absolute"), ld,
                        name.c_str());
          }

          list.add(module);
          return true; // Keep iterating over all "library" elements in the root
                       // node
        });

    if (log)
      log->Printf("found %" PRId32 " modules in total",
                  (int)list.m_list.size());
  } else if (comm.GetQXferLibrariesReadSupported()) {
    list.clear();

    // request the loaded library list
    std::string raw;
    lldb_private::Status lldberr;

    if (!comm.ReadExtFeature(ConstString("libraries"), ConstString(""), raw,
                             lldberr))
      return Status(0, ErrorType::eErrorTypeGeneric);

    if (log)
      log->Printf("parsing: %s", raw.c_str());
    XMLDocument doc;

    if (!doc.ParseMemory(raw.c_str(), raw.size(), "noname.xml"))
      return Status(0, ErrorType::eErrorTypeGeneric);

    XMLNode root_element = doc.GetRootElement("library-list");
    if (!root_element)
      return Status();

    root_element.ForEachChildElementWithName(
        "library", [log, &list](const XMLNode &library) -> bool {
          LoadedModuleInfoList::LoadedModuleInfo module;

          llvm::StringRef name = library.GetAttributeValue("name");
          module.set_name(name.str());

          // The base address of a given library will be the address of its
          // first section. Most remotes send only one section for Windows
          // targets for example.
          const XMLNode &section =
              library.FindFirstChildElementWithName("section");
          llvm::StringRef address = section.GetAttributeValue("address");
          module.set_base(
              StringConvert::ToUInt64(address.data(), LLDB_INVALID_ADDRESS, 0));
          // These addresses are absolute values.
          module.set_base_is_offset(false);

          if (log) {
            std::string name;
            lldb::addr_t base = 0;
            bool base_is_offset;
            module.get_name(name);
            module.get_base(base);
            module.get_base_is_offset(base_is_offset);

            log->Printf("found (base:0x%08" PRIx64 "[%s], name:'%s')", base,
                        (base_is_offset ? "offset" : "absolute"), name.c_str());
          }

          list.add(module);
          return true; // Keep iterating over all "library" elements in the root
                       // node
        });

    if (log)
      log->Printf("found %" PRId32 " modules in total",
                  (int)list.m_list.size());
  } else {
    return Status(0, ErrorType::eErrorTypeGeneric);
  }

  return Status();
}

lldb::ModuleSP ProcessGDBRemote::LoadModuleAtAddress(const FileSpec &file,
                                                     lldb::addr_t link_map,
                                                     lldb::addr_t base_addr,
                                                     bool value_is_offset) {
  DynamicLoader *loader = GetDynamicLoader();
  if (!loader)
    return nullptr;

  return loader->LoadModuleAtAddress(file, link_map, base_addr,
                                     value_is_offset);
}

size_t ProcessGDBRemote::LoadModules(LoadedModuleInfoList &module_list) {
  using lldb_private::process_gdb_remote::ProcessGDBRemote;

  // request a list of loaded libraries from GDBServer
  if (GetLoadedModuleList(module_list).Fail())
    return 0;

  // get a list of all the modules
  ModuleList new_modules;

  for (LoadedModuleInfoList::LoadedModuleInfo &modInfo : module_list.m_list) {
    std::string mod_name;
    lldb::addr_t mod_base;
    lldb::addr_t link_map;
    bool mod_base_is_offset;

    bool valid = true;
    valid &= modInfo.get_name(mod_name);
    valid &= modInfo.get_base(mod_base);
    valid &= modInfo.get_base_is_offset(mod_base_is_offset);
    if (!valid)
      continue;

    if (!modInfo.get_link_map(link_map))
      link_map = LLDB_INVALID_ADDRESS;

    FileSpec file(mod_name);
    FileSystem::Instance().Resolve(file);
    lldb::ModuleSP module_sp =
        LoadModuleAtAddress(file, link_map, mod_base, mod_base_is_offset);

    if (module_sp.get())
      new_modules.Append(module_sp);
  }

  if (new_modules.GetSize() > 0) {
    ModuleList removed_modules;
    Target &target = GetTarget();
    ModuleList &loaded_modules = m_process->GetTarget().GetImages();

    for (size_t i = 0; i < loaded_modules.GetSize(); ++i) {
      const lldb::ModuleSP loaded_module = loaded_modules.GetModuleAtIndex(i);

      bool found = false;
      for (size_t j = 0; j < new_modules.GetSize(); ++j) {
        if (new_modules.GetModuleAtIndex(j).get() == loaded_module.get())
          found = true;
      }

      // The main executable will never be included in libraries-svr4, don't
      // remove it
      if (!found &&
          loaded_module.get() != target.GetExecutableModulePointer()) {
        removed_modules.Append(loaded_module);
      }
    }

    loaded_modules.Remove(removed_modules);
    m_process->GetTarget().ModulesDidUnload(removed_modules, false);

    new_modules.ForEach([&target](const lldb::ModuleSP module_sp) -> bool {
      lldb_private::ObjectFile *obj = module_sp->GetObjectFile();
      if (!obj)
        return true;

      if (obj->GetType() != ObjectFile::Type::eTypeExecutable)
        return true;

      lldb::ModuleSP module_copy_sp = module_sp;
      target.SetExecutableModule(module_copy_sp, eLoadDependentsNo);
      return false;
    });

    loaded_modules.AppendIfNeeded(new_modules);
    m_process->GetTarget().ModulesDidLoad(new_modules);
  }

  return new_modules.GetSize();
}

size_t ProcessGDBRemote::LoadModules() {
  LoadedModuleInfoList module_list;
  return LoadModules(module_list);
}

Status ProcessGDBRemote::GetFileLoadAddress(const FileSpec &file,
                                            bool &is_loaded,
                                            lldb::addr_t &load_addr) {
  is_loaded = false;
  load_addr = LLDB_INVALID_ADDRESS;

  std::string file_path = file.GetPath(false);
  if (file_path.empty())
    return Status("Empty file name specified");

  StreamString packet;
  packet.PutCString("qFileLoadAddress:");
  packet.PutCStringAsRawHex8(file_path.c_str());

  StringExtractorGDBRemote response;
  if (m_gdb_comm.SendPacketAndWaitForResponse(packet.GetString(), response,
                                              false) !=
      GDBRemoteCommunication::PacketResult::Success)
    return Status("Sending qFileLoadAddress packet failed");

  if (response.IsErrorResponse()) {
    if (response.GetError() == 1) {
      // The file is not loaded into the inferior
      is_loaded = false;
      load_addr = LLDB_INVALID_ADDRESS;
      return Status();
    }

    return Status(
        "Fetching file load address from remote server returned an error");
  }

  if (response.IsNormalResponse()) {
    is_loaded = true;
    load_addr = response.GetHexMaxU64(false, LLDB_INVALID_ADDRESS);
    return Status();
  }

  return Status(
      "Unknown error happened during sending the load address packet");
}

void ProcessGDBRemote::ModulesDidLoad(ModuleList &module_list) {
  // We must call the lldb_private::Process::ModulesDidLoad () first before we
  // do anything
  Process::ModulesDidLoad(module_list);

  // After loading shared libraries, we can ask our remote GDB server if it
  // needs any symbols.
  m_gdb_comm.ServeSymbolLookups(this);
}

void ProcessGDBRemote::HandleAsyncStdout(llvm::StringRef out) {
  AppendSTDOUT(out.data(), out.size());
}

static const char *end_delimiter = "--end--;";
static const int end_delimiter_len = 8;

void ProcessGDBRemote::HandleAsyncMisc(llvm::StringRef data) {
  std::string input = data.str(); // '1' to move beyond 'A'
  if (m_partial_profile_data.length() > 0) {
    m_partial_profile_data.append(input);
    input = m_partial_profile_data;
    m_partial_profile_data.clear();
  }

  size_t found, pos = 0, len = input.length();
  while ((found = input.find(end_delimiter, pos)) != std::string::npos) {
    StringExtractorGDBRemote profileDataExtractor(
        input.substr(pos, found).c_str());
    std::string profile_data =
        HarmonizeThreadIdsForProfileData(profileDataExtractor);
    BroadcastAsyncProfileData(profile_data);

    pos = found + end_delimiter_len;
  }

  if (pos < len) {
    // Last incomplete chunk.
    m_partial_profile_data = input.substr(pos);
  }
}

std::string ProcessGDBRemote::HarmonizeThreadIdsForProfileData(
    StringExtractorGDBRemote &profileDataExtractor) {
  std::map<uint64_t, uint32_t> new_thread_id_to_used_usec_map;
  std::string output;
  llvm::raw_string_ostream output_stream(output);
  llvm::StringRef name, value;

  // Going to assuming thread_used_usec comes first, else bail out.
  while (profileDataExtractor.GetNameColonValue(name, value)) {
    if (name.compare("thread_used_id") == 0) {
      StringExtractor threadIDHexExtractor(value);
      uint64_t thread_id = threadIDHexExtractor.GetHexMaxU64(false, 0);

      bool has_used_usec = false;
      uint32_t curr_used_usec = 0;
      llvm::StringRef usec_name, usec_value;
      uint32_t input_file_pos = profileDataExtractor.GetFilePos();
      if (profileDataExtractor.GetNameColonValue(usec_name, usec_value)) {
        if (usec_name.equals("thread_used_usec")) {
          has_used_usec = true;
          usec_value.getAsInteger(0, curr_used_usec);
        } else {
          // We didn't find what we want, it is probably an older version. Bail
          // out.
          profileDataExtractor.SetFilePos(input_file_pos);
        }
      }

      if (has_used_usec) {
        uint32_t prev_used_usec = 0;
        std::map<uint64_t, uint32_t>::iterator iterator =
            m_thread_id_to_used_usec_map.find(thread_id);
        if (iterator != m_thread_id_to_used_usec_map.end()) {
          prev_used_usec = m_thread_id_to_used_usec_map[thread_id];
        }

        uint32_t real_used_usec = curr_used_usec - prev_used_usec;
        // A good first time record is one that runs for at least 0.25 sec
        bool good_first_time =
            (prev_used_usec == 0) && (real_used_usec > 250000);
        bool good_subsequent_time =
            (prev_used_usec > 0) &&
            ((real_used_usec > 0) || (HasAssignedIndexIDToThread(thread_id)));

        if (good_first_time || good_subsequent_time) {
          // We try to avoid doing too many index id reservation, resulting in
          // fast increase of index ids.

          output_stream << name << ":";
          int32_t index_id = AssignIndexIDToThread(thread_id);
          output_stream << index_id << ";";

          output_stream << usec_name << ":" << usec_value << ";";
        } else {
          // Skip past 'thread_used_name'.
          llvm::StringRef local_name, local_value;
          profileDataExtractor.GetNameColonValue(local_name, local_value);
        }

        // Store current time as previous time so that they can be compared
        // later.
        new_thread_id_to_used_usec_map[thread_id] = curr_used_usec;
      } else {
        // Bail out and use old string.
        output_stream << name << ":" << value << ";";
      }
    } else {
      output_stream << name << ":" << value << ";";
    }
  }
  output_stream << end_delimiter;
  m_thread_id_to_used_usec_map = new_thread_id_to_used_usec_map;

  return output_stream.str();
}

void ProcessGDBRemote::HandleStopReply() {
  if (GetStopID() != 0)
    return;

  if (GetID() == LLDB_INVALID_PROCESS_ID) {
    lldb::pid_t pid = m_gdb_comm.GetCurrentProcessID();
    if (pid != LLDB_INVALID_PROCESS_ID)
      SetID(pid);
  }
  BuildDynamicRegisterInfo(true);
}

static const char *const s_async_json_packet_prefix = "JSON-async:";

static StructuredData::ObjectSP
ParseStructuredDataPacket(llvm::StringRef packet) {
  Log *log(ProcessGDBRemoteLog::GetLogIfAllCategoriesSet(GDBR_LOG_PROCESS));

  if (!packet.consume_front(s_async_json_packet_prefix)) {
    if (log) {
      log->Printf(
          "GDBRemoteCommunicationClientBase::%s() received $J packet "
          "but was not a StructuredData packet: packet starts with "
          "%s",
          __FUNCTION__,
          packet.slice(0, strlen(s_async_json_packet_prefix)).str().c_str());
    }
    return StructuredData::ObjectSP();
  }

  // This is an asynchronous JSON packet, destined for a StructuredDataPlugin.
  StructuredData::ObjectSP json_sp = StructuredData::ParseJSON(packet);
  if (log) {
    if (json_sp) {
      StreamString json_str;
      json_sp->Dump(json_str);
      json_str.Flush();
      log->Printf("ProcessGDBRemote::%s() "
                  "received Async StructuredData packet: %s",
                  __FUNCTION__, json_str.GetData());
    } else {
      log->Printf("ProcessGDBRemote::%s"
                  "() received StructuredData packet:"
                  " parse failure",
                  __FUNCTION__);
    }
  }
  return json_sp;
}

void ProcessGDBRemote::HandleAsyncStructuredDataPacket(llvm::StringRef data) {
  auto structured_data_sp = ParseStructuredDataPacket(data);
  if (structured_data_sp)
    RouteAsyncStructuredData(structured_data_sp);
}

class CommandObjectProcessGDBRemoteSpeedTest : public CommandObjectParsed {
public:
  CommandObjectProcessGDBRemoteSpeedTest(CommandInterpreter &interpreter)
      : CommandObjectParsed(interpreter, "process plugin packet speed-test",
                            "Tests packet speeds of various sizes to determine "
                            "the performance characteristics of the GDB remote "
                            "connection. ",
                            NULL),
        m_option_group(),
        m_num_packets(LLDB_OPT_SET_1, false, "count", 'c', 0, eArgTypeCount,
                      "The number of packets to send of each varying size "
                      "(default is 1000).",
                      1000),
        m_max_send(LLDB_OPT_SET_1, false, "max-send", 's', 0, eArgTypeCount,
                   "The maximum number of bytes to send in a packet. Sizes "
                   "increase in powers of 2 while the size is less than or "
                   "equal to this option value. (default 1024).",
                   1024),
        m_max_recv(LLDB_OPT_SET_1, false, "max-receive", 'r', 0, eArgTypeCount,
                   "The maximum number of bytes to receive in a packet. Sizes "
                   "increase in powers of 2 while the size is less than or "
                   "equal to this option value. (default 1024).",
                   1024),
        m_json(LLDB_OPT_SET_1, false, "json", 'j',
               "Print the output as JSON data for easy parsing.", false, true) {
    m_option_group.Append(&m_num_packets, LLDB_OPT_SET_ALL, LLDB_OPT_SET_1);
    m_option_group.Append(&m_max_send, LLDB_OPT_SET_ALL, LLDB_OPT_SET_1);
    m_option_group.Append(&m_max_recv, LLDB_OPT_SET_ALL, LLDB_OPT_SET_1);
    m_option_group.Append(&m_json, LLDB_OPT_SET_ALL, LLDB_OPT_SET_1);
    m_option_group.Finalize();
  }

  ~CommandObjectProcessGDBRemoteSpeedTest() {}

  Options *GetOptions() override { return &m_option_group; }

  bool DoExecute(Args &command, CommandReturnObject &result) override {
    const size_t argc = command.GetArgumentCount();
    if (argc == 0) {
      ProcessGDBRemote *process =
          (ProcessGDBRemote *)m_interpreter.GetExecutionContext()
              .GetProcessPtr();
      if (process) {
        StreamSP output_stream_sp(
            m_interpreter.GetDebugger().GetAsyncOutputStream());
        result.SetImmediateOutputStream(output_stream_sp);

        const uint32_t num_packets =
            (uint32_t)m_num_packets.GetOptionValue().GetCurrentValue();
        const uint64_t max_send = m_max_send.GetOptionValue().GetCurrentValue();
        const uint64_t max_recv = m_max_recv.GetOptionValue().GetCurrentValue();
        const bool json = m_json.GetOptionValue().GetCurrentValue();
        const uint64_t k_recv_amount =
            4 * 1024 * 1024; // Receive amount in bytes
        process->GetGDBRemote().TestPacketSpeed(
            num_packets, max_send, max_recv, k_recv_amount, json,
            output_stream_sp ? *output_stream_sp : result.GetOutputStream());
        result.SetStatus(eReturnStatusSuccessFinishResult);
        return true;
      }
    } else {
      result.AppendErrorWithFormat("'%s' takes no arguments",
                                   m_cmd_name.c_str());
    }
    result.SetStatus(eReturnStatusFailed);
    return false;
  }

protected:
  OptionGroupOptions m_option_group;
  OptionGroupUInt64 m_num_packets;
  OptionGroupUInt64 m_max_send;
  OptionGroupUInt64 m_max_recv;
  OptionGroupBoolean m_json;
};

class CommandObjectProcessGDBRemotePacketHistory : public CommandObjectParsed {
private:
public:
  CommandObjectProcessGDBRemotePacketHistory(CommandInterpreter &interpreter)
      : CommandObjectParsed(interpreter, "process plugin packet history",
                            "Dumps the packet history buffer. ", NULL) {}

  ~CommandObjectProcessGDBRemotePacketHistory() {}

  bool DoExecute(Args &command, CommandReturnObject &result) override {
    const size_t argc = command.GetArgumentCount();
    if (argc == 0) {
      ProcessGDBRemote *process =
          (ProcessGDBRemote *)m_interpreter.GetExecutionContext()
              .GetProcessPtr();
      if (process) {
        process->GetGDBRemote().DumpHistory(result.GetOutputStream());
        result.SetStatus(eReturnStatusSuccessFinishResult);
        return true;
      }
    } else {
      result.AppendErrorWithFormat("'%s' takes no arguments",
                                   m_cmd_name.c_str());
    }
    result.SetStatus(eReturnStatusFailed);
    return false;
  }
};

class CommandObjectProcessGDBRemotePacketXferSize : public CommandObjectParsed {
private:
public:
  CommandObjectProcessGDBRemotePacketXferSize(CommandInterpreter &interpreter)
      : CommandObjectParsed(
            interpreter, "process plugin packet xfer-size",
            "Maximum size that lldb will try to read/write one one chunk.",
            NULL) {}

  ~CommandObjectProcessGDBRemotePacketXferSize() {}

  bool DoExecute(Args &command, CommandReturnObject &result) override {
    const size_t argc = command.GetArgumentCount();
    if (argc == 0) {
      result.AppendErrorWithFormat("'%s' takes an argument to specify the max "
                                   "amount to be transferred when "
                                   "reading/writing",
                                   m_cmd_name.c_str());
      result.SetStatus(eReturnStatusFailed);
      return false;
    }

    ProcessGDBRemote *process =
        (ProcessGDBRemote *)m_interpreter.GetExecutionContext().GetProcessPtr();
    if (process) {
      const char *packet_size = command.GetArgumentAtIndex(0);
      errno = 0;
      uint64_t user_specified_max = strtoul(packet_size, NULL, 10);
      if (errno == 0 && user_specified_max != 0) {
        process->SetUserSpecifiedMaxMemoryTransferSize(user_specified_max);
        result.SetStatus(eReturnStatusSuccessFinishResult);
        return true;
      }
    }
    result.SetStatus(eReturnStatusFailed);
    return false;
  }
};

class CommandObjectProcessGDBRemotePacketSend : public CommandObjectParsed {
private:
public:
  CommandObjectProcessGDBRemotePacketSend(CommandInterpreter &interpreter)
      : CommandObjectParsed(interpreter, "process plugin packet send",
                            "Send a custom packet through the GDB remote "
                            "protocol and print the answer. "
                            "The packet header and footer will automatically "
                            "be added to the packet prior to sending and "
                            "stripped from the result.",
                            NULL) {}

  ~CommandObjectProcessGDBRemotePacketSend() {}

  bool DoExecute(Args &command, CommandReturnObject &result) override {
    const size_t argc = command.GetArgumentCount();
    if (argc == 0) {
      result.AppendErrorWithFormat(
          "'%s' takes a one or more packet content arguments",
          m_cmd_name.c_str());
      result.SetStatus(eReturnStatusFailed);
      return false;
    }

    ProcessGDBRemote *process =
        (ProcessGDBRemote *)m_interpreter.GetExecutionContext().GetProcessPtr();
    if (process) {
      for (size_t i = 0; i < argc; ++i) {
        const char *packet_cstr = command.GetArgumentAtIndex(0);
        bool send_async = true;
        StringExtractorGDBRemote response;
        process->GetGDBRemote().SendPacketAndWaitForResponse(
            packet_cstr, response, send_async);
        result.SetStatus(eReturnStatusSuccessFinishResult);
        Stream &output_strm = result.GetOutputStream();
        output_strm.Printf("  packet: %s\n", packet_cstr);
        std::string &response_str = response.GetStringRef();

        if (strstr(packet_cstr, "qGetProfileData") != NULL) {
          response_str = process->HarmonizeThreadIdsForProfileData(response);
        }

        if (response_str.empty())
          output_strm.PutCString("response: \nerror: UNIMPLEMENTED\n");
        else
          output_strm.Printf("response: %s\n", response.GetStringRef().c_str());
      }
    }
    return true;
  }
};

class CommandObjectProcessGDBRemotePacketMonitor : public CommandObjectRaw {
private:
public:
  CommandObjectProcessGDBRemotePacketMonitor(CommandInterpreter &interpreter)
      : CommandObjectRaw(interpreter, "process plugin packet monitor",
                         "Send a qRcmd packet through the GDB remote protocol "
                         "and print the response."
                         "The argument passed to this command will be hex "
                         "encoded into a valid 'qRcmd' packet, sent and the "
                         "response will be printed.") {}

  ~CommandObjectProcessGDBRemotePacketMonitor() {}

  bool DoExecute(llvm::StringRef command,
                 CommandReturnObject &result) override {
    if (command.empty()) {
      result.AppendErrorWithFormat("'%s' takes a command string argument",
                                   m_cmd_name.c_str());
      result.SetStatus(eReturnStatusFailed);
      return false;
    }

    ProcessGDBRemote *process =
        (ProcessGDBRemote *)m_interpreter.GetExecutionContext().GetProcessPtr();
    if (process) {
      StreamString packet;
      packet.PutCString("qRcmd,");
      packet.PutBytesAsRawHex8(command.data(), command.size());

      bool send_async = true;
      StringExtractorGDBRemote response;
      Stream &output_strm = result.GetOutputStream();
      process->GetGDBRemote().SendPacketAndReceiveResponseWithOutputSupport(
          packet.GetString(), response, send_async,
          [&output_strm](llvm::StringRef output) { output_strm << output; });
      result.SetStatus(eReturnStatusSuccessFinishResult);
      output_strm.Printf("  packet: %s\n", packet.GetData());
      const std::string &response_str = response.GetStringRef();

      if (response_str.empty())
        output_strm.PutCString("response: \nerror: UNIMPLEMENTED\n");
      else
        output_strm.Printf("response: %s\n", response.GetStringRef().c_str());
    }
    return true;
  }
};

class CommandObjectProcessGDBRemotePacket : public CommandObjectMultiword {
private:
public:
  CommandObjectProcessGDBRemotePacket(CommandInterpreter &interpreter)
      : CommandObjectMultiword(interpreter, "process plugin packet",
                               "Commands that deal with GDB remote packets.",
                               NULL) {
    LoadSubCommand(
        "history",
        CommandObjectSP(
            new CommandObjectProcessGDBRemotePacketHistory(interpreter)));
    LoadSubCommand(
        "send", CommandObjectSP(
                    new CommandObjectProcessGDBRemotePacketSend(interpreter)));
    LoadSubCommand(
        "monitor",
        CommandObjectSP(
            new CommandObjectProcessGDBRemotePacketMonitor(interpreter)));
    LoadSubCommand(
        "xfer-size",
        CommandObjectSP(
            new CommandObjectProcessGDBRemotePacketXferSize(interpreter)));
    LoadSubCommand("speed-test",
                   CommandObjectSP(new CommandObjectProcessGDBRemoteSpeedTest(
                       interpreter)));
  }

  ~CommandObjectProcessGDBRemotePacket() {}
};

class CommandObjectMultiwordProcessGDBRemote : public CommandObjectMultiword {
public:
  CommandObjectMultiwordProcessGDBRemote(CommandInterpreter &interpreter)
      : CommandObjectMultiword(
            interpreter, "process plugin",
            "Commands for operating on a ProcessGDBRemote process.",
            "process plugin <subcommand> [<subcommand-options>]") {
    LoadSubCommand(
        "packet",
        CommandObjectSP(new CommandObjectProcessGDBRemotePacket(interpreter)));
  }

  ~CommandObjectMultiwordProcessGDBRemote() {}
};

CommandObject *ProcessGDBRemote::GetPluginCommandObject() {
  if (!m_command_sp)
    m_command_sp.reset(new CommandObjectMultiwordProcessGDBRemote(
        GetTarget().GetDebugger().GetCommandInterpreter()));
  return m_command_sp.get();
}
