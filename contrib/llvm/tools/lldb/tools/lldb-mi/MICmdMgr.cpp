//===-- MICmdMgr.cpp --------------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

// In-house headers:
#include "MICmdMgr.h"
#include "MICmdBase.h"
#include "MICmdFactory.h"
#include "MICmdInterpreter.h"
#include "MICmdInvoker.h"
#include "MICmnLog.h"
#include "MICmnResources.h"
#include "MIUtilSingletonBase.h"
#include "MIUtilSingletonHelper.h"

//++
//------------------------------------------------------------------------------------
// Details: CMICmdMgr constructor.
// Type:    Method.
// Args:    None.
// Return:  None.
// Throws:  None.
//--
CMICmdMgr::CMICmdMgr()
    : m_interpretor(CMICmdInterpreter::Instance()),
      m_factory(CMICmdFactory::Instance()),
      m_invoker(CMICmdInvoker::Instance()) {}

//++
//------------------------------------------------------------------------------------
// Details: CMICmdMgr destructor.
// Type:    Overridable.
// Args:    None.
// Return:  None.
// Throws:  None.
//--
CMICmdMgr::~CMICmdMgr() { Shutdown(); }

//++
//------------------------------------------------------------------------------------
// Details: Initialize resources for *this Command Manager.
// Type:    Method.
// Args:    None.
// Return:  MIstatus::success - Functionality succeeded.
//          MIstatus::failure - Functionality failed.
// Throws:  None.
//--
bool CMICmdMgr::Initialize() {
  m_clientUsageRefCnt++;

  if (m_bInitialized)
    return MIstatus::success;

  bool bOk = MIstatus::success;
  CMIUtilString errMsg;

  // Note initialization order is important here as some resources depend on
  // previous
  MI::ModuleInit<CMICmnLog>(IDS_MI_INIT_ERR_LOG, bOk, errMsg);
  MI::ModuleInit<CMICmnResources>(IDS_MI_INIT_ERR_RESOURCES, bOk, errMsg);
  if (bOk && !m_interpretor.Initialize()) {
    bOk = false;
    errMsg = CMIUtilString::Format(MIRSRC(IDS_MI_INIT_ERR_CMDINTERPRETER),
                                   m_interpretor.GetErrorDescription().c_str());
  }
  if (bOk && !m_factory.Initialize()) {
    bOk = false;
    errMsg = CMIUtilString::Format(MIRSRC(IDS_MI_INIT_ERR_CMDFACTORY),
                                   m_factory.GetErrorDescription().c_str());
  }
  if (bOk && !m_invoker.Initialize()) {
    bOk = false;
    errMsg = CMIUtilString::Format(MIRSRC(IDS_MI_INIT_ERR_CMDINVOKER),
                                   m_invoker.GetErrorDescription().c_str());
  }
  m_bInitialized = bOk;

  if (!bOk) {
    CMIUtilString strInitError(
        CMIUtilString::Format(MIRSRC(IDS_MI_INIT_ERR_CMDMGR), errMsg.c_str()));
    SetErrorDescription(strInitError);
    return MIstatus::failure;
  }

  return MIstatus::success;
}

//++
//------------------------------------------------------------------------------------
// Details: Release resources for *this Command Manager.
// Type:    Method.
// Args:    None.
// Return:  MIstatus::success - Functionality succeeded.
//          MIstatus::failure - Functionality failed.
// Throws:  None.
//--
bool CMICmdMgr::Shutdown() {
  if (--m_clientUsageRefCnt > 0)
    return MIstatus::success;

  if (!m_bInitialized)
    return MIstatus::success;

  m_bInitialized = false;

  ClrErrorDescription();

  bool bOk = MIstatus::success;
  CMIUtilString errMsg;

  // Tidy up
  m_setCmdDeleteCallback.clear();

  // Note shutdown order is important here
  if (!m_invoker.Shutdown()) {
    bOk = false;
    errMsg += CMIUtilString::Format(MIRSRC(IDS_MI_SHTDWN_ERR_CMDINVOKER),
                                    m_invoker.GetErrorDescription().c_str());
  }
  if (!m_factory.Shutdown()) {
    bOk = false;
    if (!errMsg.empty())
      errMsg += ", ";
    errMsg += CMIUtilString::Format(MIRSRC(IDS_MI_SHTDWN_ERR_CMDFACTORY),
                                    m_factory.GetErrorDescription().c_str());
  }
  if (!m_interpretor.Shutdown()) {
    bOk = false;
    if (!errMsg.empty())
      errMsg += ", ";
    errMsg +=
        CMIUtilString::Format(MIRSRC(IDS_MI_SHTDWN_ERR_CMDINTERPRETER),
                              m_interpretor.GetErrorDescription().c_str());
  }
  MI::ModuleShutdown<CMICmnResources>(IDS_MI_INIT_ERR_RESOURCES, bOk, errMsg);
  MI::ModuleShutdown<CMICmnLog>(IDS_MI_INIT_ERR_LOG, bOk, errMsg);

  if (!bOk) {
    SetErrorDescriptionn(MIRSRC(IDS_MI_SHUTDOWN_ERR), errMsg.c_str());
  }

  return MIstatus::success;
}

//++
//------------------------------------------------------------------------------------
// Details: Establish whether the text data is an MI format type command.
// Type:    Method.
// Args:    vTextLine               - (R) Text data to interpret.
//          vwbYesValid             - (W) True = MI type command, false = not
//          recognised.
//          vwbCmdNotInCmdFactor    - (W) True = MI command not found in the
//          command factor, false = recognised.
// Return:  MIstatus::success - Functionality succeeded.
//          MIstatus::failure - Functionality failed.
// Throws:  None.
//--
bool CMICmdMgr::CmdInterpret(const CMIUtilString &vTextLine, bool &vwbYesValid,
                             bool &vwbCmdNotInCmdFactor,
                             SMICmdData &rwCmdData) {
  return m_interpretor.ValidateIsMi(vTextLine, vwbYesValid,
                                    vwbCmdNotInCmdFactor, rwCmdData);
}

//++
//------------------------------------------------------------------------------------
// Details: Having previously had the potential command validated and found
// valid now
//          get the command executed.
//          If the Functionality returns MIstatus::failure call
//          GetErrorDescription().
//          This function is used by the application's main thread.
// Type:    Method.
// Args:    vCmdData    - (RW) Command meta data.
// Return:  MIstatus::success - Functionality succeeded.
//          MIstatus::failure - Functionality failed.
// Throws:  None.
//--
bool CMICmdMgr::CmdExecute(const SMICmdData &vCmdData) {
  bool bOk = MIstatus::success;

  // Pass the command's meta data structure to the command
  // so it can update it if required. (Need to copy it out of the
  // command before the command is deleted)
  CMICmdBase *pCmd = nullptr;
  bOk = m_factory.CmdCreate(vCmdData.strMiCmd, vCmdData, pCmd);
  if (!bOk) {
    const CMIUtilString errMsg(
        CMIUtilString::Format(MIRSRC(IDS_CMDMGR_ERR_CMD_FAILED_CREATE),
                              m_factory.GetErrorDescription().c_str()));
    SetErrorDescription(errMsg);
    return MIstatus::failure;
  }

  bOk = m_invoker.CmdExecute(*pCmd);
  if (!bOk) {
    const CMIUtilString errMsg(
        CMIUtilString::Format(MIRSRC(IDS_CMDMGR_ERR_CMD_INVOKER),
                              m_invoker.GetErrorDescription().c_str()));
    SetErrorDescription(errMsg);
    return MIstatus::failure;
  }

  return bOk;
}

//++
//------------------------------------------------------------------------------------
// Details: Iterate all interested clients and tell them a command is being
// deleted.
// Type:    Method.
// Args:    vCmdData    - (RW) The command to be deleted.
// Return:  MIstatus::success - Functional succeeded.
//          MIstatus::failure - Functional failed.
// Throws:  None.
//--
bool CMICmdMgr::CmdDelete(SMICmdData vCmdData) {
  // Note vCmdData is a copy! The command holding its copy will be deleted soon
  // we still need to iterate callback clients after a command object is deleted

  m_setCmdDeleteCallback.Delete(vCmdData);

  return MIstatus::success;
}

//++
//------------------------------------------------------------------------------------
// Details: Register an object to be called when a command object is deleted.
// Type:    Method.
// Args:    vObject - (R) A new interested client.
// Return:  MIstatus::success - Functional succeeded.
//          MIstatus::failure - Functional failed.
// Throws:  None.
//--
bool CMICmdMgr::CmdRegisterForDeleteNotification(
    CMICmdMgrSetCmdDeleteCallback::ICallback &vObject) {
  return m_setCmdDeleteCallback.Register(vObject);
}

//++
//------------------------------------------------------------------------------------
// Details: Unregister an object from being called when a command object is
// deleted.
// Type:    Method.
// Args:    vObject - (R) The was interested client.
// Return:  MIstatus::success - Functional succeeded.
//          MIstatus::failure - Functional failed.
// Throws:  None.
//--
bool CMICmdMgr::CmdUnregisterForDeleteNotification(
    CMICmdMgrSetCmdDeleteCallback::ICallback &vObject) {
  return m_setCmdDeleteCallback.Unregister(vObject);
}
