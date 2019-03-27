//===-- MICmdArgValPrintValues.h --------------------------------*- C++ -*-===//
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
//          argument and so extract a value from it. The print-values looks
//          like:
//            0 or --no-values
//            1 or --all-values
//            2 or --simple-values
//          Based on the Interpreter pattern.
//--
class CMICmdArgValPrintValues : public CMICmdArgValBaseTemplate<MIuint> {
  // Methods:
public:
  /* ctor */ CMICmdArgValPrintValues();
  /* ctor */ CMICmdArgValPrintValues(const CMIUtilString &vrArgName,
                                     const bool vbMandatory,
                                     const bool vbHandleByCmd);
  //
  bool IsArgPrintValues(const CMIUtilString &vrTxt) const;

  // Overridden:
public:
  // From CMICmdArgValBase
  /* dtor */ ~CMICmdArgValPrintValues() override;
  // From CMICmdArgSet::IArg
  bool Validate(CMICmdArgContext &vArgContext) override;

  // Methods:
private:
  bool ExtractPrintValues(const CMIUtilString &vrTxt);
  MIuint GetPrintValues() const;

  // Attributes:
private:
  MIuint m_nPrintValues;
};
