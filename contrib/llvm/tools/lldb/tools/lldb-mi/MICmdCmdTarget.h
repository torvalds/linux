//===-- MICmdCmdTarget.h ----------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

// Overview:    CMICmdCmdTargetSelect           interface.
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

// In-house headers:
#include "MICmdBase.h"
#include "MICmnMIValueList.h"
#include "MICmnMIValueTuple.h"

//++
//============================================================================
// Details: MI command class. MI commands derived from the command base class.
//          *this class implements MI command "target-select".
//          http://sourceware.org/gdb/onlinedocs/gdb/GDB_002fMI-Target-Manipulation.html#GDB_002fMI-Target-Manipulation
//--
class CMICmdCmdTargetSelect : public CMICmdBase {
  // Statics:
public:
  // Required by the CMICmdFactory when registering *this command
  static CMICmdBase *CreateSelf();

  // Methods:
public:
  /* ctor */ CMICmdCmdTargetSelect();

  // Overridden:
public:
  // From CMICmdInvoker::ICmd
  bool Execute() override;
  bool Acknowledge() override;
  bool ParseArgs() override;
  // From CMICmnBase
  /* dtor */ ~CMICmdCmdTargetSelect() override;

  // Attributes:
private:
  const CMIUtilString m_constStrArgNamedType;
  const CMIUtilString m_constStrArgNamedParameters;
};

//++
//============================================================================
// Details: MI command class. MI commands derived from the command base class.
//          *this class implements MI command "target-attach".
//          http://sourceware.org/gdb/onlinedocs/gdb/GDB_002fMI-Target-Manipulation.html#GDB_002fMI-Target-Manipulation
//--
class CMICmdCmdTargetAttach : public CMICmdBase {
  // Statics:
public:
  // Required by the CMICmdFactory when registering *this command
  static CMICmdBase *CreateSelf();

  // Methods:
public:
  /* ctor */ CMICmdCmdTargetAttach();

  // Overridden:
public:
  // From CMICmdInvoker::ICmd
  bool Execute() override;
  bool Acknowledge() override;
  bool ParseArgs() override;
  // From CMICmnBase
  /* dtor */ ~CMICmdCmdTargetAttach() override;

  // Attributes:
private:
  const CMIUtilString m_constStrArgPid;
  const CMIUtilString m_constStrArgNamedFile;
  const CMIUtilString m_constStrArgWaitFor;
};

//++
//============================================================================
// Details: MI command class. MI commands derived from the command base class.
//          *this class implements MI command "target-attach".
//          http://sourceware.org/gdb/onlinedocs/gdb/GDB_002fMI-Target-Manipulation.html#GDB_002fMI-Target-Manipulation
//--
class CMICmdCmdTargetDetach : public CMICmdBase {
  // Statics:
public:
  // Required by the CMICmdFactory when registering *this command
  static CMICmdBase *CreateSelf();

  // Methods:
public:
  /* ctor */ CMICmdCmdTargetDetach();

  // Overridden:
public:
  // From CMICmdInvoker::ICmd
  bool Execute() override;
  bool Acknowledge() override;
  bool ParseArgs() override;
  // From CMICmnBase
  /* dtor */ ~CMICmdCmdTargetDetach() override;
};
