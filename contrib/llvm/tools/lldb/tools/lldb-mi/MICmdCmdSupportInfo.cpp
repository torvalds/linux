//===-- MICmdCmdSupportListInfo.cpp -----------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

// Overview:    CMICmdCmdSupportInfoMiCmdQuery          implementation.

// In-house headers:
#include "MICmdCmdSupportInfo.h"
#include "MICmdArgValString.h"
#include "MICmdFactory.h"
#include "MICmnMIResultRecord.h"
#include "MICmnMIValueConst.h"
#include "MICmnMIValueTuple.h"

//++
//------------------------------------------------------------------------------------
// Details: CMICmdCmdSupportInfoMiCmdQuery constructor.
// Type:    Method.
// Args:    None.
// Return:  None.
// Throws:  None.
//--
CMICmdCmdSupportInfoMiCmdQuery::CMICmdCmdSupportInfoMiCmdQuery()
    : m_bCmdFound(false), m_constStrArgCmdName("cmd_name") {
  // Command factory matches this name with that received from the stdin stream
  m_strMiCmd = "info-gdb-mi-command";

  // Required by the CMICmdFactory when registering *this command
  m_pSelfCreatorFn = &CMICmdCmdSupportInfoMiCmdQuery::CreateSelf;
}

//++
//------------------------------------------------------------------------------------
// Details: CMICmdCmdSupportInfoMiCmdQuery destructor.
// Type:    Overrideable.
// Args:    None.
// Return:  None.
// Throws:  None.
//--
CMICmdCmdSupportInfoMiCmdQuery::~CMICmdCmdSupportInfoMiCmdQuery() {}

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
bool CMICmdCmdSupportInfoMiCmdQuery::ParseArgs() {
  m_setCmdArgs.Add(new CMICmdArgValString(m_constStrArgCmdName, true, true));
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
bool CMICmdCmdSupportInfoMiCmdQuery::Execute() {
  CMICMDBASE_GETOPTION(pArgNamedCmdName, String, m_constStrArgCmdName);
  const CMIUtilString &rCmdToQuery(pArgNamedCmdName->GetValue());
  const MIuint nLen = rCmdToQuery.length();
  const CMICmdFactory &rCmdFactory = CMICmdFactory::Instance();
  if ((nLen > 1) && (rCmdToQuery[0] == '-'))
    m_bCmdFound = rCmdFactory.CmdExist(rCmdToQuery.substr(1, nLen - 1).c_str());
  else
    m_bCmdFound = rCmdFactory.CmdExist(rCmdToQuery);

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
bool CMICmdCmdSupportInfoMiCmdQuery::Acknowledge() {
  const CMICmnMIValueConst miValueConst(m_bCmdFound ? "true" : "false");
  const CMICmnMIValueResult miValueResult("exists", miValueConst);
  const CMICmnMIValueTuple miValueTuple(miValueResult);
  const CMICmnMIValueResult miValueResult2("command", miValueTuple);
  const CMICmnMIResultRecord miRecordResult(
      m_cmdData.strMiCmdToken, CMICmnMIResultRecord::eResultClass_Done,
      miValueResult2);
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
CMICmdBase *CMICmdCmdSupportInfoMiCmdQuery::CreateSelf() {
  return new CMICmdCmdSupportInfoMiCmdQuery();
}
