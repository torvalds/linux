//===-- MIUtilDateTimeStd.h -------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#pragma once

// Third party headers
#include <ctime>

// In-house headers:
#include "MIUtilString.h"

//++
//============================================================================
// Details: MI common code utility class. Used to retrieve system local date
//          time.
//--
class CMIUtilDateTimeStd {
  // Methods:
public:
  /* ctor */ CMIUtilDateTimeStd();

  CMIUtilString GetDate();
  CMIUtilString GetTime();
  CMIUtilString GetDateTimeLogFilename();

  // Overrideable:
public:
  // From CMICmnBase
  /* dtor */ virtual ~CMIUtilDateTimeStd();

  // Attributes:
private:
  std::time_t m_rawTime;
  char m_pScratch[16];
};
