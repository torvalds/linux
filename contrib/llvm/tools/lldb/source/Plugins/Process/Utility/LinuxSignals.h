//===-- LinuxSignals.h ------------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef liblldb_LinuxSignals_H_
#define liblldb_LinuxSignals_H_

#include "lldb/Target/UnixSignals.h"

namespace lldb_private {

/// Linux specific set of Unix signals.
class LinuxSignals : public UnixSignals {
public:
  LinuxSignals();

private:
  void Reset() override;
};

} // namespace lldb_private

#endif // liblldb_LinuxSignals_H_
