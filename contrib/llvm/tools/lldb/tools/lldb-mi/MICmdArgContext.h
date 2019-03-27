//===-- MICmdArgContext.h ---------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#pragma once

// In-house headers:
#include "MIUtilString.h"

//++
//============================================================================
// Details: MI common code class. Command arguments and options string. Holds
//          the context string.
//          Based on the Interpreter pattern.
//--
class CMICmdArgContext {
  // Methods:
public:
  /* ctor */ CMICmdArgContext();
  /* ctor */ CMICmdArgContext(const CMIUtilString &vrCmdLineArgsRaw);
  //
  const CMIUtilString &GetArgsLeftToParse() const;
  size_t GetNumberArgsPresent() const;
  CMIUtilString::VecString_t GetArgs() const;
  bool IsEmpty() const;
  bool RemoveArg(const CMIUtilString &vArg);
  bool RemoveArgAtPos(const CMIUtilString &vArg, size_t nArgIndex);
  //
  CMICmdArgContext &operator=(const CMICmdArgContext &vOther);

  // Overridden:
public:
  // From CMIUtilString
  /* dtor */ virtual ~CMICmdArgContext();

  // Attributes:
private:
  CMIUtilString m_strCmdArgsAndOptions;
};
