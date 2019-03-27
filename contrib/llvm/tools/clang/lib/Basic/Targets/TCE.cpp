//===--- TCE.cpp - Implement TCE target feature support -------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file implements TCE TargetInfo objects.
//
//===----------------------------------------------------------------------===//

#include "TCE.h"
#include "Targets.h"
#include "clang/Basic/MacroBuilder.h"

using namespace clang;
using namespace clang::targets;

void TCETargetInfo::getTargetDefines(const LangOptions &Opts,
                                     MacroBuilder &Builder) const {
  DefineStd(Builder, "tce", Opts);
  Builder.defineMacro("__TCE__");
  Builder.defineMacro("__TCE_V1__");
}

void TCELETargetInfo::getTargetDefines(const LangOptions &Opts,
                                       MacroBuilder &Builder) const {
  DefineStd(Builder, "tcele", Opts);
  Builder.defineMacro("__TCE__");
  Builder.defineMacro("__TCE_V1__");
  Builder.defineMacro("__TCELE__");
  Builder.defineMacro("__TCELE_V1__");
}
