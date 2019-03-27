//===-- SpecialCaseList.h - special case list for sanitizers ----*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//===----------------------------------------------------------------------===//
//
// This is a utility class used to parse user-provided text files with
// "special case lists" for code sanitizers. Such files are used to
// define an "ABI list" for DataFlowSanitizer and blacklists for sanitizers
// like AddressSanitizer or UndefinedBehaviorSanitizer.
//
// Empty lines and lines starting with "#" are ignored. Sections are defined
// using a '[section_name]' header and can be used to specify sanitizers the
// entries below it apply to. Section names are regular expressions, and
// entries without a section header match all sections (e.g. an '[*]' header
// is assumed.)
// The remaining lines should have the form:
//   prefix:wildcard_expression[=category]
// If category is not specified, it is assumed to be empty string.
// Definitions of "prefix" and "category" are sanitizer-specific. For example,
// sanitizer blacklists support prefixes "src", "fun" and "global".
// Wildcard expressions define, respectively, source files, functions or
// globals which shouldn't be instrumented.
// Examples of categories:
//   "functional": used in DFSan to list functions with pure functional
//                 semantics.
//   "init": used in ASan blacklist to disable initialization-order bugs
//           detection for certain globals or source files.
// Full special case list file example:
// ---
// [address]
// # Blacklisted items:
// fun:*_ZN4base6subtle*
// global:*global_with_bad_access_or_initialization*
// global:*global_with_initialization_issues*=init
// type:*Namespace::ClassName*=init
// src:file_with_tricky_code.cc
// src:ignore-global-initializers-issues.cc=init
//
// [dataflow]
// # Functions with pure functional semantics:
// fun:cos=functional
// fun:sin=functional
// ---
// Note that the wild card is in fact an llvm::Regex, but * is automatically
// replaced with .*
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_SUPPORT_SPECIALCASELIST_H
#define LLVM_SUPPORT_SPECIALCASELIST_H

#include "llvm/ADT/StringMap.h"
#include "llvm/ADT/StringSet.h"
#include "llvm/Support/Regex.h"
#include "llvm/Support/TrigramIndex.h"
#include <string>
#include <vector>

namespace llvm {
class MemoryBuffer;
class Regex;
class StringRef;

class SpecialCaseList {
public:
  /// Parses the special case list entries from files. On failure, returns
  /// 0 and writes an error message to string.
  static std::unique_ptr<SpecialCaseList>
  create(const std::vector<std::string> &Paths, std::string &Error);
  /// Parses the special case list from a memory buffer. On failure, returns
  /// 0 and writes an error message to string.
  static std::unique_ptr<SpecialCaseList> create(const MemoryBuffer *MB,
                                                 std::string &Error);
  /// Parses the special case list entries from files. On failure, reports a
  /// fatal error.
  static std::unique_ptr<SpecialCaseList>
  createOrDie(const std::vector<std::string> &Paths);

  ~SpecialCaseList();

  /// Returns true, if special case list contains a line
  /// \code
  ///   @Prefix:<E>=@Category
  /// \endcode
  /// where @Query satisfies wildcard expression <E> in a given @Section.
  bool inSection(StringRef Section, StringRef Prefix, StringRef Query,
                 StringRef Category = StringRef()) const;

  /// Returns the line number corresponding to the special case list entry if
  /// the special case list contains a line
  /// \code
  ///   @Prefix:<E>=@Category
  /// \endcode
  /// where @Query satisfies wildcard expression <E> in a given @Section.
  /// Returns zero if there is no blacklist entry corresponding to this
  /// expression.
  unsigned inSectionBlame(StringRef Section, StringRef Prefix, StringRef Query,
                          StringRef Category = StringRef()) const;

protected:
  // Implementations of the create*() functions that can also be used by derived
  // classes.
  bool createInternal(const std::vector<std::string> &Paths,
                      std::string &Error);
  bool createInternal(const MemoryBuffer *MB, std::string &Error);

  SpecialCaseList() = default;
  SpecialCaseList(SpecialCaseList const &) = delete;
  SpecialCaseList &operator=(SpecialCaseList const &) = delete;

  /// Represents a set of regular expressions.  Regular expressions which are
  /// "literal" (i.e. no regex metacharacters) are stored in Strings.  The
  /// reason for doing so is efficiency; StringMap is much faster at matching
  /// literal strings than Regex.
  class Matcher {
  public:
    bool insert(std::string Regexp, unsigned LineNumber, std::string &REError);
    // Returns the line number in the source file that this query matches to.
    // Returns zero if no match is found.
    unsigned match(StringRef Query) const;

  private:
    StringMap<unsigned> Strings;
    TrigramIndex Trigrams;
    std::vector<std::pair<std::unique_ptr<Regex>, unsigned>> RegExes;
  };

  using SectionEntries = StringMap<StringMap<Matcher>>;

  struct Section {
    Section(std::unique_ptr<Matcher> M) : SectionMatcher(std::move(M)){};

    std::unique_ptr<Matcher> SectionMatcher;
    SectionEntries Entries;
  };

  std::vector<Section> Sections;

  /// Parses just-constructed SpecialCaseList entries from a memory buffer.
  bool parse(const MemoryBuffer *MB, StringMap<size_t> &SectionsMap,
             std::string &Error);

  // Helper method for derived classes to search by Prefix, Query, and Category
  // once they have already resolved a section entry.
  unsigned inSectionBlame(const SectionEntries &Entries, StringRef Prefix,
                          StringRef Query, StringRef Category) const;
};

}  // namespace llvm

#endif  // LLVM_SUPPORT_SPECIALCASELIST_H

