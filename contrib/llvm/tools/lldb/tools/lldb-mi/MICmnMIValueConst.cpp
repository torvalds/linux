//===-- MICmnMIValueConst.cpp -----------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

// In-house headers:
#include "MICmnMIValueConst.h"

// Instantiations:
const CMIUtilString CMICmnMIValueConst::ms_constStrDblQuote("\"");

//++
//------------------------------------------------------------------------------------
// Details: CMICmnMIValueConst constructor.
// Type:    Method.
// Args:    vString - (R) MI Const c-string value.
// Return:  None.
// Throws:  None.
//--
CMICmnMIValueConst::CMICmnMIValueConst(const CMIUtilString &vString)
    : m_strPartConst(vString), m_bNoQuotes(false) {
  BuildConst();
}

//++
//------------------------------------------------------------------------------------
// Details: CMICmnMIValueConst constructor.
// Type:    Method.
// Args:    vString     - (R) MI Const c-string value.
//          vbNoQuotes  - (R) True = return string not surrounded with quotes,
//          false = use quotes.
// Return:  None.
// Throws:  None.
//--
CMICmnMIValueConst::CMICmnMIValueConst(const CMIUtilString &vString,
                                       const bool vbNoQuotes)
    : m_strPartConst(vString), m_bNoQuotes(vbNoQuotes) {
  BuildConst();
}

//++
//------------------------------------------------------------------------------------
// Details: CMICmnMIValueConst destructor.
// Type:    Overrideable.
// Args:    None.
// Return:  None.
// Throws:  None.
//--
CMICmnMIValueConst::~CMICmnMIValueConst() {}

//++
//------------------------------------------------------------------------------------
// Details: Build the Value Const data.
// Type:    Method.
// Args:    None.
// Return:  MIstatus::success - Functional succeeded.
//          MIstatus::failure - Functional failed.
// Throws:  None.
//--
bool CMICmnMIValueConst::BuildConst() {
  if (m_strPartConst.length() != 0) {
    const CMIUtilString strValue(m_strPartConst.StripCREndOfLine());
    if (m_bNoQuotes) {
      m_strValue = strValue;
    } else {
      const char *pFormat = "%s%s%s";
      m_strValue =
          CMIUtilString::Format(pFormat, ms_constStrDblQuote.c_str(),
                                strValue.c_str(), ms_constStrDblQuote.c_str());
    }
  } else {
    const char *pFormat = "%s%s";
    m_strValue = CMIUtilString::Format(pFormat, ms_constStrDblQuote.c_str(),
                                       ms_constStrDblQuote.c_str());
  }

  return MIstatus::success;
}
