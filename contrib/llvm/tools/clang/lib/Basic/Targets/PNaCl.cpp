//===--- PNaCl.cpp - Implement PNaCl target feature support ---------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file implements PNaCl TargetInfo objects.
//
//===----------------------------------------------------------------------===//

#include "PNaCl.h"
#include "clang/Basic/MacroBuilder.h"

using namespace clang;
using namespace clang::targets;

ArrayRef<const char *> PNaClTargetInfo::getGCCRegNames() const { return None; }

ArrayRef<TargetInfo::GCCRegAlias> PNaClTargetInfo::getGCCRegAliases() const {
  return None;
}

void PNaClTargetInfo::getArchDefines(const LangOptions &Opts,
                                     MacroBuilder &Builder) const {
  Builder.defineMacro("__le32__");
  Builder.defineMacro("__pnacl__");
}
