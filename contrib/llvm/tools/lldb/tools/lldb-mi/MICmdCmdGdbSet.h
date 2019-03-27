//===-- MICmdCmdGdbSet.h ----------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

// Overview:    CMICmdCmdGdbSet interface.
//
//              To implement new MI commands, derive a new command class from
//              the command base
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

//++
//============================================================================
// Details: MI command class. MI commands derived from the command base class.
//          *this class implements MI command "gdb-set".
//          This command does not follow the MI documentation exactly. While
//          *this
//          command is implemented it does not do anything with the gdb-set
//          variable past in.
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
class CMICmdCmdGdbSet : public CMICmdBase {
  // Statics:
public:
  // Required by the CMICmdFactory when registering *this command
  static CMICmdBase *CreateSelf();

  // Methods:
public:
  /* ctor */ CMICmdCmdGdbSet();

  // Overridden:
public:
  // From CMICmdInvoker::ICmd
  bool Execute() override;
  bool Acknowledge() override;
  bool ParseArgs() override;
  // From CMICmnBase
  /* dtor */ ~CMICmdCmdGdbSet() override;

  // Typedefs:
private:
  typedef bool (CMICmdCmdGdbSet::*FnGdbOptionPtr)(
      const CMIUtilString::VecString_t &vrWords);
  typedef std::map<CMIUtilString, FnGdbOptionPtr>
      MapGdbOptionNameToFnGdbOptionPtr_t;

  // Methods:
private:
  bool GetOptionFn(const CMIUtilString &vrGdbOptionName,
                   FnGdbOptionPtr &vrwpFn) const;
  bool OptionFnTargetAsync(const CMIUtilString::VecString_t &vrWords);
  bool OptionFnPrint(const CMIUtilString::VecString_t &vrWords);
  bool OptionFnSolibSearchPath(const CMIUtilString::VecString_t &vrWords);
  bool OptionFnOutputRadix(const CMIUtilString::VecString_t &vrWords);
  bool OptionFnDisassemblyFlavor(const CMIUtilString::VecString_t &vrWords);
  bool OptionFnBreakpoint(const CMIUtilString::VecString_t &vrWords);
  bool OptionFnFallback(const CMIUtilString::VecString_t &vrWords);

  // Attributes:
private:
  const static MapGdbOptionNameToFnGdbOptionPtr_t
      ms_mapGdbOptionNameToFnGdbOptionPtr;
  //
  const CMIUtilString m_constStrArgNamedGdbOption;
  bool m_bGdbOptionRecognised; // True = This command has a function with a name
                               // that matches the Print argument, false = not
                               // found
  bool m_bGdbOptionFnSuccessful; // True = The print function completed its task
                                 // ok, false = function failed for some reason
  bool m_bGbbOptionFnHasError;   // True = The option function has an error
                                 // condition (not the command!), false = option
                                 // function ok.
  CMIUtilString m_strGdbOptionName;
  CMIUtilString m_strGdbOptionFnError;
};
