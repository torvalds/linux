//===-- HostNativeProcessBase.h ---------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef lldb_Host_HostNativeProcessBase_h_
#define lldb_Host_HostNativeProcessBase_h_

#include "lldb/Host/HostProcess.h"
#include "lldb/Utility/Status.h"
#include "lldb/lldb-defines.h"
#include "lldb/lldb-types.h"

namespace lldb_private {

class HostThread;

class HostNativeProcessBase {
  DISALLOW_COPY_AND_ASSIGN(HostNativeProcessBase);

public:
  HostNativeProcessBase() : m_process(LLDB_INVALID_PROCESS) {}
  explicit HostNativeProcessBase(lldb::process_t process)
      : m_process(process) {}
  virtual ~HostNativeProcessBase() {}

  virtual Status Terminate() = 0;
  virtual Status GetMainModule(FileSpec &file_spec) const = 0;

  virtual lldb::pid_t GetProcessId() const = 0;
  virtual bool IsRunning() const = 0;

  lldb::process_t GetSystemHandle() const { return m_process; }

  virtual HostThread
  StartMonitoring(const Host::MonitorChildProcessCallback &callback,
                  bool monitor_signals) = 0;

protected:
  lldb::process_t m_process;
};
}

#endif
