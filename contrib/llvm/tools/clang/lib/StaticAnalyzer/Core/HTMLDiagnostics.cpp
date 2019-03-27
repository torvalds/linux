//===- HTMLDiagnostics.cpp - HTML Diagnostics for Paths -------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
//  This file defines the HTMLDiagnostics object.
//
//===----------------------------------------------------------------------===//

#include "clang/AST/Decl.h"
#include "clang/AST/DeclBase.h"
#include "clang/AST/Stmt.h"
#include "clang/Basic/FileManager.h"
#include "clang/Basic/LLVM.h"
#include "clang/Basic/SourceLocation.h"
#include "clang/Basic/SourceManager.h"
#include "clang/Lex/Lexer.h"
#include "clang/Lex/Preprocessor.h"
#include "clang/Lex/Token.h"
#include "clang/Rewrite/Core/HTMLRewrite.h"
#include "clang/Rewrite/Core/Rewriter.h"
#include "clang/StaticAnalyzer/Core/AnalyzerOptions.h"
#include "clang/StaticAnalyzer/Core/BugReporter/PathDiagnostic.h"
#include "clang/StaticAnalyzer/Core/IssueHash.h"
#include "clang/StaticAnalyzer/Core/PathDiagnosticConsumers.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/iterator_range.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/Errc.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/Path.h"
#include "llvm/Support/raw_ostream.h"
#include <algorithm>
#include <cassert>
#include <map>
#include <memory>
#include <set>
#include <sstream>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

using namespace clang;
using namespace ento;

//===----------------------------------------------------------------------===//
// Boilerplate.
//===----------------------------------------------------------------------===//

namespace {

class HTMLDiagnostics : public PathDiagnosticConsumer {
  std::string Directory;
  bool createdDir = false;
  bool noDir = false;
  const Preprocessor &PP;
  AnalyzerOptions &AnalyzerOpts;
  const bool SupportsCrossFileDiagnostics;

public:
  HTMLDiagnostics(AnalyzerOptions &AnalyzerOpts,
                  const std::string& prefix,
                  const Preprocessor &pp,
                  bool supportsMultipleFiles)
      : Directory(prefix), PP(pp), AnalyzerOpts(AnalyzerOpts),
        SupportsCrossFileDiagnostics(supportsMultipleFiles) {}

  ~HTMLDiagnostics() override { FlushDiagnostics(nullptr); }

  void FlushDiagnosticsImpl(std::vector<const PathDiagnostic *> &Diags,
                            FilesMade *filesMade) override;

  StringRef getName() const override {
    return "HTMLDiagnostics";
  }

  bool supportsCrossFileDiagnostics() const override {
    return SupportsCrossFileDiagnostics;
  }

  unsigned ProcessMacroPiece(raw_ostream &os,
                             const PathDiagnosticMacroPiece& P,
                             unsigned num);

  void HandlePiece(Rewriter& R, FileID BugFileID,
                   const PathDiagnosticPiece& P, unsigned num, unsigned max);

  void HighlightRange(Rewriter& R, FileID BugFileID, SourceRange Range,
                      const char *HighlightStart = "<span class=\"mrange\">",
                      const char *HighlightEnd = "</span>");

  void ReportDiag(const PathDiagnostic& D,
                  FilesMade *filesMade);

  // Generate the full HTML report
  std::string GenerateHTML(const PathDiagnostic& D, Rewriter &R,
                           const SourceManager& SMgr, const PathPieces& path,
                           const char *declName);

  // Add HTML header/footers to file specified by FID
  void FinalizeHTML(const PathDiagnostic& D, Rewriter &R,
                    const SourceManager& SMgr, const PathPieces& path,
                    FileID FID, const FileEntry *Entry, const char *declName);

  // Rewrite the file specified by FID with HTML formatting.
  void RewriteFile(Rewriter &R, const PathPieces& path, FileID FID);


private:
  /// \return Javascript for displaying shortcuts help;
  StringRef showHelpJavascript();

  /// \return Javascript for navigating the HTML report using j/k keys.
  StringRef generateKeyboardNavigationJavascript();

  /// \return JavaScript for an option to only show relevant lines.
  std::string showRelevantLinesJavascript(
    const PathDiagnostic &D, const PathPieces &path);

  /// Write executed lines from \p D in JSON format into \p os.
  void dumpCoverageData(const PathDiagnostic &D,
                        const PathPieces &path,
                        llvm::raw_string_ostream &os);
};

} // namespace

void ento::createHTMLDiagnosticConsumer(AnalyzerOptions &AnalyzerOpts,
                                        PathDiagnosticConsumers &C,
                                        const std::string& prefix,
                                        const Preprocessor &PP) {
  C.push_back(new HTMLDiagnostics(AnalyzerOpts, prefix, PP, true));
}

void ento::createHTMLSingleFileDiagnosticConsumer(AnalyzerOptions &AnalyzerOpts,
                                                  PathDiagnosticConsumers &C,
                                                  const std::string& prefix,
                                                  const Preprocessor &PP) {
  C.push_back(new HTMLDiagnostics(AnalyzerOpts, prefix, PP, false));
}

//===----------------------------------------------------------------------===//
// Report processing.
//===----------------------------------------------------------------------===//

void HTMLDiagnostics::FlushDiagnosticsImpl(
  std::vector<const PathDiagnostic *> &Diags,
  FilesMade *filesMade) {
  for (const auto Diag : Diags)
    ReportDiag(*Diag, filesMade);
}

void HTMLDiagnostics::ReportDiag(const PathDiagnostic& D,
                                 FilesMade *filesMade) {
  // Create the HTML directory if it is missing.
  if (!createdDir) {
    createdDir = true;
    if (std::error_code ec = llvm::sys::fs::create_directories(Directory)) {
      llvm::errs() << "warning: could not create directory '"
                   << Directory << "': " << ec.message() << '\n';
      noDir = true;
      return;
    }
  }

  if (noDir)
    return;

  // First flatten out the entire path to make it easier to use.
  PathPieces path = D.path.flatten(/*ShouldFlattenMacros=*/false);

  // The path as already been prechecked that the path is non-empty.
  assert(!path.empty());
  const SourceManager &SMgr = path.front()->getLocation().getManager();

  // Create a new rewriter to generate HTML.
  Rewriter R(const_cast<SourceManager&>(SMgr), PP.getLangOpts());

  // The file for the first path element is considered the main report file, it
  // will usually be equivalent to SMgr.getMainFileID(); however, it might be a
  // header when -analyzer-opt-analyze-headers is used.
  FileID ReportFile = path.front()->getLocation().asLocation().getExpansionLoc().getFileID();

  // Get the function/method name
  SmallString<128> declName("unknown");
  int offsetDecl = 0;
  if (const Decl *DeclWithIssue = D.getDeclWithIssue()) {
      if (const auto *ND = dyn_cast<NamedDecl>(DeclWithIssue))
          declName = ND->getDeclName().getAsString();

      if (const Stmt *Body = DeclWithIssue->getBody()) {
          // Retrieve the relative position of the declaration which will be used
          // for the file name
          FullSourceLoc L(
              SMgr.getExpansionLoc(path.back()->getLocation().asLocation()),
              SMgr);
          FullSourceLoc FunL(SMgr.getExpansionLoc(Body->getBeginLoc()), SMgr);
          offsetDecl = L.getExpansionLineNumber() - FunL.getExpansionLineNumber();
      }
  }

  std::string report = GenerateHTML(D, R, SMgr, path, declName.c_str());
  if (report.empty()) {
    llvm::errs() << "warning: no diagnostics generated for main file.\n";
    return;
  }

  // Create a path for the target HTML file.
  int FD;
  SmallString<128> Model, ResultPath;

  if (!AnalyzerOpts.ShouldWriteStableReportFilename) {
      llvm::sys::path::append(Model, Directory, "report-%%%%%%.html");
      if (std::error_code EC =
          llvm::sys::fs::make_absolute(Model)) {
          llvm::errs() << "warning: could not make '" << Model
                       << "' absolute: " << EC.message() << '\n';
        return;
      }
      if (std::error_code EC =
          llvm::sys::fs::createUniqueFile(Model, FD, ResultPath)) {
          llvm::errs() << "warning: could not create file in '" << Directory
                       << "': " << EC.message() << '\n';
          return;
      }
  } else {
      int i = 1;
      std::error_code EC;
      do {
          // Find a filename which is not already used
          const FileEntry* Entry = SMgr.getFileEntryForID(ReportFile);
          std::stringstream filename;
          Model = "";
          filename << "report-"
                   << llvm::sys::path::filename(Entry->getName()).str()
                   << "-" << declName.c_str()
                   << "-" << offsetDecl
                   << "-" << i << ".html";
          llvm::sys::path::append(Model, Directory,
                                  filename.str());
          EC = llvm::sys::fs::openFileForReadWrite(
              Model, FD, llvm::sys::fs::CD_CreateNew, llvm::sys::fs::OF_None);
          if (EC && EC != llvm::errc::file_exists) {
              llvm::errs() << "warning: could not create file '" << Model
                           << "': " << EC.message() << '\n';
              return;
          }
          i++;
      } while (EC);
  }

  llvm::raw_fd_ostream os(FD, true);

  if (filesMade)
    filesMade->addDiagnostic(D, getName(),
                             llvm::sys::path::filename(ResultPath));

  // Emit the HTML to disk.
  os << report;
}

std::string HTMLDiagnostics::GenerateHTML(const PathDiagnostic& D, Rewriter &R,
    const SourceManager& SMgr, const PathPieces& path, const char *declName) {
  // Rewrite source files as HTML for every new file the path crosses
  std::vector<FileID> FileIDs;
  for (auto I : path) {
    FileID FID = I->getLocation().asLocation().getExpansionLoc().getFileID();
    if (std::find(FileIDs.begin(), FileIDs.end(), FID) != FileIDs.end())
      continue;

    FileIDs.push_back(FID);
    RewriteFile(R, path, FID);
  }

  if (SupportsCrossFileDiagnostics && FileIDs.size() > 1) {
    // Prefix file names, anchor tags, and nav cursors to every file
    for (auto I = FileIDs.begin(), E = FileIDs.end(); I != E; I++) {
      std::string s;
      llvm::raw_string_ostream os(s);

      if (I != FileIDs.begin())
        os << "<hr class=divider>\n";

      os << "<div id=File" << I->getHashValue() << ">\n";

      // Left nav arrow
      if (I != FileIDs.begin())
        os << "<div class=FileNav><a href=\"#File" << (I - 1)->getHashValue()
           << "\">&#x2190;</a></div>";

      os << "<h4 class=FileName>" << SMgr.getFileEntryForID(*I)->getName()
         << "</h4>\n";

      // Right nav arrow
      if (I + 1 != E)
        os << "<div class=FileNav><a href=\"#File" << (I + 1)->getHashValue()
           << "\">&#x2192;</a></div>";

      os << "</div>\n";

      R.InsertTextBefore(SMgr.getLocForStartOfFile(*I), os.str());
    }

    // Append files to the main report file in the order they appear in the path
    for (auto I : llvm::make_range(FileIDs.begin() + 1, FileIDs.end())) {
      std::string s;
      llvm::raw_string_ostream os(s);

      const RewriteBuffer *Buf = R.getRewriteBufferFor(I);
      for (auto BI : *Buf)
        os << BI;

      R.InsertTextAfter(SMgr.getLocForEndOfFile(FileIDs[0]), os.str());
    }
  }

  const RewriteBuffer *Buf = R.getRewriteBufferFor(FileIDs[0]);
  if (!Buf)
    return {};

  // Add CSS, header, and footer.
  FileID FID =
      path.back()->getLocation().asLocation().getExpansionLoc().getFileID();
  const FileEntry* Entry = SMgr.getFileEntryForID(FID);
  FinalizeHTML(D, R, SMgr, path, FileIDs[0], Entry, declName);

  std::string file;
  llvm::raw_string_ostream os(file);
  for (auto BI : *Buf)
    os << BI;

  return os.str();
}

void HTMLDiagnostics::dumpCoverageData(
    const PathDiagnostic &D,
    const PathPieces &path,
    llvm::raw_string_ostream &os) {

  const FilesToLineNumsMap &ExecutedLines = D.getExecutedLines();

  os << "var relevant_lines = {";
  for (auto I = ExecutedLines.begin(),
            E = ExecutedLines.end(); I != E; ++I) {
    if (I != ExecutedLines.begin())
      os << ", ";

    os << "\"" << I->first.getHashValue() << "\": {";
    for (unsigned LineNo : I->second) {
      if (LineNo != *(I->second.begin()))
        os << ", ";

      os << "\"" << LineNo << "\": 1";
    }
    os << "}";
  }

  os << "};";
}

std::string HTMLDiagnostics::showRelevantLinesJavascript(
      const PathDiagnostic &D, const PathPieces &path) {
  std::string s;
  llvm::raw_string_ostream os(s);
  os << "<script type='text/javascript'>\n";
  dumpCoverageData(D, path, os);
  os << R"<<<(

var filterCounterexample = function (hide) {
  var tables = document.getElementsByClassName("code");
  for (var t=0; t<tables.length; t++) {
    var table = tables[t];
    var file_id = table.getAttribute("data-fileid");
    var lines_in_fid = relevant_lines[file_id];
    if (!lines_in_fid) {
      lines_in_fid = {};
    }
    var lines = table.getElementsByClassName("codeline");
    for (var i=0; i<lines.length; i++) {
        var el = lines[i];
        var lineNo = el.getAttribute("data-linenumber");
        if (!lines_in_fid[lineNo]) {
          if (hide) {
            el.setAttribute("hidden", "");
          } else {
            el.removeAttribute("hidden");
          }
        }
    }
  }
}

window.addEventListener("keydown", function (event) {
  if (event.defaultPrevented) {
    return;
  }
  if (event.key == "S") {
    var checked = document.getElementsByName("showCounterexample")[0].checked;
    filterCounterexample(!checked);
    document.getElementsByName("showCounterexample")[0].checked = !checked;
  } else {
    return;
  }
  event.preventDefault();
}, true);

document.addEventListener("DOMContentLoaded", function() {
    document.querySelector('input[name="showCounterexample"]').onchange=
        function (event) {
      filterCounterexample(this.checked);
    };
});
</script>

<form>
    <input type="checkbox" name="showCounterexample" id="showCounterexample" />
    <label for="showCounterexample">
       Show only relevant lines
    </label>
</form>
)<<<";

  return os.str();
}

void HTMLDiagnostics::FinalizeHTML(const PathDiagnostic& D, Rewriter &R,
    const SourceManager& SMgr, const PathPieces& path, FileID FID,
    const FileEntry *Entry, const char *declName) {
  // This is a cludge; basically we want to append either the full
  // working directory if we have no directory information.  This is
  // a work in progress.

  llvm::SmallString<0> DirName;

  if (llvm::sys::path::is_relative(Entry->getName())) {
    llvm::sys::fs::current_path(DirName);
    DirName += '/';
  }

  int LineNumber = path.back()->getLocation().asLocation().getExpansionLineNumber();
  int ColumnNumber = path.back()->getLocation().asLocation().getExpansionColumnNumber();

  R.InsertTextBefore(SMgr.getLocForStartOfFile(FID), showHelpJavascript());

  R.InsertTextBefore(SMgr.getLocForStartOfFile(FID),
                     generateKeyboardNavigationJavascript());

  // Checkbox and javascript for filtering the output to the counterexample.
  R.InsertTextBefore(SMgr.getLocForStartOfFile(FID),
                     showRelevantLinesJavascript(D, path));

  // Add the name of the file as an <h1> tag.
  {
    std::string s;
    llvm::raw_string_ostream os(s);

    os << "<!-- REPORTHEADER -->\n"
       << "<h3>Bug Summary</h3>\n<table class=\"simpletable\">\n"
          "<tr><td class=\"rowname\">File:</td><td>"
       << html::EscapeText(DirName)
       << html::EscapeText(Entry->getName())
       << "</td></tr>\n<tr><td class=\"rowname\">Warning:</td><td>"
          "<a href=\"#EndPath\">line "
       << LineNumber
       << ", column "
       << ColumnNumber
       << "</a><br />"
       << D.getVerboseDescription() << "</td></tr>\n";

    // The navigation across the extra notes pieces.
    unsigned NumExtraPieces = 0;
    for (const auto &Piece : path) {
      if (const auto *P = dyn_cast<PathDiagnosticNotePiece>(Piece.get())) {
        int LineNumber =
            P->getLocation().asLocation().getExpansionLineNumber();
        int ColumnNumber =
            P->getLocation().asLocation().getExpansionColumnNumber();
        os << "<tr><td class=\"rowname\">Note:</td><td>"
           << "<a href=\"#Note" << NumExtraPieces << "\">line "
           << LineNumber << ", column " << ColumnNumber << "</a><br />"
           << P->getString() << "</td></tr>";
        ++NumExtraPieces;
      }
    }

    // Output any other meta data.

    for (PathDiagnostic::meta_iterator I = D.meta_begin(), E = D.meta_end();
         I != E; ++I) {
      os << "<tr><td></td><td>" << html::EscapeText(*I) << "</td></tr>\n";
    }

    os << R"<<<(
</table>
<!-- REPORTSUMMARYEXTRA -->
<h3>Annotated Source Code</h3>
<p>Press <a href="#" onclick="toggleHelp(); return false;">'?'</a>
   to see keyboard shortcuts</p>
<input type="checkbox" class="spoilerhider" id="showinvocation" />
<label for="showinvocation" >Show analyzer invocation</label>
<div class="spoiler">clang -cc1 )<<<";
    os << html::EscapeText(AnalyzerOpts.FullCompilerInvocation);
    os << R"<<<(
</div>
<div id='tooltiphint' hidden="true">
  <p>Keyboard shortcuts: </p>
  <ul>
    <li>Use 'j/k' keys for keyboard navigation</li>
    <li>Use 'Shift+S' to show/hide relevant lines</li>
    <li>Use '?' to toggle this window</li>
  </ul>
  <a href="#" onclick="toggleHelp(); return false;">Close</a>
</div>
)<<<";
    R.InsertTextBefore(SMgr.getLocForStartOfFile(FID), os.str());
  }

  // Embed meta-data tags.
  {
    std::string s;
    llvm::raw_string_ostream os(s);

    StringRef BugDesc = D.getVerboseDescription();
    if (!BugDesc.empty())
      os << "\n<!-- BUGDESC " << BugDesc << " -->\n";

    StringRef BugType = D.getBugType();
    if (!BugType.empty())
      os << "\n<!-- BUGTYPE " << BugType << " -->\n";

    PathDiagnosticLocation UPDLoc = D.getUniqueingLoc();
    FullSourceLoc L(SMgr.getExpansionLoc(UPDLoc.isValid()
                                             ? UPDLoc.asLocation()
                                             : D.getLocation().asLocation()),
                    SMgr);
    const Decl *DeclWithIssue = D.getDeclWithIssue();

    StringRef BugCategory = D.getCategory();
    if (!BugCategory.empty())
      os << "\n<!-- BUGCATEGORY " << BugCategory << " -->\n";

    os << "\n<!-- BUGFILE " << DirName << Entry->getName() << " -->\n";

    os << "\n<!-- FILENAME " << llvm::sys::path::filename(Entry->getName()) << " -->\n";

    os  << "\n<!-- FUNCTIONNAME " <<  declName << " -->\n";

    os << "\n<!-- ISSUEHASHCONTENTOFLINEINCONTEXT "
       << GetIssueHash(SMgr, L, D.getCheckName(), D.getBugType(), DeclWithIssue,
                       PP.getLangOpts()) << " -->\n";

    os << "\n<!-- BUGLINE "
       << LineNumber
       << " -->\n";

    os << "\n<!-- BUGCOLUMN "
      << ColumnNumber
      << " -->\n";

    os << "\n<!-- BUGPATHLENGTH " << path.size() << " -->\n";

    // Mark the end of the tags.
    os << "\n<!-- BUGMETAEND -->\n";

    // Insert the text.
    R.InsertTextBefore(SMgr.getLocForStartOfFile(FID), os.str());
  }

  html::AddHeaderFooterInternalBuiltinCSS(R, FID, Entry->getName());
}

StringRef HTMLDiagnostics::showHelpJavascript() {
  return R"<<<(
<script type='text/javascript'>

var toggleHelp = function() {
    var hint = document.querySelector("#tooltiphint");
    var attributeName = "hidden";
    if (hint.hasAttribute(attributeName)) {
      hint.removeAttribute(attributeName);
    } else {
      hint.setAttribute("hidden", "true");
    }
};
window.addEventListener("keydown", function (event) {
  if (event.defaultPrevented) {
    return;
  }
  if (event.key == "?") {
    toggleHelp();
  } else {
    return;
  }
  event.preventDefault();
});
</script>
)<<<";
}

void HTMLDiagnostics::RewriteFile(Rewriter &R,
                                  const PathPieces& path, FileID FID) {
  // Process the path.
  // Maintain the counts of extra note pieces separately.
  unsigned TotalPieces = path.size();
  unsigned TotalNotePieces =
      std::count_if(path.begin(), path.end(),
                    [](const std::shared_ptr<PathDiagnosticPiece> &p) {
                      return isa<PathDiagnosticNotePiece>(*p);
                    });

  unsigned TotalRegularPieces = TotalPieces - TotalNotePieces;
  unsigned NumRegularPieces = TotalRegularPieces;
  unsigned NumNotePieces = TotalNotePieces;

  for (auto I = path.rbegin(), E = path.rend(); I != E; ++I) {
    if (isa<PathDiagnosticNotePiece>(I->get())) {
      // This adds diagnostic bubbles, but not navigation.
      // Navigation through note pieces would be added later,
      // as a separate pass through the piece list.
      HandlePiece(R, FID, **I, NumNotePieces, TotalNotePieces);
      --NumNotePieces;
    } else {
      HandlePiece(R, FID, **I, NumRegularPieces, TotalRegularPieces);
      --NumRegularPieces;
    }
  }

  // Add line numbers, header, footer, etc.

  html::EscapeText(R, FID);
  html::AddLineNumbers(R, FID);

  // If we have a preprocessor, relex the file and syntax highlight.
  // We might not have a preprocessor if we come from a deserialized AST file,
  // for example.

  html::SyntaxHighlight(R, FID, PP);
  html::HighlightMacros(R, FID, PP);
}

void HTMLDiagnostics::HandlePiece(Rewriter& R, FileID BugFileID,
                                  const PathDiagnosticPiece& P,
                                  unsigned num, unsigned max) {
  // For now, just draw a box above the line in question, and emit the
  // warning.
  FullSourceLoc Pos = P.getLocation().asLocation();

  if (!Pos.isValid())
    return;

  SourceManager &SM = R.getSourceMgr();
  assert(&Pos.getManager() == &SM && "SourceManagers are different!");
  std::pair<FileID, unsigned> LPosInfo = SM.getDecomposedExpansionLoc(Pos);

  if (LPosInfo.first != BugFileID)
    return;

  const llvm::MemoryBuffer *Buf = SM.getBuffer(LPosInfo.first);
  const char* FileStart = Buf->getBufferStart();

  // Compute the column number.  Rewind from the current position to the start
  // of the line.
  unsigned ColNo = SM.getColumnNumber(LPosInfo.first, LPosInfo.second);
  const char *TokInstantiationPtr =Pos.getExpansionLoc().getCharacterData();
  const char *LineStart = TokInstantiationPtr-ColNo;

  // Compute LineEnd.
  const char *LineEnd = TokInstantiationPtr;
  const char* FileEnd = Buf->getBufferEnd();
  while (*LineEnd != '\n' && LineEnd != FileEnd)
    ++LineEnd;

  // Compute the margin offset by counting tabs and non-tabs.
  unsigned PosNo = 0;
  for (const char* c = LineStart; c != TokInstantiationPtr; ++c)
    PosNo += *c == '\t' ? 8 : 1;

  // Create the html for the message.

  const char *Kind = nullptr;
  bool IsNote = false;
  bool SuppressIndex = (max == 1);
  switch (P.getKind()) {
  case PathDiagnosticPiece::Call:
      llvm_unreachable("Calls and extra notes should already be handled");
  case PathDiagnosticPiece::Event:  Kind = "Event"; break;
  case PathDiagnosticPiece::ControlFlow: Kind = "Control"; break;
    // Setting Kind to "Control" is intentional.
  case PathDiagnosticPiece::Macro: Kind = "Control"; break;
  case PathDiagnosticPiece::Note:
    Kind = "Note";
    IsNote = true;
    SuppressIndex = true;
    break;
  }

  std::string sbuf;
  llvm::raw_string_ostream os(sbuf);

  os << "\n<tr><td class=\"num\"></td><td class=\"line\"><div id=\"";

  if (IsNote)
    os << "Note" << num;
  else if (num == max)
    os << "EndPath";
  else
    os << "Path" << num;

  os << "\" class=\"msg";
  if (Kind)
    os << " msg" << Kind;
  os << "\" style=\"margin-left:" << PosNo << "ex";

  // Output a maximum size.
  if (!isa<PathDiagnosticMacroPiece>(P)) {
    // Get the string and determining its maximum substring.
    const auto &Msg = P.getString();
    unsigned max_token = 0;
    unsigned cnt = 0;
    unsigned len = Msg.size();

    for (char C : Msg)
      switch (C) {
      default:
        ++cnt;
        continue;
      case ' ':
      case '\t':
      case '\n':
        if (cnt > max_token) max_token = cnt;
        cnt = 0;
      }

    if (cnt > max_token)
      max_token = cnt;

    // Determine the approximate size of the message bubble in em.
    unsigned em;
    const unsigned max_line = 120;

    if (max_token >= max_line)
      em = max_token / 2;
    else {
      unsigned characters = max_line;
      unsigned lines = len / max_line;

      if (lines > 0) {
        for (; characters > max_token; --characters)
          if (len / characters > lines) {
            ++characters;
            break;
          }
      }

      em = characters / 2;
    }

    if (em < max_line/2)
      os << "; max-width:" << em << "em";
  }
  else
    os << "; max-width:100em";

  os << "\">";

  if (!SuppressIndex) {
    os << "<table class=\"msgT\"><tr><td valign=\"top\">";
    os << "<div class=\"PathIndex";
    if (Kind) os << " PathIndex" << Kind;
    os << "\">" << num << "</div>";

    if (num > 1) {
      os << "</td><td><div class=\"PathNav\"><a href=\"#Path"
         << (num - 1)
         << "\" title=\"Previous event ("
         << (num - 1)
         << ")\">&#x2190;</a></div></td>";
    }

    os << "</td><td>";
  }

  if (const auto *MP = dyn_cast<PathDiagnosticMacroPiece>(&P)) {
    os << "Within the expansion of the macro '";

    // Get the name of the macro by relexing it.
    {
      FullSourceLoc L = MP->getLocation().asLocation().getExpansionLoc();
      assert(L.isFileID());
      StringRef BufferInfo = L.getBufferData();
      std::pair<FileID, unsigned> LocInfo = L.getDecomposedLoc();
      const char* MacroName = LocInfo.second + BufferInfo.data();
      Lexer rawLexer(SM.getLocForStartOfFile(LocInfo.first), PP.getLangOpts(),
                     BufferInfo.begin(), MacroName, BufferInfo.end());

      Token TheTok;
      rawLexer.LexFromRawLexer(TheTok);
      for (unsigned i = 0, n = TheTok.getLength(); i < n; ++i)
        os << MacroName[i];
    }

    os << "':\n";

    if (!SuppressIndex) {
      os << "</td>";
      if (num < max) {
        os << "<td><div class=\"PathNav\"><a href=\"#";
        if (num == max - 1)
          os << "EndPath";
        else
          os << "Path" << (num + 1);
        os << "\" title=\"Next event ("
        << (num + 1)
        << ")\">&#x2192;</a></div></td>";
      }

      os << "</tr></table>";
    }

    // Within a macro piece.  Write out each event.
    ProcessMacroPiece(os, *MP, 0);
  }
  else {
    os << html::EscapeText(P.getString());

    if (!SuppressIndex) {
      os << "</td>";
      if (num < max) {
        os << "<td><div class=\"PathNav\"><a href=\"#";
        if (num == max - 1)
          os << "EndPath";
        else
          os << "Path" << (num + 1);
        os << "\" title=\"Next event ("
           << (num + 1)
           << ")\">&#x2192;</a></div></td>";
      }

      os << "</tr></table>";
    }
  }

  os << "</div></td></tr>";

  // Insert the new html.
  unsigned DisplayPos = LineEnd - FileStart;
  SourceLocation Loc =
    SM.getLocForStartOfFile(LPosInfo.first).getLocWithOffset(DisplayPos);

  R.InsertTextBefore(Loc, os.str());

  // Now highlight the ranges.
  ArrayRef<SourceRange> Ranges = P.getRanges();
  for (const auto &Range : Ranges)
    HighlightRange(R, LPosInfo.first, Range);
}

static void EmitAlphaCounter(raw_ostream &os, unsigned n) {
  unsigned x = n % ('z' - 'a');
  n /= 'z' - 'a';

  if (n > 0)
    EmitAlphaCounter(os, n);

  os << char('a' + x);
}

unsigned HTMLDiagnostics::ProcessMacroPiece(raw_ostream &os,
                                            const PathDiagnosticMacroPiece& P,
                                            unsigned num) {
  for (const auto &subPiece : P.subPieces) {
    if (const auto *MP = dyn_cast<PathDiagnosticMacroPiece>(subPiece.get())) {
      num = ProcessMacroPiece(os, *MP, num);
      continue;
    }

    if (const auto *EP = dyn_cast<PathDiagnosticEventPiece>(subPiece.get())) {
      os << "<div class=\"msg msgEvent\" style=\"width:94%; "
            "margin-left:5px\">"
            "<table class=\"msgT\"><tr>"
            "<td valign=\"top\"><div class=\"PathIndex PathIndexEvent\">";
      EmitAlphaCounter(os, num++);
      os << "</div></td><td valign=\"top\">"
         << html::EscapeText(EP->getString())
         << "</td></tr></table></div>\n";
    }
  }

  return num;
}

void HTMLDiagnostics::HighlightRange(Rewriter& R, FileID BugFileID,
                                     SourceRange Range,
                                     const char *HighlightStart,
                                     const char *HighlightEnd) {
  SourceManager &SM = R.getSourceMgr();
  const LangOptions &LangOpts = R.getLangOpts();

  SourceLocation InstantiationStart = SM.getExpansionLoc(Range.getBegin());
  unsigned StartLineNo = SM.getExpansionLineNumber(InstantiationStart);

  SourceLocation InstantiationEnd = SM.getExpansionLoc(Range.getEnd());
  unsigned EndLineNo = SM.getExpansionLineNumber(InstantiationEnd);

  if (EndLineNo < StartLineNo)
    return;

  if (SM.getFileID(InstantiationStart) != BugFileID ||
      SM.getFileID(InstantiationEnd) != BugFileID)
    return;

  // Compute the column number of the end.
  unsigned EndColNo = SM.getExpansionColumnNumber(InstantiationEnd);
  unsigned OldEndColNo = EndColNo;

  if (EndColNo) {
    // Add in the length of the token, so that we cover multi-char tokens.
    EndColNo += Lexer::MeasureTokenLength(Range.getEnd(), SM, LangOpts)-1;
  }

  // Highlight the range.  Make the span tag the outermost tag for the
  // selected range.

  SourceLocation E =
    InstantiationEnd.getLocWithOffset(EndColNo - OldEndColNo);

  html::HighlightRange(R, InstantiationStart, E, HighlightStart, HighlightEnd);
}

StringRef HTMLDiagnostics::generateKeyboardNavigationJavascript() {
  return R"<<<(
<script type='text/javascript'>
var digitMatcher = new RegExp("[0-9]+");

document.addEventListener("DOMContentLoaded", function() {
    document.querySelectorAll(".PathNav > a").forEach(
        function(currentValue, currentIndex) {
            var hrefValue = currentValue.getAttribute("href");
            currentValue.onclick = function() {
                scrollTo(document.querySelector(hrefValue));
                return false;
            };
        });
});

var findNum = function() {
    var s = document.querySelector(".selected");
    if (!s || s.id == "EndPath") {
        return 0;
    }
    var out = parseInt(digitMatcher.exec(s.id)[0]);
    return out;
};

var scrollTo = function(el) {
    document.querySelectorAll(".selected").forEach(function(s) {
        s.classList.remove("selected");
    });
    el.classList.add("selected");
    window.scrollBy(0, el.getBoundingClientRect().top -
        (window.innerHeight / 2));
}

var move = function(num, up, numItems) {
  if (num == 1 && up || num == numItems - 1 && !up) {
    return 0;
  } else if (num == 0 && up) {
    return numItems - 1;
  } else if (num == 0 && !up) {
    return 1 % numItems;
  }
  return up ? num - 1 : num + 1;
}

var numToId = function(num) {
  if (num == 0) {
    return document.getElementById("EndPath")
  }
  return document.getElementById("Path" + num);
};

var navigateTo = function(up) {
  var numItems = document.querySelectorAll(
      ".line > .msgEvent, .line > .msgControl").length;
  var currentSelected = findNum();
  var newSelected = move(currentSelected, up, numItems);
  var newEl = numToId(newSelected, numItems);

  // Scroll element into center.
  scrollTo(newEl);
};

window.addEventListener("keydown", function (event) {
  if (event.defaultPrevented) {
    return;
  }
  if (event.key == "j") {
    navigateTo(/*up=*/false);
  } else if (event.key == "k") {
    navigateTo(/*up=*/true);
  } else {
    return;
  }
  event.preventDefault();
}, true);
</script>
  )<<<";
}
