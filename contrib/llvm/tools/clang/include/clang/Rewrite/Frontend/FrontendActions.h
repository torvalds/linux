//===-- FrontendActions.h - Useful Frontend Actions -------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_REWRITE_FRONTEND_FRONTENDACTIONS_H
#define LLVM_CLANG_REWRITE_FRONTEND_FRONTENDACTIONS_H

#include "clang/Frontend/FrontendAction.h"
#include "llvm/Support/raw_ostream.h"

namespace clang {
class FixItRewriter;
class FixItOptions;

//===----------------------------------------------------------------------===//
// AST Consumer Actions
//===----------------------------------------------------------------------===//

class HTMLPrintAction : public ASTFrontendAction {
protected:
  std::unique_ptr<ASTConsumer> CreateASTConsumer(CompilerInstance &CI,
                                                 StringRef InFile) override;
};

class FixItAction : public ASTFrontendAction {
protected:
  std::unique_ptr<FixItRewriter> Rewriter;
  std::unique_ptr<FixItOptions> FixItOpts;

  std::unique_ptr<ASTConsumer> CreateASTConsumer(CompilerInstance &CI,
                                                 StringRef InFile) override;

  bool BeginSourceFileAction(CompilerInstance &CI) override;

  void EndSourceFileAction() override;

  bool hasASTFileSupport() const override { return false; }

public:
  FixItAction();
  ~FixItAction() override;
};

/// Emits changes to temporary files and uses them for the original
/// frontend action.
class FixItRecompile : public WrapperFrontendAction {
public:
  FixItRecompile(std::unique_ptr<FrontendAction> WrappedAction)
    : WrapperFrontendAction(std::move(WrappedAction)) {}

protected:
  bool BeginInvocation(CompilerInstance &CI) override;
};

class RewriteObjCAction : public ASTFrontendAction {
protected:
  std::unique_ptr<ASTConsumer> CreateASTConsumer(CompilerInstance &CI,
                                                 StringRef InFile) override;
};

class RewriteMacrosAction : public PreprocessorFrontendAction {
protected:
  void ExecuteAction() override;
};

class RewriteTestAction : public PreprocessorFrontendAction {
protected:
  void ExecuteAction() override;
};

class RewriteIncludesAction : public PreprocessorFrontendAction {
  std::shared_ptr<raw_ostream> OutputStream;
  class RewriteImportsListener;
protected:
  bool BeginSourceFileAction(CompilerInstance &CI) override;
  void ExecuteAction() override;
};

}  // end namespace clang

#endif
