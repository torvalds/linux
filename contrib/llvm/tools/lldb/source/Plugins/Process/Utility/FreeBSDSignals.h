//===-- FreeBSDSignals.h ----------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef liblldb_FreeBSDSignals_H_
#define liblldb_FreeBSDSignals_H_

#include "lldb/Target/UnixSignals.h"

namespace lldb_private {

/// FreeBSD specific set of Unix signals.
class FreeBSDSignals : public UnixSignals {
public:
  FreeBSDSignals();

private:
  void Reset() override;
};

} // namespace lldb_private

#endif // liblldb_FreeBSDSignals_H_
