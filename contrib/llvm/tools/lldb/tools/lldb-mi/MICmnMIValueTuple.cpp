//===-- MICmnMIValueTuple.cpp -----------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

// In-house headers:
#include "MICmnMIValueTuple.h"

//++
//------------------------------------------------------------------------------------
// Details: CMICmnMIValueTuple constructor.
// Type:    Method.
// Args:    None.
// Return:  None.
// Throws:  None.
//--
CMICmnMIValueTuple::CMICmnMIValueTuple() : m_bSpaceAfterComma(false) {
  m_strValue = "{}";
}

//++
//------------------------------------------------------------------------------------
// Details: CMICmnMIValueTuple constructor.
// Type:    Method.
// Args:    vResult - (R) MI result object.
// Return:  None.
// Throws:  None.
//--
CMICmnMIValueTuple::CMICmnMIValueTuple(const CMICmnMIValueResult &vResult)
    : m_bSpaceAfterComma(false) {
  m_strValue = vResult.GetString();
  BuildTuple();
  m_bJustConstructed = false;
}

//++
//------------------------------------------------------------------------------------
// Details: CMICmnMIValueTuple constructor.
// Type:    Method.
// Args:    vResult         - (R) MI result object.
//          vbUseSpacing    - (R) True = put space separators into the string,
//          false = no spaces used.
// Return:  None.
// Throws:  None.
//--
CMICmnMIValueTuple::CMICmnMIValueTuple(const CMICmnMIValueResult &vResult,
                                       const bool vbUseSpacing)
    : m_bSpaceAfterComma(vbUseSpacing) {
  m_strValue = vResult.GetString();
  BuildTuple();
  m_bJustConstructed = false;
}

//++
//------------------------------------------------------------------------------------
// Details: CMICmnMIValueTuple destructor.
// Type:    Overrideable.
// Args:    None.
// Return:  None.
// Throws:  None.
//--
CMICmnMIValueTuple::~CMICmnMIValueTuple() {}

//++
//------------------------------------------------------------------------------------
// Details: Build the result value's mandatory data part, one tuple
// Type:    Method.
// Args:    None.
// Return:  None.
// Throws:  None.
//--
void CMICmnMIValueTuple::BuildTuple() {
  const char *pFormat = "{%s}";
  m_strValue = CMIUtilString::Format(pFormat, m_strValue.c_str());
}

//++
//------------------------------------------------------------------------------------
// Details: Add another MI result object to the value's list of tuples.
// Type:    Method.
// Args:    vResult - (R) The MI result object.
// Return:  None.
// Throws:  None.
//--
void CMICmnMIValueTuple::BuildTuple(const CMICmnMIValueResult &vResult) {
  // Clear out the default "<Invalid>" text
  if (m_bJustConstructed) {
    m_bJustConstructed = false;
    m_strValue = vResult.GetString();
    BuildTuple();
    return;
  }

  if (m_strValue[0] == '{') {
    m_strValue = m_strValue.substr(1, m_strValue.size() - 1);
  }
  if (m_strValue[m_strValue.size() - 1] == '}') {
    m_strValue = m_strValue.substr(0, m_strValue.size() - 1);
  }

  const char *pFormat = m_bSpaceAfterComma ? "{%s, %s}" : "{%s,%s}";
  m_strValue = CMIUtilString::Format(pFormat, m_strValue.c_str(),
                                     vResult.GetString().c_str());
}

//++
//------------------------------------------------------------------------------------
// Details: Add string value to the value's list of tuples.
// Type:    Method.
// Args:    vValue  - (R) The string object.
// Return:  None.
// Throws:  None.
//--
void CMICmnMIValueTuple::BuildTuple(const CMIUtilString &vValue) {
  // Clear out the default "<Invalid>" text
  if (m_bJustConstructed) {
    m_bJustConstructed = false;
    m_strValue = vValue;
    BuildTuple();
    return;
  }

  const CMIUtilString data(ExtractContentNoBrackets());
  const char *pFormat = m_bSpaceAfterComma ? "{%s, %s}" : "{%s,%s}";
  m_strValue = CMIUtilString::Format(pFormat, data.c_str(), vValue.c_str());
}

//++
//------------------------------------------------------------------------------------
// Details: Add another MI value object to  the value list's of list is values.
//          Only values objects can be added to a list of values otherwise this
//          function
//          will return MIstatus::failure.
// Type:    Method.
// Args:    vValue  - (R) The MI value object.
// Return:  None.
// Throws:  None.
//--
void CMICmnMIValueTuple::Add(const CMICmnMIValueResult &vResult) {
  BuildTuple(vResult);
}

//++
//------------------------------------------------------------------------------------
// Details: Add another MI value object to  the value list's of list is values.
//          Only values objects can be added to a list of values otherwise this
//          function
//          will return MIstatus::failure.
// Type:    Method.
// Args:    vValue          - (R) The MI value object.
//          vbUseSpacing    - (R) True = put space separators into the string,
//          false = no spaces used.
// Return:  None.
// Throws:  None.
//--
void CMICmnMIValueTuple::Add(const CMICmnMIValueResult &vResult,
                             const bool vbUseSpacing) {
  m_bSpaceAfterComma = vbUseSpacing;
  BuildTuple(vResult);
}

//++
//------------------------------------------------------------------------------------
// Details: Add another MI value object to  the value list's of list is values.
//          Only values objects can be added to a list of values otherwise this
//          function
//          will return MIstatus::failure.
// Type:    Method.
// Args:    vValue          - (R) The MI value object.
//          vbUseSpacing    - (R) True = put space separators into the string,
//          false = no spaces used.
// Return:  None.
// Throws:  None.
//--
void CMICmnMIValueTuple::Add(const CMICmnMIValueConst &vValue,
                             const bool vbUseSpacing) {
  m_bSpaceAfterComma = vbUseSpacing;
  BuildTuple(vValue.GetString());
}

//++
//------------------------------------------------------------------------------------
// Details: Retrieve the contents of *this value object but without the outer
// most
//          brackets.
// Type:    Method.
// Args:    None.
// Return:  CMIUtilString - Data within the object.
// Throws:  None.
//--
CMIUtilString CMICmnMIValueTuple::ExtractContentNoBrackets() const {
  CMIUtilString data(m_strValue);

  if (data[0] == '{') {
    data = data.substr(1, data.length() - 1);
  }
  if (data[data.size() - 1] == '}') {
    data = data.substr(0, data.length() - 1);
  }

  return data;
}
