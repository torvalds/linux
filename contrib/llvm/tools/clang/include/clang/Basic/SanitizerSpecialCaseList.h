//===--- SanitizerSpecialCaseList.h - SCL for sanitizers --------*- C++ -*-===//
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
#ifndef LLVM_CLANG_BASIC_SANITIZERSPECIALCASELIST_H
#define LLVM_CLANG_BASIC_SANITIZERSPECIALCASELIST_H

#include "clang/Basic/LLVM.h"
#include "clang/Basic/Sanitizers.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/SpecialCaseList.h"
#include <memory>

namespace clang {

class SanitizerSpecialCaseList : public llvm::SpecialCaseList {
public:
  static std::unique_ptr<SanitizerSpecialCaseList>
  create(const std::vector<std::string> &Paths, std::string &Error);

  static std::unique_ptr<SanitizerSpecialCaseList>
  createOrDie(const std::vector<std::string> &Paths);

  // Query blacklisted entries if any bit in Mask matches the entry's section.
  bool inSection(SanitizerMask Mask, StringRef Prefix, StringRef Query,
                 StringRef Category = StringRef()) const;

protected:
  // Initialize SanitizerSections.
  void createSanitizerSections();

  struct SanitizerSection {
    SanitizerSection(SanitizerMask SM, SectionEntries &E)
        : Mask(SM), Entries(E){};

    SanitizerMask Mask;
    SectionEntries &Entries;
  };

  std::vector<SanitizerSection> SanitizerSections;
};

} // end namespace clang

#endif
