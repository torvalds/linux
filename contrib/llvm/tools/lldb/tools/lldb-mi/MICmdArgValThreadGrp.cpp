//===-- MICmdArgValThreadGrp.cpp --------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

// In-house headers:
#include "MICmdArgValThreadGrp.h"
#include "MICmdArgContext.h"

//++
//------------------------------------------------------------------------------------
// Details: CMICmdArgValThreadGrp constructor.
// Type:    Method.
// Args:    None.
// Return:  None.
// Throws:  None.
//--
CMICmdArgValThreadGrp::CMICmdArgValThreadGrp() : m_nThreadGrp(0) {}

//++
//------------------------------------------------------------------------------------
// Details: CMICmdArgValThreadGrp constructor.
// Type:    Method.
// Args:    vrArgName       - (R) Argument's name to search by.
//          vbMandatory     - (R) True = Yes must be present, false = optional
//          argument.
//          vbHandleByCmd   - (R) True = Command processes *this option, false =
//          not handled.
// Return:  None.
// Throws:  None.
//--
CMICmdArgValThreadGrp::CMICmdArgValThreadGrp(const CMIUtilString &vrArgName,
                                             const bool vbMandatory,
                                             const bool vbHandleByCmd)
    : CMICmdArgValBaseTemplate(vrArgName, vbMandatory, vbHandleByCmd),
      m_nThreadGrp(0) {}

//++
//------------------------------------------------------------------------------------
// Details: CMICmdArgValThreadGrp destructor.
// Type:    Overridden.
// Args:    None.
// Return:  None.
// Throws:  None.
//--
CMICmdArgValThreadGrp::~CMICmdArgValThreadGrp() {}

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
bool CMICmdArgValThreadGrp::Validate(CMICmdArgContext &vwArgContext) {
  if (vwArgContext.IsEmpty())
    return m_bMandatory ? MIstatus::failure : MIstatus::success;

  if (vwArgContext.GetNumberArgsPresent() == 1) {
    const CMIUtilString &rArg(vwArgContext.GetArgsLeftToParse());
    if (IsArgThreadGrp(rArg) && ExtractNumber(rArg)) {
      m_bFound = true;
      m_bValid = true;
      m_argValue = GetNumber();
      vwArgContext.RemoveArg(rArg);
      return MIstatus::success;
    } else
      return MIstatus::failure;
  }

  // More than one option...
  const CMIUtilString::VecString_t vecOptions(vwArgContext.GetArgs());
  CMIUtilString::VecString_t::const_iterator it = vecOptions.begin();
  while (it != vecOptions.end()) {
    const CMIUtilString &rArg(*it);
    if (IsArgThreadGrp(rArg) && ExtractNumber(rArg)) {
      m_bFound = true;

      if (vwArgContext.RemoveArg(rArg)) {
        m_bValid = true;
        m_argValue = GetNumber();
        return MIstatus::success;
      } else
        return MIstatus::failure;
    }

    // Next
    ++it;
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
bool CMICmdArgValThreadGrp::IsArgThreadGrp(const CMIUtilString &vrTxt) const {
  // Look for i1 i2 i3....
  const MIint nPos = vrTxt.find('i');
  if (nPos != 0)
    return false;

  const CMIUtilString strNum = vrTxt.substr(1);
  return strNum.IsNumber();
}

//++
//------------------------------------------------------------------------------------
// Details: Extract the thread group number from the thread group argument.
// Type:    Method.
// Args:    vrTxt   - (R) Some text.
// Return:  MIstatus::success - Functional succeeded.
//          MIstatus::failure - Functional failed.
// Throws:  None.
//--
bool CMICmdArgValThreadGrp::ExtractNumber(const CMIUtilString &vrTxt) {
  const CMIUtilString strNum = vrTxt.substr(1);
  MIint64 nNumber = 0;
  bool bOk = strNum.ExtractNumber(nNumber);
  if (bOk) {
    m_nThreadGrp = static_cast<MIuint>(nNumber);
  }

  return bOk;
}

//++
//------------------------------------------------------------------------------------
// Details: Retrieve the thread group ID found in the argument.
// Type:    Method.
// Args:    None.
// Return:  MIuint - Thread group ID.
// Throws:  None.
//--
MIuint CMICmdArgValThreadGrp::GetNumber() const { return m_nThreadGrp; }
