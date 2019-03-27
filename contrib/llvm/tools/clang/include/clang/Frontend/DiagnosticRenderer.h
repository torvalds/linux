//===- DiagnosticRenderer.h - Diagnostic Pretty-Printing --------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This is a utility class that provides support for pretty-printing of
// diagnostics. It is used to implement the different code paths which require
// such functionality in a consistent way.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_FRONTEND_DIAGNOSTICRENDERER_H
#define LLVM_CLANG_FRONTEND_DIAGNOSTICRENDERER_H

#include "clang/Basic/Diagnostic.h"
#include "clang/Basic/DiagnosticOptions.h"
#include "clang/Basic/LLVM.h"
#include "clang/Basic/SourceLocation.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/IntrusiveRefCntPtr.h"
#include "llvm/ADT/PointerUnion.h"
#include "llvm/ADT/StringRef.h"

namespace clang {

class LangOptions;
class SourceManager;

using DiagOrStoredDiag =
    llvm::PointerUnion<const Diagnostic *, const StoredDiagnostic *>;

/// Class to encapsulate the logic for formatting a diagnostic message.
///
/// Actual "printing" logic is implemented by subclasses.
///
/// This class provides an interface for building and emitting
/// diagnostic, including all of the macro backtraces, caret diagnostics, FixIt
/// Hints, and code snippets. In the presence of macros this involves
/// a recursive process, synthesizing notes for each macro expansion.
///
/// A brief worklist:
/// FIXME: Sink the recursive printing of template instantiations into this
/// class.
class DiagnosticRenderer {
protected:
  const LangOptions &LangOpts;
  IntrusiveRefCntPtr<DiagnosticOptions> DiagOpts;

  /// The location of the previous diagnostic if known.
  ///
  /// This will be invalid in cases where there is no (known) previous
  /// diagnostic location, or that location itself is invalid or comes from
  /// a different source manager than SM.
  SourceLocation LastLoc;

  /// The location of the last include whose stack was printed if known.
  ///
  /// Same restriction as LastLoc essentially, but tracking include stack
  /// root locations rather than diagnostic locations.
  SourceLocation LastIncludeLoc;

  /// The level of the last diagnostic emitted.
  ///
  /// The level of the last diagnostic emitted. Used to detect level changes
  /// which change the amount of information displayed.
  DiagnosticsEngine::Level LastLevel = DiagnosticsEngine::Ignored;

  DiagnosticRenderer(const LangOptions &LangOpts,
                     DiagnosticOptions *DiagOpts);

  virtual ~DiagnosticRenderer();

  virtual void emitDiagnosticMessage(FullSourceLoc Loc, PresumedLoc PLoc,
                                     DiagnosticsEngine::Level Level,
                                     StringRef Message,
                                     ArrayRef<CharSourceRange> Ranges,
                                     DiagOrStoredDiag Info) = 0;

  virtual void emitDiagnosticLoc(FullSourceLoc Loc, PresumedLoc PLoc,
                                 DiagnosticsEngine::Level Level,
                                 ArrayRef<CharSourceRange> Ranges) = 0;

  virtual void emitCodeContext(FullSourceLoc Loc,
                               DiagnosticsEngine::Level Level,
                               SmallVectorImpl<CharSourceRange> &Ranges,
                               ArrayRef<FixItHint> Hints) = 0;

  virtual void emitIncludeLocation(FullSourceLoc Loc, PresumedLoc PLoc) = 0;
  virtual void emitImportLocation(FullSourceLoc Loc, PresumedLoc PLoc,
                                  StringRef ModuleName) = 0;
  virtual void emitBuildingModuleLocation(FullSourceLoc Loc, PresumedLoc PLoc,
                                          StringRef ModuleName) = 0;

  virtual void beginDiagnostic(DiagOrStoredDiag D,
                               DiagnosticsEngine::Level Level) {}
  virtual void endDiagnostic(DiagOrStoredDiag D,
                             DiagnosticsEngine::Level Level) {}

private:
  void emitBasicNote(StringRef Message);
  void emitIncludeStack(FullSourceLoc Loc, PresumedLoc PLoc,
                        DiagnosticsEngine::Level Level);
  void emitIncludeStackRecursively(FullSourceLoc Loc);
  void emitImportStack(FullSourceLoc Loc);
  void emitImportStackRecursively(FullSourceLoc Loc, StringRef ModuleName);
  void emitModuleBuildStack(const SourceManager &SM);
  void emitCaret(FullSourceLoc Loc, DiagnosticsEngine::Level Level,
                 ArrayRef<CharSourceRange> Ranges, ArrayRef<FixItHint> Hints);
  void emitSingleMacroExpansion(FullSourceLoc Loc,
                                DiagnosticsEngine::Level Level,
                                ArrayRef<CharSourceRange> Ranges);
  void emitMacroExpansions(FullSourceLoc Loc, DiagnosticsEngine::Level Level,
                           ArrayRef<CharSourceRange> Ranges,
                           ArrayRef<FixItHint> Hints);

public:
  /// Emit a diagnostic.
  ///
  /// This is the primary entry point for emitting diagnostic messages.
  /// It handles formatting and rendering the message as well as any ancillary
  /// information needed based on macros whose expansions impact the
  /// diagnostic.
  ///
  /// \param Loc The location for this caret.
  /// \param Level The level of the diagnostic to be emitted.
  /// \param Message The diagnostic message to emit.
  /// \param Ranges The underlined ranges for this code snippet.
  /// \param FixItHints The FixIt hints active for this diagnostic.
  void emitDiagnostic(FullSourceLoc Loc, DiagnosticsEngine::Level Level,
                      StringRef Message, ArrayRef<CharSourceRange> Ranges,
                      ArrayRef<FixItHint> FixItHints,
                      DiagOrStoredDiag D = (Diagnostic *)nullptr);

  void emitStoredDiagnostic(StoredDiagnostic &Diag);
};

/// Subclass of DiagnosticRender that turns all subdiagostics into explicit
/// notes.  It is up to subclasses to further define the behavior.
class DiagnosticNoteRenderer : public DiagnosticRenderer {
public:
  DiagnosticNoteRenderer(const LangOptions &LangOpts,
                         DiagnosticOptions *DiagOpts)
      : DiagnosticRenderer(LangOpts, DiagOpts) {}

  ~DiagnosticNoteRenderer() override;

  void emitIncludeLocation(FullSourceLoc Loc, PresumedLoc PLoc) override;

  void emitImportLocation(FullSourceLoc Loc, PresumedLoc PLoc,
                          StringRef ModuleName) override;

  void emitBuildingModuleLocation(FullSourceLoc Loc, PresumedLoc PLoc,
                                  StringRef ModuleName) override;

  virtual void emitNote(FullSourceLoc Loc, StringRef Message) = 0;
};

} // namespace clang

#endif // LLVM_CLANG_FRONTEND_DIAGNOSTICRENDERER_H
