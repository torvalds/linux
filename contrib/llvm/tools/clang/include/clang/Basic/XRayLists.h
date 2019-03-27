//===--- XRayLists.h - XRay automatic attribution ---------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// User-provided filters for always/never XRay instrumenting certain functions.
//
//===----------------------------------------------------------------------===//
#ifndef LLVM_CLANG_BASIC_XRAYLISTS_H
#define LLVM_CLANG_BASIC_XRAYLISTS_H

#include "clang/Basic/LLVM.h"
#include "clang/Basic/SourceLocation.h"
#include "clang/Basic/SourceManager.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/SpecialCaseList.h"
#include <memory>

namespace clang {

class XRayFunctionFilter {
  std::unique_ptr<llvm::SpecialCaseList> AlwaysInstrument;
  std::unique_ptr<llvm::SpecialCaseList> NeverInstrument;
  std::unique_ptr<llvm::SpecialCaseList> AttrList;
  SourceManager &SM;

public:
  XRayFunctionFilter(ArrayRef<std::string> AlwaysInstrumentPaths,
                     ArrayRef<std::string> NeverInstrumentPaths,
                     ArrayRef<std::string> AttrListPaths, SourceManager &SM);

  enum class ImbueAttribute {
    NONE,
    ALWAYS,
    NEVER,
    ALWAYS_ARG1,
  };

  ImbueAttribute shouldImbueFunction(StringRef FunctionName) const;

  ImbueAttribute
  shouldImbueFunctionsInFile(StringRef Filename,
                             StringRef Category = StringRef()) const;

  ImbueAttribute shouldImbueLocation(SourceLocation Loc,
                                     StringRef Category = StringRef()) const;
};

} // namespace clang

#endif
