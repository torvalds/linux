//==- NonnullGlobalConstantsChecker.cpp ---------------------------*- C++ -*--//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
//  This checker adds an assumption that constant globals of certain types* are
//  non-null, as otherwise they generally do not convey any useful information.
//  The assumption is useful, as many framework use e. g. global const strings,
//  and the analyzer might not be able to infer the global value if the
//  definition is in a separate translation unit.
//  The following types (and their typedef aliases) are considered to be
//  non-null:
//   - `char* const`
//   - `const CFStringRef` from CoreFoundation
//   - `NSString* const` from Foundation
//   - `CFBooleanRef` from Foundation
//
//===----------------------------------------------------------------------===//

#include "clang/StaticAnalyzer/Checkers/BuiltinCheckerRegistration.h"
#include "clang/StaticAnalyzer/Core/BugReporter/BugType.h"
#include "clang/StaticAnalyzer/Core/Checker.h"
#include "clang/StaticAnalyzer/Core/CheckerManager.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/CheckerContext.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/ExprEngine.h"

using namespace clang;
using namespace ento;

namespace {

class NonnullGlobalConstantsChecker : public Checker<check::Location> {
  mutable IdentifierInfo *NSStringII = nullptr;
  mutable IdentifierInfo *CFStringRefII = nullptr;
  mutable IdentifierInfo *CFBooleanRefII = nullptr;

public:
  NonnullGlobalConstantsChecker() {}

  void checkLocation(SVal l, bool isLoad, const Stmt *S,
                     CheckerContext &C) const;

private:
  void initIdentifierInfo(ASTContext &Ctx) const;

  bool isGlobalConstString(SVal V) const;

  bool isNonnullType(QualType Ty) const;
};

} // namespace

/// Lazily initialize cache for required identifier information.
void NonnullGlobalConstantsChecker::initIdentifierInfo(ASTContext &Ctx) const {
  if (NSStringII)
    return;

  NSStringII = &Ctx.Idents.get("NSString");
  CFStringRefII = &Ctx.Idents.get("CFStringRef");
  CFBooleanRefII = &Ctx.Idents.get("CFBooleanRef");
}

/// Add an assumption that const string-like globals are non-null.
void NonnullGlobalConstantsChecker::checkLocation(SVal location, bool isLoad,
                                                 const Stmt *S,
                                                 CheckerContext &C) const {
  initIdentifierInfo(C.getASTContext());
  if (!isLoad || !location.isValid())
    return;

  ProgramStateRef State = C.getState();

  if (isGlobalConstString(location)) {
    SVal V = State->getSVal(location.castAs<Loc>());
    Optional<DefinedOrUnknownSVal> Constr = V.getAs<DefinedOrUnknownSVal>();

    if (Constr) {

      // Assume that the variable is non-null.
      ProgramStateRef OutputState = State->assume(*Constr, true);
      C.addTransition(OutputState);
    }
  }
}

/// \param V loaded lvalue.
/// \return whether {@code val} is a string-like const global.
bool NonnullGlobalConstantsChecker::isGlobalConstString(SVal V) const {
  Optional<loc::MemRegionVal> RegionVal = V.getAs<loc::MemRegionVal>();
  if (!RegionVal)
    return false;
  auto *Region = dyn_cast<VarRegion>(RegionVal->getAsRegion());
  if (!Region)
    return false;
  const VarDecl *Decl = Region->getDecl();

  if (!Decl->hasGlobalStorage())
    return false;

  QualType Ty = Decl->getType();
  bool HasConst = Ty.isConstQualified();
  if (isNonnullType(Ty) && HasConst)
    return true;

  // Look through the typedefs.
  while (auto *T = dyn_cast<TypedefType>(Ty)) {
    Ty = T->getDecl()->getUnderlyingType();

    // It is sufficient for any intermediate typedef
    // to be classified const.
    HasConst = HasConst || Ty.isConstQualified();
    if (isNonnullType(Ty) && HasConst)
      return true;
  }
  return false;
}

/// \return whether {@code type} is extremely unlikely to be null
bool NonnullGlobalConstantsChecker::isNonnullType(QualType Ty) const {

  if (Ty->isPointerType() && Ty->getPointeeType()->isCharType())
    return true;

  if (auto *T = dyn_cast<ObjCObjectPointerType>(Ty)) {
    return T->getInterfaceDecl() &&
      T->getInterfaceDecl()->getIdentifier() == NSStringII;
  } else if (auto *T = dyn_cast<TypedefType>(Ty)) {
    IdentifierInfo* II = T->getDecl()->getIdentifier();
    return II == CFStringRefII || II == CFBooleanRefII;
  }
  return false;
}

void ento::registerNonnullGlobalConstantsChecker(CheckerManager &Mgr) {
  Mgr.registerChecker<NonnullGlobalConstantsChecker>();
}
