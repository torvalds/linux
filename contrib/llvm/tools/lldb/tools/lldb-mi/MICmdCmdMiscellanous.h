//===-- MICmdCmdMiscellanous.h ----------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

// Overview:    CMICmdCmdGdbExit                interface.
//              CMICmdCmdListThreadGroups       interface.
//              CMICmdCmdInterpreterExec        interface.
//              CMICmdCmdInferiorTtySet         interface.
//
//              To implement new MI commands derive a new command class from the
//              command base
//              class. To enable the new command for interpretation add the new
//              command class
//              to the command factory. The files of relevance are:
//                  MICmdCommands.cpp
//                  MICmdBase.h / .cpp
//                  MICmdCmd.h / .cpp
//              For an introduction to adding a new command see
//              CMICmdCmdSupportInfoMiCmdQuery
//              command class as an example.

#pragma once

// Third party headers:
#include "lldb/API/SBCommandReturnObject.h"

// In-house headers:
#include "MICmdBase.h"
#include "MICmnMIValueList.h"
#include "MICmnMIValueTuple.h"

//++
//============================================================================
// Details: MI command class. MI commands derived from the command base class.
//          *this class implements MI command "gdb-exit".
//--
class CMICmdCmdGdbExit : public CMICmdBase {
  // Statics:
public:
  // Required by the CMICmdFactory when registering *this command
  static CMICmdBase *CreateSelf();

  // Methods:
public:
  /* ctor */ CMICmdCmdGdbExit();

  // Overridden:
public:
  // From CMICmdInvoker::ICmd
  bool Execute() override;
  bool Acknowledge() override;
  // From CMICmnBase
  /* dtor */ ~CMICmdCmdGdbExit() override;
};

//++
//============================================================================
// Details: MI command class. MI commands derived from the command base class.
//          *this class implements MI command "list-thread-groups".
//          This command does not follow the MI documentation exactly.
//          http://sourceware.org/gdb/onlinedocs/gdb/GDB_002fMI-Miscellaneous-Commands.html#GDB_002fMI-Miscellaneous-Commands
//--
class CMICmdCmdListThreadGroups : public CMICmdBase {
  // Statics:
public:
  // Required by the CMICmdFactory when registering *this command
  static CMICmdBase *CreateSelf();

  // Methods:
public:
  /* ctor */ CMICmdCmdListThreadGroups();

  // Overridden:
public:
  // From CMICmdInvoker::ICmd
  bool Execute() override;
  bool Acknowledge() override;
  bool ParseArgs() override;
  // From CMICmnBase
  /* dtor */ ~CMICmdCmdListThreadGroups() override;

  // Typedefs:
private:
  typedef std::vector<CMICmnMIValueTuple> VecMIValueTuple_t;

  // Attributes:
private:
  bool m_bIsI1; // True = Yes command argument equal "i1", false = no match
  bool m_bHaveArgOption;  // True = Yes "--available" present, false = not found
  bool m_bHaveArgRecurse; // True = Yes command argument "--recurse", false = no
                          // found
  VecMIValueTuple_t m_vecMIValueTuple;
  const CMIUtilString m_constStrArgNamedAvailable;
  const CMIUtilString m_constStrArgNamedRecurse;
  const CMIUtilString m_constStrArgNamedGroup;
  const CMIUtilString m_constStrArgNamedThreadGroup;
};

//++
//============================================================================
// Details: MI command class. MI commands derived from the command base class.
//          *this class implements MI command "interpreter-exec".
//--
class CMICmdCmdInterpreterExec : public CMICmdBase {
  // Statics:
public:
  // Required by the CMICmdFactory when registering *this command
  static CMICmdBase *CreateSelf();

  // Methods:
public:
  /* ctor */ CMICmdCmdInterpreterExec();

  // Overridden:
public:
  // From CMICmdInvoker::ICmd
  bool Execute() override;
  bool Acknowledge() override;
  bool ParseArgs() override;
  // From CMICmnBase
  /* dtor */ ~CMICmdCmdInterpreterExec() override;

  // Attributes:
private:
  const CMIUtilString m_constStrArgNamedInterpreter;
  const CMIUtilString m_constStrArgNamedCommand;
  lldb::SBCommandReturnObject m_lldbResult;
};

//++
//============================================================================
// Details: MI command class. MI commands derived from the command base class.
//          *this class implements MI command "inferior-tty-set".
//--
class CMICmdCmdInferiorTtySet : public CMICmdBase {
  // Statics:
public:
  // Required by the CMICmdFactory when registering *this command
  static CMICmdBase *CreateSelf();

  // Methods:
public:
  /* ctor */ CMICmdCmdInferiorTtySet();

  // Overridden:
public:
  // From CMICmdInvoker::ICmd
  bool Execute() override;
  bool Acknowledge() override;
  // From CMICmnBase
  /* dtor */ ~CMICmdCmdInferiorTtySet() override;
};
