//===-- MICmnMIOutOfBandRecord.cpp ------------------------------*- C++ -*-===//
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
#include "MICmnMIOutOfBandRecord.h"
#include "MICmnResources.h"

// Instantiations:
static const char *
MapOutOfBandToText(CMICmnMIOutOfBandRecord::OutOfBand_e veType) {
  switch (veType) {
  case CMICmnMIOutOfBandRecord::eOutOfBand_Running:
    return "running";
  case CMICmnMIOutOfBandRecord::eOutOfBand_Stopped:
    return "stopped";
  case CMICmnMIOutOfBandRecord::eOutOfBand_BreakPointCreated:
    return "breakpoint-created";
  case CMICmnMIOutOfBandRecord::eOutOfBand_BreakPointModified:
    return "breakpoint-modified";
  case CMICmnMIOutOfBandRecord::eOutOfBand_Thread:
    return ""; // "" Meant to be empty
  case CMICmnMIOutOfBandRecord::eOutOfBand_ThreadGroupAdded:
    return "thread-group-added";
  case CMICmnMIOutOfBandRecord::eOutOfBand_ThreadGroupExited:
    return "thread-group-exited";
  case CMICmnMIOutOfBandRecord::eOutOfBand_ThreadGroupRemoved:
    return "thread-group-removed";
  case CMICmnMIOutOfBandRecord::eOutOfBand_ThreadGroupStarted:
    return "thread-group-started";
  case CMICmnMIOutOfBandRecord::eOutOfBand_ThreadCreated:
    return "thread-created";
  case CMICmnMIOutOfBandRecord::eOutOfBand_ThreadExited:
    return "thread-exited";
  case CMICmnMIOutOfBandRecord::eOutOfBand_ThreadSelected:
    return "thread-selected";
  case CMICmnMIOutOfBandRecord::eOutOfBand_TargetModuleLoaded:
    return "library-loaded";
  case CMICmnMIOutOfBandRecord::eOutOfBand_TargetModuleUnloaded:
    return "library-unloaded";
  case CMICmnMIOutOfBandRecord::eOutOfBand_TargetStreamOutput:
    return "";
  case CMICmnMIOutOfBandRecord::eOutOfBand_ConsoleStreamOutput:
    return "";
  case CMICmnMIOutOfBandRecord::eOutOfBand_LogStreamOutput:
    return "";
  }
  assert(false && "unknown CMICmnMIOutofBandRecord::OutOfBand_e");
  return NULL;
}

static const char *
MapOutOfBandToToken(CMICmnMIOutOfBandRecord::OutOfBand_e veType) {
  switch (veType) {
  case CMICmnMIOutOfBandRecord::eOutOfBand_Running:
    return "*";
  case CMICmnMIOutOfBandRecord::eOutOfBand_Stopped:
    return "*";
  case CMICmnMIOutOfBandRecord::eOutOfBand_BreakPointCreated:
    return "=";
  case CMICmnMIOutOfBandRecord::eOutOfBand_BreakPointModified:
    return "=";
  case CMICmnMIOutOfBandRecord::eOutOfBand_Thread:
    return "@";
  case CMICmnMIOutOfBandRecord::eOutOfBand_ThreadGroupAdded:
    return "=";
  case CMICmnMIOutOfBandRecord::eOutOfBand_ThreadGroupExited:
    return "=";
  case CMICmnMIOutOfBandRecord::eOutOfBand_ThreadGroupRemoved:
    return "=";
  case CMICmnMIOutOfBandRecord::eOutOfBand_ThreadGroupStarted:
    return "=";
  case CMICmnMIOutOfBandRecord::eOutOfBand_ThreadCreated:
    return "=";
  case CMICmnMIOutOfBandRecord::eOutOfBand_ThreadExited:
    return "=";
  case CMICmnMIOutOfBandRecord::eOutOfBand_ThreadSelected:
    return "=";
  case CMICmnMIOutOfBandRecord::eOutOfBand_TargetModuleLoaded:
    return "=";
  case CMICmnMIOutOfBandRecord::eOutOfBand_TargetModuleUnloaded:
    return "=";
  case CMICmnMIOutOfBandRecord::eOutOfBand_TargetStreamOutput:
    return "@";
  case CMICmnMIOutOfBandRecord::eOutOfBand_ConsoleStreamOutput:
    return "~";
  case CMICmnMIOutOfBandRecord::eOutOfBand_LogStreamOutput:
    return "&";
  }
  assert(false && "unknown CMICmnMIOutofBandRecord::OutOfBand_e");
  return NULL;
}

//++
//------------------------------------------------------------------------------------
// Details: Build the Out-of-band record's mandatory data part. The part up to
// the first
//          (additional) result i.e. async-record ==>  "*" type.
// Args:    veType      - (R) A MI Out-of-Band enumeration.
// Return:  CMIUtilString - The async record text.
// Throws:  None.
//--
static CMIUtilString
BuildAsyncRecord(CMICmnMIOutOfBandRecord::OutOfBand_e veType) {
  return CMIUtilString::Format("%s%s", MapOutOfBandToToken(veType),
                               MapOutOfBandToText(veType));
}

//++
//------------------------------------------------------------------------------------
// Details: CMICmnMIOutOfBandRecord constructor.
// Type:    Method.
// Args:    None.
// Return:  None.
// Throws:  None.
//--
CMICmnMIOutOfBandRecord::CMICmnMIOutOfBandRecord()
    : m_strAsyncRecord(MIRSRC(IDS_CMD_ERR_EVENT_HANDLED_BUT_NO_ACTION)) {}

//++
//------------------------------------------------------------------------------------
// Details: CMICmnMIOutOfBandRecord constructor.
// Type:    Method.
// Args:    veType      - (R) A MI Out-of-Bound enumeration.
// Return:  None.
// Throws:  None.
//--
CMICmnMIOutOfBandRecord::CMICmnMIOutOfBandRecord(OutOfBand_e veType)
    : m_strAsyncRecord(BuildAsyncRecord(veType)) {}

//++
//------------------------------------------------------------------------------------
// Details: CMICmnMIOutOfBandRecord constructor.
// Type:    Method.
// Args:    veType      - (R) A MI Out-of-Bound enumeration.
//          vConst      - (R) A MI const object.
// Return:  None.
// Throws:  None.
//--
CMICmnMIOutOfBandRecord::CMICmnMIOutOfBandRecord(
    OutOfBand_e veType, const CMICmnMIValueConst &vConst)
    : m_strAsyncRecord(BuildAsyncRecord(veType)) {
  m_strAsyncRecord += vConst.GetString();
}

//++
//------------------------------------------------------------------------------------
// Details: CMICmnMIOutOfBandRecord constructor.
// Type:    Method.
// Args:    veType      - (R) A MI Out-of-Bound enumeration.
//          vResult     - (R) A MI result object.
// Return:  None.
// Throws:  None.
//--
CMICmnMIOutOfBandRecord::CMICmnMIOutOfBandRecord(
    OutOfBand_e veType, const CMICmnMIValueResult &vResult)
    : m_strAsyncRecord(BuildAsyncRecord(veType)) {
  Add(vResult);
}

//++
//------------------------------------------------------------------------------------
// Details: CMICmnMIOutOfBandRecord destructor.
// Type:    Overrideable.
// Args:    None.
// Return:  None.
// Throws:  None.
//--
CMICmnMIOutOfBandRecord::~CMICmnMIOutOfBandRecord() {}

//++
//------------------------------------------------------------------------------------
// Details: Return the MI Out-of-band record as a string. The string is a direct
// result of
//          work done on *this Out-of-band record so if not enough data is added
//          then it is
//          possible to return a malformed Out-of-band record. If nothing has
//          been set or
//          added to *this MI Out-of-band record object then text "<Invalid>"
//          will be returned.
// Type:    Method.
// Args:    None.
// Return:  CMIUtilString & - MI output text.
// Throws:  None.
//--
const CMIUtilString &CMICmnMIOutOfBandRecord::GetString() const {
  return m_strAsyncRecord;
}

//++
//------------------------------------------------------------------------------------
// Details: Add to *this Out-of-band record additional information.
// Type:    Method.
// Args:    vResult           - (R) A MI result object.
// Return:  None.
// Throws:  None.
//--
void CMICmnMIOutOfBandRecord::Add(const CMICmnMIValueResult &vResult) {
  m_strAsyncRecord += ",";
  m_strAsyncRecord += vResult.GetString();
}
