//===--- SanitizerBlacklist.h - Blacklist for sanitizers --------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// User-provided blacklist used to disable/alter instrumentation done in
// sanitizers.
//
//===----------------------------------------------------------------------===//
#ifndef LLVM_CLANG_BASIC_SANITIZERBLACKLIST_H
#define LLVM_CLANG_BASIC_SANITIZERBLACKLIST_H

#include "clang/Basic/LLVM.h"
#include "clang/Basic/SanitizerSpecialCaseList.h"
#include "clang/Basic/Sanitizers.h"
#include "clang/Basic/SourceLocation.h"
#include "clang/Basic/SourceManager.h"
#include "llvm/ADT/StringRef.h"
#include <memory>

namespace clang {

class SanitizerBlacklist {
  std::unique_ptr<SanitizerSpecialCaseList> SSCL;
  SourceManager &SM;

public:
  SanitizerBlacklist(const std::vector<std::string> &BlacklistPaths,
                     SourceManager &SM);
  bool isBlacklistedGlobal(SanitizerMask Mask, StringRef GlobalName,
                           StringRef Category = StringRef()) const;
  bool isBlacklistedType(SanitizerMask Mask, StringRef MangledTypeName,
                         StringRef Category = StringRef()) const;
  bool isBlacklistedFunction(SanitizerMask Mask, StringRef FunctionName) const;
  bool isBlacklistedFile(SanitizerMask Mask, StringRef FileName,
                         StringRef Category = StringRef()) const;
  bool isBlacklistedLocation(SanitizerMask Mask, SourceLocation Loc,
                             StringRef Category = StringRef()) const;
};

}  // end namespace clang

#endif
