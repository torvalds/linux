//===-- GDBRemoteCommunicationServerLLGS.h ----------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef liblldb_GDBRemoteCommunicationServerLLGS_h_
#define liblldb_GDBRemoteCommunicationServerLLGS_h_

#include <mutex>
#include <unordered_map>

#include "lldb/Core/Communication.h"
#include "lldb/Host/MainLoop.h"
#include "lldb/Host/common/NativeProcessProtocol.h"
#include "lldb/lldb-private-forward.h"

#include "GDBRemoteCommunicationServerCommon.h"

class StringExtractorGDBRemote;

namespace lldb_private {

namespace process_gdb_remote {

class ProcessGDBRemote;

class GDBRemoteCommunicationServerLLGS
    : public GDBRemoteCommunicationServerCommon,
      public NativeProcessProtocol::NativeDelegate {
public:
  //------------------------------------------------------------------
  // Constructors and Destructors
  //------------------------------------------------------------------
  GDBRemoteCommunicationServerLLGS(
      MainLoop &mainloop,
      const NativeProcessProtocol::Factory &process_factory);

  void SetLaunchInfo(const ProcessLaunchInfo &info);

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
  Status LaunchProcess() override;

  //------------------------------------------------------------------
  /// Attach to a process.
  ///
  /// This method supports attaching llgs to a process accessible via the
  /// configured Platform.
  ///
  /// @return
  ///     An Status object indicating the success or failure of the
  ///     attach operation.
  //------------------------------------------------------------------
  Status AttachToProcess(lldb::pid_t pid);

  //------------------------------------------------------------------
  // NativeProcessProtocol::NativeDelegate overrides
  //------------------------------------------------------------------
  void InitializeDelegate(NativeProcessProtocol *process) override;

  void ProcessStateChanged(NativeProcessProtocol *process,
                           lldb::StateType state) override;

  void DidExec(NativeProcessProtocol *process) override;

  Status InitializeConnection(std::unique_ptr<Connection> &&connection);

protected:
  MainLoop &m_mainloop;
  MainLoop::ReadHandleUP m_network_handle_up;
  const NativeProcessProtocol::Factory &m_process_factory;
  lldb::tid_t m_current_tid = LLDB_INVALID_THREAD_ID;
  lldb::tid_t m_continue_tid = LLDB_INVALID_THREAD_ID;
  std::recursive_mutex m_debugged_process_mutex;
  std::unique_ptr<NativeProcessProtocol> m_debugged_process_up;

  Communication m_stdio_communication;
  MainLoop::ReadHandleUP m_stdio_handle_up;

  lldb::StateType m_inferior_prev_state = lldb::StateType::eStateInvalid;
  std::unique_ptr<llvm::MemoryBuffer> m_active_auxv_buffer_up;
  std::mutex m_saved_registers_mutex;
  std::unordered_map<uint32_t, lldb::DataBufferSP> m_saved_registers_map;
  uint32_t m_next_saved_registers_id = 1;
  bool m_handshake_completed = false;

  PacketResult SendONotification(const char *buffer, uint32_t len);

  PacketResult SendWResponse(NativeProcessProtocol *process);

  PacketResult SendStopReplyPacketForThread(lldb::tid_t tid);

  PacketResult SendStopReasonForState(lldb::StateType process_state);

  PacketResult Handle_k(StringExtractorGDBRemote &packet);

  PacketResult Handle_qProcessInfo(StringExtractorGDBRemote &packet);

  PacketResult Handle_qC(StringExtractorGDBRemote &packet);

  PacketResult Handle_QSetDisableASLR(StringExtractorGDBRemote &packet);

  PacketResult Handle_QSetWorkingDir(StringExtractorGDBRemote &packet);

  PacketResult Handle_qGetWorkingDir(StringExtractorGDBRemote &packet);

  PacketResult Handle_C(StringExtractorGDBRemote &packet);

  PacketResult Handle_c(StringExtractorGDBRemote &packet);

  PacketResult Handle_vCont(StringExtractorGDBRemote &packet);

  PacketResult Handle_vCont_actions(StringExtractorGDBRemote &packet);

  PacketResult Handle_stop_reason(StringExtractorGDBRemote &packet);

  PacketResult Handle_qRegisterInfo(StringExtractorGDBRemote &packet);

  PacketResult Handle_qfThreadInfo(StringExtractorGDBRemote &packet);

  PacketResult Handle_qsThreadInfo(StringExtractorGDBRemote &packet);

  PacketResult Handle_p(StringExtractorGDBRemote &packet);

  PacketResult Handle_P(StringExtractorGDBRemote &packet);

  PacketResult Handle_H(StringExtractorGDBRemote &packet);

  PacketResult Handle_I(StringExtractorGDBRemote &packet);

  PacketResult Handle_interrupt(StringExtractorGDBRemote &packet);

  // Handles $m and $x packets.
  PacketResult Handle_memory_read(StringExtractorGDBRemote &packet);

  PacketResult Handle_M(StringExtractorGDBRemote &packet);

  PacketResult
  Handle_qMemoryRegionInfoSupported(StringExtractorGDBRemote &packet);

  PacketResult Handle_qMemoryRegionInfo(StringExtractorGDBRemote &packet);

  PacketResult Handle_Z(StringExtractorGDBRemote &packet);

  PacketResult Handle_z(StringExtractorGDBRemote &packet);

  PacketResult Handle_s(StringExtractorGDBRemote &packet);

  PacketResult Handle_qXfer_auxv_read(StringExtractorGDBRemote &packet);

  PacketResult Handle_QSaveRegisterState(StringExtractorGDBRemote &packet);

  PacketResult Handle_jTraceStart(StringExtractorGDBRemote &packet);

  PacketResult Handle_jTraceRead(StringExtractorGDBRemote &packet);

  PacketResult Handle_jTraceStop(StringExtractorGDBRemote &packet);

  PacketResult Handle_jTraceConfigRead(StringExtractorGDBRemote &packet);

  PacketResult Handle_QRestoreRegisterState(StringExtractorGDBRemote &packet);

  PacketResult Handle_vAttach(StringExtractorGDBRemote &packet);

  PacketResult Handle_D(StringExtractorGDBRemote &packet);

  PacketResult Handle_qThreadStopInfo(StringExtractorGDBRemote &packet);

  PacketResult Handle_jThreadsInfo(StringExtractorGDBRemote &packet);

  PacketResult Handle_qWatchpointSupportInfo(StringExtractorGDBRemote &packet);

  PacketResult Handle_qFileLoadAddress(StringExtractorGDBRemote &packet);

  PacketResult Handle_QPassSignals(StringExtractorGDBRemote &packet);

  void SetCurrentThreadID(lldb::tid_t tid);

  lldb::tid_t GetCurrentThreadID() const;

  void SetContinueThreadID(lldb::tid_t tid);

  lldb::tid_t GetContinueThreadID() const { return m_continue_tid; }

  Status SetSTDIOFileDescriptor(int fd);

  FileSpec FindModuleFile(const std::string &module_path,
                          const ArchSpec &arch) override;

private:
  void HandleInferiorState_Exited(NativeProcessProtocol *process);

  void HandleInferiorState_Stopped(NativeProcessProtocol *process);

  NativeThreadProtocol *GetThreadFromSuffix(StringExtractorGDBRemote &packet);

  uint32_t GetNextSavedRegistersID();

  void MaybeCloseInferiorTerminalConnection();

  void ClearProcessSpecificData();

  void RegisterPacketHandlers();

  void DataAvailableCallback();

  void SendProcessOutput();

  void StartSTDIOForwarding();

  void StopSTDIOForwarding();

  //------------------------------------------------------------------
  // For GDBRemoteCommunicationServerLLGS only
  //------------------------------------------------------------------
  DISALLOW_COPY_AND_ASSIGN(GDBRemoteCommunicationServerLLGS);
};

} // namespace process_gdb_remote
} // namespace lldb_private

#endif // liblldb_GDBRemoteCommunicationServerLLGS_h_
