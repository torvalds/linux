//===-- SBProcess.cpp -------------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "lldb/API/SBProcess.h"

#include <inttypes.h>

#include "lldb/lldb-defines.h"
#include "lldb/lldb-types.h"

#include "lldb/Core/Debugger.h"
#include "lldb/Core/Module.h"
#include "lldb/Core/PluginManager.h"
#include "lldb/Core/StreamFile.h"
#include "lldb/Target/MemoryRegionInfo.h"
#include "lldb/Target/Process.h"
#include "lldb/Target/RegisterContext.h"
#include "lldb/Target/SystemRuntime.h"
#include "lldb/Target/Target.h"
#include "lldb/Target/Thread.h"
#include "lldb/Utility/Args.h"
#include "lldb/Utility/Log.h"
#include "lldb/Utility/State.h"
#include "lldb/Utility/Stream.h"


#include "lldb/API/SBBroadcaster.h"
#include "lldb/API/SBCommandReturnObject.h"
#include "lldb/API/SBDebugger.h"
#include "lldb/API/SBEvent.h"
#include "lldb/API/SBFileSpec.h"
#include "lldb/API/SBMemoryRegionInfo.h"
#include "lldb/API/SBMemoryRegionInfoList.h"
#include "lldb/API/SBStream.h"
#include "lldb/API/SBStringList.h"
#include "lldb/API/SBStructuredData.h"
#include "lldb/API/SBThread.h"
#include "lldb/API/SBThreadCollection.h"
#include "lldb/API/SBTrace.h"
#include "lldb/API/SBTraceOptions.h"
#include "lldb/API/SBUnixSignals.h"

using namespace lldb;
using namespace lldb_private;

SBProcess::SBProcess() : m_opaque_wp() {}

//----------------------------------------------------------------------
// SBProcess constructor
//----------------------------------------------------------------------

SBProcess::SBProcess(const SBProcess &rhs) : m_opaque_wp(rhs.m_opaque_wp) {}

SBProcess::SBProcess(const lldb::ProcessSP &process_sp)
    : m_opaque_wp(process_sp) {}

const SBProcess &SBProcess::operator=(const SBProcess &rhs) {
  if (this != &rhs)
    m_opaque_wp = rhs.m_opaque_wp;
  return *this;
}

//----------------------------------------------------------------------
// Destructor
//----------------------------------------------------------------------
SBProcess::~SBProcess() {}

const char *SBProcess::GetBroadcasterClassName() {
  return Process::GetStaticBroadcasterClass().AsCString();
}

const char *SBProcess::GetPluginName() {
  ProcessSP process_sp(GetSP());
  if (process_sp) {
    return process_sp->GetPluginName().GetCString();
  }
  return "<Unknown>";
}

const char *SBProcess::GetShortPluginName() {
  ProcessSP process_sp(GetSP());
  if (process_sp) {
    return process_sp->GetPluginName().GetCString();
  }
  return "<Unknown>";
}

lldb::ProcessSP SBProcess::GetSP() const { return m_opaque_wp.lock(); }

void SBProcess::SetSP(const ProcessSP &process_sp) { m_opaque_wp = process_sp; }

void SBProcess::Clear() { m_opaque_wp.reset(); }

bool SBProcess::IsValid() const {
  ProcessSP process_sp(m_opaque_wp.lock());
  return ((bool)process_sp && process_sp->IsValid());
}

bool SBProcess::RemoteLaunch(char const **argv, char const **envp,
                             const char *stdin_path, const char *stdout_path,
                             const char *stderr_path,
                             const char *working_directory,
                             uint32_t launch_flags, bool stop_at_entry,
                             lldb::SBError &error) {
  Log *log(lldb_private::GetLogIfAllCategoriesSet(LIBLLDB_LOG_API));
  if (log)
    log->Printf("SBProcess(%p)::RemoteLaunch (argv=%p, envp=%p, stdin=%s, "
                "stdout=%s, stderr=%s, working-dir=%s, launch_flags=0x%x, "
                "stop_at_entry=%i, &error (%p))...",
                static_cast<void *>(m_opaque_wp.lock().get()),
                static_cast<void *>(argv), static_cast<void *>(envp),
                stdin_path ? stdin_path : "NULL",
                stdout_path ? stdout_path : "NULL",
                stderr_path ? stderr_path : "NULL",
                working_directory ? working_directory : "NULL", launch_flags,
                stop_at_entry, static_cast<void *>(error.get()));

  ProcessSP process_sp(GetSP());
  if (process_sp) {
    std::lock_guard<std::recursive_mutex> guard(
        process_sp->GetTarget().GetAPIMutex());
    if (process_sp->GetState() == eStateConnected) {
      if (stop_at_entry)
        launch_flags |= eLaunchFlagStopAtEntry;
      ProcessLaunchInfo launch_info(FileSpec(stdin_path), FileSpec(stdout_path),
                                    FileSpec(stderr_path),
                                    FileSpec(working_directory), launch_flags);
      Module *exe_module = process_sp->GetTarget().GetExecutableModulePointer();
      if (exe_module)
        launch_info.SetExecutableFile(exe_module->GetPlatformFileSpec(), true);
      if (argv)
        launch_info.GetArguments().AppendArguments(argv);
      if (envp)
        launch_info.GetEnvironment() = Environment(envp);
      error.SetError(process_sp->Launch(launch_info));
    } else {
      error.SetErrorString("must be in eStateConnected to call RemoteLaunch");
    }
  } else {
    error.SetErrorString("unable to attach pid");
  }

  if (log) {
    SBStream sstr;
    error.GetDescription(sstr);
    log->Printf("SBProcess(%p)::RemoteLaunch (...) => SBError (%p): %s",
                static_cast<void *>(process_sp.get()),
                static_cast<void *>(error.get()), sstr.GetData());
  }

  return error.Success();
}

bool SBProcess::RemoteAttachToProcessWithID(lldb::pid_t pid,
                                            lldb::SBError &error) {
  ProcessSP process_sp(GetSP());
  if (process_sp) {
    std::lock_guard<std::recursive_mutex> guard(
        process_sp->GetTarget().GetAPIMutex());
    if (process_sp->GetState() == eStateConnected) {
      ProcessAttachInfo attach_info;
      attach_info.SetProcessID(pid);
      error.SetError(process_sp->Attach(attach_info));
    } else {
      error.SetErrorString(
          "must be in eStateConnected to call RemoteAttachToProcessWithID");
    }
  } else {
    error.SetErrorString("unable to attach pid");
  }

  Log *log(lldb_private::GetLogIfAllCategoriesSet(LIBLLDB_LOG_API));
  if (log) {
    SBStream sstr;
    error.GetDescription(sstr);
    log->Printf("SBProcess(%p)::RemoteAttachToProcessWithID (%" PRIu64
                ") => SBError (%p): %s",
                static_cast<void *>(process_sp.get()), pid,
                static_cast<void *>(error.get()), sstr.GetData());
  }

  return error.Success();
}

uint32_t SBProcess::GetNumThreads() {
  Log *log(lldb_private::GetLogIfAllCategoriesSet(LIBLLDB_LOG_API));

  uint32_t num_threads = 0;
  ProcessSP process_sp(GetSP());
  if (process_sp) {
    Process::StopLocker stop_locker;

    const bool can_update = stop_locker.TryLock(&process_sp->GetRunLock());
    std::lock_guard<std::recursive_mutex> guard(
        process_sp->GetTarget().GetAPIMutex());
    num_threads = process_sp->GetThreadList().GetSize(can_update);
  }

  if (log)
    log->Printf("SBProcess(%p)::GetNumThreads () => %d",
                static_cast<void *>(process_sp.get()), num_threads);

  return num_threads;
}

SBThread SBProcess::GetSelectedThread() const {
  Log *log(lldb_private::GetLogIfAllCategoriesSet(LIBLLDB_LOG_API));

  SBThread sb_thread;
  ThreadSP thread_sp;
  ProcessSP process_sp(GetSP());
  if (process_sp) {
    std::lock_guard<std::recursive_mutex> guard(
        process_sp->GetTarget().GetAPIMutex());
    thread_sp = process_sp->GetThreadList().GetSelectedThread();
    sb_thread.SetThread(thread_sp);
  }

  if (log)
    log->Printf("SBProcess(%p)::GetSelectedThread () => SBThread(%p)",
                static_cast<void *>(process_sp.get()),
                static_cast<void *>(thread_sp.get()));

  return sb_thread;
}

SBThread SBProcess::CreateOSPluginThread(lldb::tid_t tid,
                                         lldb::addr_t context) {
  Log *log(lldb_private::GetLogIfAllCategoriesSet(LIBLLDB_LOG_API));

  SBThread sb_thread;
  ThreadSP thread_sp;
  ProcessSP process_sp(GetSP());
  if (process_sp) {
    std::lock_guard<std::recursive_mutex> guard(
        process_sp->GetTarget().GetAPIMutex());
    thread_sp = process_sp->CreateOSPluginThread(tid, context);
    sb_thread.SetThread(thread_sp);
  }

  if (log)
    log->Printf("SBProcess(%p)::CreateOSPluginThread (tid=0x%" PRIx64
                ", context=0x%" PRIx64 ") => SBThread(%p)",
                static_cast<void *>(process_sp.get()), tid, context,
                static_cast<void *>(thread_sp.get()));

  return sb_thread;
}

SBTarget SBProcess::GetTarget() const {
  Log *log(lldb_private::GetLogIfAllCategoriesSet(LIBLLDB_LOG_API));

  SBTarget sb_target;
  TargetSP target_sp;
  ProcessSP process_sp(GetSP());
  if (process_sp) {
    target_sp = process_sp->GetTarget().shared_from_this();
    sb_target.SetSP(target_sp);
  }

  if (log)
    log->Printf("SBProcess(%p)::GetTarget () => SBTarget(%p)",
                static_cast<void *>(process_sp.get()),
                static_cast<void *>(target_sp.get()));

  return sb_target;
}

size_t SBProcess::PutSTDIN(const char *src, size_t src_len) {
  Log *log(lldb_private::GetLogIfAllCategoriesSet(LIBLLDB_LOG_API));

  size_t ret_val = 0;
  ProcessSP process_sp(GetSP());
  if (process_sp) {
    Status error;
    ret_val = process_sp->PutSTDIN(src, src_len, error);
  }

  if (log)
    log->Printf("SBProcess(%p)::PutSTDIN (src=\"%s\", src_len=%" PRIu64
                ") => %" PRIu64,
                static_cast<void *>(process_sp.get()), src,
                static_cast<uint64_t>(src_len), static_cast<uint64_t>(ret_val));

  return ret_val;
}

size_t SBProcess::GetSTDOUT(char *dst, size_t dst_len) const {
  size_t bytes_read = 0;
  ProcessSP process_sp(GetSP());
  if (process_sp) {
    Status error;
    bytes_read = process_sp->GetSTDOUT(dst, dst_len, error);
  }

  Log *log(lldb_private::GetLogIfAllCategoriesSet(LIBLLDB_LOG_API));
  if (log)
    log->Printf(
        "SBProcess(%p)::GetSTDOUT (dst=\"%.*s\", dst_len=%" PRIu64
        ") => %" PRIu64,
        static_cast<void *>(process_sp.get()), static_cast<int>(bytes_read),
        dst, static_cast<uint64_t>(dst_len), static_cast<uint64_t>(bytes_read));

  return bytes_read;
}

size_t SBProcess::GetSTDERR(char *dst, size_t dst_len) const {
  size_t bytes_read = 0;
  ProcessSP process_sp(GetSP());
  if (process_sp) {
    Status error;
    bytes_read = process_sp->GetSTDERR(dst, dst_len, error);
  }

  Log *log(lldb_private::GetLogIfAllCategoriesSet(LIBLLDB_LOG_API));
  if (log)
    log->Printf(
        "SBProcess(%p)::GetSTDERR (dst=\"%.*s\", dst_len=%" PRIu64
        ") => %" PRIu64,
        static_cast<void *>(process_sp.get()), static_cast<int>(bytes_read),
        dst, static_cast<uint64_t>(dst_len), static_cast<uint64_t>(bytes_read));

  return bytes_read;
}

size_t SBProcess::GetAsyncProfileData(char *dst, size_t dst_len) const {
  size_t bytes_read = 0;
  ProcessSP process_sp(GetSP());
  if (process_sp) {
    Status error;
    bytes_read = process_sp->GetAsyncProfileData(dst, dst_len, error);
  }

  Log *log(lldb_private::GetLogIfAllCategoriesSet(LIBLLDB_LOG_API));
  if (log)
    log->Printf(
        "SBProcess(%p)::GetAsyncProfileData (dst=\"%.*s\", dst_len=%" PRIu64
        ") => %" PRIu64,
        static_cast<void *>(process_sp.get()), static_cast<int>(bytes_read),
        dst, static_cast<uint64_t>(dst_len), static_cast<uint64_t>(bytes_read));

  return bytes_read;
}

lldb::SBTrace SBProcess::StartTrace(SBTraceOptions &options,
                                    lldb::SBError &error) {
  Log *log(lldb_private::GetLogIfAllCategoriesSet(LIBLLDB_LOG_API));
  ProcessSP process_sp(GetSP());
  error.Clear();
  SBTrace trace_instance;
  trace_instance.SetSP(process_sp);
  lldb::user_id_t uid = LLDB_INVALID_UID;

  if (!process_sp) {
    error.SetErrorString("invalid process");
  } else {
    uid = process_sp->StartTrace(*(options.m_traceoptions_sp), error.ref());
    trace_instance.SetTraceUID(uid);
    LLDB_LOG(log, "SBProcess::returned uid - {0}", uid);
  }
  return trace_instance;
}

void SBProcess::ReportEventState(const SBEvent &event, FILE *out) const {
  if (out == NULL)
    return;

  ProcessSP process_sp(GetSP());
  if (process_sp) {
    const StateType event_state = SBProcess::GetStateFromEvent(event);
    char message[1024];
    int message_len = ::snprintf(
        message, sizeof(message), "Process %" PRIu64 " %s\n",
        process_sp->GetID(), SBDebugger::StateAsCString(event_state));

    if (message_len > 0)
      ::fwrite(message, 1, message_len, out);
  }
}

void SBProcess::AppendEventStateReport(const SBEvent &event,
                                       SBCommandReturnObject &result) {
  ProcessSP process_sp(GetSP());
  if (process_sp) {
    const StateType event_state = SBProcess::GetStateFromEvent(event);
    char message[1024];
    ::snprintf(message, sizeof(message), "Process %" PRIu64 " %s\n",
               process_sp->GetID(), SBDebugger::StateAsCString(event_state));

    result.AppendMessage(message);
  }
}

bool SBProcess::SetSelectedThread(const SBThread &thread) {
  ProcessSP process_sp(GetSP());
  if (process_sp) {
    std::lock_guard<std::recursive_mutex> guard(
        process_sp->GetTarget().GetAPIMutex());
    return process_sp->GetThreadList().SetSelectedThreadByID(
        thread.GetThreadID());
  }
  return false;
}

bool SBProcess::SetSelectedThreadByID(lldb::tid_t tid) {
  Log *log(lldb_private::GetLogIfAllCategoriesSet(LIBLLDB_LOG_API));

  bool ret_val = false;
  ProcessSP process_sp(GetSP());
  if (process_sp) {
    std::lock_guard<std::recursive_mutex> guard(
        process_sp->GetTarget().GetAPIMutex());
    ret_val = process_sp->GetThreadList().SetSelectedThreadByID(tid);
  }

  if (log)
    log->Printf("SBProcess(%p)::SetSelectedThreadByID (tid=0x%4.4" PRIx64
                ") => %s",
                static_cast<void *>(process_sp.get()), tid,
                (ret_val ? "true" : "false"));

  return ret_val;
}

bool SBProcess::SetSelectedThreadByIndexID(uint32_t index_id) {
  Log *log(lldb_private::GetLogIfAllCategoriesSet(LIBLLDB_LOG_API));

  bool ret_val = false;
  ProcessSP process_sp(GetSP());
  if (process_sp) {
    std::lock_guard<std::recursive_mutex> guard(
        process_sp->GetTarget().GetAPIMutex());
    ret_val = process_sp->GetThreadList().SetSelectedThreadByIndexID(index_id);
  }

  if (log)
    log->Printf("SBProcess(%p)::SetSelectedThreadByID (tid=0x%x) => %s",
                static_cast<void *>(process_sp.get()), index_id,
                (ret_val ? "true" : "false"));

  return ret_val;
}

SBThread SBProcess::GetThreadAtIndex(size_t index) {
  Log *log(lldb_private::GetLogIfAllCategoriesSet(LIBLLDB_LOG_API));

  SBThread sb_thread;
  ThreadSP thread_sp;
  ProcessSP process_sp(GetSP());
  if (process_sp) {
    Process::StopLocker stop_locker;
    const bool can_update = stop_locker.TryLock(&process_sp->GetRunLock());
    std::lock_guard<std::recursive_mutex> guard(
        process_sp->GetTarget().GetAPIMutex());
    thread_sp = process_sp->GetThreadList().GetThreadAtIndex(index, can_update);
    sb_thread.SetThread(thread_sp);
  }

  if (log)
    log->Printf("SBProcess(%p)::GetThreadAtIndex (index=%d) => SBThread(%p)",
                static_cast<void *>(process_sp.get()),
                static_cast<uint32_t>(index),
                static_cast<void *>(thread_sp.get()));

  return sb_thread;
}

uint32_t SBProcess::GetNumQueues() {
  Log *log(lldb_private::GetLogIfAllCategoriesSet(LIBLLDB_LOG_API));

  uint32_t num_queues = 0;
  ProcessSP process_sp(GetSP());
  if (process_sp) {
    Process::StopLocker stop_locker;
    if (stop_locker.TryLock(&process_sp->GetRunLock())) {
      std::lock_guard<std::recursive_mutex> guard(
          process_sp->GetTarget().GetAPIMutex());
      num_queues = process_sp->GetQueueList().GetSize();
    }
  }

  if (log)
    log->Printf("SBProcess(%p)::GetNumQueues () => %d",
                static_cast<void *>(process_sp.get()), num_queues);

  return num_queues;
}

SBQueue SBProcess::GetQueueAtIndex(size_t index) {
  Log *log(lldb_private::GetLogIfAllCategoriesSet(LIBLLDB_LOG_API));

  SBQueue sb_queue;
  QueueSP queue_sp;
  ProcessSP process_sp(GetSP());
  if (process_sp) {
    Process::StopLocker stop_locker;
    if (stop_locker.TryLock(&process_sp->GetRunLock())) {
      std::lock_guard<std::recursive_mutex> guard(
          process_sp->GetTarget().GetAPIMutex());
      queue_sp = process_sp->GetQueueList().GetQueueAtIndex(index);
      sb_queue.SetQueue(queue_sp);
    }
  }

  if (log)
    log->Printf("SBProcess(%p)::GetQueueAtIndex (index=%d) => SBQueue(%p)",
                static_cast<void *>(process_sp.get()),
                static_cast<uint32_t>(index),
                static_cast<void *>(queue_sp.get()));

  return sb_queue;
}

uint32_t SBProcess::GetStopID(bool include_expression_stops) {
  ProcessSP process_sp(GetSP());
  if (process_sp) {
    std::lock_guard<std::recursive_mutex> guard(
        process_sp->GetTarget().GetAPIMutex());
    if (include_expression_stops)
      return process_sp->GetStopID();
    else
      return process_sp->GetLastNaturalStopID();
  }
  return 0;
}

SBEvent SBProcess::GetStopEventForStopID(uint32_t stop_id) {
  Log *log(lldb_private::GetLogIfAllCategoriesSet(LIBLLDB_LOG_API));

  SBEvent sb_event;
  EventSP event_sp;
  ProcessSP process_sp(GetSP());
  if (process_sp) {
    std::lock_guard<std::recursive_mutex> guard(
        process_sp->GetTarget().GetAPIMutex());
    event_sp = process_sp->GetStopEventForStopID(stop_id);
    sb_event.reset(event_sp);
  }

  if (log)
    log->Printf("SBProcess(%p)::GetStopEventForStopID (stop_id=%" PRIu32
                ") => SBEvent(%p)",
                static_cast<void *>(process_sp.get()), stop_id,
                static_cast<void *>(event_sp.get()));

  return sb_event;
}

StateType SBProcess::GetState() {

  StateType ret_val = eStateInvalid;
  ProcessSP process_sp(GetSP());
  if (process_sp) {
    std::lock_guard<std::recursive_mutex> guard(
        process_sp->GetTarget().GetAPIMutex());
    ret_val = process_sp->GetState();
  }

  Log *log(lldb_private::GetLogIfAllCategoriesSet(LIBLLDB_LOG_API));
  if (log)
    log->Printf("SBProcess(%p)::GetState () => %s",
                static_cast<void *>(process_sp.get()),
                lldb_private::StateAsCString(ret_val));

  return ret_val;
}

int SBProcess::GetExitStatus() {
  int exit_status = 0;
  ProcessSP process_sp(GetSP());
  if (process_sp) {
    std::lock_guard<std::recursive_mutex> guard(
        process_sp->GetTarget().GetAPIMutex());
    exit_status = process_sp->GetExitStatus();
  }
  Log *log(lldb_private::GetLogIfAllCategoriesSet(LIBLLDB_LOG_API));
  if (log)
    log->Printf("SBProcess(%p)::GetExitStatus () => %i (0x%8.8x)",
                static_cast<void *>(process_sp.get()), exit_status,
                exit_status);

  return exit_status;
}

const char *SBProcess::GetExitDescription() {
  const char *exit_desc = NULL;
  ProcessSP process_sp(GetSP());
  if (process_sp) {
    std::lock_guard<std::recursive_mutex> guard(
        process_sp->GetTarget().GetAPIMutex());
    exit_desc = process_sp->GetExitDescription();
  }
  Log *log(lldb_private::GetLogIfAllCategoriesSet(LIBLLDB_LOG_API));
  if (log)
    log->Printf("SBProcess(%p)::GetExitDescription () => %s",
                static_cast<void *>(process_sp.get()), exit_desc);
  return exit_desc;
}

lldb::pid_t SBProcess::GetProcessID() {
  lldb::pid_t ret_val = LLDB_INVALID_PROCESS_ID;
  ProcessSP process_sp(GetSP());
  if (process_sp)
    ret_val = process_sp->GetID();

  Log *log(lldb_private::GetLogIfAllCategoriesSet(LIBLLDB_LOG_API));
  if (log)
    log->Printf("SBProcess(%p)::GetProcessID () => %" PRIu64,
                static_cast<void *>(process_sp.get()), ret_val);

  return ret_val;
}

uint32_t SBProcess::GetUniqueID() {
  uint32_t ret_val = 0;
  ProcessSP process_sp(GetSP());
  if (process_sp)
    ret_val = process_sp->GetUniqueID();
  Log *log(lldb_private::GetLogIfAllCategoriesSet(LIBLLDB_LOG_API));
  if (log)
    log->Printf("SBProcess(%p)::GetUniqueID () => %" PRIu32,
                static_cast<void *>(process_sp.get()), ret_val);
  return ret_val;
}

ByteOrder SBProcess::GetByteOrder() const {
  ByteOrder byteOrder = eByteOrderInvalid;
  ProcessSP process_sp(GetSP());
  if (process_sp)
    byteOrder = process_sp->GetTarget().GetArchitecture().GetByteOrder();

  Log *log(lldb_private::GetLogIfAllCategoriesSet(LIBLLDB_LOG_API));
  if (log)
    log->Printf("SBProcess(%p)::GetByteOrder () => %d",
                static_cast<void *>(process_sp.get()), byteOrder);

  return byteOrder;
}

uint32_t SBProcess::GetAddressByteSize() const {
  uint32_t size = 0;
  ProcessSP process_sp(GetSP());
  if (process_sp)
    size = process_sp->GetTarget().GetArchitecture().GetAddressByteSize();

  Log *log(lldb_private::GetLogIfAllCategoriesSet(LIBLLDB_LOG_API));
  if (log)
    log->Printf("SBProcess(%p)::GetAddressByteSize () => %d",
                static_cast<void *>(process_sp.get()), size);

  return size;
}

SBError SBProcess::Continue() {
  Log *log(lldb_private::GetLogIfAllCategoriesSet(LIBLLDB_LOG_API));

  SBError sb_error;
  ProcessSP process_sp(GetSP());

  if (log)
    log->Printf("SBProcess(%p)::Continue ()...",
                static_cast<void *>(process_sp.get()));

  if (process_sp) {
    std::lock_guard<std::recursive_mutex> guard(
        process_sp->GetTarget().GetAPIMutex());

    if (process_sp->GetTarget().GetDebugger().GetAsyncExecution())
      sb_error.ref() = process_sp->Resume();
    else
      sb_error.ref() = process_sp->ResumeSynchronous(NULL);
  } else
    sb_error.SetErrorString("SBProcess is invalid");

  if (log) {
    SBStream sstr;
    sb_error.GetDescription(sstr);
    log->Printf("SBProcess(%p)::Continue () => SBError (%p): %s",
                static_cast<void *>(process_sp.get()),
                static_cast<void *>(sb_error.get()), sstr.GetData());
  }

  return sb_error;
}

SBError SBProcess::Destroy() {
  SBError sb_error;
  ProcessSP process_sp(GetSP());
  if (process_sp) {
    std::lock_guard<std::recursive_mutex> guard(
        process_sp->GetTarget().GetAPIMutex());
    sb_error.SetError(process_sp->Destroy(false));
  } else
    sb_error.SetErrorString("SBProcess is invalid");

  Log *log(lldb_private::GetLogIfAllCategoriesSet(LIBLLDB_LOG_API));
  if (log) {
    SBStream sstr;
    sb_error.GetDescription(sstr);
    log->Printf("SBProcess(%p)::Destroy () => SBError (%p): %s",
                static_cast<void *>(process_sp.get()),
                static_cast<void *>(sb_error.get()), sstr.GetData());
  }

  return sb_error;
}

SBError SBProcess::Stop() {
  SBError sb_error;
  ProcessSP process_sp(GetSP());
  if (process_sp) {
    std::lock_guard<std::recursive_mutex> guard(
        process_sp->GetTarget().GetAPIMutex());
    sb_error.SetError(process_sp->Halt());
  } else
    sb_error.SetErrorString("SBProcess is invalid");

  Log *log(lldb_private::GetLogIfAllCategoriesSet(LIBLLDB_LOG_API));
  if (log) {
    SBStream sstr;
    sb_error.GetDescription(sstr);
    log->Printf("SBProcess(%p)::Stop () => SBError (%p): %s",
                static_cast<void *>(process_sp.get()),
                static_cast<void *>(sb_error.get()), sstr.GetData());
  }

  return sb_error;
}

SBError SBProcess::Kill() {
  SBError sb_error;
  ProcessSP process_sp(GetSP());
  if (process_sp) {
    std::lock_guard<std::recursive_mutex> guard(
        process_sp->GetTarget().GetAPIMutex());
    sb_error.SetError(process_sp->Destroy(true));
  } else
    sb_error.SetErrorString("SBProcess is invalid");

  Log *log(lldb_private::GetLogIfAllCategoriesSet(LIBLLDB_LOG_API));
  if (log) {
    SBStream sstr;
    sb_error.GetDescription(sstr);
    log->Printf("SBProcess(%p)::Kill () => SBError (%p): %s",
                static_cast<void *>(process_sp.get()),
                static_cast<void *>(sb_error.get()), sstr.GetData());
  }

  return sb_error;
}

SBError SBProcess::Detach() {
  // FIXME: This should come from a process default.
  bool keep_stopped = false;
  return Detach(keep_stopped);
}

SBError SBProcess::Detach(bool keep_stopped) {
  SBError sb_error;
  ProcessSP process_sp(GetSP());
  if (process_sp) {
    std::lock_guard<std::recursive_mutex> guard(
        process_sp->GetTarget().GetAPIMutex());
    sb_error.SetError(process_sp->Detach(keep_stopped));
  } else
    sb_error.SetErrorString("SBProcess is invalid");

  return sb_error;
}

SBError SBProcess::Signal(int signo) {
  SBError sb_error;
  ProcessSP process_sp(GetSP());
  if (process_sp) {
    std::lock_guard<std::recursive_mutex> guard(
        process_sp->GetTarget().GetAPIMutex());
    sb_error.SetError(process_sp->Signal(signo));
  } else
    sb_error.SetErrorString("SBProcess is invalid");
  Log *log(lldb_private::GetLogIfAllCategoriesSet(LIBLLDB_LOG_API));
  if (log) {
    SBStream sstr;
    sb_error.GetDescription(sstr);
    log->Printf("SBProcess(%p)::Signal (signo=%i) => SBError (%p): %s",
                static_cast<void *>(process_sp.get()), signo,
                static_cast<void *>(sb_error.get()), sstr.GetData());
  }
  return sb_error;
}

SBUnixSignals SBProcess::GetUnixSignals() {
  if (auto process_sp = GetSP())
    return SBUnixSignals{process_sp};

  return {};
}

void SBProcess::SendAsyncInterrupt() {
  ProcessSP process_sp(GetSP());
  if (process_sp) {
    process_sp->SendAsyncInterrupt();
  }
}

SBThread SBProcess::GetThreadByID(tid_t tid) {
  SBThread sb_thread;
  ThreadSP thread_sp;
  ProcessSP process_sp(GetSP());
  if (process_sp) {
    Process::StopLocker stop_locker;
    const bool can_update = stop_locker.TryLock(&process_sp->GetRunLock());
    std::lock_guard<std::recursive_mutex> guard(
        process_sp->GetTarget().GetAPIMutex());
    thread_sp = process_sp->GetThreadList().FindThreadByID(tid, can_update);
    sb_thread.SetThread(thread_sp);
  }

  Log *log(lldb_private::GetLogIfAllCategoriesSet(LIBLLDB_LOG_API));
  if (log)
    log->Printf("SBProcess(%p)::GetThreadByID (tid=0x%4.4" PRIx64
                ") => SBThread (%p)",
                static_cast<void *>(process_sp.get()), tid,
                static_cast<void *>(thread_sp.get()));

  return sb_thread;
}

SBThread SBProcess::GetThreadByIndexID(uint32_t index_id) {
  SBThread sb_thread;
  ThreadSP thread_sp;
  ProcessSP process_sp(GetSP());
  if (process_sp) {
    Process::StopLocker stop_locker;
    const bool can_update = stop_locker.TryLock(&process_sp->GetRunLock());
    std::lock_guard<std::recursive_mutex> guard(
        process_sp->GetTarget().GetAPIMutex());
    thread_sp =
        process_sp->GetThreadList().FindThreadByIndexID(index_id, can_update);
    sb_thread.SetThread(thread_sp);
  }

  Log *log(lldb_private::GetLogIfAllCategoriesSet(LIBLLDB_LOG_API));
  if (log)
    log->Printf("SBProcess(%p)::GetThreadByID (tid=0x%x) => SBThread (%p)",
                static_cast<void *>(process_sp.get()), index_id,
                static_cast<void *>(thread_sp.get()));

  return sb_thread;
}

StateType SBProcess::GetStateFromEvent(const SBEvent &event) {
  Log *log(lldb_private::GetLogIfAllCategoriesSet(LIBLLDB_LOG_API));

  StateType ret_val = Process::ProcessEventData::GetStateFromEvent(event.get());

  if (log)
    log->Printf("SBProcess::GetStateFromEvent (event.sp=%p) => %s",
                static_cast<void *>(event.get()),
                lldb_private::StateAsCString(ret_val));

  return ret_val;
}

bool SBProcess::GetRestartedFromEvent(const SBEvent &event) {
  Log *log(lldb_private::GetLogIfAllCategoriesSet(LIBLLDB_LOG_API));

  bool ret_val = Process::ProcessEventData::GetRestartedFromEvent(event.get());

  if (log)
    log->Printf("SBProcess::%s (event.sp=%p) => %d", __FUNCTION__,
                static_cast<void *>(event.get()), ret_val);

  return ret_val;
}

size_t SBProcess::GetNumRestartedReasonsFromEvent(const lldb::SBEvent &event) {
  return Process::ProcessEventData::GetNumRestartedReasons(event.get());
}

const char *
SBProcess::GetRestartedReasonAtIndexFromEvent(const lldb::SBEvent &event,
                                              size_t idx) {
  return Process::ProcessEventData::GetRestartedReasonAtIndex(event.get(), idx);
}

SBProcess SBProcess::GetProcessFromEvent(const SBEvent &event) {
  ProcessSP process_sp =
      Process::ProcessEventData::GetProcessFromEvent(event.get());
  if (!process_sp) {
    // StructuredData events also know the process they come from. Try that.
    process_sp = EventDataStructuredData::GetProcessFromEvent(event.get());
  }

  return SBProcess(process_sp);
}

bool SBProcess::GetInterruptedFromEvent(const SBEvent &event) {
  return Process::ProcessEventData::GetInterruptedFromEvent(event.get());
}

lldb::SBStructuredData
SBProcess::GetStructuredDataFromEvent(const lldb::SBEvent &event) {
  return SBStructuredData(event.GetSP());
}

bool SBProcess::EventIsProcessEvent(const SBEvent &event) {
  return (event.GetBroadcasterClass() == SBProcess::GetBroadcasterClass()) &&
         !EventIsStructuredDataEvent(event);
}

bool SBProcess::EventIsStructuredDataEvent(const lldb::SBEvent &event) {
  EventSP event_sp = event.GetSP();
  EventData *event_data = event_sp ? event_sp->GetData() : nullptr;
  return event_data && (event_data->GetFlavor() ==
                        EventDataStructuredData::GetFlavorString());
}

SBBroadcaster SBProcess::GetBroadcaster() const {
  Log *log(lldb_private::GetLogIfAllCategoriesSet(LIBLLDB_LOG_API));

  ProcessSP process_sp(GetSP());

  SBBroadcaster broadcaster(process_sp.get(), false);

  if (log)
    log->Printf("SBProcess(%p)::GetBroadcaster () => SBBroadcaster (%p)",
                static_cast<void *>(process_sp.get()),
                static_cast<void *>(broadcaster.get()));

  return broadcaster;
}

const char *SBProcess::GetBroadcasterClass() {
  return Process::GetStaticBroadcasterClass().AsCString();
}

size_t SBProcess::ReadMemory(addr_t addr, void *dst, size_t dst_len,
                             SBError &sb_error) {
  Log *log(lldb_private::GetLogIfAllCategoriesSet(LIBLLDB_LOG_API));

  size_t bytes_read = 0;

  ProcessSP process_sp(GetSP());

  if (log)
    log->Printf("SBProcess(%p)::ReadMemory (addr=0x%" PRIx64
                ", dst=%p, dst_len=%" PRIu64 ", SBError (%p))...",
                static_cast<void *>(process_sp.get()), addr,
                static_cast<void *>(dst), static_cast<uint64_t>(dst_len),
                static_cast<void *>(sb_error.get()));

  if (process_sp) {
    Process::StopLocker stop_locker;
    if (stop_locker.TryLock(&process_sp->GetRunLock())) {
      std::lock_guard<std::recursive_mutex> guard(
          process_sp->GetTarget().GetAPIMutex());
      bytes_read = process_sp->ReadMemory(addr, dst, dst_len, sb_error.ref());
    } else {
      if (log)
        log->Printf("SBProcess(%p)::ReadMemory() => error: process is running",
                    static_cast<void *>(process_sp.get()));
      sb_error.SetErrorString("process is running");
    }
  } else {
    sb_error.SetErrorString("SBProcess is invalid");
  }

  if (log) {
    SBStream sstr;
    sb_error.GetDescription(sstr);
    log->Printf("SBProcess(%p)::ReadMemory (addr=0x%" PRIx64
                ", dst=%p, dst_len=%" PRIu64 ", SBError (%p): %s) => %" PRIu64,
                static_cast<void *>(process_sp.get()), addr,
                static_cast<void *>(dst), static_cast<uint64_t>(dst_len),
                static_cast<void *>(sb_error.get()), sstr.GetData(),
                static_cast<uint64_t>(bytes_read));
  }

  return bytes_read;
}

size_t SBProcess::ReadCStringFromMemory(addr_t addr, void *buf, size_t size,
                                        lldb::SBError &sb_error) {
  size_t bytes_read = 0;
  ProcessSP process_sp(GetSP());
  if (process_sp) {
    Process::StopLocker stop_locker;
    if (stop_locker.TryLock(&process_sp->GetRunLock())) {
      std::lock_guard<std::recursive_mutex> guard(
          process_sp->GetTarget().GetAPIMutex());
      bytes_read = process_sp->ReadCStringFromMemory(addr, (char *)buf, size,
                                                     sb_error.ref());
    } else {
      Log *log(lldb_private::GetLogIfAllCategoriesSet(LIBLLDB_LOG_API));
      if (log)
        log->Printf("SBProcess(%p)::ReadCStringFromMemory() => error: process "
                    "is running",
                    static_cast<void *>(process_sp.get()));
      sb_error.SetErrorString("process is running");
    }
  } else {
    sb_error.SetErrorString("SBProcess is invalid");
  }
  return bytes_read;
}

uint64_t SBProcess::ReadUnsignedFromMemory(addr_t addr, uint32_t byte_size,
                                           lldb::SBError &sb_error) {
  uint64_t value = 0;
  ProcessSP process_sp(GetSP());
  if (process_sp) {
    Process::StopLocker stop_locker;
    if (stop_locker.TryLock(&process_sp->GetRunLock())) {
      std::lock_guard<std::recursive_mutex> guard(
          process_sp->GetTarget().GetAPIMutex());
      value = process_sp->ReadUnsignedIntegerFromMemory(addr, byte_size, 0,
                                                        sb_error.ref());
    } else {
      Log *log(lldb_private::GetLogIfAllCategoriesSet(LIBLLDB_LOG_API));
      if (log)
        log->Printf("SBProcess(%p)::ReadUnsignedFromMemory() => error: process "
                    "is running",
                    static_cast<void *>(process_sp.get()));
      sb_error.SetErrorString("process is running");
    }
  } else {
    sb_error.SetErrorString("SBProcess is invalid");
  }
  return value;
}

lldb::addr_t SBProcess::ReadPointerFromMemory(addr_t addr,
                                              lldb::SBError &sb_error) {
  lldb::addr_t ptr = LLDB_INVALID_ADDRESS;
  ProcessSP process_sp(GetSP());
  if (process_sp) {
    Process::StopLocker stop_locker;
    if (stop_locker.TryLock(&process_sp->GetRunLock())) {
      std::lock_guard<std::recursive_mutex> guard(
          process_sp->GetTarget().GetAPIMutex());
      ptr = process_sp->ReadPointerFromMemory(addr, sb_error.ref());
    } else {
      Log *log(lldb_private::GetLogIfAllCategoriesSet(LIBLLDB_LOG_API));
      if (log)
        log->Printf("SBProcess(%p)::ReadPointerFromMemory() => error: process "
                    "is running",
                    static_cast<void *>(process_sp.get()));
      sb_error.SetErrorString("process is running");
    }
  } else {
    sb_error.SetErrorString("SBProcess is invalid");
  }
  return ptr;
}

size_t SBProcess::WriteMemory(addr_t addr, const void *src, size_t src_len,
                              SBError &sb_error) {
  size_t bytes_written = 0;

  Log *log(lldb_private::GetLogIfAllCategoriesSet(LIBLLDB_LOG_API));

  ProcessSP process_sp(GetSP());

  if (log)
    log->Printf("SBProcess(%p)::WriteMemory (addr=0x%" PRIx64
                ", src=%p, src_len=%" PRIu64 ", SBError (%p))...",
                static_cast<void *>(process_sp.get()), addr,
                static_cast<const void *>(src), static_cast<uint64_t>(src_len),
                static_cast<void *>(sb_error.get()));

  if (process_sp) {
    Process::StopLocker stop_locker;
    if (stop_locker.TryLock(&process_sp->GetRunLock())) {
      std::lock_guard<std::recursive_mutex> guard(
          process_sp->GetTarget().GetAPIMutex());
      bytes_written =
          process_sp->WriteMemory(addr, src, src_len, sb_error.ref());
    } else {
      if (log)
        log->Printf("SBProcess(%p)::WriteMemory() => error: process is running",
                    static_cast<void *>(process_sp.get()));
      sb_error.SetErrorString("process is running");
    }
  }

  if (log) {
    SBStream sstr;
    sb_error.GetDescription(sstr);
    log->Printf("SBProcess(%p)::WriteMemory (addr=0x%" PRIx64
                ", src=%p, src_len=%" PRIu64 ", SBError (%p): %s) => %" PRIu64,
                static_cast<void *>(process_sp.get()), addr,
                static_cast<const void *>(src), static_cast<uint64_t>(src_len),
                static_cast<void *>(sb_error.get()), sstr.GetData(),
                static_cast<uint64_t>(bytes_written));
  }

  return bytes_written;
}

bool SBProcess::GetDescription(SBStream &description) {
  Stream &strm = description.ref();

  ProcessSP process_sp(GetSP());
  if (process_sp) {
    char path[PATH_MAX];
    GetTarget().GetExecutable().GetPath(path, sizeof(path));
    Module *exe_module = process_sp->GetTarget().GetExecutableModulePointer();
    const char *exe_name = NULL;
    if (exe_module)
      exe_name = exe_module->GetFileSpec().GetFilename().AsCString();

    strm.Printf("SBProcess: pid = %" PRIu64 ", state = %s, threads = %d%s%s",
                process_sp->GetID(), lldb_private::StateAsCString(GetState()),
                GetNumThreads(), exe_name ? ", executable = " : "",
                exe_name ? exe_name : "");
  } else
    strm.PutCString("No value");

  return true;
}

uint32_t
SBProcess::GetNumSupportedHardwareWatchpoints(lldb::SBError &sb_error) const {
  Log *log(lldb_private::GetLogIfAllCategoriesSet(LIBLLDB_LOG_API));

  uint32_t num = 0;
  ProcessSP process_sp(GetSP());
  if (process_sp) {
    std::lock_guard<std::recursive_mutex> guard(
        process_sp->GetTarget().GetAPIMutex());
    sb_error.SetError(process_sp->GetWatchpointSupportInfo(num));
    if (log)
      log->Printf("SBProcess(%p)::GetNumSupportedHardwareWatchpoints () => %u",
                  static_cast<void *>(process_sp.get()), num);
  } else {
    sb_error.SetErrorString("SBProcess is invalid");
  }
  return num;
}

uint32_t SBProcess::LoadImage(lldb::SBFileSpec &sb_remote_image_spec,
                              lldb::SBError &sb_error) {
  return LoadImage(SBFileSpec(), sb_remote_image_spec, sb_error);
}

uint32_t SBProcess::LoadImage(const lldb::SBFileSpec &sb_local_image_spec,
                              const lldb::SBFileSpec &sb_remote_image_spec,
                              lldb::SBError &sb_error) {
  Log *log(lldb_private::GetLogIfAllCategoriesSet(LIBLLDB_LOG_API));
  ProcessSP process_sp(GetSP());
  if (process_sp) {
    Process::StopLocker stop_locker;
    if (stop_locker.TryLock(&process_sp->GetRunLock())) {
      if (log)
        log->Printf("SBProcess(%p)::LoadImage() => calling Platform::LoadImage"
                    "for: %s",
                    static_cast<void *>(process_sp.get()),
                    sb_local_image_spec.GetFilename());

      std::lock_guard<std::recursive_mutex> guard(
        process_sp->GetTarget().GetAPIMutex());
      PlatformSP platform_sp = process_sp->GetTarget().GetPlatform();
      return platform_sp->LoadImage(process_sp.get(), *sb_local_image_spec,
                                    *sb_remote_image_spec, sb_error.ref());
    } else {
      if (log)
        log->Printf("SBProcess(%p)::LoadImage() => error: process is running",
                    static_cast<void *>(process_sp.get()));
      sb_error.SetErrorString("process is running");
    }
  } else { 
    if (log)
      log->Printf("SBProcess(%p)::LoadImage() => error: called with invalid"
                    " process",
                    static_cast<void *>(process_sp.get()));
    sb_error.SetErrorString("process is invalid");
  }
  return LLDB_INVALID_IMAGE_TOKEN;
}

uint32_t SBProcess::LoadImageUsingPaths(const lldb::SBFileSpec &image_spec,
                                        SBStringList &paths,
                                        lldb::SBFileSpec &loaded_path, 
                                        lldb::SBError &error) {
  Log *log(lldb_private::GetLogIfAllCategoriesSet(LIBLLDB_LOG_API));
  ProcessSP process_sp(GetSP());
  if (process_sp) {
    Process::StopLocker stop_locker;
    if (stop_locker.TryLock(&process_sp->GetRunLock())) {
      if (log)
        log->Printf("SBProcess(%p)::LoadImageUsingPaths() => "
                    "calling Platform::LoadImageUsingPaths for: %s",
                    static_cast<void *>(process_sp.get()),
                    image_spec.GetFilename());

      std::lock_guard<std::recursive_mutex> guard(
        process_sp->GetTarget().GetAPIMutex());
      PlatformSP platform_sp = process_sp->GetTarget().GetPlatform();
      size_t num_paths = paths.GetSize();
      std::vector<std::string> paths_vec;
      paths_vec.reserve(num_paths);
      for (size_t i = 0; i < num_paths; i++)
        paths_vec.push_back(paths.GetStringAtIndex(i));
      FileSpec loaded_spec;
      
      uint32_t token = platform_sp->LoadImageUsingPaths(process_sp.get(),
                                                        *image_spec, 
                                                        paths_vec, 
                                                        error.ref(), 
                                                        &loaded_spec);
       if (token != LLDB_INVALID_IMAGE_TOKEN)
          loaded_path = loaded_spec;
       return token;
    } else {
      if (log)
        log->Printf("SBProcess(%p)::LoadImageUsingPaths() => error: "
                    "process is running",
                    static_cast<void *>(process_sp.get()));
      error.SetErrorString("process is running");
    }
  } else { 
    if (log)
      log->Printf("SBProcess(%p)::LoadImageUsingPaths() => error: "
                   "called with invalid process",
                    static_cast<void *>(process_sp.get()));
    error.SetErrorString("process is invalid");
  }
  
  return LLDB_INVALID_IMAGE_TOKEN;
}

lldb::SBError SBProcess::UnloadImage(uint32_t image_token) {
  lldb::SBError sb_error;
  ProcessSP process_sp(GetSP());
  if (process_sp) {
    Process::StopLocker stop_locker;
    if (stop_locker.TryLock(&process_sp->GetRunLock())) {
      std::lock_guard<std::recursive_mutex> guard(
          process_sp->GetTarget().GetAPIMutex());
      PlatformSP platform_sp = process_sp->GetTarget().GetPlatform();
      sb_error.SetError(
          platform_sp->UnloadImage(process_sp.get(), image_token));
    } else {
      Log *log(lldb_private::GetLogIfAllCategoriesSet(LIBLLDB_LOG_API));
      if (log)
        log->Printf("SBProcess(%p)::UnloadImage() => error: process is running",
                    static_cast<void *>(process_sp.get()));
      sb_error.SetErrorString("process is running");
    }
  } else
    sb_error.SetErrorString("invalid process");
  return sb_error;
}

lldb::SBError SBProcess::SendEventData(const char *event_data) {
  lldb::SBError sb_error;
  ProcessSP process_sp(GetSP());
  if (process_sp) {
    Process::StopLocker stop_locker;
    if (stop_locker.TryLock(&process_sp->GetRunLock())) {
      std::lock_guard<std::recursive_mutex> guard(
          process_sp->GetTarget().GetAPIMutex());
      sb_error.SetError(process_sp->SendEventData(event_data));
    } else {
      Log *log(lldb_private::GetLogIfAllCategoriesSet(LIBLLDB_LOG_API));
      if (log)
        log->Printf(
            "SBProcess(%p)::SendEventData() => error: process is running",
            static_cast<void *>(process_sp.get()));
      sb_error.SetErrorString("process is running");
    }
  } else
    sb_error.SetErrorString("invalid process");
  return sb_error;
}

uint32_t SBProcess::GetNumExtendedBacktraceTypes() {
  ProcessSP process_sp(GetSP());
  if (process_sp && process_sp->GetSystemRuntime()) {
    SystemRuntime *runtime = process_sp->GetSystemRuntime();
    return runtime->GetExtendedBacktraceTypes().size();
  }
  return 0;
}

const char *SBProcess::GetExtendedBacktraceTypeAtIndex(uint32_t idx) {
  ProcessSP process_sp(GetSP());
  if (process_sp && process_sp->GetSystemRuntime()) {
    SystemRuntime *runtime = process_sp->GetSystemRuntime();
    const std::vector<ConstString> &names =
        runtime->GetExtendedBacktraceTypes();
    if (idx < names.size()) {
      return names[idx].AsCString();
    } else {
      Log *log(lldb_private::GetLogIfAllCategoriesSet(LIBLLDB_LOG_API));
      if (log)
        log->Printf("SBProcess(%p)::GetExtendedBacktraceTypeAtIndex() => "
                    "error: requested extended backtrace name out of bounds",
                    static_cast<void *>(process_sp.get()));
    }
  }
  return NULL;
}

SBThreadCollection SBProcess::GetHistoryThreads(addr_t addr) {
  ProcessSP process_sp(GetSP());
  SBThreadCollection threads;
  if (process_sp) {
    threads = SBThreadCollection(process_sp->GetHistoryThreads(addr));
  }
  return threads;
}

bool SBProcess::IsInstrumentationRuntimePresent(
    InstrumentationRuntimeType type) {
  ProcessSP process_sp(GetSP());
  if (!process_sp)
    return false;

  InstrumentationRuntimeSP runtime_sp =
      process_sp->GetInstrumentationRuntime(type);

  if (!runtime_sp.get())
    return false;

  return runtime_sp->IsActive();
}

lldb::SBError SBProcess::SaveCore(const char *file_name) {
  lldb::SBError error;
  ProcessSP process_sp(GetSP());
  if (!process_sp) {
    error.SetErrorString("SBProcess is invalid");
    return error;
  }

  std::lock_guard<std::recursive_mutex> guard(
      process_sp->GetTarget().GetAPIMutex());

  if (process_sp->GetState() != eStateStopped) {
    error.SetErrorString("the process is not stopped");
    return error;
  }

  FileSpec core_file(file_name);
  error.ref() = PluginManager::SaveCore(process_sp, core_file);
  return error;
}

lldb::SBError
SBProcess::GetMemoryRegionInfo(lldb::addr_t load_addr,
                               SBMemoryRegionInfo &sb_region_info) {
  lldb::SBError sb_error;
  ProcessSP process_sp(GetSP());
  if (process_sp) {
    Process::StopLocker stop_locker;
    if (stop_locker.TryLock(&process_sp->GetRunLock())) {
      std::lock_guard<std::recursive_mutex> guard(
          process_sp->GetTarget().GetAPIMutex());

      sb_error.ref() =
          process_sp->GetMemoryRegionInfo(load_addr, sb_region_info.ref());
    } else {
      Log *log(lldb_private::GetLogIfAllCategoriesSet(LIBLLDB_LOG_API));
      if (log)
        log->Printf(
            "SBProcess(%p)::GetMemoryRegionInfo() => error: process is running",
            static_cast<void *>(process_sp.get()));
      sb_error.SetErrorString("process is running");
    }
  } else {
    sb_error.SetErrorString("SBProcess is invalid");
  }
  return sb_error;
}

lldb::SBMemoryRegionInfoList SBProcess::GetMemoryRegions() {
  lldb::SBMemoryRegionInfoList sb_region_list;

  ProcessSP process_sp(GetSP());
  Process::StopLocker stop_locker;
  if (process_sp && stop_locker.TryLock(&process_sp->GetRunLock())) {
    std::lock_guard<std::recursive_mutex> guard(
        process_sp->GetTarget().GetAPIMutex());

    process_sp->GetMemoryRegions(sb_region_list.ref());
  } else {
    Log *log(lldb_private::GetLogIfAllCategoriesSet(LIBLLDB_LOG_API));
    if (log)
      log->Printf(
          "SBProcess(%p)::GetMemoryRegionInfo() => error: process is running",
          static_cast<void *>(process_sp.get()));
  }

  return sb_region_list;
}

lldb::SBProcessInfo SBProcess::GetProcessInfo() {
  lldb::SBProcessInfo sb_proc_info;
  ProcessSP process_sp(GetSP());
  ProcessInstanceInfo proc_info;
  if (process_sp && process_sp->GetProcessInfo(proc_info)) {
    sb_proc_info.SetProcessInfo(proc_info);
  }
  return sb_proc_info;
}
