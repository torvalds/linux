//===-- MICmdInterpreter.cpp ------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

// In-house headers:
#include "MICmdInterpreter.h"
#include "MICmdFactory.h"

//++
//------------------------------------------------------------------------------------
// Details: CMICmdInterpreter constructor.
// Type:    Method.
// Args:    None.
// Return:  None.
// Throws:  None.
//--
CMICmdInterpreter::CMICmdInterpreter()
    : m_rCmdFactory(CMICmdFactory::Instance()) {}

//++
//------------------------------------------------------------------------------------
// Details: CMICmdInterpreter destructor.
// Type:    Overridable.
// Args:    None.
// Return:  None.
// Throws:  None.
//--
CMICmdInterpreter::~CMICmdInterpreter() { Shutdown(); }

//++
//------------------------------------------------------------------------------------
// Details: Initialize resources for *this Command Interpreter.
// Type:    Method.
// Args:    None.
// Return:  MIstatus::success - Functional succeeded.
//          MIstatus::failure - Functional failed.
// Throws:  None.
//--
bool CMICmdInterpreter::Initialize() {
  m_clientUsageRefCnt++;

  if (m_bInitialized)
    return MIstatus::success;

  m_bInitialized = true;

  return MIstatus::success;
}

//++
//------------------------------------------------------------------------------------
// Details: Release resources for *this Command Interpreter.
// Type:    Method.
// Args:    None.
// Return:  MIstatus::success - Functional succeeded.
//          MIstatus::failure - Functional failed.
// Throws:  None.
//--
bool CMICmdInterpreter::Shutdown() {
  if (--m_clientUsageRefCnt > 0)
    return MIstatus::success;

  if (!m_bInitialized)
    return MIstatus::success;

  m_bInitialized = false;

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
//          command factory, false = recognised.
// Return:  MIstatus::success - Functional succeeded.
//          MIstatus::failure - Functional failed.
// Throws:  None.
//--
bool CMICmdInterpreter::ValidateIsMi(const CMIUtilString &vTextLine,
                                     bool &vwbYesValid,
                                     bool &vwbCmdNotInCmdFactor,
                                     SMICmdData &rwCmdData) {
  vwbYesValid = false;
  vwbCmdNotInCmdFactor = false;
  rwCmdData.Clear();

  if (vTextLine.empty())
    return MIstatus::success;

  // MI format is [cmd #]-[command name]<space>[command arg(s)]
  // i.e. 1-file-exec-and-symbols --thread-group i1 DEVICE_EXECUTABLE
  //      5-data-evaluate-expression --thread 1 --frame 0 *(argv)

  m_miCmdData.Clear();
  m_miCmdData.strMiCmd = vTextLine;

  // The following change m_miCmdData as valid parts are identified
  vwbYesValid = (MiHasCmdTokenEndingHyphen(vTextLine) ||
                 MiHasCmdTokenEndingAlpha(vTextLine));
  vwbYesValid = vwbYesValid && MiHasCmd(vTextLine);
  if (vwbYesValid) {
    vwbCmdNotInCmdFactor = !HasCmdFactoryGotMiCmd(MiGetCmdData());
    vwbYesValid = !vwbCmdNotInCmdFactor;
  }

  // Update command's meta data valid state
  m_miCmdData.bCmdValid = vwbYesValid;

  // Ok to return new updated command information
  rwCmdData = MiGetCmdData();

  return MIstatus::success;
}

//++
//------------------------------------------------------------------------------------
// Details: Establish whether the command name entered on the stdin stream is
// recognised by
//          the MI driver.
// Type:    Method.
// Args:    vCmd    - (R) Command information structure.
// Return:  bool  - True = yes command is recognised, false = command not
// recognised.
// Throws:  None.
//--
bool CMICmdInterpreter::HasCmdFactoryGotMiCmd(const SMICmdData &vCmd) const {
  return m_rCmdFactory.CmdExist(vCmd.strMiCmd);
}

//++
//------------------------------------------------------------------------------------
// Details: Does the command entered match the criteria for a MI command format.
//          The format to validate against is 'nn-' where there can be 1 to n
//          digits.
//          I.e. '2-gdb-exit'.
//          Is the execution token present? The command token is entered into
//          the
//          command meta data structure whether correct or not for reporting or
//          later
//          command execution purposes.
// Type:    Method.
// Args:    vTextLine   - (R) Text data to interpret.
// Return:  bool  - True = yes command token present, false = command not
// recognised.
// Throws:  None.
//--
bool CMICmdInterpreter::MiHasCmdTokenEndingHyphen(
    const CMIUtilString &vTextLine) {
  // The hyphen is mandatory
  const size_t nPos = vTextLine.find('-', 0);
  if ((nPos == std::string::npos))
    return false;

  if (MiHasCmdTokenPresent(vTextLine)) {
    const std::string strNum = vTextLine.substr(0, nPos);
    if (!CMIUtilString(strNum).IsNumber())
      return false;

    m_miCmdData.strMiCmdToken = strNum;
  }

  m_miCmdData.bMIOldStyle = false;

  return true;
}

//++
//------------------------------------------------------------------------------------
// Details: Does the command entered match the criteria for a MI command format.
//          The format to validate against is 'nnA' where there can be 1 to n
//          digits.
//          'A' represents any non numeric token. I.e. '1source .gdbinit'.
//          Is the execution token present? The command token is entered into
//          the
//          command meta data structure whether correct or not for reporting or
//          later
//          command execution purposes.
// Type:    Method.
// Args:    vTextLine   - (R) Text data to interpret.
// Return:  bool  - True = yes command token present, false = command not
// recognised.
// Throws:  None.
//--
bool CMICmdInterpreter::MiHasCmdTokenEndingAlpha(
    const CMIUtilString &vTextLine) {
  char cChar = vTextLine[0];
  MIuint i = 0;
  while (::isdigit(cChar) != 0) {
    cChar = vTextLine[++i];
  }
  if (::isalpha(cChar) == 0)
    return false;
  if (i == 0)
    return false;

  const std::string strNum = vTextLine.substr(0, i);
  m_miCmdData.strMiCmdToken = strNum.c_str();
  m_miCmdData.bMIOldStyle = true;

  return true;
}

//++
//------------------------------------------------------------------------------------
// Details: Does the command entered match the criteria for a MI command format.
//          Is the command token present before the hyphen?
// Type:    Method.
// Args:    vTextLine - (R) Text data to interpret.
// Return:  bool  - True = yes command token present, false = token not present.
// Throws:  None.
//--
bool CMICmdInterpreter::MiHasCmdTokenPresent(const CMIUtilString &vTextLine) {
  const size_t nPos = vTextLine.find('-', 0);
  return (nPos > 0);
}

//++
//------------------------------------------------------------------------------------
// Details: Does the command name entered match the criteria for a MI command
// format.
//          Is a recognised command present? The command name is entered into
//          the
//          command meta data structure whether correct or not for reporting or
//          later
//          command execution purposes. Command options is present are also put
//          into the
//          command meta data structure.
// Type:    Method.
// Args:    vTextLine   - (R) Command information structure.
// Return:  bool  - True = yes command name present, false = command not
// recognised.
// Throws:  None.
//--
bool CMICmdInterpreter::MiHasCmd(const CMIUtilString &vTextLine) {
  size_t nPos = 0;
  if (m_miCmdData.bMIOldStyle) {
    char cChar = vTextLine[0];
    size_t i = 0;
    while (::isdigit(cChar) != 0) {
      cChar = vTextLine[++i];
    }
    nPos = --i;
  } else {
    nPos = vTextLine.find('-', 0);
  }

  bool bFoundCmd = false;
  const size_t nLen = vTextLine.length();
  const size_t nPos2 = vTextLine.find(' ', nPos);
  if (nPos2 != std::string::npos) {
    if (nPos2 == nLen)
      return false;
    const CMIUtilString cmd =
        CMIUtilString(vTextLine.substr(nPos + 1, nPos2 - nPos - 1));
    if (cmd.empty())
      return false;

    m_miCmdData.strMiCmd = cmd;

    if (nPos2 < nLen)
      m_miCmdData.strMiCmdOption =
          CMIUtilString(vTextLine.substr(nPos2 + 1, nLen - nPos2 - 1));

    bFoundCmd = true;
  } else {
    const CMIUtilString cmd =
        CMIUtilString(vTextLine.substr(nPos + 1, nLen - nPos - 1));
    if (cmd.empty())
      return false;
    m_miCmdData.strMiCmd = cmd;
    bFoundCmd = true;
  }

  if (bFoundCmd)
    m_miCmdData.strMiCmdAll = vTextLine;

  return bFoundCmd;
}

//++
//------------------------------------------------------------------------------------
// Details: Retrieve the just entered new command from stdin. It contains the
// command
//          name, number and any options.
// Type:    Method.
// Args:    vTextLine   - (R) Command information structure.
// Return:  SMICmdData & - Command meta data information/result/status.
// Throws:  None.
//--
const SMICmdData &CMICmdInterpreter::MiGetCmdData() const {
  return m_miCmdData;
}
