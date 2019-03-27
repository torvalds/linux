//=- DirectIvarAssignment.cpp - Check rules on ObjC properties -*- C++ ----*-==//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
//  Check that Objective C properties are set with the setter, not though a
//      direct assignment.
//
//  Two versions of a checker exist: one that checks all methods and the other
//      that only checks the methods annotated with
//      __attribute__((annotate("objc_no_direct_instance_variable_assignment")))
//
//  The checker does not warn about assignments to Ivars, annotated with
//       __attribute__((objc_allow_direct_instance_variable_assignment"))). This
//      annotation serves as a false positive suppression mechanism for the
//      checker. The annotation is allowed on properties and Ivars.
//
//===----------------------------------------------------------------------===//

#include "clang/StaticAnalyzer/Checkers/BuiltinCheckerRegistration.h"
#include "clang/AST/Attr.h"
#include "clang/AST/DeclObjC.h"
#include "clang/AST/StmtVisitor.h"
#include "clang/StaticAnalyzer/Core/BugReporter/BugReporter.h"
#include "clang/StaticAnalyzer/Core/Checker.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/AnalysisManager.h"
#include "llvm/ADT/DenseMap.h"

using namespace clang;
using namespace ento;

namespace {

/// The default method filter, which is used to filter out the methods on which
/// the check should not be performed.
///
/// Checks for the init, dealloc, and any other functions that might be allowed
/// to perform direct instance variable assignment based on their name.
static bool DefaultMethodFilter(const ObjCMethodDecl *M) {
  return M->getMethodFamily() == OMF_init ||
         M->getMethodFamily() == OMF_dealloc ||
         M->getMethodFamily() == OMF_copy ||
         M->getMethodFamily() == OMF_mutableCopy ||
         M->getSelector().getNameForSlot(0).find("init") != StringRef::npos ||
         M->getSelector().getNameForSlot(0).find("Init") != StringRef::npos;
}

class DirectIvarAssignment :
  public Checker<check::ASTDecl<ObjCImplementationDecl> > {

  typedef llvm::DenseMap<const ObjCIvarDecl*,
                         const ObjCPropertyDecl*> IvarToPropertyMapTy;

  /// A helper class, which walks the AST and locates all assignments to ivars
  /// in the given function.
  class MethodCrawler : public ConstStmtVisitor<MethodCrawler> {
    const IvarToPropertyMapTy &IvarToPropMap;
    const ObjCMethodDecl *MD;
    const ObjCInterfaceDecl *InterfD;
    BugReporter &BR;
    const CheckerBase *Checker;
    LocationOrAnalysisDeclContext DCtx;

  public:
    MethodCrawler(const IvarToPropertyMapTy &InMap, const ObjCMethodDecl *InMD,
                  const ObjCInterfaceDecl *InID, BugReporter &InBR,
                  const CheckerBase *Checker, AnalysisDeclContext *InDCtx)
        : IvarToPropMap(InMap), MD(InMD), InterfD(InID), BR(InBR),
          Checker(Checker), DCtx(InDCtx) {}

    void VisitStmt(const Stmt *S) { VisitChildren(S); }

    void VisitBinaryOperator(const BinaryOperator *BO);

    void VisitChildren(const Stmt *S) {
      for (const Stmt *Child : S->children())
        if (Child)
          this->Visit(Child);
    }
  };

public:
  bool (*ShouldSkipMethod)(const ObjCMethodDecl *);

  DirectIvarAssignment() : ShouldSkipMethod(&DefaultMethodFilter) {}

  void checkASTDecl(const ObjCImplementationDecl *D, AnalysisManager& Mgr,
                    BugReporter &BR) const;
};

static const ObjCIvarDecl *findPropertyBackingIvar(const ObjCPropertyDecl *PD,
                                               const ObjCInterfaceDecl *InterD,
                                               ASTContext &Ctx) {
  // Check for synthesized ivars.
  ObjCIvarDecl *ID = PD->getPropertyIvarDecl();
  if (ID)
    return ID;

  ObjCInterfaceDecl *NonConstInterD = const_cast<ObjCInterfaceDecl*>(InterD);

  // Check for existing "_PropName".
  ID = NonConstInterD->lookupInstanceVariable(PD->getDefaultSynthIvarName(Ctx));
  if (ID)
    return ID;

  // Check for existing "PropName".
  IdentifierInfo *PropIdent = PD->getIdentifier();
  ID = NonConstInterD->lookupInstanceVariable(PropIdent);

  return ID;
}

void DirectIvarAssignment::checkASTDecl(const ObjCImplementationDecl *D,
                                       AnalysisManager& Mgr,
                                       BugReporter &BR) const {
  const ObjCInterfaceDecl *InterD = D->getClassInterface();


  IvarToPropertyMapTy IvarToPropMap;

  // Find all properties for this class.
  for (const auto *PD : InterD->instance_properties()) {
    // Find the corresponding IVar.
    const ObjCIvarDecl *ID = findPropertyBackingIvar(PD, InterD,
                                                     Mgr.getASTContext());

    if (!ID)
      continue;

    // Store the IVar to property mapping.
    IvarToPropMap[ID] = PD;
  }

  if (IvarToPropMap.empty())
    return;

  for (const auto *M : D->instance_methods()) {
    AnalysisDeclContext *DCtx = Mgr.getAnalysisDeclContext(M);

    if ((*ShouldSkipMethod)(M))
      continue;

    const Stmt *Body = M->getBody();
    assert(Body);

    MethodCrawler MC(IvarToPropMap, M->getCanonicalDecl(), InterD, BR, this,
                     DCtx);
    MC.VisitStmt(Body);
  }
}

static bool isAnnotatedToAllowDirectAssignment(const Decl *D) {
  for (const auto *Ann : D->specific_attrs<AnnotateAttr>())
    if (Ann->getAnnotation() ==
        "objc_allow_direct_instance_variable_assignment")
      return true;
  return false;
}

void DirectIvarAssignment::MethodCrawler::VisitBinaryOperator(
                                                    const BinaryOperator *BO) {
  if (!BO->isAssignmentOp())
    return;

  const ObjCIvarRefExpr *IvarRef =
          dyn_cast<ObjCIvarRefExpr>(BO->getLHS()->IgnoreParenCasts());

  if (!IvarRef)
    return;

  if (const ObjCIvarDecl *D = IvarRef->getDecl()) {
    IvarToPropertyMapTy::const_iterator I = IvarToPropMap.find(D);

    if (I != IvarToPropMap.end()) {
      const ObjCPropertyDecl *PD = I->second;
      // Skip warnings on Ivars, annotated with
      // objc_allow_direct_instance_variable_assignment. This annotation serves
      // as a false positive suppression mechanism for the checker. The
      // annotation is allowed on properties and ivars.
      if (isAnnotatedToAllowDirectAssignment(PD) ||
          isAnnotatedToAllowDirectAssignment(D))
        return;

      ObjCMethodDecl *GetterMethod =
          InterfD->getInstanceMethod(PD->getGetterName());
      ObjCMethodDecl *SetterMethod =
          InterfD->getInstanceMethod(PD->getSetterName());

      if (SetterMethod && SetterMethod->getCanonicalDecl() == MD)
        return;

      if (GetterMethod && GetterMethod->getCanonicalDecl() == MD)
        return;

      BR.EmitBasicReport(
          MD, Checker, "Property access", categories::CoreFoundationObjectiveC,
          "Direct assignment to an instance variable backing a property; "
          "use the setter instead",
          PathDiagnosticLocation(IvarRef, BR.getSourceManager(), DCtx));
    }
  }
}
}

// Register the checker that checks for direct accesses in all functions,
// except for the initialization and copy routines.
void ento::registerDirectIvarAssignment(CheckerManager &mgr) {
  mgr.registerChecker<DirectIvarAssignment>();
}

// Register the checker that checks for direct accesses in functions annotated
// with __attribute__((annotate("objc_no_direct_instance_variable_assignment"))).
static bool AttrFilter(const ObjCMethodDecl *M) {
  for (const auto *Ann : M->specific_attrs<AnnotateAttr>())
    if (Ann->getAnnotation() == "objc_no_direct_instance_variable_assignment")
      return false;
  return true;
}

void ento::registerDirectIvarAssignmentForAnnotatedFunctions(
    CheckerManager &mgr) {
  mgr.registerChecker<DirectIvarAssignment>()->ShouldSkipMethod = &AttrFilter;
}
