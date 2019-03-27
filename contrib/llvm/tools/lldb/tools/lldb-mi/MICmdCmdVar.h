//===-- MICmdCmdVar.h -------------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

// Overview:    CMICmdCmdVarCreate              interface.
//              CMICmdCmdVarUpdate              interface.
//              CMICmdCmdVarDelete              interface.
//              CMICmdCmdVarAssign              interface.
//              CMICmdCmdVarSetFormat           interface.
//              CMICmdCmdVarListChildren        interface.
//              CMICmdCmdVarEvaluateExpression  interface.
//              CMICmdCmdVarInfoPathExpression  interface.
//              CMICmdCmdVarShowAttributes      interface.
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
#include "MICmnLLDBDebugSessionInfo.h"
#include "MICmnLLDBDebugSessionInfoVarObj.h"
#include "MICmnMIValueList.h"
#include "MICmnMIValueTuple.h"

// Declarations:
class CMICmnLLDBDebugSessionInfoVarObj;

//++
//============================================================================
// Details: MI command class. MI commands derived from the command base class.
//          *this class implements MI command "var-create".
//--
class CMICmdCmdVarCreate : public CMICmdBase {
  // Statics:
public:
  // Required by the CMICmdFactory when registering *this command
  static CMICmdBase *CreateSelf();

  // Methods:
public:
  /* ctor */ CMICmdCmdVarCreate();

  // Overridden:
public:
  // From CMICmdInvoker::ICmd
  bool Execute() override;
  bool Acknowledge() override;
  bool ParseArgs() override;

  // Overridden:
public:
  // From CMICmnBase
  /* dtor */ ~CMICmdCmdVarCreate() override;

  // Methods:
private:
  void CompleteSBValue(lldb::SBValue &vrwValue);

  // Attribute:
private:
  CMIUtilString m_strVarName;
  MIuint m_nChildren;
  MIuint64 m_nThreadId;
  CMIUtilString m_strType;
  bool m_bValid; // True = Variable is valid, false = not valid
  CMIUtilString m_strExpression;
  CMIUtilString m_strValue;
  const CMIUtilString m_constStrArgName;
  const CMIUtilString m_constStrArgFrameAddr;
  const CMIUtilString m_constStrArgExpression;
};

//++
//============================================================================
// Details: MI command class. MI commands derived from the command base class.
//          *this class implements MI command "var-update".
//--
class CMICmdCmdVarUpdate : public CMICmdBase {
  // Statics:
public:
  // Required by the CMICmdFactory when registering *this command
  static CMICmdBase *CreateSelf();

  // Methods:
public:
  /* ctor */ CMICmdCmdVarUpdate();

  // Overridden:
public:
  // From CMICmdInvoker::ICmd
  bool Execute() override;
  bool Acknowledge() override;
  bool ParseArgs() override;

  // Overridden:
public:
  // From CMICmnBase
  /* dtor */ ~CMICmdCmdVarUpdate() override;

  // Methods:
private:
  bool ExamineSBValueForChange(lldb::SBValue &vrwValue, bool &vrwbChanged);
  void MIFormResponse(const CMIUtilString &vrStrVarName,
                      const char *const vpValue,
                      const CMIUtilString &vrStrScope);

  // Attribute:
private:
  const CMIUtilString m_constStrArgPrintValues;
  const CMIUtilString m_constStrArgName;
  bool m_bValueChanged; // True = yes value changed, false = no change
  CMICmnMIValueList m_miValueList;
};

//++
//============================================================================
// Details: MI command class. MI commands derived from the command base class.
//          *this class implements MI command "var-delete".
//--
class CMICmdCmdVarDelete : public CMICmdBase {
  // Statics:
public:
  // Required by the CMICmdFactory when registering *this command
  static CMICmdBase *CreateSelf();

  // Methods:
public:
  /* ctor */ CMICmdCmdVarDelete();

  // Overridden:
public:
  // From CMICmdInvoker::ICmd
  bool Execute() override;
  bool Acknowledge() override;
  bool ParseArgs() override;
  // From CMICmnBase
  /* dtor */ ~CMICmdCmdVarDelete() override;

  // Attribute:
private:
  const CMIUtilString m_constStrArgName;
};

//++
//============================================================================
// Details: MI command class. MI commands derived from the command base class.
//          *this class implements MI command "var-assign".
//--
class CMICmdCmdVarAssign : public CMICmdBase {
  // Statics:
public:
  // Required by the CMICmdFactory when registering *this command
  static CMICmdBase *CreateSelf();

  // Methods:
public:
  /* ctor */ CMICmdCmdVarAssign();

  // Overridden:
public:
  // From CMICmdInvoker::ICmd
  bool Execute() override;
  bool Acknowledge() override;
  bool ParseArgs() override;
  // From CMICmnBase
  /* dtor */ ~CMICmdCmdVarAssign() override;

  // Attributes:
private:
  bool m_bOk; // True = success, false = failure
  CMIUtilString m_varObjName;
  const CMIUtilString m_constStrArgName;
  const CMIUtilString m_constStrArgExpression;
};

//++
//============================================================================
// Details: MI command class. MI commands derived from the command base class.
//          *this class implements MI command "var-set-format".
//--
class CMICmdCmdVarSetFormat : public CMICmdBase {
  // Statics:
public:
  // Required by the CMICmdFactory when registering *this command
  static CMICmdBase *CreateSelf();

  // Methods:
public:
  /* ctor */ CMICmdCmdVarSetFormat();

  // Overridden:
public:
  // From CMICmdInvoker::ICmd
  bool Execute() override;
  bool Acknowledge() override;
  bool ParseArgs() override;
  // From CMICmnBase
  /* dtor */ ~CMICmdCmdVarSetFormat() override;

  // Attributes:
private:
  CMIUtilString m_varObjName;
  const CMIUtilString m_constStrArgName;
  const CMIUtilString m_constStrArgFormatSpec;
};

//++
//============================================================================
// Details: MI command class. MI commands derived from the command base class.
//          *this class implements MI command "var-list-children".
//--
class CMICmdCmdVarListChildren : public CMICmdBase {
  // Statics:
public:
  // Required by the CMICmdFactory when registering *this command
  static CMICmdBase *CreateSelf();

  // Methods:
public:
  /* ctor */ CMICmdCmdVarListChildren();

  // Overridden:
public:
  // From CMICmdInvoker::ICmd
  bool Execute() override;
  bool Acknowledge() override;
  bool ParseArgs() override;
  // From CMICmnBase
  /* dtor */ ~CMICmdCmdVarListChildren() override;

  // Attributes:
private:
  const CMIUtilString m_constStrArgPrintValues;
  const CMIUtilString m_constStrArgName;
  const CMIUtilString m_constStrArgFrom;
  const CMIUtilString m_constStrArgTo;
  bool m_bValueValid; // True = yes SBValue object is valid, false = not valid
  MIuint m_nChildren;
  CMICmnMIValueList m_miValueList;
  bool m_bHasMore;
};

//++
//============================================================================
// Details: MI command class. MI commands derived from the command base class.
//          *this class implements MI command "var-evaluate-expression".
//--
class CMICmdCmdVarEvaluateExpression : public CMICmdBase {
  // Statics:
public:
  // Required by the CMICmdFactory when registering *this command
  static CMICmdBase *CreateSelf();

  // Methods:
public:
  /* ctor */ CMICmdCmdVarEvaluateExpression();

  // Overridden:
public:
  // From CMICmdInvoker::ICmd
  bool Execute() override;
  bool Acknowledge() override;
  bool ParseArgs() override;
  // From CMICmnBase
  /* dtor */ ~CMICmdCmdVarEvaluateExpression() override;

  // Attributes:
private:
  bool m_bValueValid; // True = yes SBValue object is valid, false = not valid
  CMIUtilString m_varObjName;
  const CMIUtilString m_constStrArgFormatSpec; // Not handled by *this command
  const CMIUtilString m_constStrArgName;
};

//++
//============================================================================
// Details: MI command class. MI commands derived from the command base class.
//          *this class implements MI command "var-info-path-expression".
//--
class CMICmdCmdVarInfoPathExpression : public CMICmdBase {
  // Statics:
public:
  // Required by the CMICmdFactory when registering *this command
  static CMICmdBase *CreateSelf();

  // Methods:
public:
  /* ctor */ CMICmdCmdVarInfoPathExpression();

  // Overridden:
public:
  // From CMICmdInvoker::ICmd
  bool Execute() override;
  bool Acknowledge() override;
  bool ParseArgs() override;
  // From CMICmnBase
  /* dtor */ ~CMICmdCmdVarInfoPathExpression() override;

  // Attributes:
private:
  bool m_bValueValid; // True = yes SBValue object is valid, false = not valid
  CMIUtilString m_strPathExpression;
  const CMIUtilString m_constStrArgName;
};

//++
//============================================================================
// Details: MI command class. MI commands derived from the command base class.
//          *this class implements MI command "var-show-attributes".
//--
class CMICmdCmdVarShowAttributes : public CMICmdBase {
  // Statics:
public:
  // Required by the CMICmdFactory when registering *this command
  static CMICmdBase *CreateSelf();

  // Methods:
public:
  /* ctor */ CMICmdCmdVarShowAttributes();

  // Overridden:
public:
  // From CMICmdInvoker::ICmd
  bool Execute() override;
  bool Acknowledge() override;
  bool ParseArgs() override;
  // From CMICmnBase
  /* dtor */ ~CMICmdCmdVarShowAttributes() override;

  // Attributes:
private:
  const CMIUtilString m_constStrArgName;
};
