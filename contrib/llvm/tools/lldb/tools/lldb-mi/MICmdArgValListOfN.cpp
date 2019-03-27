//===-- MICmdArgValListOfN.cpp ----------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

// In-house headers:
#include "MICmdArgValListOfN.h"
#include "MICmdArgContext.h"
#include "MICmdArgValFile.h"
#include "MICmdArgValNumber.h"
#include "MICmdArgValOptionLong.h"
#include "MICmdArgValOptionShort.h"
#include "MICmdArgValString.h"
#include "MICmdArgValThreadGrp.h"

//++
//------------------------------------------------------------------------------------
// Details: CMICmdArgValListOfN constructor.
// Type:    Method.
// Args:    None.
// Return:  None.
// Throws:  None.
//--
CMICmdArgValListOfN::CMICmdArgValListOfN() {}

//++
//------------------------------------------------------------------------------------
// Details: CMICmdArgValListOfN constructor.
// Type:    Method.
// Args:    vrArgName       - (R) Argument's name to search by.
//          vbMandatory     - (R) True = Yes must be present, false = optional
//          argument.
//          vbHandleByCmd   - (R) True = Command processes *this option, false =
//          not handled.
//          veType          - (R) The type of argument to look for and create
//          argument object of a certain type.
// Return:  None.
// Throws:  None.
//--
CMICmdArgValListOfN::CMICmdArgValListOfN(const CMIUtilString &vrArgName,
                                         const bool vbMandatory,
                                         const bool vbHandleByCmd,
                                         const ArgValType_e veType)
    : CMICmdArgValListBase(vrArgName, vbMandatory, vbHandleByCmd, veType) {}

//++
//------------------------------------------------------------------------------------
// Details: CMICmdArgValListOfN destructor.
// Type:    Overridden.
// Args:    None.
// Return:  None.
// Throws:  None.
//--
CMICmdArgValListOfN::~CMICmdArgValListOfN() {}

//++
//------------------------------------------------------------------------------------
// Details: Parse the command's argument options string and try to extract the
// list of
//          arguments based on the argument object type to look for.
// Type:    Overridden.
// Args:    vwArgContext    - (RW) The command's argument options string.
// Return:  MIstatus::success - Functional succeeded.
//          MIstatus::failure - Functional failed.
// Throws:  None.
//--
bool CMICmdArgValListOfN::Validate(CMICmdArgContext &vwArgContext) {
  if (m_eArgType >= eArgValType_count) {
    m_eArgType = eArgValType_invalid;
    return MIstatus::failure;
  }

  if (vwArgContext.IsEmpty())
    return m_bMandatory ? MIstatus::failure : MIstatus::success;

  const CMIUtilString &rArg(vwArgContext.GetArgsLeftToParse());
  if (IsListOfN(rArg) && CreateList(rArg)) {
    m_bFound = true;
    m_bValid = true;
    vwArgContext.RemoveArg(rArg);
    return MIstatus::success;
  } else
    return MIstatus::failure;
}

//++
//------------------------------------------------------------------------------------
// Details: Create list of argument objects each holding a value extract from
// the command
//          options line.
// Type:    Method.
// Args:    vrTxt   - (R) Some options text.
// Return:  bool -  True = yes valid arg, false = no.
// Throws:  None.
//--
bool CMICmdArgValListOfN::CreateList(const CMIUtilString &vrTxt) {
  CMIUtilString::VecString_t vecOptions;
  if ((m_eArgType == eArgValType_StringQuoted) ||
      (m_eArgType == eArgValType_StringQuotedNumber) ||
      (m_eArgType == eArgValType_StringQuotedNumberPath) ||
      (m_eArgType == eArgValType_StringAnything)) {
    if (vrTxt.SplitConsiderQuotes(" ", vecOptions) == 0)
      return MIstatus::failure;
  } else if (vrTxt.Split(" ", vecOptions) == 0)
    return MIstatus::failure;

  CMIUtilString::VecString_t::const_iterator it = vecOptions.begin();
  while (it != vecOptions.end()) {
    const CMIUtilString &rOption = *it;
    CMICmdArgValBase *pOption = CreationObj(rOption, m_eArgType);
    if (pOption != nullptr)
      m_argValue.push_back(pOption);
    else
      return MIstatus::failure;

    // Next
    ++it;
  }

  return MIstatus::success;
}

//++
//------------------------------------------------------------------------------------
// Details: Examine the string and determine if it is a valid string type
// argument.
// Type:    Method.
// Args:    vrTxt   - (R) Some text.
// Return:  bool -  True = yes valid arg, false = no.
// Throws:  None.
//--
bool CMICmdArgValListOfN::IsListOfN(const CMIUtilString &vrTxt) const {
  CMIUtilString::VecString_t vecOptions;
  if ((m_eArgType == eArgValType_StringQuoted) ||
      (m_eArgType == eArgValType_StringQuotedNumber) ||
      (m_eArgType == eArgValType_StringQuotedNumberPath) ||
      (m_eArgType == eArgValType_StringAnything)) {
    if (vrTxt.SplitConsiderQuotes(" ", vecOptions) == 0)
      return false;
  } else if (vrTxt.Split(" ", vecOptions) == 0)
    return false;

  CMIUtilString::VecString_t::const_iterator it = vecOptions.begin();
  while (it != vecOptions.end()) {
    const CMIUtilString &rOption = *it;
    if (!IsExpectedCorrectType(rOption, m_eArgType))
      break;

    // Next
    ++it;
  }

  return true;
}

//++
//------------------------------------------------------------------------------------
// Details: Retrieve the list of CMICmdArgValBase derived option objects found
// following
//          *this long option argument. For example "list-thread-groups [
//          --recurse 1 ]"
//          where 1 is the list of expected option to follow.
// Type:    Method.
// Args:    None.
// Return:  CMICmdArgValListBase::VecArgObjPtr_t & -    List of options.
// Throws:  None.
//--
const CMICmdArgValListBase::VecArgObjPtr_t &
CMICmdArgValListOfN::GetExpectedOptions() const {
  return m_argValue;
}
