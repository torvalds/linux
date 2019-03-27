//===--- LogDiagnosticPrinter.cpp - Log Diagnostic Printer ----------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "clang/Frontend/LogDiagnosticPrinter.h"
#include "clang/Basic/DiagnosticOptions.h"
#include "clang/Basic/FileManager.h"
#include "clang/Basic/PlistSupport.h"
#include "clang/Basic/SourceManager.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/raw_ostream.h"
using namespace clang;
using namespace markup;

LogDiagnosticPrinter::LogDiagnosticPrinter(
    raw_ostream &os, DiagnosticOptions *diags,
    std::unique_ptr<raw_ostream> StreamOwner)
    : OS(os), StreamOwner(std::move(StreamOwner)), LangOpts(nullptr),
      DiagOpts(diags) {}

static StringRef getLevelName(DiagnosticsEngine::Level Level) {
  switch (Level) {
  case DiagnosticsEngine::Ignored: return "ignored";
  case DiagnosticsEngine::Remark:  return "remark";
  case DiagnosticsEngine::Note:    return "note";
  case DiagnosticsEngine::Warning: return "warning";
  case DiagnosticsEngine::Error:   return "error";
  case DiagnosticsEngine::Fatal:   return "fatal error";
  }
  llvm_unreachable("Invalid DiagnosticsEngine level!");
}

void
LogDiagnosticPrinter::EmitDiagEntry(llvm::raw_ostream &OS,
                                    const LogDiagnosticPrinter::DiagEntry &DE) {
  OS << "    <dict>\n";
  OS << "      <key>level</key>\n"
     << "      ";
  EmitString(OS, getLevelName(DE.DiagnosticLevel)) << '\n';
  if (!DE.Filename.empty()) {
    OS << "      <key>filename</key>\n"
       << "      ";
    EmitString(OS, DE.Filename) << '\n';
  }
  if (DE.Line != 0) {
    OS << "      <key>line</key>\n"
       << "      ";
    EmitInteger(OS, DE.Line) << '\n';
  }
  if (DE.Column != 0) {
    OS << "      <key>column</key>\n"
       << "      ";
    EmitInteger(OS, DE.Column) << '\n';
  }
  if (!DE.Message.empty()) {
    OS << "      <key>message</key>\n"
       << "      ";
    EmitString(OS, DE.Message) << '\n';
  }
  OS << "      <key>ID</key>\n"
     << "      ";
  EmitInteger(OS, DE.DiagnosticID) << '\n';
  if (!DE.WarningOption.empty()) {
    OS << "      <key>WarningOption</key>\n"
       << "      ";
    EmitString(OS, DE.WarningOption) << '\n';
  }
  OS << "    </dict>\n";
}

void LogDiagnosticPrinter::EndSourceFile() {
  // We emit all the diagnostics in EndSourceFile. However, we don't emit any
  // entry if no diagnostics were present.
  //
  // Note that DiagnosticConsumer has no "end-of-compilation" callback, so we
  // will miss any diagnostics which are emitted after and outside the
  // translation unit processing.
  if (Entries.empty())
    return;

  // Write to a temporary string to ensure atomic write of diagnostic object.
  SmallString<512> Msg;
  llvm::raw_svector_ostream OS(Msg);

  OS << "<dict>\n";
  if (!MainFilename.empty()) {
    OS << "  <key>main-file</key>\n"
       << "  ";
    EmitString(OS, MainFilename) << '\n';
  }
  if (!DwarfDebugFlags.empty()) {
    OS << "  <key>dwarf-debug-flags</key>\n"
       << "  ";
    EmitString(OS, DwarfDebugFlags) << '\n';
  }
  OS << "  <key>diagnostics</key>\n";
  OS << "  <array>\n";
  for (auto &DE : Entries)
    EmitDiagEntry(OS, DE);
  OS << "  </array>\n";
  OS << "</dict>\n";

  this->OS << OS.str();
}

void LogDiagnosticPrinter::HandleDiagnostic(DiagnosticsEngine::Level Level,
                                            const Diagnostic &Info) {
  // Default implementation (Warnings/errors count).
  DiagnosticConsumer::HandleDiagnostic(Level, Info);

  // Initialize the main file name, if we haven't already fetched it.
  if (MainFilename.empty() && Info.hasSourceManager()) {
    const SourceManager &SM = Info.getSourceManager();
    FileID FID = SM.getMainFileID();
    if (FID.isValid()) {
      const FileEntry *FE = SM.getFileEntryForID(FID);
      if (FE && FE->isValid())
        MainFilename = FE->getName();
    }
  }

  // Create the diag entry.
  DiagEntry DE;
  DE.DiagnosticID = Info.getID();
  DE.DiagnosticLevel = Level;

  DE.WarningOption = DiagnosticIDs::getWarningOptionForDiag(DE.DiagnosticID);

  // Format the message.
  SmallString<100> MessageStr;
  Info.FormatDiagnostic(MessageStr);
  DE.Message = MessageStr.str();

  // Set the location information.
  DE.Filename = "";
  DE.Line = DE.Column = 0;
  if (Info.getLocation().isValid() && Info.hasSourceManager()) {
    const SourceManager &SM = Info.getSourceManager();
    PresumedLoc PLoc = SM.getPresumedLoc(Info.getLocation());

    if (PLoc.isInvalid()) {
      // At least print the file name if available:
      FileID FID = SM.getFileID(Info.getLocation());
      if (FID.isValid()) {
        const FileEntry *FE = SM.getFileEntryForID(FID);
        if (FE && FE->isValid())
          DE.Filename = FE->getName();
      }
    } else {
      DE.Filename = PLoc.getFilename();
      DE.Line = PLoc.getLine();
      DE.Column = PLoc.getColumn();
    }
  }

  // Record the diagnostic entry.
  Entries.push_back(DE);
}

