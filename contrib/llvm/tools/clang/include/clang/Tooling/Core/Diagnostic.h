//===--- Diagnostic.h - Framework for clang diagnostics tools --*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// \file
//  Structures supporting diagnostics and refactorings that span multiple
//  translation units. Indicate diagnostics reports and replacements
//  suggestions for the analyzed sources.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_TOOLING_CORE_DIAGNOSTIC_H
#define LLVM_CLANG_TOOLING_CORE_DIAGNOSTIC_H

#include "Replacement.h"
#include "clang/Basic/Diagnostic.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringMap.h"
#include "llvm/ADT/StringRef.h"
#include <string>

namespace clang {
namespace tooling {

/// Represents the diagnostic message with the error message associated
/// and the information on the location of the problem.
struct DiagnosticMessage {
  DiagnosticMessage(llvm::StringRef Message = "");

  /// Constructs a diagnostic message with anoffset to the diagnostic
  /// within the file where the problem occurred.
  ///
  /// \param Loc Should be a file location, it is not meaningful for a macro
  /// location.
  ///
  DiagnosticMessage(llvm::StringRef Message, const SourceManager &Sources,
                    SourceLocation Loc);
  std::string Message;
  std::string FilePath;
  unsigned FileOffset;
};

/// Represents the diagnostic with the level of severity and possible
/// fixes to be applied.
struct Diagnostic {
  enum Level {
    Warning = DiagnosticsEngine::Warning,
    Error = DiagnosticsEngine::Error
  };

  Diagnostic() = default;

  Diagnostic(llvm::StringRef DiagnosticName, Level DiagLevel,
             StringRef BuildDirectory);

  Diagnostic(llvm::StringRef DiagnosticName, const DiagnosticMessage &Message,
             const llvm::StringMap<Replacements> &Fix,
             const SmallVector<DiagnosticMessage, 1> &Notes, Level DiagLevel,
             llvm::StringRef BuildDirectory);

  /// Name identifying the Diagnostic.
  std::string DiagnosticName;

  /// Message associated to the diagnostic.
  DiagnosticMessage Message;

  /// Fixes to apply, grouped by file path.
  llvm::StringMap<Replacements> Fix;

  /// Potential notes about the diagnostic.
  SmallVector<DiagnosticMessage, 1> Notes;

  /// Diagnostic level. Can indicate either an error or a warning.
  Level DiagLevel;

  /// A build directory of the diagnostic source file.
  ///
  /// It's an absolute path which is `directory` field of the source file in
  /// compilation database. If users don't specify the compilation database
  /// directory, it is the current directory where clang-tidy runs.
  ///
  /// Note: it is empty in unittest.
  std::string BuildDirectory;
};

/// Collection of Diagnostics generated from a single translation unit.
struct TranslationUnitDiagnostics {
  /// Name of the main source for the translation unit.
  std::string MainSourceFile;
  std::vector<Diagnostic> Diagnostics;
};

} // end namespace tooling
} // end namespace clang
#endif // LLVM_CLANG_TOOLING_CORE_DIAGNOSTIC_H
