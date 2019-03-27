//===-- MICmdArgContext.cpp -------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

// In-house headers:
#include "MICmdArgContext.h"

//++
//------------------------------------------------------------------------------------
// Details: CMICmdArgContext constructor.
// Type:    Method.
// Args:    None.
// Return:  None.
// Throws:  None.
//--
CMICmdArgContext::CMICmdArgContext() {}

//++
//------------------------------------------------------------------------------------
// Details: CMICmdArgContext constructor.
// Type:    Method.
// Args:    vrCmdLineArgsRaw    - (R) The text description of the arguments
// options.
// Return:  None.
// Throws:  None.
//--
CMICmdArgContext::CMICmdArgContext(const CMIUtilString &vrCmdLineArgsRaw)
    : m_strCmdArgsAndOptions(vrCmdLineArgsRaw) {}

//++
//------------------------------------------------------------------------------------
// Details: CMICmdArgContext destructor.
// Type:    Method.
// Args:    None.
// Return:  None.
// Throws:  None.
//--
CMICmdArgContext::~CMICmdArgContext() {}

//++
//------------------------------------------------------------------------------------
// Details: Retrieve the remainder of the command's argument options left to
// parse.
// Type:    Method.
// Args:    None.
// Return:  CMIUtilString & - Argument options text.
// Throws:  None.
//--
const CMIUtilString &CMICmdArgContext::GetArgsLeftToParse() const {
  return m_strCmdArgsAndOptions;
}

//++
//------------------------------------------------------------------------------------
// Details: Ask if this arguments string has any arguments.
// Type:    Method.
// Args:    None.
// Return:  bool    - True = Has one or more arguments present, false = no
// arguments.
// Throws:  None.
//--
bool CMICmdArgContext::IsEmpty() const {
  return m_strCmdArgsAndOptions.empty();
}

//++
//------------------------------------------------------------------------------------
// Details: Remove the argument from the options text and any space after the
// argument
//          if applicable.
// Type:    Method.
// Args:    vArg    - (R) The name of the argument.
// Return:  MIstatus::success - Functional succeeded.
//          MIstatus::failure - Functional failed.
// Throws:  None.
//--
bool CMICmdArgContext::RemoveArg(const CMIUtilString &vArg) {
  if (vArg.empty())
    return MIstatus::success;

  const size_t nLen = vArg.length();
  const size_t nLenCntxt = m_strCmdArgsAndOptions.length();
  if (nLen > nLenCntxt)
    return MIstatus::failure;

  size_t nExtraSpace = 0;
  size_t nPos = m_strCmdArgsAndOptions.find(vArg);
  while (1) {
    if (nPos == std::string::npos)
      return MIstatus::success;

    bool bPass1 = false;
    if (nPos != 0) {
      if (m_strCmdArgsAndOptions[nPos - 1] == ' ')
        bPass1 = true;
    } else
      bPass1 = true;

    const size_t nEnd = nPos + nLen;

    if (bPass1) {
      bool bPass2 = false;
      if (nEnd < nLenCntxt) {
        if (m_strCmdArgsAndOptions[nEnd] == ' ') {
          bPass2 = true;
          nExtraSpace = 1;
        }
      } else
        bPass2 = true;

      if (bPass2)
        break;
    }

    nPos = m_strCmdArgsAndOptions.find(vArg, nEnd);
  }

  const size_t nPosEnd = nLen + nExtraSpace;
  m_strCmdArgsAndOptions = m_strCmdArgsAndOptions.replace(nPos, nPosEnd, "");
  m_strCmdArgsAndOptions = m_strCmdArgsAndOptions.Trim();

  return MIstatus::success;
}

//++
//------------------------------------------------------------------------------------
// Details: Remove the argument at the Nth word position along in the context
// string.
//          Any space after the argument is removed if applicable. A search is
//          not
//          performed as there may be more than one vArg with the same 'name' in
//          the
//          context string.
// Type:    Method.
// Args:    vArg        - (R) The name of the argument.
//          nArgIndex   - (R) The word count position to which to remove the
//          vArg word.
// Return:  MIstatus::success - Functional succeeded.
//          MIstatus::failure - Functional failed.
// Throws:  None.
//--
bool CMICmdArgContext::RemoveArgAtPos(const CMIUtilString &vArg,
                                      size_t nArgIndex) {
  size_t nWordIndex = 0;
  CMIUtilString strBuildContextUp;
  const CMIUtilString::VecString_t vecWords(GetArgs());
  const bool bSpaceRequired(GetNumberArgsPresent() > 2);

  CMIUtilString::VecString_t::const_iterator it = vecWords.begin();
  const CMIUtilString::VecString_t::const_iterator itEnd = vecWords.end();
  while (it != itEnd) {
    const CMIUtilString &rWord(*it);
    if (nWordIndex++ != nArgIndex) {
      // Single words
      strBuildContextUp += rWord;
      if (bSpaceRequired)
        strBuildContextUp += " ";
    } else {
      // If quoted loose quoted text
      if (++it != itEnd) {
        CMIUtilString words = rWord;
        while (vArg != words) {
          if (bSpaceRequired)
            words += " ";
          words += *it;
          if (++it == itEnd)
            break;
        }
        if (it != itEnd)
          --it;
      }
    }

    // Next
    if (it != itEnd)
      ++it;
  }

  m_strCmdArgsAndOptions = strBuildContextUp;
  m_strCmdArgsAndOptions = m_strCmdArgsAndOptions.Trim();

  return MIstatus::success;
}

//++
//------------------------------------------------------------------------------------
// Details: Retrieve number of arguments or options present in the command's
// option text.
// Type:    Method.
// Args:    None.
// Return:  size_t  - 0 to n arguments present.
// Throws:  None.
//--
size_t CMICmdArgContext::GetNumberArgsPresent() const {
  CMIUtilString::VecString_t vecOptions;
  return m_strCmdArgsAndOptions.SplitConsiderQuotes(" ", vecOptions);
}

//++
//------------------------------------------------------------------------------------
// Details: Retrieve all the arguments or options remaining in *this context.
// Type:    Method.
// Args:    None.
// Return:  MIUtilString::VecString_t   - List of args remaining.
// Throws:  None.
//--
CMIUtilString::VecString_t CMICmdArgContext::GetArgs() const {
  CMIUtilString::VecString_t vecOptions;
  m_strCmdArgsAndOptions.SplitConsiderQuotes(" ", vecOptions);
  return vecOptions;
}

//++
//------------------------------------------------------------------------------------
// Details: Copy assignment operator.
// Type:    Method.
// Args:    vOther  - (R) The variable to copy from.
// Return:  CMIUtilString & - this object.
// Throws:  None.
//--
CMICmdArgContext &CMICmdArgContext::operator=(const CMICmdArgContext &vOther) {
  if (this != &vOther) {
    m_strCmdArgsAndOptions = vOther.m_strCmdArgsAndOptions;
  }

  return *this;
}
