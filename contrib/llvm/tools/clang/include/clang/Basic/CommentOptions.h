//===- CommentOptions.h - Options for parsing comments ----------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
/// \file
/// Defines the clang::CommentOptions interface.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_BASIC_COMMENTOPTIONS_H
#define LLVM_CLANG_BASIC_COMMENTOPTIONS_H

#include <string>
#include <vector>

namespace clang {

/// Options for controlling comment parsing.
struct CommentOptions {
  using BlockCommandNamesTy = std::vector<std::string>;

  /// Command names to treat as block commands in comments.
  /// Should not include the leading backslash.
  BlockCommandNamesTy BlockCommandNames;

  /// Treat ordinary comments as documentation comments.
  bool ParseAllComments = false;

  CommentOptions() = default;
};

} // namespace clang

#endif // LLVM_CLANG_BASIC_COMMENTOPTIONS_H
