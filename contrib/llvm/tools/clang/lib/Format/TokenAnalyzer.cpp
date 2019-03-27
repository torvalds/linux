//===--- TokenAnalyzer.cpp - Analyze Token Streams --------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file implements an abstract TokenAnalyzer and associated helper
/// classes. TokenAnalyzer can be extended to generate replacements based on
/// an annotated and pre-processed token stream.
///
//===----------------------------------------------------------------------===//

#include "TokenAnalyzer.h"
#include "AffectedRangeManager.h"
#include "Encoding.h"
#include "FormatToken.h"
#include "FormatTokenLexer.h"
#include "TokenAnnotator.h"
#include "UnwrappedLineParser.h"
#include "clang/Basic/Diagnostic.h"
#include "clang/Basic/DiagnosticOptions.h"
#include "clang/Basic/FileManager.h"
#include "clang/Basic/SourceManager.h"
#include "clang/Format/Format.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/Support/Debug.h"

#define DEBUG_TYPE "format-formatter"

namespace clang {
namespace format {

Environment::Environment(StringRef Code, StringRef FileName,
                         ArrayRef<tooling::Range> Ranges,
                         unsigned FirstStartColumn, unsigned NextStartColumn,
                         unsigned LastStartColumn)
    : VirtualSM(new SourceManagerForFile(FileName, Code)), SM(VirtualSM->get()),
      ID(VirtualSM->get().getMainFileID()), FirstStartColumn(FirstStartColumn),
      NextStartColumn(NextStartColumn), LastStartColumn(LastStartColumn) {
  SourceLocation StartOfFile = SM.getLocForStartOfFile(ID);
  for (const tooling::Range &Range : Ranges) {
    SourceLocation Start = StartOfFile.getLocWithOffset(Range.getOffset());
    SourceLocation End = Start.getLocWithOffset(Range.getLength());
    CharRanges.push_back(CharSourceRange::getCharRange(Start, End));
  }
}

TokenAnalyzer::TokenAnalyzer(const Environment &Env, const FormatStyle &Style)
    : Style(Style), Env(Env),
      AffectedRangeMgr(Env.getSourceManager(), Env.getCharRanges()),
      UnwrappedLines(1),
      Encoding(encoding::detectEncoding(
          Env.getSourceManager().getBufferData(Env.getFileID()))) {
  LLVM_DEBUG(
      llvm::dbgs() << "File encoding: "
                   << (Encoding == encoding::Encoding_UTF8 ? "UTF8" : "unknown")
                   << "\n");
  LLVM_DEBUG(llvm::dbgs() << "Language: " << getLanguageName(Style.Language)
                          << "\n");
}

std::pair<tooling::Replacements, unsigned> TokenAnalyzer::process() {
  tooling::Replacements Result;
  FormatTokenLexer Tokens(Env.getSourceManager(), Env.getFileID(),
                          Env.getFirstStartColumn(), Style, Encoding);

  UnwrappedLineParser Parser(Style, Tokens.getKeywords(),
                             Env.getFirstStartColumn(), Tokens.lex(), *this);
  Parser.parse();
  assert(UnwrappedLines.rbegin()->empty());
  unsigned Penalty = 0;
  for (unsigned Run = 0, RunE = UnwrappedLines.size(); Run + 1 != RunE; ++Run) {
    LLVM_DEBUG(llvm::dbgs() << "Run " << Run << "...\n");
    SmallVector<AnnotatedLine *, 16> AnnotatedLines;

    TokenAnnotator Annotator(Style, Tokens.getKeywords());
    for (unsigned i = 0, e = UnwrappedLines[Run].size(); i != e; ++i) {
      AnnotatedLines.push_back(new AnnotatedLine(UnwrappedLines[Run][i]));
      Annotator.annotate(*AnnotatedLines.back());
    }

    std::pair<tooling::Replacements, unsigned> RunResult =
        analyze(Annotator, AnnotatedLines, Tokens);

    LLVM_DEBUG({
      llvm::dbgs() << "Replacements for run " << Run << ":\n";
      for (tooling::Replacements::const_iterator I = RunResult.first.begin(),
                                                 E = RunResult.first.end();
           I != E; ++I) {
        llvm::dbgs() << I->toString() << "\n";
      }
    });
    for (unsigned i = 0, e = AnnotatedLines.size(); i != e; ++i) {
      delete AnnotatedLines[i];
    }

    Penalty += RunResult.second;
    for (const auto &R : RunResult.first) {
      auto Err = Result.add(R);
      // FIXME: better error handling here. For now, simply return an empty
      // Replacements to indicate failure.
      if (Err) {
        llvm::errs() << llvm::toString(std::move(Err)) << "\n";
        return {tooling::Replacements(), 0};
      }
    }
  }
  return {Result, Penalty};
}

void TokenAnalyzer::consumeUnwrappedLine(const UnwrappedLine &TheLine) {
  assert(!UnwrappedLines.empty());
  UnwrappedLines.back().push_back(TheLine);
}

void TokenAnalyzer::finishRun() {
  UnwrappedLines.push_back(SmallVector<UnwrappedLine, 16>());
}

} // end namespace format
} // end namespace clang
