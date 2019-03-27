//===-- MICmdCmdEnviro.cpp --------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

// Overview:    CMICmdCmdEnvironmentCd          implementation.

// In-house headers:
#include "MICmdCmdEnviro.h"
#include "MICmdArgValFile.h"
#include "MICmnLLDBDebugSessionInfo.h"
#include "MICmnLLDBDebugger.h"
#include "MICmnMIResultRecord.h"
#include "MICmnMIValueConst.h"

//++
//------------------------------------------------------------------------------------
// Details: CMICmdCmdEnvironmentCd constructor.
// Type:    Method.
// Args:    None.
// Return:  None.
// Throws:  None.
//--
CMICmdCmdEnvironmentCd::CMICmdCmdEnvironmentCd()
    : m_constStrArgNamePathDir("pathdir") {
  // Command factory matches this name with that received from the stdin stream
  m_strMiCmd = "environment-cd";

  // Required by the CMICmdFactory when registering *this command
  m_pSelfCreatorFn = &CMICmdCmdEnvironmentCd::CreateSelf;
}

//++
//------------------------------------------------------------------------------------
// Details: CMICmdCmdEnvironmentCd destructor.
// Type:    Overrideable.
// Args:    None.
// Return:  None.
// Throws:  None.
//--
CMICmdCmdEnvironmentCd::~CMICmdCmdEnvironmentCd() {}

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
bool CMICmdCmdEnvironmentCd::ParseArgs() {
  m_setCmdArgs.Add(new CMICmdArgValFile(m_constStrArgNamePathDir, true, true));
  CMICmdArgContext argCntxt(m_cmdData.strMiCmdOption);
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
bool CMICmdCmdEnvironmentCd::Execute() {
  CMICMDBASE_GETOPTION(pArgPathDir, File, m_constStrArgNamePathDir);
  const CMIUtilString &strWkDir(pArgPathDir->GetValue());
  CMICmnLLDBDebugger &rDbg(CMICmnLLDBDebugger::Instance());
  lldb::SBDebugger &rLldbDbg = rDbg.GetTheDebugger();
  bool bOk = rLldbDbg.SetCurrentPlatformSDKRoot(strWkDir.c_str());
  if (bOk) {
    const CMIUtilString &rStrKeyWkDir(
        m_rLLDBDebugSessionInfo.m_constStrSharedDataKeyWkDir);
    if (!m_rLLDBDebugSessionInfo.SharedDataAdd<CMIUtilString>(rStrKeyWkDir,
                                                              strWkDir)) {
      SetError(CMIUtilString::Format(MIRSRC(IDS_DBGSESSION_ERR_SHARED_DATA_ADD),
                                     m_cmdData.strMiCmd.c_str(),
                                     rStrKeyWkDir.c_str()));
      bOk = MIstatus::failure;
    }
  } else
    SetError(CMIUtilString::Format(MIRSRC(IDS_CMD_ERR_FNFAILED),
                                   m_cmdData.strMiCmd.c_str(),
                                   "SetCurrentPlatformSDKRoot()"));

  lldb::SBTarget sbTarget = m_rLLDBDebugSessionInfo.GetTarget();
  if (sbTarget.IsValid()) {
    lldb::SBLaunchInfo sbLaunchInfo = sbTarget.GetLaunchInfo();
    sbLaunchInfo.SetWorkingDirectory(strWkDir.c_str());
    sbTarget.SetLaunchInfo(sbLaunchInfo);
  }

  return bOk;
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
bool CMICmdCmdEnvironmentCd::Acknowledge() {
  const CMIUtilString &rStrKeyWkDir(
      m_rLLDBDebugSessionInfo.m_constStrSharedDataKeyWkDir);
  CMIUtilString strWkDir;
  const bool bOk = m_rLLDBDebugSessionInfo.SharedDataRetrieve<CMIUtilString>(
      rStrKeyWkDir, strWkDir);
  if (bOk) {
    const CMICmnMIValueConst miValueConst(strWkDir);
    const CMICmnMIValueResult miValueResult("path", miValueConst);
    const CMICmnMIResultRecord miRecordResult(
        m_cmdData.strMiCmdToken, CMICmnMIResultRecord::eResultClass_Done,
        miValueResult);
    m_miResultRecord = miRecordResult;
    return MIstatus::success;
  }

  SetError(CMIUtilString::Format(MIRSRC(IDS_CMD_ERR_SHARED_DATA_NOT_FOUND),
                                 m_cmdData.strMiCmd.c_str(),
                                 rStrKeyWkDir.c_str()));
  return MIstatus::failure;
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
CMICmdBase *CMICmdCmdEnvironmentCd::CreateSelf() {
  return new CMICmdCmdEnvironmentCd();
}
