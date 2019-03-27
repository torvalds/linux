//===--- SanitizerBlacklist.cpp - Blacklist for sanitizers ----------------===//
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
#include "clang/Basic/SanitizerBlacklist.h"

using namespace clang;

SanitizerBlacklist::SanitizerBlacklist(
    const std::vector<std::string> &BlacklistPaths, SourceManager &SM)
    : SSCL(SanitizerSpecialCaseList::createOrDie(BlacklistPaths)), SM(SM) {}

bool SanitizerBlacklist::isBlacklistedGlobal(SanitizerMask Mask,
                                             StringRef GlobalName,
                                             StringRef Category) const {
  return SSCL->inSection(Mask, "global", GlobalName, Category);
}

bool SanitizerBlacklist::isBlacklistedType(SanitizerMask Mask,
                                           StringRef MangledTypeName,
                                           StringRef Category) const {
  return SSCL->inSection(Mask, "type", MangledTypeName, Category);
}

bool SanitizerBlacklist::isBlacklistedFunction(SanitizerMask Mask,
                                               StringRef FunctionName) const {
  return SSCL->inSection(Mask, "fun", FunctionName);
}

bool SanitizerBlacklist::isBlacklistedFile(SanitizerMask Mask,
                                           StringRef FileName,
                                           StringRef Category) const {
  return SSCL->inSection(Mask, "src", FileName, Category);
}

bool SanitizerBlacklist::isBlacklistedLocation(SanitizerMask Mask,
                                               SourceLocation Loc,
                                               StringRef Category) const {
  return Loc.isValid() &&
         isBlacklistedFile(Mask, SM.getFilename(SM.getFileLoc(Loc)), Category);
}

