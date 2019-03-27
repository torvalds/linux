//===-- SpecialCaseList.cpp - special case list for sanitizers ------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This is a utility class for instrumentation passes (like AddressSanitizer
// or ThreadSanitizer) to avoid instrumenting some functions or global
// variables, or to instrument some functions or global variables in a specific
// way, based on a user-supplied list.
//
//===----------------------------------------------------------------------===//

#include "llvm/Support/SpecialCaseList.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/Regex.h"
#include <string>
#include <system_error>
#include <utility>

#include <stdio.h>
namespace llvm {

bool SpecialCaseList::Matcher::insert(std::string Regexp,
                                      unsigned LineNumber,
                                      std::string &REError) {
  if (Regexp.empty()) {
    REError = "Supplied regexp was blank";
    return false;
  }

  if (Regex::isLiteralERE(Regexp)) {
    Strings[Regexp] = LineNumber;
    return true;
  }
  Trigrams.insert(Regexp);

  // Replace * with .*
  for (size_t pos = 0; (pos = Regexp.find('*', pos)) != std::string::npos;
       pos += strlen(".*")) {
    Regexp.replace(pos, strlen("*"), ".*");
  }

  Regexp = (Twine("^(") + StringRef(Regexp) + ")$").str();

  // Check that the regexp is valid.
  Regex CheckRE(Regexp);
  if (!CheckRE.isValid(REError))
    return false;

  RegExes.emplace_back(
      std::make_pair(make_unique<Regex>(std::move(CheckRE)), LineNumber));
  return true;
}

unsigned SpecialCaseList::Matcher::match(StringRef Query) const {
  auto It = Strings.find(Query);
  if (It != Strings.end())
    return It->second;
  if (Trigrams.isDefinitelyOut(Query))
    return false;
  for (auto& RegExKV : RegExes)
    if (RegExKV.first->match(Query))
      return RegExKV.second;
  return 0;
}

std::unique_ptr<SpecialCaseList>
SpecialCaseList::create(const std::vector<std::string> &Paths,
                        std::string &Error) {
  std::unique_ptr<SpecialCaseList> SCL(new SpecialCaseList());
  if (SCL->createInternal(Paths, Error))
    return SCL;
  return nullptr;
}

std::unique_ptr<SpecialCaseList> SpecialCaseList::create(const MemoryBuffer *MB,
                                                         std::string &Error) {
  std::unique_ptr<SpecialCaseList> SCL(new SpecialCaseList());
  if (SCL->createInternal(MB, Error))
    return SCL;
  return nullptr;
}

std::unique_ptr<SpecialCaseList>
SpecialCaseList::createOrDie(const std::vector<std::string> &Paths) {
  std::string Error;
  if (auto SCL = create(Paths, Error))
    return SCL;
  report_fatal_error(Error);
}

bool SpecialCaseList::createInternal(const std::vector<std::string> &Paths,
                                     std::string &Error) {
  StringMap<size_t> Sections;
  for (const auto &Path : Paths) {
    ErrorOr<std::unique_ptr<MemoryBuffer>> FileOrErr =
        MemoryBuffer::getFile(Path);
    if (std::error_code EC = FileOrErr.getError()) {
      Error = (Twine("can't open file '") + Path + "': " + EC.message()).str();
      return false;
    }
    std::string ParseError;
    if (!parse(FileOrErr.get().get(), Sections, ParseError)) {
      Error = (Twine("error parsing file '") + Path + "': " + ParseError).str();
      return false;
    }
  }
  return true;
}

bool SpecialCaseList::createInternal(const MemoryBuffer *MB,
                                     std::string &Error) {
  StringMap<size_t> Sections;
  if (!parse(MB, Sections, Error))
    return false;
  return true;
}

bool SpecialCaseList::parse(const MemoryBuffer *MB,
                            StringMap<size_t> &SectionsMap,
                            std::string &Error) {
  // Iterate through each line in the blacklist file.
  SmallVector<StringRef, 16> Lines;
  MB->getBuffer().split(Lines, '\n');

  unsigned LineNo = 1;
  StringRef Section = "*";

  for (auto I = Lines.begin(), E = Lines.end(); I != E; ++I, ++LineNo) {
    *I = I->trim();
    // Ignore empty lines and lines starting with "#"
    if (I->empty() || I->startswith("#"))
      continue;

    // Save section names
    if (I->startswith("[")) {
      if (!I->endswith("]")) {
        Error = (Twine("malformed section header on line ") + Twine(LineNo) +
                 ": " + *I).str();
        return false;
      }

      Section = I->slice(1, I->size() - 1);

      std::string REError;
      Regex CheckRE(Section);
      if (!CheckRE.isValid(REError)) {
        Error =
            (Twine("malformed regex for section ") + Section + ": '" + REError)
                .str();
        return false;
      }

      continue;
    }

    // Get our prefix and unparsed regexp.
    std::pair<StringRef, StringRef> SplitLine = I->split(":");
    StringRef Prefix = SplitLine.first;
    if (SplitLine.second.empty()) {
      // Missing ':' in the line.
      Error = (Twine("malformed line ") + Twine(LineNo) + ": '" +
               SplitLine.first + "'").str();
      return false;
    }

    std::pair<StringRef, StringRef> SplitRegexp = SplitLine.second.split("=");
    std::string Regexp = SplitRegexp.first;
    StringRef Category = SplitRegexp.second;

    // Create this section if it has not been seen before.
    if (SectionsMap.find(Section) == SectionsMap.end()) {
      std::unique_ptr<Matcher> M = make_unique<Matcher>();
      std::string REError;
      if (!M->insert(Section, LineNo, REError)) {
        Error = (Twine("malformed section ") + Section + ": '" + REError).str();
        return false;
      }

      SectionsMap[Section] = Sections.size();
      Sections.emplace_back(std::move(M));
    }

    auto &Entry = Sections[SectionsMap[Section]].Entries[Prefix][Category];
    std::string REError;
    if (!Entry.insert(std::move(Regexp), LineNo, REError)) {
      Error = (Twine("malformed regex in line ") + Twine(LineNo) + ": '" +
               SplitLine.second + "': " + REError).str();
      return false;
    }
  }
  return true;
}

SpecialCaseList::~SpecialCaseList() {}

bool SpecialCaseList::inSection(StringRef Section, StringRef Prefix,
                                StringRef Query, StringRef Category) const {
  return inSectionBlame(Section, Prefix, Query, Category);
}

unsigned SpecialCaseList::inSectionBlame(StringRef Section, StringRef Prefix,
                                         StringRef Query,
                                         StringRef Category) const {
  for (auto &SectionIter : Sections)
    if (SectionIter.SectionMatcher->match(Section)) {
      unsigned Blame =
          inSectionBlame(SectionIter.Entries, Prefix, Query, Category);
      if (Blame)
        return Blame;
    }
  return 0;
}

unsigned SpecialCaseList::inSectionBlame(const SectionEntries &Entries,
                                         StringRef Prefix, StringRef Query,
                                         StringRef Category) const {
  SectionEntries::const_iterator I = Entries.find(Prefix);
  if (I == Entries.end()) return 0;
  StringMap<Matcher>::const_iterator II = I->second.find(Category);
  if (II == I->second.end()) return 0;

  return II->getValue().match(Query);
}

}  // namespace llvm
