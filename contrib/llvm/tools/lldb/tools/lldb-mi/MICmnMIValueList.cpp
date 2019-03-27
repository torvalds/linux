//===-- MICmnMIValueList.cpp ------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

// In-house headers:
#include "MICmnMIValueList.h"
#include "MICmnResources.h"

//++
//------------------------------------------------------------------------------------
// Details: CMICmnMIValueList constructor.
// Type:    Method.
// Args:    vbValueTypeList - (R) True = yes value type list, false = result
// type list.
// Return:  None.
// Throws:  None.
//--
CMICmnMIValueList::CMICmnMIValueList(const bool vbValueTypeList) {
  m_strValue = "[]";
}

//++
//------------------------------------------------------------------------------------
// Details: CMICmnMIValueList constructor.
//          Construct a results only list.
//          return MIstatus::failure.
// Type:    Method.
// Args:    vResult - (R) MI result object.
// Return:  None.
// Throws:  None.
//--
CMICmnMIValueList::CMICmnMIValueList(const CMICmnMIValueResult &vResult) {
  m_strValue = vResult.GetString();
  BuildList();
  m_bJustConstructed = false;
}

//++
//------------------------------------------------------------------------------------
// Details: CMICmnMIValueList constructor.
//          Construct a value only list.
// Type:    Method.
// Args:    vValue  - (R) MI value object.
// Return:  None.
// Throws:  None.
//--
CMICmnMIValueList::CMICmnMIValueList(const CMICmnMIValue &vValue) {
  m_strValue = vValue.GetString();
  BuildList();
  m_bJustConstructed = false;
}

//++
//------------------------------------------------------------------------------------
// Details: CMICmnMIValueList destructor.
// Type:    Overrideable.
// Args:    None.
// Return:  None.
// Throws:  None.
//--
CMICmnMIValueList::~CMICmnMIValueList() {}

//++
//------------------------------------------------------------------------------------
// Details: Build the result value's mandatory data part, one tuple
// Type:    Method.
// Args:    None.
// Return:  None.
// Throws:  None.
//--
void CMICmnMIValueList::BuildList() {
  const char *pFormat = "[%s]";
  m_strValue = CMIUtilString::Format(pFormat, m_strValue.c_str());
}

//++
//------------------------------------------------------------------------------------
// Details: Add another MI result object to  the value list's of list is
// results.
//          Only result objects can be added to a list of result otherwise this
//          function
//          will return MIstatus::failure.
// Type:    Method.
// Args:    vResult - (R) The MI result object.
// Return:  None.
// Throws:  None.
//--
void CMICmnMIValueList::Add(const CMICmnMIValueResult &vResult) {
  BuildList(vResult);
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
void CMICmnMIValueList::Add(const CMICmnMIValue &vValue) { BuildList(vValue); }

//++
//------------------------------------------------------------------------------------
// Details: Add another MI result object to  the value list's of list is
// results.
//          Only result objects can be added to a list of result otherwise this
//          function
//          will return MIstatus::failure.
// Type:    Method.
// Args:    vResult - (R) The MI result object.
// Return:  None.
// Throws:  None.
//--
void CMICmnMIValueList::BuildList(const CMICmnMIValueResult &vResult) {
  // Clear out the default "<Invalid>" text
  if (m_bJustConstructed) {
    m_bJustConstructed = false;
    m_strValue = vResult.GetString();
    BuildList();
    return;
  }

  const CMIUtilString data(ExtractContentNoBrackets());
  const char *pFormat = "[%s,%s]";
  m_strValue =
      CMIUtilString::Format(pFormat, data.c_str(), vResult.GetString().c_str());
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
void CMICmnMIValueList::BuildList(const CMICmnMIValue &vValue) {
  // Clear out the default "<Invalid>" text
  if (m_bJustConstructed) {
    m_bJustConstructed = false;
    m_strValue = vValue.GetString();
    BuildList();
    return;
  }

  // Remove already present '[' and ']' from the start and end
  m_strValue = m_strValue.Trim();
  size_t len = m_strValue.size();
  if ((len > 1) && (m_strValue[0] == '[') && (m_strValue[len - 1] == ']'))
    m_strValue = m_strValue.substr(1, len - 2);
  const char *pFormat = "[%s,%s]";
  m_strValue = CMIUtilString::Format(pFormat, m_strValue.c_str(),
                                     vValue.GetString().c_str());
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
CMIUtilString CMICmnMIValueList::ExtractContentNoBrackets() const {
  CMIUtilString data(m_strValue);

  if (data[0] == '[') {
    data = data.substr(1, data.length() - 1);
  }
  if (data[data.size() - 1] == ']') {
    data = data.substr(0, data.length() - 1);
  }

  return data;
}
