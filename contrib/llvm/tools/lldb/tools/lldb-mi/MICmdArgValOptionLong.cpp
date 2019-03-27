//===-- MICmdArgValOptionLong.cpp -------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

// In-house headers:
#include "MICmdArgValOptionLong.h"
#include "MICmdArgContext.h"

//++
//------------------------------------------------------------------------------------
// Details: CMICmdArgValOptionLong constructor.
// Type:    Method.
// Args:    None.
// Return:  None.
// Throws:  None.
//--
CMICmdArgValOptionLong::CMICmdArgValOptionLong()
    : m_nExpectingNOptions(0), m_eExpectingOptionType(eArgValType_invalid) {}

//++
//------------------------------------------------------------------------------------
// Details: CMICmdArgValOptionLong constructor.
// Type:    Method.
// Args:    vrArgName       - (R) Argument's name to search by.
//          vbMandatory     - (R) True = Yes must be present, false = optional
//          argument.
//          vbHandleByCmd   - (R) True = Command processes *this option, false =
//          not handled.
// Return:  None.
// Throws:  None.
//--
CMICmdArgValOptionLong::CMICmdArgValOptionLong(const CMIUtilString &vrArgName,
                                               const bool vbMandatory,
                                               const bool vbHandleByCmd)
    : CMICmdArgValListBase(vrArgName, vbMandatory, vbHandleByCmd),
      m_nExpectingNOptions(0), m_eExpectingOptionType(eArgValType_invalid) {}

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
CMICmdArgValOptionLong::CMICmdArgValOptionLong(const CMIUtilString &vrArgName,
                                               const bool vbMandatory,
                                               const bool vbHandleByCmd,
                                               const ArgValType_e veType,
                                               const MIuint vnExpectingNOptions)
    : CMICmdArgValListBase(vrArgName, vbMandatory, vbHandleByCmd),
      m_nExpectingNOptions(vnExpectingNOptions),
      m_eExpectingOptionType(veType) {}

//++
//------------------------------------------------------------------------------------
// Details: CMICmdArgValOptionLong destructor.
// Type:    Overridden.
// Args:    None.
// Return:  None.
// Throws:  None.
//--
CMICmdArgValOptionLong::~CMICmdArgValOptionLong() {
  // Tidy up
  Destroy();
}

//++
//------------------------------------------------------------------------------------
// Details: Tear down resources used by *this object.
// Type:    Method.
// Args:    None.
// Return:  None.
// Throws:  None.
//--
void CMICmdArgValOptionLong::Destroy() {
  // Tidy up
  VecArgObjPtr_t::const_iterator it = m_vecArgsExpected.begin();
  while (it != m_vecArgsExpected.end()) {
    CMICmdArgValBase *pOptionObj = *it;
    delete pOptionObj;

    // Next
    ++it;
  }
  m_vecArgsExpected.clear();
}

//++
//------------------------------------------------------------------------------------
// Details: Parse the command's argument options string and try to extract the
// long
//          argument *this argument type is looking for.
// Type:    Overridden.
// Args:    vwArgContext    - (RW) The command's argument options string.
// Return:  MIstatus::success - Functional succeeded.
//          MIstatus::failure - Functional failed.
// Throws:  None.
//--
bool CMICmdArgValOptionLong::Validate(CMICmdArgContext &vwArgContext) {
  if (vwArgContext.IsEmpty())
    return m_bMandatory ? MIstatus::failure : MIstatus::success;

  if (vwArgContext.GetNumberArgsPresent() == 1) {
    const CMIUtilString &rArg(vwArgContext.GetArgsLeftToParse());
    if (IsArgLongOption(rArg) && ArgNameMatch(rArg)) {
      m_bFound = true;

      if (!vwArgContext.RemoveArg(rArg))
        return MIstatus::failure;

      if (m_nExpectingNOptions == 0) {
        m_bValid = true;
        return MIstatus::success;
      }

      m_bIsMissingOptions = true;
      return MIstatus::failure;
    } else
      return MIstatus::failure;
  }

  // More than one option...
  MIuint nArgIndex = 0;
  const CMIUtilString::VecString_t vecOptions(vwArgContext.GetArgs());
  CMIUtilString::VecString_t::const_iterator it = vecOptions.begin();
  while (it != vecOptions.end()) {
    const CMIUtilString &rArg(*it);
    if (IsArgOptionCorrect(rArg) && ArgNameMatch(rArg)) {
      m_bFound = true;

      if (!vwArgContext.RemoveArg(rArg))
        return MIstatus::failure;

      if (m_nExpectingNOptions != 0) {
        if (ExtractExpectedOptions(vwArgContext, nArgIndex)) {
          m_bValid = true;
          return MIstatus::success;
        }

        m_bIsMissingOptions = true;
        return MIstatus::failure;
      } else {
        m_bValid = true;
        return MIstatus::success;
      }
    }

    // Next
    ++it;
    ++nArgIndex;
  }

  return MIstatus::failure;
}

//++
//------------------------------------------------------------------------------------
// Details: Parse the text following *this argument and extract the options the
// values of
//          CMICmdArgValListBase::m_eArgType forming argument objects for each
//          of those
//          options extracted.
// Type:    Method.
// Args:    vrwTxt      - (RW)  The command's argument options string.
//          nArgIndex   - (R)   The Nth arg position in argument context from
//          the left.
// Return:  MIstatus::success - Functional succeeded.
//          MIstatus::failure - Functional failed.
// Throws:  None.
//--
bool CMICmdArgValOptionLong::ExtractExpectedOptions(CMICmdArgContext &vrwTxt,
                                                    const MIuint nArgIndex) {
  CMIUtilString::VecString_t vecOptions = vrwTxt.GetArgs();
  if (vecOptions.size() == 0)
    return MIstatus::failure;

  MIuint nArgIndexCnt = 0;
  MIuint nTypeCnt = 0;
  MIuint nTypeCnt2 = 0;
  MIuint nFoundNOptionsCnt = 0;
  CMIUtilString::VecString_t::const_iterator it = vecOptions.begin();
  while (it != vecOptions.end()) {
    // Move to the Nth argument position from left before do validation/checking
    if (nArgIndexCnt++ == nArgIndex) {
      nTypeCnt++;
      const CMIUtilString &rOption(*it);
      if (IsExpectedCorrectType(rOption, m_eExpectingOptionType)) {
        nTypeCnt2++;
        CMICmdArgValBase *pOptionObj =
            CreationObj(rOption, m_eExpectingOptionType);
        if ((pOptionObj != nullptr) &&
            vrwTxt.RemoveArgAtPos(rOption, nArgIndex)) {
          nFoundNOptionsCnt++;
          m_vecArgsExpected.push_back(pOptionObj);
        }
      }

      // Is the sequence 'options' of same type broken. Expecting the same type
      // until the
      // next argument.
      if (nTypeCnt != nTypeCnt2)
        return MIstatus::failure;

      if (nFoundNOptionsCnt == m_nExpectingNOptions)
        return MIstatus::success;
    }

    // Next
    ++it;
  }
  if (nFoundNOptionsCnt != m_nExpectingNOptions)
    return MIstatus::failure;

  return MIstatus::success;
}

//++
//------------------------------------------------------------------------------------
// Details: Examine the string and determine if it is a valid long type option
// argument.
//          Long type argument looks like --someLongOption.
// Type:    Method.
// Args:    vrTxt   - (R) Some text.
// Return:  bool    - True = yes valid arg, false = no.
// Throws:  None.
//--
bool CMICmdArgValOptionLong::IsArgLongOption(const CMIUtilString &vrTxt) const {
  const bool bHavePosSlash = (vrTxt.find('/') != std::string::npos);
  const bool bHaveBckSlash = (vrTxt.find('\\') != std::string::npos);
  if (bHavePosSlash || bHaveBckSlash)
    return false;

  const size_t nPos = vrTxt.find("--");
  if (nPos != 0)
    return false;

  if (vrTxt.length() < 3)
    return false;

  const CMIUtilString strArg = vrTxt.substr(2);
  return !strArg.IsNumber();
}

//++
//------------------------------------------------------------------------------------
// Details: Examine the string and determine if it is a valid long type option
// argument.
//          Long type argument looks like --someLongOption.
// Type:    Overideable.
// Args:    vrTxt   - (R) Some text.
// Return:  bool    - True = yes valid arg, false = no.
// Throws:  None.
//--
bool CMICmdArgValOptionLong::IsArgOptionCorrect(
    const CMIUtilString &vrTxt) const {
  return IsArgLongOption(vrTxt);
}

//++
//------------------------------------------------------------------------------------
// Details: Does the argument name of the argument being parsed ATM match the
// name of
//          *this argument object.
// Type:    Method.
// Args:    vrTxt   - (R) Some text.
// Return:  bool    - True = yes arg name matched, false = no.
// Throws:  None.
//--
bool CMICmdArgValOptionLong::ArgNameMatch(const CMIUtilString &vrTxt) const {
  const CMIUtilString strArg = vrTxt.substr(2);
  return (strArg == GetName());
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
// Return:  CMICmdArgValListBase::VecArgObjPtr_t & - List of options.
// Throws:  None.
//--
const CMICmdArgValListBase::VecArgObjPtr_t &
CMICmdArgValOptionLong::GetExpectedOptions() const {
  return m_vecArgsExpected;
}
