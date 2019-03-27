//===-- ProcessMessage.cpp --------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "ProcessMessage.h"

using namespace lldb_private;

const char *ProcessMessage::PrintCrashReason() const {
  return CrashReasonAsString(m_crash_reason);
}

const char *ProcessMessage::PrintKind(Kind kind) {
#ifdef LLDB_CONFIGURATION_BUILDANDINTEGRATION
  // Just return the code in ascii for integration builds.
  chcar str[8];
  sprintf(str, "%d", reason);
#else
  const char *str = NULL;

  switch (kind) {
  case eInvalidMessage:
    str = "eInvalidMessage";
    break;
  case eAttachMessage:
    str = "eAttachMessage";
    break;
  case eExitMessage:
    str = "eExitMessage";
    break;
  case eLimboMessage:
    str = "eLimboMessage";
    break;
  case eSignalMessage:
    str = "eSignalMessage";
    break;
  case eSignalDeliveredMessage:
    str = "eSignalDeliveredMessage";
    break;
  case eTraceMessage:
    str = "eTraceMessage";
    break;
  case eBreakpointMessage:
    str = "eBreakpointMessage";
    break;
  case eWatchpointMessage:
    str = "eWatchpointMessage";
    break;
  case eCrashMessage:
    str = "eCrashMessage";
    break;
  case eNewThreadMessage:
    str = "eNewThreadMessage";
    break;
  case eExecMessage:
    str = "eExecMessage";
    break;
  }
#endif

  return str;
}

const char *ProcessMessage::PrintKind() const { return PrintKind(m_kind); }
