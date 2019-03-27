//===--- HeaderIncludes.cpp - Generate Header Includes --------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "clang/Frontend/DependencyOutputOptions.h"
#include "clang/Frontend/Utils.h"
#include "clang/Basic/SourceManager.h"
#include "clang/Frontend/FrontendDiagnostic.h"
#include "clang/Lex/Preprocessor.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/Support/raw_ostream.h"
using namespace clang;

namespace {
class HeaderIncludesCallback : public PPCallbacks {
  SourceManager &SM;
  raw_ostream *OutputFile;
  const DependencyOutputOptions &DepOpts;
  unsigned CurrentIncludeDepth;
  bool HasProcessedPredefines;
  bool OwnsOutputFile;
  bool ShowAllHeaders;
  bool ShowDepth;
  bool MSStyle;

public:
  HeaderIncludesCallback(const Preprocessor *PP, bool ShowAllHeaders_,
                         raw_ostream *OutputFile_,
                         const DependencyOutputOptions &DepOpts,
                         bool OwnsOutputFile_, bool ShowDepth_, bool MSStyle_)
      : SM(PP->getSourceManager()), OutputFile(OutputFile_), DepOpts(DepOpts),
        CurrentIncludeDepth(0), HasProcessedPredefines(false),
        OwnsOutputFile(OwnsOutputFile_), ShowAllHeaders(ShowAllHeaders_),
        ShowDepth(ShowDepth_), MSStyle(MSStyle_) {}

  ~HeaderIncludesCallback() override {
    if (OwnsOutputFile)
      delete OutputFile;
  }

  void FileChanged(SourceLocation Loc, FileChangeReason Reason,
                   SrcMgr::CharacteristicKind FileType,
                   FileID PrevFID) override;
};
}

static void PrintHeaderInfo(raw_ostream *OutputFile, StringRef Filename,
                            bool ShowDepth, unsigned CurrentIncludeDepth,
                            bool MSStyle) {
  // Write to a temporary string to avoid unnecessary flushing on errs().
  SmallString<512> Pathname(Filename);
  if (!MSStyle)
    Lexer::Stringify(Pathname);

  SmallString<256> Msg;
  if (MSStyle)
    Msg += "Note: including file:";

  if (ShowDepth) {
    // The main source file is at depth 1, so skip one dot.
    for (unsigned i = 1; i != CurrentIncludeDepth; ++i)
      Msg += MSStyle ? ' ' : '.';

    if (!MSStyle)
      Msg += ' ';
  }
  Msg += Pathname;
  Msg += '\n';

  *OutputFile << Msg;
  OutputFile->flush();
}

void clang::AttachHeaderIncludeGen(Preprocessor &PP,
                                   const DependencyOutputOptions &DepOpts,
                                   bool ShowAllHeaders, StringRef OutputPath,
                                   bool ShowDepth, bool MSStyle) {
  raw_ostream *OutputFile = &llvm::errs();
  bool OwnsOutputFile = false;

  // Choose output stream, when printing in cl.exe /showIncludes style.
  if (MSStyle) {
    switch (DepOpts.ShowIncludesDest) {
    default:
      llvm_unreachable("Invalid destination for /showIncludes output!");
    case ShowIncludesDestination::Stderr:
      OutputFile = &llvm::errs();
      break;
    case ShowIncludesDestination::Stdout:
      OutputFile = &llvm::outs();
      break;
    }
  }

  // Open the output file, if used.
  if (!OutputPath.empty()) {
    std::error_code EC;
    llvm::raw_fd_ostream *OS = new llvm::raw_fd_ostream(
        OutputPath.str(), EC, llvm::sys::fs::F_Append | llvm::sys::fs::F_Text);
    if (EC) {
      PP.getDiagnostics().Report(clang::diag::warn_fe_cc_print_header_failure)
          << EC.message();
      delete OS;
    } else {
      OS->SetUnbuffered();
      OutputFile = OS;
      OwnsOutputFile = true;
    }
  }

  // Print header info for extra headers, pretending they were discovered by
  // the regular preprocessor. The primary use case is to support proper
  // generation of Make / Ninja file dependencies for implicit includes, such
  // as sanitizer blacklists. It's only important for cl.exe compatibility,
  // the GNU way to generate rules is -M / -MM / -MD / -MMD.
  for (const auto &Header : DepOpts.ExtraDeps)
    PrintHeaderInfo(OutputFile, Header, ShowDepth, 2, MSStyle);
  PP.addPPCallbacks(llvm::make_unique<HeaderIncludesCallback>(
      &PP, ShowAllHeaders, OutputFile, DepOpts, OwnsOutputFile, ShowDepth,
      MSStyle));
}

void HeaderIncludesCallback::FileChanged(SourceLocation Loc,
                                         FileChangeReason Reason,
                                       SrcMgr::CharacteristicKind NewFileType,
                                       FileID PrevFID) {
  // Unless we are exiting a #include, make sure to skip ahead to the line the
  // #include directive was at.
  PresumedLoc UserLoc = SM.getPresumedLoc(Loc);
  if (UserLoc.isInvalid())
    return;

  // Adjust the current include depth.
  if (Reason == PPCallbacks::EnterFile) {
    ++CurrentIncludeDepth;
  } else if (Reason == PPCallbacks::ExitFile) {
    if (CurrentIncludeDepth)
      --CurrentIncludeDepth;

    // We track when we are done with the predefines by watching for the first
    // place where we drop back to a nesting depth of 1.
    if (CurrentIncludeDepth == 1 && !HasProcessedPredefines) {
      if (!DepOpts.ShowIncludesPretendHeader.empty()) {
        PrintHeaderInfo(OutputFile, DepOpts.ShowIncludesPretendHeader,
                        ShowDepth, 2, MSStyle);
      }
      HasProcessedPredefines = true;
    }

    return;
  } else
    return;

  // Show the header if we are (a) past the predefines, or (b) showing all
  // headers and in the predefines at a depth past the initial file and command
  // line buffers.
  bool ShowHeader = (HasProcessedPredefines ||
                     (ShowAllHeaders && CurrentIncludeDepth > 2));
  unsigned IncludeDepth = CurrentIncludeDepth;
  if (!HasProcessedPredefines)
    --IncludeDepth; // Ignore indent from <built-in>.
  else if (!DepOpts.ShowIncludesPretendHeader.empty())
    ++IncludeDepth; // Pretend inclusion by ShowIncludesPretendHeader.

  // Dump the header include information we are past the predefines buffer or
  // are showing all headers and this isn't the magic implicit <command line>
  // header.
  // FIXME: Identify headers in a more robust way than comparing their name to
  // "<command line>" and "<built-in>" in a bunch of places.
  if (ShowHeader && Reason == PPCallbacks::EnterFile &&
      UserLoc.getFilename() != StringRef("<command line>")) {
    PrintHeaderInfo(OutputFile, UserLoc.getFilename(), ShowDepth, IncludeDepth,
                    MSStyle);
  }
}
