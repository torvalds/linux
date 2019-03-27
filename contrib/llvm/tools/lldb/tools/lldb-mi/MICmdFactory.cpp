//===-- MICmdFactory.cpp ----------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

// In-house headers:
#include "MICmdFactory.h"
#include "MICmdBase.h"
#include "MICmdCommands.h"
#include "MICmdData.h"
#include "MICmnResources.h"

//++
//------------------------------------------------------------------------------------
// Details: CMICmdFactory constructor.
// Type:    Method.
// Args:    None.
// Return:  None.
// Throws:  None.
//--
CMICmdFactory::CMICmdFactory() {}

//++
//------------------------------------------------------------------------------------
// Details: CMICmdFactory destructor.
// Type:    Overridable.
// Args:    None.
// Return:  None.
// Throws:  None.
//--
CMICmdFactory::~CMICmdFactory() { Shutdown(); }

//++
//------------------------------------------------------------------------------------
// Details: Initialize resources for *this Command factory.
// Type:    Method.
// Args:    None.
// Return:  MIstatus::success - Functionality succeeded.
//          MIstatus::failure - Functionality failed.
// Throws:  None.
//--
bool CMICmdFactory::Initialize() {
  m_clientUsageRefCnt++;

  if (m_bInitialized)
    return MIstatus::success;

  m_bInitialized = true;

  MICmnCommands::RegisterAll();

  return MIstatus::success;
}

//++
//------------------------------------------------------------------------------------
// Details: Release resources for *this Command Factory.
// Type:    Method.
// Args:    None.
// Return:  MIstatus::success - Functionality succeeded.
//          MIstatus::failure - Functionality failed.
// Throws:  None.
//--
bool CMICmdFactory::Shutdown() {
  if (--m_clientUsageRefCnt > 0)
    return MIstatus::success;

  if (!m_bInitialized)
    return MIstatus::success;

  m_mapMiCmdToCmdCreatorFn.clear();

  m_bInitialized = false;

  return MIstatus::success;
}

//++
//------------------------------------------------------------------------------------
// Details: Register a command's creator function with the command identifier
// the MI
//          command name i.e. 'file-exec-and-symbols'.
// Type:    Method.
// Args:    vMiCmd          - (R) Command's name, the MI command.
//          vCmdCreateFn    - (R) Command's creator function pointer.
// Return:  MIstatus::success - Functionality succeeded.
//          MIstatus::failure - Functionality failed.
// Throws:  None.
//--
bool CMICmdFactory::CmdRegister(const CMIUtilString &vMiCmd,
                                CmdCreatorFnPtr vCmdCreateFn) {
  if (!IsValid(vMiCmd)) {
    SetErrorDescription(CMIUtilString::Format(
        MIRSRC(IDS_CMDFACTORY_ERR_INVALID_CMD_NAME), vMiCmd.c_str()));
    return MIstatus::failure;
  }
  if (vCmdCreateFn == nullptr) {
    SetErrorDescription(CMIUtilString::Format(
        MIRSRC(IDS_CMDFACTORY_ERR_INVALID_CMD_CR8FN), vMiCmd.c_str()));
    return MIstatus::failure;
  }

  if (HaveAlready(vMiCmd)) {
    SetErrorDescription(CMIUtilString::Format(
        MIRSRC(IDS_CMDFACTORY_ERR_CMD_ALREADY_REGED), vMiCmd.c_str()));
    return MIstatus::failure;
  }

  MapPairMiCmdToCmdCreatorFn_t pr(vMiCmd, vCmdCreateFn);
  m_mapMiCmdToCmdCreatorFn.insert(pr);

  return MIstatus::success;
}

//++
//------------------------------------------------------------------------------------
// Details: Check a command is already registered.
// Type:    Method.
// Args:    vMiCmd  - (R) Command's name, the MI command.
// Return:  True - registered.
//          False - not found.
// Throws:  None.
//--
bool CMICmdFactory::HaveAlready(const CMIUtilString &vMiCmd) const {
  const MapMiCmdToCmdCreatorFn_t::const_iterator it =
      m_mapMiCmdToCmdCreatorFn.find(vMiCmd);
  return it != m_mapMiCmdToCmdCreatorFn.end();
}

//++
//------------------------------------------------------------------------------------
// Details: Check a command's name is valid:
//              - name is not empty
//              - name does not have spaces
// Type:    Method.
// Args:    vMiCmd  - (R) Command's name, the MI command.
// Return:  True - valid.
//          False - not valid.
// Throws:  None.
//--
bool CMICmdFactory::IsValid(const CMIUtilString &vMiCmd) const {
  bool bValid = true;

  if (vMiCmd.empty()) {
    bValid = false;
    return false;
  }

  const size_t nPos = vMiCmd.find(' ');
  if (nPos != std::string::npos)
    bValid = false;

  return bValid;
}

//++
//------------------------------------------------------------------------------------
// Details: Check a command is already registered.
// Type:    Method.
// Args:    vMiCmd  - (R) Command's name, the MI command.
// Return:  True - registered.
//          False - not found.
// Throws:  None.
//--
bool CMICmdFactory::CmdExist(const CMIUtilString &vMiCmd) const {
  return HaveAlready(vMiCmd);
}

//++
//------------------------------------------------------------------------------------
// Details: Create a command given the specified MI command name. The command
// data object
//          contains the options for the command.
// Type:    Method.
// Args:    vMiCmd      - (R) Command's name, the MI command.
//          vCmdData    - (RW) Command's metadata status/information/result
//          object.
//          vpNewCmd    - (W) New command instance.
// Return:  MIstatus::success - Functionality succeeded.
//          MIstatus::failure - Functionality failed.
// Throws:  None.
//--
bool CMICmdFactory::CmdCreate(const CMIUtilString &vMiCmd,
                              const SMICmdData &vCmdData,
                              CMICmdBase *&vpNewCmd) {
  vpNewCmd = nullptr;

  if (!IsValid(vMiCmd)) {
    SetErrorDescription(CMIUtilString::Format(
        MIRSRC(IDS_CMDFACTORY_ERR_INVALID_CMD_NAME), vMiCmd.c_str()));
    return MIstatus::failure;
  }
  if (!HaveAlready(vMiCmd)) {
    SetErrorDescription(CMIUtilString::Format(
        MIRSRC(IDS_CMDFACTORY_ERR_CMD_NOT_REGISTERED), vMiCmd.c_str()));
    return MIstatus::failure;
  }

  const MapMiCmdToCmdCreatorFn_t::const_iterator it =
      m_mapMiCmdToCmdCreatorFn.find(vMiCmd);
  const CMIUtilString &rMiCmd((*it).first);
  MIunused(rMiCmd);
  CmdCreatorFnPtr pFn = (*it).second;
  CMICmdBase *pCmd = (*pFn)();

  SMICmdData cmdData(vCmdData);
  cmdData.id = pCmd->GetGUID();
  pCmd->SetCmdData(cmdData);
  vpNewCmd = pCmd;

  return MIstatus::success;
}
