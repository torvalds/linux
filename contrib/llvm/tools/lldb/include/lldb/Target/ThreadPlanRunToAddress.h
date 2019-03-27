//===-- ThreadPlanRunToAddress.h --------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef liblldb_ThreadPlanRunToAddress_h_
#define liblldb_ThreadPlanRunToAddress_h_

#include <vector>

#include "lldb/Target/ThreadPlan.h"
#include "lldb/lldb-private.h"

namespace lldb_private {

class ThreadPlanRunToAddress : public ThreadPlan {
public:
  ThreadPlanRunToAddress(Thread &thread, Address &address, bool stop_others);

  ThreadPlanRunToAddress(Thread &thread, lldb::addr_t address,
                         bool stop_others);

  ThreadPlanRunToAddress(Thread &thread,
                         const std::vector<lldb::addr_t> &addresses,
                         bool stop_others);

  ~ThreadPlanRunToAddress() override;

  void GetDescription(Stream *s, lldb::DescriptionLevel level) override;

  bool ValidatePlan(Stream *error) override;

  bool ShouldStop(Event *event_ptr) override;

  bool StopOthers() override;

  void SetStopOthers(bool new_value) override;

  lldb::StateType GetPlanRunState() override;

  bool WillStop() override;

  bool MischiefManaged() override;

protected:
  bool DoPlanExplainsStop(Event *event_ptr) override;

  void SetInitialBreakpoints();
  bool AtOurAddress();

private:
  bool m_stop_others;
  std::vector<lldb::addr_t>
      m_addresses; // This is the address we are going to run to.
                   // TODO: Would it be useful to have multiple addresses?
  std::vector<lldb::break_id_t> m_break_ids; // This is the breakpoint we are
                                             // using to stop us at m_address.

  DISALLOW_COPY_AND_ASSIGN(ThreadPlanRunToAddress);
};

} // namespace lldb_private

#endif // liblldb_ThreadPlanRunToAddress_h_
