//===-- MIUtilString.cpp ----------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

// Third party headers
#include "llvm/Support/Compiler.h"
#include <cstdlib>
#include <inttypes.h>
#include <limits.h>
#include <memory>
#include <sstream>
#include <stdarg.h>
#include <string.h>

// In-house headers:
#include "MIUtilString.h"

//++
//------------------------------------------------------------------------------------
// Details: CMIUtilString constructor.
// Type:    Method.
// Args:    None.
// Return:  None.
// Throws:  None.
//--
CMIUtilString::CMIUtilString() : std::string() {}

//++
//------------------------------------------------------------------------------------
// Details: CMIUtilString constructor.
// Type:    Method.
// Args:    vpData  - Pointer to UTF8 text data.
// Return:  None.
// Throws:  None.
//--
CMIUtilString::CMIUtilString(const char *vpData) : std::string(vpData) {}

//++
//------------------------------------------------------------------------------------
// Details: CMIUtilString constructor.
// Type:    Method.
// Args:    vpStr  - Text data.
// Return:  None.
// Throws:  None.
//--
CMIUtilString::CMIUtilString(const std::string &vrStr) : std::string(vrStr) {}

//++
//------------------------------------------------------------------------------------
// Details: CMIUtilString assignment operator.
// Type:    Method.
// Args:    vpRhs   - Pointer to UTF8 text data.
// Return:  CMIUtilString & - *this string.
// Throws:  None.
//--
CMIUtilString &CMIUtilString::operator=(const char *vpRhs) {
  assign(vpRhs);
  return *this;
}

//++
//------------------------------------------------------------------------------------
// Details: CMIUtilString assignment operator.
// Type:    Method.
// Args:    vrRhs   - The other string to copy from.
// Return:  CMIUtilString & - *this string.
// Throws:  None.
//--
CMIUtilString &CMIUtilString::operator=(const std::string &vrRhs) {
  assign(vrRhs);
  return *this;
}

//++
//------------------------------------------------------------------------------------
// Details: CMIUtilString destructor.
// Type:    Method.
// Args:    None.
// Return:  None.
// Throws:  None.
//--
CMIUtilString::~CMIUtilString() {}

//++
//------------------------------------------------------------------------------------
// Details: Perform a snprintf format style on a string data. A new string
// object is
//          created and returned.
// Type:    Static method.
// Args:    vrFormat      - (R) Format string data instruction.
//          vArgs         - (R) Var list args of any type.
// Return:  CMIUtilString - Number of splits found in the string data.
// Throws:  None.
//--
CMIUtilString CMIUtilString::FormatPriv(const CMIUtilString &vrFormat,
                                        va_list vArgs) {
  CMIUtilString strResult;
  MIint nFinal = 0;
  MIint n = vrFormat.size();

  // IOR: mysterious crash in this function on some windows builds not able to
  // duplicate
  // but found article which may be related. Crash occurs in vsnprintf() or
  // va_copy()
  // Duplicate vArgs va_list argument pointer to ensure that it can be safely
  // used in
  // a new frame
  // http://julipedia.meroh.net/2011/09/using-vacopy-to-safely-pass-ap.html
  va_list argsDup;
  va_copy(argsDup, vArgs);

  // Create a copy va_list to reset when we spin
  va_list argsCpy;
  va_copy(argsCpy, argsDup);

  if (n == 0)
    return strResult;

  n = n << 4; // Reserve 16 times as much the length of the vrFormat

  std::unique_ptr<char[]> pFormatted;
  while (1) {
    pFormatted.reset(new char[n + 1]); // +1 for safety margin
    ::strncpy(&pFormatted[0], vrFormat.c_str(), n);

    //  We need to restore the variable argument list pointer to the start again
    //  before running vsnprintf() more then once
    va_copy(argsDup, argsCpy);

    nFinal = ::vsnprintf(&pFormatted[0], n, vrFormat.c_str(), argsDup);
    if ((nFinal < 0) || (nFinal >= n))
      n += abs(nFinal - n + 1);
    else
      break;
  }

  va_end(argsCpy);
  va_end(argsDup);

  strResult = pFormatted.get();

  return strResult;
}

//++
//------------------------------------------------------------------------------------
// Details: Perform a snprintf format style on a string data. A new string
// object is
//          created and returned.
// Type:    Static method.
// Args:    vFormat       - (R) Format string data instruction.
//          ...           - (R) Var list args of any type.
// Return:  CMIUtilString - Number of splits found in the string data.
// Throws:  None.
//--
CMIUtilString CMIUtilString::Format(const char *vFormating, ...) {
  va_list args;
  va_start(args, vFormating);
  CMIUtilString strResult = CMIUtilString::FormatPriv(vFormating, args);
  va_end(args);

  return strResult;
}

//++
//------------------------------------------------------------------------------------
// Details: Perform a snprintf format style on a string data. A new string
// object is
//          created and returned.
// Type:    Static method.
// Args:    vrFormat      - (R) Format string data instruction.
//          vArgs         - (R) Var list args of any type.
// Return:  CMIUtilString - Number of splits found in the string data.
// Throws:  None.
//--
CMIUtilString CMIUtilString::FormatValist(const CMIUtilString &vrFormating,
                                          va_list vArgs) {
  return CMIUtilString::FormatPriv(vrFormating, vArgs);
}

//++
//------------------------------------------------------------------------------------
// Details: Splits string into array of strings using delimiter. If multiple
// delimiter
//          are found in sequence then they are not added to the list of splits.
// Type:    Method.
// Args:    vData       - (R) String data to be split up.
//          vDelimiter  - (R) Delimiter char or text.
//          vwVecSplits - (W) Container of splits found in string data.
// Return:  size_t - Number of splits found in the string data.
// Throws:  None.
//--
size_t CMIUtilString::Split(const CMIUtilString &vDelimiter,
                            VecString_t &vwVecSplits) const {
  vwVecSplits.clear();

  if (this->empty() || vDelimiter.empty())
    return 0;

  const size_t nLen(length());
  size_t nOffset(0);
  do {
    // Find first occurrence which doesn't match to the delimiter
    const size_t nSectionPos(FindFirstNot(vDelimiter, nOffset));
    if (nSectionPos == std::string::npos)
      break;

    // Find next occurrence of the delimiter after section
    size_t nNextDelimiterPos(FindFirst(vDelimiter, nSectionPos));
    if (nNextDelimiterPos == std::string::npos)
      nNextDelimiterPos = nLen;

    // Extract string between delimiters
    const size_t nSectionLen(nNextDelimiterPos - nSectionPos);
    const std::string strSection(substr(nSectionPos, nSectionLen));
    vwVecSplits.push_back(strSection);

    // Next
    nOffset = nNextDelimiterPos + 1;
  } while (nOffset < nLen);

  return vwVecSplits.size();
}

//++
//------------------------------------------------------------------------------------
// Details: Splits string into array of strings using delimiter. However the
// string is
//          also considered for text surrounded by quotes. Text with quotes
//          including the
//          delimiter is treated as a whole. If multiple delimiter are found in
//          sequence
//          then they are not added to the list of splits. Quotes that are
//          embedded in
//          the string as string formatted quotes are ignored (proceeded by a
//          '\\') i.e.
//          "\"MI GDB local C++.cpp\":88".
// Type:    Method.
// Args:    vData       - (R) String data to be split up.
//          vDelimiter  - (R) Delimiter char or text.
//          vwVecSplits - (W) Container of splits found in string data.
// Return:  size_t - Number of splits found in the string data.
// Throws:  None.
//--
size_t CMIUtilString::SplitConsiderQuotes(const CMIUtilString &vDelimiter,
                                          VecString_t &vwVecSplits) const {
  vwVecSplits.clear();

  if (this->empty() || vDelimiter.empty())
    return 0;

  const size_t nLen(length());
  size_t nOffset(0);
  do {
    // Find first occurrence which doesn't match to the delimiter
    const size_t nSectionPos(FindFirstNot(vDelimiter, nOffset));
    if (nSectionPos == std::string::npos)
      break;

    // Find next occurrence of the delimiter after (quoted) section
    const bool bSkipQuotedText(true);
    bool bUnmatchedQuote(false);
    size_t nNextDelimiterPos(
        FindFirst(vDelimiter, bSkipQuotedText, bUnmatchedQuote, nSectionPos));
    if (bUnmatchedQuote) {
      vwVecSplits.clear();
      return 0;
    }
    if (nNextDelimiterPos == std::string::npos)
      nNextDelimiterPos = nLen;

    // Extract string between delimiters
    const size_t nSectionLen(nNextDelimiterPos - nSectionPos);
    const std::string strSection(substr(nSectionPos, nSectionLen));
    vwVecSplits.push_back(strSection);

    // Next
    nOffset = nNextDelimiterPos + 1;
  } while (nOffset < nLen);

  return vwVecSplits.size();
}

//++
//------------------------------------------------------------------------------------
// Details: Split string into lines using \n and return an array of strings.
// Type:    Method.
// Args:    vwVecSplits - (W) Container of splits found in string data.
// Return:  size_t - Number of splits found in the string data.
// Throws:  None.
//--
size_t CMIUtilString::SplitLines(VecString_t &vwVecSplits) const {
  return Split("\n", vwVecSplits);
}

//++
//------------------------------------------------------------------------------------
// Details: Remove '\n' from the end of string if found. It does not alter
//          *this string.
// Type:    Method.
// Args:    None.
// Return:  CMIUtilString - New version of the string.
// Throws:  None.
//--
CMIUtilString CMIUtilString::StripCREndOfLine() const {
  const size_t nPos = rfind('\n');
  if (nPos == std::string::npos)
    return *this;

  const CMIUtilString strNew(substr(0, nPos));

  return strNew;
}

//++
//------------------------------------------------------------------------------------
// Details: Remove all '\n' from the string and replace with a space. It does
// not alter
//          *this string.
// Type:    Method.
// Args:    None.
// Return:  CMIUtilString - New version of the string.
// Throws:  None.
//--
CMIUtilString CMIUtilString::StripCRAll() const {
  return FindAndReplace("\n", " ");
}

//++
//------------------------------------------------------------------------------------
// Details: Find and replace all matches of a sub string with another string. It
// does not
//          alter *this string.
// Type:    Method.
// Args:    vFind         - (R) The string to look for.
//          vReplaceWith  - (R) The string to replace the vFind match.
// Return:  CMIUtilString - New version of the string.
// Throws:  None.
//--
CMIUtilString
CMIUtilString::FindAndReplace(const CMIUtilString &vFind,
                              const CMIUtilString &vReplaceWith) const {
  if (vFind.empty() || this->empty())
    return *this;

  size_t nPos = find(vFind);
  if (nPos == std::string::npos)
    return *this;

  CMIUtilString strNew(*this);
  while (nPos != std::string::npos) {
    strNew.replace(nPos, vFind.length(), vReplaceWith);
    nPos += vReplaceWith.length();
    nPos = strNew.find(vFind, nPos);
  }

  return strNew;
}

//++
//------------------------------------------------------------------------------------
// Details: Check if *this string is a decimal number.
// Type:    Method.
// Args:    None.
// Return:  bool - True = yes number, false not a number.
// Throws:  None.
//--
bool CMIUtilString::IsNumber() const {
  if (empty())
    return false;

  if ((at(0) == '-') && (length() == 1))
    return false;

  const size_t nPos = find_first_not_of("-.0123456789");
  return nPos == std::string::npos;
}

//++
//------------------------------------------------------------------------------------
// Details: Check if *this string is a hexadecimal number.
// Type:    Method.
// Args:    None.
// Return:  bool - True = yes number, false not a number.
// Throws:  None.
//--
bool CMIUtilString::IsHexadecimalNumber() const {
  // Compare '0x..' prefix
  if ((strncmp(c_str(), "0x", 2) != 0) && (strncmp(c_str(), "0X", 2) != 0))
    return false;

  // Skip '0x..' prefix
  const size_t nPos = find_first_not_of("01234567890ABCDEFabcedf", 2);
  return nPos == std::string::npos;
}

//++
//------------------------------------------------------------------------------------
// Details: Extract the number from the string. The number can be either a
// hexadecimal or
//          natural number. It cannot contain other non-numeric characters.
// Type:    Method.
// Args:    vwrNumber   - (W) Number extracted from the string.
// Return:  bool - True = yes number, false not a number.
// Throws:  None.
//--
bool CMIUtilString::ExtractNumber(MIint64 &vwrNumber) const {
  vwrNumber = 0;

  if (!IsNumber()) {
    return ExtractNumberFromHexadecimal(vwrNumber);
  }

  std::stringstream ss(const_cast<CMIUtilString &>(*this));
  ss >> vwrNumber;

  return true;
}

//++
//------------------------------------------------------------------------------------
// Details: Extract the number from the hexadecimal string..
// Type:    Method.
// Args:    vwrNumber   - (W) Number extracted from the string.
// Return:  bool - True = yes number, false not a number.
// Throws:  None.
//--
bool CMIUtilString::ExtractNumberFromHexadecimal(MIint64 &vwrNumber) const {
  vwrNumber = 0;

  const size_t nPos = find_first_not_of("xX01234567890ABCDEFabcedf");
  if (nPos != std::string::npos)
    return false;

  errno = 0;
  const MIuint64 nNum = ::strtoull(this->c_str(), nullptr, 16);
  if (errno == ERANGE)
    return false;

  vwrNumber = static_cast<MIint64>(nNum);

  return true;
}

//++
//------------------------------------------------------------------------------------
// Details: Determine if the text is all valid alpha numeric characters. Letters
// can be
//          either upper or lower case.
// Type:    Static method.
// Args:    vpText  - (R) The text data to examine.
// Return:  bool - True = yes all alpha, false = one or more chars is non alpha.
// Throws:  None.
//--
bool CMIUtilString::IsAllValidAlphaAndNumeric(const char *vpText) {
  const size_t len = ::strlen(vpText);
  if (len == 0)
    return false;

  for (size_t i = 0; i < len; i++, vpText++) {
    const char c = *vpText;
    if (::isalnum((int)c) == 0)
      return false;
  }

  return true;
}

//++
//------------------------------------------------------------------------------------
// Details: Check if two strings share equal contents.
// Type:    Method.
// Args:    vrLhs   - (R) String A.
//          vrRhs   - (R) String B.
// Return:  bool - True = yes equal, false - different.
// Throws:  None.
//--
bool CMIUtilString::Compare(const CMIUtilString &vrLhs,
                            const CMIUtilString &vrRhs) {
  // Check the sizes match
  if (vrLhs.size() != vrRhs.size())
    return false;

  return (::strncmp(vrLhs.c_str(), vrRhs.c_str(), vrLhs.size()) == 0);
}

//++
//------------------------------------------------------------------------------------
// Details: Remove from either end of *this string the following: " \t\n\v\f\r".
// Type:    Method.
// Args:    None.
// Return:  CMIUtilString - Trimmed string.
// Throws:  None.
//--
CMIUtilString CMIUtilString::Trim() const {
  CMIUtilString strNew(*this);
  const char *pWhiteSpace = " \t\n\v\f\r";
  const size_t nPos = find_last_not_of(pWhiteSpace);
  if (nPos != std::string::npos) {
    strNew = substr(0, nPos + 1);
  }
  const size_t nPos2 = strNew.find_first_not_of(pWhiteSpace);
  if (nPos2 != std::string::npos) {
    strNew = strNew.substr(nPos2);
  }

  return strNew;
}

//++
//------------------------------------------------------------------------------------
// Details: Remove from either end of *this string the specified character.
// Type:    Method.
// Args:    None.
// Return:  CMIUtilString - Trimmed string.
// Throws:  None.
//--
CMIUtilString CMIUtilString::Trim(const char vChar) const {
  CMIUtilString strNew(*this);
  const size_t nLen = strNew.length();
  if (nLen > 1) {
    if ((strNew[0] == vChar) && (strNew[nLen - 1] == vChar))
      strNew = strNew.substr(1, nLen - 2);
  }

  return strNew;
}

//++
//------------------------------------------------------------------------------------
// Details: Do a printf equivalent for printing a number in binary i.e. "b%llB".
// Type:    Static method.
// Args:    vnDecimal   - (R) The number to represent in binary.
// Return:  CMIUtilString - Binary number in text.
// Throws:  None.
//--
CMIUtilString CMIUtilString::FormatBinary(const MIuint64 vnDecimal) {
  CMIUtilString strBinaryNumber;

  const MIuint nConstBits = 64;
  MIuint nRem[nConstBits + 1];
  MIint i = 0;
  MIuint nLen = 0;
  MIuint64 nNum = vnDecimal;
  while ((nNum > 0) && (nLen < nConstBits)) {
    nRem[i++] = nNum % 2;
    nNum = nNum >> 1;
    nLen++;
  }
  char pN[nConstBits + 1];
  MIuint j = 0;
  for (i = nLen; i > 0; --i, j++) {
    pN[j] = '0' + nRem[i - 1];
  }
  pN[j] = 0; // String NUL termination

  strBinaryNumber = CMIUtilString::Format("0b%s", &pN[0]);

  return strBinaryNumber;
}

//++
//------------------------------------------------------------------------------------
// Details: Remove from a string doubled up characters so only one set left.
// Characters
//          are only removed if the previous character is already a same
//          character.
// Type:    Method.
// Args:    vChar   - (R) The character to search for and remove adjacent
// duplicates.
// Return:  CMIUtilString - New version of the string.
// Throws:  None.
//--
CMIUtilString CMIUtilString::RemoveRepeatedCharacters(const char vChar) {
  return RemoveRepeatedCharacters(0, vChar);
}

//++
//------------------------------------------------------------------------------------
// Details: Recursively remove from a string doubled up characters so only one
// set left.
//          Characters are only removed if the previous character is already a
//          same
//          character.
// Type:    Method.
// Args:    vChar   - (R) The character to search for and remove adjacent
// duplicates.
//          vnPos   - Character position in the string.
// Return:  CMIUtilString - New version of the string.
// Throws:  None.
//--
CMIUtilString CMIUtilString::RemoveRepeatedCharacters(size_t vnPos,
                                                      const char vChar) {
  const char cQuote = '"';

  // Look for first quote of two
  const size_t nPos = find(cQuote, vnPos);
  if (nPos == std::string::npos)
    return *this;

  const size_t nPosNext = nPos + 1;
  if (nPosNext > length())
    return *this;

  if (at(nPosNext) == cQuote) {
    *this = substr(0, nPos) + substr(nPosNext, length());
    RemoveRepeatedCharacters(nPosNext, vChar);
  }

  return *this;
}

//++
//------------------------------------------------------------------------------------
// Details: Is the text in *this string surrounded by quotes.
// Type:    Method.
// Args:    None.
// Return:  bool - True = Yes string is quoted, false = no quoted.
// Throws:  None.
//--
bool CMIUtilString::IsQuoted() const {
  const char cQuote = '"';

  if (at(0) != cQuote)
    return false;

  const size_t nLen = length();
  return !((nLen > 0) && (at(nLen - 1) != cQuote));
}

//++
//------------------------------------------------------------------------------------
// Details: Find first occurrence in *this string which matches the pattern.
// Type:    Method.
// Args:    vrPattern   - (R) The pattern to search for.
//          vnPos       - The starting position at which to start searching.
//          (Dflt = 0)
// Return:  size_t - The position of the first substring that match.
// Throws:  None.
//--
size_t CMIUtilString::FindFirst(const CMIUtilString &vrPattern,
                                size_t vnPos /* = 0 */) const {
  return find(vrPattern, vnPos);
}

//++
//------------------------------------------------------------------------------------
// Details: Find first occurrence in *this string which matches the pattern and
// isn't surrounded by quotes.
// Type:    Method.
// Args:    vrPattern                 - (R) The pattern to search for.
//          vbSkipQuotedText          - (R) True = don't look at quoted text,
//          false = otherwise.
//          vrwbNotFoundClosedQuote   - (W) True = parsing error: unmatched
//          quote, false = otherwise.
//          vnPos                     - Position of the first character in the
//          string to be considered in the search. (Dflt = 0)
// Return:  size_t - The position of the first substring that matches and isn't
// quoted.
// Throws:  None.
//--
size_t CMIUtilString::FindFirst(const CMIUtilString &vrPattern,
                                const bool vbSkipQuotedText,
                                bool &vrwbNotFoundClosedQuote,
                                size_t vnPos /* = 0 */) const {
  vrwbNotFoundClosedQuote = false;

  if (!vbSkipQuotedText)
    return FindFirst(vrPattern, vnPos);

  const size_t nLen(length());

  size_t nPos = vnPos;
  do {
    const size_t nQuotePos(FindFirstQuote(nPos));
    const size_t nPatternPos(FindFirst(vrPattern, nPos));
    if (nQuotePos == std::string::npos)
      return nPatternPos;

    const size_t nQuoteClosedPos = FindFirstQuote(nQuotePos + 1);
    if (nQuoteClosedPos == std::string::npos) {
      vrwbNotFoundClosedQuote = true;
      return std::string::npos;
    }

    if ((nPatternPos == std::string::npos) || (nPatternPos < nQuotePos))
      return nPatternPos;

    nPos = nQuoteClosedPos + 1;
  } while (nPos < nLen);

  return std::string::npos;
}

//++
//------------------------------------------------------------------------------------
// Details: Find first occurrence in *this string which doesn't match the
// pattern.
// Type:    Method.
// Args:    vrPattern   - (R) The pattern to search for.
//          vnPos       - Position of the first character in the string to be
//          considered in the search. (Dflt = 0)
// Return:  size_t - The position of the first character that doesn't match.
// Throws:  None.
//--
size_t CMIUtilString::FindFirstNot(const CMIUtilString &vrPattern,
                                   size_t vnPos /* = 0 */) const {
  const size_t nLen(length());
  const size_t nPatternLen(vrPattern.length());

  size_t nPatternPos(vnPos);
  do {
    const bool bMatchPattern(compare(nPatternPos, nPatternLen, vrPattern) == 0);
    if (!bMatchPattern)
      return nPatternPos;
    nPatternPos += nPatternLen;
  } while (nPatternPos < nLen);

  return std::string::npos;
}

//++
//------------------------------------------------------------------------------------
// Details: Find first occurrence of not escaped quotation mark in *this string.
// Type:    Method.
// Args:    vnPos   - Position of the first character in the string to be
// considered in the search.
// Return:  size_t - The position of the quotation mark.
// Throws:  None.
//--
size_t CMIUtilString::FindFirstQuote(size_t vnPos) const {
  const char cBckSlash('\\');
  const char cQuote('"');
  const size_t nLen(length());

  size_t nPos = vnPos;
  do {
    const size_t nBckSlashPos(find(cBckSlash, nPos));
    const size_t nQuotePos(find(cQuote, nPos));
    if ((nBckSlashPos == std::string::npos) || (nQuotePos == std::string::npos))
      return nQuotePos;

    if (nQuotePos < nBckSlashPos)
      return nQuotePos;

    // Skip 2 characters: First is '\', second is that which is escaped by '\'
    nPos = nBckSlashPos + 2;
  } while (nPos < nLen);

  return std::string::npos;
}

//++
//------------------------------------------------------------------------------------
// Details: Get escaped string from *this string.
// Type:    Method.
// Args:    None.
// Return:  CMIUtilString - The escaped version of the initial string.
// Throws:  None.
//--
CMIUtilString CMIUtilString::Escape(bool vbEscapeQuotes /* = false */) const {
  const size_t nLen(length());
  CMIUtilString strNew;
  strNew.reserve(nLen);
  for (size_t nIndex(0); nIndex < nLen; ++nIndex) {
    const char cUnescapedChar((*this)[nIndex]);
    if (cUnescapedChar == '"' && vbEscapeQuotes)
      strNew.append("\\\"");
    else
      strNew.append(ConvertToPrintableASCII((char)cUnescapedChar));
  }
  return strNew;
}

//++
//------------------------------------------------------------------------------------
// Details: Get string with backslashes in front of double quote '"' and
// backslash '\\'
//          characters.
// Type:    Method.
// Args:    None.
// Return:  CMIUtilString - The wrapped version of the initial string.
// Throws:  None.
//--
CMIUtilString CMIUtilString::AddSlashes() const {
  const char cBckSlash('\\');
  const size_t nLen(length());
  CMIUtilString strNew;
  strNew.reserve(nLen);

  size_t nOffset(0);
  while (nOffset < nLen) {
    const size_t nUnescapedCharPos(find_first_of("\"\\", nOffset));
    const bool bUnescapedCharNotFound(nUnescapedCharPos == std::string::npos);
    if (bUnescapedCharNotFound) {
      const size_t nAppendAll(std::string::npos);
      strNew.append(*this, nOffset, nAppendAll);
      break;
    }
    const size_t nAppendLen(nUnescapedCharPos - nOffset);
    strNew.append(*this, nOffset, nAppendLen);
    strNew.push_back(cBckSlash);
    const char cUnescapedChar((*this)[nUnescapedCharPos]);
    strNew.push_back(cUnescapedChar);
    nOffset = nUnescapedCharPos + 1;
  }

  return strNew;
}

//++
//------------------------------------------------------------------------------------
// Details: Remove backslashes added by CMIUtilString::AddSlashes.
// Type:    Method.
// Args:    None.
// Return:  CMIUtilString - The initial version of wrapped string.
// Throws:  None.
//--
CMIUtilString CMIUtilString::StripSlashes() const {
  const char cBckSlash('\\');
  const size_t nLen(length());
  CMIUtilString strNew;
  strNew.reserve(nLen);

  size_t nOffset(0);
  while (nOffset < nLen) {
    const size_t nBckSlashPos(find(cBckSlash, nOffset));
    const bool bBckSlashNotFound(nBckSlashPos == std::string::npos);
    if (bBckSlashNotFound) {
      const size_t nAppendAll(std::string::npos);
      strNew.append(*this, nOffset, nAppendAll);
      break;
    }
    const size_t nAppendLen(nBckSlashPos - nOffset);
    strNew.append(*this, nOffset, nAppendLen);
    const bool bBckSlashIsLast(nBckSlashPos == nLen);
    if (bBckSlashIsLast) {
      strNew.push_back(cBckSlash);
      break;
    }
    const char cEscapedChar((*this)[nBckSlashPos + 1]);
    const size_t nEscapedCharPos(std::string("\"\\").find(cEscapedChar));
    const bool bEscapedCharNotFound(nEscapedCharPos == std::string::npos);
    if (bEscapedCharNotFound)
      strNew.push_back(cBckSlash);
    strNew.push_back(cEscapedChar);
    nOffset = nBckSlashPos + 2;
  }

  return strNew;
}

CMIUtilString CMIUtilString::ConvertToPrintableASCII(const char vChar,
                                                     bool bEscapeQuotes) {
  switch (vChar) {
  case '\a':
    return "\\a";
  case '\b':
    return "\\b";
  case '\t':
    return "\\t";
  case '\n':
    return "\\n";
  case '\v':
    return "\\v";
  case '\f':
    return "\\f";
  case '\r':
    return "\\r";
  case '\033':
    return "\\e";
  case '\\':
    return "\\\\";
  case '"':
    if (bEscapeQuotes)
      return "\\\"";
    LLVM_FALLTHROUGH;
  default:
    if (::isprint(vChar))
      return Format("%c", vChar);
    else
      return Format("\\x%02" PRIx8, vChar);
  }
}

CMIUtilString
CMIUtilString::ConvertCharValueToPrintableASCII(char vChar,
                                                bool bEscapeQuotes) {
  switch (vChar) {
  case '\a':
    return "\\a";
  case '\b':
    return "\\b";
  case '\t':
    return "\\t";
  case '\n':
    return "\\n";
  case '\v':
    return "\\v";
  case '\f':
    return "\\f";
  case '\r':
    return "\\r";
  case '\033':
    return "\\e";
  case '\\':
    return "\\\\";
  case '"':
    if (bEscapeQuotes)
      return "\\\"";
    LLVM_FALLTHROUGH;
  default:
    if (::isprint(vChar))
      return Format("%c", vChar);
    else
      return CMIUtilString();
  }
}

CMIUtilString CMIUtilString::ConvertToPrintableASCII(const char16_t vChar16,
                                                     bool bEscapeQuotes) {
  if (vChar16 == (char16_t)(char)vChar16) {
    // Convert char16_t to char (if possible)
    CMIUtilString str =
        ConvertCharValueToPrintableASCII((char)vChar16, bEscapeQuotes);
    if (str.length() > 0)
      return str;
  }
  return Format("\\u%02" PRIx8 "%02" PRIx8, (vChar16 >> 8) & 0xff,
                vChar16 & 0xff);
}

CMIUtilString CMIUtilString::ConvertToPrintableASCII(const char32_t vChar32,
                                                     bool bEscapeQuotes) {
  if (vChar32 == (char32_t)(char)vChar32) {
    // Convert char32_t to char (if possible)
    CMIUtilString str =
        ConvertCharValueToPrintableASCII((char)vChar32, bEscapeQuotes);
    if (str.length() > 0)
      return str;
  }
  return Format("\\U%02" PRIx8 "%02" PRIx8 "%02" PRIx8 "%02" PRIx8,
                (vChar32 >> 24) & 0xff, (vChar32 >> 16) & 0xff,
                (vChar32 >> 8) & 0xff, vChar32 & 0xff);
}
