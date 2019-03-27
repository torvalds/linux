//===-- MICmdArgValOptionShort.cpp ------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

// In-house headers:
#include "MICmdArgValOptionShort.h"
#include "MICmdArgContext.h"

//++
//------------------------------------------------------------------------------------
// Details: CMICmdArgValOptionShort constructor.
// Type:    Method.
// Args:    None.
// Return:  None.
// Throws:  None.
//--
CMICmdArgValOptionShort::CMICmdArgValOptionShort() {}

//++
//------------------------------------------------------------------------------------
// Details: CMICmdArgValOptionShort constructor.
// Type:    Method.
// Args:    vrArgName       - (R) Argument's name to search by.
//          vbMandatory     - (R) True = Yes must be present, false = optional
//          argument.
//          vbHandleByCmd   - (R) True = Command processes *this option, false =
//          not handled.
// Return:  None.
// Throws:  None.
//--
CMICmdArgValOptionShort::CMICmdArgValOptionShort(const CMIUtilString &vrArgName,
                                                 const bool vbMandatory,
                                                 const bool vbHandleByCmd)
    : CMICmdArgValOptionLong(vrArgName, vbMandatory, vbHandleByCmd) {}

//++
//------------------------------------------------------------------------------------
// Details: CMICmdArgValOptionLong constructor.
// Type:    Method.
// Args:    vrArgName           - (R) Argument's name to search by.
//          vbMandatory         - (R) True = Yes must be present, false =
//          optional argument.
//          vbHandleByCmd       - (R) True = Command processes *this option,
//          false = not handled.
//          veType              - (R) The type of argument to look for and
//          create argument object of a certain type.
//          vnExpectingNOptions - (R) The number of options expected to read
//          following *this argument.
// Return:  None.
// Throws:  None.
//--
CMICmdArgValOptionShort::CMICmdArgValOptionShort(
    const CMIUtilString &vrArgName, const bool vbMandatory,
    const bool vbHandleByCmd, const ArgValType_e veType,
    const MIuint vnExpectingNOptions)
    : CMICmdArgValOptionLong(vrArgName, vbMandatory, vbHandleByCmd, veType,
                             vnExpectingNOptions) {}

//++
//------------------------------------------------------------------------------------
// Details: CMICmdArgValOptionShort destructor.
// Type:    Overridden.
// Args:    None.
// Return:  None.
// Throws:  None.
//--
CMICmdArgValOptionShort::~CMICmdArgValOptionShort() {}

//++
//------------------------------------------------------------------------------------
// Details: Examine the string and determine if it is a valid short type option
// argument.
// Type:    Method.
// Args:    vrTxt   - (R) Some text.
// Return:  bool    - True = yes valid arg, false = no.
// Throws:  None.
//--
bool CMICmdArgValOptionShort::IsArgShortOption(
    const CMIUtilString &vrTxt) const {
  // Look for --someLongOption
  MIint nPos = vrTxt.find("--");
  if (nPos == 0)
    return false;

  // Look for -f short option
  nPos = vrTxt.find('-');
  if (nPos != 0)
    return false;

  if (vrTxt.length() > 2)
    return false;

  return true;
}

//++
//------------------------------------------------------------------------------------
// Details: Examine the string and determine if it is a valid short type option
// argument.
//          Long type argument looks like -f some short option.
// Type:    Overridden.
// Args:    vrTxt   - (R) Some text.
// Return:  bool    - True = yes valid arg, false = no.
// Throws:  None.
//--
bool CMICmdArgValOptionShort::IsArgOptionCorrect(
    const CMIUtilString &vrTxt) const {
  return IsArgShortOption(vrTxt);
}

//++
//------------------------------------------------------------------------------------
// Details: Does the argument name of the argument being parsed ATM match the
// name of
//          *this argument object.
// Type:    Overridden.
// Args:    vrTxt   - (R) Some text.
// Return:  bool    - True = yes arg name matched, false = no.
// Throws:  None.
//--
bool CMICmdArgValOptionShort::ArgNameMatch(const CMIUtilString &vrTxt) const {
  const CMIUtilString strArg = vrTxt.substr(1);
  return (strArg == GetName());
}
