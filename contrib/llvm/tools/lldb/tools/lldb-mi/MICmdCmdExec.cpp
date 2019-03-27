//===-- MICmdCmdExec.cpp ----------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

// Overview:    CMICmdCmdExecRun                implementation.
//              CMICmdCmdExecContinue           implementation.
//              CMICmdCmdExecNext               implementation.
//              CMICmdCmdExecStep               implementation.
//              CMICmdCmdExecNextInstruction    implementation.
//              CMICmdCmdExecStepInstruction    implementation.
//              CMICmdCmdExecFinish             implementation.
//              CMICmdCmdExecInterrupt          implementation.
//              CMICmdCmdExecArguments          implementation.
//              CMICmdCmdExecAbort              implementation.

// Third Party Headers:
#include "lldb/API/SBCommandInterpreter.h"
#include "lldb/API/SBProcess.h"
#include "lldb/API/SBStream.h"
#include "lldb/API/SBThread.h"
#include "lldb/lldb-enumerations.h"

// In-house headers:
#include "MICmdArgValListOfN.h"
#include "MICmdArgValNumber.h"
#include "MICmdArgValOptionLong.h"
#include "MICmdArgValOptionShort.h"
#include "MICmdArgValString.h"
#include "MICmdArgValThreadGrp.h"
#include "MICmdCmdExec.h"
#include "MICmnLLDBDebugSessionInfo.h"
#include "MICmnLLDBDebugger.h"
#include "MICmnMIOutOfBandRecord.h"
#include "MICmnMIResultRecord.h"
#include "MICmnMIValueConst.h"
#include "MICmnStreamStdout.h"
#include "MIDriver.h"

//++
//------------------------------------------------------------------------------------
// Details: CMICmdCmdExecRun constructor.
// Type:    Method.
// Args:    None.
// Return:  None.
// Throws:  None.
//--
CMICmdCmdExecRun::CMICmdCmdExecRun() : m_constStrArgStart("start") {
  // Command factory matches this name with that received from the stdin stream
  m_strMiCmd = "exec-run";

  // Required by the CMICmdFactory when registering *this command
  m_pSelfCreatorFn = &CMICmdCmdExecRun::CreateSelf;
}

//++
//------------------------------------------------------------------------------------
// Details: CMICmdCmdExecRun destructor.
// Type:    Overrideable.
// Args:    None.
// Return:  None.
// Throws:  None.
//--
CMICmdCmdExecRun::~CMICmdCmdExecRun() {}

//++
//------------------------------------------------------------------------------------
// Details: The invoker requires this function. It parses the command line
// options'
//          arguments to extract values for each of those arguments.
// Type:    Overridden.
// Args:    None.
// Return:  MIstatus::success - Functional succeeded.
//          MIstatus::failure - Functional failed.
// Throws:  None.
//--
bool CMICmdCmdExecRun::ParseArgs() {
  m_setCmdArgs.Add(new CMICmdArgValOptionLong(
      m_constStrArgStart, false, true,
      CMICmdArgValListBase::eArgValType_OptionLong, 0));
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
bool CMICmdCmdExecRun::Execute() {
  CMICmnLLDBDebugSessionInfo &rSessionInfo(
      CMICmnLLDBDebugSessionInfo::Instance());

  {
    // Check we have a valid target.
    // Note: target created via 'file-exec-and-symbols' command.
    lldb::SBTarget sbTarget = rSessionInfo.GetTarget();
    if (!sbTarget.IsValid() ||
        sbTarget == rSessionInfo.GetDebugger().GetDummyTarget()) {
      SetError(CMIUtilString::Format(MIRSRC(IDS_CMD_ERR_INVALID_TARGET_CURRENT),
                                     m_cmdData.strMiCmd.c_str()));
      return MIstatus::failure;
    }
  }

  lldb::SBError error;
  lldb::SBStream errMsg;
  lldb::SBLaunchInfo launchInfo = rSessionInfo.GetTarget().GetLaunchInfo();
  launchInfo.SetListener(rSessionInfo.GetListener());

  // Run to first instruction or main() requested?
  CMICMDBASE_GETOPTION(pArgStart, OptionLong, m_constStrArgStart);
  if (pArgStart->GetFound()) {
    launchInfo.SetLaunchFlags(launchInfo.GetLaunchFlags() |
                              lldb::eLaunchFlagStopAtEntry);
  }

  lldb::SBProcess process = rSessionInfo.GetTarget().Launch(launchInfo, error);
  if (!process.IsValid()) {
    SetError(CMIUtilString::Format(MIRSRC(IDS_CMD_ERR_INVALID_PROCESS),
                                   m_cmdData.strMiCmd.c_str(),
                                   errMsg.GetData()));
    return MIstatus::failure;
  }

  const auto successHandler = [this] {
    if (!CMIDriver::Instance().SetDriverStateRunningDebugging()) {
      const CMIUtilString &rErrMsg(CMIDriver::Instance().GetErrorDescription());
      SetError(CMIUtilString::Format(
          MIRSRC(IDS_CMD_ERR_SET_NEW_DRIVER_STATE),
          m_cmdData.strMiCmd.c_str(), rErrMsg.c_str()));
      return MIstatus::failure;
    }
    return MIstatus::success;
  };

  return HandleSBErrorWithSuccess(error, successHandler);
}

//++
//------------------------------------------------------------------------------------
// Details: The invoker requires this function. The command prepares a MI Record
// Result
//          for the work carried out in the Execute().
//          Called only if Execute() set status as successful on completion.
// Type:    Overridden.
// Args:    None.
// Return:  MIstatus::success - Functional succeeded.
//          MIstatus::failure - Functional failed.
// Throws:  None.
//--
bool CMICmdCmdExecRun::Acknowledge() {
  m_miResultRecord = CMICmnMIResultRecord(
      m_cmdData.strMiCmdToken, CMICmnMIResultRecord::eResultClass_Running);

  CMICmnLLDBDebugSessionInfo &rSessionInfo(
      CMICmnLLDBDebugSessionInfo::Instance());
  lldb::pid_t pid = rSessionInfo.GetProcess().GetProcessID();
  // Give the client '=thread-group-started,id="i1" pid="xyz"'
  m_bHasResultRecordExtra = true;
  const CMICmnMIValueConst miValueConst2("i1");
  const CMICmnMIValueResult miValueResult2("id", miValueConst2);
  const CMIUtilString strPid(CMIUtilString::Format("%lld", pid));
  const CMICmnMIValueConst miValueConst(strPid);
  const CMICmnMIValueResult miValueResult("pid", miValueConst);
  CMICmnMIOutOfBandRecord miOutOfBand(
      CMICmnMIOutOfBandRecord::eOutOfBand_ThreadGroupStarted, miValueResult2);
  miOutOfBand.Add(miValueResult);
  m_miResultRecordExtra = miOutOfBand.GetString();

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
CMICmdBase *CMICmdCmdExecRun::CreateSelf() { return new CMICmdCmdExecRun(); }

//---------------------------------------------------------------------------------------
//---------------------------------------------------------------------------------------
//---------------------------------------------------------------------------------------

//++
//------------------------------------------------------------------------------------
// Details: CMICmdCmdExecContinue constructor.
// Type:    Method.
// Args:    None.
// Return:  None.
// Throws:  None.
//--
CMICmdCmdExecContinue::CMICmdCmdExecContinue() {
  // Command factory matches this name with that received from the stdin stream
  m_strMiCmd = "exec-continue";

  // Required by the CMICmdFactory when registering *this command
  m_pSelfCreatorFn = &CMICmdCmdExecContinue::CreateSelf;
}

//++
//------------------------------------------------------------------------------------
// Details: CMICmdCmdExecContinue destructor.
// Type:    Overrideable.
// Args:    None.
// Return:  None.
// Throws:  None.
//--
CMICmdCmdExecContinue::~CMICmdCmdExecContinue() {}

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
bool CMICmdCmdExecContinue::Execute() {
  const auto successHandler = [this] {
    // CODETAG_DEBUG_SESSION_RUNNING_PROG_RECEIVED_SIGINT_PAUSE_PROGRAM
    if (!CMIDriver::Instance().SetDriverStateRunningDebugging()) {
      const CMIUtilString &rErrMsg(CMIDriver::Instance().GetErrorDescription());
      SetError(CMIUtilString::Format(
          MIRSRC(IDS_CMD_ERR_SET_NEW_DRIVER_STATE),
          m_cmdData.strMiCmd.c_str(), rErrMsg.c_str()));
      return MIstatus::failure;
    }
    return MIstatus::success;
  };

  return HandleSBErrorWithSuccess(
      CMICmnLLDBDebugSessionInfo::Instance().GetProcess().Continue(),
      successHandler);
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
bool CMICmdCmdExecContinue::Acknowledge() {
  m_miResultRecord = CMICmnMIResultRecord(
      m_cmdData.strMiCmdToken, CMICmnMIResultRecord::eResultClass_Running);
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
CMICmdBase *CMICmdCmdExecContinue::CreateSelf() {
  return new CMICmdCmdExecContinue();
}

//---------------------------------------------------------------------------------------
//---------------------------------------------------------------------------------------
//---------------------------------------------------------------------------------------

//++
//------------------------------------------------------------------------------------
// Details: CMICmdCmdExecNext constructor.
// Type:    Method.
// Args:    None.
// Return:  None.
// Throws:  None.
//--
CMICmdCmdExecNext::CMICmdCmdExecNext() : m_constStrArgNumber("number") {
  // Command factory matches this name with that received from the stdin stream
  m_strMiCmd = "exec-next";

  // Required by the CMICmdFactory when registering *this command
  m_pSelfCreatorFn = &CMICmdCmdExecNext::CreateSelf;
}

//++
//------------------------------------------------------------------------------------
// Details: CMICmdCmdExecNext destructor.
// Type:    Overrideable.
// Args:    None.
// Return:  None.
// Throws:  None.
//--
CMICmdCmdExecNext::~CMICmdCmdExecNext() {}

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
bool CMICmdCmdExecNext::ParseArgs() {
  m_setCmdArgs.Add(new CMICmdArgValNumber(m_constStrArgNumber, false, false));
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
bool CMICmdCmdExecNext::Execute() {
  CMICMDBASE_GETOPTION(pArgThread, OptionLong, m_constStrArgThread);

  // Retrieve the --thread option's thread ID (only 1)
  MIuint64 nThreadId = UINT64_MAX;
  if (pArgThread->GetFound() &&
      !pArgThread->GetExpectedOption<CMICmdArgValNumber, MIuint64>(nThreadId)) {
    SetError(CMIUtilString::Format(MIRSRC(IDS_CMD_ERR_THREAD_INVALID),
                                   m_cmdData.strMiCmd.c_str(),
                                   m_constStrArgThread.c_str()));
    return MIstatus::failure;
  }

  CMICmnLLDBDebugSessionInfo &rSessionInfo(
      CMICmnLLDBDebugSessionInfo::Instance());

  lldb::SBError error;
  if (nThreadId != UINT64_MAX) {
    lldb::SBThread sbThread = rSessionInfo.GetProcess().GetThreadByIndexID(nThreadId);
    if (!sbThread.IsValid()) {
      SetError(CMIUtilString::Format(MIRSRC(IDS_CMD_ERR_THREAD_INVALID),
                                     m_cmdData.strMiCmd.c_str(),
                                     m_constStrArgThread.c_str()));
      return MIstatus::failure;
    }
    sbThread.StepOver(lldb::eOnlyDuringStepping, error);
  } else
    rSessionInfo.GetProcess().GetSelectedThread().StepOver(
        lldb::eOnlyDuringStepping, error);

  return HandleSBError(error);
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
bool CMICmdCmdExecNext::Acknowledge() {
  m_miResultRecord = CMICmnMIResultRecord(
      m_cmdData.strMiCmdToken, CMICmnMIResultRecord::eResultClass_Running);
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
CMICmdBase *CMICmdCmdExecNext::CreateSelf() { return new CMICmdCmdExecNext(); }

//---------------------------------------------------------------------------------------
//---------------------------------------------------------------------------------------
//---------------------------------------------------------------------------------------

//++
//------------------------------------------------------------------------------------
// Details: CMICmdCmdExecStep constructor.
// Type:    Method.
// Args:    None.
// Return:  None.
// Throws:  None.
//--
CMICmdCmdExecStep::CMICmdCmdExecStep() : m_constStrArgNumber("number") {
  // Command factory matches this name with that received from the stdin stream
  m_strMiCmd = "exec-step";

  // Required by the CMICmdFactory when registering *this command
  m_pSelfCreatorFn = &CMICmdCmdExecStep::CreateSelf;
}

//++
//------------------------------------------------------------------------------------
// Details: CMICmdCmdExecStep destructor.
// Type:    Overrideable.
// Args:    None.
// Return:  None.
// Throws:  None.
//--
CMICmdCmdExecStep::~CMICmdCmdExecStep() {}

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
bool CMICmdCmdExecStep::ParseArgs() {
  m_setCmdArgs.Add(new CMICmdArgValNumber(m_constStrArgNumber, false, false));
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
bool CMICmdCmdExecStep::Execute() {
  CMICMDBASE_GETOPTION(pArgThread, OptionLong, m_constStrArgThread);

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

  lldb::SBError error;
  if (nThreadId != UINT64_MAX) {
    lldb::SBThread sbThread =
        rSessionInfo.GetProcess().GetThreadByIndexID(nThreadId);
    if (!sbThread.IsValid()) {
      SetError(CMIUtilString::Format(MIRSRC(IDS_CMD_ERR_THREAD_INVALID),
                                     m_cmdData.strMiCmd.c_str(),
                                     m_constStrArgThread.c_str()));
      return MIstatus::failure;
    }
    sbThread.StepInto(nullptr, LLDB_INVALID_LINE_NUMBER, error);
  } else
    rSessionInfo.GetProcess().GetSelectedThread().StepInto(
        nullptr, LLDB_INVALID_LINE_NUMBER, error);

  return HandleSBError(error);
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
bool CMICmdCmdExecStep::Acknowledge() {
  m_miResultRecord = CMICmnMIResultRecord(
      m_cmdData.strMiCmdToken, CMICmnMIResultRecord::eResultClass_Running);
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
CMICmdBase *CMICmdCmdExecStep::CreateSelf() { return new CMICmdCmdExecStep(); }

//---------------------------------------------------------------------------------------
//---------------------------------------------------------------------------------------
//---------------------------------------------------------------------------------------

//++
//------------------------------------------------------------------------------------
// Details: CMICmdCmdExecNextInstruction constructor.
// Type:    Method.
// Args:    None.
// Return:  None.
// Throws:  None.
//--
CMICmdCmdExecNextInstruction::CMICmdCmdExecNextInstruction()
    : m_constStrArgNumber("number") {
  // Command factory matches this name with that received from the stdin stream
  m_strMiCmd = "exec-next-instruction";

  // Required by the CMICmdFactory when registering *this command
  m_pSelfCreatorFn = &CMICmdCmdExecNextInstruction::CreateSelf;
}

//++
//------------------------------------------------------------------------------------
// Details: CMICmdCmdExecNextInstruction destructor.
// Type:    Overrideable.
// Args:    None.
// Return:  None.
// Throws:  None.
//--
CMICmdCmdExecNextInstruction::~CMICmdCmdExecNextInstruction() {}

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
bool CMICmdCmdExecNextInstruction::ParseArgs() {
  m_setCmdArgs.Add(new CMICmdArgValNumber(m_constStrArgNumber, false, false));
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
bool CMICmdCmdExecNextInstruction::Execute() {
  CMICMDBASE_GETOPTION(pArgThread, OptionLong, m_constStrArgThread);

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

  lldb::SBError error;
  if (nThreadId != UINT64_MAX) {
    lldb::SBThread sbThread =
        rSessionInfo.GetProcess().GetThreadByIndexID(nThreadId);
    if (!sbThread.IsValid()) {
      SetError(CMIUtilString::Format(MIRSRC(IDS_CMD_ERR_THREAD_INVALID),
                                     m_cmdData.strMiCmd.c_str(),
                                     m_constStrArgThread.c_str()));
      return MIstatus::failure;
    }
    sbThread.StepInstruction(true, error);
  } else
    rSessionInfo.GetProcess().GetSelectedThread().StepInstruction(
        true, error);

  return HandleSBError(error);
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
bool CMICmdCmdExecNextInstruction::Acknowledge() {
  m_miResultRecord = CMICmnMIResultRecord(
      m_cmdData.strMiCmdToken, CMICmnMIResultRecord::eResultClass_Running);
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
CMICmdBase *CMICmdCmdExecNextInstruction::CreateSelf() {
  return new CMICmdCmdExecNextInstruction();
}

//---------------------------------------------------------------------------------------
//---------------------------------------------------------------------------------------
//---------------------------------------------------------------------------------------

//++
//------------------------------------------------------------------------------------
// Details: CMICmdCmdExecStepInstruction constructor.
// Type:    Method.
// Args:    None.
// Return:  None.
// Throws:  None.
//--
CMICmdCmdExecStepInstruction::CMICmdCmdExecStepInstruction()
    : m_constStrArgNumber("number") {
  // Command factory matches this name with that received from the stdin stream
  m_strMiCmd = "exec-step-instruction";

  // Required by the CMICmdFactory when registering *this command
  m_pSelfCreatorFn = &CMICmdCmdExecStepInstruction::CreateSelf;
}

//++
//------------------------------------------------------------------------------------
// Details: CMICmdCmdExecStepInstruction destructor.
// Type:    Overrideable.
// Args:    None.
// Return:  None.
// Throws:  None.
//--
CMICmdCmdExecStepInstruction::~CMICmdCmdExecStepInstruction() {}

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
bool CMICmdCmdExecStepInstruction::ParseArgs() {
  m_setCmdArgs.Add(new CMICmdArgValNumber(m_constStrArgNumber, false, false));
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
bool CMICmdCmdExecStepInstruction::Execute() {
  CMICMDBASE_GETOPTION(pArgThread, OptionLong, m_constStrArgThread);

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

  lldb::SBError error;
  if (nThreadId != UINT64_MAX) {
    lldb::SBThread sbThread =
        rSessionInfo.GetProcess().GetThreadByIndexID(nThreadId);
    if (!sbThread.IsValid()) {
      SetError(CMIUtilString::Format(MIRSRC(IDS_CMD_ERR_THREAD_INVALID),
                                     m_cmdData.strMiCmd.c_str(),
                                     m_constStrArgThread.c_str()));
      return MIstatus::failure;
    }
    sbThread.StepInstruction(false, error);
  } else
    rSessionInfo.GetProcess().GetSelectedThread().StepInstruction(
        false, error);

  return HandleSBError(error);
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
bool CMICmdCmdExecStepInstruction::Acknowledge() {
  m_miResultRecord = CMICmnMIResultRecord(
      m_cmdData.strMiCmdToken, CMICmnMIResultRecord::eResultClass_Running);
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
CMICmdBase *CMICmdCmdExecStepInstruction::CreateSelf() {
  return new CMICmdCmdExecStepInstruction();
}

//---------------------------------------------------------------------------------------
//---------------------------------------------------------------------------------------
//---------------------------------------------------------------------------------------

//++
//------------------------------------------------------------------------------------
// Details: CMICmdCmdExecFinish constructor.
// Type:    Method.
// Args:    None.
// Return:  None.
// Throws:  None.
//--
CMICmdCmdExecFinish::CMICmdCmdExecFinish() {
  // Command factory matches this name with that received from the stdin stream
  m_strMiCmd = "exec-finish";

  // Required by the CMICmdFactory when registering *this command
  m_pSelfCreatorFn = &CMICmdCmdExecFinish::CreateSelf;
}

//++
//------------------------------------------------------------------------------------
// Details: CMICmdCmdExecFinish destructor.
// Type:    Overrideable.
// Args:    None.
// Return:  None.
// Throws:  None.
//--
CMICmdCmdExecFinish::~CMICmdCmdExecFinish() {}

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
bool CMICmdCmdExecFinish::ParseArgs() { return ParseValidateCmdOptions(); }

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
bool CMICmdCmdExecFinish::Execute() {
  CMICMDBASE_GETOPTION(pArgThread, OptionLong, m_constStrArgThread);

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

  lldb::SBError error;
  if (nThreadId != UINT64_MAX) {
    lldb::SBThread sbThread =
        rSessionInfo.GetProcess().GetThreadByIndexID(nThreadId);
    if (!sbThread.IsValid()) {
      SetError(CMIUtilString::Format(MIRSRC(IDS_CMD_ERR_THREAD_INVALID),
                                     m_cmdData.strMiCmd.c_str(),
                                     m_constStrArgThread.c_str()));
      return MIstatus::failure;
    }
    sbThread.StepOut(error);
  } else
    rSessionInfo.GetProcess().GetSelectedThread().StepOut(error);

  return HandleSBError(error);
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
bool CMICmdCmdExecFinish::Acknowledge() {
  m_miResultRecord = CMICmnMIResultRecord(
      m_cmdData.strMiCmdToken, CMICmnMIResultRecord::eResultClass_Running);
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
CMICmdBase *CMICmdCmdExecFinish::CreateSelf() {
  return new CMICmdCmdExecFinish();
}

//---------------------------------------------------------------------------------------
//---------------------------------------------------------------------------------------
//---------------------------------------------------------------------------------------

//++
//------------------------------------------------------------------------------------
// Details: CMICmdCmdExecInterrupt constructor.
// Type:    Method.
// Args:    None.
// Return:  None.
// Throws:  None.
//--
CMICmdCmdExecInterrupt::CMICmdCmdExecInterrupt() {
  // Command factory matches this name with that received from the stdin stream
  m_strMiCmd = "exec-interrupt";

  // Required by the CMICmdFactory when registering *this command
  m_pSelfCreatorFn = &CMICmdCmdExecInterrupt::CreateSelf;
}

//++
//------------------------------------------------------------------------------------
// Details: CMICmdCmdExecInterrupt destructor.
// Type:    Overrideable.
// Args:    None.
// Return:  None.
// Throws:  None.
//--
CMICmdCmdExecInterrupt::~CMICmdCmdExecInterrupt() {}

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
bool CMICmdCmdExecInterrupt::Execute() {
  const auto successHandler = [this] {
    // CODETAG_DEBUG_SESSION_RUNNING_PROG_RECEIVED_SIGINT_PAUSE_PROGRAM
    if (!CMIDriver::Instance().SetDriverStateRunningNotDebugging()) {
      const CMIUtilString &rErrMsg(CMIDriver::Instance().GetErrorDescription());
      SetErrorDescription(CMIUtilString::Format(
          MIRSRC(IDS_CMD_ERR_SET_NEW_DRIVER_STATE),
          m_cmdData.strMiCmd.c_str(),
          rErrMsg.c_str()));
      return MIstatus::failure;
    }
    return MIstatus::success;
  };

  return HandleSBErrorWithSuccess(
      CMICmnLLDBDebugSessionInfo::Instance().GetProcess().Stop(),
      successHandler);
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
bool CMICmdCmdExecInterrupt::Acknowledge() {
  m_miResultRecord = CMICmnMIResultRecord(
      m_cmdData.strMiCmdToken, CMICmnMIResultRecord::eResultClass_Done);
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
CMICmdBase *CMICmdCmdExecInterrupt::CreateSelf() {
  return new CMICmdCmdExecInterrupt();
}

//---------------------------------------------------------------------------------------
//---------------------------------------------------------------------------------------
//---------------------------------------------------------------------------------------

//++
//------------------------------------------------------------------------------------
// Details: CMICmdCmdExecArguments constructor.
// Type:    Method.
// Args:    None.
// Return:  None.
// Throws:  None.
//--
CMICmdCmdExecArguments::CMICmdCmdExecArguments()
    : m_constStrArgArguments("arguments") {
  // Command factory matches this name with that received from the stdin stream
  m_strMiCmd = "exec-arguments";

  // Required by the CMICmdFactory when registering *this command
  m_pSelfCreatorFn = &CMICmdCmdExecArguments::CreateSelf;
}

//++
//------------------------------------------------------------------------------------
// Details: CMICmdCmdExecArguments destructor.
// Type:    Overrideable.
// Args:    None.
// Return:  None.
// Throws:  None.
//--
CMICmdCmdExecArguments::~CMICmdCmdExecArguments() {}

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
bool CMICmdCmdExecArguments::ParseArgs() {
  m_setCmdArgs.Add(new CMICmdArgValListOfN(
      m_constStrArgArguments, false, true,
      CMICmdArgValListBase::eArgValType_StringAnything));
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
bool CMICmdCmdExecArguments::Execute() {
  CMICMDBASE_GETOPTION(pArgArguments, ListOfN, m_constStrArgArguments);

  CMICmnLLDBDebugSessionInfo &rSessionInfo(
      CMICmnLLDBDebugSessionInfo::Instance());
  lldb::SBTarget sbTarget = rSessionInfo.GetTarget();
  if (!sbTarget.IsValid()) {
    SetError(CMIUtilString::Format(MIRSRC(IDS_CMD_ERR_INVALID_TARGET_CURRENT),
                                   m_cmdData.strMiCmd.c_str()));
    return MIstatus::failure;
  }

  lldb::SBLaunchInfo sbLaunchInfo = sbTarget.GetLaunchInfo();
  sbLaunchInfo.SetArguments(NULL, false);

  CMIUtilString strArg;
  size_t nArgIndex = 0;
  while (pArgArguments->GetExpectedOption<CMICmdArgValString, CMIUtilString>(
      strArg, nArgIndex)) {
    const char *argv[2] = {strArg.c_str(), NULL};
    sbLaunchInfo.SetArguments(argv, true);
    ++nArgIndex;
  }

  sbTarget.SetLaunchInfo(sbLaunchInfo);

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
bool CMICmdCmdExecArguments::Acknowledge() {
  m_miResultRecord = CMICmnMIResultRecord(
      m_cmdData.strMiCmdToken, CMICmnMIResultRecord::eResultClass_Done);
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
CMICmdBase *CMICmdCmdExecArguments::CreateSelf() {
  return new CMICmdCmdExecArguments();
}

//---------------------------------------------------------------------------------------
//---------------------------------------------------------------------------------------
//---------------------------------------------------------------------------------------

//++
//------------------------------------------------------------------------------------
// Details: CMICmdCmdExecAbort constructor.
// Type:    Method.
// Args:    None.
// Return:  None.
// Throws:  None.
//--
CMICmdCmdExecAbort::CMICmdCmdExecAbort() {
  // Command factory matches this name with that received from the stdin stream
  m_strMiCmd = "exec-abort";

  // Required by the CMICmdFactory when registering *this command
  m_pSelfCreatorFn = &CMICmdCmdExecAbort::CreateSelf;
}

//++
//------------------------------------------------------------------------------------
// Details: CMICmdCmdExecAbort destructor.
// Type:    Overrideable.
// Args:    None.
// Return:  None.
// Throws:  None.
//--
CMICmdCmdExecAbort::~CMICmdCmdExecAbort() {}

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
bool CMICmdCmdExecAbort::Execute() {
  CMICmnLLDBDebugSessionInfo &rSessionInfo(
      CMICmnLLDBDebugSessionInfo::Instance());
  lldb::SBProcess sbProcess = rSessionInfo.GetProcess();
  if (!sbProcess.IsValid()) {
    SetError(CMIUtilString::Format(MIRSRC(IDS_CMD_ERR_INVALID_PROCESS),
                                   m_cmdData.strMiCmd.c_str()));
    return MIstatus::failure;
  }

  lldb::SBError sbError = sbProcess.Destroy();
  if (sbError.Fail()) {
    SetError(CMIUtilString::Format(MIRSRC(IDS_CMD_ERR_LLDBPROCESS_DESTROY),
                                   m_cmdData.strMiCmd.c_str(),
                                   sbError.GetCString()));
    return MIstatus::failure;
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
// Return:  MIstatus::success - Function succeeded.
//          MIstatus::failure - Function failed.
// Throws:  None.
//--
bool CMICmdCmdExecAbort::Acknowledge() {
  m_miResultRecord = CMICmnMIResultRecord(
      m_cmdData.strMiCmdToken, CMICmnMIResultRecord::eResultClass_Done);
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
CMICmdBase *CMICmdCmdExecAbort::CreateSelf() {
  return new CMICmdCmdExecAbort();
}
