//===-- SBEvent.h -----------------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_SBEvent_h_
#define LLDB_SBEvent_h_

#include "lldb/API/SBDefines.h"

#include <stdio.h>
#include <vector>

namespace lldb {

class SBBroadcaster;

class LLDB_API SBEvent {
public:
  SBEvent();

  SBEvent(const lldb::SBEvent &rhs);

  // Make an event that contains a C string.
  SBEvent(uint32_t event, const char *cstr, uint32_t cstr_len);

  SBEvent(lldb::EventSP &event_sp);

  SBEvent(lldb_private::Event *event_sp);

  ~SBEvent();

  const SBEvent &operator=(const lldb::SBEvent &rhs);

  bool IsValid() const;

  const char *GetDataFlavor();

  uint32_t GetType() const;

  lldb::SBBroadcaster GetBroadcaster() const;

  const char *GetBroadcasterClass() const;

  bool BroadcasterMatchesPtr(const lldb::SBBroadcaster *broadcaster);

  bool BroadcasterMatchesRef(const lldb::SBBroadcaster &broadcaster);

  void Clear();

  static const char *GetCStringFromEvent(const lldb::SBEvent &event);

  bool GetDescription(lldb::SBStream &description);

  bool GetDescription(lldb::SBStream &description) const;

protected:
  friend class SBListener;
  friend class SBBroadcaster;
  friend class SBBreakpoint;
  friend class SBDebugger;
  friend class SBProcess;
  friend class SBTarget;
  friend class SBThread;
  friend class SBWatchpoint;

  lldb::EventSP &GetSP() const;

  void reset(lldb::EventSP &event_sp);

  void reset(lldb_private::Event *event);

  lldb_private::Event *get() const;

private:
  mutable lldb::EventSP m_event_sp;
  mutable lldb_private::Event *m_opaque_ptr;
};

} // namespace lldb

#endif // LLDB_SBEvent_h_
