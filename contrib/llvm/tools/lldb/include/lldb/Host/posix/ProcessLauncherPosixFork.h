//===-- ProcessLauncherPosixFork.h ------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef lldb_Host_posix_ProcessLauncherPosixFork_h_
#define lldb_Host_posix_ProcessLauncherPosixFork_h_

#include "lldb/Host/ProcessLauncher.h"

namespace lldb_private {

class ProcessLauncherPosixFork : public ProcessLauncher {
public:
  HostProcess LaunchProcess(const ProcessLaunchInfo &launch_info,
                            Status &error) override;
};

} // end of namespace lldb_private

#endif
