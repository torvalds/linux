//===--- UsingDeclarationsSorter.h ------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file declares UsingDeclarationsSorter, a TokenAnalyzer that
/// sorts consecutive using declarations.
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_LIB_FORMAT_USINGDECLARATIONSSORTER_H
#define LLVM_CLANG_LIB_FORMAT_USINGDECLARATIONSSORTER_H

#include "TokenAnalyzer.h"

namespace clang {
namespace format {

class UsingDeclarationsSorter : public TokenAnalyzer {
public:
  UsingDeclarationsSorter(const Environment &Env, const FormatStyle &Style);

  std::pair<tooling::Replacements, unsigned>
  analyze(TokenAnnotator &Annotator,
          SmallVectorImpl<AnnotatedLine *> &AnnotatedLines,
          FormatTokenLexer &Tokens) override;
};

} // end namespace format
} // end namespace clang

#endif
