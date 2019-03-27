//===-- MICmdCmdFile.cpp ----------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

// Overview:    CMICmdCmdFileExecAndSymbols     implementation.

// Third Party Headers:
#include "lldb/API/SBStream.h"

// In-house headers:
#include "MICmdArgValFile.h"
#include "MICmdArgValOptionLong.h"
#include "MICmdArgValOptionShort.h"
#include "MICmdArgValString.h"
#include "MICmdCmdFile.h"
#include "MICmnLLDBDebugSessionInfo.h"
#include "MICmnLLDBDebugger.h"
#include "MICmnMIResultRecord.h"
#include "MIUtilFileStd.h"

//++
//------------------------------------------------------------------------------------
// Details: CMICmdCmdFileExecAndSymbols constructor.
// Type:    Method.
// Args:    None.
// Return:  None.
// Throws:  None.
//--
CMICmdCmdFileExecAndSymbols::CMICmdCmdFileExecAndSymbols()
    : m_constStrArgNameFile("file"), m_constStrArgNamedPlatformName("p"),
      m_constStrArgNamedRemotePath("r") {
  // Command factory matches this name with that received from the stdin stream
  m_strMiCmd = "file-exec-and-symbols";

  // Required by the CMICmdFactory when registering *this command
  m_pSelfCreatorFn = &CMICmdCmdFileExecAndSymbols::CreateSelf;
}

//++
//------------------------------------------------------------------------------------
// Details: CMICmdCmdFileExecAndSymbols destructor.
// Type:    Overrideable.
// Args:    None.
// Return:  None.
// Throws:  None.
//--
CMICmdCmdFileExecAndSymbols::~CMICmdCmdFileExecAndSymbols() {}

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
bool CMICmdCmdFileExecAndSymbols::ParseArgs() {
  m_setCmdArgs.Add(new CMICmdArgValFile(m_constStrArgNameFile, true, true));
  m_setCmdArgs.Add(
      new CMICmdArgValOptionShort(m_constStrArgNamedPlatformName, false, true,
                                  CMICmdArgValListBase::eArgValType_String, 1));
  m_setCmdArgs.Add(new CMICmdArgValOptionShort(
      m_constStrArgNamedRemotePath, false, true,
      CMICmdArgValListBase::eArgValType_StringQuotedNumberPath, 1));
  return ParseValidateCmdOptions();
}

//++
//------------------------------------------------------------------------------------
// Details: The invoker requires this function. The command does work in this
// function.
//          The command is likely to communicate with the LLDB SBDebugger in
//          here.
//          Synopsis: -file-exec-and-symbols file
//          Ref:
//          http://sourceware.org/gdb/onlinedocs/gdb/GDB_002fMI-File-Commands.html#GDB_002fMI-File-Commands
// Type:    Overridden.
// Args:    None.
// Return:  MIstatus::success - Functional succeeded.
//          MIstatus::failure - Functional failed.
// Throws:  None.
//--
bool CMICmdCmdFileExecAndSymbols::Execute() {
  CMICMDBASE_GETOPTION(pArgNamedFile, File, m_constStrArgNameFile);
  CMICMDBASE_GETOPTION(pArgPlatformName, OptionShort,
                       m_constStrArgNamedPlatformName);
  CMICMDBASE_GETOPTION(pArgRemotePath, OptionShort,
                       m_constStrArgNamedRemotePath);
  CMICmdArgValFile *pArgFile = static_cast<CMICmdArgValFile *>(pArgNamedFile);
  const CMIUtilString &strExeFilePath(pArgFile->GetValue());
  bool bPlatformName = pArgPlatformName->GetFound();
  CMIUtilString platformName;
  if (bPlatformName) {
    pArgPlatformName->GetExpectedOption<CMICmdArgValString, CMIUtilString>(
        platformName);
  }
  CMICmnLLDBDebugSessionInfo &rSessionInfo(
      CMICmnLLDBDebugSessionInfo::Instance());
  lldb::SBDebugger &rDbgr = rSessionInfo.GetDebugger();
  lldb::SBError error;
  const char *pTargetTriple = nullptr; // Let LLDB discover the triple required
  const char *pTargetPlatformName = platformName.c_str();
  const bool bAddDepModules = false;
  lldb::SBTarget target =
      rDbgr.CreateTarget(strExeFilePath.c_str(), pTargetTriple,
                         pTargetPlatformName, bAddDepModules, error);
  CMIUtilString strWkDir;
  const CMIUtilString &rStrKeyWkDir(rSessionInfo.m_constStrSharedDataKeyWkDir);
  if (!rSessionInfo.SharedDataRetrieve<CMIUtilString>(rStrKeyWkDir, strWkDir)) {
    strWkDir = CMIUtilFileStd::StripOffFileName(strExeFilePath);
    if (!rSessionInfo.SharedDataAdd<CMIUtilString>(rStrKeyWkDir, strWkDir)) {
      SetError(CMIUtilString::Format(MIRSRC(IDS_DBGSESSION_ERR_SHARED_DATA_ADD),
                                     m_cmdData.strMiCmd.c_str(),
                                     rStrKeyWkDir.c_str()));
      return MIstatus::failure;
    }
  }
  if (!rDbgr.SetCurrentPlatformSDKRoot(strWkDir.c_str())) {

    SetError(CMIUtilString::Format(MIRSRC(IDS_CMD_ERR_FNFAILED),
                                   m_cmdData.strMiCmd.c_str(),
                                   "SetCurrentPlatformSDKRoot()"));
    return MIstatus::failure;
  }
  if (pArgRemotePath->GetFound()) {
    CMIUtilString remotePath;
    pArgRemotePath->GetExpectedOption<CMICmdArgValString, CMIUtilString>(
        remotePath);
    lldb::SBModule module = target.FindModule(target.GetExecutable());
    if (module.IsValid()) {
      module.SetPlatformFileSpec(lldb::SBFileSpec(remotePath.c_str()));
    }
  }
  lldb::SBStream err;
  if (error.Fail()) {
    const bool bOk = error.GetDescription(err);
    MIunused(bOk);
  }
  if (!target.IsValid()) {
    SetError(CMIUtilString::Format(MIRSRC(IDS_CMD_ERR_INVALID_TARGET),
                                   m_cmdData.strMiCmd.c_str(),
                                   strExeFilePath.c_str(), err.GetData()));
    return MIstatus::failure;
  }
  if (error.Fail()) {
    SetError(CMIUtilString::Format(MIRSRC(IDS_CMD_ERR_CREATE_TARGET),
                                   m_cmdData.strMiCmd.c_str(), err.GetData()));
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
bool CMICmdCmdFileExecAndSymbols::Acknowledge() {
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
CMICmdBase *CMICmdCmdFileExecAndSymbols::CreateSelf() {
  return new CMICmdCmdFileExecAndSymbols();
}

//++
//------------------------------------------------------------------------------------
// Details: If the MI Driver is not operating via a client i.e. Eclipse but say
// operating
//          on a executable passed in as a argument to the drive then what
//          should the driver
//          do on a command failing? Either continue operating or exit the
//          application.
//          Override this function where a command failure cannot allow the
//          driver to
//          continue operating.
// Type:    Overridden.
// Args:    None.
// Return:  bool - True = Fatal if command fails, false = can continue if
// command fails.
// Throws:  None.
//--
bool CMICmdCmdFileExecAndSymbols::GetExitAppOnCommandFailure() const {
  return true;
}
