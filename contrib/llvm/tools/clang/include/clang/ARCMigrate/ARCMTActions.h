//===--- ARCMTActions.h - ARC Migrate Tool Frontend Actions -----*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_ARCMIGRATE_ARCMTACTIONS_H
#define LLVM_CLANG_ARCMIGRATE_ARCMTACTIONS_H

#include "clang/ARCMigrate/FileRemapper.h"
#include "clang/Frontend/FrontendAction.h"
#include <memory>

namespace clang {
namespace arcmt {

class CheckAction : public WrapperFrontendAction {
protected:
  bool BeginInvocation(CompilerInstance &CI) override;

public:
  CheckAction(std::unique_ptr<FrontendAction> WrappedAction);
};

class ModifyAction : public WrapperFrontendAction {
protected:
  bool BeginInvocation(CompilerInstance &CI) override;

public:
  ModifyAction(std::unique_ptr<FrontendAction> WrappedAction);
};

class MigrateSourceAction : public ASTFrontendAction {
  FileRemapper Remapper;
protected:
  bool BeginInvocation(CompilerInstance &CI) override;
  std::unique_ptr<ASTConsumer> CreateASTConsumer(CompilerInstance &CI,
                                                 StringRef InFile) override;
};

class MigrateAction : public WrapperFrontendAction {
  std::string MigrateDir;
  std::string PlistOut;
  bool EmitPremigrationARCErros;
protected:
  bool BeginInvocation(CompilerInstance &CI) override;

public:
  MigrateAction(std::unique_ptr<FrontendAction> WrappedAction,
                StringRef migrateDir,
                StringRef plistOut,
                bool emitPremigrationARCErrors);
};

/// Migrates to modern ObjC syntax.
class ObjCMigrateAction : public WrapperFrontendAction {
  std::string MigrateDir;
  unsigned    ObjCMigAction;
  FileRemapper Remapper;
  CompilerInstance *CompInst;
public:
  ObjCMigrateAction(std::unique_ptr<FrontendAction> WrappedAction,
                    StringRef migrateDir, unsigned migrateAction);

protected:
  std::unique_ptr<ASTConsumer> CreateASTConsumer(CompilerInstance &CI,
                                                 StringRef InFile) override;
  bool BeginInvocation(CompilerInstance &CI) override;
};

}
}

#endif
