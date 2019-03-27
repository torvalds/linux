//===--- USRFindingAction.cpp - Clang refactoring library -----------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
///
/// \file
/// Provides an action to find USR for the symbol at <offset>, as well as
/// all additional USRs.
///
//===----------------------------------------------------------------------===//

#include "clang/Tooling/Refactoring/Rename/USRFindingAction.h"
#include "clang/AST/AST.h"
#include "clang/AST/ASTConsumer.h"
#include "clang/AST/ASTContext.h"
#include "clang/AST/Decl.h"
#include "clang/AST/RecursiveASTVisitor.h"
#include "clang/Basic/FileManager.h"
#include "clang/Frontend/CompilerInstance.h"
#include "clang/Frontend/FrontendAction.h"
#include "clang/Lex/Lexer.h"
#include "clang/Lex/Preprocessor.h"
#include "clang/Tooling/CommonOptionsParser.h"
#include "clang/Tooling/Refactoring.h"
#include "clang/Tooling/Refactoring/Rename/USRFinder.h"
#include "clang/Tooling/Tooling.h"

#include <algorithm>
#include <set>
#include <string>
#include <vector>

using namespace llvm;

namespace clang {
namespace tooling {

const NamedDecl *getCanonicalSymbolDeclaration(const NamedDecl *FoundDecl) {
  // If FoundDecl is a constructor or destructor, we want to instead take
  // the Decl of the corresponding class.
  if (const auto *CtorDecl = dyn_cast<CXXConstructorDecl>(FoundDecl))
    FoundDecl = CtorDecl->getParent();
  else if (const auto *DtorDecl = dyn_cast<CXXDestructorDecl>(FoundDecl))
    FoundDecl = DtorDecl->getParent();
  // FIXME: (Alex L): Canonicalize implicit template instantions, just like
  // the indexer does it.

  // Note: please update the declaration's doc comment every time the
  // canonicalization rules are changed.
  return FoundDecl;
}

namespace {
// NamedDeclFindingConsumer should delegate finding USRs of given Decl to
// AdditionalUSRFinder. AdditionalUSRFinder adds USRs of ctor and dtor if given
// Decl refers to class and adds USRs of all overridden methods if Decl refers
// to virtual method.
class AdditionalUSRFinder : public RecursiveASTVisitor<AdditionalUSRFinder> {
public:
  AdditionalUSRFinder(const Decl *FoundDecl, ASTContext &Context)
      : FoundDecl(FoundDecl), Context(Context) {}

  std::vector<std::string> Find() {
    // Fill OverriddenMethods and PartialSpecs storages.
    TraverseDecl(Context.getTranslationUnitDecl());
    if (const auto *MethodDecl = dyn_cast<CXXMethodDecl>(FoundDecl)) {
      addUSRsOfOverridenFunctions(MethodDecl);
      for (const auto &OverriddenMethod : OverriddenMethods) {
        if (checkIfOverriddenFunctionAscends(OverriddenMethod))
          USRSet.insert(getUSRForDecl(OverriddenMethod));
      }
      addUSRsOfInstantiatedMethods(MethodDecl);
    } else if (const auto *RecordDecl = dyn_cast<CXXRecordDecl>(FoundDecl)) {
      handleCXXRecordDecl(RecordDecl);
    } else if (const auto *TemplateDecl =
                   dyn_cast<ClassTemplateDecl>(FoundDecl)) {
      handleClassTemplateDecl(TemplateDecl);
    } else {
      USRSet.insert(getUSRForDecl(FoundDecl));
    }
    return std::vector<std::string>(USRSet.begin(), USRSet.end());
  }

  bool shouldVisitTemplateInstantiations() const { return true; }

  bool VisitCXXMethodDecl(const CXXMethodDecl *MethodDecl) {
    if (MethodDecl->isVirtual())
      OverriddenMethods.push_back(MethodDecl);
    if (MethodDecl->getInstantiatedFromMemberFunction())
      InstantiatedMethods.push_back(MethodDecl);
    return true;
  }

  bool VisitClassTemplatePartialSpecializationDecl(
      const ClassTemplatePartialSpecializationDecl *PartialSpec) {
    PartialSpecs.push_back(PartialSpec);
    return true;
  }

private:
  void handleCXXRecordDecl(const CXXRecordDecl *RecordDecl) {
    RecordDecl = RecordDecl->getDefinition();
    if (const auto *ClassTemplateSpecDecl =
            dyn_cast<ClassTemplateSpecializationDecl>(RecordDecl))
      handleClassTemplateDecl(ClassTemplateSpecDecl->getSpecializedTemplate());
    addUSRsOfCtorDtors(RecordDecl);
  }

  void handleClassTemplateDecl(const ClassTemplateDecl *TemplateDecl) {
    for (const auto *Specialization : TemplateDecl->specializations())
      addUSRsOfCtorDtors(Specialization);

    for (const auto *PartialSpec : PartialSpecs) {
      if (PartialSpec->getSpecializedTemplate() == TemplateDecl)
        addUSRsOfCtorDtors(PartialSpec);
    }
    addUSRsOfCtorDtors(TemplateDecl->getTemplatedDecl());
  }

  void addUSRsOfCtorDtors(const CXXRecordDecl *RecordDecl) {
    RecordDecl = RecordDecl->getDefinition();

    // Skip if the CXXRecordDecl doesn't have definition.
    if (!RecordDecl)
      return;

    for (const auto *CtorDecl : RecordDecl->ctors())
      USRSet.insert(getUSRForDecl(CtorDecl));

    USRSet.insert(getUSRForDecl(RecordDecl->getDestructor()));
    USRSet.insert(getUSRForDecl(RecordDecl));
  }

  void addUSRsOfOverridenFunctions(const CXXMethodDecl *MethodDecl) {
    USRSet.insert(getUSRForDecl(MethodDecl));
    // Recursively visit each OverridenMethod.
    for (const auto &OverriddenMethod : MethodDecl->overridden_methods())
      addUSRsOfOverridenFunctions(OverriddenMethod);
  }

  void addUSRsOfInstantiatedMethods(const CXXMethodDecl *MethodDecl) {
    // For renaming a class template method, all references of the instantiated
    // member methods should be renamed too, so add USRs of the instantiated
    // methods to the USR set.
    USRSet.insert(getUSRForDecl(MethodDecl));
    if (const auto *FT = MethodDecl->getInstantiatedFromMemberFunction())
      USRSet.insert(getUSRForDecl(FT));
    for (const auto *Method : InstantiatedMethods) {
      if (USRSet.find(getUSRForDecl(
              Method->getInstantiatedFromMemberFunction())) != USRSet.end())
        USRSet.insert(getUSRForDecl(Method));
    }
  }

  bool checkIfOverriddenFunctionAscends(const CXXMethodDecl *MethodDecl) {
    for (const auto &OverriddenMethod : MethodDecl->overridden_methods()) {
      if (USRSet.find(getUSRForDecl(OverriddenMethod)) != USRSet.end())
        return true;
      return checkIfOverriddenFunctionAscends(OverriddenMethod);
    }
    return false;
  }

  const Decl *FoundDecl;
  ASTContext &Context;
  std::set<std::string> USRSet;
  std::vector<const CXXMethodDecl *> OverriddenMethods;
  std::vector<const CXXMethodDecl *> InstantiatedMethods;
  std::vector<const ClassTemplatePartialSpecializationDecl *> PartialSpecs;
};
} // namespace

std::vector<std::string> getUSRsForDeclaration(const NamedDecl *ND,
                                               ASTContext &Context) {
  AdditionalUSRFinder Finder(ND, Context);
  return Finder.Find();
}

class NamedDeclFindingConsumer : public ASTConsumer {
public:
  NamedDeclFindingConsumer(ArrayRef<unsigned> SymbolOffsets,
                           ArrayRef<std::string> QualifiedNames,
                           std::vector<std::string> &SpellingNames,
                           std::vector<std::vector<std::string>> &USRList,
                           bool Force, bool &ErrorOccurred)
      : SymbolOffsets(SymbolOffsets), QualifiedNames(QualifiedNames),
        SpellingNames(SpellingNames), USRList(USRList), Force(Force),
        ErrorOccurred(ErrorOccurred) {}

private:
  bool FindSymbol(ASTContext &Context, const SourceManager &SourceMgr,
                  unsigned SymbolOffset, const std::string &QualifiedName) {
    DiagnosticsEngine &Engine = Context.getDiagnostics();
    const FileID MainFileID = SourceMgr.getMainFileID();

    if (SymbolOffset >= SourceMgr.getFileIDSize(MainFileID)) {
      ErrorOccurred = true;
      unsigned InvalidOffset = Engine.getCustomDiagID(
          DiagnosticsEngine::Error,
          "SourceLocation in file %0 at offset %1 is invalid");
      Engine.Report(SourceLocation(), InvalidOffset)
          << SourceMgr.getFileEntryForID(MainFileID)->getName() << SymbolOffset;
      return false;
    }

    const SourceLocation Point = SourceMgr.getLocForStartOfFile(MainFileID)
                                     .getLocWithOffset(SymbolOffset);
    const NamedDecl *FoundDecl = QualifiedName.empty()
                                     ? getNamedDeclAt(Context, Point)
                                     : getNamedDeclFor(Context, QualifiedName);

    if (FoundDecl == nullptr) {
      if (QualifiedName.empty()) {
        FullSourceLoc FullLoc(Point, SourceMgr);
        unsigned CouldNotFindSymbolAt = Engine.getCustomDiagID(
            DiagnosticsEngine::Error,
            "clang-rename could not find symbol (offset %0)");
        Engine.Report(Point, CouldNotFindSymbolAt) << SymbolOffset;
        ErrorOccurred = true;
        return false;
      }

      if (Force) {
        SpellingNames.push_back(std::string());
        USRList.push_back(std::vector<std::string>());
        return true;
      }

      unsigned CouldNotFindSymbolNamed = Engine.getCustomDiagID(
          DiagnosticsEngine::Error, "clang-rename could not find symbol %0");
      Engine.Report(CouldNotFindSymbolNamed) << QualifiedName;
      ErrorOccurred = true;
      return false;
    }

    FoundDecl = getCanonicalSymbolDeclaration(FoundDecl);
    SpellingNames.push_back(FoundDecl->getNameAsString());
    AdditionalUSRFinder Finder(FoundDecl, Context);
    USRList.push_back(Finder.Find());
    return true;
  }

  void HandleTranslationUnit(ASTContext &Context) override {
    const SourceManager &SourceMgr = Context.getSourceManager();
    for (unsigned Offset : SymbolOffsets) {
      if (!FindSymbol(Context, SourceMgr, Offset, ""))
        return;
    }
    for (const std::string &QualifiedName : QualifiedNames) {
      if (!FindSymbol(Context, SourceMgr, 0, QualifiedName))
        return;
    }
  }

  ArrayRef<unsigned> SymbolOffsets;
  ArrayRef<std::string> QualifiedNames;
  std::vector<std::string> &SpellingNames;
  std::vector<std::vector<std::string>> &USRList;
  bool Force;
  bool &ErrorOccurred;
};

std::unique_ptr<ASTConsumer> USRFindingAction::newASTConsumer() {
  return llvm::make_unique<NamedDeclFindingConsumer>(
      SymbolOffsets, QualifiedNames, SpellingNames, USRList, Force,
      ErrorOccurred);
}

} // end namespace tooling
} // end namespace clang
