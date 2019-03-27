//===-- MICmdCmdGdbInfo.h ---------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

// Overview:    CMICmdCmdGdbInfo    interface.
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
#include <map>

// In-house headers:
#include "MICmdBase.h"

//++
//============================================================================
// Details: MI command class. MI commands derived from the command base class.
//          *this class implements GDB command "info".
//          The design of matching the info request to a request action (or
//          command) is very simple. The request function which carries out
//          the task of information gathering and printing to stdout is part of
//          *this class. Should the request function become more complicated
//          then
//          that request should really reside in a command type class. Then this
//          class instantiates a request info command for a matching request.
//          The
//          design/code of *this class then does not then become bloated. Use a
//          lightweight version of the current MI command system.
//--
class CMICmdCmdGdbInfo : public CMICmdBase {
  // Statics:
public:
  // Required by the CMICmdFactory when registering *this command
  static CMICmdBase *CreateSelf();

  // Methods:
public:
  /* ctor */ CMICmdCmdGdbInfo();

  // Overridden:
public:
  // From CMICmdInvoker::ICmd
  bool Execute() override;
  bool Acknowledge() override;
  bool ParseArgs() override;
  // From CMICmnBase
  /* dtor */ ~CMICmdCmdGdbInfo() override;

  // Typedefs:
private:
  typedef bool (CMICmdCmdGdbInfo::*FnPrintPtr)();
  typedef std::map<CMIUtilString, FnPrintPtr> MapPrintFnNameToPrintFn_t;

  // Methods:
private:
  bool GetPrintFn(const CMIUtilString &vrPrintFnName, FnPrintPtr &vrwpFn) const;
  bool PrintFnSharedLibrary();

  // Attributes:
private:
  const static MapPrintFnNameToPrintFn_t ms_mapPrintFnNameToPrintFn;
  //
  const CMIUtilString m_constStrArgNamedPrint;
  bool m_bPrintFnRecognised; // True = This command has a function with a name
                             // that matches the Print argument, false = not
                             // found
  bool m_bPrintFnSuccessful; // True = The print function completed its task ok,
                             // false = function failed for some reason
  CMIUtilString m_strPrintFnName;
  CMIUtilString m_strPrintFnError;
};
