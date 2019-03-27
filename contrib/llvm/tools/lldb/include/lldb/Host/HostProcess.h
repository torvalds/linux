//===-- HostProcess.h ------------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef lldb_Host_HostProcess_h_
#define lldb_Host_HostProcess_h_

#include "lldb/Host/Host.h"
#include "lldb/lldb-types.h"

//----------------------------------------------------------------------
/// @class HostInfo HostInfo.h "lldb/Host/HostProcess.h"
/// A class that represents a running process on the host machine.
///
/// HostProcess allows querying and manipulation of processes running on the
/// host machine.  It is not intended to be represent a process which is being
/// debugged, although the native debug engine of a platform may likely back
/// inferior processes by a HostProcess.
///
/// HostProcess is implemented using static polymorphism so that on any given
/// platform, an instance of HostProcess will always be able to bind
/// statically to the concrete Process implementation for that platform.  See
/// HostInfo for more details.
///
//----------------------------------------------------------------------

namespace lldb_private {

class HostNativeProcessBase;
class HostThread;

class HostProcess {
public:
  HostProcess();
  HostProcess(lldb::process_t process);
  ~HostProcess();

  Status Terminate();
  Status GetMainModule(FileSpec &file_spec) const;

  lldb::pid_t GetProcessId() const;
  bool IsRunning() const;

  HostThread StartMonitoring(const Host::MonitorChildProcessCallback &callback,
                             bool monitor_signals);

  HostNativeProcessBase &GetNativeProcess();
  const HostNativeProcessBase &GetNativeProcess() const;

private:
  std::shared_ptr<HostNativeProcessBase> m_native_process;
};
}

#endif
