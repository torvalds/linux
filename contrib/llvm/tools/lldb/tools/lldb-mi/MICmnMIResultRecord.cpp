//===-- MICmnMIResultRecord.cpp ---------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

// Third Party Headers:
#include <assert.h>

// In-house headers:
#include "MICmnMIResultRecord.h"
#include "MICmnResources.h"

//++
//------------------------------------------------------------------------------------
// Details: Map a result class to the corresponding string.
// Args:    veType      - (R) A MI result class enumeration.
// Return:  const char* - The string corresponding to the result class.
// Throws:  None.
//--
static const char *
MapResultClassToResultClassText(CMICmnMIResultRecord::ResultClass_e veType) {
  switch (veType) {
  case CMICmnMIResultRecord::eResultClass_Done:
    return "done";
  case CMICmnMIResultRecord::eResultClass_Running:
    return "running";
  case CMICmnMIResultRecord::eResultClass_Connected:
    return "connected";
  case CMICmnMIResultRecord::eResultClass_Error:
    return "error";
  case CMICmnMIResultRecord::eResultClass_Exit:
    return "exit";
  }
  assert(false && "unknown CMICmnMIResultRecord::ResultClass_e");
  return NULL;
}

//++
//------------------------------------------------------------------------------------
// Details: Build the result record's mandatory data part. The part up to the
// first
//          (additional) result i.e. result-record ==>  [ token ] "^"
//          result-class.
// Args:    vrToken     - (R) The command's transaction ID or token.
//          veType      - (R) A MI result class enumeration.
// Return:  CMIUtilString & - MI result record mandatory data
// Throws:  None.
//--
static const CMIUtilString
BuildResultRecord(const CMIUtilString &vrToken,
                  CMICmnMIResultRecord::ResultClass_e veType) {
  const char *pStrResultRecord = MapResultClassToResultClassText(veType);
  return CMIUtilString::Format("%s^%s", vrToken.c_str(), pStrResultRecord);
}

//++
//------------------------------------------------------------------------------------
// Details: CMICmnMIResultRecord constructor.
// Type:    Method.
// Args:    None.
// Return:  None.
// Throws:  None.
//--
CMICmnMIResultRecord::CMICmnMIResultRecord()
    : m_strResultRecord(MIRSRC(IDS_CMD_ERR_CMD_RUN_BUT_NO_ACTION)) {}

//++
//------------------------------------------------------------------------------------
// Details: CMICmnMIResultRecord constructor.
// Type:    Method.
// Args:    vrToken - (R) The command's transaction ID or token.
//          veType  - (R) A MI result class enumeration.
// Return:  None.
// Throws:  None.
//--
CMICmnMIResultRecord::CMICmnMIResultRecord(const CMIUtilString &vrToken,
                                           ResultClass_e veType)
    : m_strResultRecord(BuildResultRecord(vrToken, veType)) {}

//++
//------------------------------------------------------------------------------------
// Details: CMICmnMIResultRecord constructor.
// Type:    Method.
// Args:    vrToken     - (R) The command's transaction ID or token.
//          veType      - (R) A MI result class enumeration.
//          vMIResult   - (R) A MI result object.
// Return:  None.
// Throws:  None.
//--
CMICmnMIResultRecord::CMICmnMIResultRecord(const CMIUtilString &vrToken,
                                           ResultClass_e veType,
                                           const CMICmnMIValueResult &vValue)
    : m_strResultRecord(BuildResultRecord(vrToken, veType)) {
  Add(vValue);
}

//++
//------------------------------------------------------------------------------------
// Details: CMICmnMIResultRecord destructor.
// Type:    Overrideable.
// Args:    None.
// Return:  None.
// Throws:  None.
//--
CMICmnMIResultRecord::~CMICmnMIResultRecord() {}

//++
//------------------------------------------------------------------------------------
// Details: Return the MI result record as a string. The string is a direct
// result of
//          work done on *this result record so if not enough data is added then
//          it is
//          possible to return a malformed result record. If nothing has been
//          set or
//          added to *this MI result record object then text "<Invalid>" will be
//          returned.
// Type:    Method.
// Args:    None.
// Return:  CMIUtilString & - MI output text.
// Throws:  None.
//--
const CMIUtilString &CMICmnMIResultRecord::GetString() const {
  return m_strResultRecord;
}

//++
//------------------------------------------------------------------------------------
// Details: Add to *this result record additional information.
// Type:    Method.
// Args:    vMIValue    - (R) A MI value derived object.
// Return:  None.
// Throws:  None.
//--
void CMICmnMIResultRecord::Add(const CMICmnMIValue &vMIValue) {
  m_strResultRecord += ",";
  m_strResultRecord += vMIValue.GetString();
}
