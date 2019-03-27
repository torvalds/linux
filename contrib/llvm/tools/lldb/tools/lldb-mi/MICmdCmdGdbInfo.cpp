//===-- MICmdCmdGdbInfo.cpp -------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

// Overview:    CMICmdCmdGdbInfo        implementation.

// Third party headers:
#include "lldb/API/SBCommandReturnObject.h"
#include <inttypes.h>

// In-house headers:
#include "MICmdArgValString.h"
#include "MICmdCmdGdbInfo.h"
#include "MICmnLLDBDebugSessionInfo.h"
#include "MICmnMIResultRecord.h"
#include "MICmnMIValueConst.h"
#include "MICmnStreamStdout.h"

// Instantiations:
const CMICmdCmdGdbInfo::MapPrintFnNameToPrintFn_t
    CMICmdCmdGdbInfo::ms_mapPrintFnNameToPrintFn = {
        {"sharedlibrary", &CMICmdCmdGdbInfo::PrintFnSharedLibrary}};

//++
//------------------------------------------------------------------------------------
// Details: CMICmdCmdGdbInfo constructor.
// Type:    Method.
// Args:    None.
// Return:  None.
// Throws:  None.
//--
CMICmdCmdGdbInfo::CMICmdCmdGdbInfo()
    : m_constStrArgNamedPrint("print"), m_bPrintFnRecognised(true),
      m_bPrintFnSuccessful(false),
      m_strPrintFnError(MIRSRC(IDS_WORD_ERR_MSG_NOT_IMPLEMENTED_BRKTS)) {
  // Command factory matches this name with that received from the stdin stream
  m_strMiCmd = "info";

  // Required by the CMICmdFactory when registering *this command
  m_pSelfCreatorFn = &CMICmdCmdGdbInfo::CreateSelf;
}

//++
//------------------------------------------------------------------------------------
// Details: CMICmdCmdGdbInfo destructor.
// Type:    Overrideable.
// Args:    None.
// Return:  None.
// Throws:  None.
//--
CMICmdCmdGdbInfo::~CMICmdCmdGdbInfo() {}

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
bool CMICmdCmdGdbInfo::ParseArgs() {
  m_setCmdArgs.Add(new CMICmdArgValString(m_constStrArgNamedPrint, true, true));
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
bool CMICmdCmdGdbInfo::Execute() {
  CMICMDBASE_GETOPTION(pArgPrint, String, m_constStrArgNamedPrint);
  const CMIUtilString &rPrintRequest(pArgPrint->GetValue());

  FnPrintPtr pPrintRequestFn = nullptr;
  if (!GetPrintFn(rPrintRequest, pPrintRequestFn)) {
    m_strPrintFnName = rPrintRequest;
    m_bPrintFnRecognised = false;
    return MIstatus::success;
  }

  m_bPrintFnSuccessful = (this->*(pPrintRequestFn))();

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
bool CMICmdCmdGdbInfo::Acknowledge() {
  if (!m_bPrintFnRecognised) {
    const CMICmnMIValueConst miValueConst(CMIUtilString::Format(
        MIRSRC(IDS_CMD_ERR_INFO_PRINTFN_NOT_FOUND), m_strPrintFnName.c_str()));
    const CMICmnMIValueResult miValueResult("msg", miValueConst);
    const CMICmnMIResultRecord miRecordResult(
        m_cmdData.strMiCmdToken, CMICmnMIResultRecord::eResultClass_Error,
        miValueResult);
    m_miResultRecord = miRecordResult;
    return MIstatus::success;
  }

  if (m_bPrintFnSuccessful) {
    const CMICmnMIResultRecord miRecordResult(
        m_cmdData.strMiCmdToken, CMICmnMIResultRecord::eResultClass_Done);
    m_miResultRecord = miRecordResult;
    return MIstatus::success;
  }

  const CMICmnMIValueConst miValueConst(CMIUtilString::Format(
      MIRSRC(IDS_CMD_ERR_INFO_PRINTFN_FAILED), m_strPrintFnError.c_str()));
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
CMICmdBase *CMICmdCmdGdbInfo::CreateSelf() { return new CMICmdCmdGdbInfo(); }

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
bool CMICmdCmdGdbInfo::GetPrintFn(const CMIUtilString &vrPrintFnName,
                                  FnPrintPtr &vrwpFn) const {
  vrwpFn = nullptr;

  const MapPrintFnNameToPrintFn_t::const_iterator it =
      ms_mapPrintFnNameToPrintFn.find(vrPrintFnName);
  if (it != ms_mapPrintFnNameToPrintFn.end()) {
    vrwpFn = (*it).second;
    return true;
  }

  return false;
}

//++
//------------------------------------------------------------------------------------
// Details: Carry out work to complete the request to prepare and send back
// information
//          asked for.
// Type:    Method.
// Args:    None.
// Return:  MIstatus::success - Functional succeeded.
//          MIstatus::failure - Functional failed.
// Throws:  None.
//--
bool CMICmdCmdGdbInfo::PrintFnSharedLibrary() {
  bool bOk = CMICmnStreamStdout::TextToStdout(
      "~\"From        To          Syms Read   Shared Object Library\"");

  CMICmnLLDBDebugSessionInfo &rSessionInfo(
      CMICmnLLDBDebugSessionInfo::Instance());
  lldb::SBTarget sbTarget = rSessionInfo.GetTarget();
  const MIuint nModules = sbTarget.GetNumModules();
  for (MIuint i = 0; bOk && (i < nModules); i++) {
    lldb::SBModule module = sbTarget.GetModuleAtIndex(i);
    if (module.IsValid()) {
      const CMIUtilString strModuleFilePath(
          module.GetFileSpec().GetDirectory());
      const CMIUtilString strModuleFileName(module.GetFileSpec().GetFilename());
      const CMIUtilString strModuleFullPath(CMIUtilString::Format(
          "%s/%s", strModuleFilePath.c_str(), strModuleFileName.c_str()));
      const CMIUtilString strHasSymbols =
          (module.GetNumSymbols() > 0) ? "Yes" : "No";
      lldb::addr_t addrLoadS = 0xffffffffffffffff;
      lldb::addr_t addrLoadSize = 0;
      bool bHaveAddrLoad = false;
      const MIuint nSections = module.GetNumSections();
      for (MIuint j = 0; j < nSections; j++) {
        lldb::SBSection section = module.GetSectionAtIndex(j);
        lldb::addr_t addrLoad = section.GetLoadAddress(sbTarget);
        if (addrLoad != (lldb::addr_t)-1) {
          if (!bHaveAddrLoad) {
            bHaveAddrLoad = true;
            addrLoadS = addrLoad;
          }

          addrLoadSize += section.GetByteSize();
        }
      }
      bOk = bOk &&
        CMICmnStreamStdout::TextToStdout(CMIUtilString::Format(
                "~\"0x%016" PRIx64 "\t0x%016" PRIx64 "\t%s\t\t%s\"", addrLoadS,
                addrLoadS + addrLoadSize, strHasSymbols.c_str(),
                strModuleFullPath.c_str()));
    }
  }

  return bOk;
}
