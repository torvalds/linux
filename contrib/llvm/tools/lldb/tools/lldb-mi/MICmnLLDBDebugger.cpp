//===-- MICmnLLDBDebugger.cpp -----------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

// Third party headers:
#include "lldb/API/SBCommandInterpreter.h"
#include "lldb/API/SBProcess.h"
#include "lldb/API/SBStream.h"
#include "lldb/API/SBTarget.h"
#include "lldb/API/SBThread.h"
#include "lldb/API/SBType.h"
#include "lldb/API/SBTypeCategory.h"
#include "lldb/API/SBTypeNameSpecifier.h"
#include "lldb/API/SBTypeSummary.h"
#include <cassert>

// In-house headers:
#include "MICmnLLDBDebugSessionInfo.h"
#include "MICmnLLDBDebugger.h"
#include "MICmnLLDBDebuggerHandleEvents.h"
#include "MICmnLog.h"
#include "MICmnResources.h"
#include "MICmnThreadMgrStd.h"
#include "MIDriverBase.h"
#include "MIUtilSingletonHelper.h"

//++
//------------------------------------------------------------------------------------
// MI private summary providers
static inline bool MI_char_summary_provider(lldb::SBValue value,
                                            lldb::SBTypeSummaryOptions options,
                                            lldb::SBStream &stream) {
  if (!value.IsValid())
    return false;

  lldb::SBType value_type = value.GetType();
  if (!value_type.IsValid())
    return false;

  lldb::BasicType type_code = value_type.GetBasicType();
  if (type_code == lldb::eBasicTypeSignedChar)
    stream.Printf("%d %s", (int)value.GetValueAsSigned(), value.GetValue());
  else if (type_code == lldb::eBasicTypeUnsignedChar)
    stream.Printf("%u %s", (unsigned)value.GetValueAsUnsigned(),
                  value.GetValue());
  else
    return false;

  return true;
}

//++
//------------------------------------------------------------------------------------
// MI summary helper routines
static inline bool MI_add_summary(lldb::SBTypeCategory category,
                                  const char *typeName,
                                  lldb::SBTypeSummary::FormatCallback cb,
                                  uint32_t options, bool regex = false) {
#if defined(LLDB_DISABLE_PYTHON)
  return false;
#else
  lldb::SBTypeSummary summary =
      lldb::SBTypeSummary::CreateWithCallback(cb, options);
  return summary.IsValid()
             ? category.AddTypeSummary(
                   lldb::SBTypeNameSpecifier(typeName, regex), summary)
             : false;
#endif
}

//++
//------------------------------------------------------------------------------------
// Details: CMICmnLLDBDebugger constructor.
// Type:    Method.
// Args:    None.
// Return:  None.
// Throws:  None.
//--
CMICmnLLDBDebugger::CMICmnLLDBDebugger()
    : m_constStrThisThreadId("MI debugger event") {}

//++
//------------------------------------------------------------------------------------
// Details: CMICmnLLDBDebugger destructor.
// Type:    Overridable.
// Args:    None.
// Return:  None.
// Throws:  None.
//--
CMICmnLLDBDebugger::~CMICmnLLDBDebugger() { Shutdown(); }

//++
//------------------------------------------------------------------------------------
// Details: Initialize resources for *this debugger object.
// Type:    Method.
// Args:    None.
// Return:  MIstatus::success - Functionality succeeded.
//          MIstatus::failure - Functionality failed.
// Throws:  None.
//--
bool CMICmnLLDBDebugger::Initialize() {
  m_clientUsageRefCnt++;

  if (m_bInitialized)
    return MIstatus::success;

  bool bOk = MIstatus::success;
  CMIUtilString errMsg;
  ClrErrorDescription();

  if (m_pClientDriver == nullptr) {
    bOk = false;
    errMsg = MIRSRC(IDS_LLDBDEBUGGER_ERR_CLIENTDRIVER);
  }

  // Note initialization order is important here as some resources depend on
  // previous
  MI::ModuleInit<CMICmnLog>(IDS_MI_INIT_ERR_LOG, bOk, errMsg);
  MI::ModuleInit<CMICmnResources>(IDS_MI_INIT_ERR_RESOURCES, bOk, errMsg);
  MI::ModuleInit<CMICmnThreadMgrStd>(IDS_MI_INIT_ERR_THREADMGR, bOk, errMsg);
  MI::ModuleInit<CMICmnLLDBDebuggerHandleEvents>(
      IDS_MI_INIT_ERR_OUTOFBANDHANDLER, bOk, errMsg);
  MI::ModuleInit<CMICmnLLDBDebugSessionInfo>(IDS_MI_INIT_ERR_DEBUGSESSIONINFO,
                                             bOk, errMsg);

  // Note order is important here!
  if (bOk)
    lldb::SBDebugger::Initialize();
  if (bOk && !InitSBDebugger()) {
    bOk = false;
    if (!errMsg.empty())
      errMsg += ", ";
    errMsg += GetErrorDescription().c_str();
  }
  if (bOk && !InitSBListener()) {
    bOk = false;
    if (!errMsg.empty())
      errMsg += ", ";
    errMsg += GetErrorDescription().c_str();
  }
  bOk = bOk && InitStdStreams();
  bOk = bOk && RegisterMISummaryProviders();
  m_bInitialized = bOk;

  if (!bOk && !HaveErrorDescription()) {
    CMIUtilString strInitError(CMIUtilString::Format(
        MIRSRC(IDS_MI_INIT_ERR_LLDBDEBUGGER), errMsg.c_str()));
    SetErrorDescription(strInitError);
  }

  return bOk;
}

//++
//------------------------------------------------------------------------------------
// Details: Release resources for *this debugger object.
// Type:    Method.
// Args:    None.
// Return:  MIstatus::success - Functionality succeeded.
//          MIstatus::failure - Functionality failed.
// Throws:  None.
//--
bool CMICmnLLDBDebugger::Shutdown() {
  if (--m_clientUsageRefCnt > 0)
    return MIstatus::success;

  if (!m_bInitialized)
    return MIstatus::success;

  m_bInitialized = false;

  ClrErrorDescription();

  bool bOk = MIstatus::success;
  CMIUtilString errMsg;

  // Explicitly delete the remote target in case MI needs to exit prematurely
  // otherwise
  // LLDB debugger may hang in its Destroy() fn waiting on events
  lldb::SBTarget sbTarget = CMICmnLLDBDebugSessionInfo::Instance().GetTarget();
  m_lldbDebugger.DeleteTarget(sbTarget);

  // Debug: May need this but does seem to work without it so commented out the
  // fudge 19/06/2014
  // It appears we need to wait as hang does not occur when hitting a debug
  // breakpoint here
  // const std::chrono::milliseconds time( 1000 );
  // std::this_thread::sleep_for( time );

  lldb::SBDebugger::Destroy(m_lldbDebugger);
  lldb::SBDebugger::Terminate();
  m_pClientDriver = nullptr;
  m_mapBroadcastClassNameToEventMask.clear();
  m_mapIdToEventMask.clear();

  // Note shutdown order is important here
  MI::ModuleShutdown<CMICmnLLDBDebugSessionInfo>(
      IDS_MI_INIT_ERR_DEBUGSESSIONINFO, bOk, errMsg);
  MI::ModuleShutdown<CMICmnLLDBDebuggerHandleEvents>(
      IDS_MI_INIT_ERR_OUTOFBANDHANDLER, bOk, errMsg);
  MI::ModuleShutdown<CMICmnThreadMgrStd>(IDS_MI_INIT_ERR_THREADMGR, bOk,
                                         errMsg);
  MI::ModuleShutdown<CMICmnResources>(IDS_MI_INIT_ERR_RESOURCES, bOk, errMsg);
  MI::ModuleShutdown<CMICmnLog>(IDS_MI_INIT_ERR_LOG, bOk, errMsg);

  if (!bOk) {
    SetErrorDescriptionn(MIRSRC(IDS_MI_SHTDWN_ERR_LLDBDEBUGGER),
                         errMsg.c_str());
  }

  return MIstatus::success;
}

//++
//------------------------------------------------------------------------------------
// Details: Return the LLDB debugger instance created for this debug session.
// Type:    Method.
// Args:    None.
// Return:  lldb::SBDebugger & - LLDB debugger object reference.
// Throws:  None.
//--
lldb::SBDebugger &CMICmnLLDBDebugger::GetTheDebugger() {
  return m_lldbDebugger;
}

//++
//------------------------------------------------------------------------------------
// Details: Return the LLDB listener instance created for this debug session.
// Type:    Method.
// Args:    None.
// Return:  lldb::SBListener & - LLDB listener object reference.
// Throws:  None.
//--
lldb::SBListener &CMICmnLLDBDebugger::GetTheListener() {
  return m_lldbListener;
}

//++
//------------------------------------------------------------------------------------
// Details: Set the client driver that wants to use *this LLDB debugger. Call
// this function
//          prior to Initialize().
// Type:    Method.
// Args:    vClientDriver   - (R) A driver.
// Return:  MIstatus::success - Functionality succeeded.
//          MIstatus::failure - Functionality failed.
// Throws:  None.
//--
bool CMICmnLLDBDebugger::SetDriver(const CMIDriverBase &vClientDriver) {
  m_pClientDriver = const_cast<CMIDriverBase *>(&vClientDriver);

  return MIstatus::success;
}

//++
//------------------------------------------------------------------------------------
// Details: Get the client driver that is use *this LLDB debugger.
// Type:    Method.
// Args:    vClientDriver   - (R) A driver.
// Return:  CMIDriverBase & - A driver instance.
// Throws:  None.
//--
CMIDriverBase &CMICmnLLDBDebugger::GetDriver() const {
  return *m_pClientDriver;
}

//++
//------------------------------------------------------------------------------------
// Details: Wait until all events have been handled.
//          This function works in pair with
//          CMICmnLLDBDebugger::MonitorSBListenerEvents
//          that handles events from queue. When all events were handled and
//          queue is
//          empty the MonitorSBListenerEvents notifies this function that it's
//          ready to
//          go on. To synchronize them the m_mutexEventQueue and
//          m_conditionEventQueueEmpty are used.
// Type:    Method.
// Args:    None.
// Return:  None.
// Throws:  None.
//--
void CMICmnLLDBDebugger::WaitForHandleEvent() {
  std::unique_lock<std::mutex> lock(m_mutexEventQueue);

  lldb::SBEvent event;
  if (ThreadIsActive() && m_lldbListener.PeekAtNextEvent(event))
    m_conditionEventQueueEmpty.wait(lock);
}

//++
//------------------------------------------------------------------------------------
// Details: Check if need to rebroadcast stop event. This function will return
// true if
//          debugger is in synchronouse mode. In such case the
//          CMICmnLLDBDebugger::RebroadcastStopEvent should be called to
//          rebroadcast
//          a new stop event (if any).
// Type:    Method.
// Args:    None.
// Return:  bool    - True = Need to rebroadcast stop event, false = otherwise.
// Throws:  None.
//--
bool CMICmnLLDBDebugger::CheckIfNeedToRebroadcastStopEvent() {
  CMICmnLLDBDebugSessionInfo &rSessionInfo(
      CMICmnLLDBDebugSessionInfo::Instance());
  if (!rSessionInfo.GetDebugger().GetAsync()) {
    const bool include_expression_stops = false;
    m_nLastStopId =
        CMICmnLLDBDebugSessionInfo::Instance().GetProcess().GetStopID(
            include_expression_stops);
    return true;
  }

  return false;
}

//++
//------------------------------------------------------------------------------------
// Details: Rebroadcast stop event if needed. This function should be called
// only if the
//          CMICmnLLDBDebugger::CheckIfNeedToRebroadcastStopEvent() returned
//          true.
// Type:    Method.
// Args:    None.
// Return:  None.
// Throws:  None.
//--
void CMICmnLLDBDebugger::RebroadcastStopEvent() {
  lldb::SBProcess process = CMICmnLLDBDebugSessionInfo::Instance().GetProcess();
  const bool include_expression_stops = false;
  const uint32_t nStopId = process.GetStopID(include_expression_stops);
  if (m_nLastStopId != nStopId) {
    lldb::SBEvent event = process.GetStopEventForStopID(nStopId);
    process.GetBroadcaster().BroadcastEvent(event);
  }
}

//++
//------------------------------------------------------------------------------------
// Details: Initialize the LLDB Debugger object.
// Type:    Method.
// Args:    None.
// Return:  MIstatus::success - Functionality succeeded.
//          MIstatus::failure - Functionality failed.
// Throws:  None.
//--
bool CMICmnLLDBDebugger::InitSBDebugger() {
  m_lldbDebugger = lldb::SBDebugger::Create(false);
  if (!m_lldbDebugger.IsValid()) {
    SetErrorDescription(MIRSRC(IDS_LLDBDEBUGGER_ERR_INVALIDDEBUGGER));
    return MIstatus::failure;
  }

  m_lldbDebugger.GetCommandInterpreter().SetPromptOnQuit(false);

  return MIstatus::success;
}

//++
//------------------------------------------------------------------------------------
// Details: Set the LLDB Debugger's std in, err and out streams. (Not
// implemented left
//          here for reference. Was called in the
//          CMICmnLLDBDebugger::Initialize() )
// Type:    Method.
// Args:    None.
// Return:  MIstatus::success - Functionality succeeded.
//          MIstatus::failure - Functionality failed.
// Throws:  None.
//--
bool CMICmnLLDBDebugger::InitStdStreams() {
  // This is not required when operating the MI driver's code as it has its own
  // streams. Setting the Stdin for the lldbDebugger especially on LINUX will
  // cause
  // another thread to run and partially consume stdin data meant for MI stdin
  // handler
  // m_lldbDebugger.SetErrorFileHandle( m_pClientDriver->GetStderr(), false );
  // m_lldbDebugger.SetOutputFileHandle( m_pClientDriver->GetStdout(), false );
  // m_lldbDebugger.SetInputFileHandle( m_pClientDriver->GetStdin(), false );

  return MIstatus::success;
}

//++
//------------------------------------------------------------------------------------
// Details: Set up the events from the SBDebugger's we would like to listen to.
// Type:    Method.
// Args:    None.
// Return:  MIstatus::success - Functionality succeeded.
//          MIstatus::failure - Functionality failed.
// Throws:  None.
//--
bool CMICmnLLDBDebugger::InitSBListener() {
  m_lldbListener = m_lldbDebugger.GetListener();
  if (!m_lldbListener.IsValid()) {
    SetErrorDescription(MIRSRC(IDS_LLDBDEBUGGER_ERR_INVALIDLISTENER));
    return MIstatus::failure;
  }

  const CMIUtilString strDbgId("CMICmnLLDBDebugger1");
  MIuint eventMask = lldb::SBTarget::eBroadcastBitBreakpointChanged |
                     lldb::SBTarget::eBroadcastBitModulesLoaded |
                     lldb::SBTarget::eBroadcastBitModulesUnloaded |
                     lldb::SBTarget::eBroadcastBitWatchpointChanged |
                     lldb::SBTarget::eBroadcastBitSymbolsLoaded;
  bool bOk = RegisterForEvent(
      strDbgId, CMIUtilString(lldb::SBTarget::GetBroadcasterClassName()),
      eventMask);

  eventMask = lldb::SBThread::eBroadcastBitStackChanged;
  bOk = bOk &&
        RegisterForEvent(
            strDbgId, CMIUtilString(lldb::SBThread::GetBroadcasterClassName()),
            eventMask);

  eventMask = lldb::SBProcess::eBroadcastBitStateChanged |
              lldb::SBProcess::eBroadcastBitInterrupt |
              lldb::SBProcess::eBroadcastBitSTDOUT |
              lldb::SBProcess::eBroadcastBitSTDERR |
              lldb::SBProcess::eBroadcastBitProfileData |
              lldb::SBProcess::eBroadcastBitStructuredData;
  bOk = bOk &&
        RegisterForEvent(
            strDbgId, CMIUtilString(lldb::SBProcess::GetBroadcasterClassName()),
            eventMask);

  eventMask = lldb::SBCommandInterpreter::eBroadcastBitQuitCommandReceived |
              lldb::SBCommandInterpreter::eBroadcastBitThreadShouldExit |
              lldb::SBCommandInterpreter::eBroadcastBitAsynchronousOutputData |
              lldb::SBCommandInterpreter::eBroadcastBitAsynchronousErrorData;
  bOk = bOk &&
        RegisterForEvent(
            strDbgId, m_lldbDebugger.GetCommandInterpreter().GetBroadcaster(),
            eventMask);

  return bOk;
}

//++
//------------------------------------------------------------------------------------
// Details: Register with the debugger, the SBListener, the type of events you
// are interested
//          in. Others, like commands, may have already set the mask.
// Type:    Method.
// Args:    vClientName         - (R) ID of the client who wants these events
// set.
//          vBroadcasterClass   - (R) The SBBroadcaster's class name.
//          vEventMask          - (R) The mask of events to listen for.
// Return:  MIstatus::success - Functionality succeeded.
//          MIstatus::failure - Functionality failed.
// Throws:  None.
//--
bool CMICmnLLDBDebugger::RegisterForEvent(
    const CMIUtilString &vClientName, const CMIUtilString &vBroadcasterClass,
    const MIuint vEventMask) {
  MIuint existingMask = 0;
  if (!BroadcasterGetMask(vBroadcasterClass, existingMask))
    return MIstatus::failure;

  if (!ClientSaveMask(vClientName, vBroadcasterClass, vEventMask))
    return MIstatus::failure;

  const char *pBroadCasterName = vBroadcasterClass.c_str();
  MIuint eventMask = vEventMask;
  eventMask += existingMask;
  const MIuint result = m_lldbListener.StartListeningForEventClass(
      m_lldbDebugger, pBroadCasterName, eventMask);
  if (result == 0) {
    SetErrorDescription(CMIUtilString::Format(
        MIRSRC(IDS_LLDBDEBUGGER_ERR_STARTLISTENER), pBroadCasterName));
    return MIstatus::failure;
  }

  return BroadcasterSaveMask(vBroadcasterClass, eventMask);
}

//++
//------------------------------------------------------------------------------------
// Details: Register with the debugger, the SBListener, the type of events you
// are interested
//          in. Others, like commands, may have already set the mask.
// Type:    Method.
// Args:    vClientName     - (R) ID of the client who wants these events set.
//          vBroadcaster    - (R) An SBBroadcaster's derived class.
//          vEventMask      - (R) The mask of events to listen for.
// Return:  MIstatus::success - Functionality succeeded.
//          MIstatus::failure - Functionality failed.
// Throws:  None.
//--
bool CMICmnLLDBDebugger::RegisterForEvent(
    const CMIUtilString &vClientName, const lldb::SBBroadcaster &vBroadcaster,
    const MIuint vEventMask) {
  const char *pBroadcasterName = vBroadcaster.GetName();
  if (pBroadcasterName == nullptr) {
    SetErrorDescription(
        CMIUtilString::Format(MIRSRC(IDS_LLDBDEBUGGER_ERR_BROADCASTER_NAME),
                              MIRSRC(IDS_WORD_INVALIDNULLPTR)));
    return MIstatus::failure;
  }
  CMIUtilString broadcasterName(pBroadcasterName);
  if (broadcasterName.length() == 0) {
    SetErrorDescription(
        CMIUtilString::Format(MIRSRC(IDS_LLDBDEBUGGER_ERR_BROADCASTER_NAME),
                              MIRSRC(IDS_WORD_INVALIDEMPTY)));
    return MIstatus::failure;
  }

  MIuint existingMask = 0;
  if (!BroadcasterGetMask(broadcasterName, existingMask))
    return MIstatus::failure;

  if (!ClientSaveMask(vClientName, broadcasterName, vEventMask))
    return MIstatus::failure;

  MIuint eventMask = vEventMask;
  eventMask += existingMask;
  const MIuint result =
      m_lldbListener.StartListeningForEvents(vBroadcaster, eventMask);
  if (result == 0) {
    SetErrorDescription(CMIUtilString::Format(
        MIRSRC(IDS_LLDBDEBUGGER_ERR_STARTLISTENER), pBroadcasterName));
    return MIstatus::failure;
  }

  return BroadcasterSaveMask(broadcasterName, eventMask);
}

//++
//------------------------------------------------------------------------------------
// Details: Unregister with the debugger, the SBListener, the type of events you
// are no
//          longer interested in. Others, like commands, may still remain
//          interested so
//          an event may not necessarily be stopped.
// Type:    Method.
// Args:    vClientName         - (R) ID of the client who no longer requires
// these events.
//          vBroadcasterClass   - (R) The SBBroadcaster's class name.
// Return:  MIstatus::success - Functionality succeeded.
//          MIstatus::failure - Functionality failed.
// Throws:  None.
//--
bool CMICmnLLDBDebugger::UnregisterForEvent(
    const CMIUtilString &vClientName, const CMIUtilString &vBroadcasterClass) {
  MIuint clientsEventMask = 0;
  if (!ClientGetTheirMask(vClientName, vBroadcasterClass, clientsEventMask))
    return MIstatus::failure;
  if (!ClientRemoveTheirMask(vClientName, vBroadcasterClass))
    return MIstatus::failure;

  const MIuint otherClientsEventMask =
      ClientGetMaskForAllClients(vBroadcasterClass);
  MIuint newEventMask = 0;
  for (MIuint i = 0; i < 32; i++) {
    const MIuint bit = 1 << i;
    const MIuint clientBit = bit & clientsEventMask;
    const MIuint othersBit = bit & otherClientsEventMask;
    if ((clientBit != 0) && (othersBit == 0)) {
      newEventMask += clientBit;
    }
  }

  const char *pBroadCasterName = vBroadcasterClass.c_str();
  if (!m_lldbListener.StopListeningForEventClass(
          m_lldbDebugger, pBroadCasterName, newEventMask)) {
    SetErrorDescription(
        CMIUtilString::Format(MIRSRC(IDS_LLDBDEBUGGER_ERR_STOPLISTENER),
                              vClientName.c_str(), pBroadCasterName));
    return MIstatus::failure;
  }

  return BroadcasterSaveMask(vBroadcasterClass, otherClientsEventMask);
}

//++
//------------------------------------------------------------------------------------
// Details: Given the SBBroadcaster class name retrieve it's current event mask.
// Type:    Method.
// Args:    vBroadcasterClass   - (R) The SBBroadcaster's class name.
//          vEventMask          - (W) The mask of events to listen for.
// Return:  MIstatus::success - Functionality succeeded.
//          MIstatus::failure - Functionality failed.
// Throws:  None.
//--
bool CMICmnLLDBDebugger::BroadcasterGetMask(
    const CMIUtilString &vBroadcasterClass, MIuint &vwEventMask) const {
  vwEventMask = 0;

  if (vBroadcasterClass.empty()) {
    SetErrorDescription(
        CMIUtilString::Format(MIRSRC(IDS_LLDBDEBUGGER_ERR_INVALIDBROADCASTER),
                              vBroadcasterClass.c_str()));
    return MIstatus::failure;
  }

  const MapBroadcastClassNameToEventMask_t::const_iterator it =
      m_mapBroadcastClassNameToEventMask.find(vBroadcasterClass);
  if (it != m_mapBroadcastClassNameToEventMask.end()) {
    vwEventMask = (*it).second;
  }

  return MIstatus::success;
}

//++
//------------------------------------------------------------------------------------
// Details: Remove the event mask for the specified SBBroadcaster class name.
// Type:    Method.
// Args:    vBroadcasterClass - (R) The SBBroadcaster's class name.
// Return:  MIstatus::success - Functionality succeeded.
//          MIstatus::failure - Functionality failed.
// Throws:  None.
//--
bool CMICmnLLDBDebugger::BroadcasterRemoveMask(
    const CMIUtilString &vBroadcasterClass) {
  MapBroadcastClassNameToEventMask_t::const_iterator it =
      m_mapBroadcastClassNameToEventMask.find(vBroadcasterClass);
  if (it != m_mapBroadcastClassNameToEventMask.end()) {
    m_mapBroadcastClassNameToEventMask.erase(it);
  }

  return MIstatus::success;
}

//++
//------------------------------------------------------------------------------------
// Details: Given the SBBroadcaster class name save it's current event mask.
// Type:    Method.
// Args:    vBroadcasterClass - (R) The SBBroadcaster's class name.
//          vEventMask        - (R) The mask of events to listen for.
// Return:  MIstatus::success - Functionality succeeded.
//          MIstatus::failure - Functionality failed.
// Throws:  None.
//--
bool CMICmnLLDBDebugger::BroadcasterSaveMask(
    const CMIUtilString &vBroadcasterClass, const MIuint vEventMask) {
  if (vBroadcasterClass.empty()) {
    SetErrorDescription(
        CMIUtilString::Format(MIRSRC(IDS_LLDBDEBUGGER_ERR_INVALIDBROADCASTER),
                              vBroadcasterClass.c_str()));
    return MIstatus::failure;
  }

  BroadcasterRemoveMask(vBroadcasterClass);
  MapPairBroadcastClassNameToEventMask_t pr(vBroadcasterClass, vEventMask);
  m_mapBroadcastClassNameToEventMask.insert(pr);

  return MIstatus::success;
}

//++
//------------------------------------------------------------------------------------
// Details: Iterate all the clients who have registered event masks against
// particular
//          SBBroadcasters and build up the mask that is for all of them.
// Type:    Method.
// Args:    vBroadcasterClass   - (R) The broadcaster to retrieve the mask for.
// Return:  MIuint - Event mask.
// Throws:  None.
//--
MIuint CMICmnLLDBDebugger::ClientGetMaskForAllClients(
    const CMIUtilString &vBroadcasterClass) const {
  MIuint mask = 0;
  MapIdToEventMask_t::const_iterator it = m_mapIdToEventMask.begin();
  while (it != m_mapIdToEventMask.end()) {
    const CMIUtilString &rId((*it).first);
    if (rId.find(vBroadcasterClass) != std::string::npos) {
      const MIuint clientsMask = (*it).second;
      mask |= clientsMask;
    }

    // Next
    ++it;
  }

  return mask;
}

//++
//------------------------------------------------------------------------------------
// Details: Given the client save its particular event requirements.
// Type:    Method.
// Args:    vClientName       - (R) The Client's unique ID.
//          vBroadcasterClass - (R) The SBBroadcaster's class name targeted for
//          the events.
//          vEventMask        - (R) The mask of events to listen for.
// Return:  MIstatus::success - Functionality succeeded.
//          MIstatus::failure - Functionality failed.
// Throws:  None.
//--
bool CMICmnLLDBDebugger::ClientSaveMask(const CMIUtilString &vClientName,
                                        const CMIUtilString &vBroadcasterClass,
                                        const MIuint vEventMask) {
  if (vClientName.empty()) {
    SetErrorDescription(CMIUtilString::Format(
        MIRSRC(IDS_LLDBDEBUGGER_ERR_INVALIDCLIENTNAME), vClientName.c_str()));
    return MIstatus::failure;
  }

  CMIUtilString strId(vBroadcasterClass);
  strId += vClientName;

  ClientRemoveTheirMask(vClientName, vBroadcasterClass);
  MapPairIdToEventMask_t pr(strId, vEventMask);
  m_mapIdToEventMask.insert(pr);

  return MIstatus::success;
}

//++
//------------------------------------------------------------------------------------
// Details: Given the client remove it's particular event requirements.
// Type:    Method.
// Args:    vClientName       - (R) The Client's unique ID.
//          vBroadcasterClass - (R) The SBBroadcaster's class name.
// Return:  MIstatus::success - Functionality succeeded.
//          MIstatus::failure - Functionality failed.
// Throws:  None.
//--
bool CMICmnLLDBDebugger::ClientRemoveTheirMask(
    const CMIUtilString &vClientName, const CMIUtilString &vBroadcasterClass) {
  if (vClientName.empty()) {
    SetErrorDescription(CMIUtilString::Format(
        MIRSRC(IDS_LLDBDEBUGGER_ERR_INVALIDCLIENTNAME), vClientName.c_str()));
    return MIstatus::failure;
  }

  CMIUtilString strId(vBroadcasterClass);
  strId += vClientName;

  const MapIdToEventMask_t::const_iterator it = m_mapIdToEventMask.find(strId);
  if (it != m_mapIdToEventMask.end()) {
    m_mapIdToEventMask.erase(it);
  }

  return MIstatus::success;
}

//++
//------------------------------------------------------------------------------------
// Details: Retrieve the client's event mask used for on a particular
// SBBroadcaster.
// Type:    Method.
// Args:    vClientName       - (R) The Client's unique ID.
//          vBroadcasterClass - (R) The SBBroadcaster's class name.
//          vwEventMask       - (W) The client's mask.
// Return:  MIstatus::success - Functionality succeeded.
//          MIstatus::failure - Functionality failed.
// Throws:  None.
//--
bool CMICmnLLDBDebugger::ClientGetTheirMask(
    const CMIUtilString &vClientName, const CMIUtilString &vBroadcasterClass,
    MIuint &vwEventMask) {
  vwEventMask = 0;

  if (vClientName.empty()) {
    SetErrorDescription(CMIUtilString::Format(
        MIRSRC(IDS_LLDBDEBUGGER_ERR_INVALIDCLIENTNAME), vClientName.c_str()));
    return MIstatus::failure;
  }

  const CMIUtilString strId(vBroadcasterClass + vClientName);
  const MapIdToEventMask_t::const_iterator it = m_mapIdToEventMask.find(strId);
  if (it != m_mapIdToEventMask.end()) {
    vwEventMask = (*it).second;
  }

  SetErrorDescription(CMIUtilString::Format(
      MIRSRC(IDS_LLDBDEBUGGER_ERR_CLIENTNOTREGISTERED), vClientName.c_str()));

  return MIstatus::failure;
}

//++
//------------------------------------------------------------------------------------
// Details: Momentarily wait for an events being broadcast and inspect those
// that do
//          come this way. Check if the target should exit event if so start
//          shutting
//          down this thread and the application. Any other events pass on to
//          the
//          Out-of-band handler to further determine what kind of event arrived.
//          This function runs in the thread "MI debugger event".
// Type:    Method.
// Args:    vrbIsAlive  - (W) False = yes exit event monitoring thread, true =
// continue.
// Return:  MIstatus::success - Functional succeeded.
//          MIstatus::failure - Functional failed.
// Throws:  None.
//--
bool CMICmnLLDBDebugger::MonitorSBListenerEvents(bool &vrbIsAlive) {
  vrbIsAlive = true;

  // Lock the mutex of event queue
  // Note that it should be locked while we are in
  // CMICmnLLDBDebugger::MonitorSBListenerEvents to
  // avoid a race condition with CMICmnLLDBDebugger::WaitForHandleEvent
  std::unique_lock<std::mutex> lock(m_mutexEventQueue);

  lldb::SBEvent event;
  const bool bGotEvent = m_lldbListener.GetNextEvent(event);
  if (!bGotEvent) {
    // Notify that we are finished and unlock the mutex of event queue before
    // sleeping
    m_conditionEventQueueEmpty.notify_one();
    lock.unlock();

    // Wait a bit to reduce CPU load
    const std::chrono::milliseconds time(1);
    std::this_thread::sleep_for(time);
    return MIstatus::success;
  }
  assert(event.IsValid());
  assert(event.GetBroadcaster().IsValid());

  // Debugging
  m_pLog->WriteLog(CMIUtilString::Format("##### An event occurred: %s",
                                         event.GetBroadcasterClass()));

  bool bHandledEvent = false;
  bool bOk = false;
  {
    // Lock Mutex before handling events so that we don't disturb a running cmd
    CMIUtilThreadLock lock(
        CMICmnLLDBDebugSessionInfo::Instance().GetSessionMutex());
    bOk = CMICmnLLDBDebuggerHandleEvents::Instance().HandleEvent(event,
                                                                 bHandledEvent);
  }

  if (!bHandledEvent) {
    const CMIUtilString msg(
        CMIUtilString::Format(MIRSRC(IDS_LLDBDEBUGGER_WRN_UNKNOWN_EVENT),
                              event.GetBroadcasterClass()));
    m_pLog->WriteLog(msg);
  }

  if (!bOk)
    m_pLog->WriteLog(
        CMICmnLLDBDebuggerHandleEvents::Instance().GetErrorDescription());

  return MIstatus::success;
}

//++
//------------------------------------------------------------------------------------
// Details: The main worker method for this thread.
// Type:    Method.
// Args:    vrbIsAlive  - (W) True = *this thread is working, false = thread has
// exited.
// Return:  MIstatus::success - Functional succeeded.
//          MIstatus::failure - Functional failed.
// Throws:  None.
//--
bool CMICmnLLDBDebugger::ThreadRun(bool &vrbIsAlive) {
  return MonitorSBListenerEvents(vrbIsAlive);
}

//++
//------------------------------------------------------------------------------------
// Details: Let this thread clean up after itself.
// Type:    Method.
// Args:
// Return:  MIstatus::success - Functionality succeeded.
//          MIstatus::failure - Functionality failed.
// Throws:  None.
//--
bool CMICmnLLDBDebugger::ThreadFinish() { return MIstatus::success; }

//++
//------------------------------------------------------------------------------------
// Details: Retrieve *this thread object's name.
// Type:    Overridden.
// Args:    None.
// Return:  CMIUtilString & - Text.
// Throws:  None.
//--
const CMIUtilString &CMICmnLLDBDebugger::ThreadGetName() const {
  return m_constStrThisThreadId;
}

//++
//------------------------------------------------------------------------------------
// Details: Loads lldb-mi formatters
// Type:    Method.
// Args:    None.
// Return:  true - Functionality succeeded.
//          false - Functionality failed.
// Throws:  None.
//--
bool CMICmnLLDBDebugger::LoadMIFormatters(lldb::SBTypeCategory miCategory) {
  if (!MI_add_summary(miCategory, "char", MI_char_summary_provider,
                      lldb::eTypeOptionHideValue |
                          lldb::eTypeOptionSkipPointers))
    return false;

  if (!MI_add_summary(miCategory, "unsigned char", MI_char_summary_provider,
                      lldb::eTypeOptionHideValue |
                          lldb::eTypeOptionSkipPointers))
    return false;

  if (!MI_add_summary(miCategory, "signed char", MI_char_summary_provider,
                      lldb::eTypeOptionHideValue |
                          lldb::eTypeOptionSkipPointers))
    return false;

  return true;
}

//++
//------------------------------------------------------------------------------------
// Details: Registers lldb-mi custom summary providers
// Type:    Method.
// Args:    None.
// Return:  true - Functionality succeeded.
//          false - Functionality failed.
// Throws:  None.
//--
bool CMICmnLLDBDebugger::RegisterMISummaryProviders() {
  static const char *miCategoryName = "lldb-mi";
  lldb::SBTypeCategory miCategory =
      m_lldbDebugger.CreateCategory(miCategoryName);
  if (!miCategory.IsValid())
    return false;

  if (!LoadMIFormatters(miCategory)) {
    m_lldbDebugger.DeleteCategory(miCategoryName);
    return false;
  }
  miCategory.SetEnabled(true);
  return true;
}
