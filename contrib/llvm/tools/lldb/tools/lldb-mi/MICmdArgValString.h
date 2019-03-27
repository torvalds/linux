//===-- MICmdArgValString.h -------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#pragma once

// In-house headers:
#include "MICmdArgValBase.h"

// Declarations:
class CMICmdArgContext;

//++
//============================================================================
// Details: MI common code class. Command argument class. Arguments object
//          needing specialization derived from the CMICmdArgValBase class.
//          An argument knows what type of argument it is and how it is to
//          interpret the options (context) string to find and validate a
//          matching
//          argument and so extract a value from it .
//          Based on the Interpreter pattern.
//--
class CMICmdArgValString : public CMICmdArgValBaseTemplate<CMIUtilString> {
  // Methods:
public:
  /* ctor */ CMICmdArgValString();
  /* ctor */ CMICmdArgValString(const bool vbAnything);
  /* ctor */ CMICmdArgValString(const bool vbHandleQuotes,
                                const bool vbAcceptNumbers,
                                const bool vbHandleDirPaths);
  /* ctor */ CMICmdArgValString(const CMIUtilString &vrArgName,
                                const bool vbMandatory,
                                const bool vbHandleByCmd,
                                const bool vbHandleQuotes = false,
                                const bool vbAcceptNumbers = false);
  /* ctor */ CMICmdArgValString(const CMIUtilString &vrArgName,
                                const bool vbMandatory,
                                const bool vbHandleByCmd,
                                const bool vbHandleQuotes,
                                const bool vbAcceptNumbers,
                                const bool vbHandleDirPaths);
  //
  bool IsStringArg(const CMIUtilString &vrTxt) const;

  // Overridden:
public:
  // From CMICmdArgValBase
  /* dtor */ ~CMICmdArgValString() override;
  // From CMICmdArgSet::IArg
  bool Validate(CMICmdArgContext &vrwArgContext) override;

  // Methods:
private:
  bool ValidateSingleText(CMICmdArgContext &vrwArgContext);
  bool ValidateQuotedText(CMICmdArgContext &vrwArgContext);
  bool ValidateQuotedTextEmbedded(CMICmdArgContext &vrwArgContext);
  bool ValidateQuotedQuotedTextEmbedded(CMICmdArgContext &vrwArgContext);
  bool IsStringArgSingleText(const CMIUtilString &vrTxt) const;
  bool IsStringArgQuotedText(const CMIUtilString &vrTxt) const;
  bool IsStringArgQuotedTextEmbedded(const CMIUtilString &vrTxt) const;
  bool IsStringArgQuotedQuotedTextEmbedded(const CMIUtilString &vrTxt) const;

  // Attribute:
private:
  bool m_bHandleQuotedString; // True = Parse a string surrounded by quotes
                              // spaces are not delimiters, false = only text up
                              // to next
                              // delimiting space character
  bool m_bAcceptNumbers;      // True = Parse a string and accept as a number if
                         // number, false = numbers not recognised as string
                         // types
  bool m_bHandleDirPaths; // True = Parse a string and accept directory file
                          // style string if present, false = directory file
                          // path not
                          // accepted
  bool m_bHandleAnything; // True = Parse a string and accept anything if
                          // present, false = validate for criteria matches
};
