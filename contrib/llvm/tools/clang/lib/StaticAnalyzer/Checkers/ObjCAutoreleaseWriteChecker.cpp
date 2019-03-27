//===- ObjCAutoreleaseWriteChecker.cpp ----------------------------*- C++ -*-==//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file defines ObjCAutoreleaseWriteChecker which warns against writes
// into autoreleased out parameters which cause crashes.
// An example of a problematic write is a write to {@code error} in the example
// below:
//
// - (BOOL) mymethod:(NSError *__autoreleasing *)error list:(NSArray*) list {
//     [list enumerateObjectsUsingBlock:^(id obj, NSUInteger idx, BOOL *stop) {
//       NSString *myString = obj;
//       if ([myString isEqualToString:@"error"] && error)
//         *error = [NSError errorWithDomain:@"MyDomain" code:-1];
//     }];
//     return false;
// }
//
// Such code will crash on read from `*error` due to the autorelease pool
// in `enumerateObjectsUsingBlock` implementation freeing the error object
// on exit from the function.
//
//===----------------------------------------------------------------------===//

#include "clang/StaticAnalyzer/Checkers/BuiltinCheckerRegistration.h"
#include "clang/ASTMatchers/ASTMatchFinder.h"
#include "clang/StaticAnalyzer/Core/BugReporter/BugReporter.h"
#include "clang/StaticAnalyzer/Core/BugReporter/BugType.h"
#include "clang/StaticAnalyzer/Core/Checker.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/AnalysisManager.h"
#include "llvm/ADT/Twine.h"

using namespace clang;
using namespace ento;
using namespace ast_matchers;

namespace {

const char *ProblematicWriteBind = "problematicwrite";
const char *CapturedBind = "capturedbind";
const char *ParamBind = "parambind";
const char *IsMethodBind = "ismethodbind";

class ObjCAutoreleaseWriteChecker : public Checker<check::ASTCodeBody> {
public:
  void checkASTCodeBody(const Decl *D,
                        AnalysisManager &AM,
                        BugReporter &BR) const;
private:
  std::vector<std::string> SelectorsWithAutoreleasingPool = {
      // Common to NSArray,  NSSet, NSOrderedSet
      "enumerateObjectsUsingBlock:",
      "enumerateObjectsWithOptions:usingBlock:",

      // Common to NSArray and NSOrderedSet
      "enumerateObjectsAtIndexes:options:usingBlock:",
      "indexOfObjectAtIndexes:options:passingTest:",
      "indexesOfObjectsAtIndexes:options:passingTest:",
      "indexOfObjectPassingTest:",
      "indexOfObjectWithOptions:passingTest:",
      "indexesOfObjectsPassingTest:",
      "indexesOfObjectsWithOptions:passingTest:",

      // NSDictionary
      "enumerateKeysAndObjectsUsingBlock:",
      "enumerateKeysAndObjectsWithOptions:usingBlock:",
      "keysOfEntriesPassingTest:",
      "keysOfEntriesWithOptions:passingTest:",

      // NSSet
      "objectsPassingTest:",
      "objectsWithOptions:passingTest:",
      "enumerateIndexPathsWithOptions:usingBlock:",

      // NSIndexSet
      "enumerateIndexesWithOptions:usingBlock:",
      "enumerateIndexesUsingBlock:",
      "enumerateIndexesInRange:options:usingBlock:",
      "enumerateRangesUsingBlock:",
      "enumerateRangesWithOptions:usingBlock:",
      "enumerateRangesInRange:options:usingBlock:",
      "indexPassingTest:",
      "indexesPassingTest:",
      "indexWithOptions:passingTest:",
      "indexesWithOptions:passingTest:",
      "indexInRange:options:passingTest:",
      "indexesInRange:options:passingTest:"
  };

  std::vector<std::string> FunctionsWithAutoreleasingPool = {
      "dispatch_async", "dispatch_group_async", "dispatch_barrier_async"};
};
}

static inline std::vector<llvm::StringRef> toRefs(std::vector<std::string> V) {
  return std::vector<llvm::StringRef>(V.begin(), V.end());
}

static auto callsNames(std::vector<std::string> FunctionNames)
    -> decltype(callee(functionDecl())) {
  return callee(functionDecl(hasAnyName(toRefs(FunctionNames))));
}

static void emitDiagnostics(BoundNodes &Match, const Decl *D, BugReporter &BR,
                            AnalysisManager &AM,
                            const ObjCAutoreleaseWriteChecker *Checker) {
  AnalysisDeclContext *ADC = AM.getAnalysisDeclContext(D);

  const auto *PVD = Match.getNodeAs<ParmVarDecl>(ParamBind);
  QualType Ty = PVD->getType();
  if (Ty->getPointeeType().getObjCLifetime() != Qualifiers::OCL_Autoreleasing)
    return;
  const char *ActionMsg = "Write to";
  const auto *MarkedStmt = Match.getNodeAs<Expr>(ProblematicWriteBind);
  bool IsCapture = false;

  // Prefer to warn on write, but if not available, warn on capture.
  if (!MarkedStmt) {
    MarkedStmt = Match.getNodeAs<Expr>(CapturedBind);
    assert(MarkedStmt);
    ActionMsg = "Capture of";
    IsCapture = true;
  }

  SourceRange Range = MarkedStmt->getSourceRange();
  PathDiagnosticLocation Location = PathDiagnosticLocation::createBegin(
      MarkedStmt, BR.getSourceManager(), ADC);
  bool IsMethod = Match.getNodeAs<ObjCMethodDecl>(IsMethodBind) != nullptr;
  const char *Name = IsMethod ? "method" : "function";

  BR.EmitBasicReport(
      ADC->getDecl(), Checker,
      /*Name=*/(llvm::Twine(ActionMsg)
                + " autoreleasing out parameter inside autorelease pool").str(),
      /*Category=*/"Memory",
      (llvm::Twine(ActionMsg) + " autoreleasing out parameter " +
       (IsCapture ? "'" + PVD->getName() + "'" + " " : "") + "inside " +
       "autorelease pool that may exit before " + Name + " returns; consider "
       "writing first to a strong local variable declared outside of the block")
          .str(),
      Location,
      Range);
}

void ObjCAutoreleaseWriteChecker::checkASTCodeBody(const Decl *D,
                                                  AnalysisManager &AM,
                                                  BugReporter &BR) const {

  auto DoublePointerParamM =
      parmVarDecl(hasType(hasCanonicalType(pointerType(
                      pointee(hasCanonicalType(objcObjectPointerType()))))))
          .bind(ParamBind);

  auto ReferencedParamM =
      declRefExpr(to(parmVarDecl(DoublePointerParamM))).bind(CapturedBind);

  // Write into a binded object, e.g. *ParamBind = X.
  auto WritesIntoM = binaryOperator(
    hasLHS(unaryOperator(
        hasOperatorName("*"),
        hasUnaryOperand(
          ignoringParenImpCasts(ReferencedParamM))
    )),
    hasOperatorName("=")
  ).bind(ProblematicWriteBind);

  auto ArgumentCaptureM = hasAnyArgument(
    ignoringParenImpCasts(ReferencedParamM));
  auto CapturedInParamM = stmt(anyOf(
      callExpr(ArgumentCaptureM),
      objcMessageExpr(ArgumentCaptureM)));

  // WritesIntoM happens inside a block passed as an argument.
  auto WritesOrCapturesInBlockM = hasAnyArgument(allOf(
      hasType(hasCanonicalType(blockPointerType())),
      forEachDescendant(
        stmt(anyOf(WritesIntoM, CapturedInParamM))
      )));

  auto BlockPassedToMarkedFuncM = stmt(anyOf(
    callExpr(allOf(
      callsNames(FunctionsWithAutoreleasingPool), WritesOrCapturesInBlockM)),
    objcMessageExpr(allOf(
       hasAnySelector(toRefs(SelectorsWithAutoreleasingPool)),
       WritesOrCapturesInBlockM))
  ));

  auto HasParamAndWritesInMarkedFuncM = allOf(
      hasAnyParameter(DoublePointerParamM),
      forEachDescendant(BlockPassedToMarkedFuncM));

  auto MatcherM = decl(anyOf(
      objcMethodDecl(HasParamAndWritesInMarkedFuncM).bind(IsMethodBind),
      functionDecl(HasParamAndWritesInMarkedFuncM),
      blockDecl(HasParamAndWritesInMarkedFuncM)));

  auto Matches = match(MatcherM, *D, AM.getASTContext());
  for (BoundNodes Match : Matches)
    emitDiagnostics(Match, D, BR, AM, this);
}

void ento::registerAutoreleaseWriteChecker(CheckerManager &Mgr) {
  Mgr.registerChecker<ObjCAutoreleaseWriteChecker>();
}
