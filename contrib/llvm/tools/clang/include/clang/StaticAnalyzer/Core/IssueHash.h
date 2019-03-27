//===---------- IssueHash.h - Generate identification hashes ----*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
#ifndef LLVM_CLANG_STATICANALYZER_CORE_ISSUE_HASH_H
#define LLVM_CLANG_STATICANALYZER_CORE_ISSUE_HASH_H

#include "llvm/ADT/SmallString.h"

namespace clang {
class Decl;
class SourceManager;
class FullSourceLoc;
class LangOptions;

/// Get an MD5 hash to help identify bugs.
///
/// This function returns a hash that helps identify bugs within a source file.
/// This identification can be utilized to diff diagnostic results on different
/// snapshots of a projects, or maintain a database of suppressed diagnotics.
///
/// The hash contains the normalized text of the location associated with the
/// diagnostic. Normalization means removing the whitespaces. The associated
/// location is the either the last location of a diagnostic path or a uniqueing
/// location. The bugtype and the name of the checker is also part of the hash.
/// The last component is the string representation of the enclosing declaration
/// of the associated location.
///
/// In case a new hash is introduced, the old one should still be maintained for
/// a while. One should not introduce a new hash for every change, it is
/// possible to introduce experimental hashes that may change in the future.
/// Such hashes should be marked as experimental using a comment in the plist
/// files.
llvm::SmallString<32> GetIssueHash(const SourceManager &SM,
                                   FullSourceLoc &IssueLoc,
                                   llvm::StringRef CheckerName,
                                   llvm::StringRef BugType, const Decl *D,
                                   const LangOptions &LangOpts);

/// Get the string representation of issue hash. See GetIssueHash() for
/// more information.
std::string GetIssueString(const SourceManager &SM, FullSourceLoc &IssueLoc,
                           llvm::StringRef CheckerName, llvm::StringRef BugType,
                           const Decl *D, const LangOptions &LangOpts);
} // namespace clang

#endif
