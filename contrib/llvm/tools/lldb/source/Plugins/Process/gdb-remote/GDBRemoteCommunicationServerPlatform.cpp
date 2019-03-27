//===-- GDBRemoteCommunicationServerPlatform.cpp ----------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "GDBRemoteCommunicationServerPlatform.h"

#include <errno.h>

#include <chrono>
#include <csignal>
#include <cstring>
#include <mutex>
#include <sstream>

#include "llvm/Support/FileSystem.h"
#include "llvm/Support/Threading.h"

#include "lldb/Host/Config.h"
#include "lldb/Host/ConnectionFileDescriptor.h"
#include "lldb/Host/Host.h"
#include "lldb/Host/HostInfo.h"
#include "lldb/Target/FileAction.h"
#include "lldb/Target/Platform.h"
#include "lldb/Target/Process.h"
#include "lldb/Target/UnixSignals.h"
#include "lldb/Utility/JSON.h"
#include "lldb/Utility/Log.h"
#include "lldb/Utility/StreamGDBRemote.h"
#include "lldb/Utility/StreamString.h"
#include "lldb/Utility/StructuredData.h"
#include "lldb/Utility/UriParser.h"

#include "lldb/Utility/StringExtractorGDBRemote.h"

using namespace lldb;
using namespace lldb_private;
using namespace lldb_private::process_gdb_remote;

//----------------------------------------------------------------------
// GDBRemoteCommunicationServerPlatform constructor
//----------------------------------------------------------------------
GDBRemoteCommunicationServerPlatform::GDBRemoteCommunicationServerPlatform(
    const Socket::SocketProtocol socket_protocol, const char *socket_scheme)
    : GDBRemoteCommunicationServerCommon("gdb-remote.server",
                                         "gdb-remote.server.rx_packet"),
      m_socket_protocol(socket_protocol), m_socket_scheme(socket_scheme),
      m_spawned_pids_mutex(), m_port_map(), m_port_offset(0) {
  m_pending_gdb_server.pid = LLDB_INVALID_PROCESS_ID;
  m_pending_gdb_server.port = 0;

  RegisterMemberFunctionHandler(
      StringExtractorGDBRemote::eServerPacketType_qC,
      &GDBRemoteCommunicationServerPlatform::Handle_qC);
  RegisterMemberFunctionHandler(
      StringExtractorGDBRemote::eServerPacketType_qGetWorkingDir,
      &GDBRemoteCommunicationServerPlatform::Handle_qGetWorkingDir);
  RegisterMemberFunctionHandler(
      StringExtractorGDBRemote::eServerPacketType_qLaunchGDBServer,
      &GDBRemoteCommunicationServerPlatform::Handle_qLaunchGDBServer);
  RegisterMemberFunctionHandler(
      StringExtractorGDBRemote::eServerPacketType_qQueryGDBServer,
      &GDBRemoteCommunicationServerPlatform::Handle_qQueryGDBServer);
  RegisterMemberFunctionHandler(
      StringExtractorGDBRemote::eServerPacketType_qKillSpawnedProcess,
      &GDBRemoteCommunicationServerPlatform::Handle_qKillSpawnedProcess);
  RegisterMemberFunctionHandler(
      StringExtractorGDBRemote::eServerPacketType_qProcessInfo,
      &GDBRemoteCommunicationServerPlatform::Handle_qProcessInfo);
  RegisterMemberFunctionHandler(
      StringExtractorGDBRemote::eServerPacketType_QSetWorkingDir,
      &GDBRemoteCommunicationServerPlatform::Handle_QSetWorkingDir);
  RegisterMemberFunctionHandler(
      StringExtractorGDBRemote::eServerPacketType_jSignalsInfo,
      &GDBRemoteCommunicationServerPlatform::Handle_jSignalsInfo);

  RegisterPacketHandler(StringExtractorGDBRemote::eServerPacketType_interrupt,
                        [](StringExtractorGDBRemote packet, Status &error,
                           bool &interrupt, bool &quit) {
                          error.SetErrorString("interrupt received");
                          interrupt = true;
                          return PacketResult::Success;
                        });
}

//----------------------------------------------------------------------
// Destructor
//----------------------------------------------------------------------
GDBRemoteCommunicationServerPlatform::~GDBRemoteCommunicationServerPlatform() {}

Status GDBRemoteCommunicationServerPlatform::LaunchGDBServer(
    const lldb_private::Args &args, std::string hostname, lldb::pid_t &pid,
    uint16_t &port, std::string &socket_name) {
  if (port == UINT16_MAX)
    port = GetNextAvailablePort();

  // Spawn a new thread to accept the port that gets bound after binding to
  // port 0 (zero).

  // ignore the hostname send from the remote end, just use the ip address that
  // we're currently communicating with as the hostname

  // Spawn a debugserver and try to get the port it listens to.
  ProcessLaunchInfo debugserver_launch_info;
  if (hostname.empty())
    hostname = "127.0.0.1";

  Log *log(GetLogIfAnyCategoriesSet(LIBLLDB_LOG_PLATFORM));
  if (log)
    log->Printf("Launching debugserver with: %s:%u...", hostname.c_str(), port);

  // Do not run in a new session so that it can not linger after the platform
  // closes.
  debugserver_launch_info.SetLaunchInSeparateProcessGroup(false);
  debugserver_launch_info.SetMonitorProcessCallback(
      std::bind(&GDBRemoteCommunicationServerPlatform::DebugserverProcessReaped,
                this, std::placeholders::_1),
      false);

  llvm::StringRef platform_scheme;
  llvm::StringRef platform_ip;
  int platform_port;
  llvm::StringRef platform_path;
  std::string platform_uri = GetConnection()->GetURI();
  bool ok = UriParser::Parse(platform_uri, platform_scheme, platform_ip,
                             platform_port, platform_path);
  UNUSED_IF_ASSERT_DISABLED(ok);
  assert(ok);

  std::ostringstream url;
// debugserver does not accept the URL scheme prefix.
#if !defined(__APPLE__)
  url << m_socket_scheme << "://";
#endif
  uint16_t *port_ptr = &port;
  if (m_socket_protocol == Socket::ProtocolTcp)
    url << platform_ip.str() << ":" << port;
  else {
    socket_name = GetDomainSocketPath("gdbserver").GetPath();
    url << socket_name;
    port_ptr = nullptr;
  }

  Status error = StartDebugserverProcess(
      url.str().c_str(), nullptr, debugserver_launch_info, port_ptr, &args, -1);

  pid = debugserver_launch_info.GetProcessID();
  if (pid != LLDB_INVALID_PROCESS_ID) {
    std::lock_guard<std::recursive_mutex> guard(m_spawned_pids_mutex);
    m_spawned_pids.insert(pid);
    if (port > 0)
      AssociatePortWithProcess(port, pid);
  } else {
    if (port > 0)
      FreePort(port);
  }
  return error;
}

GDBRemoteCommunication::PacketResult
GDBRemoteCommunicationServerPlatform::Handle_qLaunchGDBServer(
    StringExtractorGDBRemote &packet) {
  // Spawn a local debugserver as a platform so we can then attach or launch a
  // process...

  Log *log(GetLogIfAnyCategoriesSet(LIBLLDB_LOG_PLATFORM));
  if (log)
    log->Printf("GDBRemoteCommunicationServerPlatform::%s() called",
                __FUNCTION__);

  ConnectionFileDescriptor file_conn;
  std::string hostname;
  packet.SetFilePos(::strlen("qLaunchGDBServer;"));
  llvm::StringRef name;
  llvm::StringRef value;
  uint16_t port = UINT16_MAX;
  while (packet.GetNameColonValue(name, value)) {
    if (name.equals("host"))
      hostname = value;
    else if (name.equals("port"))
      value.getAsInteger(0, port);
  }

  lldb::pid_t debugserver_pid = LLDB_INVALID_PROCESS_ID;
  std::string socket_name;
  Status error =
      LaunchGDBServer(Args(), hostname, debugserver_pid, port, socket_name);
  if (error.Fail()) {
    if (log)
      log->Printf("GDBRemoteCommunicationServerPlatform::%s() debugserver "
                  "launch failed: %s",
                  __FUNCTION__, error.AsCString());
    return SendErrorResponse(9);
  }

  if (log)
    log->Printf("GDBRemoteCommunicationServerPlatform::%s() debugserver "
                "launched successfully as pid %" PRIu64,
                __FUNCTION__, debugserver_pid);

  StreamGDBRemote response;
  response.Printf("pid:%" PRIu64 ";port:%u;", debugserver_pid,
                  port + m_port_offset);
  if (!socket_name.empty()) {
    response.PutCString("socket_name:");
    response.PutCStringAsRawHex8(socket_name.c_str());
    response.PutChar(';');
  }

  PacketResult packet_result = SendPacketNoLock(response.GetString());
  if (packet_result != PacketResult::Success) {
    if (debugserver_pid != LLDB_INVALID_PROCESS_ID)
      Host::Kill(debugserver_pid, SIGINT);
  }
  return packet_result;
}

GDBRemoteCommunication::PacketResult
GDBRemoteCommunicationServerPlatform::Handle_qQueryGDBServer(
    StringExtractorGDBRemote &packet) {
  if (m_pending_gdb_server.pid == LLDB_INVALID_PROCESS_ID)
    return SendErrorResponse(4);

  JSONObject::SP server_sp = std::make_shared<JSONObject>();
  server_sp->SetObject("port",
                       std::make_shared<JSONNumber>(m_pending_gdb_server.port));
  if (!m_pending_gdb_server.socket_name.empty())
    server_sp->SetObject(
        "socket_name",
        std::make_shared<JSONString>(m_pending_gdb_server.socket_name.c_str()));

  JSONArray server_list;
  server_list.AppendObject(server_sp);

  StreamGDBRemote response;
  server_list.Write(response);

  StreamGDBRemote escaped_response;
  escaped_response.PutEscapedBytes(response.GetString().data(),
                                   response.GetSize());
  return SendPacketNoLock(escaped_response.GetString());
}

GDBRemoteCommunication::PacketResult
GDBRemoteCommunicationServerPlatform::Handle_qKillSpawnedProcess(
    StringExtractorGDBRemote &packet) {
  packet.SetFilePos(::strlen("qKillSpawnedProcess:"));

  lldb::pid_t pid = packet.GetU64(LLDB_INVALID_PROCESS_ID);

  // verify that we know anything about this pid. Scope for locker
  {
    std::lock_guard<std::recursive_mutex> guard(m_spawned_pids_mutex);
    if (m_spawned_pids.find(pid) == m_spawned_pids.end()) {
      // not a pid we know about
      return SendErrorResponse(10);
    }
  }

  // go ahead and attempt to kill the spawned process
  if (KillSpawnedProcess(pid))
    return SendOKResponse();
  else
    return SendErrorResponse(11);
}

bool GDBRemoteCommunicationServerPlatform::KillSpawnedProcess(lldb::pid_t pid) {
  // make sure we know about this process
  {
    std::lock_guard<std::recursive_mutex> guard(m_spawned_pids_mutex);
    if (m_spawned_pids.find(pid) == m_spawned_pids.end())
      return false;
  }

  // first try a SIGTERM (standard kill)
  Host::Kill(pid, SIGTERM);

  // check if that worked
  for (size_t i = 0; i < 10; ++i) {
    {
      std::lock_guard<std::recursive_mutex> guard(m_spawned_pids_mutex);
      if (m_spawned_pids.find(pid) == m_spawned_pids.end()) {
        // it is now killed
        return true;
      }
    }
    usleep(10000);
  }

  // check one more time after the final usleep
  {
    std::lock_guard<std::recursive_mutex> guard(m_spawned_pids_mutex);
    if (m_spawned_pids.find(pid) == m_spawned_pids.end())
      return true;
  }

  // the launched process still lives.  Now try killing it again, this time
  // with an unblockable signal.
  Host::Kill(pid, SIGKILL);

  for (size_t i = 0; i < 10; ++i) {
    {
      std::lock_guard<std::recursive_mutex> guard(m_spawned_pids_mutex);
      if (m_spawned_pids.find(pid) == m_spawned_pids.end()) {
        // it is now killed
        return true;
      }
    }
    usleep(10000);
  }

  // check one more time after the final usleep Scope for locker
  {
    std::lock_guard<std::recursive_mutex> guard(m_spawned_pids_mutex);
    if (m_spawned_pids.find(pid) == m_spawned_pids.end())
      return true;
  }

  // no luck - the process still lives
  return false;
}

GDBRemoteCommunication::PacketResult
GDBRemoteCommunicationServerPlatform::Handle_qProcessInfo(
    StringExtractorGDBRemote &packet) {
  lldb::pid_t pid = m_process_launch_info.GetProcessID();
  m_process_launch_info.Clear();

  if (pid == LLDB_INVALID_PROCESS_ID)
    return SendErrorResponse(1);

  ProcessInstanceInfo proc_info;
  if (!Host::GetProcessInfo(pid, proc_info))
    return SendErrorResponse(1);

  StreamString response;
  CreateProcessInfoResponse_DebugServerStyle(proc_info, response);
  return SendPacketNoLock(response.GetString());
}

GDBRemoteCommunication::PacketResult
GDBRemoteCommunicationServerPlatform::Handle_qGetWorkingDir(
    StringExtractorGDBRemote &packet) {

  llvm::SmallString<64> cwd;
  if (std::error_code ec = llvm::sys::fs::current_path(cwd))
    return SendErrorResponse(ec.value());

  StreamString response;
  response.PutBytesAsRawHex8(cwd.data(), cwd.size());
  return SendPacketNoLock(response.GetString());
}

GDBRemoteCommunication::PacketResult
GDBRemoteCommunicationServerPlatform::Handle_QSetWorkingDir(
    StringExtractorGDBRemote &packet) {
  packet.SetFilePos(::strlen("QSetWorkingDir:"));
  std::string path;
  packet.GetHexByteString(path);

  if (std::error_code ec = llvm::sys::fs::set_current_path(path))
    return SendErrorResponse(ec.value());
  return SendOKResponse();
}

GDBRemoteCommunication::PacketResult
GDBRemoteCommunicationServerPlatform::Handle_qC(
    StringExtractorGDBRemote &packet) {
  // NOTE: lldb should now be using qProcessInfo for process IDs.  This path
  // here
  // should not be used.  It is reporting process id instead of thread id.  The
  // correct answer doesn't seem to make much sense for lldb-platform.
  // CONSIDER: flip to "unsupported".
  lldb::pid_t pid = m_process_launch_info.GetProcessID();

  StreamString response;
  response.Printf("QC%" PRIx64, pid);

  // If we launch a process and this GDB server is acting as a platform, then
  // we need to clear the process launch state so we can start launching
  // another process. In order to launch a process a bunch or packets need to
  // be sent: environment packets, working directory, disable ASLR, and many
  // more settings. When we launch a process we then need to know when to clear
  // this information. Currently we are selecting the 'qC' packet as that
  // packet which seems to make the most sense.
  if (pid != LLDB_INVALID_PROCESS_ID) {
    m_process_launch_info.Clear();
  }

  return SendPacketNoLock(response.GetString());
}

GDBRemoteCommunication::PacketResult
GDBRemoteCommunicationServerPlatform::Handle_jSignalsInfo(
    StringExtractorGDBRemote &packet) {
  StructuredData::Array signal_array;

  const auto &signals = Host::GetUnixSignals();
  for (auto signo = signals->GetFirstSignalNumber();
       signo != LLDB_INVALID_SIGNAL_NUMBER;
       signo = signals->GetNextSignalNumber(signo)) {
    auto dictionary = std::make_shared<StructuredData::Dictionary>();

    dictionary->AddIntegerItem("signo", signo);
    dictionary->AddStringItem("name", signals->GetSignalAsCString(signo));

    bool suppress, stop, notify;
    signals->GetSignalInfo(signo, suppress, stop, notify);
    dictionary->AddBooleanItem("suppress", suppress);
    dictionary->AddBooleanItem("stop", stop);
    dictionary->AddBooleanItem("notify", notify);

    signal_array.Push(dictionary);
  }

  StreamString response;
  signal_array.Dump(response);
  return SendPacketNoLock(response.GetString());
}

bool GDBRemoteCommunicationServerPlatform::DebugserverProcessReaped(
    lldb::pid_t pid) {
  std::lock_guard<std::recursive_mutex> guard(m_spawned_pids_mutex);
  FreePortForProcess(pid);
  m_spawned_pids.erase(pid);
  return true;
}

Status GDBRemoteCommunicationServerPlatform::LaunchProcess() {
  if (!m_process_launch_info.GetArguments().GetArgumentCount())
    return Status("%s: no process command line specified to launch",
                  __FUNCTION__);

  // specify the process monitor if not already set.  This should generally be
  // what happens since we need to reap started processes.
  if (!m_process_launch_info.GetMonitorProcessCallback())
    m_process_launch_info.SetMonitorProcessCallback(
        std::bind(
            &GDBRemoteCommunicationServerPlatform::DebugserverProcessReaped,
            this, std::placeholders::_1),
        false);

  Status error = Host::LaunchProcess(m_process_launch_info);
  if (!error.Success()) {
    fprintf(stderr, "%s: failed to launch executable %s", __FUNCTION__,
            m_process_launch_info.GetArguments().GetArgumentAtIndex(0));
    return error;
  }

  printf("Launched '%s' as process %" PRIu64 "...\n",
         m_process_launch_info.GetArguments().GetArgumentAtIndex(0),
         m_process_launch_info.GetProcessID());

  // add to list of spawned processes.  On an lldb-gdbserver, we would expect
  // there to be only one.
  const auto pid = m_process_launch_info.GetProcessID();
  if (pid != LLDB_INVALID_PROCESS_ID) {
    // add to spawned pids
    std::lock_guard<std::recursive_mutex> guard(m_spawned_pids_mutex);
    m_spawned_pids.insert(pid);
  }

  return error;
}

void GDBRemoteCommunicationServerPlatform::SetPortMap(PortMap &&port_map) {
  m_port_map = port_map;
}

uint16_t GDBRemoteCommunicationServerPlatform::GetNextAvailablePort() {
  if (m_port_map.empty())
    return 0; // Bind to port zero and get a port, we didn't have any
              // limitations

  for (auto &pair : m_port_map) {
    if (pair.second == LLDB_INVALID_PROCESS_ID) {
      pair.second = ~(lldb::pid_t)LLDB_INVALID_PROCESS_ID;
      return pair.first;
    }
  }
  return UINT16_MAX;
}

bool GDBRemoteCommunicationServerPlatform::AssociatePortWithProcess(
    uint16_t port, lldb::pid_t pid) {
  PortMap::iterator pos = m_port_map.find(port);
  if (pos != m_port_map.end()) {
    pos->second = pid;
    return true;
  }
  return false;
}

bool GDBRemoteCommunicationServerPlatform::FreePort(uint16_t port) {
  PortMap::iterator pos = m_port_map.find(port);
  if (pos != m_port_map.end()) {
    pos->second = LLDB_INVALID_PROCESS_ID;
    return true;
  }
  return false;
}

bool GDBRemoteCommunicationServerPlatform::FreePortForProcess(lldb::pid_t pid) {
  if (!m_port_map.empty()) {
    for (auto &pair : m_port_map) {
      if (pair.second == pid) {
        pair.second = LLDB_INVALID_PROCESS_ID;
        return true;
      }
    }
  }
  return false;
}

const FileSpec &GDBRemoteCommunicationServerPlatform::GetDomainSocketDir() {
  static FileSpec g_domainsocket_dir;
  static llvm::once_flag g_once_flag;

  llvm::call_once(g_once_flag, []() {
    const char *domainsocket_dir_env =
        ::getenv("LLDB_DEBUGSERVER_DOMAINSOCKET_DIR");
    if (domainsocket_dir_env != nullptr)
      g_domainsocket_dir = FileSpec(domainsocket_dir_env);
    else
      g_domainsocket_dir = HostInfo::GetProcessTempDir();
  });

  return g_domainsocket_dir;
}

FileSpec
GDBRemoteCommunicationServerPlatform::GetDomainSocketPath(const char *prefix) {
  llvm::SmallString<128> socket_path;
  llvm::SmallString<128> socket_name(
      (llvm::StringRef(prefix) + ".%%%%%%").str());

  FileSpec socket_path_spec(GetDomainSocketDir());
  socket_path_spec.AppendPathComponent(socket_name.c_str());

  llvm::sys::fs::createUniqueFile(socket_path_spec.GetCString(), socket_path);
  return FileSpec(socket_path.c_str());
}

void GDBRemoteCommunicationServerPlatform::SetPortOffset(uint16_t port_offset) {
  m_port_offset = port_offset;
}

void GDBRemoteCommunicationServerPlatform::SetPendingGdbServer(
    lldb::pid_t pid, uint16_t port, const std::string &socket_name) {
  m_pending_gdb_server.pid = pid;
  m_pending_gdb_server.port = port;
  m_pending_gdb_server.socket_name = socket_name;
}
