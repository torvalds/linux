//===-- ProcessLauncher.h ---------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef lldb_Host_ProcessLauncher_h_
#define lldb_Host_ProcessLauncher_h_

namespace lldb_private {

class ProcessLaunchInfo;
class Status;
class HostProcess;

class ProcessLauncher {
public:
  virtual ~ProcessLauncher() {}
  virtual HostProcess LaunchProcess(const ProcessLaunchInfo &launch_info,
                                    Status &error) = 0;
};
}

#endif
