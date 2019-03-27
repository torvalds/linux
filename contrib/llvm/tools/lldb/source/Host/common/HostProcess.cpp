//===-- HostProcess.cpp -----------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "lldb/Host/HostProcess.h"
#include "lldb/Host/HostNativeProcess.h"
#include "lldb/Host/HostThread.h"

using namespace lldb;
using namespace lldb_private;

HostProcess::HostProcess() : m_native_process(new HostNativeProcess) {}

HostProcess::HostProcess(lldb::process_t process)
    : m_native_process(new HostNativeProcess(process)) {}

HostProcess::~HostProcess() {}

Status HostProcess::Terminate() { return m_native_process->Terminate(); }

Status HostProcess::GetMainModule(FileSpec &file_spec) const {
  return m_native_process->GetMainModule(file_spec);
}

lldb::pid_t HostProcess::GetProcessId() const {
  return m_native_process->GetProcessId();
}

bool HostProcess::IsRunning() const { return m_native_process->IsRunning(); }

HostThread
HostProcess::StartMonitoring(const Host::MonitorChildProcessCallback &callback,
                             bool monitor_signals) {
  return m_native_process->StartMonitoring(callback, monitor_signals);
}

HostNativeProcessBase &HostProcess::GetNativeProcess() {
  return *m_native_process;
}

const HostNativeProcessBase &HostProcess::GetNativeProcess() const {
  return *m_native_process;
}
