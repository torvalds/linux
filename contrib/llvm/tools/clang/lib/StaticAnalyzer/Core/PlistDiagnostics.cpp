//===--- PlistDiagnostics.cpp - Plist Diagnostics for Paths -----*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
//  This file defines the PlistDiagnostics object.
//
//===----------------------------------------------------------------------===//

#include "clang/Basic/FileManager.h"
#include "clang/Basic/PlistSupport.h"
#include "clang/Basic/SourceManager.h"
#include "clang/Basic/Version.h"
#include "clang/Lex/Preprocessor.h"
#include "clang/Lex/TokenConcatenation.h"
#include "clang/Rewrite/Core/HTMLRewrite.h"
#include "clang/StaticAnalyzer/Core/AnalyzerOptions.h"
#include "clang/StaticAnalyzer/Core/BugReporter/PathDiagnostic.h"
#include "clang/StaticAnalyzer/Core/IssueHash.h"
#include "clang/StaticAnalyzer/Core/PathDiagnosticConsumers.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/Support/Casting.h"

using namespace clang;
using namespace ento;
using namespace markup;

//===----------------------------------------------------------------------===//
// Declarations of helper classes and functions for emitting bug reports in
// plist format.
//===----------------------------------------------------------------------===//

namespace {
  class PlistDiagnostics : public PathDiagnosticConsumer {
    const std::string OutputFile;
    const Preprocessor &PP;
    AnalyzerOptions &AnOpts;
    const bool SupportsCrossFileDiagnostics;
  public:
    PlistDiagnostics(AnalyzerOptions &AnalyzerOpts,
                     const std::string& prefix,
                     const Preprocessor &PP,
                     bool supportsMultipleFiles);

    ~PlistDiagnostics() override {}

    void FlushDiagnosticsImpl(std::vector<const PathDiagnostic *> &Diags,
                              FilesMade *filesMade) override;

    StringRef getName() const override {
      return "PlistDiagnostics";
    }

    PathGenerationScheme getGenerationScheme() const override {
      return Extensive;
    }
    bool supportsLogicalOpControlFlow() const override { return true; }
    bool supportsCrossFileDiagnostics() const override {
      return SupportsCrossFileDiagnostics;
    }
  };
} // end anonymous namespace

namespace {

/// A helper class for emitting a single report.
class PlistPrinter {
  const FIDMap& FM;
  AnalyzerOptions &AnOpts;
  const Preprocessor &PP;
  llvm::SmallVector<const PathDiagnosticMacroPiece *, 0> MacroPieces;

public:
  PlistPrinter(const FIDMap& FM, AnalyzerOptions &AnOpts,
               const Preprocessor &PP)
    : FM(FM), AnOpts(AnOpts), PP(PP) {
  }

  void ReportDiag(raw_ostream &o, const PathDiagnosticPiece& P) {
    ReportPiece(o, P, /*indent*/ 4, /*depth*/ 0, /*includeControlFlow*/ true);

    // Don't emit a warning about an unused private field.
    (void)AnOpts;
  }

  /// Print the expansions of the collected macro pieces.
  ///
  /// Each time ReportDiag is called on a PathDiagnosticMacroPiece (or, if one
  /// is found through a call piece, etc), it's subpieces are reported, and the
  /// piece itself is collected. Call this function after the entire bugpath
  /// was reported.
  void ReportMacroExpansions(raw_ostream &o, unsigned indent);

private:
  void ReportPiece(raw_ostream &o, const PathDiagnosticPiece &P,
                   unsigned indent, unsigned depth, bool includeControlFlow,
                   bool isKeyEvent = false) {
    switch (P.getKind()) {
      case PathDiagnosticPiece::ControlFlow:
        if (includeControlFlow)
          ReportControlFlow(o, cast<PathDiagnosticControlFlowPiece>(P), indent);
        break;
      case PathDiagnosticPiece::Call:
        ReportCall(o, cast<PathDiagnosticCallPiece>(P), indent,
                   depth);
        break;
      case PathDiagnosticPiece::Event:
        ReportEvent(o, cast<PathDiagnosticEventPiece>(P), indent, depth,
                    isKeyEvent);
        break;
      case PathDiagnosticPiece::Macro:
        ReportMacroSubPieces(o, cast<PathDiagnosticMacroPiece>(P), indent,
                             depth);
        break;
      case PathDiagnosticPiece::Note:
        ReportNote(o, cast<PathDiagnosticNotePiece>(P), indent);
        break;
    }
  }

  void EmitRanges(raw_ostream &o, const ArrayRef<SourceRange> Ranges,
                  unsigned indent);
  void EmitMessage(raw_ostream &o, StringRef Message, unsigned indent);

  void ReportControlFlow(raw_ostream &o,
                         const PathDiagnosticControlFlowPiece& P,
                         unsigned indent);
  void ReportEvent(raw_ostream &o, const PathDiagnosticEventPiece& P,
                   unsigned indent, unsigned depth, bool isKeyEvent = false);
  void ReportCall(raw_ostream &o, const PathDiagnosticCallPiece &P,
                  unsigned indent, unsigned depth);
  void ReportMacroSubPieces(raw_ostream &o, const PathDiagnosticMacroPiece& P,
                            unsigned indent, unsigned depth);
  void ReportNote(raw_ostream &o, const PathDiagnosticNotePiece& P,
                  unsigned indent);
};

} // end of anonymous namespace

namespace {

struct ExpansionInfo {
  std::string MacroName;
  std::string Expansion;
  ExpansionInfo(std::string N, std::string E)
    : MacroName(std::move(N)), Expansion(std::move(E)) {}
};

} // end of anonymous namespace

static void printBugPath(llvm::raw_ostream &o, const FIDMap& FM,
                         AnalyzerOptions &AnOpts,
                         const Preprocessor &PP,
                         const PathPieces &Path);

/// Print coverage information to output stream {@code o}.
/// May modify the used list of files {@code Fids} by inserting new ones.
static void printCoverage(const PathDiagnostic *D,
                          unsigned InputIndentLevel,
                          SmallVectorImpl<FileID> &Fids,
                          FIDMap &FM,
                          llvm::raw_fd_ostream &o);

static ExpansionInfo getExpandedMacro(SourceLocation MacroLoc,
                                      const Preprocessor &PP);

//===----------------------------------------------------------------------===//
// Methods of PlistPrinter.
//===----------------------------------------------------------------------===//

void PlistPrinter::EmitRanges(raw_ostream &o,
                              const ArrayRef<SourceRange> Ranges,
                              unsigned indent) {

  if (Ranges.empty())
    return;

  Indent(o, indent) << "<key>ranges</key>\n";
  Indent(o, indent) << "<array>\n";
  ++indent;

  const SourceManager &SM = PP.getSourceManager();
  const LangOptions &LangOpts = PP.getLangOpts();

  for (auto &R : Ranges)
    EmitRange(o, SM,
              Lexer::getAsCharRange(SM.getExpansionRange(R), SM, LangOpts),
              FM, indent + 1);
  --indent;
  Indent(o, indent) << "</array>\n";
}

void PlistPrinter::EmitMessage(raw_ostream &o, StringRef Message,
                               unsigned indent) {
  // Output the text.
  assert(!Message.empty());
  Indent(o, indent) << "<key>extended_message</key>\n";
  Indent(o, indent);
  EmitString(o, Message) << '\n';

  // Output the short text.
  // FIXME: Really use a short string.
  Indent(o, indent) << "<key>message</key>\n";
  Indent(o, indent);
  EmitString(o, Message) << '\n';
}

void PlistPrinter::ReportControlFlow(raw_ostream &o,
                                     const PathDiagnosticControlFlowPiece& P,
                                     unsigned indent) {

  const SourceManager &SM = PP.getSourceManager();
  const LangOptions &LangOpts = PP.getLangOpts();

  Indent(o, indent) << "<dict>\n";
  ++indent;

  Indent(o, indent) << "<key>kind</key><string>control</string>\n";

  // Emit edges.
  Indent(o, indent) << "<key>edges</key>\n";
  ++indent;
  Indent(o, indent) << "<array>\n";
  ++indent;
  for (PathDiagnosticControlFlowPiece::const_iterator I=P.begin(), E=P.end();
       I!=E; ++I) {
    Indent(o, indent) << "<dict>\n";
    ++indent;

    // Make the ranges of the start and end point self-consistent with adjacent edges
    // by forcing to use only the beginning of the range.  This simplifies the layout
    // logic for clients.
    Indent(o, indent) << "<key>start</key>\n";
    SourceRange StartEdge(
        SM.getExpansionLoc(I->getStart().asRange().getBegin()));
    EmitRange(o, SM, Lexer::getAsCharRange(StartEdge, SM, LangOpts), FM,
              indent + 1);

    Indent(o, indent) << "<key>end</key>\n";
    SourceRange EndEdge(SM.getExpansionLoc(I->getEnd().asRange().getBegin()));
    EmitRange(o, SM, Lexer::getAsCharRange(EndEdge, SM, LangOpts), FM,
              indent + 1);

    --indent;
    Indent(o, indent) << "</dict>\n";
  }
  --indent;
  Indent(o, indent) << "</array>\n";
  --indent;

  // Output any helper text.
  const auto &s = P.getString();
  if (!s.empty()) {
    Indent(o, indent) << "<key>alternate</key>";
    EmitString(o, s) << '\n';
  }

  --indent;
  Indent(o, indent) << "</dict>\n";
}

void PlistPrinter::ReportEvent(raw_ostream &o, const PathDiagnosticEventPiece& P,
                               unsigned indent, unsigned depth,
                               bool isKeyEvent) {

  const SourceManager &SM = PP.getSourceManager();

  Indent(o, indent) << "<dict>\n";
  ++indent;

  Indent(o, indent) << "<key>kind</key><string>event</string>\n";

  if (isKeyEvent) {
    Indent(o, indent) << "<key>key_event</key><true/>\n";
  }

  // Output the location.
  FullSourceLoc L = P.getLocation().asLocation();

  Indent(o, indent) << "<key>location</key>\n";
  EmitLocation(o, SM, L, FM, indent);

  // Output the ranges (if any).
  ArrayRef<SourceRange> Ranges = P.getRanges();
  EmitRanges(o, Ranges, indent);

  // Output the call depth.
  Indent(o, indent) << "<key>depth</key>";
  EmitInteger(o, depth) << '\n';

  // Output the text.
  EmitMessage(o, P.getString(), indent);

  // Finish up.
  --indent;
  Indent(o, indent); o << "</dict>\n";
}

void PlistPrinter::ReportCall(raw_ostream &o, const PathDiagnosticCallPiece &P,
                              unsigned indent,
                              unsigned depth) {

  if (auto callEnter = P.getCallEnterEvent())
    ReportPiece(o, *callEnter, indent, depth, /*includeControlFlow*/ true,
                P.isLastInMainSourceFile());


  ++depth;

  if (auto callEnterWithinCaller = P.getCallEnterWithinCallerEvent())
    ReportPiece(o, *callEnterWithinCaller, indent, depth,
                /*includeControlFlow*/ true);

  for (PathPieces::const_iterator I = P.path.begin(), E = P.path.end();I!=E;++I)
    ReportPiece(o, **I, indent, depth, /*includeControlFlow*/ true);

  --depth;

  if (auto callExit = P.getCallExitEvent())
    ReportPiece(o, *callExit, indent, depth, /*includeControlFlow*/ true);
}

void PlistPrinter::ReportMacroSubPieces(raw_ostream &o,
                                        const PathDiagnosticMacroPiece& P,
                                        unsigned indent, unsigned depth) {
  MacroPieces.push_back(&P);

  for (PathPieces::const_iterator I = P.subPieces.begin(),
                                  E = P.subPieces.end();
       I != E; ++I) {
    ReportPiece(o, **I, indent, depth, /*includeControlFlow*/ false);
  }
}

void PlistPrinter::ReportMacroExpansions(raw_ostream &o, unsigned indent) {

  for (const PathDiagnosticMacroPiece *P : MacroPieces) {
    const SourceManager &SM = PP.getSourceManager();
    ExpansionInfo EI = getExpandedMacro(P->getLocation().asLocation(), PP);

    Indent(o, indent) << "<dict>\n";
    ++indent;

    // Output the location.
    FullSourceLoc L = P->getLocation().asLocation();

    Indent(o, indent) << "<key>location</key>\n";
    EmitLocation(o, SM, L, FM, indent);

    // Output the ranges (if any).
    ArrayRef<SourceRange> Ranges = P->getRanges();
    EmitRanges(o, Ranges, indent);

    // Output the macro name.
    Indent(o, indent) << "<key>name</key>";
    EmitString(o, EI.MacroName) << '\n';

    // Output what it expands into.
    Indent(o, indent) << "<key>expansion</key>";
    EmitString(o, EI.Expansion) << '\n';

    // Finish up.
    --indent;
    Indent(o, indent);
    o << "</dict>\n";
  }
}

void PlistPrinter::ReportNote(raw_ostream &o, const PathDiagnosticNotePiece& P,
                              unsigned indent) {

  const SourceManager &SM = PP.getSourceManager();

  Indent(o, indent) << "<dict>\n";
  ++indent;

  // Output the location.
  FullSourceLoc L = P.getLocation().asLocation();

  Indent(o, indent) << "<key>location</key>\n";
  EmitLocation(o, SM, L, FM, indent);

  // Output the ranges (if any).
  ArrayRef<SourceRange> Ranges = P.getRanges();
  EmitRanges(o, Ranges, indent);

  // Output the text.
  EmitMessage(o, P.getString(), indent);

  // Finish up.
  --indent;
  Indent(o, indent); o << "</dict>\n";
}

//===----------------------------------------------------------------------===//
// Static function definitions.
//===----------------------------------------------------------------------===//

/// Print coverage information to output stream {@code o}.
/// May modify the used list of files {@code Fids} by inserting new ones.
static void printCoverage(const PathDiagnostic *D,
                          unsigned InputIndentLevel,
                          SmallVectorImpl<FileID> &Fids,
                          FIDMap &FM,
                          llvm::raw_fd_ostream &o) {
  unsigned IndentLevel = InputIndentLevel;

  Indent(o, IndentLevel) << "<key>ExecutedLines</key>\n";
  Indent(o, IndentLevel) << "<dict>\n";
  IndentLevel++;

  // Mapping from file IDs to executed lines.
  const FilesToLineNumsMap &ExecutedLines = D->getExecutedLines();
  for (auto I = ExecutedLines.begin(), E = ExecutedLines.end(); I != E; ++I) {
    unsigned FileKey = AddFID(FM, Fids, I->first);
    Indent(o, IndentLevel) << "<key>" << FileKey << "</key>\n";
    Indent(o, IndentLevel) << "<array>\n";
    IndentLevel++;
    for (unsigned LineNo : I->second) {
      Indent(o, IndentLevel);
      EmitInteger(o, LineNo) << "\n";
    }
    IndentLevel--;
    Indent(o, IndentLevel) << "</array>\n";
  }
  IndentLevel--;
  Indent(o, IndentLevel) << "</dict>\n";

  assert(IndentLevel == InputIndentLevel);
}

static void printBugPath(llvm::raw_ostream &o, const FIDMap& FM,
                         AnalyzerOptions &AnOpts,
                         const Preprocessor &PP,
                         const PathPieces &Path) {
  PlistPrinter Printer(FM, AnOpts, PP);
  assert(std::is_partitioned(
           Path.begin(), Path.end(),
           [](const std::shared_ptr<PathDiagnosticPiece> &E)
             { return E->getKind() == PathDiagnosticPiece::Note; }) &&
         "PathDiagnostic is not partitioned so that notes precede the rest");

  PathPieces::const_iterator FirstNonNote = std::partition_point(
      Path.begin(), Path.end(),
      [](const std::shared_ptr<PathDiagnosticPiece> &E)
        { return E->getKind() == PathDiagnosticPiece::Note; });

  PathPieces::const_iterator I = Path.begin();

  if (FirstNonNote != Path.begin()) {
    o << "   <key>notes</key>\n"
         "   <array>\n";

    for (; I != FirstNonNote; ++I)
      Printer.ReportDiag(o, **I);

    o << "   </array>\n";
  }

  o << "   <key>path</key>\n";

  o << "   <array>\n";

  for (PathPieces::const_iterator E = Path.end(); I != E; ++I)
    Printer.ReportDiag(o, **I);

  o << "   </array>\n";

  if (!AnOpts.ShouldDisplayMacroExpansions)
    return;

  o << "   <key>macro_expansions</key>\n"
       "   <array>\n";
  Printer.ReportMacroExpansions(o, /* indent */ 4);
  o << "   </array>\n";
}

//===----------------------------------------------------------------------===//
// Methods of PlistDiagnostics.
//===----------------------------------------------------------------------===//

PlistDiagnostics::PlistDiagnostics(AnalyzerOptions &AnalyzerOpts,
                                   const std::string& output,
                                   const Preprocessor &PP,
                                   bool supportsMultipleFiles)
  : OutputFile(output), PP(PP), AnOpts(AnalyzerOpts),
    SupportsCrossFileDiagnostics(supportsMultipleFiles) {}

void ento::createPlistDiagnosticConsumer(AnalyzerOptions &AnalyzerOpts,
                                         PathDiagnosticConsumers &C,
                                         const std::string& s,
                                         const Preprocessor &PP) {
  C.push_back(new PlistDiagnostics(AnalyzerOpts, s, PP,
                                   /*supportsMultipleFiles*/ false));
}

void ento::createPlistMultiFileDiagnosticConsumer(AnalyzerOptions &AnalyzerOpts,
                                                  PathDiagnosticConsumers &C,
                                                  const std::string &s,
                                                  const Preprocessor &PP) {
  C.push_back(new PlistDiagnostics(AnalyzerOpts, s, PP,
                                   /*supportsMultipleFiles*/ true));
}
void PlistDiagnostics::FlushDiagnosticsImpl(
                                    std::vector<const PathDiagnostic *> &Diags,
                                    FilesMade *filesMade) {
  // Build up a set of FIDs that we use by scanning the locations and
  // ranges of the diagnostics.
  FIDMap FM;
  SmallVector<FileID, 10> Fids;
  const SourceManager& SM = PP.getSourceManager();
  const LangOptions &LangOpts = PP.getLangOpts();

  auto AddPieceFID = [&FM, &Fids, &SM](const PathDiagnosticPiece &Piece) {
    AddFID(FM, Fids, SM, Piece.getLocation().asLocation());
    ArrayRef<SourceRange> Ranges = Piece.getRanges();
    for (const SourceRange &Range : Ranges) {
      AddFID(FM, Fids, SM, Range.getBegin());
      AddFID(FM, Fids, SM, Range.getEnd());
    }
  };

  for (const PathDiagnostic *D : Diags) {

    SmallVector<const PathPieces *, 5> WorkList;
    WorkList.push_back(&D->path);

    while (!WorkList.empty()) {
      const PathPieces &Path = *WorkList.pop_back_val();

      for (const auto &Iter : Path) {
        const PathDiagnosticPiece &Piece = *Iter;
        AddPieceFID(Piece);

        if (const PathDiagnosticCallPiece *Call =
                dyn_cast<PathDiagnosticCallPiece>(&Piece)) {
          if (auto CallEnterWithin = Call->getCallEnterWithinCallerEvent())
            AddPieceFID(*CallEnterWithin);

          if (auto CallEnterEvent = Call->getCallEnterEvent())
            AddPieceFID(*CallEnterEvent);

          WorkList.push_back(&Call->path);
        } else if (const PathDiagnosticMacroPiece *Macro =
                       dyn_cast<PathDiagnosticMacroPiece>(&Piece)) {
          WorkList.push_back(&Macro->subPieces);
        }
      }
    }
  }

  // Open the file.
  std::error_code EC;
  llvm::raw_fd_ostream o(OutputFile, EC, llvm::sys::fs::F_Text);
  if (EC) {
    llvm::errs() << "warning: could not create file: " << EC.message() << '\n';
    return;
  }

  EmitPlistHeader(o);

  // Write the root object: a <dict> containing...
  //  - "clang_version", the string representation of clang version
  //  - "files", an <array> mapping from FIDs to file names
  //  - "diagnostics", an <array> containing the path diagnostics
  o << "<dict>\n" <<
       " <key>clang_version</key>\n";
  EmitString(o, getClangFullVersion()) << '\n';
  o << " <key>diagnostics</key>\n"
       " <array>\n";

  for (std::vector<const PathDiagnostic*>::iterator DI=Diags.begin(),
       DE = Diags.end(); DI!=DE; ++DI) {

    o << "  <dict>\n";

    const PathDiagnostic *D = *DI;
    printBugPath(o, FM, AnOpts, PP, D->path);

    // Output the bug type and bug category.
    o << "   <key>description</key>";
    EmitString(o, D->getShortDescription()) << '\n';
    o << "   <key>category</key>";
    EmitString(o, D->getCategory()) << '\n';
    o << "   <key>type</key>";
    EmitString(o, D->getBugType()) << '\n';
    o << "   <key>check_name</key>";
    EmitString(o, D->getCheckName()) << '\n';

    o << "   <!-- This hash is experimental and going to change! -->\n";
    o << "   <key>issue_hash_content_of_line_in_context</key>";
    PathDiagnosticLocation UPDLoc = D->getUniqueingLoc();
    FullSourceLoc L(SM.getExpansionLoc(UPDLoc.isValid()
                                            ? UPDLoc.asLocation()
                                            : D->getLocation().asLocation()),
                    SM);
    const Decl *DeclWithIssue = D->getDeclWithIssue();
    EmitString(o, GetIssueHash(SM, L, D->getCheckName(), D->getBugType(),
                               DeclWithIssue, LangOpts))
        << '\n';

    // Output information about the semantic context where
    // the issue occurred.
    if (const Decl *DeclWithIssue = D->getDeclWithIssue()) {
      // FIXME: handle blocks, which have no name.
      if (const NamedDecl *ND = dyn_cast<NamedDecl>(DeclWithIssue)) {
        StringRef declKind;
        switch (ND->getKind()) {
          case Decl::CXXRecord:
            declKind = "C++ class";
            break;
          case Decl::CXXMethod:
            declKind = "C++ method";
            break;
          case Decl::ObjCMethod:
            declKind = "Objective-C method";
            break;
          case Decl::Function:
            declKind = "function";
            break;
          default:
            break;
        }
        if (!declKind.empty()) {
          const std::string &declName = ND->getDeclName().getAsString();
          o << "  <key>issue_context_kind</key>";
          EmitString(o, declKind) << '\n';
          o << "  <key>issue_context</key>";
          EmitString(o, declName) << '\n';
        }

        // Output the bug hash for issue unique-ing. Currently, it's just an
        // offset from the beginning of the function.
        if (const Stmt *Body = DeclWithIssue->getBody()) {

          // If the bug uniqueing location exists, use it for the hash.
          // For example, this ensures that two leaks reported on the same line
          // will have different issue_hashes and that the hash will identify
          // the leak location even after code is added between the allocation
          // site and the end of scope (leak report location).
          if (UPDLoc.isValid()) {
            FullSourceLoc UFunL(
                SM.getExpansionLoc(
                    D->getUniqueingDecl()->getBody()->getBeginLoc()),
                SM);
            o << "  <key>issue_hash_function_offset</key><string>"
              << L.getExpansionLineNumber() - UFunL.getExpansionLineNumber()
              << "</string>\n";

          // Otherwise, use the location on which the bug is reported.
          } else {
            FullSourceLoc FunL(SM.getExpansionLoc(Body->getBeginLoc()), SM);
            o << "  <key>issue_hash_function_offset</key><string>"
              << L.getExpansionLineNumber() - FunL.getExpansionLineNumber()
              << "</string>\n";
          }

        }
      }
    }

    // Output the location of the bug.
    o << "  <key>location</key>\n";
    EmitLocation(o, SM, D->getLocation().asLocation(), FM, 2);

    // Output the diagnostic to the sub-diagnostic client, if any.
    if (!filesMade->empty()) {
      StringRef lastName;
      PDFileEntry::ConsumerFiles *files = filesMade->getFiles(*D);
      if (files) {
        for (PDFileEntry::ConsumerFiles::const_iterator CI = files->begin(),
                CE = files->end(); CI != CE; ++CI) {
          StringRef newName = CI->first;
          if (newName != lastName) {
            if (!lastName.empty()) {
              o << "  </array>\n";
            }
            lastName = newName;
            o <<  "  <key>" << lastName << "_files</key>\n";
            o << "  <array>\n";
          }
          o << "   <string>" << CI->second << "</string>\n";
        }
        o << "  </array>\n";
      }
    }

    printCoverage(D, /*IndentLevel=*/2, Fids, FM, o);

    // Close up the entry.
    o << "  </dict>\n";
  }

  o << " </array>\n";

  o << " <key>files</key>\n"
       " <array>\n";
  for (FileID FID : Fids)
    EmitString(o << "  ", SM.getFileEntryForID(FID)->getName()) << '\n';
  o << " </array>\n";

  if (llvm::AreStatisticsEnabled() && AnOpts.ShouldSerializeStats) {
    o << " <key>statistics</key>\n";
    std::string stats;
    llvm::raw_string_ostream os(stats);
    llvm::PrintStatisticsJSON(os);
    os.flush();
    EmitString(o, html::EscapeText(stats)) << '\n';
  }

  // Finish.
  o << "</dict>\n</plist>";
}

//===----------------------------------------------------------------------===//
// Declarations of helper functions and data structures for expanding macros.
//===----------------------------------------------------------------------===//

namespace {

using ExpArgTokens = llvm::SmallVector<Token, 2>;

/// Maps unexpanded macro arguments to expanded arguments. A macro argument may
/// need to expanded further when it is nested inside another macro.
class MacroArgMap : public std::map<const IdentifierInfo *, ExpArgTokens> {
public:
  void expandFromPrevMacro(const MacroArgMap &Super);
};

struct MacroNameAndArgs {
  std::string Name;
  const MacroInfo *MI = nullptr;
  MacroArgMap Args;

  MacroNameAndArgs(std::string N, const MacroInfo *MI, MacroArgMap M)
    : Name(std::move(N)), MI(MI), Args(std::move(M)) {}
};

class TokenPrinter {
  llvm::raw_ostream &OS;
  const Preprocessor &PP;

  Token PrevTok, PrevPrevTok;
  TokenConcatenation ConcatInfo;

public:
  TokenPrinter(llvm::raw_ostream &OS, const Preprocessor &PP)
    : OS(OS), PP(PP), ConcatInfo(PP) {
    PrevTok.setKind(tok::unknown);
    PrevPrevTok.setKind(tok::unknown);
  }

  void printToken(const Token &Tok);
};

} // end of anonymous namespace

/// The implementation method of getMacroExpansion: It prints the expansion of
/// a macro to \p Printer, and returns with the name of the macro.
///
/// Since macros can be nested in one another, this function may call itself
/// recursively.
///
/// Unfortunately, macro arguments have to expanded manually. To understand why,
/// observe the following example:
///
///   #define PRINT(x) print(x)
///   #define DO_SOMETHING(str) PRINT(str)
///
///   DO_SOMETHING("Cute panda cubs.");
///
/// As we expand the last line, we'll immediately replace PRINT(str) with
/// print(x). The information that both 'str' and 'x' refers to the same string
/// is an information we have to forward, hence the argument \p PrevArgs.
static std::string getMacroNameAndPrintExpansion(TokenPrinter &Printer,
                                                 SourceLocation MacroLoc,
                                                 const Preprocessor &PP,
                                                 const MacroArgMap &PrevArgs);

/// Retrieves the name of the macro and what it's arguments expand into
/// at \p ExpanLoc.
///
/// For example, for the following macro expansion:
///
///   #define SET_TO_NULL(x) x = 0
///   #define NOT_SUSPICIOUS(a) \
///     {                       \
///       int b = 0;            \
///     }                       \
///     SET_TO_NULL(a)
///
///   int *ptr = new int(4);
///   NOT_SUSPICIOUS(&ptr);
///   *ptr = 5;
///
/// When \p ExpanLoc references the last line, the macro name "NOT_SUSPICIOUS"
/// and the MacroArgMap map { (a, &ptr) } will be returned.
///
/// When \p ExpanLoc references "SET_TO_NULL(a)" within the definition of
/// "NOT_SUSPICOUS", the macro name "SET_TO_NULL" and the MacroArgMap map
/// { (x, a) } will be returned.
static MacroNameAndArgs getMacroNameAndArgs(SourceLocation ExpanLoc,
                                            const Preprocessor &PP);

/// Retrieves the ')' token that matches '(' \p It points to.
static MacroInfo::tokens_iterator getMatchingRParen(
    MacroInfo::tokens_iterator It,
    MacroInfo::tokens_iterator End);

/// Retrieves the macro info for \p II refers to at \p Loc. This is important
/// because macros can be redefined or undefined.
static const MacroInfo *getMacroInfoForLocation(const Preprocessor &PP,
                                                const SourceManager &SM,
                                                const IdentifierInfo *II,
                                                SourceLocation Loc);

//===----------------------------------------------------------------------===//
// Definitions of helper functions and methods for expanding macros.
//===----------------------------------------------------------------------===//

static ExpansionInfo getExpandedMacro(SourceLocation MacroLoc,
                                      const Preprocessor &PP) {

  llvm::SmallString<200> ExpansionBuf;
  llvm::raw_svector_ostream OS(ExpansionBuf);
  TokenPrinter Printer(OS, PP);
  std::string MacroName =
            getMacroNameAndPrintExpansion(Printer, MacroLoc, PP, MacroArgMap{});
  return { MacroName, OS.str() };
}

static std::string getMacroNameAndPrintExpansion(TokenPrinter &Printer,
                                                 SourceLocation MacroLoc,
                                                 const Preprocessor &PP,
                                                 const MacroArgMap &PrevArgs) {

  const SourceManager &SM = PP.getSourceManager();

  MacroNameAndArgs Info = getMacroNameAndArgs(SM.getExpansionLoc(MacroLoc), PP);

  // Manually expand its arguments from the previous macro.
  Info.Args.expandFromPrevMacro(PrevArgs);

  // Iterate over the macro's tokens and stringify them.
  for (auto It = Info.MI->tokens_begin(), E = Info.MI->tokens_end(); It != E;
       ++It) {
    Token T = *It;

    // If this token is not an identifier, we only need to print it.
    if (T.isNot(tok::identifier)) {
      Printer.printToken(T);
      continue;
    }

    const auto *II = T.getIdentifierInfo();
    assert(II &&
          "This token is an identifier but has no IdentifierInfo!");

    // If this token is a macro that should be expanded inside the current
    // macro.
    if (const MacroInfo *MI =
                         getMacroInfoForLocation(PP, SM, II, T.getLocation())) {
      getMacroNameAndPrintExpansion(Printer, T.getLocation(), PP, Info.Args);

      // If this is a function-like macro, skip its arguments, as
      // getExpandedMacro() already printed them. If this is the case, let's
      // first jump to the '(' token.
      if (MI->getNumParams() != 0)
        It = getMatchingRParen(++It, E);
      continue;
    }

    // If this token is the current macro's argument, we should expand it.
    auto ArgMapIt = Info.Args.find(II);
    if (ArgMapIt != Info.Args.end()) {
      for (MacroInfo::tokens_iterator ArgIt = ArgMapIt->second.begin(),
                                      ArgEnd = ArgMapIt->second.end();
           ArgIt != ArgEnd; ++ArgIt) {

        // These tokens may still be macros, if that is the case, handle it the
        // same way we did above.
        const auto *ArgII = ArgIt->getIdentifierInfo();
        if (!ArgII) {
          Printer.printToken(*ArgIt);
          continue;
        }

        const auto *MI = PP.getMacroInfo(ArgII);
        if (!MI) {
          Printer.printToken(*ArgIt);
          continue;
        }

        getMacroNameAndPrintExpansion(Printer, ArgIt->getLocation(), PP,
                                      Info.Args);
        if (MI->getNumParams() != 0)
          ArgIt = getMatchingRParen(++ArgIt, ArgEnd);
      }
      continue;
    }

    // If control reached here, then this token isn't a macro identifier, nor an
    // unexpanded macro argument that we need to handle, print it.
    Printer.printToken(T);
  }

  return Info.Name;
}

static MacroNameAndArgs getMacroNameAndArgs(SourceLocation ExpanLoc,
                                            const Preprocessor &PP) {

  const SourceManager &SM = PP.getSourceManager();
  const LangOptions &LangOpts = PP.getLangOpts();

  // First, we create a Lexer to lex *at the expansion location* the tokens
  // referring to the macro's name and its arguments.
  std::pair<FileID, unsigned> LocInfo = SM.getDecomposedLoc(ExpanLoc);
  const llvm::MemoryBuffer *MB = SM.getBuffer(LocInfo.first);
  const char *MacroNameTokenPos = MB->getBufferStart() + LocInfo.second;

  Lexer RawLexer(SM.getLocForStartOfFile(LocInfo.first), LangOpts,
                 MB->getBufferStart(), MacroNameTokenPos, MB->getBufferEnd());

  // Acquire the macro's name.
  Token TheTok;
  RawLexer.LexFromRawLexer(TheTok);

  std::string MacroName = PP.getSpelling(TheTok);

  const auto *II = PP.getIdentifierInfo(MacroName);
  assert(II && "Failed to acquire the IndetifierInfo for the macro!");

  const MacroInfo *MI = getMacroInfoForLocation(PP, SM, II, ExpanLoc);
  assert(MI && "The macro must've been defined at it's expansion location!");

  // Acquire the macro's arguments.
  //
  // The rough idea here is to lex from the first left parentheses to the last
  // right parentheses, and map the macro's unexpanded arguments to what they
  // will be expanded to. An expanded macro argument may contain several tokens
  // (like '3 + 4'), so we'll lex until we find a tok::comma or tok::r_paren, at
  // which point we start lexing the next argument or finish.
  ArrayRef<const IdentifierInfo *> MacroArgs = MI->params();
  if (MacroArgs.empty())
    return { MacroName, MI, {} };

  RawLexer.LexFromRawLexer(TheTok);
  assert(TheTok.is(tok::l_paren) &&
         "The token after the macro's identifier token should be '('!");

  MacroArgMap Args;

  // When the macro's argument is a function call, like
  //   CALL_FN(someFunctionName(param1, param2))
  // we will find tok::l_paren, tok::r_paren, and tok::comma that do not divide
  // actual macro arguments, or do not represent the macro argument's closing
  // parentheses, so we'll count how many parentheses aren't closed yet.
  // If ParanthesesDepth
  //   * = 0, then there are no more arguments to lex.
  //   * = 1, then if we find a tok::comma, we can start lexing the next arg.
  //   * > 1, then tok::comma is a part of the current arg.
  int ParenthesesDepth = 1;

  // If we encounter __VA_ARGS__, we will lex until the closing tok::r_paren,
  // even if we lex a tok::comma and ParanthesesDepth == 1.
  const IdentifierInfo *__VA_ARGS__II = PP.getIdentifierInfo("__VA_ARGS__");

  for (const IdentifierInfo *UnexpArgII : MacroArgs) {
    MacroArgMap::mapped_type ExpandedArgTokens;

    // One could also simply not supply a single argument to __VA_ARGS__ -- this
    // results in a preprocessor warning, but is not an error:
    //   #define VARIADIC(ptr, ...) \
    //     someVariadicTemplateFunction(__VA_ARGS__)
    //
    //   int *ptr;
    //   VARIADIC(ptr); // Note that there are no commas, this isn't just an
    //                  // empty parameter -- there are no parameters for '...'.
    // In any other case, ParenthesesDepth mustn't be 0 here.
    if (ParenthesesDepth != 0) {

      // Lex the first token of the next macro parameter.
      RawLexer.LexFromRawLexer(TheTok);

      while (!(ParenthesesDepth == 1 &&
              (UnexpArgII == __VA_ARGS__II ? false : TheTok.is(tok::comma)))) {
        assert(TheTok.isNot(tok::eof) &&
               "EOF encountered while looking for expanded macro args!");

        if (TheTok.is(tok::l_paren))
          ++ParenthesesDepth;

        if (TheTok.is(tok::r_paren))
          --ParenthesesDepth;

        if (ParenthesesDepth == 0)
          break;

        if (TheTok.is(tok::raw_identifier))
          PP.LookUpIdentifierInfo(TheTok);

        ExpandedArgTokens.push_back(TheTok);
        RawLexer.LexFromRawLexer(TheTok);
      }
    } else {
      assert(UnexpArgII == __VA_ARGS__II);
    }

    Args.emplace(UnexpArgII, std::move(ExpandedArgTokens));
  }

  assert(TheTok.is(tok::r_paren) &&
         "Expanded macro argument acquisition failed! After the end of the loop"
         " this token should be ')'!");

  return { MacroName, MI, Args };
}

static MacroInfo::tokens_iterator getMatchingRParen(
    MacroInfo::tokens_iterator It,
    MacroInfo::tokens_iterator End) {

  assert(It->is(tok::l_paren) && "This token should be '('!");

  // Skip until we find the closing ')'.
  int ParenthesesDepth = 1;
  while (ParenthesesDepth != 0) {
    ++It;

    assert(It->isNot(tok::eof) &&
           "Encountered EOF while attempting to skip macro arguments!");
    assert(It != End &&
           "End of the macro definition reached before finding ')'!");

    if (It->is(tok::l_paren))
      ++ParenthesesDepth;

    if (It->is(tok::r_paren))
      --ParenthesesDepth;
  }
  return It;
}

static const MacroInfo *getMacroInfoForLocation(const Preprocessor &PP,
                                                const SourceManager &SM,
                                                const IdentifierInfo *II,
                                                SourceLocation Loc) {

  const MacroDirective *MD = PP.getLocalMacroDirectiveHistory(II);
  if (!MD)
    return nullptr;

  return MD->findDirectiveAtLoc(Loc, SM).getMacroInfo();
}

void MacroArgMap::expandFromPrevMacro(const MacroArgMap &Super) {

  for (value_type &Pair : *this) {
    ExpArgTokens &CurrExpArgTokens = Pair.second;

    // For each token in the expanded macro argument.
    auto It = CurrExpArgTokens.begin();
    while (It != CurrExpArgTokens.end()) {
      if (It->isNot(tok::identifier)) {
        ++It;
        continue;
      }

      const auto *II = It->getIdentifierInfo();
      assert(II);

      // Is this an argument that "Super" expands further?
      if (!Super.count(II)) {
        ++It;
        continue;
      }

      const ExpArgTokens &SuperExpArgTokens = Super.at(II);

      It = CurrExpArgTokens.insert(
          It, SuperExpArgTokens.begin(), SuperExpArgTokens.end());
      std::advance(It, SuperExpArgTokens.size());
      It = CurrExpArgTokens.erase(It);
    }
  }
}

void TokenPrinter::printToken(const Token &Tok) {
  // If this is the first token to be printed, don't print space.
  if (PrevTok.isNot(tok::unknown)) {
    // If the tokens were already space separated, or if they must be to avoid
    // them being implicitly pasted, add a space between them.
    if(Tok.hasLeadingSpace() || ConcatInfo.AvoidConcat(PrevPrevTok, PrevTok,
                                                       Tok)) {
      // AvoidConcat doesn't check for ##, don't print a space around it.
      if (PrevTok.isNot(tok::hashhash) && Tok.isNot(tok::hashhash)) {
        OS << ' ';
      }
    }
  }

  if (!Tok.isOneOf(tok::hash, tok::hashhash)) {
    if (PrevTok.is(tok::hash))
      OS << '\"' << PP.getSpelling(Tok) << '\"';
    else
      OS << PP.getSpelling(Tok);
  }

  PrevPrevTok = PrevTok;
  PrevTok = Tok;
}
