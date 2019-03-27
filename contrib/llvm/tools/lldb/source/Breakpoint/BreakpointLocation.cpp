//===-- BreakpointLocation.cpp ----------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "lldb/Breakpoint/BreakpointLocation.h"
#include "lldb/Breakpoint/BreakpointID.h"
#include "lldb/Breakpoint/StoppointCallbackContext.h"
#include "lldb/Core/Debugger.h"
#include "lldb/Core/Module.h"
#include "lldb/Core/ValueObject.h"
#include "lldb/Expression/DiagnosticManager.h"
#include "lldb/Expression/ExpressionVariable.h"
#include "lldb/Expression/UserExpression.h"
#include "lldb/Symbol/CompileUnit.h"
#include "lldb/Symbol/Symbol.h"
#include "lldb/Symbol/TypeSystem.h"
#include "lldb/Target/Process.h"
#include "lldb/Target/Target.h"
#include "lldb/Target/Thread.h"
#include "lldb/Target/ThreadSpec.h"
#include "lldb/Utility/Log.h"
#include "lldb/Utility/StreamString.h"

using namespace lldb;
using namespace lldb_private;

BreakpointLocation::BreakpointLocation(break_id_t loc_id, Breakpoint &owner,
                                       const Address &addr, lldb::tid_t tid,
                                       bool hardware, bool check_for_resolver)
    : StoppointLocation(loc_id, addr.GetOpcodeLoadAddress(&owner.GetTarget()),
                        hardware),
      m_being_created(true), m_should_resolve_indirect_functions(false),
      m_is_reexported(false), m_is_indirect(false), m_address(addr),
      m_owner(owner), m_options_ap(), m_bp_site_sp(), m_condition_mutex() {
  if (check_for_resolver) {
    Symbol *symbol = m_address.CalculateSymbolContextSymbol();
    if (symbol && symbol->IsIndirect()) {
      SetShouldResolveIndirectFunctions(true);
    }
  }

  SetThreadID(tid);
  m_being_created = false;
}

BreakpointLocation::~BreakpointLocation() { ClearBreakpointSite(); }

lldb::addr_t BreakpointLocation::GetLoadAddress() const {
  return m_address.GetOpcodeLoadAddress(&m_owner.GetTarget());
}

const BreakpointOptions *
BreakpointLocation::GetOptionsSpecifyingKind(BreakpointOptions::OptionKind kind)
const {
    if (m_options_ap && m_options_ap->IsOptionSet(kind))
      return m_options_ap.get();
    else
      return m_owner.GetOptions();
}

Address &BreakpointLocation::GetAddress() { return m_address; }

Breakpoint &BreakpointLocation::GetBreakpoint() { return m_owner; }

Target &BreakpointLocation::GetTarget() { return m_owner.GetTarget(); }

bool BreakpointLocation::IsEnabled() const {
  if (!m_owner.IsEnabled())
    return false;
  else if (m_options_ap.get() != nullptr)
    return m_options_ap->IsEnabled();
  else
    return true;
}

void BreakpointLocation::SetEnabled(bool enabled) {
  GetLocationOptions()->SetEnabled(enabled);
  if (enabled) {
    ResolveBreakpointSite();
  } else {
    ClearBreakpointSite();
  }
  SendBreakpointLocationChangedEvent(enabled ? eBreakpointEventTypeEnabled
                                             : eBreakpointEventTypeDisabled);
}

bool BreakpointLocation::IsAutoContinue() const {
  if (m_options_ap 
      && m_options_ap->IsOptionSet(BreakpointOptions::eAutoContinue))
    return m_options_ap->IsAutoContinue();
  else
    return m_owner.IsAutoContinue();
}

void BreakpointLocation::SetAutoContinue(bool auto_continue) {
  GetLocationOptions()->SetAutoContinue(auto_continue);
  SendBreakpointLocationChangedEvent(eBreakpointEventTypeAutoContinueChanged);
}

void BreakpointLocation::SetThreadID(lldb::tid_t thread_id) {
  if (thread_id != LLDB_INVALID_THREAD_ID)
    GetLocationOptions()->SetThreadID(thread_id);
  else {
    // If we're resetting this to an invalid thread id, then don't make an
    // options pointer just to do that.
    if (m_options_ap.get() != nullptr)
      m_options_ap->SetThreadID(thread_id);
  }
  SendBreakpointLocationChangedEvent(eBreakpointEventTypeThreadChanged);
}

lldb::tid_t BreakpointLocation::GetThreadID() {
  const ThreadSpec *thread_spec = 
      GetOptionsSpecifyingKind(BreakpointOptions::eThreadSpec)
          ->GetThreadSpecNoCreate();
  if (thread_spec)
    return thread_spec->GetTID();
  else
    return LLDB_INVALID_THREAD_ID;
}

void BreakpointLocation::SetThreadIndex(uint32_t index) {
  if (index != 0)
    GetLocationOptions()->GetThreadSpec()->SetIndex(index);
  else {
    // If we're resetting this to an invalid thread id, then don't make an
    // options pointer just to do that.
    if (m_options_ap.get() != nullptr)
      m_options_ap->GetThreadSpec()->SetIndex(index);
  }
  SendBreakpointLocationChangedEvent(eBreakpointEventTypeThreadChanged);
}

uint32_t BreakpointLocation::GetThreadIndex() const {
  const ThreadSpec *thread_spec = 
      GetOptionsSpecifyingKind(BreakpointOptions::eThreadSpec)
          ->GetThreadSpecNoCreate();
  if (thread_spec)
    return thread_spec->GetIndex();
  else
    return 0;
}

void BreakpointLocation::SetThreadName(const char *thread_name) {
  if (thread_name != nullptr)
    GetLocationOptions()->GetThreadSpec()->SetName(thread_name);
  else {
    // If we're resetting this to an invalid thread id, then don't make an
    // options pointer just to do that.
    if (m_options_ap.get() != nullptr)
      m_options_ap->GetThreadSpec()->SetName(thread_name);
  }
  SendBreakpointLocationChangedEvent(eBreakpointEventTypeThreadChanged);
}

const char *BreakpointLocation::GetThreadName() const {
  const ThreadSpec *thread_spec = 
      GetOptionsSpecifyingKind(BreakpointOptions::eThreadSpec)
          ->GetThreadSpecNoCreate();
  if (thread_spec)
    return thread_spec->GetName();
  else
    return nullptr;
}

void BreakpointLocation::SetQueueName(const char *queue_name) {
  if (queue_name != nullptr)
    GetLocationOptions()->GetThreadSpec()->SetQueueName(queue_name);
  else {
    // If we're resetting this to an invalid thread id, then don't make an
    // options pointer just to do that.
    if (m_options_ap.get() != nullptr)
      m_options_ap->GetThreadSpec()->SetQueueName(queue_name);
  }
  SendBreakpointLocationChangedEvent(eBreakpointEventTypeThreadChanged);
}

const char *BreakpointLocation::GetQueueName() const {
  const ThreadSpec *thread_spec = 
      GetOptionsSpecifyingKind(BreakpointOptions::eThreadSpec)
          ->GetThreadSpecNoCreate();
  if (thread_spec)
    return thread_spec->GetQueueName();
  else
    return nullptr;
}

bool BreakpointLocation::InvokeCallback(StoppointCallbackContext *context) {
  if (m_options_ap.get() != nullptr && m_options_ap->HasCallback())
    return m_options_ap->InvokeCallback(context, m_owner.GetID(), GetID());
  else
    return m_owner.InvokeCallback(context, GetID());
}

void BreakpointLocation::SetCallback(BreakpointHitCallback callback,
                                     void *baton, bool is_synchronous) {
  // The default "Baton" class will keep a copy of "baton" and won't free or
  // delete it when it goes goes out of scope.
  GetLocationOptions()->SetCallback(
      callback, std::make_shared<UntypedBaton>(baton), is_synchronous);
  SendBreakpointLocationChangedEvent(eBreakpointEventTypeCommandChanged);
}

void BreakpointLocation::SetCallback(BreakpointHitCallback callback,
                                     const BatonSP &baton_sp,
                                     bool is_synchronous) {
  GetLocationOptions()->SetCallback(callback, baton_sp, is_synchronous);
  SendBreakpointLocationChangedEvent(eBreakpointEventTypeCommandChanged);
}

void BreakpointLocation::ClearCallback() {
  GetLocationOptions()->ClearCallback();
}

void BreakpointLocation::SetCondition(const char *condition) {
  GetLocationOptions()->SetCondition(condition);
  SendBreakpointLocationChangedEvent(eBreakpointEventTypeConditionChanged);
}

const char *BreakpointLocation::GetConditionText(size_t *hash) const {
  return GetOptionsSpecifyingKind(BreakpointOptions::eCondition)
      ->GetConditionText(hash);
}

bool BreakpointLocation::ConditionSaysStop(ExecutionContext &exe_ctx,
                                           Status &error) {
  Log *log = lldb_private::GetLogIfAllCategoriesSet(LIBLLDB_LOG_BREAKPOINTS);

  std::lock_guard<std::mutex> guard(m_condition_mutex);

  size_t condition_hash;
  const char *condition_text = GetConditionText(&condition_hash);

  if (!condition_text) {
    m_user_expression_sp.reset();
    return false;
  }

  error.Clear();

  DiagnosticManager diagnostics;

  if (condition_hash != m_condition_hash || !m_user_expression_sp ||
      !m_user_expression_sp->MatchesContext(exe_ctx)) {
    LanguageType language = eLanguageTypeUnknown;
    // See if we can figure out the language from the frame, otherwise use the
    // default language:
    CompileUnit *comp_unit = m_address.CalculateSymbolContextCompileUnit();
    if (comp_unit)
      language = comp_unit->GetLanguage();

    m_user_expression_sp.reset(GetTarget().GetUserExpressionForLanguage(
        condition_text, llvm::StringRef(), language, Expression::eResultTypeAny,
        EvaluateExpressionOptions(), error));
    if (error.Fail()) {
      if (log)
        log->Printf("Error getting condition expression: %s.",
                    error.AsCString());
      m_user_expression_sp.reset();
      return true;
    }

    if (!m_user_expression_sp->Parse(diagnostics, exe_ctx,
                                     eExecutionPolicyOnlyWhenNeeded, true,
                                     false)) {
      error.SetErrorStringWithFormat(
          "Couldn't parse conditional expression:\n%s",
          diagnostics.GetString().c_str());
      m_user_expression_sp.reset();
      return true;
    }

    m_condition_hash = condition_hash;
  }

  // We need to make sure the user sees any parse errors in their condition, so
  // we'll hook the constructor errors up to the debugger's Async I/O.

  ValueObjectSP result_value_sp;

  EvaluateExpressionOptions options;
  options.SetUnwindOnError(true);
  options.SetIgnoreBreakpoints(true);
  options.SetTryAllThreads(true);
  options.SetResultIsInternal(
      true); // Don't generate a user variable for condition expressions.

  Status expr_error;

  diagnostics.Clear();

  ExpressionVariableSP result_variable_sp;

  ExpressionResults result_code = m_user_expression_sp->Execute(
      diagnostics, exe_ctx, options, m_user_expression_sp, result_variable_sp);

  bool ret;

  if (result_code == eExpressionCompleted) {
    if (!result_variable_sp) {
      error.SetErrorString("Expression did not return a result");
      return false;
    }

    result_value_sp = result_variable_sp->GetValueObject();

    if (result_value_sp) {
      ret = result_value_sp->IsLogicalTrue(error);
      if (log) {
        if (error.Success()) {
          log->Printf("Condition successfully evaluated, result is %s.\n",
                      ret ? "true" : "false");
        } else {
          error.SetErrorString(
              "Failed to get an integer result from the expression");
          ret = false;
        }
      }
    } else {
      ret = false;
      error.SetErrorString("Failed to get any result from the expression");
    }
  } else {
    ret = false;
    error.SetErrorStringWithFormat("Couldn't execute expression:\n%s",
                                   diagnostics.GetString().c_str());
  }

  return ret;
}

uint32_t BreakpointLocation::GetIgnoreCount() {
  return GetOptionsSpecifyingKind(BreakpointOptions::eIgnoreCount)
      ->GetIgnoreCount();
}

void BreakpointLocation::SetIgnoreCount(uint32_t n) {
  GetLocationOptions()->SetIgnoreCount(n);
  SendBreakpointLocationChangedEvent(eBreakpointEventTypeIgnoreChanged);
}

void BreakpointLocation::DecrementIgnoreCount() {
  if (m_options_ap.get() != nullptr) {
    uint32_t loc_ignore = m_options_ap->GetIgnoreCount();
    if (loc_ignore != 0)
      m_options_ap->SetIgnoreCount(loc_ignore - 1);
  }
}

bool BreakpointLocation::IgnoreCountShouldStop() {
  if (m_options_ap.get() != nullptr) {
    uint32_t loc_ignore = m_options_ap->GetIgnoreCount();
    if (loc_ignore != 0) {
      m_owner.DecrementIgnoreCount();
      DecrementIgnoreCount(); // Have to decrement our owners' ignore count,
                              // since it won't get a
                              // chance to.
      return false;
    }
  }
  return true;
}

BreakpointOptions *BreakpointLocation::GetLocationOptions() {
  // If we make the copy we don't copy the callbacks because that is
  // potentially expensive and we don't want to do that for the simple case
  // where someone is just disabling the location.
  if (m_options_ap.get() == nullptr)
    m_options_ap.reset(
        new BreakpointOptions(false));

  return m_options_ap.get();
}

bool BreakpointLocation::ValidForThisThread(Thread *thread) {
  return thread
      ->MatchesSpec(GetOptionsSpecifyingKind(BreakpointOptions::eThreadSpec)
      ->GetThreadSpecNoCreate());
}

// RETURNS - true if we should stop at this breakpoint, false if we
// should continue.  Note, we don't check the thread spec for the breakpoint
// here, since if the breakpoint is not for this thread, then the event won't
// even get reported, so the check is redundant.

bool BreakpointLocation::ShouldStop(StoppointCallbackContext *context) {
  bool should_stop = true;
  Log *log = lldb_private::GetLogIfAllCategoriesSet(LIBLLDB_LOG_BREAKPOINTS);

  // Do this first, if a location is disabled, it shouldn't increment its hit
  // count.
  if (!IsEnabled())
    return false;

  if (!IgnoreCountShouldStop())
    return false;

  if (!m_owner.IgnoreCountShouldStop())
    return false;

  // We only run synchronous callbacks in ShouldStop:
  context->is_synchronous = true;
  should_stop = InvokeCallback(context);

  if (log) {
    StreamString s;
    GetDescription(&s, lldb::eDescriptionLevelVerbose);
    log->Printf("Hit breakpoint location: %s, %s.\n", s.GetData(),
                should_stop ? "stopping" : "continuing");
  }

  return should_stop;
}

void BreakpointLocation::BumpHitCount() {
  if (IsEnabled()) {
    // Step our hit count, and also step the hit count of the owner.
    IncrementHitCount();
    m_owner.IncrementHitCount();
  }
}

void BreakpointLocation::UndoBumpHitCount() {
  if (IsEnabled()) {
    // Step our hit count, and also step the hit count of the owner.
    DecrementHitCount();
    m_owner.DecrementHitCount();
  }
}

bool BreakpointLocation::IsResolved() const {
  return m_bp_site_sp.get() != nullptr;
}

lldb::BreakpointSiteSP BreakpointLocation::GetBreakpointSite() const {
  return m_bp_site_sp;
}

bool BreakpointLocation::ResolveBreakpointSite() {
  if (m_bp_site_sp)
    return true;

  Process *process = m_owner.GetTarget().GetProcessSP().get();
  if (process == nullptr)
    return false;

  lldb::break_id_t new_id =
      process->CreateBreakpointSite(shared_from_this(), m_owner.IsHardware());

  if (new_id == LLDB_INVALID_BREAK_ID) {
    Log *log = lldb_private::GetLogIfAllCategoriesSet(LIBLLDB_LOG_BREAKPOINTS);
    if (log)
      log->Warning("Tried to add breakpoint site at 0x%" PRIx64
                   " but it was already present.\n",
                   m_address.GetOpcodeLoadAddress(&m_owner.GetTarget()));
    return false;
  }

  return true;
}

bool BreakpointLocation::SetBreakpointSite(BreakpointSiteSP &bp_site_sp) {
  m_bp_site_sp = bp_site_sp;
  SendBreakpointLocationChangedEvent(eBreakpointEventTypeLocationsResolved);
  return true;
}

bool BreakpointLocation::ClearBreakpointSite() {
  if (m_bp_site_sp.get()) {
    ProcessSP process_sp(m_owner.GetTarget().GetProcessSP());
    // If the process exists, get it to remove the owner, it will remove the
    // physical implementation of the breakpoint as well if there are no more
    // owners.  Otherwise just remove this owner.
    if (process_sp)
      process_sp->RemoveOwnerFromBreakpointSite(GetBreakpoint().GetID(),
                                                GetID(), m_bp_site_sp);
    else
      m_bp_site_sp->RemoveOwner(GetBreakpoint().GetID(), GetID());

    m_bp_site_sp.reset();
    return true;
  }
  return false;
}

void BreakpointLocation::GetDescription(Stream *s,
                                        lldb::DescriptionLevel level) {
  SymbolContext sc;

  // If the description level is "initial" then the breakpoint is printing out
  // our initial state, and we should let it decide how it wants to print our
  // label.
  if (level != eDescriptionLevelInitial) {
    s->Indent();
    BreakpointID::GetCanonicalReference(s, m_owner.GetID(), GetID());
  }

  if (level == lldb::eDescriptionLevelBrief)
    return;

  if (level != eDescriptionLevelInitial)
    s->PutCString(": ");

  if (level == lldb::eDescriptionLevelVerbose)
    s->IndentMore();

  if (m_address.IsSectionOffset()) {
    m_address.CalculateSymbolContext(&sc);

    if (level == lldb::eDescriptionLevelFull ||
        level == eDescriptionLevelInitial) {
      if (IsReExported())
        s->PutCString("re-exported target = ");
      else
        s->PutCString("where = ");
      sc.DumpStopContext(s, m_owner.GetTarget().GetProcessSP().get(), m_address,
                         false, true, false, true, true);
    } else {
      if (sc.module_sp) {
        s->EOL();
        s->Indent("module = ");
        sc.module_sp->GetFileSpec().Dump(s);
      }

      if (sc.comp_unit != nullptr) {
        s->EOL();
        s->Indent("compile unit = ");
        static_cast<FileSpec *>(sc.comp_unit)->GetFilename().Dump(s);

        if (sc.function != nullptr) {
          s->EOL();
          s->Indent("function = ");
          s->PutCString(sc.function->GetName().AsCString("<unknown>"));
        }

        if (sc.line_entry.line > 0) {
          s->EOL();
          s->Indent("location = ");
          sc.line_entry.DumpStopContext(s, true);
        }

      } else {
        // If we don't have a comp unit, see if we have a symbol we can print.
        if (sc.symbol) {
          s->EOL();
          if (IsReExported())
            s->Indent("re-exported target = ");
          else
            s->Indent("symbol = ");
          s->PutCString(sc.symbol->GetName().AsCString("<unknown>"));
        }
      }
    }
  }

  if (level == lldb::eDescriptionLevelVerbose) {
    s->EOL();
    s->Indent();
  }

  if (m_address.IsSectionOffset() &&
      (level == eDescriptionLevelFull || level == eDescriptionLevelInitial))
    s->Printf(", ");
  s->Printf("address = ");

  ExecutionContextScope *exe_scope = nullptr;
  Target *target = &m_owner.GetTarget();
  if (target)
    exe_scope = target->GetProcessSP().get();
  if (exe_scope == nullptr)
    exe_scope = target;

  if (level == eDescriptionLevelInitial)
    m_address.Dump(s, exe_scope, Address::DumpStyleLoadAddress,
                   Address::DumpStyleFileAddress);
  else
    m_address.Dump(s, exe_scope, Address::DumpStyleLoadAddress,
                   Address::DumpStyleModuleWithFileAddress);

  if (IsIndirect() && m_bp_site_sp) {
    Address resolved_address;
    resolved_address.SetLoadAddress(m_bp_site_sp->GetLoadAddress(), target);
    Symbol *resolved_symbol = resolved_address.CalculateSymbolContextSymbol();
    if (resolved_symbol) {
      if (level == eDescriptionLevelFull || level == eDescriptionLevelInitial)
        s->Printf(", ");
      else if (level == lldb::eDescriptionLevelVerbose) {
        s->EOL();
        s->Indent();
      }
      s->Printf("indirect target = %s",
                resolved_symbol->GetName().GetCString());
    }
  }

  if (level == lldb::eDescriptionLevelVerbose) {
    s->EOL();
    s->Indent();
    s->Printf("resolved = %s\n", IsResolved() ? "true" : "false");

    s->Indent();
    s->Printf("hit count = %-4u\n", GetHitCount());

    if (m_options_ap.get()) {
      s->Indent();
      m_options_ap->GetDescription(s, level);
      s->EOL();
    }
    s->IndentLess();
  } else if (level != eDescriptionLevelInitial) {
    s->Printf(", %sresolved, hit count = %u ", (IsResolved() ? "" : "un"),
              GetHitCount());
    if (m_options_ap.get()) {
      m_options_ap->GetDescription(s, level);
    }
  }
}

void BreakpointLocation::Dump(Stream *s) const {
  if (s == nullptr)
    return;

  lldb::tid_t tid = GetOptionsSpecifyingKind(BreakpointOptions::eThreadSpec)
      ->GetThreadSpecNoCreate()->GetTID();
  s->Printf(
      "BreakpointLocation %u: tid = %4.4" PRIx64 "  load addr = 0x%8.8" PRIx64
      "  state = %s  type = %s breakpoint  "
      "hw_index = %i  hit_count = %-4u  ignore_count = %-4u",
      GetID(), tid,
      (uint64_t)m_address.GetOpcodeLoadAddress(&m_owner.GetTarget()),
      (m_options_ap.get() ? m_options_ap->IsEnabled() : m_owner.IsEnabled())
          ? "enabled "
          : "disabled",
      IsHardware() ? "hardware" : "software", GetHardwareIndex(), GetHitCount(),
      GetOptionsSpecifyingKind(BreakpointOptions::eIgnoreCount)
          ->GetIgnoreCount());
}

void BreakpointLocation::SendBreakpointLocationChangedEvent(
    lldb::BreakpointEventType eventKind) {
  if (!m_being_created && !m_owner.IsInternal() &&
      m_owner.GetTarget().EventTypeHasListeners(
          Target::eBroadcastBitBreakpointChanged)) {
    Breakpoint::BreakpointEventData *data = new Breakpoint::BreakpointEventData(
        eventKind, m_owner.shared_from_this());
    data->GetBreakpointLocationCollection().Add(shared_from_this());
    m_owner.GetTarget().BroadcastEvent(Target::eBroadcastBitBreakpointChanged,
                                       data);
  }
}

void BreakpointLocation::SwapLocation(BreakpointLocationSP swap_from) {
  m_address = swap_from->m_address;
  m_should_resolve_indirect_functions =
      swap_from->m_should_resolve_indirect_functions;
  m_is_reexported = swap_from->m_is_reexported;
  m_is_indirect = swap_from->m_is_indirect;
  m_user_expression_sp.reset();
}
