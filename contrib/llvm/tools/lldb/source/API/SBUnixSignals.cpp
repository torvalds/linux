//===-- SBUnixSignals.cpp -------------------------------------------*- C++
//-*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "lldb/Target/Platform.h"
#include "lldb/Target/Process.h"
#include "lldb/Target/UnixSignals.h"
#include "lldb/Utility/Log.h"
#include "lldb/lldb-defines.h"

#include "lldb/API/SBUnixSignals.h"

using namespace lldb;
using namespace lldb_private;

SBUnixSignals::SBUnixSignals() {}

SBUnixSignals::SBUnixSignals(const SBUnixSignals &rhs)
    : m_opaque_wp(rhs.m_opaque_wp) {}

SBUnixSignals::SBUnixSignals(ProcessSP &process_sp)
    : m_opaque_wp(process_sp ? process_sp->GetUnixSignals() : nullptr) {}

SBUnixSignals::SBUnixSignals(PlatformSP &platform_sp)
    : m_opaque_wp(platform_sp ? platform_sp->GetUnixSignals() : nullptr) {}

const SBUnixSignals &SBUnixSignals::operator=(const SBUnixSignals &rhs) {
  if (this != &rhs)
    m_opaque_wp = rhs.m_opaque_wp;
  return *this;
}

SBUnixSignals::~SBUnixSignals() {}

UnixSignalsSP SBUnixSignals::GetSP() const { return m_opaque_wp.lock(); }

void SBUnixSignals::SetSP(const UnixSignalsSP &signals_sp) {
  m_opaque_wp = signals_sp;
}

void SBUnixSignals::Clear() { m_opaque_wp.reset(); }

bool SBUnixSignals::IsValid() const { return static_cast<bool>(GetSP()); }

const char *SBUnixSignals::GetSignalAsCString(int32_t signo) const {
  if (auto signals_sp = GetSP())
    return signals_sp->GetSignalAsCString(signo);

  return nullptr;
}

int32_t SBUnixSignals::GetSignalNumberFromName(const char *name) const {
  if (auto signals_sp = GetSP())
    return signals_sp->GetSignalNumberFromName(name);

  return LLDB_INVALID_SIGNAL_NUMBER;
}

bool SBUnixSignals::GetShouldSuppress(int32_t signo) const {
  if (auto signals_sp = GetSP())
    return signals_sp->GetShouldSuppress(signo);

  return false;
}

bool SBUnixSignals::SetShouldSuppress(int32_t signo, bool value) {
  Log *log(lldb_private::GetLogIfAllCategoriesSet(LIBLLDB_LOG_API));
  auto signals_sp = GetSP();

  if (log) {
    log->Printf("SBUnixSignals(%p)::SetShouldSuppress (signo=%d, value=%d)",
                static_cast<void *>(signals_sp.get()), signo, value);
  }

  if (signals_sp)
    return signals_sp->SetShouldSuppress(signo, value);

  return false;
}

bool SBUnixSignals::GetShouldStop(int32_t signo) const {
  if (auto signals_sp = GetSP())
    return signals_sp->GetShouldStop(signo);

  return false;
}

bool SBUnixSignals::SetShouldStop(int32_t signo, bool value) {
  Log *log(lldb_private::GetLogIfAllCategoriesSet(LIBLLDB_LOG_API));
  auto signals_sp = GetSP();

  if (log) {
    log->Printf("SBUnixSignals(%p)::SetShouldStop (signo=%d, value=%d)",
                static_cast<void *>(signals_sp.get()), signo, value);
  }

  if (signals_sp)
    return signals_sp->SetShouldStop(signo, value);

  return false;
}

bool SBUnixSignals::GetShouldNotify(int32_t signo) const {
  if (auto signals_sp = GetSP())
    return signals_sp->GetShouldNotify(signo);

  return false;
}

bool SBUnixSignals::SetShouldNotify(int32_t signo, bool value) {
  Log *log(lldb_private::GetLogIfAllCategoriesSet(LIBLLDB_LOG_API));
  auto signals_sp = GetSP();

  if (log) {
    log->Printf("SBUnixSignals(%p)::SetShouldNotify (signo=%d, value=%d)",
                static_cast<void *>(signals_sp.get()), signo, value);
  }

  if (signals_sp)
    return signals_sp->SetShouldNotify(signo, value);

  return false;
}

int32_t SBUnixSignals::GetNumSignals() const {
  if (auto signals_sp = GetSP())
    return signals_sp->GetNumSignals();

  return -1;
}

int32_t SBUnixSignals::GetSignalAtIndex(int32_t index) const {
  if (auto signals_sp = GetSP())
    return signals_sp->GetSignalAtIndex(index);

  return LLDB_INVALID_SIGNAL_NUMBER;
}
