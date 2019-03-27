//===-- MICmdArgValPrintValues.cpp ------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

// In-house headers:
#include "MICmdArgValPrintValues.h"
#include "MICmdArgContext.h"

//++
//------------------------------------------------------------------------------------
// Details: CMICmdArgValPrintValues constructor.
// Type:    Method.
// Args:    None.
// Return:  None.
// Throws:  None.
//--
CMICmdArgValPrintValues::CMICmdArgValPrintValues() : m_nPrintValues(0) {}

//++
//------------------------------------------------------------------------------------
// Details: CMICmdArgValPrintValues constructor.
// Type:    Method.
// Args:    vrArgName       - (R) Argument's name to search by.
//          vbMandatory     - (R) True = Yes must be present, false = optional
//          argument.
//          vbHandleByCmd   - (R) True = Command processes *this option, false =
//          not handled.
// Return:  None.
// Throws:  None.
//--
CMICmdArgValPrintValues::CMICmdArgValPrintValues(const CMIUtilString &vrArgName,
                                                 const bool vbMandatory,
                                                 const bool vbHandleByCmd)
    : CMICmdArgValBaseTemplate(vrArgName, vbMandatory, vbHandleByCmd),
      m_nPrintValues(0) {}

//++
//------------------------------------------------------------------------------------
// Details: CMICmdArgValPrintValues destructor.
// Type:    Overridden.
// Args:    None.
// Return:  None.
// Throws:  None.
//--
CMICmdArgValPrintValues::~CMICmdArgValPrintValues() {}

//++
//------------------------------------------------------------------------------------
// Details: Parse the command's argument options string and try to extract the
// value *this
//          argument is looking for.
// Type:    Overridden.
// Args:    vwArgContext    - (RW) The command's argument options string.
// Return:  MIstatus::success - Functional succeeded.
//          MIstatus::failure - Functional failed.
// Throws:  None.
//--
bool CMICmdArgValPrintValues::Validate(CMICmdArgContext &vwArgContext) {
  if (vwArgContext.IsEmpty())
    return m_bMandatory ? MIstatus::failure : MIstatus::success;

  const CMIUtilString strArg(vwArgContext.GetArgs()[0]);
  if (IsArgPrintValues(strArg) && ExtractPrintValues(strArg)) {
    m_bFound = true;
    m_bValid = true;
    m_argValue = GetPrintValues();
    vwArgContext.RemoveArg(strArg);
    return MIstatus::success;
  }

  return MIstatus::failure;
}

//++
//------------------------------------------------------------------------------------
// Details: Examine the string and determine if it is a valid string type
// argument.
// Type:    Method.
// Args:    vrTxt   - (R) Some text.
// Return:  bool    - True = yes valid arg, false = no.
// Throws:  None.
//--
bool CMICmdArgValPrintValues::IsArgPrintValues(
    const CMIUtilString &vrTxt) const {
  return (CMIUtilString::Compare(vrTxt, "0") ||
          CMIUtilString::Compare(vrTxt, "--no-values") ||
          CMIUtilString::Compare(vrTxt, "1") ||
          CMIUtilString::Compare(vrTxt, "--all-values") ||
          CMIUtilString::Compare(vrTxt, "2") ||
          CMIUtilString::Compare(vrTxt, "--simple-values"));
}

//++
//------------------------------------------------------------------------------------
// Details: Extract the print-values from the print-values argument.
// Type:    Method.
// Args:    vrTxt   - (R) Some text.
// Return:  MIstatus::success - Functional succeeded.
//          MIstatus::failure - Functional failed.
// Throws:  None.
//--
bool CMICmdArgValPrintValues::ExtractPrintValues(const CMIUtilString &vrTxt) {
  if (CMIUtilString::Compare(vrTxt, "0") ||
      CMIUtilString::Compare(vrTxt, "--no-values"))
    m_nPrintValues = 0;
  else if (CMIUtilString::Compare(vrTxt, "1") ||
           CMIUtilString::Compare(vrTxt, "--all-values"))
    m_nPrintValues = 1;
  else if (CMIUtilString::Compare(vrTxt, "2") ||
           CMIUtilString::Compare(vrTxt, "--simple-values"))
    m_nPrintValues = 2;
  else
    return MIstatus::failure;

  return MIstatus::success;
}

//++
//------------------------------------------------------------------------------------
// Details: Retrieve the print-values found in the argument.
// Type:    Method.
// Args:    None.
// Return:  MIuint - The print-values.
// Throws:  None.
//--
MIuint CMICmdArgValPrintValues::GetPrintValues() const {
  return m_nPrintValues;
}
