//===-- SBTrace.cpp ---------------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "lldb/Target/Process.h"
#include "lldb/Utility/Log.h"

#include "lldb/API/SBTrace.h"
#include "lldb/API/SBTraceOptions.h"

using namespace lldb;
using namespace lldb_private;

class TraceImpl {
public:
  lldb::user_id_t uid;
};

lldb::ProcessSP SBTrace::GetSP() const { return m_opaque_wp.lock(); }

size_t SBTrace::GetTraceData(SBError &error, void *buf, size_t size,
                             size_t offset, lldb::tid_t thread_id) {
  ProcessSP process_sp(GetSP());
  Log *log(lldb_private::GetLogIfAllCategoriesSet(LIBLLDB_LOG_API));
  llvm::MutableArrayRef<uint8_t> buffer(static_cast<uint8_t *>(buf), size);
  error.Clear();

  if (!process_sp) {
    error.SetErrorString("invalid process");
  } else {
    error.SetError(
        process_sp->GetData(GetTraceUID(), thread_id, buffer, offset));
    LLDB_LOG(log, "SBTrace::bytes_read - {0}", buffer.size());
  }
  return buffer.size();
}

size_t SBTrace::GetMetaData(SBError &error, void *buf, size_t size,
                            size_t offset, lldb::tid_t thread_id) {
  ProcessSP process_sp(GetSP());
  Log *log(lldb_private::GetLogIfAllCategoriesSet(LIBLLDB_LOG_API));
  llvm::MutableArrayRef<uint8_t> buffer(static_cast<uint8_t *>(buf), size);
  error.Clear();

  if (!process_sp) {
    error.SetErrorString("invalid process");
  } else {

    error.SetError(
        process_sp->GetMetaData(GetTraceUID(), thread_id, buffer, offset));
    LLDB_LOG(log, "SBTrace::bytes_read - {0}", buffer.size());
  }
  return buffer.size();
}

void SBTrace::StopTrace(SBError &error, lldb::tid_t thread_id) {
  ProcessSP process_sp(GetSP());
  error.Clear();

  if (!process_sp) {
    error.SetErrorString("invalid process");
    return;
  }
  error.SetError(process_sp->StopTrace(GetTraceUID(), thread_id));
}

void SBTrace::GetTraceConfig(SBTraceOptions &options, SBError &error) {
  ProcessSP process_sp(GetSP());
  error.Clear();

  if (!process_sp) {
    error.SetErrorString("invalid process");
  } else {
    error.SetError(process_sp->GetTraceConfig(GetTraceUID(),
                                              *(options.m_traceoptions_sp)));
  }
}

lldb::user_id_t SBTrace::GetTraceUID() {
  if (m_trace_impl_sp)
    return m_trace_impl_sp->uid;
  return LLDB_INVALID_UID;
}

void SBTrace::SetTraceUID(lldb::user_id_t uid) {
  if (m_trace_impl_sp)
    m_trace_impl_sp->uid = uid;
}

SBTrace::SBTrace() {
  m_trace_impl_sp.reset(new TraceImpl);
  if (m_trace_impl_sp)
    m_trace_impl_sp->uid = LLDB_INVALID_UID;
}

void SBTrace::SetSP(const ProcessSP &process_sp) { m_opaque_wp = process_sp; }

bool SBTrace::IsValid() {
  if (!m_trace_impl_sp)
    return false;
  if (!GetSP())
    return false;
  return true;
}
