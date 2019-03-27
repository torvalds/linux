//===-- UnixSignals.cpp -----------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "lldb/Target/UnixSignals.h"
#include "Plugins/Process/Utility/FreeBSDSignals.h"
#include "Plugins/Process/Utility/LinuxSignals.h"
#include "Plugins/Process/Utility/MipsLinuxSignals.h"
#include "Plugins/Process/Utility/NetBSDSignals.h"
#include "lldb/Host/StringConvert.h"
#include "lldb/Utility/ArchSpec.h"

using namespace lldb_private;

UnixSignals::Signal::Signal(const char *name, bool default_suppress,
                            bool default_stop, bool default_notify,
                            const char *description, const char *alias)
    : m_name(name), m_alias(alias), m_description(),
      m_suppress(default_suppress), m_stop(default_stop),
      m_notify(default_notify) {
  if (description)
    m_description.assign(description);
}

lldb::UnixSignalsSP UnixSignals::Create(const ArchSpec &arch) {
  const auto &triple = arch.GetTriple();
  switch (triple.getOS()) {
  case llvm::Triple::Linux: {
    switch (triple.getArch()) {
    case llvm::Triple::mips:
    case llvm::Triple::mipsel:
    case llvm::Triple::mips64:
    case llvm::Triple::mips64el:
      return std::make_shared<MipsLinuxSignals>();
    default:
      return std::make_shared<LinuxSignals>();
    }
  }
  case llvm::Triple::FreeBSD:
  case llvm::Triple::OpenBSD:
    return std::make_shared<FreeBSDSignals>();
  case llvm::Triple::NetBSD:
    return std::make_shared<NetBSDSignals>();
  default:
    return std::make_shared<UnixSignals>();
  }
}

//----------------------------------------------------------------------
// UnixSignals constructor
//----------------------------------------------------------------------
UnixSignals::UnixSignals() { Reset(); }

UnixSignals::UnixSignals(const UnixSignals &rhs) : m_signals(rhs.m_signals) {}

UnixSignals::~UnixSignals() = default;

void UnixSignals::Reset() {
  // This builds one standard set of Unix Signals.  If yours aren't quite in
  // this order, you can either subclass this class, and use Add & Remove to
  // change them
  // or you can subclass and build them afresh in your constructor;
  //
  // Note: the signals below are the Darwin signals.  Do not change these!
  m_signals.clear();
  //        SIGNO  NAME          SUPPRESS STOP   NOTIFY DESCRIPTION
  //        ====== ============  ======== ====== ======
  //        ===================================================
  AddSignal(1, "SIGHUP", false, true, true, "hangup");
  AddSignal(2, "SIGINT", true, true, true, "interrupt");
  AddSignal(3, "SIGQUIT", false, true, true, "quit");
  AddSignal(4, "SIGILL", false, true, true, "illegal instruction");
  AddSignal(5, "SIGTRAP", true, true, true,
            "trace trap (not reset when caught)");
  AddSignal(6, "SIGABRT", false, true, true, "abort()");
  AddSignal(7, "SIGEMT", false, true, true, "pollable event");
  AddSignal(8, "SIGFPE", false, true, true, "floating point exception");
  AddSignal(9, "SIGKILL", false, true, true, "kill");
  AddSignal(10, "SIGBUS", false, true, true, "bus error");
  AddSignal(11, "SIGSEGV", false, true, true, "segmentation violation");
  AddSignal(12, "SIGSYS", false, true, true, "bad argument to system call");
  AddSignal(13, "SIGPIPE", false, false, false,
            "write on a pipe with no one to read it");
  AddSignal(14, "SIGALRM", false, false, false, "alarm clock");
  AddSignal(15, "SIGTERM", false, true, true,
            "software termination signal from kill");
  AddSignal(16, "SIGURG", false, false, false,
            "urgent condition on IO channel");
  AddSignal(17, "SIGSTOP", true, true, true,
            "sendable stop signal not from tty");
  AddSignal(18, "SIGTSTP", false, true, true, "stop signal from tty");
  AddSignal(19, "SIGCONT", false, true, true, "continue a stopped process");
  AddSignal(20, "SIGCHLD", false, false, false,
            "to parent on child stop or exit");
  AddSignal(21, "SIGTTIN", false, true, true,
            "to readers process group upon background tty read");
  AddSignal(22, "SIGTTOU", false, true, true,
            "to readers process group upon background tty write");
  AddSignal(23, "SIGIO", false, false, false, "input/output possible signal");
  AddSignal(24, "SIGXCPU", false, true, true, "exceeded CPU time limit");
  AddSignal(25, "SIGXFSZ", false, true, true, "exceeded file size limit");
  AddSignal(26, "SIGVTALRM", false, false, false, "virtual time alarm");
  AddSignal(27, "SIGPROF", false, false, false, "profiling time alarm");
  AddSignal(28, "SIGWINCH", false, false, false, "window size changes");
  AddSignal(29, "SIGINFO", false, true, true, "information request");
  AddSignal(30, "SIGUSR1", false, true, true, "user defined signal 1");
  AddSignal(31, "SIGUSR2", false, true, true, "user defined signal 2");
}

void UnixSignals::AddSignal(int signo, const char *name, bool default_suppress,
                            bool default_stop, bool default_notify,
                            const char *description, const char *alias) {
  Signal new_signal(name, default_suppress, default_stop, default_notify,
                    description, alias);
  m_signals.insert(std::make_pair(signo, new_signal));
  ++m_version;
}

void UnixSignals::RemoveSignal(int signo) {
  collection::iterator pos = m_signals.find(signo);
  if (pos != m_signals.end())
    m_signals.erase(pos);
  ++m_version;
}

const char *UnixSignals::GetSignalAsCString(int signo) const {
  collection::const_iterator pos = m_signals.find(signo);
  if (pos == m_signals.end())
    return nullptr;
  else
    return pos->second.m_name.GetCString();
}

bool UnixSignals::SignalIsValid(int32_t signo) const {
  return m_signals.find(signo) != m_signals.end();
}

ConstString UnixSignals::GetShortName(ConstString name) const {
  if (name) {
    const char *signame = name.AsCString();
    return ConstString(signame + 3); // Remove "SIG" from name
  }
  return name;
}

int32_t UnixSignals::GetSignalNumberFromName(const char *name) const {
  ConstString const_name(name);

  collection::const_iterator pos, end = m_signals.end();
  for (pos = m_signals.begin(); pos != end; pos++) {
    if ((const_name == pos->second.m_name) ||
        (const_name == pos->second.m_alias) ||
        (const_name == GetShortName(pos->second.m_name)) ||
        (const_name == GetShortName(pos->second.m_alias)))
      return pos->first;
  }

  const int32_t signo =
      StringConvert::ToSInt32(name, LLDB_INVALID_SIGNAL_NUMBER, 0);
  if (signo != LLDB_INVALID_SIGNAL_NUMBER)
    return signo;
  return LLDB_INVALID_SIGNAL_NUMBER;
}

int32_t UnixSignals::GetFirstSignalNumber() const {
  if (m_signals.empty())
    return LLDB_INVALID_SIGNAL_NUMBER;

  return (*m_signals.begin()).first;
}

int32_t UnixSignals::GetNextSignalNumber(int32_t current_signal) const {
  collection::const_iterator pos = m_signals.find(current_signal);
  collection::const_iterator end = m_signals.end();
  if (pos == end)
    return LLDB_INVALID_SIGNAL_NUMBER;
  else {
    pos++;
    if (pos == end)
      return LLDB_INVALID_SIGNAL_NUMBER;
    else
      return pos->first;
  }
}

const char *UnixSignals::GetSignalInfo(int32_t signo, bool &should_suppress,
                                       bool &should_stop,
                                       bool &should_notify) const {
  collection::const_iterator pos = m_signals.find(signo);
  if (pos == m_signals.end())
    return nullptr;
  else {
    const Signal &signal = pos->second;
    should_suppress = signal.m_suppress;
    should_stop = signal.m_stop;
    should_notify = signal.m_notify;
    return signal.m_name.AsCString("");
  }
}

bool UnixSignals::GetShouldSuppress(int signo) const {
  collection::const_iterator pos = m_signals.find(signo);
  if (pos != m_signals.end())
    return pos->second.m_suppress;
  return false;
}

bool UnixSignals::SetShouldSuppress(int signo, bool value) {
  collection::iterator pos = m_signals.find(signo);
  if (pos != m_signals.end()) {
    pos->second.m_suppress = value;
    ++m_version;
    return true;
  }
  return false;
}

bool UnixSignals::SetShouldSuppress(const char *signal_name, bool value) {
  const int32_t signo = GetSignalNumberFromName(signal_name);
  if (signo != LLDB_INVALID_SIGNAL_NUMBER)
    return SetShouldSuppress(signo, value);
  return false;
}

bool UnixSignals::GetShouldStop(int signo) const {
  collection::const_iterator pos = m_signals.find(signo);
  if (pos != m_signals.end())
    return pos->second.m_stop;
  return false;
}

bool UnixSignals::SetShouldStop(int signo, bool value) {
  collection::iterator pos = m_signals.find(signo);
  if (pos != m_signals.end()) {
    pos->second.m_stop = value;
    ++m_version;
    return true;
  }
  return false;
}

bool UnixSignals::SetShouldStop(const char *signal_name, bool value) {
  const int32_t signo = GetSignalNumberFromName(signal_name);
  if (signo != LLDB_INVALID_SIGNAL_NUMBER)
    return SetShouldStop(signo, value);
  return false;
}

bool UnixSignals::GetShouldNotify(int signo) const {
  collection::const_iterator pos = m_signals.find(signo);
  if (pos != m_signals.end())
    return pos->second.m_notify;
  return false;
}

bool UnixSignals::SetShouldNotify(int signo, bool value) {
  collection::iterator pos = m_signals.find(signo);
  if (pos != m_signals.end()) {
    pos->second.m_notify = value;
    ++m_version;
    return true;
  }
  return false;
}

bool UnixSignals::SetShouldNotify(const char *signal_name, bool value) {
  const int32_t signo = GetSignalNumberFromName(signal_name);
  if (signo != LLDB_INVALID_SIGNAL_NUMBER)
    return SetShouldNotify(signo, value);
  return false;
}

int32_t UnixSignals::GetNumSignals() const { return m_signals.size(); }

int32_t UnixSignals::GetSignalAtIndex(int32_t index) const {
  if (index < 0 || m_signals.size() <= static_cast<size_t>(index))
    return LLDB_INVALID_SIGNAL_NUMBER;
  auto it = m_signals.begin();
  std::advance(it, index);
  return it->first;
}

uint64_t UnixSignals::GetVersion() const { return m_version; }

std::vector<int32_t>
UnixSignals::GetFilteredSignals(llvm::Optional<bool> should_suppress,
                                llvm::Optional<bool> should_stop,
                                llvm::Optional<bool> should_notify) {
  std::vector<int32_t> result;
  for (int32_t signo = GetFirstSignalNumber();
       signo != LLDB_INVALID_SIGNAL_NUMBER;
       signo = GetNextSignalNumber(signo)) {

    bool signal_suppress = false;
    bool signal_stop = false;
    bool signal_notify = false;
    GetSignalInfo(signo, signal_suppress, signal_stop, signal_notify);

    // If any of filtering conditions are not met, we move on to the next
    // signal.
    if (should_suppress.hasValue() &&
        signal_suppress != should_suppress.getValue())
      continue;

    if (should_stop.hasValue() && signal_stop != should_stop.getValue())
      continue;

    if (should_notify.hasValue() && signal_notify != should_notify.getValue())
      continue;

    result.push_back(signo);
  }

  return result;
}
