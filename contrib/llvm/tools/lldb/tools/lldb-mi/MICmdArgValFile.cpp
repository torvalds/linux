//===-- MICmdArgValFile.cpp -------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

// In-house headers:
#include "MICmdArgValFile.h"
#include "MICmdArgContext.h"

//++
//------------------------------------------------------------------------------------
// Details: CMICmdArgValFile constructor.
// Type:    Method.
// Args:    None.
// Return:  None.
// Throws:  None.
//--
CMICmdArgValFile::CMICmdArgValFile() {}

//++
//------------------------------------------------------------------------------------
// Details: CMICmdArgValFile constructor.
// Type:    Method.
// Args:    vrArgName       - (R) Argument's name to search by.
//          vbMandatory     - (R) True = Yes must be present, false = optional
//          argument.
//          vbHandleByCmd   - (R) True = Command processes *this option, false =
//          not handled.
// Return:  None.
// Throws:  None.
//--
CMICmdArgValFile::CMICmdArgValFile(const CMIUtilString &vrArgName,
                                   const bool vbMandatory,
                                   const bool vbHandleByCmd)
    : CMICmdArgValBaseTemplate(vrArgName, vbMandatory, vbHandleByCmd) {}

//++
//------------------------------------------------------------------------------------
// Details: CMICmdArgValFile destructor.
// Type:    Overridden.
// Args:    None.
// Return:  None.
// Throws:  None.
//--
CMICmdArgValFile::~CMICmdArgValFile() {}

//++
//------------------------------------------------------------------------------------
// Details: Parse the command's argument options string and try to extract the
// value *this
//          argument is looking for.
// Type:    Overridden.
// Args:    vwArgContext    - (R) The command's argument options string.
// Return:  MIstatus::success - Functional succeeded.
//          MIstatus::failure - Functional failed.
// Throws:  None.
//--
bool CMICmdArgValFile::Validate(CMICmdArgContext &vwArgContext) {
  if (vwArgContext.IsEmpty())
    return m_bMandatory ? MIstatus::failure : MIstatus::success;

  // The GDB/MI spec suggests there is only parameter

  if (vwArgContext.GetNumberArgsPresent() == 1) {
    const CMIUtilString &rFile(vwArgContext.GetArgsLeftToParse());
    if (IsFilePath(rFile)) {
      m_bFound = true;
      m_bValid = true;
      m_argValue = rFile.Trim('"');
      vwArgContext.RemoveArg(rFile);
      return MIstatus::success;
    } else
      return MIstatus::failure;
  }

  // In reality there are more than one option,  if so the file option
  // is the last one (don't handle that here - find the best looking one)
  const CMIUtilString::VecString_t vecOptions(vwArgContext.GetArgs());
  CMIUtilString::VecString_t::const_iterator it = vecOptions.begin();
  while (it != vecOptions.end()) {
    const CMIUtilString &rTxt(*it);
    if (IsFilePath(rTxt)) {
      m_bFound = true;

      if (vwArgContext.RemoveArg(rTxt)) {
        m_bValid = true;
        m_argValue = rTxt.Trim('"');
        return MIstatus::success;
      } else
        return MIstatus::success;
    }

    // Next
    ++it;
  }

  return MIstatus::failure;
}

//++
//------------------------------------------------------------------------------------
// Details: Given some text extract the file name path from it. If a space is
// found in
//          path done return the path surrounded in quotes.
// Type:    Method.
// Args:    vrTxt   - (R) The text to extract the file name path from.
// Return:  CMIUtilString - File name and or path.
// Throws:  None.
//--
CMIUtilString
CMICmdArgValFile::GetFileNamePath(const CMIUtilString &vrTxt) const {
  CMIUtilString fileNamePath(vrTxt);

  // Look for a space in the path
  const char cSpace = ' ';
  const size_t nPos = fileNamePath.find(cSpace);
  if (nPos != std::string::npos)
    fileNamePath = CMIUtilString::Format("\"%s\"", fileNamePath.c_str());

  return fileNamePath;
}

//++
//------------------------------------------------------------------------------------
// Details: Examine the string and determine if it is a valid file name path.
// Type:    Method.
// Args:    vrFileNamePath  - (R) File's name and directory path.
// Return:  bool -  True = yes valid file path, false = no.
// Throws:  None.
//--
bool CMICmdArgValFile::IsFilePath(const CMIUtilString &vrFileNamePath) const {
  if (vrFileNamePath.empty())
    return false;

  const bool bHavePosSlash = (vrFileNamePath.find('/') != std::string::npos);
  const bool bHaveBckSlash = (vrFileNamePath.find('\\') != std::string::npos);

  // Look for --someLongOption
  size_t nPos = vrFileNamePath.find("--");
  const bool bLong = (nPos == 0);
  if (bLong)
    return false;

  // Look for -f type short parameters
  nPos = vrFileNamePath.find('-');
  const bool bShort = (nPos == 0);
  if (bShort)
    return false;

  // Look for i1 i2 i3....
  nPos = vrFileNamePath.find('i');
  const bool bFoundI1 = ((nPos == 0) && (::isdigit(vrFileNamePath[1])));
  if (bFoundI1)
    return false;

  const bool bValidChars = IsValidChars(vrFileNamePath);
  return bValidChars || bHavePosSlash || bHaveBckSlash;
}

//++
//------------------------------------------------------------------------------------
// Details: Determine if the path contains valid characters for a file path.
// Letters can be
//          either upper or lower case.
// Type:    Method.
// Args:    vrText  - (R) The text data to examine.
// Return:  bool - True = yes valid, false = one or more chars is valid.
// Throws:  None.
//--
bool CMICmdArgValFile::IsValidChars(const CMIUtilString &vrText) const {
  static CMIUtilString s_strSpecialCharacters(".'\"`@#$%^&*()_+-={}[]| ");
  const char *pPtr = vrText.c_str();
  for (MIuint i = 0; i < vrText.length(); i++, pPtr++) {
    const char c = *pPtr;
    if (::isalnum((int)c) == 0) {
      if (s_strSpecialCharacters.find(c) == CMIUtilString::npos)
        return false;
    }
  }

  return true;
}
