//===-- MICmdCmdStack.h -----------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

// Overview:    CMICmdCmdStackInfoDepth         interface.
//              CMICmdCmdStackInfoFrame         interface.
//              CMICmdCmdStackListFrames        interface.
//              CMICmdCmdStackListArguments     interface.
//              CMICmdCmdStackListLocals        interface.
//              CMICmdCmdStackSelectFrame       interface.
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
//          *this class implements MI command "stack-info-depth".
//--
class CMICmdCmdStackInfoDepth : public CMICmdBase {
  // Statics:
public:
  // Required by the CMICmdFactory when registering *this command
  static CMICmdBase *CreateSelf();

  // Methods:
public:
  /* ctor */ CMICmdCmdStackInfoDepth();

  // Overridden:
public:
  // From CMICmdInvoker::ICmd
  bool Execute() override;
  bool Acknowledge() override;
  bool ParseArgs() override;
  // From CMICmnBase
  /* dtor */ ~CMICmdCmdStackInfoDepth() override;

  // Attributes:
private:
  MIuint m_nThreadFrames;
  const CMIUtilString m_constStrArgMaxDepth; // Not handled by *this command
};

//++
//============================================================================
// Details: MI command class. MI commands derived from the command base class.
//          *this class implements MI command "stack-info-frame".
//--
class CMICmdCmdStackInfoFrame : public CMICmdBase {
  // Statics:
public:
  // Required by the CMICmdFactory when registering *this command
  static CMICmdBase *CreateSelf();

  // Methods:
public:
  /* ctor */ CMICmdCmdStackInfoFrame();

  // Overridden:
public:
  // From CMICmdInvoker::ICmd
  bool Execute() override;
  bool Acknowledge() override;
  bool ParseArgs() override;
  // From CMICmnBase
  /* dtor */ ~CMICmdCmdStackInfoFrame() override;

  // Attributes:
private:
  CMICmnMIValueTuple m_miValueTuple;
};

//++
//============================================================================
// Details: MI command class. MI commands derived from the command base class.
//          *this class implements MI command "stack-list-frames".
//--
class CMICmdCmdStackListFrames : public CMICmdBase {
  // Statics:
public:
  // Required by the CMICmdFactory when registering *this command
  static CMICmdBase *CreateSelf();

  // Methods:
public:
  /* ctor */ CMICmdCmdStackListFrames();

  // Overridden:
public:
  // From CMICmdInvoker::ICmd
  bool Execute() override;
  bool Acknowledge() override;
  bool ParseArgs() override;
  // From CMICmnBase
  /* dtor */ ~CMICmdCmdStackListFrames() override;

  // Typedefs:
private:
  typedef std::vector<CMICmnMIValueResult> VecMIValueResult_t;

  // Attributes:
private:
  MIuint m_nThreadFrames;
  VecMIValueResult_t m_vecMIValueResult;
  const CMIUtilString m_constStrArgFrameLow;
  const CMIUtilString m_constStrArgFrameHigh;
};

//++
//============================================================================
// Details: MI command class. MI commands derived from the command base class.
//          *this class implements MI command "stack-list-arguments".
//--
class CMICmdCmdStackListArguments : public CMICmdBase {
  // Statics:
public:
  // Required by the CMICmdFactory when registering *this command
  static CMICmdBase *CreateSelf();

  // Methods:
public:
  /* ctor */ CMICmdCmdStackListArguments();

  // Overridden:
public:
  // From CMICmdInvoker::ICmd
  bool Execute() override;
  bool Acknowledge() override;
  bool ParseArgs() override;
  // From CMICmnBase
  /* dtor */ ~CMICmdCmdStackListArguments() override;

  // Attributes:
private:
  bool m_bThreadInvalid; // True = yes invalid thread, false = thread object
                         // valid
  CMICmnMIValueList m_miValueList;
  const CMIUtilString m_constStrArgPrintValues;
  const CMIUtilString m_constStrArgFrameLow;
  const CMIUtilString m_constStrArgFrameHigh;
};

//++
//============================================================================
// Details: MI command class. MI commands derived from the command base class.
//          *this class implements MI command "stack-list-locals".
//--
class CMICmdCmdStackListLocals : public CMICmdBase {
  // Statics:
public:
  // Required by the CMICmdFactory when registering *this command
  static CMICmdBase *CreateSelf();

  // Methods:
public:
  /* ctor */ CMICmdCmdStackListLocals();

  // Overridden:
public:
  // From CMICmdInvoker::ICmd
  bool Execute() override;
  bool Acknowledge() override;
  bool ParseArgs() override;
  // From CMICmnBase
  /* dtor */ ~CMICmdCmdStackListLocals() override;

  // Attributes:
private:
  bool m_bThreadInvalid; // True = yes invalid thread, false = thread object
                         // valid
  CMICmnMIValueList m_miValueList;
  const CMIUtilString m_constStrArgPrintValues;
};

//++
//============================================================================
// Details: MI command class. MI commands derived from the command base class.
//          *this class implements MI command "stack-list-variables".
//--
class CMICmdCmdStackListVariables : public CMICmdBase {
  // Statics:
public:
  // Required by the CMICmdFactory when registering *this command
  static CMICmdBase *CreateSelf();

  // Methods:
public:
  /* ctor */ CMICmdCmdStackListVariables();

  // Overridden:
public:
  // From CMICmdInvoker::ICmd
  bool Execute() override;
  bool Acknowledge() override;
  bool ParseArgs() override;
  // From CMICmnBase
  /* dtor */ ~CMICmdCmdStackListVariables() override;

  // Attributes
private:
  bool m_bThreadInvalid; // True = yes invalid thread, false = thread object
                         // valid
  CMICmnMIValueList m_miValueList;
  const CMIUtilString m_constStrArgPrintValues;
};

//++
//============================================================================
// Details: MI command class. MI commands derived from the command base class.
//          *this class implements MI command "stack-select-frame".
//--
class CMICmdCmdStackSelectFrame : public CMICmdBase {
  // Statics:
public:
  // Required by the CMICmdFactory when registering *this command
  static CMICmdBase *CreateSelf();

  // Methods:
public:
  /* ctor */ CMICmdCmdStackSelectFrame();

  // Overridden:
public:
  // From CMICmdInvoker::ICmd
  bool Execute() override;
  bool Acknowledge() override;
  bool ParseArgs() override;
  // From CMICmnBase
  /* dtor */ ~CMICmdCmdStackSelectFrame() override;

  // Attributes:
private:
  bool m_bFrameInvalid; // True = yes invalid frame, false = ok
  const CMIUtilString m_constStrArgFrameId;
};
