//===- FileMatchTrie.h ------------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
//  This file implements a match trie to find the matching file in a compilation
//  database based on a given path in the presence of symlinks.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_TOOLING_FILEMATCHTRIE_H
#define LLVM_CLANG_TOOLING_FILEMATCHTRIE_H

#include "clang/Basic/LLVM.h"
#include "llvm/ADT/StringRef.h"
#include <memory>

namespace clang {
namespace tooling {

class FileMatchTrieNode;

struct PathComparator {
  virtual ~PathComparator() = default;

  virtual bool equivalent(StringRef FileA, StringRef FileB) const = 0;
};

/// A trie to efficiently match against the entries of the compilation
/// database in order of matching suffix length.
///
/// When a clang tool is supposed to operate on a specific file, we have to
/// find the corresponding file in the compilation database. Although entries
/// in the compilation database are keyed by filename, a simple string match
/// is insufficient because of symlinks. Commonly, a project hierarchy looks
/// like this:
///   /<project-root>/src/<path>/<somefile>.cc      (used as input for the tool)
///   /<project-root>/build/<symlink-to-src>/<path>/<somefile>.cc (stored in DB)
///
/// Furthermore, there might be symlinks inside the source folder or inside the
/// database, so that the same source file is translated with different build
/// options.
///
/// For a given input file, the \c FileMatchTrie finds its entries in order
/// of matching suffix length. For each suffix length, there might be one or
/// more entries in the database. For each of those entries, it calls
/// \c llvm::sys::fs::equivalent() (injected as \c PathComparator). There might
/// be zero or more entries with the same matching suffix length that are
/// equivalent to the input file. Three cases are distinguished:
/// 0  equivalent files: Continue with the next suffix length.
/// 1  equivalent file:  Best match found, return it.
/// >1 equivalent files: Match is ambiguous, return error.
class FileMatchTrie {
public:
  FileMatchTrie();

  /// Construct a new \c FileMatchTrie with the given \c PathComparator.
  ///
  /// The \c FileMatchTrie takes ownership of 'Comparator'. Used for testing.
  FileMatchTrie(PathComparator* Comparator);

  ~FileMatchTrie();

  /// Insert a new absolute path. Relative paths are ignored.
  void insert(StringRef NewPath);

  /// Finds the corresponding file in this trie.
  ///
  /// Returns file name stored in this trie that is equivalent to 'FileName'
  /// according to 'Comparator', if it can be uniquely identified. If there
  /// are no matches an empty \c StringRef is returned. If there are ambiguous
  /// matches, an empty \c StringRef is returned and a corresponding message
  /// written to 'Error'.
  StringRef findEquivalent(StringRef FileName,
                           raw_ostream &Error) const;

private:
  FileMatchTrieNode *Root;
  std::unique_ptr<PathComparator> Comparator;
};

} // namespace tooling
} // namespace clang

#endif // LLVM_CLANG_TOOLING_FILEMATCHTRIE_H
