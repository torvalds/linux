//===-- MICmdCmdMiscellanous.cpp --------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

// Overview:    CMICmdCmdGdbExit                implementation.
//              CMICmdCmdListThreadGroups       implementation.
//              CMICmdCmdInterpreterExec        implementation.
//              CMICmdCmdInferiorTtySet         implementation.

// Third Party Headers:
#include "lldb/API/SBCommandInterpreter.h"
#include "lldb/API/SBThread.h"

// In-house headers:
#include "MICmdArgValFile.h"
#include "MICmdArgValListOfN.h"
#include "MICmdArgValNumber.h"
#include "MICmdArgValOptionLong.h"
#include "MICmdArgValOptionShort.h"
#include "MICmdArgValString.h"
#include "MICmdArgValThreadGrp.h"
#include "MICmdCmdMiscellanous.h"
#include "MICmnLLDBDebugSessionInfo.h"
#include "MICmnLLDBDebugger.h"
#include "MICmnMIOutOfBandRecord.h"
#include "MICmnMIResultRecord.h"
#include "MICmnMIValueConst.h"
#include "MICmnStreamStderr.h"
#include "MICmnStreamStdout.h"
#include "MIDriverBase.h"

//++
//------------------------------------------------------------------------------------
// Details: CMICmdCmdGdbExit constructor.
// Type:    Method.
// Args:    None.
// Return:  None.
// Throws:  None.
//--
CMICmdCmdGdbExit::CMICmdCmdGdbExit() {
  // Command factory matches this name with that received from the stdin stream
  m_strMiCmd = "gdb-exit";

  // Required by the CMICmdFactory when registering *this command
  m_pSelfCreatorFn = &CMICmdCmdGdbExit::CreateSelf;
}

//++
//------------------------------------------------------------------------------------
// Details: CMICmdCmdGdbExit destructor.
// Type:    Overrideable.
// Args:    None.
// Return:  None.
// Throws:  None.
//--
CMICmdCmdGdbExit::~CMICmdCmdGdbExit() {}

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
bool CMICmdCmdGdbExit::Execute() {
  CMICmnLLDBDebugger::Instance().GetDriver().SetExitApplicationFlag(true);
  const lldb::SBError sbErr = m_rLLDBDebugSessionInfo.GetProcess().Destroy();
  // Do not check for sbErr.Fail() here, m_lldbProcess is likely !IsValid()

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
bool CMICmdCmdGdbExit::Acknowledge() {
  const CMICmnMIResultRecord miRecordResult(
      m_cmdData.strMiCmdToken, CMICmnMIResultRecord::eResultClass_Exit);
  m_miResultRecord = miRecordResult;

  // Prod the client i.e. Eclipse with out-of-band results to help it 'continue'
  // because it is using LLDB debugger
  // Give the client '=thread-group-exited,id="i1"'
  m_bHasResultRecordExtra = true;
  const CMICmnMIValueConst miValueConst2("i1");
  const CMICmnMIValueResult miValueResult2("id", miValueConst2);
  const CMICmnMIOutOfBandRecord miOutOfBand(
      CMICmnMIOutOfBandRecord::eOutOfBand_ThreadGroupExited, miValueResult2);
  m_miResultRecordExtra = miOutOfBand.GetString();

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
CMICmdBase *CMICmdCmdGdbExit::CreateSelf() { return new CMICmdCmdGdbExit(); }

//---------------------------------------------------------------------------------------
//---------------------------------------------------------------------------------------
//---------------------------------------------------------------------------------------

//++
//------------------------------------------------------------------------------------
// Details: CMICmdCmdListThreadGroups constructor.
// Type:    Method.
// Args:    None.
// Return:  None.
// Throws:  None.
//--
CMICmdCmdListThreadGroups::CMICmdCmdListThreadGroups()
    : m_bIsI1(false), m_bHaveArgOption(false), m_bHaveArgRecurse(false),
      m_constStrArgNamedAvailable("available"),
      m_constStrArgNamedRecurse("recurse"), m_constStrArgNamedGroup("group"),
      m_constStrArgNamedThreadGroup("i1") {
  // Command factory matches this name with that received from the stdin stream
  m_strMiCmd = "list-thread-groups";

  // Required by the CMICmdFactory when registering *this command
  m_pSelfCreatorFn = &CMICmdCmdListThreadGroups::CreateSelf;
}

//++
//------------------------------------------------------------------------------------
// Details: CMICmdCmdListThreadGroups destructor.
// Type:    Overrideable.
// Args:    None.
// Return:  None.
// Throws:  None.
//--
CMICmdCmdListThreadGroups::~CMICmdCmdListThreadGroups() {
  m_vecMIValueTuple.clear();
}

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
bool CMICmdCmdListThreadGroups::ParseArgs() {
  m_setCmdArgs.Add(
      new CMICmdArgValOptionLong(m_constStrArgNamedAvailable, false, true));
  m_setCmdArgs.Add(
      new CMICmdArgValOptionLong(m_constStrArgNamedRecurse, false, true,
                                 CMICmdArgValListBase::eArgValType_Number, 1));
  m_setCmdArgs.Add(
      new CMICmdArgValListOfN(m_constStrArgNamedGroup, false, true,
                              CMICmdArgValListBase::eArgValType_Number));
  m_setCmdArgs.Add(
      new CMICmdArgValThreadGrp(m_constStrArgNamedThreadGroup, false, true));
  return ParseValidateCmdOptions();
}

//++
//------------------------------------------------------------------------------------
// Details: The invoker requires this function. The command does work in this
// function.
//          The command is likely to communicate with the LLDB SBDebugger in
//          here.
//          Synopsis: -list-thread-groups [ --available ] [ --recurse 1 ] [
//          group ... ]
//          This command does not follow the MI documentation exactly. Has an
//          extra
//          argument "i1" to handle.
//          Ref:
// http://sourceware.org/gdb/onlinedocs/gdb/GDB_002fMI-Miscellaneous-Commands.html#GDB_002fMI-Miscellaneous-Commands
// Type:    Overridden.
// Args:    None.
// Return:  MIstatus::success - Functional succeeded.
//          MIstatus::failure - Functional failed.
// Throws:  None.
//--
bool CMICmdCmdListThreadGroups::Execute() {
  if (m_setCmdArgs.IsArgContextEmpty())
    // No options so "top level thread groups"
    return MIstatus::success;

  CMICMDBASE_GETOPTION(pArgAvailable, OptionLong, m_constStrArgNamedAvailable);
  CMICMDBASE_GETOPTION(pArgRecurse, OptionLong, m_constStrArgNamedRecurse);
  CMICMDBASE_GETOPTION(pArgThreadGroup, ThreadGrp,
                       m_constStrArgNamedThreadGroup);

  // Got some options so "threads"
  if (pArgAvailable->GetFound()) {
    if (pArgRecurse->GetFound()) {
      m_bHaveArgRecurse = true;
      return MIstatus::success;
    }

    m_bHaveArgOption = true;
    return MIstatus::success;
  }
  // "i1" as first argument (pos 0 of possible arg)
  if (!pArgThreadGroup->GetFound())
    return MIstatus::success;
  m_bIsI1 = true;

  CMICmnLLDBDebugSessionInfo &rSessionInfo(
      CMICmnLLDBDebugSessionInfo::Instance());
  lldb::SBProcess sbProcess = rSessionInfo.GetProcess();

  // Note do not check for sbProcess is IsValid(), continue

  m_vecMIValueTuple.clear();
  const MIuint nThreads = sbProcess.GetNumThreads();
  for (MIuint i = 0; i < nThreads; i++) {
    //  GetThreadAtIndex() uses a base 0 index
    //  GetThreadByIndexID() uses a base 1 index
    lldb::SBThread thread = sbProcess.GetThreadAtIndex(i);

    if (thread.IsValid()) {
      CMICmnMIValueTuple miTuple;
      if (!rSessionInfo.MIResponseFormThreadInfo(
              m_cmdData, thread,
              CMICmnLLDBDebugSessionInfo::eThreadInfoFormat_NoFrames, miTuple))
        return MIstatus::failure;

      m_vecMIValueTuple.push_back(miTuple);
    }
  }

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
bool CMICmdCmdListThreadGroups::Acknowledge() {
  if (m_bHaveArgOption) {
    if (m_bHaveArgRecurse) {
      const CMICmnMIValueConst miValueConst(
          MIRSRC(IDS_WORD_NOT_IMPLEMENTED_BRKTS));
      const CMICmnMIValueResult miValueResult("msg", miValueConst);
      const CMICmnMIResultRecord miRecordResult(
          m_cmdData.strMiCmdToken, CMICmnMIResultRecord::eResultClass_Error,
          miValueResult);
      m_miResultRecord = miRecordResult;

      return MIstatus::success;
    }

    const CMICmnMIValueConst miValueConst1("i1");
    const CMICmnMIValueResult miValueResult1("id", miValueConst1);
    CMICmnMIValueTuple miTuple(miValueResult1);

    const CMICmnMIValueConst miValueConst2("process");
    const CMICmnMIValueResult miValueResult2("type", miValueConst2);
    miTuple.Add(miValueResult2);

    CMICmnLLDBDebugSessionInfo &rSessionInfo(
        CMICmnLLDBDebugSessionInfo::Instance());
    if (rSessionInfo.GetProcess().IsValid()) {
      const lldb::pid_t pid = rSessionInfo.GetProcess().GetProcessID();
      const CMIUtilString strPid(CMIUtilString::Format("%lld", pid));
      const CMICmnMIValueConst miValueConst3(strPid);
      const CMICmnMIValueResult miValueResult3("pid", miValueConst3);
      miTuple.Add(miValueResult3);
    }

    const CMICmnMIValueConst miValueConst4(
        MIRSRC(IDS_WORD_NOT_IMPLEMENTED_BRKTS));
    const CMICmnMIValueResult miValueResult4("num_children", miValueConst4);
    miTuple.Add(miValueResult4);

    const CMICmnMIValueConst miValueConst5(
        MIRSRC(IDS_WORD_NOT_IMPLEMENTED_BRKTS));
    const CMICmnMIValueResult miValueResult5("cores", miValueConst5);
    miTuple.Add(miValueResult5);

    const CMICmnMIValueList miValueList(miTuple);
    const CMICmnMIValueResult miValueResult6("groups", miValueList);
    const CMICmnMIResultRecord miRecordResult(
        m_cmdData.strMiCmdToken, CMICmnMIResultRecord::eResultClass_Done,
        miValueResult6);
    m_miResultRecord = miRecordResult;

    return MIstatus::success;
  }

  if (!m_bIsI1) {
    const CMICmnMIValueConst miValueConst1("i1");
    const CMICmnMIValueResult miValueResult1("id", miValueConst1);
    CMICmnMIValueTuple miTuple(miValueResult1);

    const CMICmnMIValueConst miValueConst2("process");
    const CMICmnMIValueResult miValueResult2("type", miValueConst2);
    miTuple.Add(miValueResult2);

    CMICmnLLDBDebugSessionInfo &rSessionInfo(
        CMICmnLLDBDebugSessionInfo::Instance());
    if (rSessionInfo.GetProcess().IsValid()) {
      const lldb::pid_t pid = rSessionInfo.GetProcess().GetProcessID();
      const CMIUtilString strPid(CMIUtilString::Format("%lld", pid));
      const CMICmnMIValueConst miValueConst3(strPid);
      const CMICmnMIValueResult miValueResult3("pid", miValueConst3);
      miTuple.Add(miValueResult3);
    }

    if (rSessionInfo.GetTarget().IsValid()) {
      lldb::SBTarget sbTrgt = rSessionInfo.GetTarget();
      const char *pDir = sbTrgt.GetExecutable().GetDirectory();
      const char *pFileName = sbTrgt.GetExecutable().GetFilename();
      const CMIUtilString strFile(
          CMIUtilString::Format("%s/%s", pDir, pFileName));
      const CMICmnMIValueConst miValueConst4(strFile);
      const CMICmnMIValueResult miValueResult4("executable", miValueConst4);
      miTuple.Add(miValueResult4);
    }

    const CMICmnMIValueList miValueList(miTuple);
    const CMICmnMIValueResult miValueResult5("groups", miValueList);
    const CMICmnMIResultRecord miRecordResult(
        m_cmdData.strMiCmdToken, CMICmnMIResultRecord::eResultClass_Done,
        miValueResult5);
    m_miResultRecord = miRecordResult;
    return MIstatus::success;
  }

  // Build up a list of thread information from tuples
  VecMIValueTuple_t::const_iterator it = m_vecMIValueTuple.begin();
  if (it == m_vecMIValueTuple.end()) {
    const CMICmnMIValueConst miValueConst("[]");
    const CMICmnMIValueResult miValueResult("threads", miValueConst);
    const CMICmnMIResultRecord miRecordResult(
        m_cmdData.strMiCmdToken, CMICmnMIResultRecord::eResultClass_Done,
        miValueResult);
    m_miResultRecord = miRecordResult;
    return MIstatus::success;
  }
  CMICmnMIValueList miValueList(*it);
  ++it;
  while (it != m_vecMIValueTuple.end()) {
    const CMICmnMIValueTuple &rTuple(*it);
    miValueList.Add(rTuple);

    // Next
    ++it;
  }

  const CMICmnMIValueResult miValueResult("threads", miValueList);
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
CMICmdBase *CMICmdCmdListThreadGroups::CreateSelf() {
  return new CMICmdCmdListThreadGroups();
}

//---------------------------------------------------------------------------------------
//---------------------------------------------------------------------------------------
//---------------------------------------------------------------------------------------

//++
//------------------------------------------------------------------------------------
// Details: CMICmdCmdInterpreterExec constructor.
// Type:    Method.
// Args:    None.
// Return:  None.
// Throws:  None.
//--
CMICmdCmdInterpreterExec::CMICmdCmdInterpreterExec()
    : m_constStrArgNamedInterpreter("interpreter"),
      m_constStrArgNamedCommand("command") {
  // Command factory matches this name with that received from the stdin stream
  m_strMiCmd = "interpreter-exec";

  // Required by the CMICmdFactory when registering *this command
  m_pSelfCreatorFn = &CMICmdCmdInterpreterExec::CreateSelf;
}

//++
//------------------------------------------------------------------------------------
// Details: CMICmdCmdInterpreterExec destructor.
// Type:    Overrideable.
// Args:    None.
// Return:  None.
// Throws:  None.
//--
CMICmdCmdInterpreterExec::~CMICmdCmdInterpreterExec() {}

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
bool CMICmdCmdInterpreterExec::ParseArgs() {
  m_setCmdArgs.Add(
      new CMICmdArgValString(m_constStrArgNamedInterpreter, true, true));
  m_setCmdArgs.Add(
      new CMICmdArgValString(m_constStrArgNamedCommand, true, true, true));
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
bool CMICmdCmdInterpreterExec::Execute() {
  CMICMDBASE_GETOPTION(pArgInterpreter, String, m_constStrArgNamedInterpreter);
  CMICMDBASE_GETOPTION(pArgCommand, String, m_constStrArgNamedCommand);

  // Handle the interpreter parameter by do nothing on purpose (set to 'handled'
  // in
  // the arg definition above)
  const CMIUtilString &rStrInterpreter(pArgInterpreter->GetValue());
  MIunused(rStrInterpreter);

  const CMIUtilString &rStrCommand(pArgCommand->GetValue());
  CMICmnLLDBDebugSessionInfo &rSessionInfo(
      CMICmnLLDBDebugSessionInfo::Instance());
  const lldb::ReturnStatus rtn =
      rSessionInfo.GetDebugger().GetCommandInterpreter().HandleCommand(
          rStrCommand.c_str(), m_lldbResult, true);
  MIunused(rtn);

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
bool CMICmdCmdInterpreterExec::Acknowledge() {
  if (m_lldbResult.GetOutputSize() > 0) {
    const CMIUtilString line(m_lldbResult.GetOutput());
    const bool bEscapeQuotes(true);
    CMICmnMIValueConst miValueConst(line.Escape(bEscapeQuotes));
    CMICmnMIOutOfBandRecord miOutOfBandRecord(CMICmnMIOutOfBandRecord::eOutOfBand_ConsoleStreamOutput, miValueConst);
    const bool bOk = CMICmnStreamStdout::TextToStdout(miOutOfBandRecord.GetString());
    if (!bOk)
      return MIstatus::failure;
  }
  if (m_lldbResult.GetErrorSize() > 0) {
    const CMIUtilString line(m_lldbResult.GetError());
    const bool bEscapeQuotes(true);
    CMICmnMIValueConst miValueConst(line.Escape(bEscapeQuotes));
    CMICmnMIOutOfBandRecord miOutOfBandRecord(CMICmnMIOutOfBandRecord::eOutOfBand_LogStreamOutput, miValueConst);
    const bool bOk = CMICmnStreamStdout::TextToStdout(miOutOfBandRecord.GetString());
    if (!bOk)
      return MIstatus::failure;
  }

  const CMICmnMIResultRecord miRecordResult(
      m_cmdData.strMiCmdToken, CMICmnMIResultRecord::eResultClass_Done);
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
CMICmdBase *CMICmdCmdInterpreterExec::CreateSelf() {
  return new CMICmdCmdInterpreterExec();
}

//---------------------------------------------------------------------------------------
//---------------------------------------------------------------------------------------
//---------------------------------------------------------------------------------------

//++
//------------------------------------------------------------------------------------
// Details: CMICmdCmdInferiorTtySet constructor.
// Type:    Method.
// Args:    None.
// Return:  None.
// Throws:  None.
//--
CMICmdCmdInferiorTtySet::CMICmdCmdInferiorTtySet() {
  // Command factory matches this name with that received from the stdin stream
  m_strMiCmd = "inferior-tty-set";

  // Required by the CMICmdFactory when registering *this command
  m_pSelfCreatorFn = &CMICmdCmdInferiorTtySet::CreateSelf;
}

//++
//------------------------------------------------------------------------------------
// Details: CMICmdCmdInferiorTtySet destructor.
// Type:    Overrideable.
// Args:    None.
// Return:  None.
// Throws:  None.
//--
CMICmdCmdInferiorTtySet::~CMICmdCmdInferiorTtySet() {}

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
bool CMICmdCmdInferiorTtySet::Execute() {
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
bool CMICmdCmdInferiorTtySet::Acknowledge() {
  const CMICmnMIResultRecord miRecordResult(
      m_cmdData.strMiCmdToken, CMICmnMIResultRecord::eResultClass_Error);
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
CMICmdBase *CMICmdCmdInferiorTtySet::CreateSelf() {
  return new CMICmdCmdInferiorTtySet();
}
