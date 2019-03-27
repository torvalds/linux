//===-- HistoryUnwind.cpp ---------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "lldb/lldb-private.h"

#include "Plugins/Process/Utility/HistoryUnwind.h"
#include "Plugins/Process/Utility/RegisterContextHistory.h"

#include "lldb/Target/Process.h"
#include "lldb/Target/StackFrame.h"
#include "lldb/Target/Target.h"
#include "lldb/Target/Thread.h"

using namespace lldb;
using namespace lldb_private;

// Constructor

HistoryUnwind::HistoryUnwind(Thread &thread, std::vector<lldb::addr_t> pcs,
                             bool stop_id_is_valid)
    : Unwind(thread), m_pcs(pcs), m_stop_id_is_valid(stop_id_is_valid) {}

// Destructor

HistoryUnwind::~HistoryUnwind() {}

void HistoryUnwind::DoClear() {
  std::lock_guard<std::recursive_mutex> guard(m_unwind_mutex);
  m_pcs.clear();
  m_stop_id_is_valid = false;
}

lldb::RegisterContextSP
HistoryUnwind::DoCreateRegisterContextForFrame(StackFrame *frame) {
  RegisterContextSP rctx;
  if (frame) {
    addr_t pc = frame->GetFrameCodeAddress().GetLoadAddress(
        &frame->GetThread()->GetProcess()->GetTarget());
    if (pc != LLDB_INVALID_ADDRESS) {
      rctx.reset(new RegisterContextHistory(
          *frame->GetThread().get(), frame->GetConcreteFrameIndex(),
          frame->GetThread()->GetProcess()->GetAddressByteSize(), pc));
    }
  }
  return rctx;
}

bool HistoryUnwind::DoGetFrameInfoAtIndex(uint32_t frame_idx, lldb::addr_t &cfa,
                                          lldb::addr_t &pc) {
  // FIXME do not throw away the lock after we acquire it..
  std::unique_lock<std::recursive_mutex> guard(m_unwind_mutex);
  guard.unlock();
  if (frame_idx < m_pcs.size()) {
    cfa = frame_idx;
    pc = m_pcs[frame_idx];
    return true;
  }
  return false;
}

uint32_t HistoryUnwind::DoGetFrameCount() { return m_pcs.size(); }
