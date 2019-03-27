//===-- SBProcessInfo.cpp ---------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "lldb/API/SBProcessInfo.h"

#include "lldb/API/SBFileSpec.h"
#include "lldb/Target/Process.h"

using namespace lldb;
using namespace lldb_private;

SBProcessInfo::SBProcessInfo() : m_opaque_ap() {}

SBProcessInfo::SBProcessInfo(const SBProcessInfo &rhs) : m_opaque_ap() {
  if (rhs.IsValid()) {
    ref() = *rhs.m_opaque_ap;
  }
}

SBProcessInfo::~SBProcessInfo() {}

SBProcessInfo &SBProcessInfo::operator=(const SBProcessInfo &rhs) {
  if (this != &rhs) {
    if (rhs.IsValid())
      ref() = *rhs.m_opaque_ap;
    else
      m_opaque_ap.reset();
  }
  return *this;
}

ProcessInstanceInfo &SBProcessInfo::ref() {
  if (m_opaque_ap == nullptr) {
    m_opaque_ap.reset(new ProcessInstanceInfo());
  }
  return *m_opaque_ap;
}

void SBProcessInfo::SetProcessInfo(const ProcessInstanceInfo &proc_info_ref) {
  ref() = proc_info_ref;
}

bool SBProcessInfo::IsValid() const { return m_opaque_ap != nullptr; }

const char *SBProcessInfo::GetName() {
  const char *name = nullptr;
  if (m_opaque_ap) {
    name = m_opaque_ap->GetName();
  }
  return name;
}

SBFileSpec SBProcessInfo::GetExecutableFile() {
  SBFileSpec file_spec;
  if (m_opaque_ap) {
    file_spec.SetFileSpec(m_opaque_ap->GetExecutableFile());
  }
  return file_spec;
}

lldb::pid_t SBProcessInfo::GetProcessID() {
  lldb::pid_t proc_id = LLDB_INVALID_PROCESS_ID;
  if (m_opaque_ap) {
    proc_id = m_opaque_ap->GetProcessID();
  }
  return proc_id;
}

uint32_t SBProcessInfo::GetUserID() {
  uint32_t user_id = UINT32_MAX;
  if (m_opaque_ap) {
    user_id = m_opaque_ap->GetUserID();
  }
  return user_id;
}

uint32_t SBProcessInfo::GetGroupID() {
  uint32_t group_id = UINT32_MAX;
  if (m_opaque_ap) {
    group_id = m_opaque_ap->GetGroupID();
  }
  return group_id;
}

bool SBProcessInfo::UserIDIsValid() {
  bool is_valid = false;
  if (m_opaque_ap) {
    is_valid = m_opaque_ap->UserIDIsValid();
  }
  return is_valid;
}

bool SBProcessInfo::GroupIDIsValid() {
  bool is_valid = false;
  if (m_opaque_ap) {
    is_valid = m_opaque_ap->GroupIDIsValid();
  }
  return is_valid;
}

uint32_t SBProcessInfo::GetEffectiveUserID() {
  uint32_t user_id = UINT32_MAX;
  if (m_opaque_ap) {
    user_id = m_opaque_ap->GetEffectiveUserID();
  }
  return user_id;
}

uint32_t SBProcessInfo::GetEffectiveGroupID() {
  uint32_t group_id = UINT32_MAX;
  if (m_opaque_ap) {
    group_id = m_opaque_ap->GetEffectiveGroupID();
  }
  return group_id;
}

bool SBProcessInfo::EffectiveUserIDIsValid() {
  bool is_valid = false;
  if (m_opaque_ap) {
    is_valid = m_opaque_ap->EffectiveUserIDIsValid();
  }
  return is_valid;
}

bool SBProcessInfo::EffectiveGroupIDIsValid() {
  bool is_valid = false;
  if (m_opaque_ap) {
    is_valid = m_opaque_ap->EffectiveGroupIDIsValid();
  }
  return is_valid;
}

lldb::pid_t SBProcessInfo::GetParentProcessID() {
  lldb::pid_t proc_id = LLDB_INVALID_PROCESS_ID;
  if (m_opaque_ap) {
    proc_id = m_opaque_ap->GetParentProcessID();
  }
  return proc_id;
}
