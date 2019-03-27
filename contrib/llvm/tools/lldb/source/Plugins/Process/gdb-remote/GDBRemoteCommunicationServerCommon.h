//===-- GDBRemoteCommunicationServerCommon.h --------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef liblldb_GDBRemoteCommunicationServerCommon_h_
#define liblldb_GDBRemoteCommunicationServerCommon_h_

#include <string>

#include "lldb/Target/Process.h"
#include "lldb/lldb-private-forward.h"

#include "GDBRemoteCommunicationServer.h"
#include "GDBRemoteCommunicationServerCommon.h"

class StringExtractorGDBRemote;

namespace lldb_private {
namespace process_gdb_remote {

class ProcessGDBRemote;

class GDBRemoteCommunicationServerCommon : public GDBRemoteCommunicationServer {
public:
  GDBRemoteCommunicationServerCommon(const char *comm_name,
                                     const char *listener_name);

  ~GDBRemoteCommunicationServerCommon() override;

protected:
  ProcessLaunchInfo m_process_launch_info;
  Status m_process_launch_error;
  ProcessInstanceInfoList m_proc_infos;
  uint32_t m_proc_infos_index;
  bool m_thread_suffix_supported;
  bool m_list_threads_in_stop_reply;

  PacketResult Handle_A(StringExtractorGDBRemote &packet);

  PacketResult Handle_qHostInfo(StringExtractorGDBRemote &packet);

  PacketResult Handle_qProcessInfoPID(StringExtractorGDBRemote &packet);

  PacketResult Handle_qfProcessInfo(StringExtractorGDBRemote &packet);

  PacketResult Handle_qsProcessInfo(StringExtractorGDBRemote &packet);

  PacketResult Handle_qUserName(StringExtractorGDBRemote &packet);

  PacketResult Handle_qGroupName(StringExtractorGDBRemote &packet);

  PacketResult Handle_qSpeedTest(StringExtractorGDBRemote &packet);

  PacketResult Handle_vFile_Open(StringExtractorGDBRemote &packet);

  PacketResult Handle_vFile_Close(StringExtractorGDBRemote &packet);

  PacketResult Handle_vFile_pRead(StringExtractorGDBRemote &packet);

  PacketResult Handle_vFile_pWrite(StringExtractorGDBRemote &packet);

  PacketResult Handle_vFile_Size(StringExtractorGDBRemote &packet);

  PacketResult Handle_vFile_Mode(StringExtractorGDBRemote &packet);

  PacketResult Handle_vFile_Exists(StringExtractorGDBRemote &packet);

  PacketResult Handle_vFile_symlink(StringExtractorGDBRemote &packet);

  PacketResult Handle_vFile_unlink(StringExtractorGDBRemote &packet);

  PacketResult Handle_vFile_Stat(StringExtractorGDBRemote &packet);

  PacketResult Handle_vFile_MD5(StringExtractorGDBRemote &packet);

  PacketResult Handle_qEcho(StringExtractorGDBRemote &packet);

  PacketResult Handle_qModuleInfo(StringExtractorGDBRemote &packet);

  PacketResult Handle_jModulesInfo(StringExtractorGDBRemote &packet);

  PacketResult Handle_qPlatform_shell(StringExtractorGDBRemote &packet);

  PacketResult Handle_qPlatform_mkdir(StringExtractorGDBRemote &packet);

  PacketResult Handle_qPlatform_chmod(StringExtractorGDBRemote &packet);

  PacketResult Handle_qSupported(StringExtractorGDBRemote &packet);

  PacketResult Handle_QThreadSuffixSupported(StringExtractorGDBRemote &packet);

  PacketResult Handle_QListThreadsInStopReply(StringExtractorGDBRemote &packet);

  PacketResult Handle_QSetDetachOnError(StringExtractorGDBRemote &packet);

  PacketResult Handle_QStartNoAckMode(StringExtractorGDBRemote &packet);

  PacketResult Handle_QSetSTDIN(StringExtractorGDBRemote &packet);

  PacketResult Handle_QSetSTDOUT(StringExtractorGDBRemote &packet);

  PacketResult Handle_QSetSTDERR(StringExtractorGDBRemote &packet);

  PacketResult Handle_qLaunchSuccess(StringExtractorGDBRemote &packet);

  PacketResult Handle_QEnvironment(StringExtractorGDBRemote &packet);

  PacketResult Handle_QEnvironmentHexEncoded(StringExtractorGDBRemote &packet);

  PacketResult Handle_QLaunchArch(StringExtractorGDBRemote &packet);

  static void CreateProcessInfoResponse(const ProcessInstanceInfo &proc_info,
                                        StreamString &response);

  static void CreateProcessInfoResponse_DebugServerStyle(
      const ProcessInstanceInfo &proc_info, StreamString &response);

  template <typename T>
  void RegisterMemberFunctionHandler(
      StringExtractorGDBRemote::ServerPacketType packet_type,
      PacketResult (T::*handler)(StringExtractorGDBRemote &packet)) {
    RegisterPacketHandler(packet_type,
                          [this, handler](StringExtractorGDBRemote packet,
                                          Status &error, bool &interrupt,
                                          bool &quit) {
                            return (static_cast<T *>(this)->*handler)(packet);
                          });
  }

  //------------------------------------------------------------------
  /// Launch a process with the current launch settings.
  ///
  /// This method supports running an lldb-gdbserver or similar
  /// server in a situation where the startup code has been provided
  /// with all the information for a child process to be launched.
  ///
  /// @return
  ///     An Status object indicating the success or failure of the
  ///     launch.
  //------------------------------------------------------------------
  virtual Status LaunchProcess() = 0;

  virtual FileSpec FindModuleFile(const std::string &module_path,
                                  const ArchSpec &arch);

private:
  ModuleSpec GetModuleInfo(llvm::StringRef module_path, llvm::StringRef triple);
};

} // namespace process_gdb_remote
} // namespace lldb_private

#endif // liblldb_GDBRemoteCommunicationServerCommon_h_
