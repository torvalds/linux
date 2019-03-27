//===-- ThreadPlanRunToAddress.cpp ------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "lldb/Target/ThreadPlanRunToAddress.h"
#include "lldb/Target/Process.h"
#include "lldb/Target/RegisterContext.h"
#include "lldb/Target/Target.h"
#include "lldb/Target/Thread.h"
#include "lldb/Utility/Log.h"
#include "lldb/Utility/Stream.h"

using namespace lldb;
using namespace lldb_private;

//----------------------------------------------------------------------
// ThreadPlanRunToAddress: Continue plan
//----------------------------------------------------------------------

ThreadPlanRunToAddress::ThreadPlanRunToAddress(Thread &thread, Address &address,
                                               bool stop_others)
    : ThreadPlan(ThreadPlan::eKindRunToAddress, "Run to address plan", thread,
                 eVoteNoOpinion, eVoteNoOpinion),
      m_stop_others(stop_others), m_addresses(), m_break_ids() {
  m_addresses.push_back(
      address.GetOpcodeLoadAddress(m_thread.CalculateTarget().get()));
  SetInitialBreakpoints();
}

ThreadPlanRunToAddress::ThreadPlanRunToAddress(Thread &thread,
                                               lldb::addr_t address,
                                               bool stop_others)
    : ThreadPlan(ThreadPlan::eKindRunToAddress, "Run to address plan", thread,
                 eVoteNoOpinion, eVoteNoOpinion),
      m_stop_others(stop_others), m_addresses(), m_break_ids() {
  m_addresses.push_back(
      m_thread.CalculateTarget()->GetOpcodeLoadAddress(address));
  SetInitialBreakpoints();
}

ThreadPlanRunToAddress::ThreadPlanRunToAddress(
    Thread &thread, const std::vector<lldb::addr_t> &addresses,
    bool stop_others)
    : ThreadPlan(ThreadPlan::eKindRunToAddress, "Run to address plan", thread,
                 eVoteNoOpinion, eVoteNoOpinion),
      m_stop_others(stop_others), m_addresses(addresses), m_break_ids() {
  // Convert all addresses into opcode addresses to make sure we set
  // breakpoints at the correct address.
  Target &target = thread.GetProcess()->GetTarget();
  std::vector<lldb::addr_t>::iterator pos, end = m_addresses.end();
  for (pos = m_addresses.begin(); pos != end; ++pos)
    *pos = target.GetOpcodeLoadAddress(*pos);

  SetInitialBreakpoints();
}

void ThreadPlanRunToAddress::SetInitialBreakpoints() {
  size_t num_addresses = m_addresses.size();
  m_break_ids.resize(num_addresses);

  for (size_t i = 0; i < num_addresses; i++) {
    Breakpoint *breakpoint;
    breakpoint = m_thread.CalculateTarget()
                     ->CreateBreakpoint(m_addresses[i], true, false)
                     .get();
    if (breakpoint != nullptr) {
      if (breakpoint->IsHardware() && !breakpoint->HasResolvedLocations())
        m_could_not_resolve_hw_bp = true;
      m_break_ids[i] = breakpoint->GetID();
      breakpoint->SetThreadID(m_thread.GetID());
      breakpoint->SetBreakpointKind("run-to-address");
    }
  }
}

ThreadPlanRunToAddress::~ThreadPlanRunToAddress() {
  size_t num_break_ids = m_break_ids.size();
  for (size_t i = 0; i < num_break_ids; i++) {
    m_thread.CalculateTarget()->RemoveBreakpointByID(m_break_ids[i]);
  }
  m_could_not_resolve_hw_bp = false;
}

void ThreadPlanRunToAddress::GetDescription(Stream *s,
                                            lldb::DescriptionLevel level) {
  size_t num_addresses = m_addresses.size();

  if (level == lldb::eDescriptionLevelBrief) {
    if (num_addresses == 0) {
      s->Printf("run to address with no addresses given.");
      return;
    } else if (num_addresses == 1)
      s->Printf("run to address: ");
    else
      s->Printf("run to addresses: ");

    for (size_t i = 0; i < num_addresses; i++) {
      s->Address(m_addresses[i], sizeof(addr_t));
      s->Printf(" ");
    }
  } else {
    if (num_addresses == 0) {
      s->Printf("run to address with no addresses given.");
      return;
    } else if (num_addresses == 1)
      s->Printf("Run to address: ");
    else {
      s->Printf("Run to addresses: ");
    }

    for (size_t i = 0; i < num_addresses; i++) {
      if (num_addresses > 1) {
        s->Printf("\n");
        s->Indent();
      }

      s->Address(m_addresses[i], sizeof(addr_t));
      s->Printf(" using breakpoint: %d - ", m_break_ids[i]);
      Breakpoint *breakpoint =
          m_thread.CalculateTarget()->GetBreakpointByID(m_break_ids[i]).get();
      if (breakpoint)
        breakpoint->Dump(s);
      else
        s->Printf("but the breakpoint has been deleted.");
    }
  }
}

bool ThreadPlanRunToAddress::ValidatePlan(Stream *error) {
  if (m_could_not_resolve_hw_bp) {
    if (error)
      error->Printf("Could not set hardware breakpoint(s)");
    return false;
  }

  // If we couldn't set the breakpoint for some reason, then this won't work.
  bool all_bps_good = true;
  size_t num_break_ids = m_break_ids.size();
  for (size_t i = 0; i < num_break_ids; i++) {
    if (m_break_ids[i] == LLDB_INVALID_BREAK_ID) {
      all_bps_good = false;
      if (error) {
        error->Printf("Could not set breakpoint for address: ");
        error->Address(m_addresses[i], sizeof(addr_t));
        error->Printf("\n");
      }
    }
  }
  return all_bps_good;
}

bool ThreadPlanRunToAddress::DoPlanExplainsStop(Event *event_ptr) {
  return AtOurAddress();
}

bool ThreadPlanRunToAddress::ShouldStop(Event *event_ptr) {
  return AtOurAddress();
}

bool ThreadPlanRunToAddress::StopOthers() { return m_stop_others; }

void ThreadPlanRunToAddress::SetStopOthers(bool new_value) {
  m_stop_others = new_value;
}

StateType ThreadPlanRunToAddress::GetPlanRunState() { return eStateRunning; }

bool ThreadPlanRunToAddress::WillStop() { return true; }

bool ThreadPlanRunToAddress::MischiefManaged() {
  Log *log(lldb_private::GetLogIfAllCategoriesSet(LIBLLDB_LOG_STEP));

  if (AtOurAddress()) {
    // Remove the breakpoint
    size_t num_break_ids = m_break_ids.size();

    for (size_t i = 0; i < num_break_ids; i++) {
      if (m_break_ids[i] != LLDB_INVALID_BREAK_ID) {
        m_thread.CalculateTarget()->RemoveBreakpointByID(m_break_ids[i]);
        m_break_ids[i] = LLDB_INVALID_BREAK_ID;
      }
    }
    if (log)
      log->Printf("Completed run to address plan.");
    ThreadPlan::MischiefManaged();
    return true;
  } else
    return false;
}

bool ThreadPlanRunToAddress::AtOurAddress() {
  lldb::addr_t current_address = m_thread.GetRegisterContext()->GetPC();
  bool found_it = false;
  size_t num_addresses = m_addresses.size();
  for (size_t i = 0; i < num_addresses; i++) {
    if (m_addresses[i] == current_address) {
      found_it = true;
      break;
    }
  }
  return found_it;
}
