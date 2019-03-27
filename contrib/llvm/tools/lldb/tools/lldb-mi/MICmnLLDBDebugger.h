//===-- MICmnLLDBDebugger.h -------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#pragma once

// Third party headers
#include "lldb/API/SBDebugger.h"
#include "lldb/API/SBEvent.h"
#include "lldb/API/SBListener.h"
#include <condition_variable>
#include <map>
#include <mutex>

// In-house headers:
#include "MICmnBase.h"
#include "MIUtilSingletonBase.h"
#include "MIUtilThreadBaseStd.h"

// Declarations:
class CMIDriverBase;
class CMICmnLLDBDebuggerHandleEvents;

//++
//============================================================================
// Details: MI proxy/adapter for the LLDB public SBDebugger API. The CMIDriver
//          requires *this object. Command classes make calls on *this object
//          to facilitate their work effort. The instance runs in its own worker
//          thread.
//          A singleton class.
//--
class CMICmnLLDBDebugger : public CMICmnBase,
                           public CMIUtilThreadActiveObjBase,
                           public MI::ISingleton<CMICmnLLDBDebugger> {
  friend class MI::ISingleton<CMICmnLLDBDebugger>;

  // Methods:
public:
  bool Initialize() override;
  bool Shutdown() override;

  bool SetDriver(const CMIDriverBase &vClientDriver);
  CMIDriverBase &GetDriver() const;
  lldb::SBDebugger &GetTheDebugger();
  lldb::SBListener &GetTheListener();
  void WaitForHandleEvent();
  bool CheckIfNeedToRebroadcastStopEvent();
  void RebroadcastStopEvent();

  // MI Commands can use these functions to listen for events they require
  bool RegisterForEvent(const CMIUtilString &vClientName,
                        const CMIUtilString &vBroadcasterClass,
                        const MIuint vEventMask);
  bool UnregisterForEvent(const CMIUtilString &vClientName,
                          const CMIUtilString &vBroadcasterClass);
  bool RegisterForEvent(const CMIUtilString &vClientName,
                        const lldb::SBBroadcaster &vBroadcaster,
                        const MIuint vEventMask);
  bool UnregisterForEvent(const CMIUtilString &vClientName,
                          const lldb::SBBroadcaster &vBroadcaster);

  // Overridden:
public:
  // From CMIUtilThreadActiveObjBase
  const CMIUtilString &ThreadGetName() const override;

  // Overridden:
protected:
  // From CMIUtilThreadActiveObjBase
  bool ThreadRun(bool &vrIsAlive) override;
  bool ThreadFinish() override;

  // Typedefs:
private:
  typedef std::map<CMIUtilString, MIuint> MapBroadcastClassNameToEventMask_t;
  typedef std::pair<CMIUtilString, MIuint>
      MapPairBroadcastClassNameToEventMask_t;
  typedef std::map<CMIUtilString, MIuint> MapIdToEventMask_t;
  typedef std::pair<CMIUtilString, MIuint> MapPairIdToEventMask_t;

  // Methods:
private:
  /* ctor */ CMICmnLLDBDebugger();
  /* ctor */ CMICmnLLDBDebugger(const CMICmnLLDBDebugger &);
  void operator=(const CMICmnLLDBDebugger &);

  bool InitSBDebugger();
  bool InitSBListener();
  bool InitStdStreams();
  bool MonitorSBListenerEvents(bool &vrbYesExit);

  bool BroadcasterGetMask(const CMIUtilString &vBroadcasterClass,
                          MIuint &vEventMask) const;
  bool BroadcasterRemoveMask(const CMIUtilString &vBroadcasterClass);
  bool BroadcasterSaveMask(const CMIUtilString &vBroadcasterClass,
                           const MIuint vEventMask);

  MIuint
  ClientGetMaskForAllClients(const CMIUtilString &vBroadcasterClass) const;
  bool ClientSaveMask(const CMIUtilString &vClientName,
                      const CMIUtilString &vBroadcasterClass,
                      const MIuint vEventMask);
  bool ClientRemoveTheirMask(const CMIUtilString &vClientName,
                             const CMIUtilString &vBroadcasterClass);
  bool ClientGetTheirMask(const CMIUtilString &vClientName,
                          const CMIUtilString &vBroadcasterClass,
                          MIuint &vwEventMask);
  bool LoadMIFormatters(lldb::SBTypeCategory miCategory);
  bool RegisterMISummaryProviders();
  // Overridden:
private:
  // From CMICmnBase
  /* dtor */ ~CMICmnLLDBDebugger() override;

  // Attributes:
private:
  CMIDriverBase
      *m_pClientDriver; // The driver that wants to use *this LLDB debugger
  lldb::SBDebugger m_lldbDebugger; // SBDebugger is the primordial object that
                                   // creates SBTargets and provides access to
                                   // them
  lldb::SBListener m_lldbListener; // API clients can register its own listener
                                   // to debugger events
  const CMIUtilString m_constStrThisThreadId;
  MapBroadcastClassNameToEventMask_t m_mapBroadcastClassNameToEventMask;
  MapIdToEventMask_t m_mapIdToEventMask;
  std::mutex m_mutexEventQueue;
  std::condition_variable m_conditionEventQueueEmpty;
  uint32_t m_nLastStopId;
};
