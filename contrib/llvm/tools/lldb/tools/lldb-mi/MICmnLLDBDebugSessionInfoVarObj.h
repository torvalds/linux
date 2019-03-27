//===-- MICmnLLDBDebugSessionInfoVarObj.h -----------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#pragma once

// Third Party Headers:
#include "lldb/API/SBValue.h"
#include <map>

// In-house headers:
#include "MIUtilString.h"

//++
//============================================================================
// Details: MI debug session variable object. The static functionality in *this
//          class manages a map container of *these variable objects.
//--
class CMICmnLLDBDebugSessionInfoVarObj {
  // Enums:
public:
  //++ ----------------------------------------------------------------------
  // Details: Enumeration of a variable type that is not a composite type
  //--
  enum varFormat_e {
    // CODETAG_SESSIONINFO_VARFORMAT_ENUM
    // *** Order is import here ***
    eVarFormat_Invalid = 0,
    eVarFormat_Binary,
    eVarFormat_Octal,
    eVarFormat_Decimal,
    eVarFormat_Hex,
    eVarFormat_Natural,
    eVarFormat_count // Always last one
  };

  //++ ----------------------------------------------------------------------
  // Details: Enumeration of a variable type by composite or internal type
  //--
  enum varType_e {
    eVarType_InValid = 0,
    eVarType_Composite, // i.e. struct
    eVarType_Internal,  // i.e. int
    eVarType_count      // Always last one
  };

  // Statics:
public:
  static varFormat_e GetVarFormatForString(const CMIUtilString &vrStrFormat);
  static varFormat_e GetVarFormatForChar(char vcFormat);
  static CMIUtilString GetValueStringFormatted(const lldb::SBValue &vrValue,
                                               const varFormat_e veVarFormat);
  static void VarObjAdd(const CMICmnLLDBDebugSessionInfoVarObj &vrVarObj);
  static void VarObjDelete(const CMIUtilString &vrVarName);
  static bool VarObjGet(const CMIUtilString &vrVarName,
                        CMICmnLLDBDebugSessionInfoVarObj &vrwVarObj);
  static void VarObjUpdate(const CMICmnLLDBDebugSessionInfoVarObj &vrVarObj);
  static void VarObjIdInc();
  static MIuint VarObjIdGet();
  static void VarObjIdResetToZero();
  static void VarObjClear();
  static void VarObjSetFormat(varFormat_e eDefaultFormat);

  // Methods:
public:
  /* ctor */ CMICmnLLDBDebugSessionInfoVarObj();
  /* ctor */ CMICmnLLDBDebugSessionInfoVarObj(
      const CMIUtilString &vrStrNameReal, const CMIUtilString &vrStrName,
      const lldb::SBValue &vrValue);
  /* ctor */ CMICmnLLDBDebugSessionInfoVarObj(
      const CMIUtilString &vrStrNameReal, const CMIUtilString &vrStrName,
      const lldb::SBValue &vrValue, const CMIUtilString &vrStrVarObjParentName);
  /* ctor */ CMICmnLLDBDebugSessionInfoVarObj(
      const CMICmnLLDBDebugSessionInfoVarObj &vrOther);
  /* ctor */ CMICmnLLDBDebugSessionInfoVarObj(
      CMICmnLLDBDebugSessionInfoVarObj &vrOther);
  /* ctor */ CMICmnLLDBDebugSessionInfoVarObj(
      CMICmnLLDBDebugSessionInfoVarObj &&vrOther);
  //
  CMICmnLLDBDebugSessionInfoVarObj &
  operator=(const CMICmnLLDBDebugSessionInfoVarObj &vrOther);
  CMICmnLLDBDebugSessionInfoVarObj &
  operator=(CMICmnLLDBDebugSessionInfoVarObj &&vrwOther);
  //
  const CMIUtilString &GetName() const;
  const CMIUtilString &GetNameReal() const;
  const CMIUtilString &GetValueFormatted() const;
  lldb::SBValue &GetValue();
  const lldb::SBValue &GetValue() const;
  varType_e GetType() const;
  bool SetVarFormat(const varFormat_e veVarFormat);
  const CMIUtilString &GetVarParentName() const;
  void UpdateValue();

  // Overridden:
public:
  // From CMICmnBase
  /* dtor */ virtual ~CMICmnLLDBDebugSessionInfoVarObj();

  // Typedefs:
private:
  typedef std::map<CMIUtilString, CMICmnLLDBDebugSessionInfoVarObj>
      MapKeyToVarObj_t;
  typedef std::pair<CMIUtilString, CMICmnLLDBDebugSessionInfoVarObj>
      MapPairKeyToVarObj_t;

  // Statics:
private:
  static CMIUtilString GetStringFormatted(const MIuint64 vnValue,
                                          const char *vpStrValueNatural,
                                          varFormat_e veVarFormat);

  // Methods:
private:
  bool CopyOther(const CMICmnLLDBDebugSessionInfoVarObj &vrOther);
  bool MoveOther(CMICmnLLDBDebugSessionInfoVarObj &vrwOther);

  // Attributes:
private:
  static const char *ms_aVarFormatStrings[];
  static const char *ms_aVarFormatChars[];
  static MapKeyToVarObj_t ms_mapVarIdToVarObj;
  static MIuint ms_nVarUniqueId;
  static varFormat_e ms_eDefaultFormat; // overrides "natural" format
  //
  // *** Update the copy move constructors and assignment operator ***
  varFormat_e m_eVarFormat;
  varType_e m_eVarType;
  CMIUtilString m_strName;
  lldb::SBValue m_SBValue;
  CMIUtilString m_strNameReal;
  CMIUtilString m_strFormattedValue;
  CMIUtilString m_strVarObjParentName;
  // *** Update the copy move constructors and assignment operator ***
};
