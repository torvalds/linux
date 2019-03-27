//===--- NamespaceEndCommentsFixer.h ----------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file declares NamespaceEndCommentsFixer, a TokenAnalyzer that
/// fixes namespace end comments.
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_LIB_FORMAT_NAMESPACEENDCOMMENTSFIXER_H
#define LLVM_CLANG_LIB_FORMAT_NAMESPACEENDCOMMENTSFIXER_H

#include "TokenAnalyzer.h"

namespace clang {
namespace format {

// Finds the namespace token corresponding to a closing namespace `}`, if that
// is to be formatted.
// If \p Line contains the closing `}` of a namespace, is affected and is not in
// a preprocessor directive, the result will be the matching namespace token.
// Otherwise returns null.
// \p AnnotatedLines is the sequence of lines from which \p Line is a member of.
const FormatToken *
getNamespaceToken(const AnnotatedLine *Line,
                  const SmallVectorImpl<AnnotatedLine *> &AnnotatedLines);

class NamespaceEndCommentsFixer : public TokenAnalyzer {
public:
  NamespaceEndCommentsFixer(const Environment &Env, const FormatStyle &Style);

  std::pair<tooling::Replacements, unsigned>
  analyze(TokenAnnotator &Annotator,
          SmallVectorImpl<AnnotatedLine *> &AnnotatedLines,
          FormatTokenLexer &Tokens) override;
};

} // end namespace format
} // end namespace clang

#endif
