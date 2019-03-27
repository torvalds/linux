//===-- MICmdCmdData.h ------------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

// Overview:    CMICmdCmdDataEvaluateExpression     interface.
//              CMICmdCmdDataDisassemble            interface.
//              CMICmdCmdDataReadMemoryBytes        interface.
//              CMICmdCmdDataReadMemory             interface.
//              CMICmdCmdDataListRegisterNames      interface.
//              CMICmdCmdDataListRegisterValues     interface.
//              CMICmdCmdDataListRegisterChanged    interface.
//              CMICmdCmdDataWriteMemoryBytes       interface.
//              CMICmdCmdDataWriteMemory            interface.
//              CMICmdCmdDataInfoLine               interface.
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
//

#pragma once

// Third party headers:
#include "lldb/API/SBError.h"

// In-house headers:
#include "MICmdBase.h"
#include "MICmnLLDBDebugSessionInfoVarObj.h"
#include "MICmnMIValueList.h"
#include "MICmnMIValueTuple.h"
#include "MICmnMIResultRecord.h"

//++
//============================================================================
// Details: MI command class. MI commands derived from the command base class.
//          *this class implements MI command "data-evaluate-expression".
//--
class CMICmdCmdDataEvaluateExpression : public CMICmdBase {
  // Statics:
public:
  // Required by the CMICmdFactory when registering *this command
  static CMICmdBase *CreateSelf();

  // Methods:
public:
  /* ctor */ CMICmdCmdDataEvaluateExpression();

  // Overridden:
public:
  // From CMICmdInvoker::ICmd
  bool Execute() override;
  bool Acknowledge() override;
  bool ParseArgs() override;
  // From CMICmnBase
  /* dtor */ ~CMICmdCmdDataEvaluateExpression() override;

  // Methods:
private:
  bool HaveInvalidCharacterInExpression(const CMIUtilString &vrExpr,
                                        char &vrwInvalidChar);

  // Attributes:
private:
  bool m_bExpressionValid;     // True = yes is valid, false = not valid
  bool m_bEvaluatedExpression; // True = yes is expression evaluated, false =
                               // failed
  lldb::SBError m_Error;       // Status object, which is examined when
                               // m_bEvaluatedExpression is false
  CMIUtilString m_strValue;
  CMICmnMIValueTuple m_miValueTuple;
  bool m_bFoundInvalidChar; // True = yes found unexpected character in the
                            // expression, false = all ok
  char m_cExpressionInvalidChar;
  const CMIUtilString m_constStrArgExpr;
};

//++
//============================================================================
// Details: MI command class. MI commands derived from the command base class.
//          *this class implements MI command "data-disassemble".
//--
class CMICmdCmdDataDisassemble : public CMICmdBase {
  // Statics:
public:
  // Required by the CMICmdFactory when registering *this command
  static CMICmdBase *CreateSelf();

  // Methods:
public:
  /* ctor */ CMICmdCmdDataDisassemble();

  // Overridden:
public:
  // From CMICmdInvoker::ICmd
  bool Execute() override;
  bool Acknowledge() override;
  bool ParseArgs() override;
  // From CMICmnBase
  /* dtor */ ~CMICmdCmdDataDisassemble() override;

  // Attributes:
private:
  const CMIUtilString
      m_constStrArgAddrStart; // MI spec non mandatory, *this command mandatory
  const CMIUtilString
      m_constStrArgAddrEnd; // MI spec non mandatory, *this command mandatory
  const CMIUtilString m_constStrArgMode;
  CMICmnMIValueList m_miValueList;
};

//++
//============================================================================
// Details: MI command class. MI commands derived from the command base class.
//          *this class implements MI command "data-read-memory-bytes".
//--
class CMICmdCmdDataReadMemoryBytes : public CMICmdBase {
  // Statics:
public:
  // Required by the CMICmdFactory when registering *this command
  static CMICmdBase *CreateSelf();

  // Methods:
public:
  /* ctor */ CMICmdCmdDataReadMemoryBytes();

  // Overridden:
public:
  // From CMICmdInvoker::ICmd
  bool Execute() override;
  bool Acknowledge() override;
  bool ParseArgs() override;
  // From CMICmnBase
  /* dtor */ ~CMICmdCmdDataReadMemoryBytes() override;

  // Attributes:
private:
  const CMIUtilString m_constStrArgByteOffset;
  const CMIUtilString m_constStrArgAddrExpr;
  const CMIUtilString m_constStrArgNumBytes;
  unsigned char *m_pBufferMemory;
  MIuint64 m_nAddrStart;
  MIuint64 m_nAddrNumBytesToRead;
};

//++
//============================================================================
// Details: MI command class. MI commands derived from the command base class.
//          *this class implements MI command "data-read-memory".
//--
class CMICmdCmdDataReadMemory : public CMICmdBase {
  // Statics:
public:
  // Required by the CMICmdFactory when registering *this command
  static CMICmdBase *CreateSelf();

  // Methods:
public:
  /* ctor */ CMICmdCmdDataReadMemory();

  // Overridden:
public:
  // From CMICmdInvoker::ICmd
  bool Execute() override;
  bool Acknowledge() override;
  // From CMICmnBase
  /* dtor */ ~CMICmdCmdDataReadMemory() override;
};

//++
//============================================================================
// Details: MI command class. MI commands derived from the command base class.
//          *this class implements MI command "data-list-register-names".
//--
class CMICmdCmdDataListRegisterNames : public CMICmdBase {
  // Statics:
public:
  // Required by the CMICmdFactory when registering *this command
  static CMICmdBase *CreateSelf();

  // Methods:
public:
  /* ctor */ CMICmdCmdDataListRegisterNames();

  // Overridden:
public:
  // From CMICmdInvoker::ICmd
  bool Execute() override;
  bool Acknowledge() override;
  bool ParseArgs() override;
  // From CMICmnBase
  /* dtor */ ~CMICmdCmdDataListRegisterNames() override;

  // Methods:
private:
  lldb::SBValue GetRegister(const MIuint vRegisterIndex) const;

  // Attributes:
private:
  const CMIUtilString m_constStrArgRegNo; // Not handled by *this command
  CMICmnMIValueList m_miValueList;
};

//++
//============================================================================
// Details: MI command class. MI commands derived from the command base class.
//          *this class implements MI command "data-list-register-values".
//--
class CMICmdCmdDataListRegisterValues : public CMICmdBase {
  // Statics:
public:
  // Required by the CMICmdFactory when registering *this command
  static CMICmdBase *CreateSelf();

  // Methods:
public:
  /* ctor */ CMICmdCmdDataListRegisterValues();

  // Overridden:
public:
  // From CMICmdInvoker::ICmd
  bool Execute() override;
  bool Acknowledge() override;
  bool ParseArgs() override;
  // From CMICmnBase
  /* dtor */ ~CMICmdCmdDataListRegisterValues() override;

  // Methods:
private:
  lldb::SBValue GetRegister(const MIuint vRegisterIndex) const;
  void AddToOutput(const MIuint vnIndex, const lldb::SBValue &vrValue,
                   CMICmnLLDBDebugSessionInfoVarObj::varFormat_e veVarFormat);

  // Attributes:
private:
  const CMIUtilString m_constStrArgSkip; // Not handled by *this command
  const CMIUtilString m_constStrArgFormat;
  const CMIUtilString m_constStrArgRegNo;
  CMICmnMIValueList m_miValueList;
};

//++
//============================================================================
// Details: MI command class. MI commands derived from the command base class.
//          *this class implements MI command "data-list-changed-registers".
//--
class CMICmdCmdDataListRegisterChanged : public CMICmdBase {
  // Statics:
public:
  // Required by the CMICmdFactory when registering *this command
  static CMICmdBase *CreateSelf();

  // Methods:
public:
  /* ctor */ CMICmdCmdDataListRegisterChanged();

  // Overridden:
public:
  // From CMICmdInvoker::ICmd
  bool Execute() override;
  bool Acknowledge() override;
  // From CMICmnBase
  /* dtor */ ~CMICmdCmdDataListRegisterChanged() override;
};

//++
//============================================================================
// Details: MI command class. MI commands derived from the command base class.
//          *this class implements MI command "data-read-memory-bytes".
//--
class CMICmdCmdDataWriteMemoryBytes : public CMICmdBase {
  // Statics:
public:
  // Required by the CMICmdFactory when registering *this command
  static CMICmdBase *CreateSelf();

  // Methods:
public:
  /* ctor */ CMICmdCmdDataWriteMemoryBytes();

  // Overridden:
public:
  // From CMICmdInvoker::ICmd
  bool Execute() override;
  bool Acknowledge() override;
  bool ParseArgs() override;
  // From CMICmnBase
  /* dtor */ ~CMICmdCmdDataWriteMemoryBytes() override;

  // Attributes:
private:
  const CMIUtilString m_constStrArgAddr;
  const CMIUtilString m_constStrArgContents;
  const CMIUtilString m_constStrArgCount;
  CMIUtilString m_strContents;
};

//++
//============================================================================
// Details: MI command class. MI commands derived from the command base class.
//          *this class implements MI command "data-read-memory".
//          Not specified in MI spec but Eclipse gives *this command.
//--
class CMICmdCmdDataWriteMemory : public CMICmdBase {
  // Statics:
public:
  // Required by the CMICmdFactory when registering *this command
  static CMICmdBase *CreateSelf();

  // Methods:
public:
  /* ctor */ CMICmdCmdDataWriteMemory();

  // Overridden:
public:
  // From CMICmdInvoker::ICmd
  bool Execute() override;
  bool Acknowledge() override;
  bool ParseArgs() override;
  // From CMICmnBase
  /* dtor */ ~CMICmdCmdDataWriteMemory() override;

  // Attributes:
private:
  const CMIUtilString m_constStrArgOffset; // Not specified in MI spec but
                                           // Eclipse gives this option.
  const CMIUtilString m_constStrArgAddr; // Not specified in MI spec but Eclipse
                                         // gives this option.
  const CMIUtilString
      m_constStrArgD; // Not specified in MI spec but Eclipse gives this option.
  const CMIUtilString m_constStrArgNumber;   // Not specified in MI spec but
                                             // Eclipse gives this option.
  const CMIUtilString m_constStrArgContents; // Not specified in MI spec but
                                             // Eclipse gives this option.
  MIuint64 m_nAddr;
  CMIUtilString m_strContents;
  MIuint64 m_nCount;
  unsigned char *m_pBufferMemory;
};

//++
//============================================================================
// Details: MI command class. MI commands derived from the command base class.
//          *this class implements MI command "data-info-line".
//          See MIExtensions.txt for details.
//--
class CMICmdCmdDataInfoLine : public CMICmdBase {
  // Statics:
public:
  // Required by the CMICmdFactory when registering *this command
  static CMICmdBase *CreateSelf();

  // Methods:
public:
  /* ctor */ CMICmdCmdDataInfoLine();

  // Overridden:
public:
  // From CMICmdInvoker::ICmd
  bool Execute() override;
  bool Acknowledge() override;
  bool ParseArgs() override;
  // From CMICmnBase
  /* dtor */ ~CMICmdCmdDataInfoLine() override;

  // Attributes:
private:
  const CMIUtilString m_constStrArgLocation;
  CMICmnMIResultRecord m_resultRecord;
};
