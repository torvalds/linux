//===-- MICmdInvoker.cpp ----------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

// In-house headers:
#include "MICmdInvoker.h"
#include "MICmdBase.h"
#include "MICmdMgr.h"
#include "MICmnLog.h"
#include "MICmnStreamStdout.h"
#include "MIDriver.h"

//++
//------------------------------------------------------------------------------------
// Details: CMICmdInvoker constructor.
// Type:    Method.
// Args:    None.
// Return:  None.
// Throws:  None.
//--
CMICmdInvoker::CMICmdInvoker() : m_rStreamOut(CMICmnStreamStdout::Instance()) {}

//++
//------------------------------------------------------------------------------------
// Details: CMICmdInvoker destructor.
// Type:    Overridable.
// Args:    None.
// Return:  None.
// Throws:  None.
//--
CMICmdInvoker::~CMICmdInvoker() { Shutdown(); }

//++
//------------------------------------------------------------------------------------
// Details: Initialize resources for *this Command Invoker.
// Type:    Method.
// Args:    None.
// Return:  MIstatus::success - Functional succeeded.
//          MIstatus::failure - Functional failed.
// Throws:  None.
//--
bool CMICmdInvoker::Initialize() {
  m_clientUsageRefCnt++;

  if (m_bInitialized)
    return MIstatus::success;

  m_bInitialized = true;

  return MIstatus::success;
}

//++
//------------------------------------------------------------------------------------
// Details: Release resources for *this Stdin stream.
// Type:    Method.
// Args:    None.
// Return:  MIstatus::success - Functional succeeded.
//          MIstatus::failure - Functional failed.
// Throws:  None.
//--
bool CMICmdInvoker::Shutdown() {
  if (--m_clientUsageRefCnt > 0)
    return MIstatus::success;

  if (!m_bInitialized)
    return MIstatus::success;

  CmdDeleteAll();

  m_bInitialized = false;

  return MIstatus::success;
}

//++
//------------------------------------------------------------------------------------
// Details: Empty the map of invoked commands doing work. Command objects are
// deleted too.
// Type:    Method.
// Args:    None.
// Return:  None.
// Throws:  None.
//--
void CMICmdInvoker::CmdDeleteAll() {
  CMICmdMgr &rMgr = CMICmdMgr::Instance();
  MapCmdIdToCmd_t::const_iterator it = m_mapCmdIdToCmd.begin();
  while (it != m_mapCmdIdToCmd.end()) {
    const MIuint cmdId((*it).first);
    MIunused(cmdId);
    CMICmdBase *pCmd = (*it).second;
    const CMIUtilString &rCmdName(pCmd->GetCmdData().strMiCmd);
    MIunused(rCmdName);
    rMgr.CmdDelete(pCmd->GetCmdData());

    // Next
    ++it;
  }
  m_mapCmdIdToCmd.clear();
}

//++
//------------------------------------------------------------------------------------
// Details: Remove from the map of invoked commands doing work a command that
// has finished
//          its work. The command object is deleted too.
// Type:    Method.
// Args:    vId             - (R) Command object's unique ID.
//          vbYesDeleteCmd  - (R) True = Delete command object, false = delete
//          via the Command Manager.
// Return:  None.
// Throws:  None.
//--
bool CMICmdInvoker::CmdDelete(const MIuint vId,
                              const bool vbYesDeleteCmd /*= false*/) {
  CMICmdMgr &rMgr = CMICmdMgr::Instance();
  MapCmdIdToCmd_t::const_iterator it = m_mapCmdIdToCmd.find(vId);
  if (it != m_mapCmdIdToCmd.end()) {
    CMICmdBase *pCmd = (*it).second;
    if (vbYesDeleteCmd) {
      // Via registered interest command manager callback *this object to delete
      // the command
      m_mapCmdIdToCmd.erase(it);
      delete pCmd;
    } else
      // Notify other interested object of this command's pending deletion
      rMgr.CmdDelete(pCmd->GetCmdData());
  }

  if (m_mapCmdIdToCmd.empty())
    rMgr.CmdUnregisterForDeleteNotification(*this);

  return MIstatus::success;
}

//++
//------------------------------------------------------------------------------------
// Details: Add to the map of invoked commands doing work a command that is
// about to
//          start to do work.
// Type:    Method.
// Args:    vCmd    - (R) Command object.
// Return:  None.
// Throws:  None.
//--
bool CMICmdInvoker::CmdAdd(const CMICmdBase &vCmd) {
  if (m_mapCmdIdToCmd.empty()) {
    CMICmdMgr &rMgr = CMICmdMgr::Instance();
    rMgr.CmdRegisterForDeleteNotification(*this);
  }

  const MIuint &cmdId(vCmd.GetCmdData().id);
  MapCmdIdToCmd_t::const_iterator it = m_mapCmdIdToCmd.find(cmdId);
  if (it != m_mapCmdIdToCmd.end())
    return MIstatus::success;

  MapPairCmdIdToCmd_t pr(cmdId, const_cast<CMICmdBase *>(&vCmd));
  m_mapCmdIdToCmd.insert(pr);

  return MIstatus::success;
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
// Args:    vCmd    - (RW) Command object.
// Return:  MIstatus::success - Functionality succeeded.
//          MIstatus::failure - Functionality failed.
// Throws:  None.
//--
bool CMICmdInvoker::CmdExecute(CMICmdBase &vCmd) {
  bool bOk = CmdAdd(vCmd);

  if (bOk) {
    vCmd.AddCommonArgs();
    if (!vCmd.ParseArgs()) {
      // Report command execution failed
      const SMICmdData cmdData(vCmd.GetCmdData());
      CmdStdout(cmdData);
      CmdCauseAppExit(vCmd);
      CmdDelete(cmdData.id);

      // Proceed to wait or execute next command
      return MIstatus::success;
    }
  }

  if (bOk && !vCmd.Execute()) {
    // Report command execution failed
    const SMICmdData cmdData(vCmd.GetCmdData());
    CmdStdout(cmdData);
    CmdCauseAppExit(vCmd);
    CmdDelete(cmdData.id);

    // Proceed to wait or execute next command
    return MIstatus::success;
  }

  bOk = CmdExecuteFinished(vCmd);

  return bOk;
}

//++
//------------------------------------------------------------------------------------
// Details: Called when a command has finished its Execution() work either
// synchronously
//          because the command executed was the type a non event type or
//          asynchronously
//          via the command's callback (because of an SB Listener event). Needs
//          to be called
//          so that *this invoker call do some house keeping and then proceed to
//          call
//          the command's Acknowledge() function.
// Type:    Method.
// Args:    vCmd    - (R) Command object.
// Return:  MIstatus::success - Functionality succeeded.
//          MIstatus::failure - Functionality failed.
// Throws:  None.
//--
bool CMICmdInvoker::CmdExecuteFinished(CMICmdBase &vCmd) {
  // Command finished now get the command to gather it's information and form
  // the MI
  // Result record
  if (!vCmd.Acknowledge()) {
    // Report command acknowledge functionality failed
    const SMICmdData cmdData(vCmd.GetCmdData());
    CmdStdout(cmdData);
    CmdCauseAppExit(vCmd);
    CmdDelete(cmdData.id);

    // Proceed to wait or execute next command
    return MIstatus::success;
  }

  // Retrieve the command's latest data/information. Needed for commands of the
  // event type so have
  // a record of commands pending finishing execution.
  const CMIUtilString &rMIResultRecord(vCmd.GetMIResultRecord());
  SMICmdData cmdData(
      vCmd.GetCmdData()); // Make a copy as the command will be deleted soon
  cmdData.strMiCmdResultRecord = rMIResultRecord; // Precautionary copy as the
                                                  // command might forget to do
                                                  // this
  if (vCmd.HasMIResultRecordExtra()) {
    cmdData.bHasResultRecordExtra = true;
    const CMIUtilString &rMIExtra(vCmd.GetMIResultRecordExtra());
    cmdData.strMiCmdResultRecordExtra =
        rMIExtra; // Precautionary copy as the command might forget to do this
  }

  // Send command's MI response to the client
  bool bOk = CmdStdout(cmdData);

  // Delete the command object as do not require anymore
  bOk = bOk && CmdDelete(vCmd.GetCmdData().id);

  return bOk;
}

//++
//------------------------------------------------------------------------------------
// Details: If the MI Driver is not operating via a client i.e. Eclipse check
// the command
//          on failure suggests the application exits. A command can be such
//          that a
//          failure cannot the allow the application to continue operating.
// Args:    vCmd    - (R) Command object.
// Return:  None.
// Return:  None.
// Throws:  None.
//--
void CMICmdInvoker::CmdCauseAppExit(const CMICmdBase &vCmd) const {
  if (vCmd.GetExitAppOnCommandFailure()) {
    CMIDriver &rDriver(CMIDriver::Instance());
    if (rDriver.IsDriverDebuggingArgExecutable()) {
      rDriver.SetExitApplicationFlag(true);
    }
  }
}

//++
//------------------------------------------------------------------------------------
// Details: Write to stdout and the Log file the command's MI formatted result.
// Type:    vCmdData    - (R) A command's information.
// Return:  MIstatus::success - Functionality succeeded.
//          MIstatus::failure - Functionality failed.
// Return:  None.
// Throws:  None.
//--
bool CMICmdInvoker::CmdStdout(const SMICmdData &vCmdData) const {
  bool bOk = m_pLog->WriteLog(vCmdData.strMiCmdAll);
  const bool bLock = bOk && m_rStreamOut.Lock();
  bOk = bOk && bLock &&
        m_rStreamOut.WriteMIResponse(vCmdData.strMiCmdResultRecord);
  if (bOk && vCmdData.bHasResultRecordExtra) {
    bOk = m_rStreamOut.WriteMIResponse(vCmdData.strMiCmdResultRecordExtra);
  }
  bOk = bLock && m_rStreamOut.Unlock();

  return bOk;
}

//++
//------------------------------------------------------------------------------------
// Details: Required by the CMICmdMgr::ICmdDeleteCallback. *this object is
// registered
//          with the Command Manager to receive callbacks when a command is
//          being deleted.
//          An object, *this invoker, does not delete a command object itself
//          but calls
//          the Command Manager to delete a command object. This function is the
//          Invoker's
//          called.
//          The Invoker owns the command objects and so can delete them but must
//          do it
//          via the manager so other objects can be notified of the deletion.
// Type:    Method.
// Args:    vCmd    - (RW) Command.
// Return:  None.
// Throws:  None.
//--
void CMICmdInvoker::Delete(SMICmdData &vCmd) { CmdDelete(vCmd.id, true); }
