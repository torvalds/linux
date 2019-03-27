//===-- SBListener.h --------------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_SBListener_h_
#define LLDB_SBListener_h_

#include "lldb/API/SBDefines.h"

namespace lldb {

class LLDB_API SBListener {
public:
  SBListener();

  SBListener(const char *name);

  SBListener(const SBListener &rhs);

  ~SBListener();

  const lldb::SBListener &operator=(const lldb::SBListener &rhs);

  void AddEvent(const lldb::SBEvent &event);

  void Clear();

  bool IsValid() const;

  uint32_t StartListeningForEventClass(SBDebugger &debugger,
                                       const char *broadcaster_class,
                                       uint32_t event_mask);

  bool StopListeningForEventClass(SBDebugger &debugger,
                                  const char *broadcaster_class,
                                  uint32_t event_mask);

  uint32_t StartListeningForEvents(const lldb::SBBroadcaster &broadcaster,
                                   uint32_t event_mask);

  bool StopListeningForEvents(const lldb::SBBroadcaster &broadcaster,
                              uint32_t event_mask);

  // Returns true if an event was received, false if we timed out.
  bool WaitForEvent(uint32_t num_seconds, lldb::SBEvent &event);

  bool WaitForEventForBroadcaster(uint32_t num_seconds,
                                  const lldb::SBBroadcaster &broadcaster,
                                  lldb::SBEvent &sb_event);

  bool WaitForEventForBroadcasterWithType(
      uint32_t num_seconds, const lldb::SBBroadcaster &broadcaster,
      uint32_t event_type_mask, lldb::SBEvent &sb_event);

  bool PeekAtNextEvent(lldb::SBEvent &sb_event);

  bool PeekAtNextEventForBroadcaster(const lldb::SBBroadcaster &broadcaster,
                                     lldb::SBEvent &sb_event);

  bool
  PeekAtNextEventForBroadcasterWithType(const lldb::SBBroadcaster &broadcaster,
                                        uint32_t event_type_mask,
                                        lldb::SBEvent &sb_event);

  bool GetNextEvent(lldb::SBEvent &sb_event);

  bool GetNextEventForBroadcaster(const lldb::SBBroadcaster &broadcaster,
                                  lldb::SBEvent &sb_event);

  bool
  GetNextEventForBroadcasterWithType(const lldb::SBBroadcaster &broadcaster,
                                     uint32_t event_type_mask,
                                     lldb::SBEvent &sb_event);

  bool HandleBroadcastEvent(const lldb::SBEvent &event);

protected:
  friend class SBAttachInfo;
  friend class SBBroadcaster;
  friend class SBCommandInterpreter;
  friend class SBDebugger;
  friend class SBLaunchInfo;
  friend class SBTarget;

  SBListener(const lldb::ListenerSP &listener_sp);

  lldb::ListenerSP GetSP();

private:
  lldb_private::Listener *operator->() const;

  lldb_private::Listener *get() const;

  void reset(lldb::ListenerSP listener_sp);

  lldb::ListenerSP m_opaque_sp;
  lldb_private::Listener *m_unused_ptr;
};

} // namespace lldb

#endif // LLDB_SBListener_h_
