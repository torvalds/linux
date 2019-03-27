//===-- GDBRemoteSignals.h --------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef liblldb_GDBRemoteSignals_H_
#define liblldb_GDBRemoteSignals_H_

#include "lldb/Target/UnixSignals.h"

namespace lldb_private {

/// Empty set of Unix signals to be filled by PlatformRemoteGDBServer
class GDBRemoteSignals : public UnixSignals {
public:
  GDBRemoteSignals();

  GDBRemoteSignals(const lldb::UnixSignalsSP &rhs);

private:
  void Reset() override;
};

} // namespace lldb_private

#endif // liblldb_GDBRemoteSignals_H_
