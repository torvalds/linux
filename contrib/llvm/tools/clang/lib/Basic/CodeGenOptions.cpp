//===--- CodeGenOptions.cpp -----------------------------------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "clang/Basic/CodeGenOptions.h"
#include <string.h>

namespace clang {

CodeGenOptions::CodeGenOptions() {
#define CODEGENOPT(Name, Bits, Default) Name = Default;
#define ENUM_CODEGENOPT(Name, Type, Bits, Default) set##Name(Default);
#include "clang/Basic/CodeGenOptions.def"

  RelocationModel = llvm::Reloc::PIC_;
  memcpy(CoverageVersion, "402*", 4);
}

bool CodeGenOptions::isNoBuiltinFunc(const char *Name) const {
  StringRef FuncName(Name);
  for (unsigned i = 0, e = NoBuiltinFuncs.size(); i != e; ++i)
    if (FuncName.equals(NoBuiltinFuncs[i]))
      return true;
  return false;
}

}  // end namespace clang
