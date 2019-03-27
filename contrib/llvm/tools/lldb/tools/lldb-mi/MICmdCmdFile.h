//===-- MICmdCmdFile.h ------------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

// Overview:    CMICmdCmdFileExecAndSymbols     interface.
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
//          *this class implements MI command "file-exec-and-symbols".
//          This command does not follow the MI documentation exactly.
// Gotchas: This command has additional flags that were not available in GDB MI.
//          See MIextensions.txt for details.
//--
class CMICmdCmdFileExecAndSymbols : public CMICmdBase {
  // Statics:
public:
  // Required by the CMICmdFactory when registering *this command
  static CMICmdBase *CreateSelf();

  // Methods:
public:
  /* ctor */ CMICmdCmdFileExecAndSymbols();

  // Overridden:
public:
  // From CMICmdInvoker::ICmd
  bool Execute() override;
  bool Acknowledge() override;
  bool ParseArgs() override;
  // From CMICmnBase
  /* dtor */ ~CMICmdCmdFileExecAndSymbols() override;
  bool GetExitAppOnCommandFailure() const override;

  // Attributes:
private:
  const CMIUtilString m_constStrArgNameFile;
  const CMIUtilString
      m_constStrArgNamedPlatformName; // Added to support iOS platform selection
  const CMIUtilString m_constStrArgNamedRemotePath; // Added to support iOS
                                                    // device remote file
                                                    // location
};
