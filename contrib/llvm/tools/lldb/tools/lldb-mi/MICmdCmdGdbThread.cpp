//===-- MICmdCmdGdbThread.cpp -----------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

// Overview:    CMICmdCmdGdbThread      implementation.

// In-house headers:
#include "MICmdCmdGdbThread.h"
#include "MICmnMIResultRecord.h"
#include "MICmnMIValueConst.h"

//++
//------------------------------------------------------------------------------------
// Details: CMICmdCmdGdbThread constructor.
// Type:    Method.
// Args:    None.
// Return:  None.
// Throws:  None.
//--
CMICmdCmdGdbThread::CMICmdCmdGdbThread() {
  // Command factory matches this name with that received from the stdin stream
  m_strMiCmd = "thread";

  // Required by the CMICmdFactory when registering *this command
  m_pSelfCreatorFn = &CMICmdCmdGdbThread::CreateSelf;
}

//++
//------------------------------------------------------------------------------------
// Details: CMICmdCmdThread destructor.
// Type:    Overrideable.
// Args:    None.
// Return:  None.
// Throws:  None.
//--
CMICmdCmdGdbThread::~CMICmdCmdGdbThread() {}

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
bool CMICmdCmdGdbThread::Execute() {
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
bool CMICmdCmdGdbThread::Acknowledge() {
  const CMICmnMIValueConst miValueConst(MIRSRC(IDS_WORD_NOT_IMPLEMENTED));
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
CMICmdBase *CMICmdCmdGdbThread::CreateSelf() {
  return new CMICmdCmdGdbThread();
}
