//===-- MICmnBase.cpp -------------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

// Third party headers
#include <stdarg.h>

// In-house headers:
#include "MICmnBase.h"
#include "MICmnLog.h"
#include "MICmnStreamStderr.h"

//++
//------------------------------------------------------------------------------------
// Details: CMICmnBase constructor.
// Type:    Method.
// Args:    None.
// Return:  None.
// Throws:  None.
//--
CMICmnBase::CMICmnBase()
    : m_strMILastErrorDescription(CMIUtilString()), m_bInitialized(false),
      m_pLog(&CMICmnLog::Instance()), m_clientUsageRefCnt(0) {}

//++
//------------------------------------------------------------------------------------
// Details: CMICmnBase destructor.
// Type:    Overrideable.
// Args:    None.
// Return:  None.
// Throws:  None.
//--
CMICmnBase::~CMICmnBase() { m_pLog = NULL; }

//++
//------------------------------------------------------------------------------------
// Details: Retrieve whether *this object has an error description set.
// Type:    Method.
// Args:    None.
// Return:  bool    - True = Yes already defined, false = empty description.
// Throws:  None.
//--
bool CMICmnBase::HaveErrorDescription() const {
  return m_strMILastErrorDescription.empty();
}

//++
//------------------------------------------------------------------------------------
// Details: Retrieve MI's last error condition.
// Type:    Method.
// Args:    None.
// Return:  CMIUtilString & - Text description.
// Throws:  None.
//--
const CMIUtilString &CMICmnBase::GetErrorDescription() const {
  return m_strMILastErrorDescription;
}

//++
//------------------------------------------------------------------------------------
// Details: Set MI's error condition description. This may be accessed by
// clients and
//          seen by users.  Message is available to the client using the server
//          and sent
//          to the Logger.
// Type:    Method.
// Args:    vrTxt   - (R) Text description.
// Return:  None.
// Throws:  None.
//--
void CMICmnBase::SetErrorDescription(const CMIUtilString &vrTxt) const {
  m_strMILastErrorDescription = vrTxt;
  if (!vrTxt.empty()) {
    const CMIUtilString txt(CMIUtilString::Format("Error: %s", vrTxt.c_str()));
    CMICmnStreamStderr::Instance().Write(txt);
  }
}

//++
//------------------------------------------------------------------------------------
// Details: Set MI's error condition description. This may be accessed by
// clients and
//          seen by users.  Message is available to the client using the server
//          and sent
//          to the Logger.
// Type:    Method.
// Args:    vrTxt   - (R) Text description.
// Return:  None.
// Throws:  None.
//--
void CMICmnBase::SetErrorDescriptionNoLog(const CMIUtilString &vrTxt) const {
  m_strMILastErrorDescription = vrTxt;
}

//++
//------------------------------------------------------------------------------------
// Details: Clear MI's error condition description.
// Type:    Method.
// Args:    None.
// Return:  None.
// Throws:  None.
//--
void CMICmnBase::ClrErrorDescription() const {
  m_strMILastErrorDescription.clear();
}

//++
//------------------------------------------------------------------------------------
// Details: Set MI's error condition description. This may be accessed by
// clients and
//          seen by users. Message is available to the client using the server
//          and sent
//          to the Logger.
// Type:    Method.
// Args:    vFormat - (R) Format string.
//          ...     - (R) Variable number of CMIUtilString type objects.
// Return:  None.
// Throws:  None.
//--
void CMICmnBase::SetErrorDescriptionn(const char *vFormat, ...) const {
  va_list args;
  va_start(args, vFormat);
  CMIUtilString strResult = CMIUtilString::FormatValist(vFormat, args);
  va_end(args);

  SetErrorDescription(strResult);
}
