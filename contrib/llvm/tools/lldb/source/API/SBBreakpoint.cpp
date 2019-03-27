//===-- SBBreakpoint.cpp ----------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "lldb/API/SBBreakpoint.h"
#include "lldb/API/SBBreakpointLocation.h"
#include "lldb/API/SBDebugger.h"
#include "lldb/API/SBEvent.h"
#include "lldb/API/SBProcess.h"
#include "lldb/API/SBStream.h"
#include "lldb/API/SBStringList.h"
#include "lldb/API/SBThread.h"

#include "lldb/Breakpoint/Breakpoint.h"
#include "lldb/Breakpoint/BreakpointIDList.h"
#include "lldb/Breakpoint/BreakpointLocation.h"
#include "lldb/Breakpoint/BreakpointResolver.h"
#include "lldb/Breakpoint/BreakpointResolverScripted.h"
#include "lldb/Breakpoint/StoppointCallbackContext.h"
#include "lldb/Core/Address.h"
#include "lldb/Core/Debugger.h"
#include "lldb/Core/StreamFile.h"
#include "lldb/Interpreter/CommandInterpreter.h"
#include "lldb/Interpreter/ScriptInterpreter.h"
#include "lldb/Target/Process.h"
#include "lldb/Target/SectionLoadList.h"
#include "lldb/Target/Target.h"
#include "lldb/Target/Thread.h"
#include "lldb/Target/ThreadSpec.h"
#include "lldb/Utility/Log.h"
#include "lldb/Utility/Stream.h"

#include "SBBreakpointOptionCommon.h"

#include "lldb/lldb-enumerations.h"

#include "llvm/ADT/STLExtras.h"

using namespace lldb;
using namespace lldb_private;

SBBreakpoint::SBBreakpoint() {}

SBBreakpoint::SBBreakpoint(const SBBreakpoint &rhs)
    : m_opaque_wp(rhs.m_opaque_wp) {}

SBBreakpoint::SBBreakpoint(const lldb::BreakpointSP &bp_sp)
    : m_opaque_wp(bp_sp) {}

SBBreakpoint::~SBBreakpoint() = default;

const SBBreakpoint &SBBreakpoint::operator=(const SBBreakpoint &rhs) {
  m_opaque_wp = rhs.m_opaque_wp;
  return *this;
}

bool SBBreakpoint::operator==(const lldb::SBBreakpoint &rhs) {
  return m_opaque_wp.lock() == rhs.m_opaque_wp.lock();
}

bool SBBreakpoint::operator!=(const lldb::SBBreakpoint &rhs) {
  return m_opaque_wp.lock() != rhs.m_opaque_wp.lock();
}

break_id_t SBBreakpoint::GetID() const {
  Log *log(lldb_private::GetLogIfAllCategoriesSet(LIBLLDB_LOG_API));

  break_id_t break_id = LLDB_INVALID_BREAK_ID;
  BreakpointSP bkpt_sp = GetSP();
  if (bkpt_sp)
    break_id = bkpt_sp->GetID();

  LLDB_LOG(log, "breakpoint = {0}, id = {1}", bkpt_sp.get(), break_id);
  return break_id;
}

bool SBBreakpoint::IsValid() const {
  BreakpointSP bkpt_sp = GetSP();
  if (!bkpt_sp)
    return false;
  else if (bkpt_sp->GetTarget().GetBreakpointByID(bkpt_sp->GetID()))
    return true;
  else
    return false;
}

void SBBreakpoint::ClearAllBreakpointSites() {
  BreakpointSP bkpt_sp = GetSP();
  if (bkpt_sp) {
    std::lock_guard<std::recursive_mutex> guard(
        bkpt_sp->GetTarget().GetAPIMutex());
    bkpt_sp->ClearAllBreakpointSites();
  }
}

SBBreakpointLocation SBBreakpoint::FindLocationByAddress(addr_t vm_addr) {
  SBBreakpointLocation sb_bp_location;

  BreakpointSP bkpt_sp = GetSP();
  if (bkpt_sp) {
    if (vm_addr != LLDB_INVALID_ADDRESS) {
      std::lock_guard<std::recursive_mutex> guard(
          bkpt_sp->GetTarget().GetAPIMutex());
      Address address;
      Target &target = bkpt_sp->GetTarget();
      if (!target.GetSectionLoadList().ResolveLoadAddress(vm_addr, address)) {
        address.SetRawAddress(vm_addr);
      }
      sb_bp_location.SetLocation(bkpt_sp->FindLocationByAddress(address));
    }
  }
  return sb_bp_location;
}

break_id_t SBBreakpoint::FindLocationIDByAddress(addr_t vm_addr) {
  break_id_t break_id = LLDB_INVALID_BREAK_ID;
  BreakpointSP bkpt_sp = GetSP();

  if (bkpt_sp && vm_addr != LLDB_INVALID_ADDRESS) {
    std::lock_guard<std::recursive_mutex> guard(
        bkpt_sp->GetTarget().GetAPIMutex());
    Address address;
    Target &target = bkpt_sp->GetTarget();
    if (!target.GetSectionLoadList().ResolveLoadAddress(vm_addr, address)) {
      address.SetRawAddress(vm_addr);
    }
    break_id = bkpt_sp->FindLocationIDByAddress(address);
  }

  return break_id;
}

SBBreakpointLocation SBBreakpoint::FindLocationByID(break_id_t bp_loc_id) {
  SBBreakpointLocation sb_bp_location;
  BreakpointSP bkpt_sp = GetSP();

  if (bkpt_sp) {
    std::lock_guard<std::recursive_mutex> guard(
        bkpt_sp->GetTarget().GetAPIMutex());
    sb_bp_location.SetLocation(bkpt_sp->FindLocationByID(bp_loc_id));
  }

  return sb_bp_location;
}

SBBreakpointLocation SBBreakpoint::GetLocationAtIndex(uint32_t index) {
  SBBreakpointLocation sb_bp_location;
  BreakpointSP bkpt_sp = GetSP();

  if (bkpt_sp) {
    std::lock_guard<std::recursive_mutex> guard(
        bkpt_sp->GetTarget().GetAPIMutex());
    sb_bp_location.SetLocation(bkpt_sp->GetLocationAtIndex(index));
  }

  return sb_bp_location;
}

void SBBreakpoint::SetEnabled(bool enable) {
  Log *log(lldb_private::GetLogIfAllCategoriesSet(LIBLLDB_LOG_API));
  BreakpointSP bkpt_sp = GetSP();

  LLDB_LOG(log, "breakpoint = {0}, enable = {1}", bkpt_sp.get(), enable);

  if (bkpt_sp) {
    std::lock_guard<std::recursive_mutex> guard(
        bkpt_sp->GetTarget().GetAPIMutex());
    bkpt_sp->SetEnabled(enable);
  }
}

bool SBBreakpoint::IsEnabled() {
  BreakpointSP bkpt_sp = GetSP();
  if (bkpt_sp) {
    std::lock_guard<std::recursive_mutex> guard(
        bkpt_sp->GetTarget().GetAPIMutex());
    return bkpt_sp->IsEnabled();
  } else
    return false;
}

void SBBreakpoint::SetOneShot(bool one_shot) {
  Log *log(lldb_private::GetLogIfAllCategoriesSet(LIBLLDB_LOG_API));
  BreakpointSP bkpt_sp = GetSP();

  LLDB_LOG(log, "breakpoint = {0}, one_shot = {1}", bkpt_sp.get(), one_shot);

  if (bkpt_sp) {
    std::lock_guard<std::recursive_mutex> guard(
        bkpt_sp->GetTarget().GetAPIMutex());
    bkpt_sp->SetOneShot(one_shot);
  }
}

bool SBBreakpoint::IsOneShot() const {
  BreakpointSP bkpt_sp = GetSP();
  if (bkpt_sp) {
    std::lock_guard<std::recursive_mutex> guard(
        bkpt_sp->GetTarget().GetAPIMutex());
    return bkpt_sp->IsOneShot();
  } else
    return false;
}

bool SBBreakpoint::IsInternal() {
  BreakpointSP bkpt_sp = GetSP();
  if (bkpt_sp) {
    std::lock_guard<std::recursive_mutex> guard(
        bkpt_sp->GetTarget().GetAPIMutex());
    return bkpt_sp->IsInternal();
  } else
    return false;
}

void SBBreakpoint::SetIgnoreCount(uint32_t count) {
  Log *log(lldb_private::GetLogIfAllCategoriesSet(LIBLLDB_LOG_API));
  BreakpointSP bkpt_sp = GetSP();

  LLDB_LOG(log, "breakpoint = {0}, count = {1}", bkpt_sp.get(), count);

  if (bkpt_sp) {
    std::lock_guard<std::recursive_mutex> guard(
        bkpt_sp->GetTarget().GetAPIMutex());
    bkpt_sp->SetIgnoreCount(count);
  }
}

void SBBreakpoint::SetCondition(const char *condition) {
  BreakpointSP bkpt_sp = GetSP();
  if (bkpt_sp) {
    std::lock_guard<std::recursive_mutex> guard(
        bkpt_sp->GetTarget().GetAPIMutex());
    bkpt_sp->SetCondition(condition);
  }
}

const char *SBBreakpoint::GetCondition() {
  BreakpointSP bkpt_sp = GetSP();
  if (bkpt_sp) {
    std::lock_guard<std::recursive_mutex> guard(
        bkpt_sp->GetTarget().GetAPIMutex());
    return bkpt_sp->GetConditionText();
  }
  return nullptr;
}

void SBBreakpoint::SetAutoContinue(bool auto_continue) {
  BreakpointSP bkpt_sp = GetSP();
  if (bkpt_sp) {
    std::lock_guard<std::recursive_mutex> guard(
        bkpt_sp->GetTarget().GetAPIMutex());
    bkpt_sp->SetAutoContinue(auto_continue);
  }
}

bool SBBreakpoint::GetAutoContinue() {
  BreakpointSP bkpt_sp = GetSP();
  if (bkpt_sp) {
    std::lock_guard<std::recursive_mutex> guard(
        bkpt_sp->GetTarget().GetAPIMutex());
    return bkpt_sp->IsAutoContinue();
  }
  return false;
}

uint32_t SBBreakpoint::GetHitCount() const {
  uint32_t count = 0;
  BreakpointSP bkpt_sp = GetSP();
  if (bkpt_sp) {
    std::lock_guard<std::recursive_mutex> guard(
        bkpt_sp->GetTarget().GetAPIMutex());
    count = bkpt_sp->GetHitCount();
  }

  Log *log(lldb_private::GetLogIfAllCategoriesSet(LIBLLDB_LOG_API));
  LLDB_LOG(log, "breakpoint = {0}, count = {1}", bkpt_sp.get(), count);

  return count;
}

uint32_t SBBreakpoint::GetIgnoreCount() const {
  uint32_t count = 0;
  BreakpointSP bkpt_sp = GetSP();
  if (bkpt_sp) {
    std::lock_guard<std::recursive_mutex> guard(
        bkpt_sp->GetTarget().GetAPIMutex());
    count = bkpt_sp->GetIgnoreCount();
  }

  Log *log(lldb_private::GetLogIfAllCategoriesSet(LIBLLDB_LOG_API));
  LLDB_LOG(log, "breakpoint = {0}, count = {1}", bkpt_sp.get(), count);

  return count;
}

void SBBreakpoint::SetThreadID(tid_t tid) {
  BreakpointSP bkpt_sp = GetSP();
  if (bkpt_sp) {
    std::lock_guard<std::recursive_mutex> guard(
        bkpt_sp->GetTarget().GetAPIMutex());
    bkpt_sp->SetThreadID(tid);
  }
  Log *log(lldb_private::GetLogIfAllCategoriesSet(LIBLLDB_LOG_API));
  LLDB_LOG(log, "breakpoint = {0}, tid = {1:x}", bkpt_sp.get(), tid);
}

tid_t SBBreakpoint::GetThreadID() {
  tid_t tid = LLDB_INVALID_THREAD_ID;
  BreakpointSP bkpt_sp = GetSP();
  if (bkpt_sp) {
    std::lock_guard<std::recursive_mutex> guard(
        bkpt_sp->GetTarget().GetAPIMutex());
    tid = bkpt_sp->GetThreadID();
  }

  Log *log(lldb_private::GetLogIfAllCategoriesSet(LIBLLDB_LOG_API));
  LLDB_LOG(log, "breakpoint = {0}, tid = {1:x}", bkpt_sp.get(), tid);
  return tid;
}

void SBBreakpoint::SetThreadIndex(uint32_t index) {
  Log *log(lldb_private::GetLogIfAllCategoriesSet(LIBLLDB_LOG_API));
  BreakpointSP bkpt_sp = GetSP();
  LLDB_LOG(log, "breakpoint = {0}, index = {1}", bkpt_sp.get(), index);
  if (bkpt_sp) {
    std::lock_guard<std::recursive_mutex> guard(
        bkpt_sp->GetTarget().GetAPIMutex());
    bkpt_sp->GetOptions()->GetThreadSpec()->SetIndex(index);
  }
}

uint32_t SBBreakpoint::GetThreadIndex() const {
  uint32_t thread_idx = UINT32_MAX;
  BreakpointSP bkpt_sp = GetSP();
  if (bkpt_sp) {
    std::lock_guard<std::recursive_mutex> guard(
        bkpt_sp->GetTarget().GetAPIMutex());
    const ThreadSpec *thread_spec =
        bkpt_sp->GetOptions()->GetThreadSpecNoCreate();
    if (thread_spec != nullptr)
      thread_idx = thread_spec->GetIndex();
  }
  Log *log(lldb_private::GetLogIfAllCategoriesSet(LIBLLDB_LOG_API));
  LLDB_LOG(log, "breakpoint = {0}, index = {1}", bkpt_sp.get(), thread_idx);

  return thread_idx;
}

void SBBreakpoint::SetThreadName(const char *thread_name) {
  Log *log(lldb_private::GetLogIfAllCategoriesSet(LIBLLDB_LOG_API));
  BreakpointSP bkpt_sp = GetSP();
  LLDB_LOG(log, "breakpoint = {0}, name = {1}", bkpt_sp.get(), thread_name);

  if (bkpt_sp) {
    std::lock_guard<std::recursive_mutex> guard(
        bkpt_sp->GetTarget().GetAPIMutex());
    bkpt_sp->GetOptions()->GetThreadSpec()->SetName(thread_name);
  }
}

const char *SBBreakpoint::GetThreadName() const {
  const char *name = nullptr;
  BreakpointSP bkpt_sp = GetSP();
  if (bkpt_sp) {
    std::lock_guard<std::recursive_mutex> guard(
        bkpt_sp->GetTarget().GetAPIMutex());
    const ThreadSpec *thread_spec =
        bkpt_sp->GetOptions()->GetThreadSpecNoCreate();
    if (thread_spec != nullptr)
      name = thread_spec->GetName();
  }
  Log *log(lldb_private::GetLogIfAllCategoriesSet(LIBLLDB_LOG_API));
  LLDB_LOG(log, "breakpoint = {0}, name = {1}", bkpt_sp.get(), name);

  return name;
}

void SBBreakpoint::SetQueueName(const char *queue_name) {
  Log *log(lldb_private::GetLogIfAllCategoriesSet(LIBLLDB_LOG_API));
  BreakpointSP bkpt_sp = GetSP();
  LLDB_LOG(log, "breakpoint = {0}, queue_name = {1}", bkpt_sp.get(),
           queue_name);
  if (bkpt_sp) {
    std::lock_guard<std::recursive_mutex> guard(
        bkpt_sp->GetTarget().GetAPIMutex());
    bkpt_sp->GetOptions()->GetThreadSpec()->SetQueueName(queue_name);
  }
}

const char *SBBreakpoint::GetQueueName() const {
  const char *name = nullptr;
  BreakpointSP bkpt_sp = GetSP();
  if (bkpt_sp) {
    std::lock_guard<std::recursive_mutex> guard(
        bkpt_sp->GetTarget().GetAPIMutex());
    const ThreadSpec *thread_spec =
        bkpt_sp->GetOptions()->GetThreadSpecNoCreate();
    if (thread_spec)
      name = thread_spec->GetQueueName();
  }
  Log *log(lldb_private::GetLogIfAllCategoriesSet(LIBLLDB_LOG_API));
  LLDB_LOG(log, "breakpoint = {0}, name = {1}", bkpt_sp.get(), name);

  return name;
}

size_t SBBreakpoint::GetNumResolvedLocations() const {
  size_t num_resolved = 0;
  BreakpointSP bkpt_sp = GetSP();
  if (bkpt_sp) {
    std::lock_guard<std::recursive_mutex> guard(
        bkpt_sp->GetTarget().GetAPIMutex());
    num_resolved = bkpt_sp->GetNumResolvedLocations();
  }
  Log *log(lldb_private::GetLogIfAllCategoriesSet(LIBLLDB_LOG_API));
  LLDB_LOG(log, "breakpoint = {0}, num_resolved = {1}", bkpt_sp.get(),
           num_resolved);
  return num_resolved;
}

size_t SBBreakpoint::GetNumLocations() const {
  BreakpointSP bkpt_sp = GetSP();
  size_t num_locs = 0;
  if (bkpt_sp) {
    std::lock_guard<std::recursive_mutex> guard(
        bkpt_sp->GetTarget().GetAPIMutex());
    num_locs = bkpt_sp->GetNumLocations();
  }
  Log *log(lldb_private::GetLogIfAllCategoriesSet(LIBLLDB_LOG_API));
  LLDB_LOG(log, "breakpoint = {0}, num_locs = {1}", bkpt_sp.get(), num_locs);
  return num_locs;
}

void SBBreakpoint::SetCommandLineCommands(SBStringList &commands) {
  BreakpointSP bkpt_sp = GetSP();
  if (!bkpt_sp)
    return;
  if (commands.GetSize() == 0)
    return;

  std::lock_guard<std::recursive_mutex> guard(
      bkpt_sp->GetTarget().GetAPIMutex());
  std::unique_ptr<BreakpointOptions::CommandData> cmd_data_up(
      new BreakpointOptions::CommandData(*commands, eScriptLanguageNone));

  bkpt_sp->GetOptions()->SetCommandDataCallback(cmd_data_up);
}

bool SBBreakpoint::GetCommandLineCommands(SBStringList &commands) {
  BreakpointSP bkpt_sp = GetSP();
  if (!bkpt_sp)
    return false;
  StringList command_list;
  bool has_commands =
      bkpt_sp->GetOptions()->GetCommandLineCallbacks(command_list);
  if (has_commands)
    commands.AppendList(command_list);
  return has_commands;
}

bool SBBreakpoint::GetDescription(SBStream &s) {
  return GetDescription(s, true);
}

bool SBBreakpoint::GetDescription(SBStream &s, bool include_locations) {
  BreakpointSP bkpt_sp = GetSP();
  if (bkpt_sp) {
    std::lock_guard<std::recursive_mutex> guard(
        bkpt_sp->GetTarget().GetAPIMutex());
    s.Printf("SBBreakpoint: id = %i, ", bkpt_sp->GetID());
    bkpt_sp->GetResolverDescription(s.get());
    bkpt_sp->GetFilterDescription(s.get());
    if (include_locations) {
      const size_t num_locations = bkpt_sp->GetNumLocations();
      s.Printf(", locations = %" PRIu64, (uint64_t)num_locations);
    }
    return true;
  }
  s.Printf("No value");
  return false;
}

SBError
SBBreakpoint::AddLocation(SBAddress &address) {
    BreakpointSP bkpt_sp = GetSP();
    SBError error;
  
    if (!address.IsValid()) {
      error.SetErrorString("Can't add an invalid address.");
      return error;
    }
  
    if (!bkpt_sp) {
      error.SetErrorString("No breakpoint to add a location to.");
      return error;
    }
  
    if (!llvm::isa<BreakpointResolverScripted>(bkpt_sp->GetResolver().get())) {
      error.SetErrorString("Only a scripted resolver can add locations.");
      return error;
    }
  
    if (bkpt_sp->GetSearchFilter()->AddressPasses(address.ref()))
      bkpt_sp->AddLocation(address.ref());
    else
    {
      StreamString s;
      address.get()->Dump(&s, &bkpt_sp->GetTarget(),
                          Address::DumpStyleModuleWithFileAddress);
      error.SetErrorStringWithFormat("Address: %s didn't pass the filter.",
                                     s.GetData());
    }
    return error;
}


void SBBreakpoint
  ::SetCallback(SBBreakpointHitCallback callback,
  void *baton) {
  Log *log(lldb_private::GetLogIfAllCategoriesSet(LIBLLDB_LOG_API));
  BreakpointSP bkpt_sp = GetSP();
  LLDB_LOG(log, "breakpoint = {0}, callback = {1}, baton = {2}", bkpt_sp.get(),
           callback, baton);

  if (bkpt_sp) {
    std::lock_guard<std::recursive_mutex> guard(
        bkpt_sp->GetTarget().GetAPIMutex());
    BatonSP baton_sp(new SBBreakpointCallbackBaton(callback, baton));
    bkpt_sp->SetCallback(SBBreakpointCallbackBaton
      ::PrivateBreakpointHitCallback, baton_sp,
                         false);
  }
}

void SBBreakpoint::SetScriptCallbackFunction(
    const char *callback_function_name) {
  Log *log(lldb_private::GetLogIfAllCategoriesSet(LIBLLDB_LOG_API));
  BreakpointSP bkpt_sp = GetSP();
  LLDB_LOG(log, "breakpoint = {0}, callback = {1}", bkpt_sp.get(),
           callback_function_name);

  if (bkpt_sp) {
    std::lock_guard<std::recursive_mutex> guard(
        bkpt_sp->GetTarget().GetAPIMutex());
    BreakpointOptions *bp_options = bkpt_sp->GetOptions();
    bkpt_sp->GetTarget()
        .GetDebugger()
        .GetCommandInterpreter()
        .GetScriptInterpreter()
        ->SetBreakpointCommandCallbackFunction(bp_options,
                                               callback_function_name);
  }
}

SBError SBBreakpoint::SetScriptCallbackBody(const char *callback_body_text) {
  Log *log(lldb_private::GetLogIfAllCategoriesSet(LIBLLDB_LOG_API));
  BreakpointSP bkpt_sp = GetSP();
  LLDB_LOG(log, "breakpoint = {0}, callback body:\n{1}", bkpt_sp.get(),
           callback_body_text);

  SBError sb_error;
  if (bkpt_sp) {
    std::lock_guard<std::recursive_mutex> guard(
        bkpt_sp->GetTarget().GetAPIMutex());
    BreakpointOptions *bp_options = bkpt_sp->GetOptions();
    Status error =
        bkpt_sp->GetTarget()
            .GetDebugger()
            .GetCommandInterpreter()
            .GetScriptInterpreter()
            ->SetBreakpointCommandCallback(bp_options, callback_body_text);
    sb_error.SetError(error);
  } else
    sb_error.SetErrorString("invalid breakpoint");

  return sb_error;
}

bool SBBreakpoint::AddName(const char *new_name) {
  Log *log(lldb_private::GetLogIfAllCategoriesSet(LIBLLDB_LOG_API));
  BreakpointSP bkpt_sp = GetSP();
  LLDB_LOG(log, "breakpoint = {0}, name = {1}", bkpt_sp.get(), new_name);

  if (bkpt_sp) {
    std::lock_guard<std::recursive_mutex> guard(
        bkpt_sp->GetTarget().GetAPIMutex());
    Status error; // Think I'm just going to swallow the error here, it's
                  // probably more annoying to have to provide it.
    bkpt_sp->GetTarget().AddNameToBreakpoint(bkpt_sp, new_name, error);
    if (error.Fail())
    {
      if (log)
        log->Printf("Failed to add name: '%s' to breakpoint: %s", 
            new_name, error.AsCString());
      return false;
    }
  }

  return true;
}

void SBBreakpoint::RemoveName(const char *name_to_remove) {
  Log *log(lldb_private::GetLogIfAllCategoriesSet(LIBLLDB_LOG_API));
  BreakpointSP bkpt_sp = GetSP();
  LLDB_LOG(log, "breakpoint = {0}, name = {1}", bkpt_sp.get(), name_to_remove);

  if (bkpt_sp) {
    std::lock_guard<std::recursive_mutex> guard(
        bkpt_sp->GetTarget().GetAPIMutex());
    bkpt_sp->GetTarget().RemoveNameFromBreakpoint(bkpt_sp, 
                                                 ConstString(name_to_remove));
  }
}

bool SBBreakpoint::MatchesName(const char *name) {
  Log *log(lldb_private::GetLogIfAllCategoriesSet(LIBLLDB_LOG_API));
  BreakpointSP bkpt_sp = GetSP();
  LLDB_LOG(log, "breakpoint = {0}, name = {1}", bkpt_sp.get(), name);

  if (bkpt_sp) {
    std::lock_guard<std::recursive_mutex> guard(
        bkpt_sp->GetTarget().GetAPIMutex());
    return bkpt_sp->MatchesName(name);
  }

  return false;
}

void SBBreakpoint::GetNames(SBStringList &names) {
  Log *log(lldb_private::GetLogIfAllCategoriesSet(LIBLLDB_LOG_API));
  BreakpointSP bkpt_sp = GetSP();
  LLDB_LOG(log, "breakpoint = {0}", bkpt_sp.get());

  if (bkpt_sp) {
    std::lock_guard<std::recursive_mutex> guard(
        bkpt_sp->GetTarget().GetAPIMutex());
    std::vector<std::string> names_vec;
    bkpt_sp->GetNames(names_vec);
    for (std::string name : names_vec) {
      names.AppendString(name.c_str());
    }
  }
}

bool SBBreakpoint::EventIsBreakpointEvent(const lldb::SBEvent &event) {
  return Breakpoint::BreakpointEventData::GetEventDataFromEvent(event.get()) !=
         nullptr;
}

BreakpointEventType
SBBreakpoint::GetBreakpointEventTypeFromEvent(const SBEvent &event) {
  if (event.IsValid())
    return Breakpoint::BreakpointEventData::GetBreakpointEventTypeFromEvent(
        event.GetSP());
  return eBreakpointEventTypeInvalidType;
}

SBBreakpoint SBBreakpoint::GetBreakpointFromEvent(const lldb::SBEvent &event) {
  if (event.IsValid())
    return SBBreakpoint(
        Breakpoint::BreakpointEventData::GetBreakpointFromEvent(event.GetSP()));
  return SBBreakpoint();
}

SBBreakpointLocation
SBBreakpoint::GetBreakpointLocationAtIndexFromEvent(const lldb::SBEvent &event,
                                                    uint32_t loc_idx) {
  SBBreakpointLocation sb_breakpoint_loc;
  if (event.IsValid())
    sb_breakpoint_loc.SetLocation(
        Breakpoint::BreakpointEventData::GetBreakpointLocationAtIndexFromEvent(
            event.GetSP(), loc_idx));
  return sb_breakpoint_loc;
}

uint32_t
SBBreakpoint::GetNumBreakpointLocationsFromEvent(const lldb::SBEvent &event) {
  uint32_t num_locations = 0;
  if (event.IsValid())
    num_locations =
        (Breakpoint::BreakpointEventData::GetNumBreakpointLocationsFromEvent(
            event.GetSP()));
  return num_locations;
}

bool SBBreakpoint::IsHardware() const {
  BreakpointSP bkpt_sp = GetSP();
  if (bkpt_sp)
    return bkpt_sp->IsHardware();
  return false;
}

BreakpointSP SBBreakpoint::GetSP() const { return m_opaque_wp.lock(); }

// This is simple collection of breakpoint id's and their target.
class SBBreakpointListImpl {
public:
  SBBreakpointListImpl(lldb::TargetSP target_sp) : m_target_wp() {
    if (target_sp && target_sp->IsValid())
      m_target_wp = target_sp;
  }

  ~SBBreakpointListImpl() = default;

  size_t GetSize() { return m_break_ids.size(); }

  BreakpointSP GetBreakpointAtIndex(size_t idx) {
    if (idx >= m_break_ids.size())
      return BreakpointSP();
    TargetSP target_sp = m_target_wp.lock();
    if (!target_sp)
      return BreakpointSP();
    lldb::break_id_t bp_id = m_break_ids[idx];
    return target_sp->GetBreakpointList().FindBreakpointByID(bp_id);
  }

  BreakpointSP FindBreakpointByID(lldb::break_id_t desired_id) {
    TargetSP target_sp = m_target_wp.lock();
    if (!target_sp)
      return BreakpointSP();

    for (lldb::break_id_t &break_id : m_break_ids) {
      if (break_id == desired_id)
        return target_sp->GetBreakpointList().FindBreakpointByID(break_id);
    }
    return BreakpointSP();
  }

  bool Append(BreakpointSP bkpt) {
    TargetSP target_sp = m_target_wp.lock();
    if (!target_sp || !bkpt)
      return false;
    if (bkpt->GetTargetSP() != target_sp)
      return false;
    m_break_ids.push_back(bkpt->GetID());
    return true;
  }

  bool AppendIfUnique(BreakpointSP bkpt) {
    TargetSP target_sp = m_target_wp.lock();
    if (!target_sp || !bkpt)
      return false;
    if (bkpt->GetTargetSP() != target_sp)
      return false;
    lldb::break_id_t bp_id = bkpt->GetID();
    if (find(m_break_ids.begin(), m_break_ids.end(), bp_id) ==
        m_break_ids.end())
      return false;

    m_break_ids.push_back(bkpt->GetID());
    return true;
  }

  bool AppendByID(lldb::break_id_t id) {
    TargetSP target_sp = m_target_wp.lock();
    if (!target_sp)
      return false;
    if (id == LLDB_INVALID_BREAK_ID)
      return false;
    m_break_ids.push_back(id);
    return true;
  }

  void Clear() { m_break_ids.clear(); }

  void CopyToBreakpointIDList(lldb_private::BreakpointIDList &bp_list) {
    for (lldb::break_id_t id : m_break_ids) {
      bp_list.AddBreakpointID(BreakpointID(id));
    }
  }

  TargetSP GetTarget() { return m_target_wp.lock(); }

private:
  std::vector<lldb::break_id_t> m_break_ids;
  TargetWP m_target_wp;
};

SBBreakpointList::SBBreakpointList(SBTarget &target)
    : m_opaque_sp(new SBBreakpointListImpl(target.GetSP())) {}

SBBreakpointList::~SBBreakpointList() {}

size_t SBBreakpointList::GetSize() const {
  if (!m_opaque_sp)
    return 0;
  else
    return m_opaque_sp->GetSize();
}

SBBreakpoint SBBreakpointList::GetBreakpointAtIndex(size_t idx) {
  if (!m_opaque_sp)
    return SBBreakpoint();

  BreakpointSP bkpt_sp = m_opaque_sp->GetBreakpointAtIndex(idx);
  return SBBreakpoint(bkpt_sp);
}

SBBreakpoint SBBreakpointList::FindBreakpointByID(lldb::break_id_t id) {
  if (!m_opaque_sp)
    return SBBreakpoint();
  BreakpointSP bkpt_sp = m_opaque_sp->FindBreakpointByID(id);
  return SBBreakpoint(bkpt_sp);
}

void SBBreakpointList::Append(const SBBreakpoint &sb_bkpt) {
  if (!sb_bkpt.IsValid())
    return;
  if (!m_opaque_sp)
    return;
  m_opaque_sp->Append(sb_bkpt.m_opaque_wp.lock());
}

void SBBreakpointList::AppendByID(lldb::break_id_t id) {
  if (!m_opaque_sp)
    return;
  m_opaque_sp->AppendByID(id);
}

bool SBBreakpointList::AppendIfUnique(const SBBreakpoint &sb_bkpt) {
  if (!sb_bkpt.IsValid())
    return false;
  if (!m_opaque_sp)
    return false;
  return m_opaque_sp->AppendIfUnique(sb_bkpt.GetSP());
}

void SBBreakpointList::Clear() {
  if (m_opaque_sp)
    m_opaque_sp->Clear();
}

void SBBreakpointList::CopyToBreakpointIDList(
    lldb_private::BreakpointIDList &bp_id_list) {
  if (m_opaque_sp)
    m_opaque_sp->CopyToBreakpointIDList(bp_id_list);
}
