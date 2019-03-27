//===- FixItRewriter.cpp - Fix-It Rewriter Diagnostic Client --------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This is a diagnostic client adaptor that performs rewrites as
// suggested by code modification hints attached to diagnostics. It
// then forwards any diagnostics to the adapted diagnostic client.
//
//===----------------------------------------------------------------------===//

#include "clang/Rewrite/Frontend/FixItRewriter.h"
#include "clang/Basic/Diagnostic.h"
#include "clang/Basic/FileManager.h"
#include "clang/Basic/LLVM.h"
#include "clang/Basic/SourceLocation.h"
#include "clang/Basic/SourceManager.h"
#include "clang/Edit/Commit.h"
#include "clang/Edit/EditsReceiver.h"
#include "clang/Frontend/FrontendDiagnostic.h"
#include "clang/Rewrite/Core/RewriteBuffer.h"
#include "clang/Rewrite/Core/Rewriter.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/raw_ostream.h"
#include <cstdio>
#include <memory>
#include <string>
#include <system_error>
#include <utility>

using namespace clang;

FixItRewriter::FixItRewriter(DiagnosticsEngine &Diags, SourceManager &SourceMgr,
                             const LangOptions &LangOpts,
                             FixItOptions *FixItOpts)
    : Diags(Diags), Editor(SourceMgr, LangOpts), Rewrite(SourceMgr, LangOpts),
      FixItOpts(FixItOpts) {
  Owner = Diags.takeClient();
  Client = Diags.getClient();
  Diags.setClient(this, false);
}

FixItRewriter::~FixItRewriter() {
  Diags.setClient(Client, Owner.release() != nullptr);
}

bool FixItRewriter::WriteFixedFile(FileID ID, raw_ostream &OS) {
  const RewriteBuffer *RewriteBuf = Rewrite.getRewriteBufferFor(ID);
  if (!RewriteBuf) return true;
  RewriteBuf->write(OS);
  OS.flush();
  return false;
}

namespace {

class RewritesReceiver : public edit::EditsReceiver {
  Rewriter &Rewrite;

public:
  RewritesReceiver(Rewriter &Rewrite) : Rewrite(Rewrite) {}

  void insert(SourceLocation loc, StringRef text) override {
    Rewrite.InsertText(loc, text);
  }

  void replace(CharSourceRange range, StringRef text) override {
    Rewrite.ReplaceText(range.getBegin(), Rewrite.getRangeSize(range), text);
  }
};

} // namespace

bool FixItRewriter::WriteFixedFiles(
             std::vector<std::pair<std::string, std::string>> *RewrittenFiles) {
  if (NumFailures > 0 && !FixItOpts->FixWhatYouCan) {
    Diag(FullSourceLoc(), diag::warn_fixit_no_changes);
    return true;
  }

  RewritesReceiver Rec(Rewrite);
  Editor.applyRewrites(Rec);

  if (FixItOpts->InPlace) {
    // Overwriting open files on Windows is tricky, but the rewriter can do it
    // for us.
    Rewrite.overwriteChangedFiles();
    return false;
  }

  for (iterator I = buffer_begin(), E = buffer_end(); I != E; ++I) {
    const FileEntry *Entry = Rewrite.getSourceMgr().getFileEntryForID(I->first);
    int fd;
    std::string Filename = FixItOpts->RewriteFilename(Entry->getName(), fd);
    std::error_code EC;
    std::unique_ptr<llvm::raw_fd_ostream> OS;
    if (fd != -1) {
      OS.reset(new llvm::raw_fd_ostream(fd, /*shouldClose=*/true));
    } else {
      OS.reset(new llvm::raw_fd_ostream(Filename, EC, llvm::sys::fs::F_None));
    }
    if (EC) {
      Diags.Report(clang::diag::err_fe_unable_to_open_output) << Filename
                                                              << EC.message();
      continue;
    }
    RewriteBuffer &RewriteBuf = I->second;
    RewriteBuf.write(*OS);
    OS->flush();

    if (RewrittenFiles)
      RewrittenFiles->push_back(std::make_pair(Entry->getName(), Filename));
  }

  return false;
}

bool FixItRewriter::IncludeInDiagnosticCounts() const {
  return Client ? Client->IncludeInDiagnosticCounts() : true;
}

void FixItRewriter::HandleDiagnostic(DiagnosticsEngine::Level DiagLevel,
                                     const Diagnostic &Info) {
  // Default implementation (Warnings/errors count).
  DiagnosticConsumer::HandleDiagnostic(DiagLevel, Info);

  if (!FixItOpts->Silent ||
      DiagLevel >= DiagnosticsEngine::Error ||
      (DiagLevel == DiagnosticsEngine::Note && !PrevDiagSilenced) ||
      (DiagLevel > DiagnosticsEngine::Note && Info.getNumFixItHints())) {
    Client->HandleDiagnostic(DiagLevel, Info);
    PrevDiagSilenced = false;
  } else {
    PrevDiagSilenced = true;
  }

  // Skip over any diagnostics that are ignored or notes.
  if (DiagLevel <= DiagnosticsEngine::Note)
    return;
  // Skip over errors if we are only fixing warnings.
  if (DiagLevel >= DiagnosticsEngine::Error && FixItOpts->FixOnlyWarnings) {
    ++NumFailures;
    return;
  }

  // Make sure that we can perform all of the modifications we
  // in this diagnostic.
  edit::Commit commit(Editor);
  for (unsigned Idx = 0, Last = Info.getNumFixItHints();
       Idx < Last; ++Idx) {
    const FixItHint &Hint = Info.getFixItHint(Idx);

    if (Hint.CodeToInsert.empty()) {
      if (Hint.InsertFromRange.isValid())
        commit.insertFromRange(Hint.RemoveRange.getBegin(),
                           Hint.InsertFromRange, /*afterToken=*/false,
                           Hint.BeforePreviousInsertions);
      else
        commit.remove(Hint.RemoveRange);
    } else {
      if (Hint.RemoveRange.isTokenRange() ||
          Hint.RemoveRange.getBegin() != Hint.RemoveRange.getEnd())
        commit.replace(Hint.RemoveRange, Hint.CodeToInsert);
      else
        commit.insert(Hint.RemoveRange.getBegin(), Hint.CodeToInsert,
                    /*afterToken=*/false, Hint.BeforePreviousInsertions);
    }
  }
  bool CanRewrite = Info.getNumFixItHints() > 0 && commit.isCommitable();

  if (!CanRewrite) {
    if (Info.getNumFixItHints() > 0)
      Diag(Info.getLocation(), diag::note_fixit_in_macro);

    // If this was an error, refuse to perform any rewriting.
    if (DiagLevel >= DiagnosticsEngine::Error) {
      if (++NumFailures == 1)
        Diag(Info.getLocation(), diag::note_fixit_unfixed_error);
    }
    return;
  }

  if (!Editor.commit(commit)) {
    ++NumFailures;
    Diag(Info.getLocation(), diag::note_fixit_failed);
    return;
  }

  Diag(Info.getLocation(), diag::note_fixit_applied);
}

/// Emit a diagnostic via the adapted diagnostic client.
void FixItRewriter::Diag(SourceLocation Loc, unsigned DiagID) {
  // When producing this diagnostic, we temporarily bypass ourselves,
  // clear out any current diagnostic, and let the downstream client
  // format the diagnostic.
  Diags.setClient(Client, false);
  Diags.Clear();
  Diags.Report(Loc, DiagID);
  Diags.setClient(this, false);
}

FixItOptions::~FixItOptions() = default;
