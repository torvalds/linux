//===-- MICmdArgValFile.h ---------------------------------------*- C++ -*-===//
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
class CMICmdArgValFile : public CMICmdArgValBaseTemplate<CMIUtilString> {
  // Methods:
public:
  /* ctor */ CMICmdArgValFile();
  /* ctor */ CMICmdArgValFile(const CMIUtilString &vrArgName,
                              const bool vbMandatory, const bool vbHandleByCmd);
  //
  bool IsFilePath(const CMIUtilString &vrFileNamePath) const;
  CMIUtilString GetFileNamePath(const CMIUtilString &vrTxt) const;

  // Overridden:
public:
  // From CMICmdArgValBase
  /* dtor */ ~CMICmdArgValFile() override;
  // From CMICmdArgSet::IArg
  bool Validate(CMICmdArgContext &vwArgContext) override;

  // Methods:
private:
  bool IsValidChars(const CMIUtilString &vrText) const;
};
