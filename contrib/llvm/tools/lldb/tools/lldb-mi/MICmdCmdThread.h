//===-- MICmdCmdThread.h ----------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

// Overview:    CMICmdCmdThreadInfo interface.
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
//          *this class implements MI command "thread-info".
//--
class CMICmdCmdThreadInfo : public CMICmdBase {
  // Statics:
public:
  // Required by the CMICmdFactory when registering *this command
  static CMICmdBase *CreateSelf();

  // Methods:
public:
  /* ctor */ CMICmdCmdThreadInfo();

  // Overridden:
public:
  // From CMICmdInvoker::ICmd
  bool Execute() override;
  bool Acknowledge() override;
  bool ParseArgs() override;
  // From CMICmnBase
  /* dtor */ ~CMICmdCmdThreadInfo() override;

  // Typedefs:
private:
  typedef std::vector<CMICmnMIValueTuple> VecMIValueTuple_t;

  // Attributes:
private:
  CMICmnMIValueTuple m_miValueTupleThread;
  bool m_bSingleThread;  // True = yes single thread, false = multiple threads
  bool m_bThreadInvalid; // True = invalid, false = ok
  VecMIValueTuple_t m_vecMIValueTuple;
  const CMIUtilString m_constStrArgNamedThreadId;

  // mi value of current-thread-id if multiple threads are requested
  bool m_bHasCurrentThread;
  CMICmnMIValue m_miValueCurrThreadId;
};
