//===-- MICmnLLDBUtilSBValue.h ----------------------------------*- C++ -*-===//
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

// In-house headers:
#include "MICmnMIValueTuple.h"
#include "MIDataTypes.h"

// Declarations:
class CMIUtilString;

//++
//============================================================================
// Details: Utility helper class to lldb::SBValue. Using a lldb::SBValue extract
//          value object information to help form verbose debug information.
//--
class CMICmnLLDBUtilSBValue {
  // Methods:
public:
  /* ctor */ CMICmnLLDBUtilSBValue(const lldb::SBValue &vrValue,
                                   const bool vbHandleCharType = false,
                                   const bool vbHandleArrayType = true);
  /* dtor */ ~CMICmnLLDBUtilSBValue();
  //
  CMIUtilString GetName() const;
  CMIUtilString GetValue(const bool vbExpandAggregates = false) const;
  CMIUtilString GetTypeName() const;
  CMIUtilString GetTypeNameDisplay() const;
  bool IsCharType() const;
  bool IsFirstChildCharType() const;
  bool IsPointeeCharType() const;
  bool IsIntegerType() const;
  bool IsPointerType() const;
  bool IsArrayType() const;
  bool IsLLDBVariable() const;
  bool IsNameUnknown() const;
  bool IsValueUnknown() const;
  bool IsValid() const;
  bool HasName() const;

  // Methods:
private:
  template <typename charT>
  CMIUtilString
  ReadCStringFromHostMemory(lldb::SBValue &vrValue,
                            const MIuint vnMaxLen = UINT32_MAX) const;
  bool GetSimpleValue(const bool vbHandleArrayType,
                      CMIUtilString &vrValue) const;
  bool GetCompositeValue(const bool vbPrintFieldNames,
                         CMICmnMIValueTuple &vwrMiValueTuple,
                         const MIuint vnDepth = 1) const;
  CMIUtilString
  GetValueSummary(bool valueOnly,
                  const CMIUtilString &failVal = CMIUtilString()) const;

  // Statics:
private:
  static bool IsCharBasicType(lldb::BasicType eType);

  // Attributes:
private:
  lldb::SBValue &m_rValue;
  bool m_bValidSBValue; // True = SBValue is a valid object, false = not valid.
  bool m_bHandleCharType;  // True = Yes return text molding to char type, false
                           // = just return data.
  bool m_bHandleArrayType; // True = Yes return special stub for array type,
                           // false = just return data.
};
