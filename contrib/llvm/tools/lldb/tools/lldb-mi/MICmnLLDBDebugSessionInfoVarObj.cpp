//===-- MICmnLLDBDebugSessionInfoVarObj.cpp ---------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

// In-house headers:
#include "MICmnLLDBDebugSessionInfoVarObj.h"
#include "MICmnLLDBProxySBValue.h"
#include "MICmnLLDBUtilSBValue.h"

// Instantiations:
const char *CMICmnLLDBDebugSessionInfoVarObj::ms_aVarFormatStrings[] = {
    // CODETAG_SESSIONINFO_VARFORMAT_ENUM
    // *** Order is import here.
    "<Invalid var format>", "binary", "octal", "decimal",
    "hexadecimal",          "natural"};
const char *CMICmnLLDBDebugSessionInfoVarObj::ms_aVarFormatChars[] = {
    // CODETAG_SESSIONINFO_VARFORMAT_ENUM
    // *** Order is import here.
    "<Invalid var format>", "t", "o", "d", "x", "N"};
CMICmnLLDBDebugSessionInfoVarObj::MapKeyToVarObj_t
    CMICmnLLDBDebugSessionInfoVarObj::ms_mapVarIdToVarObj;
MIuint CMICmnLLDBDebugSessionInfoVarObj::ms_nVarUniqueId = 0; // Index from 0
CMICmnLLDBDebugSessionInfoVarObj::varFormat_e
    CMICmnLLDBDebugSessionInfoVarObj::ms_eDefaultFormat = eVarFormat_Natural;

//++
//------------------------------------------------------------------------------------
// Details: CMICmnLLDBDebugSessionInfoVarObj constructor.
// Type:    Method.
// Args:    None.
// Return:  None.
// Throws:  None.
//--
CMICmnLLDBDebugSessionInfoVarObj::CMICmnLLDBDebugSessionInfoVarObj()
    : m_eVarFormat(eVarFormat_Natural), m_eVarType(eVarType_Internal) {
  // Do not call UpdateValue() in here as not necessary
}

//++
//------------------------------------------------------------------------------------
// Details: CMICmnLLDBDebugSessionInfoVarObj constructor.
// Type:    Method.
// Args:    vrStrNameReal   - (R) The actual name of the variable, the
// expression.
//          vrStrName       - (R) The name given for *this var object.
//          vrValue         - (R) The LLDB SBValue object represented by *this
//          object.
// Return:  None.
// Throws:  None.
//--
CMICmnLLDBDebugSessionInfoVarObj::CMICmnLLDBDebugSessionInfoVarObj(
    const CMIUtilString &vrStrNameReal, const CMIUtilString &vrStrName,
    const lldb::SBValue &vrValue)
    : m_eVarFormat(eVarFormat_Natural), m_eVarType(eVarType_Internal),
      m_strName(vrStrName), m_SBValue(vrValue), m_strNameReal(vrStrNameReal) {
  UpdateValue();
}

//++
//------------------------------------------------------------------------------------
// Details: CMICmnLLDBDebugSessionInfoVarObj constructor.
// Type:    Method.
// Args:    vrStrNameReal           - (R) The actual name of the variable, the
// expression.
//          vrStrName               - (R) The name given for *this var object.
//          vrValue                 - (R) The LLDB SBValue object represented by
//          *this object.
//          vrStrVarObjParentName   - (R) The var object parent to *this var
//          object (LLDB SBValue equivalent).
// Return:  None.
// Throws:  None.
//--
CMICmnLLDBDebugSessionInfoVarObj::CMICmnLLDBDebugSessionInfoVarObj(
    const CMIUtilString &vrStrNameReal, const CMIUtilString &vrStrName,
    const lldb::SBValue &vrValue, const CMIUtilString &vrStrVarObjParentName)
    : m_eVarFormat(eVarFormat_Natural), m_eVarType(eVarType_Internal),
      m_strName(vrStrName), m_SBValue(vrValue), m_strNameReal(vrStrNameReal),
      m_strVarObjParentName(vrStrVarObjParentName) {
  UpdateValue();
}

//++
//------------------------------------------------------------------------------------
// Details: CMICmnLLDBDebugSessionInfoVarObj copy constructor.
// Type:    Method.
// Args:    vrOther - (R) The object to copy from.
// Return:  None.
// Throws:  None.
//--
CMICmnLLDBDebugSessionInfoVarObj::CMICmnLLDBDebugSessionInfoVarObj(
    const CMICmnLLDBDebugSessionInfoVarObj &vrOther) {
  CopyOther(vrOther);
}

//++
//------------------------------------------------------------------------------------
// Details: CMICmnLLDBDebugSessionInfoVarObj copy constructor.
// Type:    Method.
// Args:    vrOther - (R) The object to copy from.
// Return:  None.
// Throws:  None.
//--
CMICmnLLDBDebugSessionInfoVarObj::CMICmnLLDBDebugSessionInfoVarObj(
    CMICmnLLDBDebugSessionInfoVarObj &vrOther) {
  CopyOther(vrOther);
}

//++
//------------------------------------------------------------------------------------
// Details: CMICmnLLDBDebugSessionInfoVarObj move constructor.
// Type:    Method.
// Args:    vrwOther    - (R) The object to copy from.
// Return:  None.
// Throws:  None.
//--
CMICmnLLDBDebugSessionInfoVarObj::CMICmnLLDBDebugSessionInfoVarObj(
    CMICmnLLDBDebugSessionInfoVarObj &&vrwOther) {
  MoveOther(vrwOther);
}

//++
//------------------------------------------------------------------------------------
// Details: CMICmnLLDBDebugSessionInfoVarObj assignment operator.
// Type:    Method.
// Args:    vrOther - (R) The object to copy from.
// Return:  CMICmnLLDBDebugSessionInfoVarObj & - Updated *this object.
// Throws:  None.
//--
CMICmnLLDBDebugSessionInfoVarObj &CMICmnLLDBDebugSessionInfoVarObj::
operator=(const CMICmnLLDBDebugSessionInfoVarObj &vrOther) {
  CopyOther(vrOther);

  return *this;
}

//++
//------------------------------------------------------------------------------------
// Details: CMICmnLLDBDebugSessionInfoVarObj assignment operator.
// Type:    Method.
// Args:    vrwOther    - (R) The object to copy from.
// Return:  CMICmnLLDBDebugSessionInfoVarObj & - Updated *this object.
// Throws:  None.
//--
CMICmnLLDBDebugSessionInfoVarObj &CMICmnLLDBDebugSessionInfoVarObj::
operator=(CMICmnLLDBDebugSessionInfoVarObj &&vrwOther) {
  MoveOther(vrwOther);

  return *this;
}

//++
//------------------------------------------------------------------------------------
// Details: Copy the other instance of that object to *this object.
// Type:    Method.
// Args:    vrOther - (R) The object to copy from.
// Return:  MIstatus::success - Functional succeeded.
//          MIstatus::failure - Functional failed.
// Throws:  None.
//--
bool CMICmnLLDBDebugSessionInfoVarObj::CopyOther(
    const CMICmnLLDBDebugSessionInfoVarObj &vrOther) {
  // Check for self-assignment
  if (this == &vrOther)
    return MIstatus::success;

  m_eVarFormat = vrOther.m_eVarFormat;
  m_eVarType = vrOther.m_eVarType;
  m_strName = vrOther.m_strName;
  m_SBValue = vrOther.m_SBValue;
  m_strNameReal = vrOther.m_strNameReal;
  m_strFormattedValue = vrOther.m_strFormattedValue;
  m_strVarObjParentName = vrOther.m_strVarObjParentName;

  return MIstatus::success;
}

//++
//------------------------------------------------------------------------------------
// Details: Move that object to *this object.
// Type:    Method.
// Args:    vrwOther    - (RW) The object to copy from.
// Return:  MIstatus::success - Functional succeeded.
//          MIstatus::failure - Functional failed.
// Throws:  None.
//--
bool CMICmnLLDBDebugSessionInfoVarObj::MoveOther(
    CMICmnLLDBDebugSessionInfoVarObj &vrwOther) {
  // Check for self-assignment
  if (this == &vrwOther)
    return MIstatus::success;

  CopyOther(vrwOther);
  vrwOther.m_eVarFormat = eVarFormat_Natural;
  vrwOther.m_eVarType = eVarType_Internal;
  vrwOther.m_strName.clear();
  vrwOther.m_SBValue.Clear();
  vrwOther.m_strNameReal.clear();
  vrwOther.m_strFormattedValue.clear();
  vrwOther.m_strVarObjParentName.clear();

  return MIstatus::success;
}

//++
//------------------------------------------------------------------------------------
// Details: CMICmnLLDBDebugSessionInfoVarObj destructor.
// Type:    Overridden.
// Args:    None.
// Return:  None.
// Throws:  None.
//--
CMICmnLLDBDebugSessionInfoVarObj::~CMICmnLLDBDebugSessionInfoVarObj() {}

//++
//------------------------------------------------------------------------------------
// Details: Retrieve the var format enumeration for the specified string.
// Type:    Static method.
// Args:    vrStrFormat - (R) Text description of the var format.
// Return:  varFormat_e - Var format enumeration.
//                      - No match found return eVarFormat_Invalid.
// Throws:  None.
//--
CMICmnLLDBDebugSessionInfoVarObj::varFormat_e
CMICmnLLDBDebugSessionInfoVarObj::GetVarFormatForString(
    const CMIUtilString &vrStrFormat) {
  // CODETAG_SESSIONINFO_VARFORMAT_ENUM
  for (MIuint i = 0; i < eVarFormat_count; i++) {
    const char *pVarFormatString = ms_aVarFormatStrings[i];
    if (vrStrFormat == pVarFormatString)
      return static_cast<varFormat_e>(i);
  }

  return eVarFormat_Invalid;
}

//++
//------------------------------------------------------------------------------------
// Details: Retrieve the var format enumeration for the specified character.
// Type:    Static method.
// Args:    vcFormat    - Character representing the var format.
// Return:  varFormat_e - Var format enumeration.
//                      - No match found return eVarFormat_Invalid.
// Throws:  None.
//--
CMICmnLLDBDebugSessionInfoVarObj::varFormat_e
CMICmnLLDBDebugSessionInfoVarObj::GetVarFormatForChar(char vcFormat) {
  if ('r' == vcFormat)
    return eVarFormat_Hex;

  // CODETAG_SESSIONINFO_VARFORMAT_ENUM
  for (MIuint i = 0; i < eVarFormat_count; i++) {
    const char *pVarFormatChar = ms_aVarFormatChars[i];
    if (*pVarFormatChar == vcFormat)
      return static_cast<varFormat_e>(i);
  }

  return eVarFormat_Invalid;
}

//++
//------------------------------------------------------------------------------------
// Details: Return the equivalent var value formatted string for the given value
// type,
//          which was prepared for printing (i.e. value was escaped and now it's
//          ready
//          for wrapping into quotes).
//          The SBValue vrValue parameter is checked by LLDB private code for
//          valid
//          scalar type via MI Driver proxy function as the valued returned can
//          also be
//          an error condition. The proxy function determines if the check was
//          valid
//          otherwise return an error condition state by other means saying so.
// Type:    Static method.
// Args:    vrValue     - (R) The var value object.
//          veVarFormat - (R) Var format enumeration.
// Returns: CMIUtilString   - Value formatted string.
// Throws:  None.
//--
CMIUtilString CMICmnLLDBDebugSessionInfoVarObj::GetValueStringFormatted(
    const lldb::SBValue &vrValue,
    const CMICmnLLDBDebugSessionInfoVarObj::varFormat_e veVarFormat) {
  const CMICmnLLDBUtilSBValue utilValue(vrValue, true);
  if (utilValue.IsIntegerType()) {
    MIuint64 nValue = 0;
    if (CMICmnLLDBProxySBValue::GetValueAsUnsigned(vrValue, nValue)) {
      lldb::SBValue &rValue = const_cast<lldb::SBValue &>(vrValue);
      return GetStringFormatted(nValue, rValue.GetValue(), veVarFormat);
    }
  }

  return utilValue.GetValue().AddSlashes();
}

//++
//------------------------------------------------------------------------------------
// Details: Return number formatted string according to the given value type.
// Type:    Static method.
// Args:    vnValue             - (R) The number value to get formatted.
//          vpStrValueNatural   - (R) The natural representation of the number
//          value.
//          veVarFormat         - (R) Var format enumeration.
// Returns: CMIUtilString       - Numerical formatted string.
// Throws:  None.
//--
CMIUtilString CMICmnLLDBDebugSessionInfoVarObj::GetStringFormatted(
    const MIuint64 vnValue, const char *vpStrValueNatural,
    const CMICmnLLDBDebugSessionInfoVarObj::varFormat_e veVarFormat) {
  CMIUtilString strFormattedValue;
  CMICmnLLDBDebugSessionInfoVarObj::varFormat_e veFormat = veVarFormat;
  if (ms_eDefaultFormat != eVarFormat_Invalid &&
      veVarFormat == eVarFormat_Natural) {
    veFormat = ms_eDefaultFormat;
  }

  switch (veFormat) {
  case eVarFormat_Binary:
    strFormattedValue = CMIUtilString::FormatBinary(vnValue);
    break;
  case eVarFormat_Octal:
    strFormattedValue = CMIUtilString::Format("0%llo", vnValue);
    break;
  case eVarFormat_Decimal:
    strFormattedValue = CMIUtilString::Format("%lld", vnValue);
    break;
  case eVarFormat_Hex:
    strFormattedValue = CMIUtilString::Format("0x%llx", vnValue);
    break;
  case eVarFormat_Natural:
  default: {
    strFormattedValue = (vpStrValueNatural != nullptr) ? vpStrValueNatural : "";
  }
  }

  return strFormattedValue;
}

//++
//------------------------------------------------------------------------------------
// Details: Delete internal container contents.
// Type:    Static method.
// Args:    None.
// Returns: None.
// Throws:  None.
//--
void CMICmnLLDBDebugSessionInfoVarObj::VarObjClear() {
  ms_mapVarIdToVarObj.clear();
}

//++
//------------------------------------------------------------------------------------
// Details: Add a var object to the internal container.
// Type:    Static method.
// Args:    vrVarObj    - (R) The var value object.
// Returns: None.
// Throws:  None.
//--
void CMICmnLLDBDebugSessionInfoVarObj::VarObjAdd(
    const CMICmnLLDBDebugSessionInfoVarObj &vrVarObj) {
  VarObjDelete(vrVarObj.GetName());
  MapPairKeyToVarObj_t pr(vrVarObj.GetName(), vrVarObj);
  ms_mapVarIdToVarObj.insert(pr);
}

//++
//------------------------------------------------------------------------------------
// Details: Delete the var object from the internal container matching the
// specified name.
// Type:    Static method.
// Args:    vrVarName   - (R) The var value name.
// Returns: None.
// Throws:  None.
//--
void CMICmnLLDBDebugSessionInfoVarObj::VarObjDelete(
    const CMIUtilString &vrVarName) {
  const MapKeyToVarObj_t::const_iterator it =
      ms_mapVarIdToVarObj.find(vrVarName);
  if (it != ms_mapVarIdToVarObj.end()) {
    ms_mapVarIdToVarObj.erase(it);
  }
}

//++
//------------------------------------------------------------------------------------
// Details: Update an existing var object in the internal container.
// Type:    Static method.
// Args:    vrVarObj    - (R) The var value object.
// Returns: None.
// Throws:  None.
//--
void CMICmnLLDBDebugSessionInfoVarObj::VarObjUpdate(
    const CMICmnLLDBDebugSessionInfoVarObj &vrVarObj) {
  VarObjAdd(vrVarObj);
}

//++
//------------------------------------------------------------------------------------
// Details: Retrieve the var object matching the specified name.
// Type:    Static method.
// Args:    vrVarName   - (R) The var value name.
//          vrwVarObj   - (W) A var object.
// Returns: bool    - True = object found, false = object not found.
// Throws:  None.
//--
bool CMICmnLLDBDebugSessionInfoVarObj::VarObjGet(
    const CMIUtilString &vrVarName,
    CMICmnLLDBDebugSessionInfoVarObj &vrwVarObj) {
  const MapKeyToVarObj_t::const_iterator it =
      ms_mapVarIdToVarObj.find(vrVarName);
  if (it != ms_mapVarIdToVarObj.end()) {
    const CMICmnLLDBDebugSessionInfoVarObj &rVarObj = (*it).second;
    vrwVarObj = rVarObj;
    return true;
  }

  return false;
}

//++
//------------------------------------------------------------------------------------
// Details: A count is kept of the number of var value objects created. This is
// count is
//          used to ID the var value object. Reset the count to 0.
// Type:    Static method.
// Args:    None.
// Returns: None.
// Throws:  None.
//--
void CMICmnLLDBDebugSessionInfoVarObj::VarObjIdResetToZero() {
  ms_nVarUniqueId = 0;
}

//++
//------------------------------------------------------------------------------------
// Details: Default format is globally used as the data format when "natural" is
// in effect, that is, this overrides the default
// Type:    Static method.
// Args:    None.
// Returns: None.
// Throws:  None.
//--
void CMICmnLLDBDebugSessionInfoVarObj::VarObjSetFormat(
    varFormat_e eDefaultFormat) {
  ms_eDefaultFormat = eDefaultFormat;
}

//++
//------------------------------------------------------------------------------------
// Details: A count is kept of the number of var value objects created. This is
// count is
//          used to ID the var value object. Increment the count by 1.
// Type:    Static method.
// Args:    None.
// Returns: None.
// Throws:  None.
//--
void CMICmnLLDBDebugSessionInfoVarObj::VarObjIdInc() { ms_nVarUniqueId++; }

//++
//------------------------------------------------------------------------------------
// Details: A count is kept of the number of var value objects created. This is
// count is
//          used to ID the var value object. Retrieve ID.
// Type:    Static method.
// Args:    None.
// Returns: None.
// Throws:  None.
//--
MIuint CMICmnLLDBDebugSessionInfoVarObj::VarObjIdGet() {
  return ms_nVarUniqueId;
}

//++
//------------------------------------------------------------------------------------
// Details: Retrieve the value formatted object's name.
// Type:    Method.
// Args:    None.
// Returns: CMIUtilString & - Value's var%u name text.
// Throws:  None.
//--
const CMIUtilString &CMICmnLLDBDebugSessionInfoVarObj::GetName() const {
  return m_strName;
}

//++
//------------------------------------------------------------------------------------
// Details: Retrieve the value formatted object's variable name as given in the
// MI command
//          to create the var object.
// Type:    Method.
// Args:    None.
// Returns: CMIUtilString & - Value's real name text.
// Throws:  None.
//--
const CMIUtilString &CMICmnLLDBDebugSessionInfoVarObj::GetNameReal() const {
  return m_strNameReal;
}

//++
//------------------------------------------------------------------------------------
// Details: Retrieve the value formatted string.
// Type:    Method.
// Args:    None.
// Returns: CMIUtilString & - Value formatted string.
// Throws:  None.
//--
const CMIUtilString &
CMICmnLLDBDebugSessionInfoVarObj::GetValueFormatted() const {
  return m_strFormattedValue;
}

//++
//------------------------------------------------------------------------------------
// Details: Retrieve the LLDB Value object.
// Type:    Method.
// Args:    None.
// Returns: lldb::SBValue & - LLDB Value object.
// Throws:  None.
//--
lldb::SBValue &CMICmnLLDBDebugSessionInfoVarObj::GetValue() {
  return m_SBValue;
}

//++
//------------------------------------------------------------------------------------
// Details: Retrieve the LLDB Value object.
// Type:    Method.
// Args:    None.
// Returns: lldb::SBValue & - Constant LLDB Value object.
// Throws:  None.
//--
const lldb::SBValue &CMICmnLLDBDebugSessionInfoVarObj::GetValue() const {
  return m_SBValue;
}

//++
//------------------------------------------------------------------------------------
// Details: Set the var format type for *this object and update the formatting.
// Type:    Method.
// Args:    None.
// Return:  MIstatus::success - Functional succeeded.
//          MIstatus::failure - Functional failed.
// Throws:  None.
//--
bool CMICmnLLDBDebugSessionInfoVarObj::SetVarFormat(
    const varFormat_e veVarFormat) {
  if (veVarFormat >= eVarFormat_count)
    return MIstatus::failure;

  m_eVarFormat = veVarFormat;
  UpdateValue();
  return MIstatus::success;
}

//++
//------------------------------------------------------------------------------------
// Details: Update *this var obj. Update it's value and type.
// Type:    Method.
// Args:    None.
// Returns: None.
// Throws:  None.
//--
void CMICmnLLDBDebugSessionInfoVarObj::UpdateValue() {
  m_strFormattedValue = GetValueStringFormatted(m_SBValue, m_eVarFormat);

  MIuint64 nValue = 0;
  if (CMICmnLLDBProxySBValue::GetValueAsUnsigned(m_SBValue, nValue) ==
      MIstatus::failure)
    m_eVarType = eVarType_Composite;

  CMICmnLLDBDebugSessionInfoVarObj::VarObjUpdate(*this);
}

//++
//------------------------------------------------------------------------------------
// Details: Retrieve the enumeration type of the var object.
// Type:    Method.
// Args:    None.
// Returns: varType_e   - Enumeration value.
// Throws:  None.
//--
CMICmnLLDBDebugSessionInfoVarObj::varType_e
CMICmnLLDBDebugSessionInfoVarObj::GetType() const {
  return m_eVarType;
}

//++
//------------------------------------------------------------------------------------
// Details: Retrieve the parent var object's name, the parent var object  to
// *this var
//          object (if assigned). The parent is equivalent to LLDB SBValue
//          variable's
//          parent.
// Type:    Method.
// Args:    None.
// Returns: CMIUtilString & - Pointer to var object, NULL = no parent.
// Throws:  None.
//--
const CMIUtilString &
CMICmnLLDBDebugSessionInfoVarObj::GetVarParentName() const {
  return m_strVarObjParentName;
}
