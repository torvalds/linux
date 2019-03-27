//===-- MonitoringProcessLauncher.h -----------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef lldb_Host_MonitoringProcessLauncher_h_
#define lldb_Host_MonitoringProcessLauncher_h_

#include <memory>
#include "lldb/Host/ProcessLauncher.h"

namespace lldb_private {

class MonitoringProcessLauncher : public ProcessLauncher {
public:
  explicit MonitoringProcessLauncher(
      std::unique_ptr<ProcessLauncher> delegate_launcher);

  /// Launch the process specified in launch_info. The monitoring callback in
  /// launch_info must be set, and it will be called when the process
  /// terminates.
  HostProcess LaunchProcess(const ProcessLaunchInfo &launch_info,
                            Status &error) override;

private:
  std::unique_ptr<ProcessLauncher> m_delegate_launcher;
};

} // namespace lldb_private

#endif // lldb_Host_MonitoringProcessLauncher_h_
