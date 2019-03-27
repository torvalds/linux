//=- NSErrorChecker.cpp - Coding conventions for uses of NSError -*- C++ -*-==//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
//  This file defines a CheckNSError, a flow-insenstive check
//  that determines if an Objective-C class interface correctly returns
//  a non-void return type.
//
//  File under feature request PR 2600.
//
//===----------------------------------------------------------------------===//

#include "clang/StaticAnalyzer/Checkers/BuiltinCheckerRegistration.h"
#include "clang/AST/Decl.h"
#include "clang/AST/DeclObjC.h"
#include "clang/StaticAnalyzer/Core/BugReporter/BugType.h"
#include "clang/StaticAnalyzer/Core/Checker.h"
#include "clang/StaticAnalyzer/Core/CheckerManager.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/CheckerContext.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/ProgramStateTrait.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/Support/raw_ostream.h"

using namespace clang;
using namespace ento;

static bool IsNSError(QualType T, IdentifierInfo *II);
static bool IsCFError(QualType T, IdentifierInfo *II);

//===----------------------------------------------------------------------===//
// NSErrorMethodChecker
//===----------------------------------------------------------------------===//

namespace {
class NSErrorMethodChecker
    : public Checker< check::ASTDecl<ObjCMethodDecl> > {
  mutable IdentifierInfo *II;

public:
  NSErrorMethodChecker() : II(nullptr) {}

  void checkASTDecl(const ObjCMethodDecl *D,
                    AnalysisManager &mgr, BugReporter &BR) const;
};
}

void NSErrorMethodChecker::checkASTDecl(const ObjCMethodDecl *D,
                                        AnalysisManager &mgr,
                                        BugReporter &BR) const {
  if (!D->isThisDeclarationADefinition())
    return;
  if (!D->getReturnType()->isVoidType())
    return;

  if (!II)
    II = &D->getASTContext().Idents.get("NSError");

  bool hasNSError = false;
  for (const auto *I : D->parameters())  {
    if (IsNSError(I->getType(), II)) {
      hasNSError = true;
      break;
    }
  }

  if (hasNSError) {
    const char *err = "Method accepting NSError** "
        "should have a non-void return value to indicate whether or not an "
        "error occurred";
    PathDiagnosticLocation L =
      PathDiagnosticLocation::create(D, BR.getSourceManager());
    BR.EmitBasicReport(D, this, "Bad return type when passing NSError**",
                       "Coding conventions (Apple)", err, L);
  }
}

//===----------------------------------------------------------------------===//
// CFErrorFunctionChecker
//===----------------------------------------------------------------------===//

namespace {
class CFErrorFunctionChecker
    : public Checker< check::ASTDecl<FunctionDecl> > {
  mutable IdentifierInfo *II;

public:
  CFErrorFunctionChecker() : II(nullptr) {}

  void checkASTDecl(const FunctionDecl *D,
                    AnalysisManager &mgr, BugReporter &BR) const;
};
}

void CFErrorFunctionChecker::checkASTDecl(const FunctionDecl *D,
                                        AnalysisManager &mgr,
                                        BugReporter &BR) const {
  if (!D->doesThisDeclarationHaveABody())
    return;
  if (!D->getReturnType()->isVoidType())
    return;

  if (!II)
    II = &D->getASTContext().Idents.get("CFErrorRef");

  bool hasCFError = false;
  for (auto I : D->parameters())  {
    if (IsCFError(I->getType(), II)) {
      hasCFError = true;
      break;
    }
  }

  if (hasCFError) {
    const char *err = "Function accepting CFErrorRef* "
        "should have a non-void return value to indicate whether or not an "
        "error occurred";
    PathDiagnosticLocation L =
      PathDiagnosticLocation::create(D, BR.getSourceManager());
    BR.EmitBasicReport(D, this, "Bad return type when passing CFErrorRef*",
                       "Coding conventions (Apple)", err, L);
  }
}

//===----------------------------------------------------------------------===//
// NSOrCFErrorDerefChecker
//===----------------------------------------------------------------------===//

namespace {

class NSErrorDerefBug : public BugType {
public:
  NSErrorDerefBug(const CheckerBase *Checker)
      : BugType(Checker, "NSError** null dereference",
                "Coding conventions (Apple)") {}
};

class CFErrorDerefBug : public BugType {
public:
  CFErrorDerefBug(const CheckerBase *Checker)
      : BugType(Checker, "CFErrorRef* null dereference",
                "Coding conventions (Apple)") {}
};

}

namespace {
class NSOrCFErrorDerefChecker
    : public Checker< check::Location,
                        check::Event<ImplicitNullDerefEvent> > {
  mutable IdentifierInfo *NSErrorII, *CFErrorII;
  mutable std::unique_ptr<NSErrorDerefBug> NSBT;
  mutable std::unique_ptr<CFErrorDerefBug> CFBT;
public:
  bool ShouldCheckNSError, ShouldCheckCFError;
  NSOrCFErrorDerefChecker() : NSErrorII(nullptr), CFErrorII(nullptr),
                              ShouldCheckNSError(0), ShouldCheckCFError(0) { }

  void checkLocation(SVal loc, bool isLoad, const Stmt *S,
                     CheckerContext &C) const;
  void checkEvent(ImplicitNullDerefEvent event) const;
};
}

typedef llvm::ImmutableMap<SymbolRef, unsigned> ErrorOutFlag;
REGISTER_TRAIT_WITH_PROGRAMSTATE(NSErrorOut, ErrorOutFlag)
REGISTER_TRAIT_WITH_PROGRAMSTATE(CFErrorOut, ErrorOutFlag)

template <typename T>
static bool hasFlag(SVal val, ProgramStateRef state) {
  if (SymbolRef sym = val.getAsSymbol())
    if (const unsigned *attachedFlags = state->get<T>(sym))
      return *attachedFlags;
  return false;
}

template <typename T>
static void setFlag(ProgramStateRef state, SVal val, CheckerContext &C) {
  // We tag the symbol that the SVal wraps.
  if (SymbolRef sym = val.getAsSymbol())
    C.addTransition(state->set<T>(sym, true));
}

static QualType parameterTypeFromSVal(SVal val, CheckerContext &C) {
  const StackFrameContext * SFC = C.getStackFrame();
  if (Optional<loc::MemRegionVal> X = val.getAs<loc::MemRegionVal>()) {
    const MemRegion* R = X->getRegion();
    if (const VarRegion *VR = R->getAs<VarRegion>())
      if (const StackArgumentsSpaceRegion *
          stackReg = dyn_cast<StackArgumentsSpaceRegion>(VR->getMemorySpace()))
        if (stackReg->getStackFrame() == SFC)
          return VR->getValueType();
  }

  return QualType();
}

void NSOrCFErrorDerefChecker::checkLocation(SVal loc, bool isLoad,
                                            const Stmt *S,
                                            CheckerContext &C) const {
  if (!isLoad)
    return;
  if (loc.isUndef() || !loc.getAs<Loc>())
    return;

  ASTContext &Ctx = C.getASTContext();
  ProgramStateRef state = C.getState();

  // If we are loading from NSError**/CFErrorRef* parameter, mark the resulting
  // SVal so that we can later check it when handling the
  // ImplicitNullDerefEvent event.
  // FIXME: Cumbersome! Maybe add hook at construction of SVals at start of
  // function ?

  QualType parmT = parameterTypeFromSVal(loc, C);
  if (parmT.isNull())
    return;

  if (!NSErrorII)
    NSErrorII = &Ctx.Idents.get("NSError");
  if (!CFErrorII)
    CFErrorII = &Ctx.Idents.get("CFErrorRef");

  if (ShouldCheckNSError && IsNSError(parmT, NSErrorII)) {
    setFlag<NSErrorOut>(state, state->getSVal(loc.castAs<Loc>()), C);
    return;
  }

  if (ShouldCheckCFError && IsCFError(parmT, CFErrorII)) {
    setFlag<CFErrorOut>(state, state->getSVal(loc.castAs<Loc>()), C);
    return;
  }
}

void NSOrCFErrorDerefChecker::checkEvent(ImplicitNullDerefEvent event) const {
  if (event.IsLoad)
    return;

  SVal loc = event.Location;
  ProgramStateRef state = event.SinkNode->getState();
  BugReporter &BR = *event.BR;

  bool isNSError = hasFlag<NSErrorOut>(loc, state);
  bool isCFError = false;
  if (!isNSError)
    isCFError = hasFlag<CFErrorOut>(loc, state);

  if (!(isNSError || isCFError))
    return;

  // Storing to possible null NSError/CFErrorRef out parameter.
  SmallString<128> Buf;
  llvm::raw_svector_ostream os(Buf);

  os << "Potential null dereference.  According to coding standards ";
  os << (isNSError
         ? "in 'Creating and Returning NSError Objects' the parameter"
         : "documented in CoreFoundation/CFError.h the parameter");

  os  << " may be null";

  BugType *bug = nullptr;
  if (isNSError) {
    if (!NSBT)
      NSBT.reset(new NSErrorDerefBug(this));
    bug = NSBT.get();
  }
  else {
    if (!CFBT)
      CFBT.reset(new CFErrorDerefBug(this));
    bug = CFBT.get();
  }
  BR.emitReport(llvm::make_unique<BugReport>(*bug, os.str(), event.SinkNode));
}

static bool IsNSError(QualType T, IdentifierInfo *II) {

  const PointerType* PPT = T->getAs<PointerType>();
  if (!PPT)
    return false;

  const ObjCObjectPointerType* PT =
    PPT->getPointeeType()->getAs<ObjCObjectPointerType>();

  if (!PT)
    return false;

  const ObjCInterfaceDecl *ID = PT->getInterfaceDecl();

  // FIXME: Can ID ever be NULL?
  if (ID)
    return II == ID->getIdentifier();

  return false;
}

static bool IsCFError(QualType T, IdentifierInfo *II) {
  const PointerType* PPT = T->getAs<PointerType>();
  if (!PPT) return false;

  const TypedefType* TT = PPT->getPointeeType()->getAs<TypedefType>();
  if (!TT) return false;

  return TT->getDecl()->getIdentifier() == II;
}

void ento::registerNSErrorChecker(CheckerManager &mgr) {
  mgr.registerChecker<NSErrorMethodChecker>();
  NSOrCFErrorDerefChecker *checker =
      mgr.registerChecker<NSOrCFErrorDerefChecker>();
  checker->ShouldCheckNSError = true;
}

void ento::registerCFErrorChecker(CheckerManager &mgr) {
  mgr.registerChecker<CFErrorFunctionChecker>();
  NSOrCFErrorDerefChecker *checker =
      mgr.registerChecker<NSOrCFErrorDerefChecker>();
  checker->ShouldCheckCFError = true;
}
