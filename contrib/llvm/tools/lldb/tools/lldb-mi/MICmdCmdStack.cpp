//===-- MICmdCmdStack.cpp ---------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

// Overview:    CMICmdCmdStackInfoDepth         implementation.
//              CMICmdCmdStackInfoFrame         implementation.
//              CMICmdCmdStackListFrames        implementation.
//              CMICmdCmdStackListArguments     implementation.
//              CMICmdCmdStackListLocals        implementation.
//              CMICmdCmdStackSelectFrame       implementation.

// Third Party Headers:
#include "lldb/API/SBThread.h"

// In-house headers:
#include "MICmdArgValListOfN.h"
#include "MICmdArgValNumber.h"
#include "MICmdArgValOptionLong.h"
#include "MICmdArgValOptionShort.h"
#include "MICmdArgValPrintValues.h"
#include "MICmdArgValString.h"
#include "MICmdArgValThreadGrp.h"
#include "MICmdCmdStack.h"
#include "MICmnLLDBDebugSessionInfo.h"
#include "MICmnLLDBDebugger.h"
#include "MICmnMIOutOfBandRecord.h"
#include "MICmnMIResultRecord.h"
#include "MICmnMIValueConst.h"

#include <algorithm>

//++
//------------------------------------------------------------------------------------
// Details: CMICmdCmdStackInfoDepth constructor.
// Type:    Method.
// Args:    None.
// Return:  None.
// Throws:  None.
//--
CMICmdCmdStackInfoDepth::CMICmdCmdStackInfoDepth()
    : m_nThreadFrames(0), m_constStrArgMaxDepth("max-depth") {
  // Command factory matches this name with that received from the stdin stream
  m_strMiCmd = "stack-info-depth";

  // Required by the CMICmdFactory when registering *this command
  m_pSelfCreatorFn = &CMICmdCmdStackInfoDepth::CreateSelf;
}

//++
//------------------------------------------------------------------------------------
// Details: CMICmdCmdStackInfoDepth destructor.
// Type:    Overrideable.
// Args:    None.
// Return:  None.
// Throws:  None.
//--
CMICmdCmdStackInfoDepth::~CMICmdCmdStackInfoDepth() {}

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
bool CMICmdCmdStackInfoDepth::ParseArgs() {
  m_setCmdArgs.Add(new CMICmdArgValNumber(m_constStrArgMaxDepth, false, false));
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
bool CMICmdCmdStackInfoDepth::Execute() {
  CMICMDBASE_GETOPTION(pArgThread, OptionLong, m_constStrArgThread);
  CMICMDBASE_GETOPTION(pArgMaxDepth, Number, m_constStrArgMaxDepth);

  // Retrieve the --thread option's thread ID (only 1)
  MIuint64 nThreadId = UINT64_MAX;
  if (pArgThread->GetFound() &&
      !pArgThread->GetExpectedOption<CMICmdArgValNumber, MIuint64>(nThreadId)) {
    SetError(CMIUtilString::Format(MIRSRC(IDS_CMD_ERR_OPTION_NOT_FOUND),
                                   m_cmdData.strMiCmd.c_str(),
                                   m_constStrArgThread.c_str()));
    return MIstatus::failure;
  }

  CMICmnLLDBDebugSessionInfo &rSessionInfo(
      CMICmnLLDBDebugSessionInfo::Instance());
  lldb::SBProcess sbProcess = rSessionInfo.GetProcess();
  lldb::SBThread thread = (nThreadId != UINT64_MAX)
                              ? sbProcess.GetThreadByIndexID(nThreadId)
                              : sbProcess.GetSelectedThread();
  m_nThreadFrames = thread.GetNumFrames();

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
bool CMICmdCmdStackInfoDepth::Acknowledge() {
  const CMIUtilString strDepth(CMIUtilString::Format("%d", m_nThreadFrames));
  const CMICmnMIValueConst miValueConst(strDepth);
  const CMICmnMIValueResult miValueResult("depth", miValueConst);
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
CMICmdBase *CMICmdCmdStackInfoDepth::CreateSelf() {
  return new CMICmdCmdStackInfoDepth();
}

//---------------------------------------------------------------------------------------
//---------------------------------------------------------------------------------------
//---------------------------------------------------------------------------------------

//++
//------------------------------------------------------------------------------------
// Details: CMICmdCmdStackInfoFrame constructor.
// Type:    Method.
// Args:    None.
// Return:  None.
// Throws:  None.
//--
CMICmdCmdStackInfoFrame::CMICmdCmdStackInfoFrame() {
  // Command factory matches this name with that received from the stdin stream
  m_strMiCmd = "stack-info-frame";

  // Required by the CMICmdFactory when registering *this command
  m_pSelfCreatorFn = &CMICmdCmdStackInfoFrame::CreateSelf;
}

//++
//------------------------------------------------------------------------------------
// Details: CMICmdCmdStackInfoFrame destructor.
// Type:    Overrideable.
// Args:    None.
// Return:  None.
// Throws:  None.
//--
CMICmdCmdStackInfoFrame::~CMICmdCmdStackInfoFrame() {}

//++
//------------------------------------------------------------------------------------
// Details: The invoker requires this function. The parses the command line
// options
//          arguments to extract values for each of those arguments.
// Type:    Overridden.
// Args:    None.
// Return:  MIstatus::success - Function succeeded.
//          MIstatus::failure - Function failed.
// Throws:  None.
//--
bool CMICmdCmdStackInfoFrame::ParseArgs() { return ParseValidateCmdOptions(); }

//++
//------------------------------------------------------------------------------------
// Details: The invoker requires this function. The command does work in this
// function.
//          The command is likely to communicate with the LLDB SBDebugger in
//          here.
// Type:    Overridden.
// Args:    None.
// Return:  MIstatus::success - Function succeeded.
//          MIstatus::failure - Function failed.
// Throws:  None.
//--
bool CMICmdCmdStackInfoFrame::Execute() {
  CMICmnLLDBDebugSessionInfo &rSessionInfo(
      CMICmnLLDBDebugSessionInfo::Instance());
  lldb::SBProcess sbProcess = rSessionInfo.GetProcess();
  if (!sbProcess.IsValid()) {
    SetError(CMIUtilString::Format(MIRSRC(IDS_CMD_ERR_INVALID_PROCESS),
                                   m_cmdData.strMiCmd.c_str()));
    return MIstatus::failure;
  }

  lldb::SBThread sbThread = sbProcess.GetSelectedThread();
  MIuint nFrameId = sbThread.GetSelectedFrame().GetFrameID();
  if (!rSessionInfo.MIResponseFormFrameInfo(
          sbThread, nFrameId,
          CMICmnLLDBDebugSessionInfo::eFrameInfoFormat_NoArguments,
          m_miValueTuple))
    return MIstatus::failure;

  return MIstatus::success;
}

//++
//------------------------------------------------------------------------------------
// Details: The invoker requires this function. The command prepares a MI Record
// Result
//          for the work carried out in the Execute().
// Type:    Overridden.
// Args:    None.
// Return:  MIstatus::success - Function succeeded.
//          MIstatus::failure - Function failed.
// Throws:  None.
//--
bool CMICmdCmdStackInfoFrame::Acknowledge() {
  const CMICmnMIValueResult miValueResult("frame", m_miValueTuple);
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
CMICmdBase *CMICmdCmdStackInfoFrame::CreateSelf() {
  return new CMICmdCmdStackInfoFrame();
}

//---------------------------------------------------------------------------------------
//---------------------------------------------------------------------------------------
//---------------------------------------------------------------------------------------

//++
//------------------------------------------------------------------------------------
// Details: CMICmdCmdStackListFrames constructor.
// Type:    Method.
// Args:    None.
// Return:  None.
// Throws:  None.
//--
CMICmdCmdStackListFrames::CMICmdCmdStackListFrames()
    : m_nThreadFrames(0), m_constStrArgFrameLow("low-frame"),
      m_constStrArgFrameHigh("high-frame") {
  // Command factory matches this name with that received from the stdin stream
  m_strMiCmd = "stack-list-frames";

  // Required by the CMICmdFactory when registering *this command
  m_pSelfCreatorFn = &CMICmdCmdStackListFrames::CreateSelf;
}

//++
//------------------------------------------------------------------------------------
// Details: CMICmdCmdStackListFrames destructor.
// Type:    Overrideable.
// Args:    None.
// Return:  None.
// Throws:  None.
//--
CMICmdCmdStackListFrames::~CMICmdCmdStackListFrames() {
  m_vecMIValueResult.clear();
}

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
bool CMICmdCmdStackListFrames::ParseArgs() {
  m_setCmdArgs.Add(new CMICmdArgValNumber(m_constStrArgFrameLow, false, true));
  m_setCmdArgs.Add(new CMICmdArgValNumber(m_constStrArgFrameHigh, false, true));
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
bool CMICmdCmdStackListFrames::Execute() {
  CMICMDBASE_GETOPTION(pArgThread, OptionLong, m_constStrArgThread);
  CMICMDBASE_GETOPTION(pArgFrameLow, Number, m_constStrArgFrameLow);
  CMICMDBASE_GETOPTION(pArgFrameHigh, Number, m_constStrArgFrameHigh);

  // Retrieve the --thread option's thread ID (only 1)
  MIuint64 nThreadId = UINT64_MAX;
  if (pArgThread->GetFound() &&
      !pArgThread->GetExpectedOption<CMICmdArgValNumber, MIuint64>(nThreadId)) {
    SetError(CMIUtilString::Format(MIRSRC(IDS_CMD_ERR_OPTION_NOT_FOUND),
                                   m_cmdData.strMiCmd.c_str(),
                                   m_constStrArgThread.c_str()));
    return MIstatus::failure;
  }

  // Frame low and high options are not mandatory
  MIuint nFrameHigh =
      pArgFrameHigh->GetFound() ? pArgFrameHigh->GetValue() : UINT32_MAX;
  const MIuint nFrameLow =
      pArgFrameLow->GetFound() ? pArgFrameLow->GetValue() : 0;

  CMICmnLLDBDebugSessionInfo &rSessionInfo(
      CMICmnLLDBDebugSessionInfo::Instance());
  lldb::SBProcess sbProcess = rSessionInfo.GetProcess();
  lldb::SBThread thread = (nThreadId != UINT64_MAX)
                              ? sbProcess.GetThreadByIndexID(nThreadId)
                              : sbProcess.GetSelectedThread();
  MIuint nThreadFrames = thread.GetNumFrames();

  // Adjust nThreadFrames for the nFrameHigh argument as we use nFrameHigh+1 in
  // the min calc as the arg
  // is not an index, but a frame id value.
  if (nFrameHigh < UINT32_MAX) {
    nFrameHigh++;
    nThreadFrames = (nFrameHigh < nThreadFrames) ? nFrameHigh : nThreadFrames;
  }

  m_nThreadFrames = nThreadFrames;
  if (nThreadFrames == 0)
    return MIstatus::success;

  m_vecMIValueResult.clear();
  for (MIuint nLevel = nFrameLow; nLevel < nThreadFrames; nLevel++) {
    CMICmnMIValueTuple miValueTuple;
    if (!rSessionInfo.MIResponseFormFrameInfo(
            thread, nLevel,
            CMICmnLLDBDebugSessionInfo::eFrameInfoFormat_NoArguments,
            miValueTuple))
      return MIstatus::failure;

    const CMICmnMIValueResult miValueResult8("frame", miValueTuple);
    m_vecMIValueResult.push_back(miValueResult8);
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
bool CMICmdCmdStackListFrames::Acknowledge() {
  if (m_nThreadFrames == 0) {
    // MI print "3^done,stack=[{}]"
    const CMICmnMIValueTuple miValueTuple;
    const CMICmnMIValueList miValueList(miValueTuple);
    const CMICmnMIValueResult miValueResult("stack", miValueList);
    const CMICmnMIResultRecord miRecordResult(
        m_cmdData.strMiCmdToken, CMICmnMIResultRecord::eResultClass_Done,
        miValueResult);
    m_miResultRecord = miRecordResult;

    return MIstatus::success;
  }

  // Build up a list of thread information from tuples
  VecMIValueResult_t::const_iterator it = m_vecMIValueResult.begin();
  if (it == m_vecMIValueResult.end()) {
    // MI print "3^done,stack=[{}]"
    const CMICmnMIValueTuple miValueTuple;
    const CMICmnMIValueList miValueList(miValueTuple);
    const CMICmnMIValueResult miValueResult("stack", miValueList);
    const CMICmnMIResultRecord miRecordResult(
        m_cmdData.strMiCmdToken, CMICmnMIResultRecord::eResultClass_Done,
        miValueResult);
    m_miResultRecord = miRecordResult;
    return MIstatus::success;
  }
  CMICmnMIValueList miValueList(*it);
  ++it;
  while (it != m_vecMIValueResult.end()) {
    const CMICmnMIValueResult &rTuple(*it);
    miValueList.Add(rTuple);

    // Next
    ++it;
  }
  const CMICmnMIValueResult miValueResult("stack", miValueList);
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
CMICmdBase *CMICmdCmdStackListFrames::CreateSelf() {
  return new CMICmdCmdStackListFrames();
}

//---------------------------------------------------------------------------------------
//---------------------------------------------------------------------------------------
//---------------------------------------------------------------------------------------

//++
//------------------------------------------------------------------------------------
// Details: CMICmdCmdStackListArguments constructor.
// Type:    Method.
// Args:    None.
// Return:  None.
// Throws:  None.
//--
CMICmdCmdStackListArguments::CMICmdCmdStackListArguments()
    : m_bThreadInvalid(false), m_miValueList(true),
      m_constStrArgPrintValues("print-values"),
      m_constStrArgFrameLow("low-frame"), m_constStrArgFrameHigh("high-frame") {
  // Command factory matches this name with that received from the stdin stream
  m_strMiCmd = "stack-list-arguments";

  // Required by the CMICmdFactory when registering *this command
  m_pSelfCreatorFn = &CMICmdCmdStackListArguments::CreateSelf;
}

//++
//------------------------------------------------------------------------------------
// Details: CMICmdCmdStackListArguments destructor.
// Type:    Overrideable.
// Args:    None.
// Return:  None.
// Throws:  None.
//--
CMICmdCmdStackListArguments::~CMICmdCmdStackListArguments() {}

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
bool CMICmdCmdStackListArguments::ParseArgs() {
  m_setCmdArgs.Add(
      new CMICmdArgValPrintValues(m_constStrArgPrintValues, true, true));
  m_setCmdArgs.Add(new CMICmdArgValNumber(m_constStrArgFrameLow, false, true));
  m_setCmdArgs.Add(new CMICmdArgValNumber(m_constStrArgFrameHigh, false, true));
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
bool CMICmdCmdStackListArguments::Execute() {
  CMICMDBASE_GETOPTION(pArgThread, OptionLong, m_constStrArgThread);
  CMICMDBASE_GETOPTION(pArgPrintValues, PrintValues, m_constStrArgPrintValues);
  CMICMDBASE_GETOPTION(pArgFrameLow, Number, m_constStrArgFrameLow);
  CMICMDBASE_GETOPTION(pArgFrameHigh, Number, m_constStrArgFrameHigh);

  // Retrieve the --thread option's thread ID (only 1)
  MIuint64 nThreadId = UINT64_MAX;
  if (pArgThread->GetFound()) {
    if (!pArgThread->GetExpectedOption<CMICmdArgValNumber, MIuint64>(
            nThreadId)) {
      SetError(CMIUtilString::Format(MIRSRC(IDS_CMD_ERR_OPTION_NOT_FOUND),
                                     m_cmdData.strMiCmd.c_str(),
                                     m_constStrArgThread.c_str()));
      return MIstatus::failure;
    }
  }

  const CMICmnLLDBDebugSessionInfo::VariableInfoFormat_e eVarInfoFormat =
      static_cast<CMICmnLLDBDebugSessionInfo::VariableInfoFormat_e>(
          pArgPrintValues->GetValue());

  MIuint nFrameLow = 0;
  MIuint nFrameHigh = UINT32_MAX;
  if (pArgFrameLow->GetFound() && pArgFrameHigh->GetFound()) {
    nFrameLow = pArgFrameLow->GetValue();
    nFrameHigh = pArgFrameHigh->GetValue() + 1;
  } else if (pArgFrameLow->GetFound() || pArgFrameHigh->GetFound()) {
    // Only low-frame or high-frame was specified but both are required
    SetError(
        CMIUtilString::Format(MIRSRC(IDS_CMD_ERR_THREAD_FRAME_RANGE_INVALID),
                              m_cmdData.strMiCmd.c_str()));
    return MIstatus::failure;
  }

  CMICmnLLDBDebugSessionInfo &rSessionInfo(
      CMICmnLLDBDebugSessionInfo::Instance());
  lldb::SBProcess sbProcess = rSessionInfo.GetProcess();
  lldb::SBThread thread = (nThreadId != UINT64_MAX)
                              ? sbProcess.GetThreadByIndexID(nThreadId)
                              : sbProcess.GetSelectedThread();
  m_bThreadInvalid = !thread.IsValid();
  if (m_bThreadInvalid)
    return MIstatus::success;

  const lldb::StopReason eStopReason = thread.GetStopReason();
  if ((eStopReason == lldb::eStopReasonNone) ||
      (eStopReason == lldb::eStopReasonInvalid)) {
    m_bThreadInvalid = true;
    return MIstatus::success;
  }

  const MIuint nFrames = thread.GetNumFrames();
  if (nFrameLow >= nFrames) {
    // The low-frame is larger than the actual number of frames
    SetError(
        CMIUtilString::Format(MIRSRC(IDS_CMD_ERR_THREAD_FRAME_RANGE_INVALID),
                              m_cmdData.strMiCmd.c_str()));
    return MIstatus::failure;
  }

  nFrameHigh = std::min(nFrameHigh, nFrames);
  for (MIuint i = nFrameLow; i < nFrameHigh; i++) {
    lldb::SBFrame frame = thread.GetFrameAtIndex(i);
    CMICmnMIValueList miValueList(true);
    const MIuint maskVarTypes =
        CMICmnLLDBDebugSessionInfo::eVariableType_Arguments;
    if (!rSessionInfo.MIResponseFormVariableInfo(frame, maskVarTypes,
                                                 eVarInfoFormat, miValueList))
      return MIstatus::failure;
    const CMICmnMIValueConst miValueConst(CMIUtilString::Format("%d", i));
    const CMICmnMIValueResult miValueResult("level", miValueConst);
    CMICmnMIValueTuple miValueTuple(miValueResult);
    const CMICmnMIValueResult miValueResult2("args", miValueList);
    miValueTuple.Add(miValueResult2);
    const CMICmnMIValueResult miValueResult3("frame", miValueTuple);
    m_miValueList.Add(miValueResult3);
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
bool CMICmdCmdStackListArguments::Acknowledge() {
  if (m_bThreadInvalid) {
    // MI print "%s^done,stack-args=[]"
    const CMICmnMIValueList miValueList(true);
    const CMICmnMIValueResult miValueResult("stack-args", miValueList);
    const CMICmnMIResultRecord miRecordResult(
        m_cmdData.strMiCmdToken, CMICmnMIResultRecord::eResultClass_Done,
        miValueResult);
    m_miResultRecord = miRecordResult;
    return MIstatus::success;
  }

  // MI print
  // "%s^done,stack-args=[frame={level=\"0\",args=[%s]},frame={level=\"1\",args=[%s]}]"
  const CMICmnMIValueResult miValueResult4("stack-args", m_miValueList);
  const CMICmnMIResultRecord miRecordResult(
      m_cmdData.strMiCmdToken, CMICmnMIResultRecord::eResultClass_Done,
      miValueResult4);
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
CMICmdBase *CMICmdCmdStackListArguments::CreateSelf() {
  return new CMICmdCmdStackListArguments();
}

//---------------------------------------------------------------------------------------
//---------------------------------------------------------------------------------------
//---------------------------------------------------------------------------------------

//++
//------------------------------------------------------------------------------------
// Details: CMICmdCmdStackListLocals constructor.
// Type:    Method.
// Args:    None.
// Return:  None.
// Throws:  None.
//--
CMICmdCmdStackListLocals::CMICmdCmdStackListLocals()
    : m_bThreadInvalid(false), m_miValueList(true),
      m_constStrArgPrintValues("print-values") {
  // Command factory matches this name with that received from the stdin stream
  m_strMiCmd = "stack-list-locals";

  // Required by the CMICmdFactory when registering *this command
  m_pSelfCreatorFn = &CMICmdCmdStackListLocals::CreateSelf;
}

//++
//------------------------------------------------------------------------------------
// Details: CMICmdCmdStackListLocals destructor.
// Type:    Overrideable.
// Args:    None.
// Return:  None.
// Throws:  None.
//--
CMICmdCmdStackListLocals::~CMICmdCmdStackListLocals() {}

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
bool CMICmdCmdStackListLocals::ParseArgs() {
  m_setCmdArgs.Add(
      new CMICmdArgValPrintValues(m_constStrArgPrintValues, true, true));
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
bool CMICmdCmdStackListLocals::Execute() {
  CMICMDBASE_GETOPTION(pArgThread, OptionLong, m_constStrArgThread);
  CMICMDBASE_GETOPTION(pArgFrame, OptionLong, m_constStrArgFrame);
  CMICMDBASE_GETOPTION(pArgPrintValues, PrintValues, m_constStrArgPrintValues);

  // Retrieve the --thread option's thread ID (only 1)
  MIuint64 nThreadId = UINT64_MAX;
  if (pArgThread->GetFound()) {
    if (!pArgThread->GetExpectedOption<CMICmdArgValNumber, MIuint64>(
            nThreadId)) {
      SetError(CMIUtilString::Format(MIRSRC(IDS_CMD_ERR_OPTION_NOT_FOUND),
                                     m_cmdData.strMiCmd.c_str(),
                                     m_constStrArgThread.c_str()));
      return MIstatus::failure;
    }
  }

  MIuint64 nFrame = UINT64_MAX;
  if (pArgFrame->GetFound()) {
    if (!pArgFrame->GetExpectedOption<CMICmdArgValNumber, MIuint64>(nFrame)) {
      SetError(CMIUtilString::Format(MIRSRC(IDS_CMD_ERR_OPTION_NOT_FOUND),
                                     m_cmdData.strMiCmd.c_str(),
                                     m_constStrArgFrame.c_str()));
      return MIstatus::failure;
    }
  }

  const CMICmnLLDBDebugSessionInfo::VariableInfoFormat_e eVarInfoFormat =
      static_cast<CMICmnLLDBDebugSessionInfo::VariableInfoFormat_e>(
          pArgPrintValues->GetValue());

  CMICmnLLDBDebugSessionInfo &rSessionInfo(
      CMICmnLLDBDebugSessionInfo::Instance());
  lldb::SBProcess sbProcess = rSessionInfo.GetProcess();
  lldb::SBThread thread = (nThreadId != UINT64_MAX)
                              ? sbProcess.GetThreadByIndexID(nThreadId)
                              : sbProcess.GetSelectedThread();
  m_bThreadInvalid = !thread.IsValid();
  if (m_bThreadInvalid)
    return MIstatus::success;

  const lldb::StopReason eStopReason = thread.GetStopReason();
  if ((eStopReason == lldb::eStopReasonNone) ||
      (eStopReason == lldb::eStopReasonInvalid)) {
    m_bThreadInvalid = true;
    return MIstatus::success;
  }

  lldb::SBFrame frame = (nFrame != UINT64_MAX) ? thread.GetFrameAtIndex(nFrame)
                                               : thread.GetSelectedFrame();

  CMICmnMIValueList miValueList(true);
  const MIuint maskVarTypes = CMICmnLLDBDebugSessionInfo::eVariableType_Locals |
                              CMICmnLLDBDebugSessionInfo::eVariableType_InScope;
  if (!rSessionInfo.MIResponseFormVariableInfo(frame, maskVarTypes,
                                               eVarInfoFormat, miValueList))
    return MIstatus::failure;

  m_miValueList = miValueList;

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
bool CMICmdCmdStackListLocals::Acknowledge() {
  if (m_bThreadInvalid) {
    // MI print "%s^done,locals=[]"
    const CMICmnMIValueList miValueList(true);
    const CMICmnMIValueResult miValueResult("locals", miValueList);
    const CMICmnMIResultRecord miRecordResult(
        m_cmdData.strMiCmdToken, CMICmnMIResultRecord::eResultClass_Done,
        miValueResult);
    m_miResultRecord = miRecordResult;
    return MIstatus::success;
  }

  // MI print "%s^done,locals=[%s]"
  const CMICmnMIValueResult miValueResult("locals", m_miValueList);
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
CMICmdBase *CMICmdCmdStackListLocals::CreateSelf() {
  return new CMICmdCmdStackListLocals();
}

//---------------------------------------------------------------------------------------
//---------------------------------------------------------------------------------------
//---------------------------------------------------------------------------------------

//++
//------------------------------------------------------------------------------------
// Details: CMICmdCmdStackListVariables constructor.
// Type:    Method.
// Args:    None.
// Return:  None.
// Throws:  None.
//--
CMICmdCmdStackListVariables::CMICmdCmdStackListVariables()
    : m_bThreadInvalid(false), m_miValueList(true),
      m_constStrArgPrintValues("print-values") {
  // Command factory matches this name with that received from the stdin stream
  m_strMiCmd = "stack-list-variables";

  // Required by the CMICmdFactory when registering *this command
  m_pSelfCreatorFn = &CMICmdCmdStackListVariables::CreateSelf;
}

//++
//------------------------------------------------------------------------------------
// Details: CMICmdCmdStackListVariables destructor.
// Type:    Overrideable.
// Args:    None.
// Return:  None.
// Throws:  None.
//--
CMICmdCmdStackListVariables::~CMICmdCmdStackListVariables() {}

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
bool CMICmdCmdStackListVariables::ParseArgs() {
  m_setCmdArgs.Add(
      new CMICmdArgValPrintValues(m_constStrArgPrintValues, true, true));
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
bool CMICmdCmdStackListVariables::Execute() {
  CMICMDBASE_GETOPTION(pArgThread, OptionLong, m_constStrArgThread);
  CMICMDBASE_GETOPTION(pArgFrame, OptionLong, m_constStrArgFrame);
  CMICMDBASE_GETOPTION(pArgPrintValues, PrintValues, m_constStrArgPrintValues);

  // Retrieve the --thread option's thread ID (only 1)
  MIuint64 nThreadId = UINT64_MAX;
  if (pArgThread->GetFound()) {
    if (!pArgThread->GetExpectedOption<CMICmdArgValNumber, MIuint64>(
            nThreadId)) {
      SetError(CMIUtilString::Format(MIRSRC(IDS_CMD_ERR_OPTION_NOT_FOUND),
                                     m_cmdData.strMiCmd.c_str(),
                                     m_constStrArgThread.c_str()));
      return MIstatus::failure;
    }
  }

  MIuint64 nFrame = UINT64_MAX;
  if (pArgFrame->GetFound()) {
    if (!pArgFrame->GetExpectedOption<CMICmdArgValNumber, MIuint64>(nFrame)) {
      SetError(CMIUtilString::Format(MIRSRC(IDS_CMD_ERR_OPTION_NOT_FOUND),
                                     m_cmdData.strMiCmd.c_str(),
                                     m_constStrArgFrame.c_str()));
      return MIstatus::failure;
    }
  }

  const CMICmnLLDBDebugSessionInfo::VariableInfoFormat_e eVarInfoFormat =
      static_cast<CMICmnLLDBDebugSessionInfo::VariableInfoFormat_e>(
          pArgPrintValues->GetValue());

  CMICmnLLDBDebugSessionInfo &rSessionInfo(
      CMICmnLLDBDebugSessionInfo::Instance());
  lldb::SBProcess sbProcess = rSessionInfo.GetProcess();
  lldb::SBThread thread = (nThreadId != UINT64_MAX)
                              ? sbProcess.GetThreadByIndexID(nThreadId)
                              : sbProcess.GetSelectedThread();
  m_bThreadInvalid = !thread.IsValid();
  if (m_bThreadInvalid)
    return MIstatus::success;

  const lldb::StopReason eStopReason = thread.GetStopReason();
  if ((eStopReason == lldb::eStopReasonNone) ||
      (eStopReason == lldb::eStopReasonInvalid)) {
    m_bThreadInvalid = true;
    return MIstatus::success;
  }

  lldb::SBFrame frame = (nFrame != UINT64_MAX) ? thread.GetFrameAtIndex(nFrame)
                                               : thread.GetSelectedFrame();

  CMICmnMIValueList miValueList(true);
  const MIuint maskVarTypes =
      CMICmnLLDBDebugSessionInfo::eVariableType_Arguments |
      CMICmnLLDBDebugSessionInfo::eVariableType_Locals |
      CMICmnLLDBDebugSessionInfo::eVariableType_InScope;
  if (!rSessionInfo.MIResponseFormVariableInfo(
          frame, maskVarTypes, eVarInfoFormat, miValueList, 10, true))
    return MIstatus::failure;
  m_miValueList = miValueList;

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
bool CMICmdCmdStackListVariables::Acknowledge() {
  if (m_bThreadInvalid) {
    // MI print "%s^done,variables=[]"
    const CMICmnMIValueList miValueList(true);
    const CMICmnMIValueResult miValueResult("variables", miValueList);
    const CMICmnMIResultRecord miRecordResult(
        m_cmdData.strMiCmdToken, CMICmnMIResultRecord::eResultClass_Done,
        miValueResult);
    m_miResultRecord = miRecordResult;
    return MIstatus::success;
  }

  // MI print "%s^done,variables=[%s]"
  const CMICmnMIValueResult miValueResult("variables", m_miValueList);
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
CMICmdBase *CMICmdCmdStackListVariables::CreateSelf() {
  return new CMICmdCmdStackListVariables();
}

//---------------------------------------------------------------------------------------
//---------------------------------------------------------------------------------------
//---------------------------------------------------------------------------------------

//++
//------------------------------------------------------------------------------------
// Details: CMICmdCmdStackSelectFrame constructor.
// Type:    Method.
// Args:    None.
// Return:  None.
// Throws:  None.
//--
CMICmdCmdStackSelectFrame::CMICmdCmdStackSelectFrame()
    : m_bFrameInvalid(false), m_constStrArgFrameId("frame_id") {
  // Command factory matches this name with that received from the stdin stream
  m_strMiCmd = "stack-select-frame";

  // Required by the CMICmdFactory when registering *this command
  m_pSelfCreatorFn = &CMICmdCmdStackSelectFrame::CreateSelf;
}

//++
//------------------------------------------------------------------------------------
// Details: CMICmdCmdStackSelectFrame destructor.
// Type:    Overrideable.
// Args:    None.
// Return:  None.
// Throws:  None.
//--
CMICmdCmdStackSelectFrame::~CMICmdCmdStackSelectFrame() {}

//++
//------------------------------------------------------------------------------------
// Details: The invoker requires this function. The parses the command line
// options
//          arguments to extract values for each of those arguments.
// Type:    Overridden.
// Args:    None.
// Return:  MIstatus::success - Function succeeded.
//          MIstatus::failure - Function failed.
// Throws:  None.
//--
bool CMICmdCmdStackSelectFrame::ParseArgs() {
  m_setCmdArgs.Add(new CMICmdArgValNumber(m_constStrArgFrameId, true, false));
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
// Return:  MIstatus::success - Function succeeded.
//          MIstatus::failure - Function failed.
// Throws:  None.
//--
bool CMICmdCmdStackSelectFrame::Execute() {
  CMICMDBASE_GETOPTION(pArgFrame, Number, m_constStrArgFrameId);

  CMICmnLLDBDebugSessionInfo &rSessionInfo(
      CMICmnLLDBDebugSessionInfo::Instance());
  lldb::SBThread sbThread = rSessionInfo.GetProcess().GetSelectedThread();

  const MIuint nFrameId = pArgFrame->GetValue();
  m_bFrameInvalid = (nFrameId >= sbThread.GetNumFrames());
  if (m_bFrameInvalid)
    return MIstatus::success;

  lldb::SBFrame sbFrame = sbThread.SetSelectedFrame(nFrameId);
  m_bFrameInvalid = !sbFrame.IsValid();

  return MIstatus::success;
}

//++
//------------------------------------------------------------------------------------
// Details: The invoker requires this function. The command prepares a MI Record
// Result
//          for the work carried out in the Execute().
// Type:    Overridden.
// Args:    None.
// Return:  MIstatus::success - Function succeeded.
//          MIstatus::failure - Function failed.
// Throws:  None.
//--
bool CMICmdCmdStackSelectFrame::Acknowledge() {
  if (m_bFrameInvalid) {
    // MI print "%s^error,msg=\"Command '-stack-select-frame'. Frame ID
    // invalid\""
    const CMICmnMIValueConst miValueConst(CMIUtilString::Format(
        MIRSRC(IDS_CMD_ERR_FRAME_INVALID), m_cmdData.strMiCmd.c_str()));
    const CMICmnMIValueResult miValueResult("msg", miValueConst);
    const CMICmnMIResultRecord miRecordResult(
        m_cmdData.strMiCmdToken, CMICmnMIResultRecord::eResultClass_Error,
        miValueResult);
    m_miResultRecord = miRecordResult;

    return MIstatus::success;
  }

  const CMICmnMIResultRecord miRecordResult(
      m_cmdData.strMiCmdToken, CMICmnMIResultRecord::eResultClass_Done);
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
CMICmdBase *CMICmdCmdStackSelectFrame::CreateSelf() {
  return new CMICmdCmdStackSelectFrame();
}
