//===-- MICmdCmdThread.cpp --------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

// Overview:    CMICmdCmdThreadInfo     implementation.

// Third Party Headers:
#include "lldb/API/SBBreakpointLocation.h"
#include "lldb/API/SBThread.h"

// In-house headers:
#include "MICmdArgValNumber.h"
#include "MICmdCmdThread.h"
#include "MICmnLLDBDebugSessionInfo.h"
#include "MICmnLLDBDebugger.h"
#include "MICmnMIResultRecord.h"
#include "MICmnMIValueConst.h"

//++
//------------------------------------------------------------------------------------
// Details: CMICmdCmdThreadInfo constructor.
// Type:    Method.
// Args:    None.
// Return:  None.
// Throws:  None.
//--
CMICmdCmdThreadInfo::CMICmdCmdThreadInfo()
    : m_bSingleThread(false), m_bThreadInvalid(true),
      m_constStrArgNamedThreadId("thread-id"), m_bHasCurrentThread(false) {
  // Command factory matches this name with that received from the stdin stream
  m_strMiCmd = "thread-info";

  // Required by the CMICmdFactory when registering *this command
  m_pSelfCreatorFn = &CMICmdCmdThreadInfo::CreateSelf;
}

//++
//------------------------------------------------------------------------------------
// Details: CMICmdCmdThreadInfo destructor.
// Type:    Overrideable.
// Args:    None.
// Return:  None.
// Throws:  None.
//--
CMICmdCmdThreadInfo::~CMICmdCmdThreadInfo() { m_vecMIValueTuple.clear(); }

//++
//------------------------------------------------------------------------------------
// Details: The invoker requires this function. The parses the command line
// options
//          arguments to extract values for each of those arguments.
// Type:    Overridden.
// Args:    None.
// Return:  MIstatus::success - Functional succeeded.
//          MIstatus::failure - Functional failed.
// Throws:  None.
//--
bool CMICmdCmdThreadInfo::ParseArgs() {
  m_setCmdArgs.Add(
      new CMICmdArgValNumber(m_constStrArgNamedThreadId, false, true));
  return ParseValidateCmdOptions();
}

//++
//------------------------------------------------------------------------------------
// Details: The invoker requires this function. The command does work in this
// function.
//          The command is likely to communicate with the LLDB SBDebugger in
//          here.
// Type:    Overridden.
// Args:    None.
// Return:  MIstatus::success - Functional succeeded.
//          MIstatus::failure - Functional failed.
// Throws:  None.
//--
bool CMICmdCmdThreadInfo::Execute() {
  CMICMDBASE_GETOPTION(pArgThreadId, Number, m_constStrArgNamedThreadId);
  MIuint nThreadId = 0;
  if (pArgThreadId->GetFound() && pArgThreadId->GetValid()) {
    m_bSingleThread = true;
    nThreadId = static_cast<MIuint>(pArgThreadId->GetValue());
  }

  CMICmnLLDBDebugSessionInfo &rSessionInfo(
      CMICmnLLDBDebugSessionInfo::Instance());
  lldb::SBProcess sbProcess = rSessionInfo.GetProcess();
  lldb::SBThread thread = sbProcess.GetSelectedThread();

  if (m_bSingleThread) {
    thread = sbProcess.GetThreadByIndexID(nThreadId);
    m_bThreadInvalid = !thread.IsValid();
    if (m_bThreadInvalid)
      return MIstatus::success;

    CMICmnMIValueTuple miTuple;
    if (!rSessionInfo.MIResponseFormThreadInfo(
            m_cmdData, thread,
            CMICmnLLDBDebugSessionInfo::eThreadInfoFormat_AllFrames, miTuple))
      return MIstatus::failure;

    m_miValueTupleThread = miTuple;

    return MIstatus::success;
  }

  // Multiple threads
  m_vecMIValueTuple.clear();
  const MIuint nThreads = sbProcess.GetNumThreads();
  for (MIuint i = 0; i < nThreads; i++) {
    lldb::SBThread thread = sbProcess.GetThreadAtIndex(i);
    if (thread.IsValid()) {
      CMICmnMIValueTuple miTuple;
      if (!rSessionInfo.MIResponseFormThreadInfo(
              m_cmdData, thread,
              CMICmnLLDBDebugSessionInfo::eThreadInfoFormat_AllFrames, miTuple))
        return MIstatus::failure;

      m_vecMIValueTuple.push_back(miTuple);
    }
  }

  // -thread-info with multiple threads ends with the current thread id if any
  if (thread.IsValid()) {
    const CMIUtilString strId(CMIUtilString::Format("%d", thread.GetIndexID()));
    CMICmnMIValueConst miValueCurrThreadId(strId);
    m_miValueCurrThreadId = miValueCurrThreadId;
    m_bHasCurrentThread = true;
  }

  return MIstatus::success;
}

//++
//------------------------------------------------------------------------------------
// Details: The invoker requires this function. The command prepares a MI Record
// Result
//          for the work carried out in the Execute().
// Type:    Overridden.
// Args:    None.
// Return:  MIstatus::success - Functional succeeded.
//          MIstatus::failure - Functional failed.
// Throws:  None.
//--
bool CMICmdCmdThreadInfo::Acknowledge() {
  if (m_bSingleThread) {
    if (m_bThreadInvalid) {
      const CMICmnMIValueConst miValueConst("invalid thread id");
      const CMICmnMIValueResult miValueResult("msg", miValueConst);
      const CMICmnMIResultRecord miRecordResult(
          m_cmdData.strMiCmdToken, CMICmnMIResultRecord::eResultClass_Error,
          miValueResult);
      m_miResultRecord = miRecordResult;
      return MIstatus::success;
    }

    // MI print
    // "%s^done,threads=[{id=\"%d\",target-id=\"%s\",frame={},state=\"%s\"}]
    const CMICmnMIValueList miValueList(m_miValueTupleThread);
    const CMICmnMIValueResult miValueResult("threads", miValueList);
    const CMICmnMIResultRecord miRecordResult(
        m_cmdData.strMiCmdToken, CMICmnMIResultRecord::eResultClass_Done,
        miValueResult);
    m_miResultRecord = miRecordResult;
    return MIstatus::success;
  }

  // Build up a list of thread information from tuples
  VecMIValueTuple_t::const_iterator it = m_vecMIValueTuple.begin();
  if (it == m_vecMIValueTuple.end()) {
    const CMICmnMIValueConst miValueConst("[]");
    const CMICmnMIValueResult miValueResult("threads", miValueConst);
    const CMICmnMIResultRecord miRecordResult(
        m_cmdData.strMiCmdToken, CMICmnMIResultRecord::eResultClass_Done,
        miValueResult);
    m_miResultRecord = miRecordResult;
    return MIstatus::success;
  }
  CMICmnMIValueList miValueList(*it);
  ++it;
  while (it != m_vecMIValueTuple.end()) {
    const CMICmnMIValueTuple &rTuple(*it);
    miValueList.Add(rTuple);

    // Next
    ++it;
  }

  CMICmnMIValueResult miValueResult("threads", miValueList);
  if (m_bHasCurrentThread) {
    CMIUtilString strCurrThreadId = "current-thread-id";
    miValueResult.Add(strCurrThreadId, m_miValueCurrThreadId);
  }
  const CMICmnMIResultRecord miRecordResult(
      m_cmdData.strMiCmdToken, CMICmnMIResultRecord::eResultClass_Done,
      miValueResult);
  m_miResultRecord = miRecordResult;

  return MIstatus::success;
}

//++
//------------------------------------------------------------------------------------
// Details: Required by the CMICmdFactory when registering *this command. The
// factory
//          calls this function to create an instance of *this command.
// Type:    Static method.
// Args:    None.
// Return:  CMICmdBase * - Pointer to a new command.
// Throws:  None.
//--
CMICmdBase *CMICmdCmdThreadInfo::CreateSelf() {
  return new CMICmdCmdThreadInfo();
}
