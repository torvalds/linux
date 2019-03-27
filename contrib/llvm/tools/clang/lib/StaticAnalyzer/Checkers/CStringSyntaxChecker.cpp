//== CStringSyntaxChecker.cpp - CoreFoundation containers API *- C++ -*-==//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// An AST checker that looks for common pitfalls when using C string APIs.
//  - Identifies erroneous patterns in the last argument to strncat - the number
//    of bytes to copy.
//
//===----------------------------------------------------------------------===//
#include "clang/StaticAnalyzer/Checkers/BuiltinCheckerRegistration.h"
#include "clang/AST/Expr.h"
#include "clang/AST/OperationKinds.h"
#include "clang/AST/StmtVisitor.h"
#include "clang/Analysis/AnalysisDeclContext.h"
#include "clang/Basic/TargetInfo.h"
#include "clang/Basic/TypeTraits.h"
#include "clang/StaticAnalyzer/Core/BugReporter/BugReporter.h"
#include "clang/StaticAnalyzer/Core/Checker.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/AnalysisManager.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/CheckerContext.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/Support/raw_ostream.h"

using namespace clang;
using namespace ento;

namespace {
class WalkAST: public StmtVisitor<WalkAST> {
  const CheckerBase *Checker;
  BugReporter &BR;
  AnalysisDeclContext* AC;

  /// Check if two expressions refer to the same declaration.
  bool sameDecl(const Expr *A1, const Expr *A2) {
    if (const auto *D1 = dyn_cast<DeclRefExpr>(A1->IgnoreParenCasts()))
      if (const auto *D2 = dyn_cast<DeclRefExpr>(A2->IgnoreParenCasts()))
        return D1->getDecl() == D2->getDecl();
    return false;
  }

  /// Check if the expression E is a sizeof(WithArg).
  bool isSizeof(const Expr *E, const Expr *WithArg) {
    if (const auto *UE = dyn_cast<UnaryExprOrTypeTraitExpr>(E))
      if (UE->getKind() == UETT_SizeOf && !UE->isArgumentType())
        return sameDecl(UE->getArgumentExpr(), WithArg);
    return false;
  }

  /// Check if the expression E is a strlen(WithArg).
  bool isStrlen(const Expr *E, const Expr *WithArg) {
    if (const auto *CE = dyn_cast<CallExpr>(E)) {
      const FunctionDecl *FD = CE->getDirectCallee();
      if (!FD)
        return false;
      return (CheckerContext::isCLibraryFunction(FD, "strlen") &&
              sameDecl(CE->getArg(0), WithArg));
    }
    return false;
  }

  /// Check if the expression is an integer literal with value 1.
  bool isOne(const Expr *E) {
    if (const auto *IL = dyn_cast<IntegerLiteral>(E))
      return (IL->getValue().isIntN(1));
    return false;
  }

  StringRef getPrintableName(const Expr *E) {
    if (const auto *D = dyn_cast<DeclRefExpr>(E->IgnoreParenCasts()))
      return D->getDecl()->getName();
    return StringRef();
  }

  /// Identify erroneous patterns in the last argument to strncat - the number
  /// of bytes to copy.
  bool containsBadStrncatPattern(const CallExpr *CE);

  /// Identify erroneous patterns in the last argument to strlcpy - the number
  /// of bytes to copy.
  /// The bad pattern checked is when the size is known
  /// to be larger than the destination can handle.
  ///   char dst[2];
  ///   size_t cpy = 4;
  ///   strlcpy(dst, "abcd", sizeof("abcd") - 1);
  ///   strlcpy(dst, "abcd", 4);
  ///   strlcpy(dst + 3, "abcd", 2);
  ///   strlcpy(dst, "abcd", cpy);
  /// Identify erroneous patterns in the last argument to strlcat - the number
  /// of bytes to copy.
  /// The bad pattern checked is when the last argument is basically
  /// pointing to the destination buffer size or argument larger or
  /// equal to.
  ///   char dst[2];
  ///   strlcat(dst, src2, sizeof(dst));
  ///   strlcat(dst, src2, 2);
  ///   strlcat(dst, src2, 10);
  bool containsBadStrlcpyStrlcatPattern(const CallExpr *CE);

public:
  WalkAST(const CheckerBase *Checker, BugReporter &BR, AnalysisDeclContext *AC)
      : Checker(Checker), BR(BR), AC(AC) {}

  // Statement visitor methods.
  void VisitChildren(Stmt *S);
  void VisitStmt(Stmt *S) {
    VisitChildren(S);
  }
  void VisitCallExpr(CallExpr *CE);
};
} // end anonymous namespace

// The correct size argument should look like following:
//   strncat(dst, src, sizeof(dst) - strlen(dest) - 1);
// We look for the following anti-patterns:
//   - strncat(dst, src, sizeof(dst) - strlen(dst));
//   - strncat(dst, src, sizeof(dst) - 1);
//   - strncat(dst, src, sizeof(dst));
bool WalkAST::containsBadStrncatPattern(const CallExpr *CE) {
  if (CE->getNumArgs() != 3)
    return false;
  const Expr *DstArg = CE->getArg(0);
  const Expr *SrcArg = CE->getArg(1);
  const Expr *LenArg = CE->getArg(2);

  // Identify wrong size expressions, which are commonly used instead.
  if (const auto *BE = dyn_cast<BinaryOperator>(LenArg->IgnoreParenCasts())) {
    // - sizeof(dst) - strlen(dst)
    if (BE->getOpcode() == BO_Sub) {
      const Expr *L = BE->getLHS();
      const Expr *R = BE->getRHS();
      if (isSizeof(L, DstArg) && isStrlen(R, DstArg))
        return true;

      // - sizeof(dst) - 1
      if (isSizeof(L, DstArg) && isOne(R->IgnoreParenCasts()))
        return true;
    }
  }
  // - sizeof(dst)
  if (isSizeof(LenArg, DstArg))
    return true;

  // - sizeof(src)
  if (isSizeof(LenArg, SrcArg))
    return true;
  return false;
}

bool WalkAST::containsBadStrlcpyStrlcatPattern(const CallExpr *CE) {
  if (CE->getNumArgs() != 3)
    return false;
  const FunctionDecl *FD = CE->getDirectCallee();
  bool Append = CheckerContext::isCLibraryFunction(FD, "strlcat");
  const Expr *DstArg = CE->getArg(0);
  const Expr *LenArg = CE->getArg(2);

  const auto *DstArgDecl = dyn_cast<DeclRefExpr>(DstArg->IgnoreParenImpCasts());
  const auto *LenArgDecl = dyn_cast<DeclRefExpr>(LenArg->IgnoreParenLValueCasts());
  uint64_t DstOff = 0;
  if (isSizeof(LenArg, DstArg))
    return false;
  // - size_t dstlen = sizeof(dst)
  if (LenArgDecl) {
    const auto *LenArgVal = dyn_cast<VarDecl>(LenArgDecl->getDecl());
    if (LenArgVal->getInit())
      LenArg = LenArgVal->getInit();
  }

  // - integral value
  // We try to figure out if the last argument is possibly longer
  // than the destination can possibly handle if its size can be defined.
  if (const auto *IL = dyn_cast<IntegerLiteral>(LenArg->IgnoreParenImpCasts())) {
    uint64_t ILRawVal = IL->getValue().getZExtValue();

    // Case when there is pointer arithmetic on the destination buffer
    // especially when we offset from the base decreasing the
    // buffer length accordingly.
    if (!DstArgDecl) {
      if (const auto *BE = dyn_cast<BinaryOperator>(DstArg->IgnoreParenImpCasts())) {
        DstArgDecl = dyn_cast<DeclRefExpr>(BE->getLHS()->IgnoreParenImpCasts());
        if (BE->getOpcode() == BO_Add) {
          if ((IL = dyn_cast<IntegerLiteral>(BE->getRHS()->IgnoreParenImpCasts()))) {
            DstOff = IL->getValue().getZExtValue();
          }
        }
      }
    }
    if (DstArgDecl) {
      if (const auto *Buffer = dyn_cast<ConstantArrayType>(DstArgDecl->getType())) {
        ASTContext &C = BR.getContext();
        uint64_t BufferLen = C.getTypeSize(Buffer) / 8;
        auto RemainingBufferLen = BufferLen - DstOff;
        if (Append) {
          if (RemainingBufferLen <= ILRawVal)
            return true;
        } else {
          if (RemainingBufferLen < ILRawVal)
            return true;
        }
      }
    }
  }

  return false;
}

void WalkAST::VisitCallExpr(CallExpr *CE) {
  const FunctionDecl *FD = CE->getDirectCallee();
  if (!FD)
    return;

  if (CheckerContext::isCLibraryFunction(FD, "strncat")) {
    if (containsBadStrncatPattern(CE)) {
      const Expr *DstArg = CE->getArg(0);
      const Expr *LenArg = CE->getArg(2);
      PathDiagnosticLocation Loc =
        PathDiagnosticLocation::createBegin(LenArg, BR.getSourceManager(), AC);

      StringRef DstName = getPrintableName(DstArg);

      SmallString<256> S;
      llvm::raw_svector_ostream os(S);
      os << "Potential buffer overflow. ";
      if (!DstName.empty()) {
        os << "Replace with 'sizeof(" << DstName << ") "
              "- strlen(" << DstName <<") - 1'";
        os << " or u";
      } else
        os << "U";
      os << "se a safer 'strlcat' API";

      BR.EmitBasicReport(FD, Checker, "Anti-pattern in the argument",
                         "C String API", os.str(), Loc,
                         LenArg->getSourceRange());
    }
  } else if (CheckerContext::isCLibraryFunction(FD, "strlcpy") ||
             CheckerContext::isCLibraryFunction(FD, "strlcat")) {
    if (containsBadStrlcpyStrlcatPattern(CE)) {
      const Expr *DstArg = CE->getArg(0);
      const Expr *LenArg = CE->getArg(2);
      PathDiagnosticLocation Loc =
        PathDiagnosticLocation::createBegin(LenArg, BR.getSourceManager(), AC);

      StringRef DstName = getPrintableName(DstArg);

      SmallString<256> S;
      llvm::raw_svector_ostream os(S);
      os << "The third argument allows to potentially copy more bytes than it should. ";
      os << "Replace with the value ";
      if (!DstName.empty())
          os << "sizeof(" << DstName << ")";
      else
          os << "sizeof(<destination buffer>)";
      os << " or lower";

      BR.EmitBasicReport(FD, Checker, "Anti-pattern in the argument",
              "C String API", os.str(), Loc,
              LenArg->getSourceRange());
    }
  }

  // Recurse and check children.
  VisitChildren(CE);
}

void WalkAST::VisitChildren(Stmt *S) {
  for (Stmt *Child : S->children())
    if (Child)
      Visit(Child);
}

namespace {
class CStringSyntaxChecker: public Checker<check::ASTCodeBody> {
public:

  void checkASTCodeBody(const Decl *D, AnalysisManager& Mgr,
      BugReporter &BR) const {
    WalkAST walker(this, BR, Mgr.getAnalysisDeclContext(D));
    walker.Visit(D->getBody());
  }
};
}

void ento::registerCStringSyntaxChecker(CheckerManager &mgr) {
  mgr.registerChecker<CStringSyntaxChecker>();
}

