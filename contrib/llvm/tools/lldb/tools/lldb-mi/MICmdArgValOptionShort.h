//===-- MICmdArgValOptionShort.h --------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#pragma once

// In-house headers:
#include "MICmdArgValOptionLong.h"

// Declarations:
class CMICmdArgContext;
class CMIUtilString;

//++
//============================================================================
// Details: MI common code class. Command argument class. Arguments object
//          needing specialization derived from the CMICmdArgValOptionLong
//          class.
//          An argument knows what type of argument it is and how it is to
//          interpret the options (context) string to find and validate a
//          matching
//          argument and so extract a value from it.
//          If *this argument has expected options following it the option
//          objects
//          created to hold each of those option's values belong to *this
//          argument
//          object and so are deleted when *this object goes out of scope.
//          Based on the Interpreter pattern.
//--
class CMICmdArgValOptionShort : public CMICmdArgValOptionLong {
  // Methods:
public:
  /* ctor */ CMICmdArgValOptionShort();
  /* ctor */ CMICmdArgValOptionShort(const CMIUtilString &vrArgName,
                                     const bool vbMandatory,
                                     const bool vbHandleByCmd);
  /* ctor */ CMICmdArgValOptionShort(const CMIUtilString &vrArgName,
                                     const bool vbMandatory,
                                     const bool vbHandleByCmd,
                                     const ArgValType_e veType,
                                     const MIuint vnExpectingNOptions);
  //
  bool IsArgShortOption(const CMIUtilString &vrTxt) const;

  // Overridden:
public:
  // From CMICmdArgValBase
  /* dtor */ ~CMICmdArgValOptionShort() override;

  // Overridden:
private:
  // From CMICmdArgValOptionLong
  bool IsArgOptionCorrect(const CMIUtilString &vrTxt) const override;
  bool ArgNameMatch(const CMIUtilString &vrTxt) const override;
};
