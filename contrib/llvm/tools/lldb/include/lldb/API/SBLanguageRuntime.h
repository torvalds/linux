//===-- SBLanguageRuntime.h -------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_SBLanguageRuntime_h_
#define LLDB_SBLanguageRuntime_h_

#include "lldb/API/SBDefines.h"

namespace lldb {

class SBLanguageRuntime {
public:
  static lldb::LanguageType GetLanguageTypeFromString(const char *string);

  static const char *GetNameForLanguageType(lldb::LanguageType language);
};

} // namespace lldb

#endif // LLDB_SBLanguageRuntime_h_
