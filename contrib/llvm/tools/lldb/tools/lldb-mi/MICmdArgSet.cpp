//===-- MICmdArgSet.cpp -----------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

// In-house headers:
#include "MICmdArgSet.h"
#include "MICmdArgValBase.h"
#include "MICmnLog.h"
#include "MICmnResources.h"

//++
//------------------------------------------------------------------------------------
// Details: CMICmdArgSet constructor.
// Type:    Method.
// Args:    None.
// Return:  None.
// Throws:  None.
//--
CMICmdArgSet::CMICmdArgSet()
    : m_bIsArgsPresentButNotHandledByCmd(false), m_constStrCommaSpc(", ") {}

//++
//------------------------------------------------------------------------------------
// Details: CMICmdArgSet destructor.
// Type:    Method.
// Args:    None.
// Return:  None.
// Throws:  None.
//--
CMICmdArgSet::~CMICmdArgSet() {
  // Tidy up
  Destroy();
}

//++
//------------------------------------------------------------------------------------
// Details: Release resources used by *this container object.
// Type:    Method.
// Args:    None.
// Return:  None.
// Throws:  None.
//--
void CMICmdArgSet::Destroy() {
  // Delete command argument objects
  if (!m_setCmdArgs.empty()) {
    SetCmdArgs_t::iterator it = m_setCmdArgs.begin();
    while (it != m_setCmdArgs.end()) {
      CMICmdArgValBase *pArg(*it);
      delete pArg;

      // Next
      ++it;
    }
    m_setCmdArgs.clear();
  }

  m_setCmdArgsThatNotValid.clear();
  m_setCmdArgsThatAreMissing.clear();
  m_setCmdArgsNotHandledByCmd.clear();
  m_setCmdArgsMissingInfo.clear();
  m_bIsArgsPresentButNotHandledByCmd = false;
}

//++
//------------------------------------------------------------------------------------
// Details: Retrieve the state flag indicating that the command set up ready to
// parse
//          command arguments or options found that one or more arguments was
//          indeed
//          present but not handled. This is given as a warning in the MI log
//          file.
// Type:    Method.
// Args:    None.
// Return:  bool - True = one or more args not handled, false = all args handled
// Throws:  None.
//--
bool CMICmdArgSet::IsArgsPresentButNotHandledByCmd() const {
  return m_bIsArgsPresentButNotHandledByCmd;
}

//++
//------------------------------------------------------------------------------------
// Details: Add the list of command's arguments to parse and validate another
// one.
// Type:    Method.
// Args:    vArg    - (R) A command argument object.
// Return:  None.
// Throws:  None.
//--
void CMICmdArgSet::Add(CMICmdArgValBase *vArg) { m_setCmdArgs.push_back(vArg); }

//++
//------------------------------------------------------------------------------------
// Details: After validating an options line of text (the context) and there is
// a failure,
//          it is likely a mandatory command argument that is required is
//          missing. This
//          function returns the argument that should be present.
// Type:    Method.
// Args:    None.
// Return:  SetCmdArgs_t & - Set of argument objects.
// Throws:  None.
//--
const CMICmdArgSet::SetCmdArgs_t &CMICmdArgSet::GetArgsThatAreMissing() const {
  return m_setCmdArgsThatAreMissing;
}

//++
//------------------------------------------------------------------------------------
// Details: After validating an options line of text (the context) and there is
// a failure,
//          it may be because one or more arguments were unable to extract a
//          value. This
//          function returns the argument that were found to be invalid.
// Type:    Method.
// Args:    None.
// Return:  SetCmdArgs_t & - Set of argument objects.
// Throws:  None.
//--
const CMICmdArgSet::SetCmdArgs_t &CMICmdArgSet::GetArgsThatInvalid() const {
  return m_setCmdArgsThatNotValid;
}

//++
//------------------------------------------------------------------------------------
// Details: The list of argument or option (objects) that were specified by the
// command
//          and so recognised when parsed but were not handled. Ideally the
//          command
//          should handle all arguments and options presented to it. The command
//          sends
//          warning to the MI log file to say that these options were not
//          handled.
//          Used as one way to determine option that maybe should really be
//          implemented
//          and not just ignored.
// Type:    Method.
// Args:    None.
// Return:  SetCmdArgs_t & - Set of argument objects.
// Throws:  None.
//--
const CMICmdArgSet::SetCmdArgs_t &CMICmdArgSet::GetArgsNotHandledByCmd() const {
  return m_setCmdArgsNotHandledByCmd;
}

//++
//------------------------------------------------------------------------------------
// Details: Given a set of command argument objects parse the context option
// string to
//          find those argument and retrieve their value. If the function fails
//          call
//          GetArgsThatAreMissing() to see which commands that were mandatory
//          were
//          missing or failed to parse.
// Type:    Method.
// Args:    vStrMiCmd       - (R)  Command's name.
//          vCmdArgsText    - (RW) A command's options or argument.
// Return:  MIstatus::success - Functional succeeded.
//          MIstatus::failure - Functional failed.
// Throws:  None.
//--
bool CMICmdArgSet::Validate(const CMIUtilString &vStrMiCmd,
                            CMICmdArgContext &vwCmdArgsText) {
  m_cmdArgContext = vwCmdArgsText;

  // Iterate all the arguments or options required by a command
  SetCmdArgs_t::const_iterator it = m_setCmdArgs.begin();
  while (it != m_setCmdArgs.end()) {
    CMICmdArgValBase *pArg = *it;

    if (!pArg->Validate(vwCmdArgsText)) {
      if (pArg->GetFound()) {
        if (pArg->GetIsMissingOptions())
          m_setCmdArgsMissingInfo.push_back(pArg);
        else if (!pArg->GetValid())
          m_setCmdArgsThatNotValid.push_back(pArg);
      } else if (pArg->GetIsMandatory())
        m_setCmdArgsThatAreMissing.push_back(pArg);
    }

    if (pArg->GetFound() && !pArg->GetIsHandledByCmd()) {
      m_bIsArgsPresentButNotHandledByCmd = true;
      m_setCmdArgsNotHandledByCmd.push_back(pArg);
    }

    // Next
    ++it;
  }

  // report any issues with arguments/options
  if (IsArgsPresentButNotHandledByCmd())
    WarningArgsNotHandledbyCmdLogFile(vStrMiCmd);

  return ValidationFormErrorMessages(vwCmdArgsText);
}

//++
//------------------------------------------------------------------------------------
// Details: Having validated the command's options text and failed for some
// reason form
//          the error message made up with the faults found.
// Type:    Method.
//          vCmdArgsText    - (RW) A command's options or argument.
// Return:  MIstatus::success - Functional succeeded.
//          MIstatus::failure - Functional failed.
// Throws:  None.
//--
bool CMICmdArgSet::ValidationFormErrorMessages(
    const CMICmdArgContext &vwCmdArgsText) {
  CMIUtilString strListMissing;
  CMIUtilString strListInvalid;
  CMIUtilString strListMissingInfo;
  const bool bArgsMissing = (m_setCmdArgsThatAreMissing.size() > 0);
  const bool bArgsInvalid = (m_setCmdArgsThatNotValid.size() > 0);
  const bool bArgsMissingInfo = (m_setCmdArgsMissingInfo.size() > 0);
  if (!(bArgsMissing || bArgsInvalid || bArgsMissingInfo))
    return MIstatus::success;
  if (bArgsMissing) {
    MIuint i = 0;
    SetCmdArgs_t::const_iterator it = m_setCmdArgsThatAreMissing.begin();
    while (it != m_setCmdArgsThatAreMissing.end()) {
      if (i++ > 0)
        strListMissing += m_constStrCommaSpc;

      const CMICmdArgValBase *pArg(*it);
      strListMissing += pArg->GetName();

      // Next
      ++it;
    }
  }
  if (bArgsInvalid) {
    MIuint i = 0;
    SetCmdArgs_t::const_iterator it = m_setCmdArgsThatNotValid.begin();
    while (it != m_setCmdArgsThatNotValid.end()) {
      if (i++ > 0)
        strListMissing += m_constStrCommaSpc;

      const CMICmdArgValBase *pArg(*it);
      strListInvalid += pArg->GetName();

      // Next
      ++it;
    }
  }
  if (bArgsMissingInfo) {
    MIuint i = 0;
    SetCmdArgs_t::const_iterator it = m_setCmdArgsMissingInfo.begin();
    while (it != m_setCmdArgsMissingInfo.end()) {
      if (i++ > 0)
        strListMissingInfo += m_constStrCommaSpc;

      const CMICmdArgValBase *pArg(*it);
      strListMissingInfo += pArg->GetName();

      // Next
      ++it;
    }
  }

  bool bHaveOneError = false;
  CMIUtilString strError = MIRSRC(IDS_CMD_ARGS_ERR_PREFIX_MSG);
  if (bArgsMissing && bArgsInvalid) {
    bHaveOneError = true;
    strError +=
        CMIUtilString::Format(MIRSRC(IDS_CMD_ARGS_ERR_VALIDATION_MAN_INVALID),
                              strListMissing.c_str(), strListInvalid.c_str());
  }
  if (bArgsMissing) {
    if (bHaveOneError)
      strError += ". ";
    bHaveOneError = true;
    strError += CMIUtilString::Format(
        MIRSRC(IDS_CMD_ARGS_ERR_VALIDATION_MANDATORY), strListMissing.c_str());
  }
  if (bArgsMissingInfo) {
    if (bHaveOneError)
      strError += ". ";
    bHaveOneError = true;
    strError +=
        CMIUtilString::Format(MIRSRC(IDS_CMD_ARGS_ERR_VALIDATION_MISSING_INF),
                              strListMissingInfo.c_str());
  }
  if (bArgsInvalid) {
    if (bHaveOneError)
      strError += ". ";
    bHaveOneError = true;
    strError += CMIUtilString::Format(
        MIRSRC(IDS_CMD_ARGS_ERR_VALIDATION_INVALID), strListInvalid.c_str());
  }
  if (!vwCmdArgsText.IsEmpty()) {
    if (bHaveOneError)
      strError += ". ";
    bHaveOneError = true;
    strError +=
        CMIUtilString::Format(MIRSRC(IDS_CMD_ARGS_ERR_CONTEXT_NOT_ALL_EATTEN),
                              vwCmdArgsText.GetArgsLeftToParse().c_str());
  }

  if (bHaveOneError) {
    SetErrorDescription(strError);
    return MIstatus::failure;
  }

  return MIstatus::success;
}

//++
//------------------------------------------------------------------------------------
// Details: Ask if the command's argument options text had any arguments.
// Type:    Method.
// Args:    None.
// Return:  bool    - True = Has one or more arguments present, false = no
// arguments.
// Throws:  None.
//--
bool CMICmdArgSet::IsArgContextEmpty() const {
  return m_cmdArgContext.IsEmpty();
}

//++
//------------------------------------------------------------------------------------
// Details: Retrieve the number of arguments that are being used for the
// command.
// Type:    Method.
// Args:    None.
// Return:  size_t - Argument count.
// Throws:  None.
//--
size_t CMICmdArgSet::GetCount() const { return m_setCmdArgs.size(); }

//++
//------------------------------------------------------------------------------------
// Details: Given a set of command argument objects retrieve the argument with
// the
//          specified name.
// Type:    Method.
// Args:    vpArg   - (W) A pointer to a command's argument object.
// Return:  True - Argument found.
//          False - Argument not found.
// Throws:  None.
//--
bool CMICmdArgSet::GetArg(const CMIUtilString &vArgName,
                          CMICmdArgValBase *&vpArg) const {
  bool bFound = false;
  SetCmdArgs_t::const_iterator it = m_setCmdArgs.begin();
  while (it != m_setCmdArgs.end()) {
    CMICmdArgValBase *pArg(*it);
    if (pArg->GetName() == vArgName) {
      bFound = true;
      vpArg = pArg;
      break;
    }

    // Next
    ++it;
  }

  return bFound;
}

//++
//------------------------------------------------------------------------------------
// Details: Write a warning message to the MI Log file about the command's
// arguments or
//          options that were found present but not handled.
// Type:    Method.
// Args:    vrCmdName   - (R) The command's name.
// Return:  None.
// Throws:  None.
//--
void CMICmdArgSet::WarningArgsNotHandledbyCmdLogFile(
    const CMIUtilString &vrCmdName) {
#if MICONFIG_GIVE_WARNING_CMD_ARGS_NOT_HANDLED

  CMIUtilString strArgsNotHandled;
  const CMICmdArgSet::SetCmdArgs_t &rSetArgs = GetArgsNotHandledByCmd();
  MIuint nCnt = 0;
  CMICmdArgSet::SetCmdArgs_t::const_iterator it = rSetArgs.begin();
  while (it != rSetArgs.end()) {
    if (nCnt++ > 0)
      strArgsNotHandled += m_constStrCommaSpc;
    const CMICmdArgValBase *pArg = *it;
    strArgsNotHandled += pArg->GetName();

    // Next
    ++it;
  }

  const CMIUtilString strWarningMsg(
      CMIUtilString::Format(MIRSRC(IDS_CMD_WRN_ARGS_NOT_HANDLED),
                            vrCmdName.c_str(), strArgsNotHandled.c_str()));
  m_pLog->WriteLog(strWarningMsg);

#endif // MICONFIG_GIVE_WARNING_CMD_ARGS_NOT_HANDLED
}
