//===-- SBBreakpointName.cpp ----------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "lldb/API/SBBreakpointName.h"
#include "lldb/API/SBDebugger.h"
#include "lldb/API/SBError.h"
#include "lldb/API/SBStream.h"
#include "lldb/API/SBStringList.h"
#include "lldb/API/SBTarget.h"

#include "lldb/Breakpoint/BreakpointName.h"
#include "lldb/Breakpoint/StoppointCallbackContext.h"
#include "lldb/Core/Debugger.h"
#include "lldb/Interpreter/CommandInterpreter.h"
#include "lldb/Interpreter/ScriptInterpreter.h"
#include "lldb/Target/Target.h"
#include "lldb/Target/ThreadSpec.h"
#include "lldb/Utility/Log.h"
#include "lldb/Utility/Stream.h"

#include "SBBreakpointOptionCommon.h"

using namespace lldb;
using namespace lldb_private;

namespace lldb
{
class SBBreakpointNameImpl {
public:
  SBBreakpointNameImpl(TargetSP target_sp, const char *name) {
    if (!name || name[0] == '\0')
      return;
    m_name.assign(name);
    
    if (!target_sp)
      return;
    
    m_target_wp = target_sp;
  }

  SBBreakpointNameImpl(SBTarget &sb_target, const char *name);
  bool operator==(const SBBreakpointNameImpl &rhs);
  bool operator!=(const SBBreakpointNameImpl &rhs);

  // For now we take a simple approach and only keep the name, and relook up
  // the location when we need it.
  
  TargetSP GetTarget() const {
    return m_target_wp.lock();
  }
  
  const char *GetName() const {
    return m_name.c_str();
  }
  
  bool IsValid() const {
    return !m_name.empty() && m_target_wp.lock();
  }

  lldb_private::BreakpointName *GetBreakpointName() const;

private:
  TargetWP m_target_wp;
  std::string m_name;
};

SBBreakpointNameImpl::SBBreakpointNameImpl(SBTarget &sb_target,
                                           const char *name) {
  if (!name || name[0] == '\0')
    return;
  m_name.assign(name);

  if (!sb_target.IsValid())
    return;

  TargetSP target_sp = sb_target.GetSP();
  if (!target_sp)
    return;

  m_target_wp = target_sp;
}

bool SBBreakpointNameImpl::operator==(const SBBreakpointNameImpl &rhs) {
  return m_name == rhs.m_name && m_target_wp.lock() == rhs.m_target_wp.lock();
}

bool SBBreakpointNameImpl::operator!=(const SBBreakpointNameImpl &rhs) {
  return m_name != rhs.m_name || m_target_wp.lock() != rhs.m_target_wp.lock();
}

lldb_private::BreakpointName *SBBreakpointNameImpl::GetBreakpointName() const {
  if (!IsValid())
    return nullptr;
  TargetSP target_sp = GetTarget();
  if (!target_sp)
    return nullptr;
  Status error;
  return target_sp->FindBreakpointName(ConstString(m_name), true, error);
}

} // namespace lldb

SBBreakpointName::SBBreakpointName() {}

SBBreakpointName::SBBreakpointName(SBTarget &sb_target, const char *name)
{
  m_impl_up.reset(new SBBreakpointNameImpl(sb_target, name));
  // Call FindBreakpointName here to make sure the name is valid, reset if not:
  BreakpointName *bp_name = GetBreakpointName();
  if (!bp_name)
    m_impl_up.reset();
}

SBBreakpointName::SBBreakpointName(SBBreakpoint &sb_bkpt, const char *name)
{
  if (!sb_bkpt.IsValid()) {
    m_impl_up.reset();
    return;
  }
  BreakpointSP bkpt_sp = sb_bkpt.GetSP();
  Target &target = bkpt_sp->GetTarget();

  m_impl_up.reset(new SBBreakpointNameImpl(target.shared_from_this(), name));
  
  // Call FindBreakpointName here to make sure the name is valid, reset if not:
  BreakpointName *bp_name = GetBreakpointName();
  if (!bp_name) {
    m_impl_up.reset();
    return;
  }
  
  // Now copy over the breakpoint's options:
  target.ConfigureBreakpointName(*bp_name, *bkpt_sp->GetOptions(),
                                 BreakpointName::Permissions());
}

SBBreakpointName::SBBreakpointName(const SBBreakpointName &rhs)
{
  if (!rhs.m_impl_up)
    return;
  else
    m_impl_up.reset(new SBBreakpointNameImpl(rhs.m_impl_up->GetTarget(),
                                             rhs.m_impl_up->GetName()));
}

SBBreakpointName::~SBBreakpointName() = default;

const SBBreakpointName &SBBreakpointName::operator=(const SBBreakpointName &rhs) 
{
  if (!rhs.m_impl_up) {
    m_impl_up.reset();
    return *this;
  }
  
  m_impl_up.reset(new SBBreakpointNameImpl(rhs.m_impl_up->GetTarget(),
                                           rhs.m_impl_up->GetName()));
  return *this;
}

bool SBBreakpointName::operator==(const lldb::SBBreakpointName &rhs) {
  return *m_impl_up == *rhs.m_impl_up;
}

bool SBBreakpointName::operator!=(const lldb::SBBreakpointName &rhs) {
  return *m_impl_up != *rhs.m_impl_up;
}

bool SBBreakpointName::IsValid() const {
  if (!m_impl_up)
    return false;
  return m_impl_up->IsValid();
}

const char *SBBreakpointName::GetName() const {
  if (!m_impl_up)
    return "<Invalid Breakpoint Name Object>";
  return m_impl_up->GetName();
}

void SBBreakpointName::SetEnabled(bool enable) {
  Log *log(lldb_private::GetLogIfAllCategoriesSet(LIBLLDB_LOG_API));
  
  BreakpointName *bp_name = GetBreakpointName();
  if (!bp_name)
    return;
 
  LLDB_LOG(log, "Name: {0} enabled: {1}\n", bp_name->GetName(), enable);
  std::lock_guard<std::recursive_mutex> guard(
        m_impl_up->GetTarget()->GetAPIMutex());

  bp_name->GetOptions().SetEnabled(enable);
}

void SBBreakpointName::UpdateName(BreakpointName &bp_name) {
  if (!IsValid())
    return;

  TargetSP target_sp = m_impl_up->GetTarget();
  if (!target_sp)
    return;
  target_sp->ApplyNameToBreakpoints(bp_name);

}

bool SBBreakpointName::IsEnabled() {
  Log *log(lldb_private::GetLogIfAllCategoriesSet(LIBLLDB_LOG_API));
  
  BreakpointName *bp_name = GetBreakpointName();
  if (!bp_name)
    return false;
 
  LLDB_LOG(log, "Name: {0}\n", bp_name->GetName());
  std::lock_guard<std::recursive_mutex> guard(
        m_impl_up->GetTarget()->GetAPIMutex());

  return bp_name->GetOptions().IsEnabled();
}

void SBBreakpointName::SetOneShot(bool one_shot) {
  Log *log(lldb_private::GetLogIfAllCategoriesSet(LIBLLDB_LOG_API));
  
  BreakpointName *bp_name = GetBreakpointName();
  if (!bp_name)
    return;
 
  LLDB_LOG(log, "Name: {0} one_shot: {1}\n", bp_name->GetName(), one_shot);
  std::lock_guard<std::recursive_mutex> guard(
        m_impl_up->GetTarget()->GetAPIMutex());

  bp_name->GetOptions().SetOneShot(one_shot);
  UpdateName(*bp_name);
}

bool SBBreakpointName::IsOneShot() const {
  Log *log(lldb_private::GetLogIfAllCategoriesSet(LIBLLDB_LOG_API));
  
  const BreakpointName *bp_name = GetBreakpointName();
  if (!bp_name)
    return false;
 
  LLDB_LOG(log, "Name: {0}\n", bp_name->GetName());
  std::lock_guard<std::recursive_mutex> guard(
        m_impl_up->GetTarget()->GetAPIMutex());

  return bp_name->GetOptions().IsOneShot();
}

void SBBreakpointName::SetIgnoreCount(uint32_t count) {
  Log *log(lldb_private::GetLogIfAllCategoriesSet(LIBLLDB_LOG_API));
  
  BreakpointName *bp_name = GetBreakpointName();
  if (!bp_name)
    return;
 
  LLDB_LOG(log, "Name: {0} one_shot: {1}\n", bp_name->GetName(), count);
  std::lock_guard<std::recursive_mutex> guard(
        m_impl_up->GetTarget()->GetAPIMutex());

  bp_name->GetOptions().SetIgnoreCount(count);
  UpdateName(*bp_name);
}

uint32_t SBBreakpointName::GetIgnoreCount() const {
  Log *log(lldb_private::GetLogIfAllCategoriesSet(LIBLLDB_LOG_API));
  
  BreakpointName *bp_name = GetBreakpointName();
  if (!bp_name)
    return false;
 
  LLDB_LOG(log, "Name: {0}\n", bp_name->GetName());
  std::lock_guard<std::recursive_mutex> guard(
        m_impl_up->GetTarget()->GetAPIMutex());

  return bp_name->GetOptions().GetIgnoreCount();
}

void SBBreakpointName::SetCondition(const char *condition) {
  Log *log(lldb_private::GetLogIfAllCategoriesSet(LIBLLDB_LOG_API));
  
  BreakpointName *bp_name = GetBreakpointName();
  if (!bp_name)
    return;
 
  LLDB_LOG(log, "Name: {0} one_shot: {1}\n", bp_name->GetName(),
          condition ? condition : "<NULL>");
  
  std::lock_guard<std::recursive_mutex> guard(
        m_impl_up->GetTarget()->GetAPIMutex());

  bp_name->GetOptions().SetCondition(condition);
  UpdateName(*bp_name);
}

const char *SBBreakpointName::GetCondition() {
  Log *log(lldb_private::GetLogIfAllCategoriesSet(LIBLLDB_LOG_API));
  
  BreakpointName *bp_name = GetBreakpointName();
  if (!bp_name)
    return nullptr;
 
  LLDB_LOG(log, "Name: {0}\n", bp_name->GetName());
  std::lock_guard<std::recursive_mutex> guard(
        m_impl_up->GetTarget()->GetAPIMutex());

  return bp_name->GetOptions().GetConditionText();
}

void SBBreakpointName::SetAutoContinue(bool auto_continue) {
  Log *log(lldb_private::GetLogIfAllCategoriesSet(LIBLLDB_LOG_API));
  
  BreakpointName *bp_name = GetBreakpointName();
  if (!bp_name)
    return;
 
  LLDB_LOG(log, "Name: {0} auto-continue: {1}\n", bp_name->GetName(), auto_continue);
  
  std::lock_guard<std::recursive_mutex> guard(
        m_impl_up->GetTarget()->GetAPIMutex());

  bp_name->GetOptions().SetAutoContinue(auto_continue);
  UpdateName(*bp_name);
}

bool SBBreakpointName::GetAutoContinue() {
  Log *log(lldb_private::GetLogIfAllCategoriesSet(LIBLLDB_LOG_API));
  
  BreakpointName *bp_name = GetBreakpointName();
  if (!bp_name)
    return false;
 
  LLDB_LOG(log, "Name: {0}\n", bp_name->GetName());
  std::lock_guard<std::recursive_mutex> guard(
        m_impl_up->GetTarget()->GetAPIMutex());

  return bp_name->GetOptions().IsAutoContinue();
}

void SBBreakpointName::SetThreadID(tid_t tid) {
  Log *log(lldb_private::GetLogIfAllCategoriesSet(LIBLLDB_LOG_API));
  
  BreakpointName *bp_name = GetBreakpointName();
  if (!bp_name)
    return;
 
  LLDB_LOG(log, "Name: {0} tid: {1:x}\n", bp_name->GetName(), tid);
  
  std::lock_guard<std::recursive_mutex> guard(
        m_impl_up->GetTarget()->GetAPIMutex());

  bp_name->GetOptions().SetThreadID(tid);
  UpdateName(*bp_name);
}

tid_t SBBreakpointName::GetThreadID() {
  Log *log(lldb_private::GetLogIfAllCategoriesSet(LIBLLDB_LOG_API));
  
  BreakpointName *bp_name = GetBreakpointName();
  if (!bp_name)
    return LLDB_INVALID_THREAD_ID;
 
  LLDB_LOG(log, "Name: {0}\n", bp_name->GetName());
  std::lock_guard<std::recursive_mutex> guard(
        m_impl_up->GetTarget()->GetAPIMutex());

  return bp_name->GetOptions().GetThreadSpec()->GetTID();
}

void SBBreakpointName::SetThreadIndex(uint32_t index) {
  Log *log(lldb_private::GetLogIfAllCategoriesSet(LIBLLDB_LOG_API));
  
  BreakpointName *bp_name = GetBreakpointName();
  if (!bp_name)
    return;
 
  LLDB_LOG(log, "Name: {0} thread index: {1}\n", bp_name->GetName(), index);
  
  std::lock_guard<std::recursive_mutex> guard(
        m_impl_up->GetTarget()->GetAPIMutex());

  bp_name->GetOptions().GetThreadSpec()->SetIndex(index);
  UpdateName(*bp_name);
}

uint32_t SBBreakpointName::GetThreadIndex() const {
  Log *log(lldb_private::GetLogIfAllCategoriesSet(LIBLLDB_LOG_API));
  
  BreakpointName *bp_name = GetBreakpointName();
  if (!bp_name)
    return LLDB_INVALID_THREAD_ID;
 
  LLDB_LOG(log, "Name: {0}\n", bp_name->GetName());
  std::lock_guard<std::recursive_mutex> guard(
        m_impl_up->GetTarget()->GetAPIMutex());

  return bp_name->GetOptions().GetThreadSpec()->GetIndex();
}

void SBBreakpointName::SetThreadName(const char *thread_name) {
  Log *log(lldb_private::GetLogIfAllCategoriesSet(LIBLLDB_LOG_API));
  
  BreakpointName *bp_name = GetBreakpointName();
  if (!bp_name)
    return;
 
  LLDB_LOG(log, "Name: {0} thread name: {1}\n", bp_name->GetName(), thread_name);
  
  std::lock_guard<std::recursive_mutex> guard(
        m_impl_up->GetTarget()->GetAPIMutex());

  bp_name->GetOptions().GetThreadSpec()->SetName(thread_name);
  UpdateName(*bp_name);
}

const char *SBBreakpointName::GetThreadName() const {
  Log *log(lldb_private::GetLogIfAllCategoriesSet(LIBLLDB_LOG_API));
  
  BreakpointName *bp_name = GetBreakpointName();
  if (!bp_name)
    return nullptr;
 
  LLDB_LOG(log, "Name: {0}\n", bp_name->GetName());
  std::lock_guard<std::recursive_mutex> guard(
        m_impl_up->GetTarget()->GetAPIMutex());

  return bp_name->GetOptions().GetThreadSpec()->GetName();
}

void SBBreakpointName::SetQueueName(const char *queue_name) {
  Log *log(lldb_private::GetLogIfAllCategoriesSet(LIBLLDB_LOG_API));
  
  BreakpointName *bp_name = GetBreakpointName();
  if (!bp_name)
    return;
 
  LLDB_LOG(log, "Name: {0} queue name: {1}\n", bp_name->GetName(), queue_name);
  
  std::lock_guard<std::recursive_mutex> guard(
        m_impl_up->GetTarget()->GetAPIMutex());

  bp_name->GetOptions().GetThreadSpec()->SetQueueName(queue_name);
  UpdateName(*bp_name);
}

const char *SBBreakpointName::GetQueueName() const {
  Log *log(lldb_private::GetLogIfAllCategoriesSet(LIBLLDB_LOG_API));
  
  BreakpointName *bp_name = GetBreakpointName();
  if (!bp_name)
    return nullptr;
 
  LLDB_LOG(log, "Name: {0}\n", bp_name->GetName());
  std::lock_guard<std::recursive_mutex> guard(
        m_impl_up->GetTarget()->GetAPIMutex());

  return bp_name->GetOptions().GetThreadSpec()->GetQueueName();
}

void SBBreakpointName::SetCommandLineCommands(SBStringList &commands) {
  Log *log(lldb_private::GetLogIfAllCategoriesSet(LIBLLDB_LOG_API));
  BreakpointName *bp_name = GetBreakpointName();
  if (!bp_name)
    return;
  if (commands.GetSize() == 0)
    return;

  LLDB_LOG(log, "Name: {0} commands\n", bp_name->GetName());

  std::lock_guard<std::recursive_mutex> guard(
        m_impl_up->GetTarget()->GetAPIMutex());
  std::unique_ptr<BreakpointOptions::CommandData> cmd_data_up(
      new BreakpointOptions::CommandData(*commands, eScriptLanguageNone));

  bp_name->GetOptions().SetCommandDataCallback(cmd_data_up);
  UpdateName(*bp_name);
}

bool SBBreakpointName::GetCommandLineCommands(SBStringList &commands) {
  Log *log(lldb_private::GetLogIfAllCategoriesSet(LIBLLDB_LOG_API));
  
  BreakpointName *bp_name = GetBreakpointName();
  if (!bp_name)
    return false;
 
  LLDB_LOG(log, "Name: {0}\n", bp_name->GetName());
  StringList command_list;
  bool has_commands =
      bp_name->GetOptions().GetCommandLineCallbacks(command_list);
  if (has_commands)
    commands.AppendList(command_list);
  return has_commands;
}

const char *SBBreakpointName::GetHelpString() const {
  Log *log(lldb_private::GetLogIfAllCategoriesSet(LIBLLDB_LOG_API));
  
  BreakpointName *bp_name = GetBreakpointName();
  if (!bp_name)
    return "";
 
  LLDB_LOG(log, "Help: {0}\n", bp_name->GetHelp());
  return bp_name->GetHelp();
}

void SBBreakpointName::SetHelpString(const char *help_string) {
  Log *log(lldb_private::GetLogIfAllCategoriesSet(LIBLLDB_LOG_API));
  BreakpointName *bp_name = GetBreakpointName();
  if (!bp_name)
    return;

  LLDB_LOG(log, "Name: {0} help: {1}\n", bp_name->GetName(), help_string);

  std::lock_guard<std::recursive_mutex> guard(
        m_impl_up->GetTarget()->GetAPIMutex());
  bp_name->SetHelp(help_string);
}

bool SBBreakpointName::GetDescription(SBStream &s) {
  Log *log(lldb_private::GetLogIfAllCategoriesSet(LIBLLDB_LOG_API));
  
  BreakpointName *bp_name = GetBreakpointName();
  if (!bp_name)
  {
    s.Printf("No value");
    return false;
  }
 
  LLDB_LOG(log, "Name: {0}\n", bp_name->GetName());
  std::lock_guard<std::recursive_mutex> guard(
        m_impl_up->GetTarget()->GetAPIMutex());
  bp_name->GetDescription(s.get(), eDescriptionLevelFull);
  return true;
}

void SBBreakpointName::SetCallback(SBBreakpointHitCallback callback,
                                   void *baton) {
  Log *log(lldb_private::GetLogIfAllCategoriesSet(LIBLLDB_LOG_API));
  BreakpointName *bp_name = GetBreakpointName();
  if (!bp_name)
    return;
  LLDB_LOG(log, "callback = {1}, baton = {2}", callback, baton);
  std::lock_guard<std::recursive_mutex> guard(
        m_impl_up->GetTarget()->GetAPIMutex());

  BatonSP baton_sp(new SBBreakpointCallbackBaton(callback, baton));
  bp_name->GetOptions().SetCallback(SBBreakpointCallbackBaton
                                       ::PrivateBreakpointHitCallback,
                                    baton_sp,
                                    false);
  UpdateName(*bp_name);
}

void SBBreakpointName::SetScriptCallbackFunction(
    const char *callback_function_name) {
  Log *log(lldb_private::GetLogIfAllCategoriesSet(LIBLLDB_LOG_API));
  
  BreakpointName *bp_name = GetBreakpointName();
  if (!bp_name)
    return;
 
  LLDB_LOG(log, "Name: {0} callback: {1}\n", bp_name->GetName(),
           callback_function_name);
  
  std::lock_guard<std::recursive_mutex> guard(
        m_impl_up->GetTarget()->GetAPIMutex());

  BreakpointOptions &bp_options = bp_name->GetOptions();
  m_impl_up->GetTarget()
      ->GetDebugger()
      .GetCommandInterpreter()
      .GetScriptInterpreter()
      ->SetBreakpointCommandCallbackFunction(&bp_options,
                                             callback_function_name);
  UpdateName(*bp_name);
}

SBError SBBreakpointName::SetScriptCallbackBody(const char *callback_body_text)
{
  Log *log(lldb_private::GetLogIfAllCategoriesSet(LIBLLDB_LOG_API));
  SBError sb_error;
  BreakpointName *bp_name = GetBreakpointName();
  if (!bp_name)
    return sb_error;
 
  LLDB_LOG(log, "Name: {0} callback: {1}\n", bp_name->GetName(),
           callback_body_text);
  
  std::lock_guard<std::recursive_mutex> guard(
        m_impl_up->GetTarget()->GetAPIMutex());

  BreakpointOptions &bp_options = bp_name->GetOptions();
  Status error =
      m_impl_up->GetTarget()
          ->GetDebugger()
          .GetCommandInterpreter()
          .GetScriptInterpreter()
          ->SetBreakpointCommandCallback(&bp_options, callback_body_text);
  sb_error.SetError(error);
  if (!sb_error.Fail())
    UpdateName(*bp_name);

  return sb_error;
}

bool SBBreakpointName::GetAllowList() const
{
  BreakpointName *bp_name = GetBreakpointName();
  if (!bp_name)
    return false;
  return bp_name->GetPermissions().GetAllowList();
}

void SBBreakpointName::SetAllowList(bool value)
{
  Log *log(lldb_private::GetLogIfAllCategoriesSet(LIBLLDB_LOG_API));

  BreakpointName *bp_name = GetBreakpointName();
  if (!bp_name)
    return;
  if (log)
    log->Printf("Setting allow list to %u for %s.", value, 
                bp_name->GetName().AsCString());
  bp_name->GetPermissions().SetAllowList(value);
}
  
bool SBBreakpointName::GetAllowDelete()
{
  BreakpointName *bp_name = GetBreakpointName();
  if (!bp_name)
    return false;
  return bp_name->GetPermissions().GetAllowDelete();
}

void SBBreakpointName::SetAllowDelete(bool value)
{
  Log *log(lldb_private::GetLogIfAllCategoriesSet(LIBLLDB_LOG_API));

  BreakpointName *bp_name = GetBreakpointName();
  if (!bp_name)
    return;
  if (log)
    log->Printf("Setting allow delete to %u for %s.", value, 
                bp_name->GetName().AsCString());
  bp_name->GetPermissions().SetAllowDelete(value);
}
  
bool SBBreakpointName::GetAllowDisable()
{
  BreakpointName *bp_name = GetBreakpointName();
  if (!bp_name)
    return false;
  return bp_name->GetPermissions().GetAllowDisable();
}

void SBBreakpointName::SetAllowDisable(bool value)
{
  Log *log(lldb_private::GetLogIfAllCategoriesSet(LIBLLDB_LOG_API));

  BreakpointName *bp_name = GetBreakpointName();
  if (!bp_name)
    return;
  if (log)
    log->Printf("Setting allow disable to %u for %s.", value, 
                bp_name->GetName().AsCString());
  bp_name->GetPermissions().SetAllowDisable(value);
}

lldb_private::BreakpointName *SBBreakpointName::GetBreakpointName() const
{
  if (!IsValid())
    return nullptr;
  return m_impl_up->GetBreakpointName();
}

