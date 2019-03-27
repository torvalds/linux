//===--- PlistReporter.cpp - ARC Migrate Tool Plist Reporter ----*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "Internals.h"
#include "clang/Basic/FileManager.h"
#include "clang/Basic/PlistSupport.h"
#include "clang/Basic/SourceManager.h"
#include "clang/Lex/Lexer.h"
using namespace clang;
using namespace arcmt;
using namespace markup;

static StringRef getLevelName(DiagnosticsEngine::Level Level) {
  switch (Level) {
  case DiagnosticsEngine::Ignored:
    llvm_unreachable("ignored");
  case DiagnosticsEngine::Note:
    return "note";
  case DiagnosticsEngine::Remark:
  case DiagnosticsEngine::Warning:
    return "warning";
  case DiagnosticsEngine::Fatal:
  case DiagnosticsEngine::Error:
    return "error";
  }
  llvm_unreachable("Invalid DiagnosticsEngine level!");
}

void arcmt::writeARCDiagsToPlist(const std::string &outPath,
                                 ArrayRef<StoredDiagnostic> diags,
                                 SourceManager &SM,
                                 const LangOptions &LangOpts) {
  DiagnosticIDs DiagIDs;

  // Build up a set of FIDs that we use by scanning the locations and
  // ranges of the diagnostics.
  FIDMap FM;
  SmallVector<FileID, 10> Fids;

  for (ArrayRef<StoredDiagnostic>::iterator
         I = diags.begin(), E = diags.end(); I != E; ++I) {
    const StoredDiagnostic &D = *I;

    AddFID(FM, Fids, SM, D.getLocation());

    for (StoredDiagnostic::range_iterator
           RI = D.range_begin(), RE = D.range_end(); RI != RE; ++RI) {
      AddFID(FM, Fids, SM, RI->getBegin());
      AddFID(FM, Fids, SM, RI->getEnd());
    }
  }

  std::error_code EC;
  llvm::raw_fd_ostream o(outPath, EC, llvm::sys::fs::F_Text);
  if (EC) {
    llvm::errs() << "error: could not create file: " << outPath << '\n';
    return;
  }

  EmitPlistHeader(o);

  // Write the root object: a <dict> containing...
  //  - "files", an <array> mapping from FIDs to file names
  //  - "diagnostics", an <array> containing the diagnostics
  o << "<dict>\n"
       " <key>files</key>\n"
       " <array>\n";

  for (FileID FID : Fids)
    EmitString(o << "  ", SM.getFileEntryForID(FID)->getName()) << '\n';

  o << " </array>\n"
       " <key>diagnostics</key>\n"
       " <array>\n";

  for (ArrayRef<StoredDiagnostic>::iterator
         DI = diags.begin(), DE = diags.end(); DI != DE; ++DI) {

    const StoredDiagnostic &D = *DI;

    if (D.getLevel() == DiagnosticsEngine::Ignored)
      continue;

    o << "  <dict>\n";

    // Output the diagnostic.
    o << "   <key>description</key>";
    EmitString(o, D.getMessage()) << '\n';
    o << "   <key>category</key>";
    EmitString(o, DiagIDs.getCategoryNameFromID(
                          DiagIDs.getCategoryNumberForDiag(D.getID()))) << '\n';
    o << "   <key>type</key>";
    EmitString(o, getLevelName(D.getLevel())) << '\n';

    // Output the location of the bug.
    o << "  <key>location</key>\n";
    EmitLocation(o, SM, D.getLocation(), FM, 2);

    // Output the ranges (if any).
    if (!D.getRanges().empty()) {
      o << "   <key>ranges</key>\n";
      o << "   <array>\n";
      for (auto &R : D.getRanges()) {
        CharSourceRange ExpansionRange = SM.getExpansionRange(R);
        EmitRange(o, SM, Lexer::getAsCharRange(ExpansionRange, SM, LangOpts),
                  FM, 4);
      }
      o << "   </array>\n";
    }

    // Close up the entry.
    o << "  </dict>\n";
  }

  o << " </array>\n";

  // Finish.
  o << "</dict>\n</plist>";
}
