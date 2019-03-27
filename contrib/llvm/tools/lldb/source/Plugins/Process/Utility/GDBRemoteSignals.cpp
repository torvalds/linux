//===-- GDBRemoteSignals.cpp ------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "GDBRemoteSignals.h"

using namespace lldb_private;

GDBRemoteSignals::GDBRemoteSignals() : UnixSignals() { Reset(); }

GDBRemoteSignals::GDBRemoteSignals(const lldb::UnixSignalsSP &rhs)
    : UnixSignals(*rhs) {}

void GDBRemoteSignals::Reset() { m_signals.clear(); }
