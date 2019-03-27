//===-- SBLanguageRuntime.cpp -----------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "lldb/API/SBLanguageRuntime.h"
#include "lldb/Target/Language.h"

using namespace lldb;
using namespace lldb_private;

lldb::LanguageType
SBLanguageRuntime::GetLanguageTypeFromString(const char *string) {
  return Language::GetLanguageTypeFromString(
      llvm::StringRef::withNullAsEmpty(string));
}

const char *
SBLanguageRuntime::GetNameForLanguageType(lldb::LanguageType language) {
  return Language::GetNameForLanguageType(language);
}
