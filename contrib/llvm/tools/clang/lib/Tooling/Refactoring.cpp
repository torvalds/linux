//===--- Refactoring.cpp - Framework for clang refactoring tools ----------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
//  Implements tools to support refactorings.
//
//===----------------------------------------------------------------------===//

#include "clang/Tooling/Refactoring.h"
#include "clang/Basic/DiagnosticOptions.h"
#include "clang/Basic/FileManager.h"
#include "clang/Basic/SourceManager.h"
#include "clang/Format/Format.h"
#include "clang/Frontend/TextDiagnosticPrinter.h"
#include "clang/Lex/Lexer.h"
#include "clang/Rewrite/Core/Rewriter.h"
#include "llvm/Support/Path.h"
#include "llvm/Support/raw_os_ostream.h"

namespace clang {
namespace tooling {

RefactoringTool::RefactoringTool(
    const CompilationDatabase &Compilations, ArrayRef<std::string> SourcePaths,
    std::shared_ptr<PCHContainerOperations> PCHContainerOps)
    : ClangTool(Compilations, SourcePaths, std::move(PCHContainerOps)) {}

std::map<std::string, Replacements> &RefactoringTool::getReplacements() {
  return FileToReplaces;
}

int RefactoringTool::runAndSave(FrontendActionFactory *ActionFactory) {
  if (int Result = run(ActionFactory)) {
    return Result;
  }

  LangOptions DefaultLangOptions;
  IntrusiveRefCntPtr<DiagnosticOptions> DiagOpts = new DiagnosticOptions();
  TextDiagnosticPrinter DiagnosticPrinter(llvm::errs(), &*DiagOpts);
  DiagnosticsEngine Diagnostics(
      IntrusiveRefCntPtr<DiagnosticIDs>(new DiagnosticIDs()),
      &*DiagOpts, &DiagnosticPrinter, false);
  SourceManager Sources(Diagnostics, getFiles());
  Rewriter Rewrite(Sources, DefaultLangOptions);

  if (!applyAllReplacements(Rewrite)) {
    llvm::errs() << "Skipped some replacements.\n";
  }

  return saveRewrittenFiles(Rewrite);
}

bool RefactoringTool::applyAllReplacements(Rewriter &Rewrite) {
  bool Result = true;
  for (const auto &Entry : groupReplacementsByFile(
           Rewrite.getSourceMgr().getFileManager(), FileToReplaces))
    Result = tooling::applyAllReplacements(Entry.second, Rewrite) && Result;
  return Result;
}

int RefactoringTool::saveRewrittenFiles(Rewriter &Rewrite) {
  return Rewrite.overwriteChangedFiles() ? 1 : 0;
}

bool formatAndApplyAllReplacements(
    const std::map<std::string, Replacements> &FileToReplaces,
    Rewriter &Rewrite, StringRef Style) {
  SourceManager &SM = Rewrite.getSourceMgr();
  FileManager &Files = SM.getFileManager();

  bool Result = true;
  for (const auto &FileAndReplaces : groupReplacementsByFile(
           Rewrite.getSourceMgr().getFileManager(), FileToReplaces)) {
    const std::string &FilePath = FileAndReplaces.first;
    auto &CurReplaces = FileAndReplaces.second;

    const FileEntry *Entry = Files.getFile(FilePath);
    FileID ID = SM.getOrCreateFileID(Entry, SrcMgr::C_User);
    StringRef Code = SM.getBufferData(ID);

    auto CurStyle = format::getStyle(Style, FilePath, "LLVM");
    if (!CurStyle) {
      llvm::errs() << llvm::toString(CurStyle.takeError()) << "\n";
      return false;
    }

    auto NewReplacements =
        format::formatReplacements(Code, CurReplaces, *CurStyle);
    if (!NewReplacements) {
      llvm::errs() << llvm::toString(NewReplacements.takeError()) << "\n";
      return false;
    }
    Result = applyAllReplacements(*NewReplacements, Rewrite) && Result;
  }
  return Result;
}

} // end namespace tooling
} // end namespace clang
