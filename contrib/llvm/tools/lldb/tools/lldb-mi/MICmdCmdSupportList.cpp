//===-- MICmdCmdSupportList.cpp ---------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

// Overview:    CMICmdCmdSupportListFeatures            implementation.

// In-house headers:
#include "MICmdCmdSupportList.h"
#include "MICmnMIResultRecord.h"
#include "MICmnMIValueConst.h"
#include "MICmnMIValueList.h"

//++
//------------------------------------------------------------------------------------
// Details: CMICmdCmdSupportListFeatures constructor.
// Type:    Method.
// Args:    None.
// Return:  None.
// Throws:  None.
//--
CMICmdCmdSupportListFeatures::CMICmdCmdSupportListFeatures() {
  // Command factory matches this name with that received from the stdin stream
  m_strMiCmd = "list-features";

  // Required by the CMICmdFactory when registering *this command
  m_pSelfCreatorFn = &CMICmdCmdSupportListFeatures::CreateSelf;
}

//++
//------------------------------------------------------------------------------------
// Details: CMICmdCmdSupportListFeatures destructor.
// Type:    Overrideable.
// Args:    None.
// Return:  None.
// Throws:  None.
//--
CMICmdCmdSupportListFeatures::~CMICmdCmdSupportListFeatures() {}

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
bool CMICmdCmdSupportListFeatures::Execute() {
  // Do nothing

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
bool CMICmdCmdSupportListFeatures::Acknowledge() {
  // Declare supported features here
  const CMICmnMIValueConst miValueConst1("data-read-memory-bytes");
  const CMICmnMIValueConst miValueConst2("exec-run-start-option");
  // Some features may depend on host and/or target, decide what to add below
  CMICmnMIValueList miValueList(true);
  miValueList.Add(miValueConst1);
  miValueList.Add(miValueConst2);
  const CMICmnMIValueResult miValueResult("features", miValueList);
  const CMICmnMIResultRecord miRecordResult(
      m_cmdData.strMiCmdToken, CMICmnMIResultRecord::eResultClass_Done,
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
CMICmdBase *CMICmdCmdSupportListFeatures::CreateSelf() {
  return new CMICmdCmdSupportListFeatures();
}
