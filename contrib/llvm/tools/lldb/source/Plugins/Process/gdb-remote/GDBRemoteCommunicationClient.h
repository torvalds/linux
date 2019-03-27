//===-- GDBRemoteCommunicationClient.h --------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef liblldb_GDBRemoteCommunicationClient_h_
#define liblldb_GDBRemoteCommunicationClient_h_

#include "GDBRemoteClientBase.h"

#include <chrono>
#include <map>
#include <mutex>
#include <string>
#include <vector>

#include "lldb/Target/Process.h"
#include "lldb/Utility/ArchSpec.h"
#include "lldb/Utility/StreamGDBRemote.h"
#include "lldb/Utility/StructuredData.h"

#include "llvm/ADT/Optional.h"

namespace lldb_private {
namespace process_gdb_remote {

class GDBRemoteCommunicationClient : public GDBRemoteClientBase {
public:
  GDBRemoteCommunicationClient();

  ~GDBRemoteCommunicationClient() override;

  //------------------------------------------------------------------
  // After connecting, send the handshake to the server to make sure
  // we are communicating with it.
  //------------------------------------------------------------------
  bool HandshakeWithServer(Status *error_ptr);

  // For packets which specify a range of output to be returned,
  // return all of the output via a series of request packets of the form
  // <prefix>0,<size>
  // <prefix><size>,<size>
  // <prefix><size>*2,<size>
  // <prefix><size>*3,<size>
  // ...
  // until a "$l..." packet is received, indicating the end.
  // (size is in hex; this format is used by a standard gdbserver to
  // return the given portion of the output specified by <prefix>;
  // for example, "qXfer:libraries-svr4:read::fff,1000" means
  // "return a chunk of the xml description file for shared
  // library load addresses, where the chunk starts at offset 0xfff
  // and continues for 0x1000 bytes").
  // Concatenate the resulting server response packets together and
  // return in response_string.  If any packet fails, the return value
  // indicates that failure and the returned string value is undefined.
  PacketResult
  SendPacketsAndConcatenateResponses(const char *send_payload_prefix,
                                     std::string &response_string);

  bool GetThreadSuffixSupported();

  // This packet is usually sent first and the boolean return value
  // indicates if the packet was send and any response was received
  // even in the response is UNIMPLEMENTED. If the packet failed to
  // get a response, then false is returned. This quickly tells us
  // if we were able to connect and communicate with the remote GDB
  // server
  bool QueryNoAckModeSupported();

  void GetListThreadsInStopReplySupported();

  lldb::pid_t GetCurrentProcessID(bool allow_lazy = true);

  bool GetLaunchSuccess(std::string &error_str);

  bool LaunchGDBServer(const char *remote_accept_hostname, lldb::pid_t &pid,
                       uint16_t &port, std::string &socket_name);

  size_t QueryGDBServer(
      std::vector<std::pair<uint16_t, std::string>> &connection_urls);

  bool KillSpawnedProcess(lldb::pid_t pid);

  //------------------------------------------------------------------
  /// Sends a GDB remote protocol 'A' packet that delivers program
  /// arguments to the remote server.
  ///
  /// @param[in] argv
  ///     A NULL terminated array of const C strings to use as the
  ///     arguments.
  ///
  /// @return
  ///     Zero if the response was "OK", a positive value if the
  ///     the response was "Exx" where xx are two hex digits, or
  ///     -1 if the call is unsupported or any other unexpected
  ///     response was received.
  //------------------------------------------------------------------
  int SendArgumentsPacket(const ProcessLaunchInfo &launch_info);

  //------------------------------------------------------------------
  /// Sends a "QEnvironment:NAME=VALUE" packet that will build up the
  /// environment that will get used when launching an application
  /// in conjunction with the 'A' packet. This function can be called
  /// multiple times in a row in order to pass on the desired
  /// environment that the inferior should be launched with.
  ///
  /// @param[in] name_equal_value
  ///     A NULL terminated C string that contains a single environment
  ///     in the format "NAME=VALUE".
  ///
  /// @return
  ///     Zero if the response was "OK", a positive value if the
  ///     the response was "Exx" where xx are two hex digits, or
  ///     -1 if the call is unsupported or any other unexpected
  ///     response was received.
  //------------------------------------------------------------------
  int SendEnvironmentPacket(char const *name_equal_value);
  int SendEnvironment(const Environment &env);

  int SendLaunchArchPacket(const char *arch);

  int SendLaunchEventDataPacket(const char *data,
                                bool *was_supported = nullptr);

  //------------------------------------------------------------------
  /// Sends a "vAttach:PID" where PID is in hex.
  ///
  /// @param[in] pid
  ///     A process ID for the remote gdb server to attach to.
  ///
  /// @param[out] response
  ///     The response received from the gdb server. If the return
  ///     value is zero, \a response will contain a stop reply
  ///     packet.
  ///
  /// @return
  ///     Zero if the attach was successful, or an error indicating
  ///     an error code.
  //------------------------------------------------------------------
  int SendAttach(lldb::pid_t pid, StringExtractorGDBRemote &response);

  //------------------------------------------------------------------
  /// Sends a GDB remote protocol 'I' packet that delivers stdin
  /// data to the remote process.
  ///
  /// @param[in] data
  ///     A pointer to stdin data.
  ///
  /// @param[in] data_len
  ///     The number of bytes available at \a data.
  ///
  /// @return
  ///     Zero if the attach was successful, or an error indicating
  ///     an error code.
  //------------------------------------------------------------------
  int SendStdinNotification(const char *data, size_t data_len);

  //------------------------------------------------------------------
  /// Sets the path to use for stdin/out/err for a process
  /// that will be launched with the 'A' packet.
  ///
  /// @param[in] path
  ///     The path to use for stdin/out/err
  ///
  /// @return
  ///     Zero if the for success, or an error code for failure.
  //------------------------------------------------------------------
  int SetSTDIN(const FileSpec &file_spec);
  int SetSTDOUT(const FileSpec &file_spec);
  int SetSTDERR(const FileSpec &file_spec);

  //------------------------------------------------------------------
  /// Sets the disable ASLR flag to \a enable for a process that will
  /// be launched with the 'A' packet.
  ///
  /// @param[in] enable
  ///     A boolean value indicating whether to disable ASLR or not.
  ///
  /// @return
  ///     Zero if the for success, or an error code for failure.
  //------------------------------------------------------------------
  int SetDisableASLR(bool enable);

  //------------------------------------------------------------------
  /// Sets the DetachOnError flag to \a enable for the process controlled by the
  /// stub.
  ///
  /// @param[in] enable
  ///     A boolean value indicating whether to detach on error or not.
  ///
  /// @return
  ///     Zero if the for success, or an error code for failure.
  //------------------------------------------------------------------
  int SetDetachOnError(bool enable);

  //------------------------------------------------------------------
  /// Sets the working directory to \a path for a process that will
  /// be launched with the 'A' packet for non platform based
  /// connections. If this packet is sent to a GDB server that
  /// implements the platform, it will change the current working
  /// directory for the platform process.
  ///
  /// @param[in] working_dir
  ///     The path to a directory to use when launching our process
  ///
  /// @return
  ///     Zero if the for success, or an error code for failure.
  //------------------------------------------------------------------
  int SetWorkingDir(const FileSpec &working_dir);

  //------------------------------------------------------------------
  /// Gets the current working directory of a remote platform GDB
  /// server.
  ///
  /// @param[out] working_dir
  ///     The current working directory on the remote platform.
  ///
  /// @return
  ///     Boolean for success
  //------------------------------------------------------------------
  bool GetWorkingDir(FileSpec &working_dir);

  lldb::addr_t AllocateMemory(size_t size, uint32_t permissions);

  bool DeallocateMemory(lldb::addr_t addr);

  Status Detach(bool keep_stopped);

  Status GetMemoryRegionInfo(lldb::addr_t addr, MemoryRegionInfo &range_info);

  Status GetWatchpointSupportInfo(uint32_t &num);

  Status GetWatchpointSupportInfo(uint32_t &num, bool &after,
                                  const ArchSpec &arch);

  Status GetWatchpointsTriggerAfterInstruction(bool &after,
                                               const ArchSpec &arch);

  const ArchSpec &GetHostArchitecture();

  std::chrono::seconds GetHostDefaultPacketTimeout();

  const ArchSpec &GetProcessArchitecture();

  void GetRemoteQSupported();

  bool GetVContSupported(char flavor);

  bool GetpPacketSupported(lldb::tid_t tid);

  bool GetxPacketSupported();

  bool GetVAttachOrWaitSupported();

  bool GetSyncThreadStateSupported();

  void ResetDiscoverableSettings(bool did_exec);

  bool GetHostInfo(bool force = false);

  bool GetDefaultThreadId(lldb::tid_t &tid);

  llvm::VersionTuple GetOSVersion();

  bool GetOSBuildString(std::string &s);

  bool GetOSKernelDescription(std::string &s);

  ArchSpec GetSystemArchitecture();

  bool GetHostname(std::string &s);

  lldb::addr_t GetShlibInfoAddr();

  bool GetSupportsThreadSuffix();

  bool GetProcessInfo(lldb::pid_t pid, ProcessInstanceInfo &process_info);

  uint32_t FindProcesses(const ProcessInstanceInfoMatch &process_match_info,
                         ProcessInstanceInfoList &process_infos);

  bool GetUserName(uint32_t uid, std::string &name);

  bool GetGroupName(uint32_t gid, std::string &name);

  bool HasFullVContSupport() { return GetVContSupported('A'); }

  bool HasAnyVContSupport() { return GetVContSupported('a'); }

  bool GetStopReply(StringExtractorGDBRemote &response);

  bool GetThreadStopInfo(lldb::tid_t tid, StringExtractorGDBRemote &response);

  bool SupportsGDBStoppointPacket(GDBStoppointType type) {
    switch (type) {
    case eBreakpointSoftware:
      return m_supports_z0;
    case eBreakpointHardware:
      return m_supports_z1;
    case eWatchpointWrite:
      return m_supports_z2;
    case eWatchpointRead:
      return m_supports_z3;
    case eWatchpointReadWrite:
      return m_supports_z4;
    default:
      return false;
    }
  }

  uint8_t SendGDBStoppointTypePacket(
      GDBStoppointType type, // Type of breakpoint or watchpoint
      bool insert,           // Insert or remove?
      lldb::addr_t addr,     // Address of breakpoint or watchpoint
      uint32_t length);      // Byte Size of breakpoint or watchpoint

  bool SetNonStopMode(const bool enable);

  void TestPacketSpeed(const uint32_t num_packets, uint32_t max_send,
                       uint32_t max_recv, uint64_t recv_amount, bool json,
                       Stream &strm);

  // This packet is for testing the speed of the interface only. Both
  // the client and server need to support it, but this allows us to
  // measure the packet speed without any other work being done on the
  // other end and avoids any of that work affecting the packet send
  // and response times.
  bool SendSpeedTestPacket(uint32_t send_size, uint32_t recv_size);

  bool SetCurrentThread(uint64_t tid);

  bool SetCurrentThreadForRun(uint64_t tid);

  bool GetQXferAuxvReadSupported();

  void EnableErrorStringInPacket();

  bool GetQXferLibrariesReadSupported();

  bool GetQXferLibrariesSVR4ReadSupported();

  uint64_t GetRemoteMaxPacketSize();

  bool GetEchoSupported();

  bool GetQPassSignalsSupported();

  bool GetAugmentedLibrariesSVR4ReadSupported();

  bool GetQXferFeaturesReadSupported();

  bool GetQXferMemoryMapReadSupported();

  LazyBool SupportsAllocDeallocMemory() // const
  {
    // Uncomment this to have lldb pretend the debug server doesn't respond to
    // alloc/dealloc memory packets.
    // m_supports_alloc_dealloc_memory = lldb_private::eLazyBoolNo;
    return m_supports_alloc_dealloc_memory;
  }

  size_t GetCurrentThreadIDs(std::vector<lldb::tid_t> &thread_ids,
                             bool &sequence_mutex_unavailable);

  lldb::user_id_t OpenFile(const FileSpec &file_spec, uint32_t flags,
                           mode_t mode, Status &error);

  bool CloseFile(lldb::user_id_t fd, Status &error);

  lldb::user_id_t GetFileSize(const FileSpec &file_spec);

  Status GetFilePermissions(const FileSpec &file_spec,
                            uint32_t &file_permissions);

  Status SetFilePermissions(const FileSpec &file_spec,
                            uint32_t file_permissions);

  uint64_t ReadFile(lldb::user_id_t fd, uint64_t offset, void *dst,
                    uint64_t dst_len, Status &error);

  uint64_t WriteFile(lldb::user_id_t fd, uint64_t offset, const void *src,
                     uint64_t src_len, Status &error);

  Status CreateSymlink(const FileSpec &src, const FileSpec &dst);

  Status Unlink(const FileSpec &file_spec);

  Status MakeDirectory(const FileSpec &file_spec, uint32_t mode);

  bool GetFileExists(const FileSpec &file_spec);

  Status RunShellCommand(
      const char *command,         // Shouldn't be nullptr
      const FileSpec &working_dir, // Pass empty FileSpec to use the current
                                   // working directory
      int *status_ptr, // Pass nullptr if you don't want the process exit status
      int *signo_ptr,  // Pass nullptr if you don't want the signal that caused
                       // the process to exit
      std::string
          *command_output, // Pass nullptr if you don't want the command output
      const Timeout<std::micro> &timeout);

  bool CalculateMD5(const FileSpec &file_spec, uint64_t &high, uint64_t &low);

  lldb::DataBufferSP ReadRegister(
      lldb::tid_t tid,
      uint32_t
          reg_num); // Must be the eRegisterKindProcessPlugin register number

  lldb::DataBufferSP ReadAllRegisters(lldb::tid_t tid);

  bool
  WriteRegister(lldb::tid_t tid,
                uint32_t reg_num, // eRegisterKindProcessPlugin register number
                llvm::ArrayRef<uint8_t> data);

  bool WriteAllRegisters(lldb::tid_t tid, llvm::ArrayRef<uint8_t> data);

  bool SaveRegisterState(lldb::tid_t tid, uint32_t &save_id);

  bool RestoreRegisterState(lldb::tid_t tid, uint32_t save_id);

  bool SyncThreadState(lldb::tid_t tid);

  const char *GetGDBServerProgramName();

  uint32_t GetGDBServerProgramVersion();

  bool AvoidGPackets(ProcessGDBRemote *process);

  StructuredData::ObjectSP GetThreadsInfo();

  bool GetThreadExtendedInfoSupported();

  bool GetLoadedDynamicLibrariesInfosSupported();

  bool GetSharedCacheInfoSupported();

  bool GetModuleInfo(const FileSpec &module_file_spec,
                     const ArchSpec &arch_spec, ModuleSpec &module_spec);

  llvm::Optional<std::vector<ModuleSpec>>
  GetModulesInfo(llvm::ArrayRef<FileSpec> module_file_specs,
                 const llvm::Triple &triple);

  bool ReadExtFeature(const lldb_private::ConstString object,
                      const lldb_private::ConstString annex, std::string &out,
                      lldb_private::Status &err);

  void ServeSymbolLookups(lldb_private::Process *process);

  // Sends QPassSignals packet to the server with given signals to ignore.
  Status SendSignalsToIgnore(llvm::ArrayRef<int32_t> signals);

  //------------------------------------------------------------------
  /// Return the feature set supported by the gdb-remote server.
  ///
  /// This method returns the remote side's response to the qSupported
  /// packet.  The response is the complete string payload returned
  /// to the client.
  ///
  /// @return
  ///     The string returned by the server to the qSupported query.
  //------------------------------------------------------------------
  const std::string &GetServerSupportedFeatures() const {
    return m_qSupported_response;
  }

  //------------------------------------------------------------------
  /// Return the array of async JSON packet types supported by the remote.
  ///
  /// This method returns the remote side's array of supported JSON
  /// packet types as a list of type names.  Each of the results are
  /// expected to have an Enable{type_name} command to enable and configure
  /// the related feature.  Each type_name for an enabled feature will
  /// possibly send async-style packets that contain a payload of a
  /// binhex-encoded JSON dictionary.  The dictionary will have a
  /// string field named 'type', that contains the type_name of the
  /// supported packet type.
  ///
  /// There is a Plugin category called structured-data plugins.
  /// A plugin indicates whether it knows how to handle a type_name.
  /// If so, it can be used to process the async JSON packet.
  ///
  /// @return
  ///     The string returned by the server to the qSupported query.
  //------------------------------------------------------------------
  lldb_private::StructuredData::Array *GetSupportedStructuredDataPlugins();

  //------------------------------------------------------------------
  /// Configure a StructuredData feature on the remote end.
  ///
  /// @see \b Process::ConfigureStructuredData(...) for details.
  //------------------------------------------------------------------
  Status
  ConfigureRemoteStructuredData(const ConstString &type_name,
                                const StructuredData::ObjectSP &config_sp);

  lldb::user_id_t SendStartTracePacket(const TraceOptions &options,
                                       Status &error);

  Status SendStopTracePacket(lldb::user_id_t uid, lldb::tid_t thread_id);

  Status SendGetDataPacket(lldb::user_id_t uid, lldb::tid_t thread_id,
                           llvm::MutableArrayRef<uint8_t> &buffer,
                           size_t offset = 0);

  Status SendGetMetaDataPacket(lldb::user_id_t uid, lldb::tid_t thread_id,
                               llvm::MutableArrayRef<uint8_t> &buffer,
                               size_t offset = 0);

  Status SendGetTraceConfigPacket(lldb::user_id_t uid, TraceOptions &options);

protected:
  LazyBool m_supports_not_sending_acks;
  LazyBool m_supports_thread_suffix;
  LazyBool m_supports_threads_in_stop_reply;
  LazyBool m_supports_vCont_all;
  LazyBool m_supports_vCont_any;
  LazyBool m_supports_vCont_c;
  LazyBool m_supports_vCont_C;
  LazyBool m_supports_vCont_s;
  LazyBool m_supports_vCont_S;
  LazyBool m_qHostInfo_is_valid;
  LazyBool m_curr_pid_is_valid;
  LazyBool m_qProcessInfo_is_valid;
  LazyBool m_qGDBServerVersion_is_valid;
  LazyBool m_supports_alloc_dealloc_memory;
  LazyBool m_supports_memory_region_info;
  LazyBool m_supports_watchpoint_support_info;
  LazyBool m_supports_detach_stay_stopped;
  LazyBool m_watchpoints_trigger_after_instruction;
  LazyBool m_attach_or_wait_reply;
  LazyBool m_prepare_for_reg_writing_reply;
  LazyBool m_supports_p;
  LazyBool m_supports_x;
  LazyBool m_avoid_g_packets;
  LazyBool m_supports_QSaveRegisterState;
  LazyBool m_supports_qXfer_auxv_read;
  LazyBool m_supports_qXfer_libraries_read;
  LazyBool m_supports_qXfer_libraries_svr4_read;
  LazyBool m_supports_qXfer_features_read;
  LazyBool m_supports_qXfer_memory_map_read;
  LazyBool m_supports_augmented_libraries_svr4_read;
  LazyBool m_supports_jThreadExtendedInfo;
  LazyBool m_supports_jLoadedDynamicLibrariesInfos;
  LazyBool m_supports_jGetSharedCacheInfo;
  LazyBool m_supports_QPassSignals;
  LazyBool m_supports_error_string_reply;

  bool m_supports_qProcessInfoPID : 1, m_supports_qfProcessInfo : 1,
      m_supports_qUserName : 1, m_supports_qGroupName : 1,
      m_supports_qThreadStopInfo : 1, m_supports_z0 : 1, m_supports_z1 : 1,
      m_supports_z2 : 1, m_supports_z3 : 1, m_supports_z4 : 1,
      m_supports_QEnvironment : 1, m_supports_QEnvironmentHexEncoded : 1,
      m_supports_qSymbol : 1, m_qSymbol_requests_done : 1,
      m_supports_qModuleInfo : 1, m_supports_jThreadsInfo : 1,
      m_supports_jModulesInfo : 1;

  lldb::pid_t m_curr_pid;
  lldb::tid_t m_curr_tid; // Current gdb remote protocol thread index for all
                          // other operations
  lldb::tid_t m_curr_tid_run; // Current gdb remote protocol thread index for
                              // continue, step, etc

  uint32_t m_num_supported_hardware_watchpoints;

  ArchSpec m_host_arch;
  ArchSpec m_process_arch;
  llvm::VersionTuple m_os_version;
  std::string m_os_build;
  std::string m_os_kernel;
  std::string m_hostname;
  std::string m_gdb_server_name; // from reply to qGDBServerVersion, empty if
                                 // qGDBServerVersion is not supported
  uint32_t m_gdb_server_version; // from reply to qGDBServerVersion, zero if
                                 // qGDBServerVersion is not supported
  std::chrono::seconds m_default_packet_timeout;
  uint64_t m_max_packet_size;        // as returned by qSupported
  std::string m_qSupported_response; // the complete response to qSupported

  bool m_supported_async_json_packets_is_valid;
  lldb_private::StructuredData::ObjectSP m_supported_async_json_packets_sp;

  std::vector<MemoryRegionInfo> m_qXfer_memory_map;
  bool m_qXfer_memory_map_loaded;

  bool GetCurrentProcessInfo(bool allow_lazy_pid = true);

  bool GetGDBServerVersion();

  // Given the list of compression types that the remote debug stub can support,
  // possibly enable compression if we find an encoding we can handle.
  void MaybeEnableCompression(std::vector<std::string> supported_compressions);

  bool DecodeProcessInfoResponse(StringExtractorGDBRemote &response,
                                 ProcessInstanceInfo &process_info);

  void OnRunPacketSent(bool first) override;

  PacketResult SendThreadSpecificPacketAndWaitForResponse(
      lldb::tid_t tid, StreamString &&payload,
      StringExtractorGDBRemote &response, bool send_async);

  Status SendGetTraceDataPacket(StreamGDBRemote &packet, lldb::user_id_t uid,
                                lldb::tid_t thread_id,
                                llvm::MutableArrayRef<uint8_t> &buffer,
                                size_t offset);

  Status LoadQXferMemoryMap();

  Status GetQXferMemoryMapRegionInfo(lldb::addr_t addr,
                                     MemoryRegionInfo &region);

private:
  DISALLOW_COPY_AND_ASSIGN(GDBRemoteCommunicationClient);
};

} // namespace process_gdb_remote
} // namespace lldb_private

#endif // liblldb_GDBRemoteCommunicationClient_h_
