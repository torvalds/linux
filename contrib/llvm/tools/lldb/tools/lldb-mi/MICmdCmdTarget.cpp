//===-- MICmdCmdTarget.cpp --------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

// Overview:    CMICmdCmdTargetSelect           implementation.

// Third Party Headers:
#include "lldb/API/SBStream.h"
#include "lldb/API/SBError.h"

// In-house headers:
#include "MICmdArgValNumber.h"
#include "MICmdArgValOptionLong.h"
#include "MICmdArgValOptionShort.h"
#include "MICmdArgValString.h"
#include "MICmdCmdTarget.h"
#include "MICmnLLDBDebugSessionInfo.h"
#include "MICmnLLDBDebugger.h"
#include "MICmnMIOutOfBandRecord.h"
#include "MICmnMIResultRecord.h"
#include "MICmnMIValueConst.h"

//++
//------------------------------------------------------------------------------------
// Details: CMICmdCmdTargetSelect constructor.
// Type:    Method.
// Args:    None.
// Return:  None.
// Throws:  None.
//--
CMICmdCmdTargetSelect::CMICmdCmdTargetSelect()
    : m_constStrArgNamedType("type"),
      m_constStrArgNamedParameters("parameters") {
  // Command factory matches this name with that received from the stdin stream
  m_strMiCmd = "target-select";

  // Required by the CMICmdFactory when registering *this command
  m_pSelfCreatorFn = &CMICmdCmdTargetSelect::CreateSelf;
}

//++
//------------------------------------------------------------------------------------
// Details: CMICmdCmdTargetSelect destructor.
// Type:    Overrideable.
// Args:    None.
// Return:  None.
// Throws:  None.
//--
CMICmdCmdTargetSelect::~CMICmdCmdTargetSelect() = default;

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
bool CMICmdCmdTargetSelect::ParseArgs() {
  m_setCmdArgs.Add(new CMICmdArgValString(m_constStrArgNamedType, true, true));
  m_setCmdArgs.Add(
      new CMICmdArgValString(m_constStrArgNamedParameters, true, true));
  return ParseValidateCmdOptions();
}

//++
//------------------------------------------------------------------------------------
// Details: The invoker requires this function. The command does work in this
// function.
//          The command is likely to communicate with the LLDB SBDebugger in
//          here.
//          Synopsis: -target-select type parameters ...
//          Ref:
//          http://sourceware.org/gdb/onlinedocs/gdb/GDB_002fMI-Target-Manipulation.html#GDB_002fMI-Target-Manipulation
// Type:    Overridden.
// Args:    None.
// Return:  MIstatus::success - Functional succeeded.
//          MIstatus::failure - Functional failed.
// Throws:  None.
//--
bool CMICmdCmdTargetSelect::Execute() {
  CMICMDBASE_GETOPTION(pArgType, String, m_constStrArgNamedType);
  CMICMDBASE_GETOPTION(pArgParameters, String, m_constStrArgNamedParameters);

  CMICmnLLDBDebugSessionInfo &rSessionInfo(
      CMICmnLLDBDebugSessionInfo::Instance());
  lldb::SBTarget target = rSessionInfo.GetTarget();

  // Check we have a valid target.
  // Note: target created via 'file-exec-and-symbols' command.
  if (!target.IsValid()) {
    SetError(CMIUtilString::Format(MIRSRC(IDS_CMD_ERR_INVALID_TARGET_CURRENT),
                                   m_cmdData.strMiCmd.c_str()));
    return MIstatus::failure;
  }

  // Verify that we are executing remotely.
  const CMIUtilString &rRemoteType(pArgType->GetValue());
  if (rRemoteType != "remote") {
    SetError(CMIUtilString::Format(MIRSRC(IDS_CMD_ERR_INVALID_TARGET_TYPE),
                                   m_cmdData.strMiCmd.c_str(),
                                   rRemoteType.c_str()));
    return MIstatus::failure;
  }

  // Create a URL pointing to the remote gdb stub.
  const CMIUtilString strUrl =
      CMIUtilString::Format("connect://%s", pArgParameters->GetValue().c_str());

  lldb::SBError error;
  // Ask LLDB to connect to the target port.
  const char *pPlugin("gdb-remote");
  lldb::SBProcess process = target.ConnectRemote(
      rSessionInfo.GetListener(), strUrl.c_str(), pPlugin, error);

  // Verify that we have managed to connect successfully.
  if (!process.IsValid()) {
    SetError(CMIUtilString::Format(MIRSRC(IDS_CMD_ERR_INVALID_TARGET_PLUGIN),
                                   m_cmdData.strMiCmd.c_str(),
                                   error.GetCString()));
    return MIstatus::failure;
  }

  // Set the environment path if we were given one.
  CMIUtilString strWkDir;
  if (rSessionInfo.SharedDataRetrieve<CMIUtilString>(
          rSessionInfo.m_constStrSharedDataKeyWkDir, strWkDir)) {
    lldb::SBDebugger &rDbgr = rSessionInfo.GetDebugger();
    if (!rDbgr.SetCurrentPlatformSDKRoot(strWkDir.c_str())) {
      SetError(CMIUtilString::Format(MIRSRC(IDS_CMD_ERR_FNFAILED),
                                     m_cmdData.strMiCmd.c_str(),
                                     "target-select"));
      return MIstatus::failure;
    }
  }

  // Set the shared object path if we were given one.
  CMIUtilString strSolibPath;
  if (rSessionInfo.SharedDataRetrieve<CMIUtilString>(
          rSessionInfo.m_constStrSharedDataSolibPath, strSolibPath))
    target.AppendImageSearchPath(".", strSolibPath.c_str(), error);

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
bool CMICmdCmdTargetSelect::Acknowledge() {
  const CMICmnMIResultRecord miRecordResult(
      m_cmdData.strMiCmdToken, CMICmnMIResultRecord::eResultClass_Connected);
  m_miResultRecord = miRecordResult;

  CMICmnLLDBDebugSessionInfo &rSessionInfo(
      CMICmnLLDBDebugSessionInfo::Instance());
  lldb::pid_t pid = rSessionInfo.GetProcess().GetProcessID();
  // Prod the client i.e. Eclipse with out-of-band results to help it 'continue'
  // because it is using LLDB debugger
  // Give the client '=thread-group-started,id="i1"'
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
CMICmdBase *CMICmdCmdTargetSelect::CreateSelf() {
  return new CMICmdCmdTargetSelect();
}

//++
//------------------------------------------------------------------------------------
// Details: CMICmdCmdTargetAttach constructor.
// Type:    Method.
// Args:    None.
// Return:  None.
// Throws:  None.
//--
CMICmdCmdTargetAttach::CMICmdCmdTargetAttach()
    : m_constStrArgPid("pid"), m_constStrArgNamedFile("n"),
      m_constStrArgWaitFor("waitfor") {
  // Command factory matches this name with that received from the stdin stream
  m_strMiCmd = "target-attach";

  // Required by the CMICmdFactory when registering *this command
  m_pSelfCreatorFn = &CMICmdCmdTargetAttach::CreateSelf;
}

//++
//------------------------------------------------------------------------------------
// Details: CMICmdCmdTargetAttach destructor.
// Type:    Overrideable.
// Args:    None.
// Return:  None.
// Throws:  None.
//--
CMICmdCmdTargetAttach::~CMICmdCmdTargetAttach() {}

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
bool CMICmdCmdTargetAttach::ParseArgs() {
  m_setCmdArgs.Add(new CMICmdArgValNumber(m_constStrArgPid, false, true));
  m_setCmdArgs.Add(
      new CMICmdArgValOptionShort(m_constStrArgNamedFile, false, true,
                                  CMICmdArgValListBase::eArgValType_String, 1));
  m_setCmdArgs.Add(
      new CMICmdArgValOptionLong(m_constStrArgWaitFor, false, true));
  return ParseValidateCmdOptions();
}

//++
//------------------------------------------------------------------------------------
// Details: The invoker requires this function. The command does work in this
// function.
//          The command is likely to communicate with the LLDB SBDebugger in
//          here.
//          Synopsis: -target-attach file
//          Ref:
//          http://sourceware.org/gdb/onlinedocs/gdb/GDB_002fMI-Target-Manipulation.html#GDB_002fMI-Target-Manipulation
// Type:    Overridden.
// Args:    None.
// Return:  MIstatus::success - Functional succeeded.
//          MIstatus::failure - Functional failed.
// Throws:  None.
//--
bool CMICmdCmdTargetAttach::Execute() {
  CMICMDBASE_GETOPTION(pArgPid, Number, m_constStrArgPid);
  CMICMDBASE_GETOPTION(pArgFile, OptionShort, m_constStrArgNamedFile);
  CMICMDBASE_GETOPTION(pArgWaitFor, OptionLong, m_constStrArgWaitFor);

  CMICmnLLDBDebugSessionInfo &rSessionInfo(
      CMICmnLLDBDebugSessionInfo::Instance());

  // If the current target is invalid, create one
  lldb::SBTarget target = rSessionInfo.GetTarget();
  if (!target.IsValid()) {
    target = rSessionInfo.GetDebugger().CreateTarget(NULL);
    if (!target.IsValid()) {
      SetError(CMIUtilString::Format(MIRSRC(IDS_CMD_ERR_INVALID_TARGET_CURRENT),
                                     m_cmdData.strMiCmd.c_str()));
      return MIstatus::failure;
    }
  }

  lldb::SBError error;
  lldb::SBListener listener;
  if (pArgPid->GetFound() && pArgPid->GetValid()) {
    lldb::pid_t pid;
    pid = pArgPid->GetValue();
    target.AttachToProcessWithID(listener, pid, error);
  } else if (pArgFile->GetFound() && pArgFile->GetValid()) {
    bool bWaitFor = (pArgWaitFor->GetFound());
    CMIUtilString file;
    pArgFile->GetExpectedOption<CMICmdArgValString>(file);
    target.AttachToProcessWithName(listener, file.c_str(), bWaitFor, error);
  } else {
    SetError(CMIUtilString::Format(MIRSRC(IDS_CMD_ERR_ATTACH_BAD_ARGS),
                                   m_cmdData.strMiCmd.c_str()));
    return MIstatus::failure;
  }

  lldb::SBStream errMsg;
  if (error.Fail()) {
    SetError(CMIUtilString::Format(MIRSRC(IDS_CMD_ERR_ATTACH_FAILED),
                                   m_cmdData.strMiCmd.c_str(),
                                   errMsg.GetData()));
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
// Return:  MIstatus::success - Functional succeeded.
//          MIstatus::failure - Functional failed.
// Throws:  None.
//--
bool CMICmdCmdTargetAttach::Acknowledge() {
  const CMICmnMIResultRecord miRecordResult(
      m_cmdData.strMiCmdToken, CMICmnMIResultRecord::eResultClass_Done);
  m_miResultRecord = miRecordResult;

  CMICmnLLDBDebugSessionInfo &rSessionInfo(
      CMICmnLLDBDebugSessionInfo::Instance());
  lldb::pid_t pid = rSessionInfo.GetProcess().GetProcessID();
  // Prod the client i.e. Eclipse with out-of-band results to help it 'continue'
  // because it is using LLDB debugger
  // Give the client '=thread-group-started,id="i1"'
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
CMICmdBase *CMICmdCmdTargetAttach::CreateSelf() {
  return new CMICmdCmdTargetAttach();
}

//++
//------------------------------------------------------------------------------------
// Details: CMICmdCmdTargetDetach constructor.
// Type:    Method.
// Args:    None.
// Return:  None.
// Throws:  None.
//--
CMICmdCmdTargetDetach::CMICmdCmdTargetDetach() {
  // Command factory matches this name with that received from the stdin stream
  m_strMiCmd = "target-detach";

  // Required by the CMICmdFactory when registering *this command
  m_pSelfCreatorFn = &CMICmdCmdTargetDetach::CreateSelf;
}

//++
//------------------------------------------------------------------------------------
// Details: CMICmdCmdTargetDetach destructor.
// Type:    Overrideable.
// Args:    None.
// Return:  None.
// Throws:  None.
//--
CMICmdCmdTargetDetach::~CMICmdCmdTargetDetach() {}

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
bool CMICmdCmdTargetDetach::ParseArgs() { return MIstatus::success; }

//++
//------------------------------------------------------------------------------------
// Details: The invoker requires this function. The command does work in this
// function.
//          The command is likely to communicate with the LLDB SBDebugger in
//          here.
//          Synopsis: -target-attach file
//          Ref:
//          http://sourceware.org/gdb/onlinedocs/gdb/GDB_002fMI-Target-Manipulation.html#GDB_002fMI-Target-Manipulation
// Type:    Overridden.
// Args:    None.
// Return:  MIstatus::success - Functional succeeded.
//          MIstatus::failure - Functional failed.
// Throws:  None.
//--
bool CMICmdCmdTargetDetach::Execute() {
  CMICmnLLDBDebugSessionInfo &rSessionInfo(
      CMICmnLLDBDebugSessionInfo::Instance());

  lldb::SBProcess process = rSessionInfo.GetProcess();

  if (!process.IsValid()) {
    SetError(CMIUtilString::Format(MIRSRC(IDS_CMD_ERR_INVALID_PROCESS),
                                   m_cmdData.strMiCmd.c_str()));
    return MIstatus::failure;
  }

  process.Detach();

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
bool CMICmdCmdTargetDetach::Acknowledge() {
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
CMICmdBase *CMICmdCmdTargetDetach::CreateSelf() {
  return new CMICmdCmdTargetDetach();
}
