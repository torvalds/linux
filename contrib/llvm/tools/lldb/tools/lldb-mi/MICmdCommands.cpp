//===-- MICmdCommands.cpp ---------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

// Overview:    MI command are registered with the MI command factory.
//
//              To implement new MI commands derive a new command class from the
//              command base
//              class. To enable the new command for interpretation add the new
//              command class
//              to the command factory. The files of relevance are:
//                  MICmdCommands.cpp
//                  MICmdBase.h / .cpp
//                  MICmdCmd.h / .cpp

// In-house headers:
#include "MICmdCommands.h"
#include "MICmdCmd.h"
#include "MICmdCmdBreak.h"
#include "MICmdCmdData.h"
#include "MICmdCmdEnviro.h"
#include "MICmdCmdExec.h"
#include "MICmdCmdFile.h"
#include "MICmdCmdGdbInfo.h"
#include "MICmdCmdGdbSet.h"
#include "MICmdCmdGdbShow.h"
#include "MICmdCmdGdbThread.h"
#include "MICmdCmdMiscellanous.h"
#include "MICmdCmdStack.h"
#include "MICmdCmdSupportInfo.h"
#include "MICmdCmdSupportList.h"
#include "MICmdCmdSymbol.h"
#include "MICmdCmdTarget.h"
#include "MICmdCmdThread.h"
#include "MICmdCmdTrace.h"
#include "MICmdCmdVar.h"
#include "MICmdFactory.h"

namespace MICmnCommands {
template <typename T> static bool Register();
}

//++
//------------------------------------------------------------------------------------
// Details: Command to command factory registration function.
// Type:    Template function.
// Args:    typename T  - A command type class.
// Return:  bool  - True = yes command is registered, false = command failed to
// register.
// Throws:  None.
//--
template <typename T> static bool MICmnCommands::Register() {
  static CMICmdFactory &rCmdFactory = CMICmdFactory::Instance();
  const CMIUtilString strMiCmd = T().GetMiCmd();
  CMICmdFactory::CmdCreatorFnPtr fn = T().GetCmdCreatorFn();
  return rCmdFactory.CmdRegister(strMiCmd, fn);
}

//++
//------------------------------------------------------------------------------------
// Details: Register commands with MI command factory
// Type:    Function.
// Args:    None.
// Return:  bool  - True = yes all commands are registered,
//                  false = one or more commands failed to register.
// Throws:  None.
//--
bool MICmnCommands::RegisterAll() {
  bool bOk = MIstatus::success;

  bOk &= Register<CMICmdCmdSupportInfoMiCmdQuery>();
  bOk &= Register<CMICmdCmdBreakAfter>();
  bOk &= Register<CMICmdCmdBreakCondition>();
  bOk &= Register<CMICmdCmdBreakDelete>();
  bOk &= Register<CMICmdCmdBreakDisable>();
  bOk &= Register<CMICmdCmdBreakEnable>();
  bOk &= Register<CMICmdCmdBreakInsert>();
  bOk &= Register<CMICmdCmdDataDisassemble>();
  bOk &= Register<CMICmdCmdDataEvaluateExpression>();
  bOk &= Register<CMICmdCmdDataInfoLine>();
  bOk &= Register<CMICmdCmdDataReadMemoryBytes>();
  bOk &= Register<CMICmdCmdDataReadMemory>();
  bOk &= Register<CMICmdCmdDataListRegisterNames>();
  bOk &= Register<CMICmdCmdDataListRegisterValues>();
  bOk &= Register<CMICmdCmdDataWriteMemory>();
  bOk &= Register<CMICmdCmdEnablePrettyPrinting>();
  bOk &= Register<CMICmdCmdEnvironmentCd>();
  bOk &= Register<CMICmdCmdExecAbort>();
  bOk &= Register<CMICmdCmdExecArguments>();
  bOk &= Register<CMICmdCmdExecContinue>();
  bOk &= Register<CMICmdCmdExecInterrupt>();
  bOk &= Register<CMICmdCmdExecFinish>();
  bOk &= Register<CMICmdCmdExecNext>();
  bOk &= Register<CMICmdCmdExecNextInstruction>();
  bOk &= Register<CMICmdCmdExecRun>();
  bOk &= Register<CMICmdCmdExecStep>();
  bOk &= Register<CMICmdCmdExecStepInstruction>();
  bOk &= Register<CMICmdCmdFileExecAndSymbols>();
  bOk &= Register<CMICmdCmdGdbExit>();
  bOk &= Register<CMICmdCmdGdbInfo>();
  bOk &= Register<CMICmdCmdGdbSet>();
  bOk &= Register<CMICmdCmdGdbShow>();
  bOk &= Register<CMICmdCmdGdbThread>();
  bOk &= Register<CMICmdCmdInferiorTtySet>();
  bOk &= Register<CMICmdCmdInterpreterExec>();
  bOk &= Register<CMICmdCmdListThreadGroups>();
  bOk &= Register<CMICmdCmdSource>();
  bOk &= Register<CMICmdCmdStackInfoDepth>();
  bOk &= Register<CMICmdCmdStackInfoFrame>();
  bOk &= Register<CMICmdCmdStackListFrames>();
  bOk &= Register<CMICmdCmdStackListArguments>();
  bOk &= Register<CMICmdCmdStackListLocals>();
  bOk &= Register<CMICmdCmdStackListVariables>();
  bOk &= Register<CMICmdCmdStackSelectFrame>();
  bOk &= Register<CMICmdCmdSupportListFeatures>();
  bOk &= Register<CMICmdCmdSymbolListLines>();
  bOk &= Register<CMICmdCmdTargetSelect>();
  bOk &= Register<CMICmdCmdTargetAttach>();
  bOk &= Register<CMICmdCmdTargetDetach>();
  bOk &= Register<CMICmdCmdThreadInfo>();
  bOk &= Register<CMICmdCmdVarAssign>();
  bOk &= Register<CMICmdCmdVarCreate>();
  bOk &= Register<CMICmdCmdVarDelete>();
  bOk &= Register<CMICmdCmdVarEvaluateExpression>();
  bOk &= Register<CMICmdCmdVarInfoPathExpression>();
  bOk &= Register<CMICmdCmdVarListChildren>();
  bOk &= Register<CMICmdCmdVarSetFormat>();
  bOk &= Register<CMICmdCmdVarShowAttributes>();
  bOk &= Register<CMICmdCmdVarUpdate>();

  return bOk;
}
