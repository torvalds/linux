//===--- IncludeStyle.h - Style of C++ #include directives -------*- C++-*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_TOOLING_INCLUSIONS_INCLUDESTYLE_H
#define LLVM_CLANG_TOOLING_INCLUSIONS_INCLUDESTYLE_H

#include "llvm/Support/YAMLTraits.h"
#include <string>
#include <vector>

namespace clang {
namespace tooling {

/// Style for sorting and grouping C++ #include directives.
struct IncludeStyle {
  /// Styles for sorting multiple ``#include`` blocks.
  enum IncludeBlocksStyle {
    /// Sort each ``#include`` block separately.
    /// \code
    ///    #include "b.h"               into      #include "b.h"
    ///
    ///    #include <lib/main.h>                  #include "a.h"
    ///    #include "a.h"                         #include <lib/main.h>
    /// \endcode
    IBS_Preserve,
    /// Merge multiple ``#include`` blocks together and sort as one.
    /// \code
    ///    #include "b.h"               into      #include "a.h"
    ///                                           #include "b.h"
    ///    #include <lib/main.h>                  #include <lib/main.h>
    ///    #include "a.h"
    /// \endcode
    IBS_Merge,
    /// Merge multiple ``#include`` blocks together and sort as one.
    /// Then split into groups based on category priority. See
    /// ``IncludeCategories``.
    /// \code
    ///    #include "b.h"               into      #include "a.h"
    ///                                           #include "b.h"
    ///    #include <lib/main.h>
    ///    #include "a.h"                         #include <lib/main.h>
    /// \endcode
    IBS_Regroup,
  };

  /// Dependent on the value, multiple ``#include`` blocks can be sorted
  /// as one and divided based on category.
  IncludeBlocksStyle IncludeBlocks;

  /// See documentation of ``IncludeCategories``.
  struct IncludeCategory {
    /// The regular expression that this category matches.
    std::string Regex;
    /// The priority to assign to this category.
    int Priority;
    bool operator==(const IncludeCategory &Other) const {
      return Regex == Other.Regex && Priority == Other.Priority;
    }
  };

  /// Regular expressions denoting the different ``#include`` categories
  /// used for ordering ``#includes``.
  ///
  /// `POSIX extended
  /// <http://pubs.opengroup.org/onlinepubs/9699919799/basedefs/V1_chap09.html>`_
  /// regular expressions are supported.
  ///
  /// These regular expressions are matched against the filename of an include
  /// (including the <> or "") in order. The value belonging to the first
  /// matching regular expression is assigned and ``#includes`` are sorted first
  /// according to increasing category number and then alphabetically within
  /// each category.
  ///
  /// If none of the regular expressions match, INT_MAX is assigned as
  /// category. The main header for a source file automatically gets category 0.
  /// so that it is generally kept at the beginning of the ``#includes``
  /// (http://llvm.org/docs/CodingStandards.html#include-style). However, you
  /// can also assign negative priorities if you have certain headers that
  /// always need to be first.
  ///
  /// To configure this in the .clang-format file, use:
  /// \code{.yaml}
  ///   IncludeCategories:
  ///     - Regex:           '^"(llvm|llvm-c|clang|clang-c)/'
  ///       Priority:        2
  ///     - Regex:           '^(<|"(gtest|gmock|isl|json)/)'
  ///       Priority:        3
  ///     - Regex:           '<[[:alnum:].]+>'
  ///       Priority:        4
  ///     - Regex:           '.*'
  ///       Priority:        1
  /// \endcode
  std::vector<IncludeCategory> IncludeCategories;

  /// Specify a regular expression of suffixes that are allowed in the
  /// file-to-main-include mapping.
  ///
  /// When guessing whether a #include is the "main" include (to assign
  /// category 0, see above), use this regex of allowed suffixes to the header
  /// stem. A partial match is done, so that:
  /// - "" means "arbitrary suffix"
  /// - "$" means "no suffix"
  ///
  /// For example, if configured to "(_test)?$", then a header a.h would be seen
  /// as the "main" include in both a.cc and a_test.cc.
  std::string IncludeIsMainRegex;
};

} // namespace tooling
} // namespace clang

LLVM_YAML_IS_SEQUENCE_VECTOR(clang::tooling::IncludeStyle::IncludeCategory)

namespace llvm {
namespace yaml {

template <>
struct MappingTraits<clang::tooling::IncludeStyle::IncludeCategory> {
  static void mapping(IO &IO,
                      clang::tooling::IncludeStyle::IncludeCategory &Category);
};

template <>
struct ScalarEnumerationTraits<
    clang::tooling::IncludeStyle::IncludeBlocksStyle> {
  static void
  enumeration(IO &IO, clang::tooling::IncludeStyle::IncludeBlocksStyle &Value);
};

} // namespace yaml
} // namespace llvm

#endif // LLVM_CLANG_TOOLING_INCLUSIONS_INCLUDESTYLE_H
