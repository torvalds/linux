//===-- SBAttachInfo.cpp ----------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "lldb/API/SBAttachInfo.h"

#include "lldb/API/SBFileSpec.h"
#include "lldb/API/SBListener.h"
#include "lldb/Target/Process.h"

using namespace lldb;
using namespace lldb_private;

SBAttachInfo::SBAttachInfo() : m_opaque_sp(new ProcessAttachInfo()) {}

SBAttachInfo::SBAttachInfo(lldb::pid_t pid)
    : m_opaque_sp(new ProcessAttachInfo()) {
  m_opaque_sp->SetProcessID(pid);
}

SBAttachInfo::SBAttachInfo(const char *path, bool wait_for)
    : m_opaque_sp(new ProcessAttachInfo()) {
  if (path && path[0])
    m_opaque_sp->GetExecutableFile().SetFile(path, FileSpec::Style::native);
  m_opaque_sp->SetWaitForLaunch(wait_for);
}

SBAttachInfo::SBAttachInfo(const char *path, bool wait_for, bool async)
    : m_opaque_sp(new ProcessAttachInfo()) {
  if (path && path[0])
    m_opaque_sp->GetExecutableFile().SetFile(path, FileSpec::Style::native);
  m_opaque_sp->SetWaitForLaunch(wait_for);
  m_opaque_sp->SetAsync(async);
}

SBAttachInfo::SBAttachInfo(const SBAttachInfo &rhs)
    : m_opaque_sp(new ProcessAttachInfo()) {
  *m_opaque_sp = *rhs.m_opaque_sp;
}

SBAttachInfo::~SBAttachInfo() {}

lldb_private::ProcessAttachInfo &SBAttachInfo::ref() { return *m_opaque_sp; }

SBAttachInfo &SBAttachInfo::operator=(const SBAttachInfo &rhs) {
  if (this != &rhs)
    *m_opaque_sp = *rhs.m_opaque_sp;
  return *this;
}

lldb::pid_t SBAttachInfo::GetProcessID() { return m_opaque_sp->GetProcessID(); }

void SBAttachInfo::SetProcessID(lldb::pid_t pid) {
  m_opaque_sp->SetProcessID(pid);
}

uint32_t SBAttachInfo::GetResumeCount() {
  return m_opaque_sp->GetResumeCount();
}

void SBAttachInfo::SetResumeCount(uint32_t c) {
  m_opaque_sp->SetResumeCount(c);
}

const char *SBAttachInfo::GetProcessPluginName() {
  return m_opaque_sp->GetProcessPluginName();
}

void SBAttachInfo::SetProcessPluginName(const char *plugin_name) {
  return m_opaque_sp->SetProcessPluginName(plugin_name);
}

void SBAttachInfo::SetExecutable(const char *path) {
  if (path && path[0])
    m_opaque_sp->GetExecutableFile().SetFile(path, FileSpec::Style::native);
  else
    m_opaque_sp->GetExecutableFile().Clear();
}

void SBAttachInfo::SetExecutable(SBFileSpec exe_file) {
  if (exe_file.IsValid())
    m_opaque_sp->GetExecutableFile() = exe_file.ref();
  else
    m_opaque_sp->GetExecutableFile().Clear();
}

bool SBAttachInfo::GetWaitForLaunch() {
  return m_opaque_sp->GetWaitForLaunch();
}

void SBAttachInfo::SetWaitForLaunch(bool b) {
  m_opaque_sp->SetWaitForLaunch(b);
}

void SBAttachInfo::SetWaitForLaunch(bool b, bool async) {
  m_opaque_sp->SetWaitForLaunch(b);
  m_opaque_sp->SetAsync(async);
}

bool SBAttachInfo::GetIgnoreExisting() {
  return m_opaque_sp->GetIgnoreExisting();
}

void SBAttachInfo::SetIgnoreExisting(bool b) {
  m_opaque_sp->SetIgnoreExisting(b);
}

uint32_t SBAttachInfo::GetUserID() { return m_opaque_sp->GetUserID(); }

uint32_t SBAttachInfo::GetGroupID() { return m_opaque_sp->GetGroupID(); }

bool SBAttachInfo::UserIDIsValid() { return m_opaque_sp->UserIDIsValid(); }

bool SBAttachInfo::GroupIDIsValid() { return m_opaque_sp->GroupIDIsValid(); }

void SBAttachInfo::SetUserID(uint32_t uid) { m_opaque_sp->SetUserID(uid); }

void SBAttachInfo::SetGroupID(uint32_t gid) { m_opaque_sp->SetGroupID(gid); }

uint32_t SBAttachInfo::GetEffectiveUserID() {
  return m_opaque_sp->GetEffectiveUserID();
}

uint32_t SBAttachInfo::GetEffectiveGroupID() {
  return m_opaque_sp->GetEffectiveGroupID();
}

bool SBAttachInfo::EffectiveUserIDIsValid() {
  return m_opaque_sp->EffectiveUserIDIsValid();
}

bool SBAttachInfo::EffectiveGroupIDIsValid() {
  return m_opaque_sp->EffectiveGroupIDIsValid();
}

void SBAttachInfo::SetEffectiveUserID(uint32_t uid) {
  m_opaque_sp->SetEffectiveUserID(uid);
}

void SBAttachInfo::SetEffectiveGroupID(uint32_t gid) {
  m_opaque_sp->SetEffectiveGroupID(gid);
}

lldb::pid_t SBAttachInfo::GetParentProcessID() {
  return m_opaque_sp->GetParentProcessID();
}

void SBAttachInfo::SetParentProcessID(lldb::pid_t pid) {
  m_opaque_sp->SetParentProcessID(pid);
}

bool SBAttachInfo::ParentProcessIDIsValid() {
  return m_opaque_sp->ParentProcessIDIsValid();
}

SBListener SBAttachInfo::GetListener() {
  return SBListener(m_opaque_sp->GetListener());
}

void SBAttachInfo::SetListener(SBListener &listener) {
  m_opaque_sp->SetListener(listener.GetSP());
}
