//===-- ClangHighlighter.h --------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef liblldb_ClangHighlighter_h_
#define liblldb_ClangHighlighter_h_

#include "lldb/Utility/Stream.h"
#include "llvm/ADT/StringSet.h"

#include "lldb/Core/Highlighter.h"

namespace lldb_private {

class ClangHighlighter : public Highlighter {
  llvm::StringSet<> keywords;

public:
  ClangHighlighter();
  llvm::StringRef GetName() const override { return "clang"; }

  void Highlight(const HighlightStyle &options, llvm::StringRef line,
                 llvm::Optional<size_t> cursor_pos,
                 llvm::StringRef previous_lines, Stream &s) const override;

  /// Returns true if the given string represents a keywords in any Clang
  /// supported language.
  bool isKeyword(llvm::StringRef token) const;
};

} // namespace lldb_private

#endif // liblldb_ClangHighlighter_h_
