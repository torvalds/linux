//===--- FrontendActions.cpp ----------------------------------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "clang/Rewrite/Frontend/FrontendActions.h"
#include "clang/AST/ASTConsumer.h"
#include "clang/Basic/CharInfo.h"
#include "clang/Config/config.h"
#include "clang/Frontend/CompilerInstance.h"
#include "clang/Frontend/FrontendActions.h"
#include "clang/Frontend/FrontendDiagnostic.h"
#include "clang/Frontend/Utils.h"
#include "clang/Lex/Preprocessor.h"
#include "clang/Lex/PreprocessorOptions.h"
#include "clang/Rewrite/Frontend/ASTConsumers.h"
#include "clang/Rewrite/Frontend/FixItRewriter.h"
#include "clang/Rewrite/Frontend/Rewriters.h"
#include "clang/Serialization/ASTReader.h"
#include "clang/Serialization/Module.h"
#include "clang/Serialization/ModuleManager.h"
#include "llvm/ADT/DenseSet.h"
#include "llvm/Support/CrashRecoveryContext.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/Path.h"
#include "llvm/Support/raw_ostream.h"
#include <memory>
#include <utility>

using namespace clang;

//===----------------------------------------------------------------------===//
// AST Consumer Actions
//===----------------------------------------------------------------------===//

std::unique_ptr<ASTConsumer>
HTMLPrintAction::CreateASTConsumer(CompilerInstance &CI, StringRef InFile) {
  if (std::unique_ptr<raw_ostream> OS =
          CI.createDefaultOutputFile(false, InFile))
    return CreateHTMLPrinter(std::move(OS), CI.getPreprocessor());
  return nullptr;
}

FixItAction::FixItAction() {}
FixItAction::~FixItAction() {}

std::unique_ptr<ASTConsumer>
FixItAction::CreateASTConsumer(CompilerInstance &CI, StringRef InFile) {
  return llvm::make_unique<ASTConsumer>();
}

namespace {
class FixItRewriteInPlace : public FixItOptions {
public:
  FixItRewriteInPlace() { InPlace = true; }

  std::string RewriteFilename(const std::string &Filename, int &fd) override {
    llvm_unreachable("don't call RewriteFilename for inplace rewrites");
  }
};

class FixItActionSuffixInserter : public FixItOptions {
  std::string NewSuffix;

public:
  FixItActionSuffixInserter(std::string NewSuffix, bool FixWhatYouCan)
      : NewSuffix(std::move(NewSuffix)) {
    this->FixWhatYouCan = FixWhatYouCan;
  }

  std::string RewriteFilename(const std::string &Filename, int &fd) override {
    fd = -1;
    SmallString<128> Path(Filename);
    llvm::sys::path::replace_extension(Path,
      NewSuffix + llvm::sys::path::extension(Path));
    return Path.str();
  }
};

class FixItRewriteToTemp : public FixItOptions {
public:
  std::string RewriteFilename(const std::string &Filename, int &fd) override {
    SmallString<128> Path;
    llvm::sys::fs::createTemporaryFile(llvm::sys::path::filename(Filename),
                                       llvm::sys::path::extension(Filename).drop_front(), fd,
                                       Path);
    return Path.str();
  }
};
} // end anonymous namespace

bool FixItAction::BeginSourceFileAction(CompilerInstance &CI) {
  const FrontendOptions &FEOpts = getCompilerInstance().getFrontendOpts();
  if (!FEOpts.FixItSuffix.empty()) {
    FixItOpts.reset(new FixItActionSuffixInserter(FEOpts.FixItSuffix,
                                                  FEOpts.FixWhatYouCan));
  } else {
    FixItOpts.reset(new FixItRewriteInPlace);
    FixItOpts->FixWhatYouCan = FEOpts.FixWhatYouCan;
  }
  Rewriter.reset(new FixItRewriter(CI.getDiagnostics(), CI.getSourceManager(),
                                   CI.getLangOpts(), FixItOpts.get()));
  return true;
}

void FixItAction::EndSourceFileAction() {
  // Otherwise rewrite all files.
  Rewriter->WriteFixedFiles();
}

bool FixItRecompile::BeginInvocation(CompilerInstance &CI) {

  std::vector<std::pair<std::string, std::string> > RewrittenFiles;
  bool err = false;
  {
    const FrontendOptions &FEOpts = CI.getFrontendOpts();
    std::unique_ptr<FrontendAction> FixAction(new SyntaxOnlyAction());
    if (FixAction->BeginSourceFile(CI, FEOpts.Inputs[0])) {
      std::unique_ptr<FixItOptions> FixItOpts;
      if (FEOpts.FixToTemporaries)
        FixItOpts.reset(new FixItRewriteToTemp());
      else
        FixItOpts.reset(new FixItRewriteInPlace());
      FixItOpts->Silent = true;
      FixItOpts->FixWhatYouCan = FEOpts.FixWhatYouCan;
      FixItOpts->FixOnlyWarnings = FEOpts.FixOnlyWarnings;
      FixItRewriter Rewriter(CI.getDiagnostics(), CI.getSourceManager(),
                             CI.getLangOpts(), FixItOpts.get());
      FixAction->Execute();

      err = Rewriter.WriteFixedFiles(&RewrittenFiles);

      FixAction->EndSourceFile();
      CI.setSourceManager(nullptr);
      CI.setFileManager(nullptr);
    } else {
      err = true;
    }
  }
  if (err)
    return false;
  CI.getDiagnosticClient().clear();
  CI.getDiagnostics().Reset();

  PreprocessorOptions &PPOpts = CI.getPreprocessorOpts();
  PPOpts.RemappedFiles.insert(PPOpts.RemappedFiles.end(),
                              RewrittenFiles.begin(), RewrittenFiles.end());
  PPOpts.RemappedFilesKeepOriginalName = false;

  return true;
}

#if CLANG_ENABLE_OBJC_REWRITER

std::unique_ptr<ASTConsumer>
RewriteObjCAction::CreateASTConsumer(CompilerInstance &CI, StringRef InFile) {
  if (std::unique_ptr<raw_ostream> OS =
          CI.createDefaultOutputFile(false, InFile, "cpp")) {
    if (CI.getLangOpts().ObjCRuntime.isNonFragile())
      return CreateModernObjCRewriter(
          InFile, std::move(OS), CI.getDiagnostics(), CI.getLangOpts(),
          CI.getDiagnosticOpts().NoRewriteMacros,
          (CI.getCodeGenOpts().getDebugInfo() != codegenoptions::NoDebugInfo));
    return CreateObjCRewriter(InFile, std::move(OS), CI.getDiagnostics(),
                              CI.getLangOpts(),
                              CI.getDiagnosticOpts().NoRewriteMacros);
  }
  return nullptr;
}

#endif

//===----------------------------------------------------------------------===//
// Preprocessor Actions
//===----------------------------------------------------------------------===//

void RewriteMacrosAction::ExecuteAction() {
  CompilerInstance &CI = getCompilerInstance();
  std::unique_ptr<raw_ostream> OS =
      CI.createDefaultOutputFile(true, getCurrentFileOrBufferName());
  if (!OS) return;

  RewriteMacrosInInput(CI.getPreprocessor(), OS.get());
}

void RewriteTestAction::ExecuteAction() {
  CompilerInstance &CI = getCompilerInstance();
  std::unique_ptr<raw_ostream> OS =
      CI.createDefaultOutputFile(false, getCurrentFileOrBufferName());
  if (!OS) return;

  DoRewriteTest(CI.getPreprocessor(), OS.get());
}

class RewriteIncludesAction::RewriteImportsListener : public ASTReaderListener {
  CompilerInstance &CI;
  std::weak_ptr<raw_ostream> Out;

  llvm::DenseSet<const FileEntry*> Rewritten;

public:
  RewriteImportsListener(CompilerInstance &CI, std::shared_ptr<raw_ostream> Out)
      : CI(CI), Out(Out) {}

  void visitModuleFile(StringRef Filename,
                       serialization::ModuleKind Kind) override {
    auto *File = CI.getFileManager().getFile(Filename);
    assert(File && "missing file for loaded module?");

    // Only rewrite each module file once.
    if (!Rewritten.insert(File).second)
      return;

    serialization::ModuleFile *MF =
        CI.getModuleManager()->getModuleManager().lookup(File);
    assert(File && "missing module file for loaded module?");

    // Not interested in PCH / preambles.
    if (!MF->isModule())
      return;

    auto OS = Out.lock();
    assert(OS && "loaded module file after finishing rewrite action?");

    (*OS) << "#pragma clang module build ";
    if (isValidIdentifier(MF->ModuleName))
      (*OS) << MF->ModuleName;
    else {
      (*OS) << '"';
      OS->write_escaped(MF->ModuleName);
      (*OS) << '"';
    }
    (*OS) << '\n';

    // Rewrite the contents of the module in a separate compiler instance.
    CompilerInstance Instance(CI.getPCHContainerOperations(),
                              &CI.getPreprocessor().getPCMCache());
    Instance.setInvocation(
        std::make_shared<CompilerInvocation>(CI.getInvocation()));
    Instance.createDiagnostics(
        new ForwardingDiagnosticConsumer(CI.getDiagnosticClient()),
        /*ShouldOwnClient=*/true);
    Instance.getFrontendOpts().DisableFree = false;
    Instance.getFrontendOpts().Inputs.clear();
    Instance.getFrontendOpts().Inputs.emplace_back(
        Filename, InputKind(InputKind::Unknown, InputKind::Precompiled));
    Instance.getFrontendOpts().ModuleFiles.clear();
    Instance.getFrontendOpts().ModuleMapFiles.clear();
    // Don't recursively rewrite imports. We handle them all at the top level.
    Instance.getPreprocessorOutputOpts().RewriteImports = false;

    llvm::CrashRecoveryContext().RunSafelyOnThread([&]() {
      RewriteIncludesAction Action;
      Action.OutputStream = OS;
      Instance.ExecuteAction(Action);
    });

    (*OS) << "#pragma clang module endbuild /*" << MF->ModuleName << "*/\n";
  }
};

bool RewriteIncludesAction::BeginSourceFileAction(CompilerInstance &CI) {
  if (!OutputStream) {
    OutputStream =
        CI.createDefaultOutputFile(true, getCurrentFileOrBufferName());
    if (!OutputStream)
      return false;
  }

  auto &OS = *OutputStream;

  // If we're preprocessing a module map, start by dumping the contents of the
  // module itself before switching to the input buffer.
  auto &Input = getCurrentInput();
  if (Input.getKind().getFormat() == InputKind::ModuleMap) {
    if (Input.isFile()) {
      OS << "# 1 \"";
      OS.write_escaped(Input.getFile());
      OS << "\"\n";
    }
    getCurrentModule()->print(OS);
    OS << "#pragma clang module contents\n";
  }

  // If we're rewriting imports, set up a listener to track when we import
  // module files.
  if (CI.getPreprocessorOutputOpts().RewriteImports) {
    CI.createModuleManager();
    CI.getModuleManager()->addListener(
        llvm::make_unique<RewriteImportsListener>(CI, OutputStream));
  }

  return true;
}

void RewriteIncludesAction::ExecuteAction() {
  CompilerInstance &CI = getCompilerInstance();

  // If we're rewriting imports, emit the module build output first rather
  // than switching back and forth (potentially in the middle of a line).
  if (CI.getPreprocessorOutputOpts().RewriteImports) {
    std::string Buffer;
    llvm::raw_string_ostream OS(Buffer);

    RewriteIncludesInInput(CI.getPreprocessor(), &OS,
                           CI.getPreprocessorOutputOpts());

    (*OutputStream) << OS.str();
  } else {
    RewriteIncludesInInput(CI.getPreprocessor(), OutputStream.get(),
                           CI.getPreprocessorOutputOpts());
  }

  OutputStream.reset();
}
