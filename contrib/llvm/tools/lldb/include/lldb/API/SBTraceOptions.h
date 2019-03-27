//===-- SBTraceOptions ------------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef SBTRACEOPTIONS_H_
#define SBTRACEOPTIONS_H_

#include "lldb/API/SBDefines.h"

namespace lldb {

class LLDB_API SBTraceOptions {
public:
  SBTraceOptions();

  lldb::TraceType getType() const;

  uint64_t getTraceBufferSize() const;

  /// The trace parameters consist of any custom parameters
  /// apart from the generic parameters such as
  /// TraceType, trace_buffer_size and meta_data_buffer_size.
  /// The returned parameters would be formatted as a JSON Dictionary.
  lldb::SBStructuredData getTraceParams(lldb::SBError &error);

  uint64_t getMetaDataBufferSize() const;

  /// SBStructuredData is meant to hold any custom parameters
  /// apart from meta buffer size and trace size. They should
  /// be formatted as a JSON Dictionary.
  void setTraceParams(lldb::SBStructuredData &params);

  void setType(lldb::TraceType type);

  void setTraceBufferSize(uint64_t size);

  void setMetaDataBufferSize(uint64_t size);

  void setThreadID(lldb::tid_t thread_id);

  lldb::tid_t getThreadID();

  bool IsValid();

protected:
  friend class SBProcess;
  friend class SBTrace;

  lldb::TraceOptionsSP m_traceoptions_sp;
};
}

#endif /* SBTRACEOPTIONS_H_ */
