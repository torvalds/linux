//===-- SBTrace.h -----------------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_SBTrace_h_
#define LLDB_SBTrace_h_

#include "lldb/API/SBDefines.h"
#include "lldb/API/SBError.h"

class TraceImpl;

namespace lldb {

class LLDB_API SBTrace {
public:
  SBTrace();
  //------------------------------------------------------------------
  /// Obtain the trace data as raw bytes.
  ///
  /// @param[out] error
  ///     An error explaining what went wrong.
  ///
  /// @param[in] buf
  ///     Buffer to write the trace data to.
  ///
  /// @param[in] size
  ///     The size of the buffer used to read the data. This is
  ///     also the size of the data intended to read. It is also
  ///     possible to partially read the trace data for some trace
  ///     technologies by specifying a smaller buffer.
  ///
  /// @param[in] offset
  ///     The start offset to begin reading the trace data.
  ///
  /// @param[in] thread_id
  ///     Tracing could be started for the complete process or a
  ///     single thread, in the first case the traceid obtained would
  ///     map to all the threads existing within the process and the
  ///     ones spawning later. The thread_id parameter can be used in
  ///     such a scenario to select the trace data for a specific
  ///     thread.
  ///
  /// @return
  ///     The size of the trace data effectively read by the API call.
  //------------------------------------------------------------------
  size_t GetTraceData(SBError &error, void *buf, size_t size, size_t offset = 0,
                      lldb::tid_t thread_id = LLDB_INVALID_THREAD_ID);

  //------------------------------------------------------------------
  /// Obtain any meta data as raw bytes for the tracing instance.
  /// The input parameter definition is similar to the previous
  /// function.
  //------------------------------------------------------------------
  size_t GetMetaData(SBError &error, void *buf, size_t size, size_t offset = 0,
                     lldb::tid_t thread_id = LLDB_INVALID_THREAD_ID);

  //------------------------------------------------------------------
  /// Stop the tracing instance. Stopping the trace will also
  /// lead to deletion of any gathered trace data.
  ///
  /// @param[out] error
  ///     An error explaining what went wrong.
  ///
  /// @param[in] thread_id
  ///     The trace id could map to a tracing instance for a thread
  ///     or could also map to a group of threads being traced with
  ///     the same trace options. A thread_id is normally optional
  ///     except in the case of tracing a complete process and tracing
  ///     needs to switched off on a particular thread.
  ///     A situation could occur where initially a thread (lets say
  ///     thread A) is being individually traced with a particular
  ///     trace id and then tracing is started on the complete
  ///     process, in this case thread A will continue without any
  ///     change. All newly spawned threads would be traced with the
  ///     trace id of the process.
  ///     Now if the StopTrace API is called for the whole process,
  ///     thread A will not be stopped and must be stopped separately.
  //------------------------------------------------------------------
  void StopTrace(SBError &error,
                 lldb::tid_t thread_id = LLDB_INVALID_THREAD_ID);

  //------------------------------------------------------------------
  /// Get the trace configuration being used for the trace instance.
  /// The threadid in the SBTraceOptions needs to be set when the
  /// configuration used by a specific thread is being requested.
  ///
  /// @param[out] options
  ///     The trace options actually used by the trace instance
  ///     would be filled by the API.
  ///
  /// @param[out] error
  ///     An error explaining what went wrong.
  //------------------------------------------------------------------
  void GetTraceConfig(SBTraceOptions &options, SBError &error);

  lldb::user_id_t GetTraceUID();

  bool IsValid();

protected:
  typedef std::shared_ptr<TraceImpl> TraceImplSP;

  friend class SBProcess;

  void SetTraceUID(lldb::user_id_t uid);

  TraceImplSP m_trace_impl_sp;

  lldb::ProcessSP GetSP() const;

  void SetSP(const ProcessSP &process_sp);

  lldb::ProcessWP m_opaque_wp;
};
} // namespace lldb

#endif // LLDB_SBTrace_h_
