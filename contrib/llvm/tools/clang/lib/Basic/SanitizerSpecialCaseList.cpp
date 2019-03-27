//===--- SanitizerSpecialCaseList.cpp - SCL for sanitizers ----------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// An extension of SpecialCaseList to allowing querying sections by
// SanitizerMask.
//
//===----------------------------------------------------------------------===//
#include "clang/Basic/SanitizerSpecialCaseList.h"

using namespace clang;

std::unique_ptr<SanitizerSpecialCaseList>
SanitizerSpecialCaseList::create(const std::vector<std::string> &Paths,
                                 std::string &Error) {
  std::unique_ptr<clang::SanitizerSpecialCaseList> SSCL(
      new SanitizerSpecialCaseList());
  if (SSCL->createInternal(Paths, Error)) {
    SSCL->createSanitizerSections();
    return SSCL;
  }
  return nullptr;
}

std::unique_ptr<SanitizerSpecialCaseList>
SanitizerSpecialCaseList::createOrDie(const std::vector<std::string> &Paths) {
  std::string Error;
  if (auto SSCL = create(Paths, Error))
    return SSCL;
  llvm::report_fatal_error(Error);
}

void SanitizerSpecialCaseList::createSanitizerSections() {
  for (auto &S : Sections) {
    SanitizerMask Mask = 0;

#define SANITIZER(NAME, ID)                                                    \
  if (S.SectionMatcher->match(NAME))                                           \
    Mask |= SanitizerKind::ID;
#define SANITIZER_GROUP(NAME, ID, ALIAS) SANITIZER(NAME, ID)

#include "clang/Basic/Sanitizers.def"
#undef SANITIZER
#undef SANITIZER_GROUP

    SanitizerSections.emplace_back(Mask, S.Entries);
  }
}

bool SanitizerSpecialCaseList::inSection(SanitizerMask Mask, StringRef Prefix,
                                         StringRef Query,
                                         StringRef Category) const {
  for (auto &S : SanitizerSections)
    if ((S.Mask & Mask) &&
        SpecialCaseList::inSectionBlame(S.Entries, Prefix, Query, Category))
      return true;

  return false;
}
