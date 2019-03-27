//===-- MICmdArgValConsume.h ------------------------------------*- C++ -*-===//
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
//          argument. This type having recognised its argument name just
//          consumes
//          that argument or option (ignores it). This is the so the validation
//          process can then ask if all arguments or options have been
//          recognised
//          other an error will occurred "argument not recognised". For example
//          this can be used to consume the "--" text which is not an argument
//          in
//          itself. Normally the GetValue() function (in base class) would
//          return
//          a value for the argument but is not the case for *this argument type
//          object.
//          Based on the Interpreter pattern.
//--
class CMICmdArgValConsume : public CMICmdArgValBaseTemplate<CMIUtilString> {
  // Methods:
public:
  /* ctor */ CMICmdArgValConsume();
  /* ctor */ CMICmdArgValConsume(const CMIUtilString &vrArgName,
                                 const bool vbMandatory);
  //
  bool IsOk() const;

  // Overridden:
public:
  // From CMICmdArgValBase
  /* dtor */ ~CMICmdArgValConsume() override;
  // From CMICmdArgSet::IArg
  bool Validate(CMICmdArgContext &vwArgContext) override;
};
