//===-- MICmnLLDBProxySBValue.h ---------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#pragma once

// Third Party Headers:
#include "lldb/API/SBValue.h"

// In-house headers:
#include "MIDataTypes.h"

// Declarations:
class CMIUtilString;

//++
//============================================================================
// Details: MI proxy wrapper class to lldb::SBValue. The class provides
// functionality
//          to assist in the use of SBValue's particular function usage.
//--
class CMICmnLLDBProxySBValue {
  // Statics:
public:
  static bool GetValueAsSigned(const lldb::SBValue &vrValue, MIint64 &vwValue);
  static bool GetValueAsUnsigned(const lldb::SBValue &vrValue,
                                 MIuint64 &vwValue);
  static bool GetCString(const lldb::SBValue &vrValue,
                         CMIUtilString &vwCString);
};
