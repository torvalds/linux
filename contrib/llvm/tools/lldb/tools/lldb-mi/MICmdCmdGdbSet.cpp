//===-- MICmdCmdGdbSet.cpp --------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

// Overview:    CMICmdCmdGdbSet implementation.

// In-house headers:
#include "MICmdCmdGdbSet.h"
#include "MICmdArgValListOfN.h"
#include "MICmdArgValOptionLong.h"
#include "MICmdArgValString.h"
#include "MICmnLLDBDebugSessionInfo.h"
#include "MICmnMIResultRecord.h"
#include "MICmnMIValueConst.h"

// Instantiations:
const CMICmdCmdGdbSet::MapGdbOptionNameToFnGdbOptionPtr_t
    CMICmdCmdGdbSet::ms_mapGdbOptionNameToFnGdbOptionPtr = {
        {"target-async", &CMICmdCmdGdbSet::OptionFnTargetAsync},
        {"print", &CMICmdCmdGdbSet::OptionFnPrint},
        // { "auto-solib-add", &CMICmdCmdGdbSet::OptionFnAutoSolibAdd },    //
        // Example code if need to implement GDB set other options
        {"output-radix", &CMICmdCmdGdbSet::OptionFnOutputRadix},
        {"solib-search-path", &CMICmdCmdGdbSet::OptionFnSolibSearchPath},
        {"disassembly-flavor", &CMICmdCmdGdbSet::OptionFnDisassemblyFlavor},
        {"fallback", &CMICmdCmdGdbSet::OptionFnFallback},
        {"breakpoint", &CMICmdCmdGdbSet::OptionFnBreakpoint}};

//++
//------------------------------------------------------------------------------------
// Details: CMICmdCmdGdbSet constructor.
// Type:    Method.
// Args:    None.
// Return:  None.
// Throws:  None.
//--
CMICmdCmdGdbSet::CMICmdCmdGdbSet()
    : m_constStrArgNamedGdbOption("option"), m_bGdbOptionRecognised(true),
      m_bGdbOptionFnSuccessful(false), m_bGbbOptionFnHasError(false),
      m_strGdbOptionFnError(MIRSRC(IDS_WORD_ERR_MSG_NOT_IMPLEMENTED_BRKTS)) {
  // Command factory matches this name with that received from the stdin stream
  m_strMiCmd = "gdb-set";

  // Required by the CMICmdFactory when registering *this command
  m_pSelfCreatorFn = &CMICmdCmdGdbSet::CreateSelf;
}

//++
//------------------------------------------------------------------------------------
// Details: CMICmdCmdGdbSet destructor.
// Type:    Overrideable.
// Args:    None.
// Return:  None.
// Throws:  None.
//--
CMICmdCmdGdbSet::~CMICmdCmdGdbSet() {}

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
bool CMICmdCmdGdbSet::ParseArgs() {
  m_setCmdArgs.Add(new CMICmdArgValListOfN(
      m_constStrArgNamedGdbOption, true, true,
      CMICmdArgValListBase::eArgValType_StringAnything));
  return ParseValidateCmdOptions();
}

//++
//------------------------------------------------------------------------------------
// Details: The invoker requires this function. The command is executed in this
// function.
//          The command is likely to communicate with the LLDB SBDebugger in
//          here.
// Type:    Overridden.
// Args:    None.
// Return:  MIstatus::success - Functional succeeded.
//          MIstatus::failure - Functional failed.
// Throws:  None.
//--
bool CMICmdCmdGdbSet::Execute() {
  CMICMDBASE_GETOPTION(pArgGdbOption, ListOfN, m_constStrArgNamedGdbOption);
  const CMICmdArgValListBase::VecArgObjPtr_t &rVecWords(
      pArgGdbOption->GetExpectedOptions());

  // Get the gdb-set option to carry out. This option will be used as an action
  // which should be done. Further arguments will be used as parameters for it.
  CMICmdArgValListBase::VecArgObjPtr_t::const_iterator it = rVecWords.begin();
  const CMICmdArgValString *pOption =
      static_cast<const CMICmdArgValString *>(*it);
  const CMIUtilString strOption(pOption->GetValue());
  ++it;

  // Retrieve the parameter(s) for the option
  CMIUtilString::VecString_t vecWords;
  while (it != rVecWords.end()) {
    const CMICmdArgValString *pWord =
        static_cast<const CMICmdArgValString *>(*it);
    vecWords.push_back(pWord->GetValue());

    // Next
    ++it;
  }

  FnGdbOptionPtr pPrintRequestFn = nullptr;
  if (!GetOptionFn(strOption, pPrintRequestFn)) {
    // For unimplemented option handlers, fallback on a generic handler
    // ToDo: Remove this when ALL options have been implemented
    if (!GetOptionFn("fallback", pPrintRequestFn)) {
      m_bGdbOptionRecognised = false;
      m_strGdbOptionName = "fallback"; // This would be the strOption name
      return MIstatus::success;
    }
  }

  m_bGdbOptionFnSuccessful = (this->*(pPrintRequestFn))(vecWords);
  if (!m_bGdbOptionFnSuccessful && !m_bGbbOptionFnHasError)
    return MIstatus::failure;

  return MIstatus::success;
}

//++
//------------------------------------------------------------------------------------
// Details: The invoker requires this function. The command prepares a MI Record
// Result
//          for the work carried out in the Execute() method.
// Type:    Overridden.
// Args:    None.
// Return:  MIstatus::success - Functional succeeded.
//          MIstatus::failure - Functional failed.
// Throws:  None.
//--
bool CMICmdCmdGdbSet::Acknowledge() {
  // Print error if option isn't recognized:
  // ^error,msg="The request '%s' was not recognized, not implemented"
  if (!m_bGdbOptionRecognised) {
    const CMICmnMIValueConst miValueConst(
        CMIUtilString::Format(MIRSRC(IDS_CMD_ERR_INFO_PRINTFN_NOT_FOUND),
                              m_strGdbOptionName.c_str()));
    const CMICmnMIValueResult miValueResult("msg", miValueConst);
    const CMICmnMIResultRecord miRecordResult(
        m_cmdData.strMiCmdToken, CMICmnMIResultRecord::eResultClass_Error,
        miValueResult);
    m_miResultRecord = miRecordResult;
    return MIstatus::success;
  }

  // ^done,value="%s"
  if (m_bGdbOptionFnSuccessful) {
    const CMICmnMIResultRecord miRecordResult(
        m_cmdData.strMiCmdToken, CMICmnMIResultRecord::eResultClass_Done);
    m_miResultRecord = miRecordResult;
    return MIstatus::success;
  }

  // Print error if request failed:
  // ^error,msg="The request '%s' failed.
  const CMICmnMIValueConst miValueConst(CMIUtilString::Format(
      MIRSRC(IDS_CMD_ERR_INFO_PRINTFN_FAILED), m_strGdbOptionFnError.c_str()));
  const CMICmnMIValueResult miValueResult("msg", miValueConst);
  const CMICmnMIResultRecord miRecordResult(
      m_cmdData.strMiCmdToken, CMICmnMIResultRecord::eResultClass_Error,
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
CMICmdBase *CMICmdCmdGdbSet::CreateSelf() { return new CMICmdCmdGdbSet(); }

//++
//------------------------------------------------------------------------------------
// Details: Retrieve the print function's pointer for the matching print
// request.
// Type:    Method.
// Args:    vrPrintFnName   - (R) The info requested.
//          vrwpFn          - (W) The print function's pointer of the function
//          to carry out
// Return:  bool    - True = Print request is implemented, false = not found.
// Throws:  None.
//--
bool CMICmdCmdGdbSet::GetOptionFn(const CMIUtilString &vrPrintFnName,
                                  FnGdbOptionPtr &vrwpFn) const {
  vrwpFn = nullptr;

  const MapGdbOptionNameToFnGdbOptionPtr_t::const_iterator it =
      ms_mapGdbOptionNameToFnGdbOptionPtr.find(vrPrintFnName);
  if (it != ms_mapGdbOptionNameToFnGdbOptionPtr.end()) {
    vrwpFn = (*it).second;
    return true;
  }

  return false;
}

//++
//------------------------------------------------------------------------------------
// Details: Carry out work to complete the GDB set option 'target-async' to
// prepare
//          and send back information asked for.
// Type:    Method.
// Args:    vrWords - (R) List of additional parameters used by this option.
// Return:  MIstatus::success - Function succeeded.
//          MIstatus::failure - Function failed.
// Throws:  None.
//--
bool CMICmdCmdGdbSet::OptionFnTargetAsync(
    const CMIUtilString::VecString_t &vrWords) {
  bool bAsyncMode = false;
  bool bOk = true;

  if (vrWords.size() > 1)
    // Too many arguments.
    bOk = false;
  else if (vrWords.size() == 0)
    // If no arguments, default is "on".
    bAsyncMode = true;
  else if (CMIUtilString::Compare(vrWords[0], "on"))
    bAsyncMode = true;
  else if (CMIUtilString::Compare(vrWords[0], "off"))
    bAsyncMode = false;
  else
    // Unrecognized argument.
    bOk = false;

  if (!bOk) {
    // Report error.
    m_bGbbOptionFnHasError = true;
    m_strGdbOptionFnError = MIRSRC(IDS_CMD_ERR_GDBSET_OPT_TARGETASYNC);
    return MIstatus::failure;
  }

  // Turn async mode on/off.
  CMICmnLLDBDebugSessionInfo &rSessionInfo(
      CMICmnLLDBDebugSessionInfo::Instance());
  rSessionInfo.GetDebugger().SetAsync(bAsyncMode);

  return MIstatus::success;
}

//++
//------------------------------------------------------------------------------------
// Details: Carry out work to complete the GDB set option
// 'print-char-array-as-string' to
//          prepare and send back information asked for.
// Type:    Method.
// Args:    vrWords - (R) List of additional parameters used by this option.
// Return:  MIstatus::success - Function succeeded.
//          MIstatus::failure - Function failed.
// Throws:  None.
//--
bool CMICmdCmdGdbSet::OptionFnPrint(const CMIUtilString::VecString_t &vrWords) {
  const bool bAllArgs(vrWords.size() == 2);
  const bool bArgOn(bAllArgs && (CMIUtilString::Compare(vrWords[1], "on") ||
                                 CMIUtilString::Compare(vrWords[1], "1")));
  const bool bArgOff(bAllArgs && (CMIUtilString::Compare(vrWords[1], "off") ||
                                  CMIUtilString::Compare(vrWords[1], "0")));
  if (!bAllArgs || (!bArgOn && !bArgOff)) {
    m_bGbbOptionFnHasError = true;
    m_strGdbOptionFnError = MIRSRC(IDS_CMD_ERR_GDBSET_OPT_PRINT_BAD_ARGS);
    return MIstatus::failure;
  }

  const CMIUtilString strOption(vrWords[0]);
  CMIUtilString strOptionKey;
  if (CMIUtilString::Compare(strOption, "char-array-as-string"))
    strOptionKey = m_rLLDBDebugSessionInfo.m_constStrPrintCharArrayAsString;
  else if (CMIUtilString::Compare(strOption, "expand-aggregates"))
    strOptionKey = m_rLLDBDebugSessionInfo.m_constStrPrintExpandAggregates;
  else if (CMIUtilString::Compare(strOption, "aggregate-field-names"))
    strOptionKey = m_rLLDBDebugSessionInfo.m_constStrPrintAggregateFieldNames;
  else {
    m_bGbbOptionFnHasError = true;
    m_strGdbOptionFnError = CMIUtilString::Format(
        MIRSRC(IDS_CMD_ERR_GDBSET_OPT_PRINT_UNKNOWN_OPTION), strOption.c_str());
    return MIstatus::failure;
  }

  const bool bOptionValue(bArgOn);
  if (!m_rLLDBDebugSessionInfo.SharedDataAdd<bool>(strOptionKey,
                                                   bOptionValue)) {
    m_bGbbOptionFnHasError = false;
    SetError(CMIUtilString::Format(MIRSRC(IDS_DBGSESSION_ERR_SHARED_DATA_ADD),
                                   m_cmdData.strMiCmd.c_str(),
                                   strOptionKey.c_str()));
    return MIstatus::failure;
  }

  return MIstatus::success;
}

//++
//------------------------------------------------------------------------------------
// Details: Carry out work to complete the GDB set option 'solib-search-path' to
// prepare
//          and send back information asked for.
// Type:    Method.
// Args:    vrWords - (R) List of additional parameters used by this option.
// Return:  MIstatus::success - Functional succeeded.
//          MIstatus::failure - Functional failed.
// Throws:  None.
//--
bool CMICmdCmdGdbSet::OptionFnSolibSearchPath(
    const CMIUtilString::VecString_t &vrWords) {
  // Check we have at least one argument
  if (vrWords.size() < 1) {
    m_bGbbOptionFnHasError = true;
    m_strGdbOptionFnError = MIRSRC(IDS_CMD_ERR_GDBSET_OPT_SOLIBSEARCHPATH);
    return MIstatus::failure;
  }
  const CMIUtilString &rStrValSolibPath(vrWords[0]);

  // Add 'solib-search-path' to the shared data list
  const CMIUtilString &rStrKeySolibPath(
      m_rLLDBDebugSessionInfo.m_constStrSharedDataSolibPath);
  if (!m_rLLDBDebugSessionInfo.SharedDataAdd<CMIUtilString>(rStrKeySolibPath,
                                                            rStrValSolibPath)) {
    m_bGbbOptionFnHasError = false;
    SetError(CMIUtilString::Format(MIRSRC(IDS_DBGSESSION_ERR_SHARED_DATA_ADD),
                                   m_cmdData.strMiCmd.c_str(),
                                   rStrKeySolibPath.c_str()));
    return MIstatus::failure;
  }

  return MIstatus::success;
}

//++
//------------------------------------------------------------------------------------
// Details: Carry out work to complete the GDB set option 'output-radix' to
// prepare
//          and send back information asked for.
// Type:    Method.
// Args:    vrWords - (R) List of additional parameters used by this option.
// Return:  MIstatus::success - Functional succeeded.
//          MIstatus::failure - Functional failed.
// Throws:  None.
//--
bool CMICmdCmdGdbSet::OptionFnOutputRadix(
    const CMIUtilString::VecString_t &vrWords) {
  // Check we have at least one argument
  if (vrWords.size() < 1) {
    m_bGbbOptionFnHasError = true;
    m_strGdbOptionFnError = MIRSRC(IDS_CMD_ERR_GDBSET_OPT_SOLIBSEARCHPATH);
    return MIstatus::failure;
  }
  const CMIUtilString &rStrValOutputRadix(vrWords[0]);

  CMICmnLLDBDebugSessionInfoVarObj::varFormat_e format =
      CMICmnLLDBDebugSessionInfoVarObj::eVarFormat_Invalid;
  MIint64 radix;
  if (rStrValOutputRadix.ExtractNumber(radix)) {
    switch (radix) {
    case 8:
      format = CMICmnLLDBDebugSessionInfoVarObj::eVarFormat_Octal;
      break;
    case 10:
      format = CMICmnLLDBDebugSessionInfoVarObj::eVarFormat_Natural;
      break;
    case 16:
      format = CMICmnLLDBDebugSessionInfoVarObj::eVarFormat_Hex;
      break;
    default:
      format = CMICmnLLDBDebugSessionInfoVarObj::eVarFormat_Invalid;
      break;
    }
  }
  if (format == CMICmnLLDBDebugSessionInfoVarObj::eVarFormat_Invalid) {
    m_bGbbOptionFnHasError = false;
    SetError(CMIUtilString::Format(MIRSRC(IDS_DBGSESSION_ERR_SHARED_DATA_ADD),
                                   m_cmdData.strMiCmd.c_str(), "Output Radix"));
    return MIstatus::failure;
  }
  CMICmnLLDBDebugSessionInfoVarObj::VarObjSetFormat(format);

  return MIstatus::success;
}

//++
//------------------------------------------------------------------------------------
// Details: Carry out work to complete the GDB set option 'disassembly-flavor'
// to prepare
//          and send back information asked for.
// Type:    Method.
// Args:    vrWords - (R) List of additional parameters used by this option.
// Return:  MIstatus::success - Functional succeeded.
//          MIstatus::failure - Functional failed.
// Throws:  None.
//--
bool CMICmdCmdGdbSet::OptionFnDisassemblyFlavor(
    const CMIUtilString::VecString_t &vrWords) {
  // Check we have at least one argument
  if (vrWords.size() < 1) {
    m_bGbbOptionFnHasError = true;
    // m_strGdbOptionFnError = MIRSRC(IDS_CMD_ERR_GDBSET_OPT_SOLIBSEARCHPATH);
    return MIstatus::failure;
  }
  const CMIUtilString &rStrValDisasmFlavor(vrWords[0]);

  lldb::SBDebugger &rDbgr = m_rLLDBDebugSessionInfo.GetDebugger();
  lldb::SBError error = lldb::SBDebugger::SetInternalVariable(
      "target.x86-disassembly-flavor", rStrValDisasmFlavor.c_str(),
      rDbgr.GetInstanceName());
  if (error.Fail()) {
    m_strGdbOptionFnError = error.GetCString();
    return MIstatus::failure;
  }

  return MIstatus::success;
}

//++
//------------------------------------------------------------------------------------
// Details: Carry out work to complete the GDB set option 'breakpoint' to
// prepare
//          and send back information asked for.
// Type:    Method.
// Args:    vrWords - (R) List of additional parameters used by this option.
// Return:  MIstatus::success - Function succeeded.
//          MIstatus::failure - Function failed.
// Throws:  None.
//--
bool CMICmdCmdGdbSet::OptionFnBreakpoint(
    const CMIUtilString::VecString_t &vrWords) {
  bool bPending = false;
  bool bOk = true;

  if (vrWords.size() != 2)
    // Wrong number of arguments.
    bOk = false;
  else if (CMIUtilString::Compare(vrWords[0], "pending") &&
           (CMIUtilString::Compare(vrWords[1], "on") ||
            CMIUtilString::Compare(vrWords[1], "1")))
    bPending = true;
  else if (CMIUtilString::Compare(vrWords[0], "pending") &&
           (CMIUtilString::Compare(vrWords[1], "off") ||
            CMIUtilString::Compare(vrWords[1], "0")))
    bPending = false;
  else
    // Unrecognized argument(s).
    bOk = false;

  if (!bOk) {
    // Report error.
    m_bGbbOptionFnHasError = false;
    SetError(MIRSRC(IDS_CMD_ERR_GDBSET_OPT_BREAKPOINT));
    return MIstatus::failure;
  }

  CMIUtilString sPendingVal = bPending ? "on" : "off";
  CMIUtilString sKey = "breakpoint.pending";
  if (!m_rLLDBDebugSessionInfo.SharedDataAdd(sKey, sPendingVal)) {
    m_bGbbOptionFnHasError = false;
    SetError(CMIUtilString::Format(MIRSRC(IDS_DBGSESSION_ERR_SHARED_DATA_ADD),
                                   m_cmdData.strMiCmd.c_str(), sKey.c_str()));
    return MIstatus::failure;
  }

  return MIstatus::success;
}

//++
//------------------------------------------------------------------------------------
// Details: Carry out work to complete the GDB set option to prepare and send
// back the
//          requested information.
// Type:    Method.
// Args:    None.
// Return:  MIstatus::success - Functional succeeded.
//          MIstatus::failure - Functional failed.
// Throws:  None.
//--
bool CMICmdCmdGdbSet::OptionFnFallback(
    const CMIUtilString::VecString_t &vrWords) {
  MIunused(vrWords);

  // Do nothing - intentional. This is a fallback function to do nothing.
  // This allows the search for gdb-set options to always succeed when the
  // option is not
  // found (implemented).

  return MIstatus::success;
}
