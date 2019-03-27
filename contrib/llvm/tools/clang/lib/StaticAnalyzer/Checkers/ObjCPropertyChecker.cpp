//==- ObjCPropertyChecker.cpp - Check ObjC properties ------------*- C++ -*-==//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
//  This checker finds issues with Objective-C properties.
//  Currently finds only one kind of issue:
//  - Find synthesized properties with copy attribute of mutable NS collection
//    types. Calling -copy on such collections produces an immutable copy,
//    which contradicts the type of the property.
//
//===----------------------------------------------------------------------===//

#include "clang/StaticAnalyzer/Checkers/BuiltinCheckerRegistration.h"
#include "clang/StaticAnalyzer/Core/BugReporter/BugReporter.h"
#include "clang/StaticAnalyzer/Core/Checker.h"

using namespace clang;
using namespace ento;

namespace {
class ObjCPropertyChecker
    : public Checker<check::ASTDecl<ObjCPropertyDecl>> {
  void checkCopyMutable(const ObjCPropertyDecl *D, BugReporter &BR) const;

public:
  void checkASTDecl(const ObjCPropertyDecl *D, AnalysisManager &Mgr,
                    BugReporter &BR) const;
};
} // end anonymous namespace.

void ObjCPropertyChecker::checkASTDecl(const ObjCPropertyDecl *D,
                                       AnalysisManager &Mgr,
                                       BugReporter &BR) const {
  checkCopyMutable(D, BR);
}

void ObjCPropertyChecker::checkCopyMutable(const ObjCPropertyDecl *D,
                                           BugReporter &BR) const {
  if (D->isReadOnly() || D->getSetterKind() != ObjCPropertyDecl::Copy)
    return;

  QualType T = D->getType();
  if (!T->isObjCObjectPointerType())
    return;

  const std::string &PropTypeName(T->getPointeeType().getCanonicalType()
                                                     .getUnqualifiedType()
                                                     .getAsString());
  if (!StringRef(PropTypeName).startswith("NSMutable"))
    return;

  const ObjCImplDecl *ImplD = nullptr;
  if (const ObjCInterfaceDecl *IntD =
          dyn_cast<ObjCInterfaceDecl>(D->getDeclContext())) {
    ImplD = IntD->getImplementation();
  } else if (auto *CatD = dyn_cast<ObjCCategoryDecl>(D->getDeclContext())) {
    ImplD = CatD->getClassInterface()->getImplementation();
  }

  if (!ImplD || ImplD->HasUserDeclaredSetterMethod(D))
    return;

  SmallString<128> Str;
  llvm::raw_svector_ostream OS(Str);
  OS << "Property of mutable type '" << PropTypeName
     << "' has 'copy' attribute; an immutable object will be stored instead";

  BR.EmitBasicReport(
      D, this, "Objective-C property misuse", "Logic error", OS.str(),
      PathDiagnosticLocation::createBegin(D, BR.getSourceManager()),
      D->getSourceRange());
}

void ento::registerObjCPropertyChecker(CheckerManager &Mgr) {
  Mgr.registerChecker<ObjCPropertyChecker>();
}
