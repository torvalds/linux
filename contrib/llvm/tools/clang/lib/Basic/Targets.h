//===------- Targets.h - Declare target feature support ---------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file declares things required for construction of a TargetInfo object
// from a target triple. Typically individual targets will need to include from
// here in order to get these functions if required.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_LIB_BASIC_TARGETS_H
#define LLVM_CLANG_LIB_BASIC_TARGETS_H

#include "clang/Basic/LangOptions.h"
#include "clang/Basic/MacroBuilder.h"
#include "clang/Basic/TargetInfo.h"
#include "llvm/ADT/StringRef.h"

namespace clang {
namespace targets {

LLVM_LIBRARY_VISIBILITY
clang::TargetInfo *AllocateTarget(const llvm::Triple &Triple,
                                  const clang::TargetOptions &Opts);

/// DefineStd - Define a macro name and standard variants.  For example if
/// MacroName is "unix", then this will define "__unix", "__unix__", and "unix"
/// when in GNU mode.
LLVM_LIBRARY_VISIBILITY
void DefineStd(clang::MacroBuilder &Builder, llvm::StringRef MacroName,
               const clang::LangOptions &Opts);

LLVM_LIBRARY_VISIBILITY
void defineCPUMacros(clang::MacroBuilder &Builder, llvm::StringRef CPUName,
                     bool Tuning = true);

LLVM_LIBRARY_VISIBILITY
void addMinGWDefines(const llvm::Triple &Triple, const clang::LangOptions &Opts,
                     clang::MacroBuilder &Builder);

LLVM_LIBRARY_VISIBILITY
void addCygMingDefines(const clang::LangOptions &Opts,
                       clang::MacroBuilder &Builder);
} // namespace targets
} // namespace clang
#endif // LLVM_CLANG_LIB_BASIC_TARGETS_H
