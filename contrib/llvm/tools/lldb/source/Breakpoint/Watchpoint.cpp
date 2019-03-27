//===-- Watchpoint.cpp ------------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "lldb/Breakpoint/Watchpoint.h"

#include "lldb/Breakpoint/StoppointCallbackContext.h"
#include "lldb/Core/Value.h"
#include "lldb/Core/ValueObject.h"
#include "lldb/Core/ValueObjectMemory.h"
#include "lldb/Expression/UserExpression.h"
#include "lldb/Symbol/ClangASTContext.h"
#include "lldb/Target/Process.h"
#include "lldb/Target/Target.h"
#include "lldb/Target/ThreadSpec.h"
#include "lldb/Utility/Stream.h"

using namespace lldb;
using namespace lldb_private;

Watchpoint::Watchpoint(Target &target, lldb::addr_t addr, uint32_t size,
                       const CompilerType *type, bool hardware)
    : StoppointLocation(0, addr, size, hardware), m_target(target),
      m_enabled(false), m_is_hardware(hardware), m_is_watch_variable(false),
      m_is_ephemeral(false), m_disabled_count(0), m_watch_read(0),
      m_watch_write(0), m_watch_was_read(0), m_watch_was_written(0),
      m_ignore_count(0), m_false_alarms(0), m_decl_str(), m_watch_spec_str(),
      m_type(), m_error(), m_options(), m_being_created(true) {
  if (type && type->IsValid())
    m_type = *type;
  else {
    // If we don't have a known type, then we force it to unsigned int of the
    // right size.
    ClangASTContext *ast_context = target.GetScratchClangASTContext();
    m_type = ast_context->GetBuiltinTypeForEncodingAndBitSize(eEncodingUint,
                                                              8 * size);
  }

  // Set the initial value of the watched variable:
  if (m_target.GetProcessSP()) {
    ExecutionContext exe_ctx;
    m_target.GetProcessSP()->CalculateExecutionContext(exe_ctx);
    CaptureWatchedValue(exe_ctx);
  }
  m_being_created = false;
}

Watchpoint::~Watchpoint() = default;

// This function is used when "baton" doesn't need to be freed
void Watchpoint::SetCallback(WatchpointHitCallback callback, void *baton,
                             bool is_synchronous) {
  // The default "Baton" class will keep a copy of "baton" and won't free or
  // delete it when it goes goes out of scope.
  m_options.SetCallback(callback, std::make_shared<UntypedBaton>(baton),
                        is_synchronous);

  SendWatchpointChangedEvent(eWatchpointEventTypeCommandChanged);
}

// This function is used when a baton needs to be freed and therefore is
// contained in a "Baton" subclass.
void Watchpoint::SetCallback(WatchpointHitCallback callback,
                             const BatonSP &callback_baton_sp,
                             bool is_synchronous) {
  m_options.SetCallback(callback, callback_baton_sp, is_synchronous);
  SendWatchpointChangedEvent(eWatchpointEventTypeCommandChanged);
}

void Watchpoint::ClearCallback() {
  m_options.ClearCallback();
  SendWatchpointChangedEvent(eWatchpointEventTypeCommandChanged);
}

void Watchpoint::SetDeclInfo(const std::string &str) { m_decl_str = str; }

std::string Watchpoint::GetWatchSpec() { return m_watch_spec_str; }

void Watchpoint::SetWatchSpec(const std::string &str) {
  m_watch_spec_str = str;
}

// Override default impl of StoppointLocation::IsHardware() since m_is_hardware
// member field is more accurate.
bool Watchpoint::IsHardware() const { return m_is_hardware; }

bool Watchpoint::IsWatchVariable() const { return m_is_watch_variable; }

void Watchpoint::SetWatchVariable(bool val) { m_is_watch_variable = val; }

bool Watchpoint::CaptureWatchedValue(const ExecutionContext &exe_ctx) {
  ConstString watch_name("$__lldb__watch_value");
  m_old_value_sp = m_new_value_sp;
  Address watch_address(GetLoadAddress());
  if (!m_type.IsValid()) {
    // Don't know how to report new & old values, since we couldn't make a
    // scalar type for this watchpoint. This works around an assert in
    // ValueObjectMemory::Create.
    // FIXME: This should not happen, but if it does in some case we care about,
    // we can go grab the value raw and print it as unsigned.
    return false;
  }
  m_new_value_sp = ValueObjectMemory::Create(
      exe_ctx.GetBestExecutionContextScope(), watch_name.GetStringRef(),
      watch_address, m_type);
  m_new_value_sp = m_new_value_sp->CreateConstantValue(watch_name);
  return (m_new_value_sp && m_new_value_sp->GetError().Success());
}

void Watchpoint::IncrementFalseAlarmsAndReviseHitCount() {
  ++m_false_alarms;
  if (m_false_alarms) {
    if (m_hit_count >= m_false_alarms) {
      m_hit_count -= m_false_alarms;
      m_false_alarms = 0;
    } else {
      m_false_alarms -= m_hit_count;
      m_hit_count = 0;
    }
  }
}

// RETURNS - true if we should stop at this breakpoint, false if we
// should continue.

bool Watchpoint::ShouldStop(StoppointCallbackContext *context) {
  IncrementHitCount();

  return IsEnabled();
}

void Watchpoint::GetDescription(Stream *s, lldb::DescriptionLevel level) {
  DumpWithLevel(s, level);
}

void Watchpoint::Dump(Stream *s) const {
  DumpWithLevel(s, lldb::eDescriptionLevelBrief);
}

// If prefix is nullptr, we display the watch id and ignore the prefix
// altogether.
void Watchpoint::DumpSnapshots(Stream *s, const char *prefix) const {
  if (!prefix) {
    s->Printf("\nWatchpoint %u hit:", GetID());
    prefix = "";
  }

  if (m_old_value_sp) {
    const char *old_value_cstr = m_old_value_sp->GetValueAsCString();
    if (old_value_cstr && old_value_cstr[0])
      s->Printf("\n%sold value: %s", prefix, old_value_cstr);
    else {
      const char *old_summary_cstr = m_old_value_sp->GetSummaryAsCString();
      if (old_summary_cstr && old_summary_cstr[0])
        s->Printf("\n%sold value: %s", prefix, old_summary_cstr);
    }
  }

  if (m_new_value_sp) {
    const char *new_value_cstr = m_new_value_sp->GetValueAsCString();
    if (new_value_cstr && new_value_cstr[0])
      s->Printf("\n%snew value: %s", prefix, new_value_cstr);
    else {
      const char *new_summary_cstr = m_new_value_sp->GetSummaryAsCString();
      if (new_summary_cstr && new_summary_cstr[0])
        s->Printf("\n%snew value: %s", prefix, new_summary_cstr);
    }
  }
}

void Watchpoint::DumpWithLevel(Stream *s,
                               lldb::DescriptionLevel description_level) const {
  if (s == nullptr)
    return;

  assert(description_level >= lldb::eDescriptionLevelBrief &&
         description_level <= lldb::eDescriptionLevelVerbose);

  s->Printf("Watchpoint %u: addr = 0x%8.8" PRIx64
            " size = %u state = %s type = %s%s",
            GetID(), GetLoadAddress(), m_byte_size,
            IsEnabled() ? "enabled" : "disabled", m_watch_read ? "r" : "",
            m_watch_write ? "w" : "");

  if (description_level >= lldb::eDescriptionLevelFull) {
    if (!m_decl_str.empty())
      s->Printf("\n    declare @ '%s'", m_decl_str.c_str());
    if (!m_watch_spec_str.empty())
      s->Printf("\n    watchpoint spec = '%s'", m_watch_spec_str.c_str());

    // Dump the snapshots we have taken.
    DumpSnapshots(s, "    ");

    if (GetConditionText())
      s->Printf("\n    condition = '%s'", GetConditionText());
    m_options.GetCallbackDescription(s, description_level);
  }

  if (description_level >= lldb::eDescriptionLevelVerbose) {
    s->Printf("\n    hw_index = %i  hit_count = %-4u  ignore_count = %-4u",
              GetHardwareIndex(), GetHitCount(), GetIgnoreCount());
  }
}

bool Watchpoint::IsEnabled() const { return m_enabled; }

// Within StopInfo.cpp, we purposely turn on the ephemeral mode right before
// temporarily disable the watchpoint in order to perform possible watchpoint
// actions without triggering further watchpoint events. After the temporary
// disabled watchpoint is enabled, we then turn off the ephemeral mode.

void Watchpoint::TurnOnEphemeralMode() { m_is_ephemeral = true; }

void Watchpoint::TurnOffEphemeralMode() {
  m_is_ephemeral = false;
  // Leaving ephemeral mode, reset the m_disabled_count!
  m_disabled_count = 0;
}

bool Watchpoint::IsDisabledDuringEphemeralMode() {
  return m_disabled_count > 1 && m_is_ephemeral;
}

void Watchpoint::SetEnabled(bool enabled, bool notify) {
  if (!enabled) {
    if (!m_is_ephemeral)
      SetHardwareIndex(LLDB_INVALID_INDEX32);
    else
      ++m_disabled_count;

    // Don't clear the snapshots for now.
    // Within StopInfo.cpp, we purposely do disable/enable watchpoint while
    // performing watchpoint actions.
  }
  bool changed = enabled != m_enabled;
  m_enabled = enabled;
  if (notify && !m_is_ephemeral && changed)
    SendWatchpointChangedEvent(enabled ? eWatchpointEventTypeEnabled
                                       : eWatchpointEventTypeDisabled);
}

void Watchpoint::SetWatchpointType(uint32_t type, bool notify) {
  int old_watch_read = m_watch_read;
  int old_watch_write = m_watch_write;
  m_watch_read = (type & LLDB_WATCH_TYPE_READ) != 0;
  m_watch_write = (type & LLDB_WATCH_TYPE_WRITE) != 0;
  if (notify &&
      (old_watch_read != m_watch_read || old_watch_write != m_watch_write))
    SendWatchpointChangedEvent(eWatchpointEventTypeTypeChanged);
}

bool Watchpoint::WatchpointRead() const { return m_watch_read != 0; }

bool Watchpoint::WatchpointWrite() const { return m_watch_write != 0; }

uint32_t Watchpoint::GetIgnoreCount() const { return m_ignore_count; }

void Watchpoint::SetIgnoreCount(uint32_t n) {
  bool changed = m_ignore_count != n;
  m_ignore_count = n;
  if (changed)
    SendWatchpointChangedEvent(eWatchpointEventTypeIgnoreChanged);
}

bool Watchpoint::InvokeCallback(StoppointCallbackContext *context) {
  return m_options.InvokeCallback(context, GetID());
}

void Watchpoint::SetCondition(const char *condition) {
  if (condition == nullptr || condition[0] == '\0') {
    if (m_condition_ap.get())
      m_condition_ap.reset();
  } else {
    // Pass nullptr for expr_prefix (no translation-unit level definitions).
    Status error;
    m_condition_ap.reset(m_target.GetUserExpressionForLanguage(
        condition, llvm::StringRef(), lldb::eLanguageTypeUnknown,
        UserExpression::eResultTypeAny, EvaluateExpressionOptions(), error));
    if (error.Fail()) {
      // FIXME: Log something...
      m_condition_ap.reset();
    }
  }
  SendWatchpointChangedEvent(eWatchpointEventTypeConditionChanged);
}

const char *Watchpoint::GetConditionText() const {
  if (m_condition_ap.get())
    return m_condition_ap->GetUserText();
  else
    return nullptr;
}

void Watchpoint::SendWatchpointChangedEvent(
    lldb::WatchpointEventType eventKind) {
  if (!m_being_created &&
      GetTarget().EventTypeHasListeners(
          Target::eBroadcastBitWatchpointChanged)) {
    WatchpointEventData *data =
        new Watchpoint::WatchpointEventData(eventKind, shared_from_this());
    GetTarget().BroadcastEvent(Target::eBroadcastBitWatchpointChanged, data);
  }
}

void Watchpoint::SendWatchpointChangedEvent(WatchpointEventData *data) {
  if (data == nullptr)
    return;

  if (!m_being_created &&
      GetTarget().EventTypeHasListeners(Target::eBroadcastBitWatchpointChanged))
    GetTarget().BroadcastEvent(Target::eBroadcastBitWatchpointChanged, data);
  else
    delete data;
}

Watchpoint::WatchpointEventData::WatchpointEventData(
    WatchpointEventType sub_type, const WatchpointSP &new_watchpoint_sp)
    : EventData(), m_watchpoint_event(sub_type),
      m_new_watchpoint_sp(new_watchpoint_sp) {}

Watchpoint::WatchpointEventData::~WatchpointEventData() = default;

const ConstString &Watchpoint::WatchpointEventData::GetFlavorString() {
  static ConstString g_flavor("Watchpoint::WatchpointEventData");
  return g_flavor;
}

const ConstString &Watchpoint::WatchpointEventData::GetFlavor() const {
  return WatchpointEventData::GetFlavorString();
}

WatchpointSP &Watchpoint::WatchpointEventData::GetWatchpoint() {
  return m_new_watchpoint_sp;
}

WatchpointEventType
Watchpoint::WatchpointEventData::GetWatchpointEventType() const {
  return m_watchpoint_event;
}

void Watchpoint::WatchpointEventData::Dump(Stream *s) const {}

const Watchpoint::WatchpointEventData *
Watchpoint::WatchpointEventData::GetEventDataFromEvent(const Event *event) {
  if (event) {
    const EventData *event_data = event->GetData();
    if (event_data &&
        event_data->GetFlavor() == WatchpointEventData::GetFlavorString())
      return static_cast<const WatchpointEventData *>(event->GetData());
  }
  return nullptr;
}

WatchpointEventType
Watchpoint::WatchpointEventData::GetWatchpointEventTypeFromEvent(
    const EventSP &event_sp) {
  const WatchpointEventData *data = GetEventDataFromEvent(event_sp.get());

  if (data == nullptr)
    return eWatchpointEventTypeInvalidType;
  else
    return data->GetWatchpointEventType();
}

WatchpointSP Watchpoint::WatchpointEventData::GetWatchpointFromEvent(
    const EventSP &event_sp) {
  WatchpointSP wp_sp;

  const WatchpointEventData *data = GetEventDataFromEvent(event_sp.get());
  if (data)
    wp_sp = data->m_new_watchpoint_sp;

  return wp_sp;
}
