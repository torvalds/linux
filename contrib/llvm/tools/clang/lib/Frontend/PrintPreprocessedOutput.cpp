//===--- PrintPreprocessedOutput.cpp - Implement the -E mode --------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This code simply runs the preprocessor on the input file and prints out the
// result.  This is the traditional behavior of the -E option.
//
//===----------------------------------------------------------------------===//

#include "clang/Frontend/Utils.h"
#include "clang/Basic/CharInfo.h"
#include "clang/Basic/Diagnostic.h"
#include "clang/Basic/SourceManager.h"
#include "clang/Frontend/PreprocessorOutputOptions.h"
#include "clang/Lex/MacroInfo.h"
#include "clang/Lex/PPCallbacks.h"
#include "clang/Lex/Pragma.h"
#include "clang/Lex/Preprocessor.h"
#include "clang/Lex/TokenConcatenation.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/raw_ostream.h"
#include <cstdio>
using namespace clang;

/// PrintMacroDefinition - Print a macro definition in a form that will be
/// properly accepted back as a definition.
static void PrintMacroDefinition(const IdentifierInfo &II, const MacroInfo &MI,
                                 Preprocessor &PP, raw_ostream &OS) {
  OS << "#define " << II.getName();

  if (MI.isFunctionLike()) {
    OS << '(';
    if (!MI.param_empty()) {
      MacroInfo::param_iterator AI = MI.param_begin(), E = MI.param_end();
      for (; AI+1 != E; ++AI) {
        OS << (*AI)->getName();
        OS << ',';
      }

      // Last argument.
      if ((*AI)->getName() == "__VA_ARGS__")
        OS << "...";
      else
        OS << (*AI)->getName();
    }

    if (MI.isGNUVarargs())
      OS << "...";  // #define foo(x...)

    OS << ')';
  }

  // GCC always emits a space, even if the macro body is empty.  However, do not
  // want to emit two spaces if the first token has a leading space.
  if (MI.tokens_empty() || !MI.tokens_begin()->hasLeadingSpace())
    OS << ' ';

  SmallString<128> SpellingBuffer;
  for (const auto &T : MI.tokens()) {
    if (T.hasLeadingSpace())
      OS << ' ';

    OS << PP.getSpelling(T, SpellingBuffer);
  }
}

//===----------------------------------------------------------------------===//
// Preprocessed token printer
//===----------------------------------------------------------------------===//

namespace {
class PrintPPOutputPPCallbacks : public PPCallbacks {
  Preprocessor &PP;
  SourceManager &SM;
  TokenConcatenation ConcatInfo;
public:
  raw_ostream &OS;
private:
  unsigned CurLine;

  bool EmittedTokensOnThisLine;
  bool EmittedDirectiveOnThisLine;
  SrcMgr::CharacteristicKind FileType;
  SmallString<512> CurFilename;
  bool Initialized;
  bool DisableLineMarkers;
  bool DumpDefines;
  bool DumpIncludeDirectives;
  bool UseLineDirectives;
  bool IsFirstFileEntered;
public:
  PrintPPOutputPPCallbacks(Preprocessor &pp, raw_ostream &os, bool lineMarkers,
                           bool defines, bool DumpIncludeDirectives,
                           bool UseLineDirectives)
      : PP(pp), SM(PP.getSourceManager()), ConcatInfo(PP), OS(os),
        DisableLineMarkers(lineMarkers), DumpDefines(defines),
        DumpIncludeDirectives(DumpIncludeDirectives),
        UseLineDirectives(UseLineDirectives) {
    CurLine = 0;
    CurFilename += "<uninit>";
    EmittedTokensOnThisLine = false;
    EmittedDirectiveOnThisLine = false;
    FileType = SrcMgr::C_User;
    Initialized = false;
    IsFirstFileEntered = false;
  }

  void setEmittedTokensOnThisLine() { EmittedTokensOnThisLine = true; }
  bool hasEmittedTokensOnThisLine() const { return EmittedTokensOnThisLine; }

  void setEmittedDirectiveOnThisLine() { EmittedDirectiveOnThisLine = true; }
  bool hasEmittedDirectiveOnThisLine() const {
    return EmittedDirectiveOnThisLine;
  }

  bool startNewLineIfNeeded(bool ShouldUpdateCurrentLine = true);

  void FileChanged(SourceLocation Loc, FileChangeReason Reason,
                   SrcMgr::CharacteristicKind FileType,
                   FileID PrevFID) override;
  void InclusionDirective(SourceLocation HashLoc, const Token &IncludeTok,
                          StringRef FileName, bool IsAngled,
                          CharSourceRange FilenameRange, const FileEntry *File,
                          StringRef SearchPath, StringRef RelativePath,
                          const Module *Imported,
                          SrcMgr::CharacteristicKind FileType) override;
  void Ident(SourceLocation Loc, StringRef str) override;
  void PragmaMessage(SourceLocation Loc, StringRef Namespace,
                     PragmaMessageKind Kind, StringRef Str) override;
  void PragmaDebug(SourceLocation Loc, StringRef DebugType) override;
  void PragmaDiagnosticPush(SourceLocation Loc, StringRef Namespace) override;
  void PragmaDiagnosticPop(SourceLocation Loc, StringRef Namespace) override;
  void PragmaDiagnostic(SourceLocation Loc, StringRef Namespace,
                        diag::Severity Map, StringRef Str) override;
  void PragmaWarning(SourceLocation Loc, StringRef WarningSpec,
                     ArrayRef<int> Ids) override;
  void PragmaWarningPush(SourceLocation Loc, int Level) override;
  void PragmaWarningPop(SourceLocation Loc) override;
  void PragmaAssumeNonNullBegin(SourceLocation Loc) override;
  void PragmaAssumeNonNullEnd(SourceLocation Loc) override;

  bool HandleFirstTokOnLine(Token &Tok);

  /// Move to the line of the provided source location. This will
  /// return true if the output stream required adjustment or if
  /// the requested location is on the first line.
  bool MoveToLine(SourceLocation Loc) {
    PresumedLoc PLoc = SM.getPresumedLoc(Loc);
    if (PLoc.isInvalid())
      return false;
    return MoveToLine(PLoc.getLine()) || (PLoc.getLine() == 1);
  }
  bool MoveToLine(unsigned LineNo);

  bool AvoidConcat(const Token &PrevPrevTok, const Token &PrevTok,
                   const Token &Tok) {
    return ConcatInfo.AvoidConcat(PrevPrevTok, PrevTok, Tok);
  }
  void WriteLineInfo(unsigned LineNo, const char *Extra=nullptr,
                     unsigned ExtraLen=0);
  bool LineMarkersAreDisabled() const { return DisableLineMarkers; }
  void HandleNewlinesInToken(const char *TokStr, unsigned Len);

  /// MacroDefined - This hook is called whenever a macro definition is seen.
  void MacroDefined(const Token &MacroNameTok,
                    const MacroDirective *MD) override;

  /// MacroUndefined - This hook is called whenever a macro #undef is seen.
  void MacroUndefined(const Token &MacroNameTok,
                      const MacroDefinition &MD,
                      const MacroDirective *Undef) override;

  void BeginModule(const Module *M);
  void EndModule(const Module *M);
};
}  // end anonymous namespace

void PrintPPOutputPPCallbacks::WriteLineInfo(unsigned LineNo,
                                             const char *Extra,
                                             unsigned ExtraLen) {
  startNewLineIfNeeded(/*ShouldUpdateCurrentLine=*/false);

  // Emit #line directives or GNU line markers depending on what mode we're in.
  if (UseLineDirectives) {
    OS << "#line" << ' ' << LineNo << ' ' << '"';
    OS.write_escaped(CurFilename);
    OS << '"';
  } else {
    OS << '#' << ' ' << LineNo << ' ' << '"';
    OS.write_escaped(CurFilename);
    OS << '"';

    if (ExtraLen)
      OS.write(Extra, ExtraLen);

    if (FileType == SrcMgr::C_System)
      OS.write(" 3", 2);
    else if (FileType == SrcMgr::C_ExternCSystem)
      OS.write(" 3 4", 4);
  }
  OS << '\n';
}

/// MoveToLine - Move the output to the source line specified by the location
/// object.  We can do this by emitting some number of \n's, or be emitting a
/// #line directive.  This returns false if already at the specified line, true
/// if some newlines were emitted.
bool PrintPPOutputPPCallbacks::MoveToLine(unsigned LineNo) {
  // If this line is "close enough" to the original line, just print newlines,
  // otherwise print a #line directive.
  if (LineNo-CurLine <= 8) {
    if (LineNo-CurLine == 1)
      OS << '\n';
    else if (LineNo == CurLine)
      return false;    // Spelling line moved, but expansion line didn't.
    else {
      const char *NewLines = "\n\n\n\n\n\n\n\n";
      OS.write(NewLines, LineNo-CurLine);
    }
  } else if (!DisableLineMarkers) {
    // Emit a #line or line marker.
    WriteLineInfo(LineNo, nullptr, 0);
  } else {
    // Okay, we're in -P mode, which turns off line markers.  However, we still
    // need to emit a newline between tokens on different lines.
    startNewLineIfNeeded(/*ShouldUpdateCurrentLine=*/false);
  }

  CurLine = LineNo;
  return true;
}

bool
PrintPPOutputPPCallbacks::startNewLineIfNeeded(bool ShouldUpdateCurrentLine) {
  if (EmittedTokensOnThisLine || EmittedDirectiveOnThisLine) {
    OS << '\n';
    EmittedTokensOnThisLine = false;
    EmittedDirectiveOnThisLine = false;
    if (ShouldUpdateCurrentLine)
      ++CurLine;
    return true;
  }

  return false;
}

/// FileChanged - Whenever the preprocessor enters or exits a #include file
/// it invokes this handler.  Update our conception of the current source
/// position.
void PrintPPOutputPPCallbacks::FileChanged(SourceLocation Loc,
                                           FileChangeReason Reason,
                                       SrcMgr::CharacteristicKind NewFileType,
                                       FileID PrevFID) {
  // Unless we are exiting a #include, make sure to skip ahead to the line the
  // #include directive was at.
  SourceManager &SourceMgr = SM;

  PresumedLoc UserLoc = SourceMgr.getPresumedLoc(Loc);
  if (UserLoc.isInvalid())
    return;

  unsigned NewLine = UserLoc.getLine();

  if (Reason == PPCallbacks::EnterFile) {
    SourceLocation IncludeLoc = UserLoc.getIncludeLoc();
    if (IncludeLoc.isValid())
      MoveToLine(IncludeLoc);
  } else if (Reason == PPCallbacks::SystemHeaderPragma) {
    // GCC emits the # directive for this directive on the line AFTER the
    // directive and emits a bunch of spaces that aren't needed. This is because
    // otherwise we will emit a line marker for THIS line, which requires an
    // extra blank line after the directive to avoid making all following lines
    // off by one. We can do better by simply incrementing NewLine here.
    NewLine += 1;
  }

  CurLine = NewLine;

  CurFilename.clear();
  CurFilename += UserLoc.getFilename();
  FileType = NewFileType;

  if (DisableLineMarkers) {
    startNewLineIfNeeded(/*ShouldUpdateCurrentLine=*/false);
    return;
  }

  if (!Initialized) {
    WriteLineInfo(CurLine);
    Initialized = true;
  }

  // Do not emit an enter marker for the main file (which we expect is the first
  // entered file). This matches gcc, and improves compatibility with some tools
  // which track the # line markers as a way to determine when the preprocessed
  // output is in the context of the main file.
  if (Reason == PPCallbacks::EnterFile && !IsFirstFileEntered) {
    IsFirstFileEntered = true;
    return;
  }

  switch (Reason) {
  case PPCallbacks::EnterFile:
    WriteLineInfo(CurLine, " 1", 2);
    break;
  case PPCallbacks::ExitFile:
    WriteLineInfo(CurLine, " 2", 2);
    break;
  case PPCallbacks::SystemHeaderPragma:
  case PPCallbacks::RenameFile:
    WriteLineInfo(CurLine);
    break;
  }
}

void PrintPPOutputPPCallbacks::InclusionDirective(
    SourceLocation HashLoc,
    const Token &IncludeTok,
    StringRef FileName,
    bool IsAngled,
    CharSourceRange FilenameRange,
    const FileEntry *File,
    StringRef SearchPath,
    StringRef RelativePath,
    const Module *Imported,
    SrcMgr::CharacteristicKind FileType) {
  // In -dI mode, dump #include directives prior to dumping their content or
  // interpretation.
  if (DumpIncludeDirectives) {
    startNewLineIfNeeded();
    MoveToLine(HashLoc);
    const std::string TokenText = PP.getSpelling(IncludeTok);
    assert(!TokenText.empty());
    OS << "#" << TokenText << " "
       << (IsAngled ? '<' : '"') << FileName << (IsAngled ? '>' : '"')
       << " /* clang -E -dI */";
    setEmittedDirectiveOnThisLine();
    startNewLineIfNeeded();
  }

  // When preprocessing, turn implicit imports into module import pragmas.
  if (Imported) {
    switch (IncludeTok.getIdentifierInfo()->getPPKeywordID()) {
    case tok::pp_include:
    case tok::pp_import:
    case tok::pp_include_next:
      startNewLineIfNeeded();
      MoveToLine(HashLoc);
      OS << "#pragma clang module import " << Imported->getFullModuleName(true)
         << " /* clang -E: implicit import for "
         << "#" << PP.getSpelling(IncludeTok) << " "
         << (IsAngled ? '<' : '"') << FileName << (IsAngled ? '>' : '"')
         << " */";
      // Since we want a newline after the pragma, but not a #<line>, start a
      // new line immediately.
      EmittedTokensOnThisLine = true;
      startNewLineIfNeeded();
      break;

    case tok::pp___include_macros:
      // #__include_macros has no effect on a user of a preprocessed source
      // file; the only effect is on preprocessing.
      //
      // FIXME: That's not *quite* true: it causes the module in question to
      // be loaded, which can affect downstream diagnostics.
      break;

    default:
      llvm_unreachable("unknown include directive kind");
      break;
    }
  }
}

/// Handle entering the scope of a module during a module compilation.
void PrintPPOutputPPCallbacks::BeginModule(const Module *M) {
  startNewLineIfNeeded();
  OS << "#pragma clang module begin " << M->getFullModuleName(true);
  setEmittedDirectiveOnThisLine();
}

/// Handle leaving the scope of a module during a module compilation.
void PrintPPOutputPPCallbacks::EndModule(const Module *M) {
  startNewLineIfNeeded();
  OS << "#pragma clang module end /*" << M->getFullModuleName(true) << "*/";
  setEmittedDirectiveOnThisLine();
}

/// Ident - Handle #ident directives when read by the preprocessor.
///
void PrintPPOutputPPCallbacks::Ident(SourceLocation Loc, StringRef S) {
  MoveToLine(Loc);

  OS.write("#ident ", strlen("#ident "));
  OS.write(S.begin(), S.size());
  EmittedTokensOnThisLine = true;
}

/// MacroDefined - This hook is called whenever a macro definition is seen.
void PrintPPOutputPPCallbacks::MacroDefined(const Token &MacroNameTok,
                                            const MacroDirective *MD) {
  const MacroInfo *MI = MD->getMacroInfo();
  // Only print out macro definitions in -dD mode.
  if (!DumpDefines ||
      // Ignore __FILE__ etc.
      MI->isBuiltinMacro()) return;

  MoveToLine(MI->getDefinitionLoc());
  PrintMacroDefinition(*MacroNameTok.getIdentifierInfo(), *MI, PP, OS);
  setEmittedDirectiveOnThisLine();
}

void PrintPPOutputPPCallbacks::MacroUndefined(const Token &MacroNameTok,
                                              const MacroDefinition &MD,
                                              const MacroDirective *Undef) {
  // Only print out macro definitions in -dD mode.
  if (!DumpDefines) return;

  MoveToLine(MacroNameTok.getLocation());
  OS << "#undef " << MacroNameTok.getIdentifierInfo()->getName();
  setEmittedDirectiveOnThisLine();
}

static void outputPrintable(raw_ostream &OS, StringRef Str) {
  for (unsigned char Char : Str) {
    if (isPrintable(Char) && Char != '\\' && Char != '"')
      OS << (char)Char;
    else // Output anything hard as an octal escape.
      OS << '\\'
         << (char)('0' + ((Char >> 6) & 7))
         << (char)('0' + ((Char >> 3) & 7))
         << (char)('0' + ((Char >> 0) & 7));
  }
}

void PrintPPOutputPPCallbacks::PragmaMessage(SourceLocation Loc,
                                             StringRef Namespace,
                                             PragmaMessageKind Kind,
                                             StringRef Str) {
  startNewLineIfNeeded();
  MoveToLine(Loc);
  OS << "#pragma ";
  if (!Namespace.empty())
    OS << Namespace << ' ';
  switch (Kind) {
    case PMK_Message:
      OS << "message(\"";
      break;
    case PMK_Warning:
      OS << "warning \"";
      break;
    case PMK_Error:
      OS << "error \"";
      break;
  }

  outputPrintable(OS, Str);
  OS << '"';
  if (Kind == PMK_Message)
    OS << ')';
  setEmittedDirectiveOnThisLine();
}

void PrintPPOutputPPCallbacks::PragmaDebug(SourceLocation Loc,
                                           StringRef DebugType) {
  startNewLineIfNeeded();
  MoveToLine(Loc);

  OS << "#pragma clang __debug ";
  OS << DebugType;

  setEmittedDirectiveOnThisLine();
}

void PrintPPOutputPPCallbacks::
PragmaDiagnosticPush(SourceLocation Loc, StringRef Namespace) {
  startNewLineIfNeeded();
  MoveToLine(Loc);
  OS << "#pragma " << Namespace << " diagnostic push";
  setEmittedDirectiveOnThisLine();
}

void PrintPPOutputPPCallbacks::
PragmaDiagnosticPop(SourceLocation Loc, StringRef Namespace) {
  startNewLineIfNeeded();
  MoveToLine(Loc);
  OS << "#pragma " << Namespace << " diagnostic pop";
  setEmittedDirectiveOnThisLine();
}

void PrintPPOutputPPCallbacks::PragmaDiagnostic(SourceLocation Loc,
                                                StringRef Namespace,
                                                diag::Severity Map,
                                                StringRef Str) {
  startNewLineIfNeeded();
  MoveToLine(Loc);
  OS << "#pragma " << Namespace << " diagnostic ";
  switch (Map) {
  case diag::Severity::Remark:
    OS << "remark";
    break;
  case diag::Severity::Warning:
    OS << "warning";
    break;
  case diag::Severity::Error:
    OS << "error";
    break;
  case diag::Severity::Ignored:
    OS << "ignored";
    break;
  case diag::Severity::Fatal:
    OS << "fatal";
    break;
  }
  OS << " \"" << Str << '"';
  setEmittedDirectiveOnThisLine();
}

void PrintPPOutputPPCallbacks::PragmaWarning(SourceLocation Loc,
                                             StringRef WarningSpec,
                                             ArrayRef<int> Ids) {
  startNewLineIfNeeded();
  MoveToLine(Loc);
  OS << "#pragma warning(" << WarningSpec << ':';
  for (ArrayRef<int>::iterator I = Ids.begin(), E = Ids.end(); I != E; ++I)
    OS << ' ' << *I;
  OS << ')';
  setEmittedDirectiveOnThisLine();
}

void PrintPPOutputPPCallbacks::PragmaWarningPush(SourceLocation Loc,
                                                 int Level) {
  startNewLineIfNeeded();
  MoveToLine(Loc);
  OS << "#pragma warning(push";
  if (Level >= 0)
    OS << ", " << Level;
  OS << ')';
  setEmittedDirectiveOnThisLine();
}

void PrintPPOutputPPCallbacks::PragmaWarningPop(SourceLocation Loc) {
  startNewLineIfNeeded();
  MoveToLine(Loc);
  OS << "#pragma warning(pop)";
  setEmittedDirectiveOnThisLine();
}

void PrintPPOutputPPCallbacks::
PragmaAssumeNonNullBegin(SourceLocation Loc) {
  startNewLineIfNeeded();
  MoveToLine(Loc);
  OS << "#pragma clang assume_nonnull begin";
  setEmittedDirectiveOnThisLine();
}

void PrintPPOutputPPCallbacks::
PragmaAssumeNonNullEnd(SourceLocation Loc) {
  startNewLineIfNeeded();
  MoveToLine(Loc);
  OS << "#pragma clang assume_nonnull end";
  setEmittedDirectiveOnThisLine();
}

/// HandleFirstTokOnLine - When emitting a preprocessed file in -E mode, this
/// is called for the first token on each new line.  If this really is the start
/// of a new logical line, handle it and return true, otherwise return false.
/// This may not be the start of a logical line because the "start of line"
/// marker is set for spelling lines, not expansion ones.
bool PrintPPOutputPPCallbacks::HandleFirstTokOnLine(Token &Tok) {
  // Figure out what line we went to and insert the appropriate number of
  // newline characters.
  if (!MoveToLine(Tok.getLocation()))
    return false;

  // Print out space characters so that the first token on a line is
  // indented for easy reading.
  unsigned ColNo = SM.getExpansionColumnNumber(Tok.getLocation());

  // The first token on a line can have a column number of 1, yet still expect
  // leading white space, if a macro expansion in column 1 starts with an empty
  // macro argument, or an empty nested macro expansion. In this case, move the
  // token to column 2.
  if (ColNo == 1 && Tok.hasLeadingSpace())
    ColNo = 2;

  // This hack prevents stuff like:
  // #define HASH #
  // HASH define foo bar
  // From having the # character end up at column 1, which makes it so it
  // is not handled as a #define next time through the preprocessor if in
  // -fpreprocessed mode.
  if (ColNo <= 1 && Tok.is(tok::hash))
    OS << ' ';

  // Otherwise, indent the appropriate number of spaces.
  for (; ColNo > 1; --ColNo)
    OS << ' ';

  return true;
}

void PrintPPOutputPPCallbacks::HandleNewlinesInToken(const char *TokStr,
                                                     unsigned Len) {
  unsigned NumNewlines = 0;
  for (; Len; --Len, ++TokStr) {
    if (*TokStr != '\n' &&
        *TokStr != '\r')
      continue;

    ++NumNewlines;

    // If we have \n\r or \r\n, skip both and count as one line.
    if (Len != 1 &&
        (TokStr[1] == '\n' || TokStr[1] == '\r') &&
        TokStr[0] != TokStr[1]) {
      ++TokStr;
      --Len;
    }
  }

  if (NumNewlines == 0) return;

  CurLine += NumNewlines;
}


namespace {
struct UnknownPragmaHandler : public PragmaHandler {
  const char *Prefix;
  PrintPPOutputPPCallbacks *Callbacks;

  // Set to true if tokens should be expanded
  bool ShouldExpandTokens;

  UnknownPragmaHandler(const char *prefix, PrintPPOutputPPCallbacks *callbacks,
                       bool RequireTokenExpansion)
      : Prefix(prefix), Callbacks(callbacks),
        ShouldExpandTokens(RequireTokenExpansion) {}
  void HandlePragma(Preprocessor &PP, PragmaIntroducerKind Introducer,
                    Token &PragmaTok) override {
    // Figure out what line we went to and insert the appropriate number of
    // newline characters.
    Callbacks->startNewLineIfNeeded();
    Callbacks->MoveToLine(PragmaTok.getLocation());
    Callbacks->OS.write(Prefix, strlen(Prefix));

    if (ShouldExpandTokens) {
      // The first token does not have expanded macros. Expand them, if
      // required.
      auto Toks = llvm::make_unique<Token[]>(1);
      Toks[0] = PragmaTok;
      PP.EnterTokenStream(std::move(Toks), /*NumToks=*/1,
                          /*DisableMacroExpansion=*/false);
      PP.Lex(PragmaTok);
    }
    Token PrevToken;
    Token PrevPrevToken;
    PrevToken.startToken();
    PrevPrevToken.startToken();

    // Read and print all of the pragma tokens.
    while (PragmaTok.isNot(tok::eod)) {
      if (PragmaTok.hasLeadingSpace() ||
          Callbacks->AvoidConcat(PrevPrevToken, PrevToken, PragmaTok))
        Callbacks->OS << ' ';
      std::string TokSpell = PP.getSpelling(PragmaTok);
      Callbacks->OS.write(&TokSpell[0], TokSpell.size());

      PrevPrevToken = PrevToken;
      PrevToken = PragmaTok;

      if (ShouldExpandTokens)
        PP.Lex(PragmaTok);
      else
        PP.LexUnexpandedToken(PragmaTok);
    }
    Callbacks->setEmittedDirectiveOnThisLine();
  }
};
} // end anonymous namespace


static void PrintPreprocessedTokens(Preprocessor &PP, Token &Tok,
                                    PrintPPOutputPPCallbacks *Callbacks,
                                    raw_ostream &OS) {
  bool DropComments = PP.getLangOpts().TraditionalCPP &&
                      !PP.getCommentRetentionState();

  char Buffer[256];
  Token PrevPrevTok, PrevTok;
  PrevPrevTok.startToken();
  PrevTok.startToken();
  while (1) {
    if (Callbacks->hasEmittedDirectiveOnThisLine()) {
      Callbacks->startNewLineIfNeeded();
      Callbacks->MoveToLine(Tok.getLocation());
    }

    // If this token is at the start of a line, emit newlines if needed.
    if (Tok.isAtStartOfLine() && Callbacks->HandleFirstTokOnLine(Tok)) {
      // done.
    } else if (Tok.hasLeadingSpace() ||
               // If we haven't emitted a token on this line yet, PrevTok isn't
               // useful to look at and no concatenation could happen anyway.
               (Callbacks->hasEmittedTokensOnThisLine() &&
                // Don't print "-" next to "-", it would form "--".
                Callbacks->AvoidConcat(PrevPrevTok, PrevTok, Tok))) {
      OS << ' ';
    }

    if (DropComments && Tok.is(tok::comment)) {
      // Skip comments. Normally the preprocessor does not generate
      // tok::comment nodes at all when not keeping comments, but under
      // -traditional-cpp the lexer keeps /all/ whitespace, including comments.
      SourceLocation StartLoc = Tok.getLocation();
      Callbacks->MoveToLine(StartLoc.getLocWithOffset(Tok.getLength()));
    } else if (Tok.is(tok::eod)) {
      // Don't print end of directive tokens, since they are typically newlines
      // that mess up our line tracking. These come from unknown pre-processor
      // directives or hash-prefixed comments in standalone assembly files.
      PP.Lex(Tok);
      continue;
    } else if (Tok.is(tok::annot_module_include)) {
      // PrintPPOutputPPCallbacks::InclusionDirective handles producing
      // appropriate output here. Ignore this token entirely.
      PP.Lex(Tok);
      continue;
    } else if (Tok.is(tok::annot_module_begin)) {
      // FIXME: We retrieve this token after the FileChanged callback, and
      // retrieve the module_end token before the FileChanged callback, so
      // we render this within the file and render the module end outside the
      // file, but this is backwards from the token locations: the module_begin
      // token is at the include location (outside the file) and the module_end
      // token is at the EOF location (within the file).
      Callbacks->BeginModule(
          reinterpret_cast<Module *>(Tok.getAnnotationValue()));
      PP.Lex(Tok);
      continue;
    } else if (Tok.is(tok::annot_module_end)) {
      Callbacks->EndModule(
          reinterpret_cast<Module *>(Tok.getAnnotationValue()));
      PP.Lex(Tok);
      continue;
    } else if (Tok.isAnnotation()) {
      // Ignore annotation tokens created by pragmas - the pragmas themselves
      // will be reproduced in the preprocessed output.
      PP.Lex(Tok);
      continue;
    } else if (IdentifierInfo *II = Tok.getIdentifierInfo()) {
      OS << II->getName();
    } else if (Tok.isLiteral() && !Tok.needsCleaning() &&
               Tok.getLiteralData()) {
      OS.write(Tok.getLiteralData(), Tok.getLength());
    } else if (Tok.getLength() < llvm::array_lengthof(Buffer)) {
      const char *TokPtr = Buffer;
      unsigned Len = PP.getSpelling(Tok, TokPtr);
      OS.write(TokPtr, Len);

      // Tokens that can contain embedded newlines need to adjust our current
      // line number.
      if (Tok.getKind() == tok::comment || Tok.getKind() == tok::unknown)
        Callbacks->HandleNewlinesInToken(TokPtr, Len);
    } else {
      std::string S = PP.getSpelling(Tok);
      OS.write(&S[0], S.size());

      // Tokens that can contain embedded newlines need to adjust our current
      // line number.
      if (Tok.getKind() == tok::comment || Tok.getKind() == tok::unknown)
        Callbacks->HandleNewlinesInToken(&S[0], S.size());
    }
    Callbacks->setEmittedTokensOnThisLine();

    if (Tok.is(tok::eof)) break;

    PrevPrevTok = PrevTok;
    PrevTok = Tok;
    PP.Lex(Tok);
  }
}

typedef std::pair<const IdentifierInfo *, MacroInfo *> id_macro_pair;
static int MacroIDCompare(const id_macro_pair *LHS, const id_macro_pair *RHS) {
  return LHS->first->getName().compare(RHS->first->getName());
}

static void DoPrintMacros(Preprocessor &PP, raw_ostream *OS) {
  // Ignore unknown pragmas.
  PP.IgnorePragmas();

  // -dM mode just scans and ignores all tokens in the files, then dumps out
  // the macro table at the end.
  PP.EnterMainSourceFile();

  Token Tok;
  do PP.Lex(Tok);
  while (Tok.isNot(tok::eof));

  SmallVector<id_macro_pair, 128> MacrosByID;
  for (Preprocessor::macro_iterator I = PP.macro_begin(), E = PP.macro_end();
       I != E; ++I) {
    auto *MD = I->second.getLatest();
    if (MD && MD->isDefined())
      MacrosByID.push_back(id_macro_pair(I->first, MD->getMacroInfo()));
  }
  llvm::array_pod_sort(MacrosByID.begin(), MacrosByID.end(), MacroIDCompare);

  for (unsigned i = 0, e = MacrosByID.size(); i != e; ++i) {
    MacroInfo &MI = *MacrosByID[i].second;
    // Ignore computed macros like __LINE__ and friends.
    if (MI.isBuiltinMacro()) continue;

    PrintMacroDefinition(*MacrosByID[i].first, MI, PP, *OS);
    *OS << '\n';
  }
}

/// DoPrintPreprocessedInput - This implements -E mode.
///
void clang::DoPrintPreprocessedInput(Preprocessor &PP, raw_ostream *OS,
                                     const PreprocessorOutputOptions &Opts) {
  // Show macros with no output is handled specially.
  if (!Opts.ShowCPP) {
    assert(Opts.ShowMacros && "Not yet implemented!");
    DoPrintMacros(PP, OS);
    return;
  }

  // Inform the preprocessor whether we want it to retain comments or not, due
  // to -C or -CC.
  PP.SetCommentRetentionState(Opts.ShowComments, Opts.ShowMacroComments);

  PrintPPOutputPPCallbacks *Callbacks = new PrintPPOutputPPCallbacks(
      PP, *OS, !Opts.ShowLineMarkers, Opts.ShowMacros,
      Opts.ShowIncludeDirectives, Opts.UseLineDirectives);

  // Expand macros in pragmas with -fms-extensions.  The assumption is that
  // the majority of pragmas in such a file will be Microsoft pragmas.
  // Remember the handlers we will add so that we can remove them later.
  std::unique_ptr<UnknownPragmaHandler> MicrosoftExtHandler(
      new UnknownPragmaHandler(
          "#pragma", Callbacks,
          /*RequireTokenExpansion=*/PP.getLangOpts().MicrosoftExt));

  std::unique_ptr<UnknownPragmaHandler> GCCHandler(new UnknownPragmaHandler(
      "#pragma GCC", Callbacks,
      /*RequireTokenExpansion=*/PP.getLangOpts().MicrosoftExt));

  std::unique_ptr<UnknownPragmaHandler> ClangHandler(new UnknownPragmaHandler(
      "#pragma clang", Callbacks,
      /*RequireTokenExpansion=*/PP.getLangOpts().MicrosoftExt));

  PP.AddPragmaHandler(MicrosoftExtHandler.get());
  PP.AddPragmaHandler("GCC", GCCHandler.get());
  PP.AddPragmaHandler("clang", ClangHandler.get());

  // The tokens after pragma omp need to be expanded.
  //
  //  OpenMP [2.1, Directive format]
  //  Preprocessing tokens following the #pragma omp are subject to macro
  //  replacement.
  std::unique_ptr<UnknownPragmaHandler> OpenMPHandler(
      new UnknownPragmaHandler("#pragma omp", Callbacks,
                               /*RequireTokenExpansion=*/true));
  PP.AddPragmaHandler("omp", OpenMPHandler.get());

  PP.addPPCallbacks(std::unique_ptr<PPCallbacks>(Callbacks));

  // After we have configured the preprocessor, enter the main file.
  PP.EnterMainSourceFile();

  // Consume all of the tokens that come from the predefines buffer.  Those
  // should not be emitted into the output and are guaranteed to be at the
  // start.
  const SourceManager &SourceMgr = PP.getSourceManager();
  Token Tok;
  do {
    PP.Lex(Tok);
    if (Tok.is(tok::eof) || !Tok.getLocation().isFileID())
      break;

    PresumedLoc PLoc = SourceMgr.getPresumedLoc(Tok.getLocation());
    if (PLoc.isInvalid())
      break;

    if (strcmp(PLoc.getFilename(), "<built-in>"))
      break;
  } while (true);

  // Read all the preprocessed tokens, printing them out to the stream.
  PrintPreprocessedTokens(PP, Tok, Callbacks, *OS);
  *OS << '\n';

  // Remove the handlers we just added to leave the preprocessor in a sane state
  // so that it can be reused (for example by a clang::Parser instance).
  PP.RemovePragmaHandler(MicrosoftExtHandler.get());
  PP.RemovePragmaHandler("GCC", GCCHandler.get());
  PP.RemovePragmaHandler("clang", ClangHandler.get());
  PP.RemovePragmaHandler("omp", OpenMPHandler.get());
}
