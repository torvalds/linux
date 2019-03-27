//===- IndexingAction.cpp - Frontend index action -------------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "clang/Index/IndexingAction.h"
#include "IndexingContext.h"
#include "clang/Frontend/CompilerInstance.h"
#include "clang/Frontend/FrontendAction.h"
#include "clang/Frontend/MultiplexConsumer.h"
#include "clang/Index/IndexDataConsumer.h"
#include "clang/Lex/PPCallbacks.h"
#include "clang/Lex/Preprocessor.h"
#include "clang/Serialization/ASTReader.h"
#include "llvm/ADT/STLExtras.h"
#include <memory>

using namespace clang;
using namespace clang::index;

bool IndexDataConsumer::handleDeclOccurence(const Decl *D, SymbolRoleSet Roles,
                                            ArrayRef<SymbolRelation> Relations,
                                            SourceLocation Loc,
                                            ASTNodeInfo ASTNode) {
  return true;
}

bool IndexDataConsumer::handleMacroOccurence(const IdentifierInfo *Name,
                                             const MacroInfo *MI,
                                             SymbolRoleSet Roles,
                                             SourceLocation Loc) {
  return true;
}

bool IndexDataConsumer::handleModuleOccurence(const ImportDecl *ImportD,
                                              const Module *Mod,
                                              SymbolRoleSet Roles,
                                              SourceLocation Loc) {
  return true;
}

namespace {

class IndexASTConsumer : public ASTConsumer {
  std::shared_ptr<Preprocessor> PP;
  std::shared_ptr<IndexingContext> IndexCtx;

public:
  IndexASTConsumer(std::shared_ptr<Preprocessor> PP,
                   std::shared_ptr<IndexingContext> IndexCtx)
      : PP(std::move(PP)), IndexCtx(std::move(IndexCtx)) {}

protected:
  void Initialize(ASTContext &Context) override {
    IndexCtx->setASTContext(Context);
    IndexCtx->getDataConsumer().initialize(Context);
    IndexCtx->getDataConsumer().setPreprocessor(PP);
  }

  bool HandleTopLevelDecl(DeclGroupRef DG) override {
    return IndexCtx->indexDeclGroupRef(DG);
  }

  void HandleInterestingDecl(DeclGroupRef DG) override {
    // Ignore deserialized decls.
  }

  void HandleTopLevelDeclInObjCContainer(DeclGroupRef DG) override {
    IndexCtx->indexDeclGroupRef(DG);
  }

  void HandleTranslationUnit(ASTContext &Ctx) override {
  }
};

class IndexPPCallbacks : public PPCallbacks {
  std::shared_ptr<IndexingContext> IndexCtx;

public:
  IndexPPCallbacks(std::shared_ptr<IndexingContext> IndexCtx)
      : IndexCtx(std::move(IndexCtx)) {}

  void MacroExpands(const Token &MacroNameTok, const MacroDefinition &MD,
                    SourceRange Range, const MacroArgs *Args) override {
    IndexCtx->handleMacroReference(*MacroNameTok.getIdentifierInfo(),
                                   Range.getBegin(), *MD.getMacroInfo());
  }

  void MacroDefined(const Token &MacroNameTok,
                    const MacroDirective *MD) override {
    IndexCtx->handleMacroDefined(*MacroNameTok.getIdentifierInfo(),
                                 MacroNameTok.getLocation(),
                                 *MD->getMacroInfo());
  }

  void MacroUndefined(const Token &MacroNameTok, const MacroDefinition &MD,
                      const MacroDirective *Undef) override {
    if (!MD.getMacroInfo())  // Ignore noop #undef.
      return;
    IndexCtx->handleMacroUndefined(*MacroNameTok.getIdentifierInfo(),
                                   MacroNameTok.getLocation(),
                                   *MD.getMacroInfo());
  }
};

class IndexActionBase {
protected:
  std::shared_ptr<IndexDataConsumer> DataConsumer;
  std::shared_ptr<IndexingContext> IndexCtx;

  IndexActionBase(std::shared_ptr<IndexDataConsumer> dataConsumer,
                  IndexingOptions Opts)
      : DataConsumer(std::move(dataConsumer)),
        IndexCtx(new IndexingContext(Opts, *DataConsumer)) {}

  std::unique_ptr<IndexASTConsumer>
  createIndexASTConsumer(CompilerInstance &CI) {
    return llvm::make_unique<IndexASTConsumer>(CI.getPreprocessorPtr(),
                                               IndexCtx);
  }

  std::unique_ptr<PPCallbacks> createIndexPPCallbacks() {
    return llvm::make_unique<IndexPPCallbacks>(IndexCtx);
  }

  void finish() {
    DataConsumer->finish();
  }
};

class IndexAction : public ASTFrontendAction, IndexActionBase {
public:
  IndexAction(std::shared_ptr<IndexDataConsumer> DataConsumer,
              IndexingOptions Opts)
    : IndexActionBase(std::move(DataConsumer), Opts) {}

protected:
  std::unique_ptr<ASTConsumer> CreateASTConsumer(CompilerInstance &CI,
                                                 StringRef InFile) override {
    return createIndexASTConsumer(CI);
  }

  bool BeginSourceFileAction(clang::CompilerInstance &CI) override {
    CI.getPreprocessor().addPPCallbacks(createIndexPPCallbacks());
    return true;
  }

  void EndSourceFileAction() override {
    FrontendAction::EndSourceFileAction();
    finish();
  }
};

class WrappingIndexAction : public WrapperFrontendAction, IndexActionBase {
  bool IndexActionFailed = false;

public:
  WrappingIndexAction(std::unique_ptr<FrontendAction> WrappedAction,
                      std::shared_ptr<IndexDataConsumer> DataConsumer,
                      IndexingOptions Opts)
    : WrapperFrontendAction(std::move(WrappedAction)),
      IndexActionBase(std::move(DataConsumer), Opts) {}

protected:
  std::unique_ptr<ASTConsumer> CreateASTConsumer(CompilerInstance &CI,
                                                 StringRef InFile) override {
    auto OtherConsumer = WrapperFrontendAction::CreateASTConsumer(CI, InFile);
    if (!OtherConsumer) {
      IndexActionFailed = true;
      return nullptr;
    }

    std::vector<std::unique_ptr<ASTConsumer>> Consumers;
    Consumers.push_back(std::move(OtherConsumer));
    Consumers.push_back(createIndexASTConsumer(CI));
    return llvm::make_unique<MultiplexConsumer>(std::move(Consumers));
  }

  bool BeginSourceFileAction(clang::CompilerInstance &CI) override {
    WrapperFrontendAction::BeginSourceFileAction(CI);
    CI.getPreprocessor().addPPCallbacks(createIndexPPCallbacks());
    return true;
  }

  void EndSourceFileAction() override {
    // Invoke wrapped action's method.
    WrapperFrontendAction::EndSourceFileAction();
    if (!IndexActionFailed)
      finish();
  }
};

} // anonymous namespace

std::unique_ptr<FrontendAction>
index::createIndexingAction(std::shared_ptr<IndexDataConsumer> DataConsumer,
                            IndexingOptions Opts,
                            std::unique_ptr<FrontendAction> WrappedAction) {
  if (WrappedAction)
    return llvm::make_unique<WrappingIndexAction>(std::move(WrappedAction),
                                                  std::move(DataConsumer),
                                                  Opts);
  return llvm::make_unique<IndexAction>(std::move(DataConsumer), Opts);
}

static bool topLevelDeclVisitor(void *context, const Decl *D) {
  IndexingContext &IndexCtx = *static_cast<IndexingContext*>(context);
  return IndexCtx.indexTopLevelDecl(D);
}

static void indexTranslationUnit(ASTUnit &Unit, IndexingContext &IndexCtx) {
  Unit.visitLocalTopLevelDecls(&IndexCtx, topLevelDeclVisitor);
}

static void indexPreprocessorMacros(const Preprocessor &PP,
                                    IndexDataConsumer &DataConsumer) {
  for (const auto &M : PP.macros())
    if (MacroDirective *MD = M.second.getLatest())
      DataConsumer.handleMacroOccurence(
          M.first, MD->getMacroInfo(),
          static_cast<unsigned>(index::SymbolRole::Definition),
          MD->getLocation());
}

void index::indexASTUnit(ASTUnit &Unit, IndexDataConsumer &DataConsumer,
                         IndexingOptions Opts) {
  IndexingContext IndexCtx(Opts, DataConsumer);
  IndexCtx.setASTContext(Unit.getASTContext());
  DataConsumer.initialize(Unit.getASTContext());
  DataConsumer.setPreprocessor(Unit.getPreprocessorPtr());

  if (Opts.IndexMacrosInPreprocessor)
    indexPreprocessorMacros(Unit.getPreprocessor(), DataConsumer);
  indexTranslationUnit(Unit, IndexCtx);
  DataConsumer.finish();
}

void index::indexTopLevelDecls(ASTContext &Ctx, Preprocessor &PP,
                               ArrayRef<const Decl *> Decls,
                               IndexDataConsumer &DataConsumer,
                               IndexingOptions Opts) {
  IndexingContext IndexCtx(Opts, DataConsumer);
  IndexCtx.setASTContext(Ctx);

  DataConsumer.initialize(Ctx);

  if (Opts.IndexMacrosInPreprocessor)
    indexPreprocessorMacros(PP, DataConsumer);

  for (const Decl *D : Decls)
    IndexCtx.indexTopLevelDecl(D);
  DataConsumer.finish();
}

std::unique_ptr<PPCallbacks>
index::indexMacrosCallback(IndexDataConsumer &Consumer, IndexingOptions Opts) {
  return llvm::make_unique<IndexPPCallbacks>(
      std::make_shared<IndexingContext>(Opts, Consumer));
}

void index::indexModuleFile(serialization::ModuleFile &Mod, ASTReader &Reader,
                            IndexDataConsumer &DataConsumer,
                            IndexingOptions Opts) {
  ASTContext &Ctx = Reader.getContext();
  IndexingContext IndexCtx(Opts, DataConsumer);
  IndexCtx.setASTContext(Ctx);
  DataConsumer.initialize(Ctx);

  if (Opts.IndexMacrosInPreprocessor)
    indexPreprocessorMacros(Reader.getPreprocessor(), DataConsumer);

  for (const Decl *D : Reader.getModuleFileLevelDecls(Mod)) {
    IndexCtx.indexTopLevelDecl(D);
  }
  DataConsumer.finish();
}
