//==- ObjCUnusedIVarsChecker.cpp - Check for unused ivars --------*- C++ -*-==//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
//  This file defines a CheckObjCUnusedIvars, a checker that
//  analyzes an Objective-C class's interface/implementation to determine if it
//  has any ivars that are never accessed.
//
//===----------------------------------------------------------------------===//

#include "clang/StaticAnalyzer/Checkers/BuiltinCheckerRegistration.h"
#include "clang/AST/Attr.h"
#include "clang/AST/DeclObjC.h"
#include "clang/AST/Expr.h"
#include "clang/AST/ExprObjC.h"
#include "clang/Basic/LangOptions.h"
#include "clang/Basic/SourceManager.h"
#include "clang/StaticAnalyzer/Core/BugReporter/BugReporter.h"
#include "clang/StaticAnalyzer/Core/BugReporter/PathDiagnostic.h"
#include "clang/StaticAnalyzer/Core/Checker.h"

using namespace clang;
using namespace ento;

enum IVarState { Unused, Used };
typedef llvm::DenseMap<const ObjCIvarDecl*,IVarState> IvarUsageMap;

static void Scan(IvarUsageMap& M, const Stmt *S) {
  if (!S)
    return;

  if (const ObjCIvarRefExpr *Ex = dyn_cast<ObjCIvarRefExpr>(S)) {
    const ObjCIvarDecl *D = Ex->getDecl();
    IvarUsageMap::iterator I = M.find(D);
    if (I != M.end())
      I->second = Used;
    return;
  }

  // Blocks can reference an instance variable of a class.
  if (const BlockExpr *BE = dyn_cast<BlockExpr>(S)) {
    Scan(M, BE->getBody());
    return;
  }

  if (const PseudoObjectExpr *POE = dyn_cast<PseudoObjectExpr>(S))
    for (PseudoObjectExpr::const_semantics_iterator
        i = POE->semantics_begin(), e = POE->semantics_end(); i != e; ++i) {
      const Expr *sub = *i;
      if (const OpaqueValueExpr *OVE = dyn_cast<OpaqueValueExpr>(sub))
        sub = OVE->getSourceExpr();
      Scan(M, sub);
    }

  for (const Stmt *SubStmt : S->children())
    Scan(M, SubStmt);
}

static void Scan(IvarUsageMap& M, const ObjCPropertyImplDecl *D) {
  if (!D)
    return;

  const ObjCIvarDecl *ID = D->getPropertyIvarDecl();

  if (!ID)
    return;

  IvarUsageMap::iterator I = M.find(ID);
  if (I != M.end())
    I->second = Used;
}

static void Scan(IvarUsageMap& M, const ObjCContainerDecl *D) {
  // Scan the methods for accesses.
  for (const auto *I : D->instance_methods())
    Scan(M, I->getBody());

  if (const ObjCImplementationDecl *ID = dyn_cast<ObjCImplementationDecl>(D)) {
    // Scan for @synthesized property methods that act as setters/getters
    // to an ivar.
    for (const auto *I : ID->property_impls())
      Scan(M, I);

    // Scan the associated categories as well.
    for (const auto *Cat : ID->getClassInterface()->visible_categories()) {
      if (const ObjCCategoryImplDecl *CID = Cat->getImplementation())
        Scan(M, CID);
    }
  }
}

static void Scan(IvarUsageMap &M, const DeclContext *C, const FileID FID,
                 SourceManager &SM) {
  for (const auto *I : C->decls())
    if (const auto *FD = dyn_cast<FunctionDecl>(I)) {
      SourceLocation L = FD->getBeginLoc();
      if (SM.getFileID(L) == FID)
        Scan(M, FD->getBody());
    }
}

static void checkObjCUnusedIvar(const ObjCImplementationDecl *D,
                                BugReporter &BR,
                                const CheckerBase *Checker) {

  const ObjCInterfaceDecl *ID = D->getClassInterface();
  IvarUsageMap M;

  // Iterate over the ivars.
  for (const auto *Ivar : ID->ivars()) {
    // Ignore ivars that...
    // (a) aren't private
    // (b) explicitly marked unused
    // (c) are iboutlets
    // (d) are unnamed bitfields
    if (Ivar->getAccessControl() != ObjCIvarDecl::Private ||
        Ivar->hasAttr<UnusedAttr>() || Ivar->hasAttr<IBOutletAttr>() ||
        Ivar->hasAttr<IBOutletCollectionAttr>() ||
        Ivar->isUnnamedBitfield())
      continue;

    M[Ivar] = Unused;
  }

  if (M.empty())
    return;

  // Now scan the implementation declaration.
  Scan(M, D);

  // Any potentially unused ivars?
  bool hasUnused = false;
  for (IvarUsageMap::iterator I = M.begin(), E = M.end(); I!=E; ++I)
    if (I->second == Unused) {
      hasUnused = true;
      break;
    }

  if (!hasUnused)
    return;

  // We found some potentially unused ivars.  Scan the entire translation unit
  // for functions inside the @implementation that reference these ivars.
  // FIXME: In the future hopefully we can just use the lexical DeclContext
  // to go from the ObjCImplementationDecl to the lexically "nested"
  // C functions.
  SourceManager &SM = BR.getSourceManager();
  Scan(M, D->getDeclContext(), SM.getFileID(D->getLocation()), SM);

  // Find ivars that are unused.
  for (IvarUsageMap::iterator I = M.begin(), E = M.end(); I!=E; ++I)
    if (I->second == Unused) {
      std::string sbuf;
      llvm::raw_string_ostream os(sbuf);
      os << "Instance variable '" << *I->first << "' in class '" << *ID
         << "' is never used by the methods in its @implementation "
            "(although it may be used by category methods).";

      PathDiagnosticLocation L =
        PathDiagnosticLocation::create(I->first, BR.getSourceManager());
      BR.EmitBasicReport(D, Checker, "Unused instance variable", "Optimization",
                         os.str(), L);
    }
}

//===----------------------------------------------------------------------===//
// ObjCUnusedIvarsChecker
//===----------------------------------------------------------------------===//

namespace {
class ObjCUnusedIvarsChecker : public Checker<
                                      check::ASTDecl<ObjCImplementationDecl> > {
public:
  void checkASTDecl(const ObjCImplementationDecl *D, AnalysisManager& mgr,
                    BugReporter &BR) const {
    checkObjCUnusedIvar(D, BR, this);
  }
};
}

void ento::registerObjCUnusedIvarsChecker(CheckerManager &mgr) {
  mgr.registerChecker<ObjCUnusedIvarsChecker>();
}
