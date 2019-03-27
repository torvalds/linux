//===-- SBBreakpointLocation.h ----------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_SBBreakpointLocation_h_
#define LLDB_SBBreakpointLocation_h_

#include "lldb/API/SBBreakpoint.h"
#include "lldb/API/SBDefines.h"

namespace lldb {

class LLDB_API SBBreakpointLocation {
public:
  SBBreakpointLocation();

  SBBreakpointLocation(const lldb::SBBreakpointLocation &rhs);

  ~SBBreakpointLocation();

  const lldb::SBBreakpointLocation &
  operator=(const lldb::SBBreakpointLocation &rhs);

  break_id_t GetID();

  bool IsValid() const;

  lldb::SBAddress GetAddress();

  lldb::addr_t GetLoadAddress();

  void SetEnabled(bool enabled);

  bool IsEnabled();

  uint32_t GetHitCount();

  uint32_t GetIgnoreCount();

  void SetIgnoreCount(uint32_t n);

  void SetCondition(const char *condition);

  const char *GetCondition();
   
  void SetAutoContinue(bool auto_continue);

  bool GetAutoContinue();

  void SetScriptCallbackFunction(const char *callback_function_name);

  SBError SetScriptCallbackBody(const char *script_body_text);
  
  void SetCommandLineCommands(SBStringList &commands);

  bool GetCommandLineCommands(SBStringList &commands);
 
  void SetThreadID(lldb::tid_t sb_thread_id);

  lldb::tid_t GetThreadID();

  void SetThreadIndex(uint32_t index);

  uint32_t GetThreadIndex() const;

  void SetThreadName(const char *thread_name);

  const char *GetThreadName() const;

  void SetQueueName(const char *queue_name);

  const char *GetQueueName() const;

  bool IsResolved();

  bool GetDescription(lldb::SBStream &description, DescriptionLevel level);

  SBBreakpoint GetBreakpoint();

  SBBreakpointLocation(const lldb::BreakpointLocationSP &break_loc_sp);

private:
  friend class SBBreakpoint;
  friend class SBBreakpointCallbackBaton;

  void SetLocation(const lldb::BreakpointLocationSP &break_loc_sp);
  BreakpointLocationSP GetSP() const;

  lldb::BreakpointLocationWP m_opaque_wp;
};

} // namespace lldb

#endif // LLDB_SBBreakpointLocation_h_
