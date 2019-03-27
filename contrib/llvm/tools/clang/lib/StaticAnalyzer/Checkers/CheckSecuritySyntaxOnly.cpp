//==- CheckSecuritySyntaxOnly.cpp - Basic security checks --------*- C++ -*-==//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
//  This file defines a set of flow-insensitive security checks.
//
//===----------------------------------------------------------------------===//

#include "clang/StaticAnalyzer/Checkers/BuiltinCheckerRegistration.h"
#include "clang/AST/StmtVisitor.h"
#include "clang/Analysis/AnalysisDeclContext.h"
#include "clang/Basic/TargetInfo.h"
#include "clang/StaticAnalyzer/Core/BugReporter/BugReporter.h"
#include "clang/StaticAnalyzer/Core/Checker.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/AnalysisManager.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/ADT/StringSwitch.h"
#include "llvm/Support/raw_ostream.h"

using namespace clang;
using namespace ento;

static bool isArc4RandomAvailable(const ASTContext &Ctx) {
  const llvm::Triple &T = Ctx.getTargetInfo().getTriple();
  return T.getVendor() == llvm::Triple::Apple ||
         T.getOS() == llvm::Triple::CloudABI ||
         T.isOSFreeBSD() ||
         T.isOSNetBSD() ||
         T.isOSOpenBSD() ||
         T.isOSDragonFly();
}

namespace {
struct ChecksFilter {
  DefaultBool check_bcmp;
  DefaultBool check_bcopy;
  DefaultBool check_bzero;
  DefaultBool check_gets;
  DefaultBool check_getpw;
  DefaultBool check_mktemp;
  DefaultBool check_mkstemp;
  DefaultBool check_strcpy;
  DefaultBool check_rand;
  DefaultBool check_vfork;
  DefaultBool check_FloatLoopCounter;
  DefaultBool check_UncheckedReturn;

  CheckName checkName_bcmp;
  CheckName checkName_bcopy;
  CheckName checkName_bzero;
  CheckName checkName_gets;
  CheckName checkName_getpw;
  CheckName checkName_mktemp;
  CheckName checkName_mkstemp;
  CheckName checkName_strcpy;
  CheckName checkName_rand;
  CheckName checkName_vfork;
  CheckName checkName_FloatLoopCounter;
  CheckName checkName_UncheckedReturn;
};

class WalkAST : public StmtVisitor<WalkAST> {
  BugReporter &BR;
  AnalysisDeclContext* AC;
  enum { num_setids = 6 };
  IdentifierInfo *II_setid[num_setids];

  const bool CheckRand;
  const ChecksFilter &filter;

public:
  WalkAST(BugReporter &br, AnalysisDeclContext* ac,
          const ChecksFilter &f)
  : BR(br), AC(ac), II_setid(),
    CheckRand(isArc4RandomAvailable(BR.getContext())),
    filter(f) {}

  // Statement visitor methods.
  void VisitCallExpr(CallExpr *CE);
  void VisitForStmt(ForStmt *S);
  void VisitCompoundStmt (CompoundStmt *S);
  void VisitStmt(Stmt *S) { VisitChildren(S); }

  void VisitChildren(Stmt *S);

  // Helpers.
  bool checkCall_strCommon(const CallExpr *CE, const FunctionDecl *FD);

  typedef void (WalkAST::*FnCheck)(const CallExpr *, const FunctionDecl *);

  // Checker-specific methods.
  void checkLoopConditionForFloat(const ForStmt *FS);
  void checkCall_bcmp(const CallExpr *CE, const FunctionDecl *FD);
  void checkCall_bcopy(const CallExpr *CE, const FunctionDecl *FD);
  void checkCall_bzero(const CallExpr *CE, const FunctionDecl *FD);
  void checkCall_gets(const CallExpr *CE, const FunctionDecl *FD);
  void checkCall_getpw(const CallExpr *CE, const FunctionDecl *FD);
  void checkCall_mktemp(const CallExpr *CE, const FunctionDecl *FD);
  void checkCall_mkstemp(const CallExpr *CE, const FunctionDecl *FD);
  void checkCall_strcpy(const CallExpr *CE, const FunctionDecl *FD);
  void checkCall_strcat(const CallExpr *CE, const FunctionDecl *FD);
  void checkCall_rand(const CallExpr *CE, const FunctionDecl *FD);
  void checkCall_random(const CallExpr *CE, const FunctionDecl *FD);
  void checkCall_vfork(const CallExpr *CE, const FunctionDecl *FD);
  void checkUncheckedReturnValue(CallExpr *CE);
};
} // end anonymous namespace

//===----------------------------------------------------------------------===//
// AST walking.
//===----------------------------------------------------------------------===//

void WalkAST::VisitChildren(Stmt *S) {
  for (Stmt *Child : S->children())
    if (Child)
      Visit(Child);
}

void WalkAST::VisitCallExpr(CallExpr *CE) {
  // Get the callee.
  const FunctionDecl *FD = CE->getDirectCallee();

  if (!FD)
    return;

  // Get the name of the callee. If it's a builtin, strip off the prefix.
  IdentifierInfo *II = FD->getIdentifier();
  if (!II)   // if no identifier, not a simple C function
    return;
  StringRef Name = II->getName();
  if (Name.startswith("__builtin_"))
    Name = Name.substr(10);

  // Set the evaluation function by switching on the callee name.
  FnCheck evalFunction = llvm::StringSwitch<FnCheck>(Name)
    .Case("bcmp", &WalkAST::checkCall_bcmp)
    .Case("bcopy", &WalkAST::checkCall_bcopy)
    .Case("bzero", &WalkAST::checkCall_bzero)
    .Case("gets", &WalkAST::checkCall_gets)
    .Case("getpw", &WalkAST::checkCall_getpw)
    .Case("mktemp", &WalkAST::checkCall_mktemp)
    .Case("mkstemp", &WalkAST::checkCall_mkstemp)
    .Case("mkdtemp", &WalkAST::checkCall_mkstemp)
    .Case("mkstemps", &WalkAST::checkCall_mkstemp)
    .Cases("strcpy", "__strcpy_chk", &WalkAST::checkCall_strcpy)
    .Cases("strcat", "__strcat_chk", &WalkAST::checkCall_strcat)
    .Case("drand48", &WalkAST::checkCall_rand)
    .Case("erand48", &WalkAST::checkCall_rand)
    .Case("jrand48", &WalkAST::checkCall_rand)
    .Case("lrand48", &WalkAST::checkCall_rand)
    .Case("mrand48", &WalkAST::checkCall_rand)
    .Case("nrand48", &WalkAST::checkCall_rand)
    .Case("lcong48", &WalkAST::checkCall_rand)
    .Case("rand", &WalkAST::checkCall_rand)
    .Case("rand_r", &WalkAST::checkCall_rand)
    .Case("random", &WalkAST::checkCall_random)
    .Case("vfork", &WalkAST::checkCall_vfork)
    .Default(nullptr);

  // If the callee isn't defined, it is not of security concern.
  // Check and evaluate the call.
  if (evalFunction)
    (this->*evalFunction)(CE, FD);

  // Recurse and check children.
  VisitChildren(CE);
}

void WalkAST::VisitCompoundStmt(CompoundStmt *S) {
  for (Stmt *Child : S->children())
    if (Child) {
      if (CallExpr *CE = dyn_cast<CallExpr>(Child))
        checkUncheckedReturnValue(CE);
      Visit(Child);
    }
}

void WalkAST::VisitForStmt(ForStmt *FS) {
  checkLoopConditionForFloat(FS);

  // Recurse and check children.
  VisitChildren(FS);
}

//===----------------------------------------------------------------------===//
// Check: floating point variable used as loop counter.
// Originally: <rdar://problem/6336718>
// Implements: CERT security coding advisory FLP-30.
//===----------------------------------------------------------------------===//

static const DeclRefExpr*
getIncrementedVar(const Expr *expr, const VarDecl *x, const VarDecl *y) {
  expr = expr->IgnoreParenCasts();

  if (const BinaryOperator *B = dyn_cast<BinaryOperator>(expr)) {
    if (!(B->isAssignmentOp() || B->isCompoundAssignmentOp() ||
          B->getOpcode() == BO_Comma))
      return nullptr;

    if (const DeclRefExpr *lhs = getIncrementedVar(B->getLHS(), x, y))
      return lhs;

    if (const DeclRefExpr *rhs = getIncrementedVar(B->getRHS(), x, y))
      return rhs;

    return nullptr;
  }

  if (const DeclRefExpr *DR = dyn_cast<DeclRefExpr>(expr)) {
    const NamedDecl *ND = DR->getDecl();
    return ND == x || ND == y ? DR : nullptr;
  }

  if (const UnaryOperator *U = dyn_cast<UnaryOperator>(expr))
    return U->isIncrementDecrementOp()
      ? getIncrementedVar(U->getSubExpr(), x, y) : nullptr;

  return nullptr;
}

/// CheckLoopConditionForFloat - This check looks for 'for' statements that
///  use a floating point variable as a loop counter.
///  CERT: FLP30-C, FLP30-CPP.
///
void WalkAST::checkLoopConditionForFloat(const ForStmt *FS) {
  if (!filter.check_FloatLoopCounter)
    return;

  // Does the loop have a condition?
  const Expr *condition = FS->getCond();

  if (!condition)
    return;

  // Does the loop have an increment?
  const Expr *increment = FS->getInc();

  if (!increment)
    return;

  // Strip away '()' and casts.
  condition = condition->IgnoreParenCasts();
  increment = increment->IgnoreParenCasts();

  // Is the loop condition a comparison?
  const BinaryOperator *B = dyn_cast<BinaryOperator>(condition);

  if (!B)
    return;

  // Is this a comparison?
  if (!(B->isRelationalOp() || B->isEqualityOp()))
    return;

  // Are we comparing variables?
  const DeclRefExpr *drLHS =
    dyn_cast<DeclRefExpr>(B->getLHS()->IgnoreParenLValueCasts());
  const DeclRefExpr *drRHS =
    dyn_cast<DeclRefExpr>(B->getRHS()->IgnoreParenLValueCasts());

  // Does at least one of the variables have a floating point type?
  drLHS = drLHS && drLHS->getType()->isRealFloatingType() ? drLHS : nullptr;
  drRHS = drRHS && drRHS->getType()->isRealFloatingType() ? drRHS : nullptr;

  if (!drLHS && !drRHS)
    return;

  const VarDecl *vdLHS = drLHS ? dyn_cast<VarDecl>(drLHS->getDecl()) : nullptr;
  const VarDecl *vdRHS = drRHS ? dyn_cast<VarDecl>(drRHS->getDecl()) : nullptr;

  if (!vdLHS && !vdRHS)
    return;

  // Does either variable appear in increment?
  const DeclRefExpr *drInc = getIncrementedVar(increment, vdLHS, vdRHS);

  if (!drInc)
    return;

  // Emit the error.  First figure out which DeclRefExpr in the condition
  // referenced the compared variable.
  assert(drInc->getDecl());
  const DeclRefExpr *drCond = vdLHS == drInc->getDecl() ? drLHS : drRHS;

  SmallVector<SourceRange, 2> ranges;
  SmallString<256> sbuf;
  llvm::raw_svector_ostream os(sbuf);

  os << "Variable '" << drCond->getDecl()->getName()
     << "' with floating point type '" << drCond->getType().getAsString()
     << "' should not be used as a loop counter";

  ranges.push_back(drCond->getSourceRange());
  ranges.push_back(drInc->getSourceRange());

  const char *bugType = "Floating point variable used as loop counter";

  PathDiagnosticLocation FSLoc =
    PathDiagnosticLocation::createBegin(FS, BR.getSourceManager(), AC);
  BR.EmitBasicReport(AC->getDecl(), filter.checkName_FloatLoopCounter,
                     bugType, "Security", os.str(),
                     FSLoc, ranges);
}

//===----------------------------------------------------------------------===//
// Check: Any use of bcmp.
// CWE-477: Use of Obsolete Functions
// bcmp was deprecated in POSIX.1-2008
//===----------------------------------------------------------------------===//

void WalkAST::checkCall_bcmp(const CallExpr *CE, const FunctionDecl *FD) {
  if (!filter.check_bcmp)
    return;

  const FunctionProtoType *FPT = FD->getType()->getAs<FunctionProtoType>();
  if (!FPT)
    return;

  // Verify that the function takes three arguments.
  if (FPT->getNumParams() != 3)
    return;

  for (int i = 0; i < 2; i++) {
    // Verify the first and second argument type is void*.
    const PointerType *PT = FPT->getParamType(i)->getAs<PointerType>();
    if (!PT)
      return;

    if (PT->getPointeeType().getUnqualifiedType() != BR.getContext().VoidTy)
      return;
  }

  // Verify the third argument type is integer.
  if (!FPT->getParamType(2)->isIntegralOrUnscopedEnumerationType())
    return;

  // Issue a warning.
  PathDiagnosticLocation CELoc =
    PathDiagnosticLocation::createBegin(CE, BR.getSourceManager(), AC);
  BR.EmitBasicReport(AC->getDecl(), filter.checkName_bcmp,
                     "Use of deprecated function in call to 'bcmp()'",
                     "Security",
                     "The bcmp() function is obsoleted by memcmp().",
                     CELoc, CE->getCallee()->getSourceRange());
}

//===----------------------------------------------------------------------===//
// Check: Any use of bcopy.
// CWE-477: Use of Obsolete Functions
// bcopy was deprecated in POSIX.1-2008
//===----------------------------------------------------------------------===//

void WalkAST::checkCall_bcopy(const CallExpr *CE, const FunctionDecl *FD) {
  if (!filter.check_bcopy)
    return;

  const FunctionProtoType *FPT = FD->getType()->getAs<FunctionProtoType>();
  if (!FPT)
    return;

  // Verify that the function takes three arguments.
  if (FPT->getNumParams() != 3)
    return;

  for (int i = 0; i < 2; i++) {
    // Verify the first and second argument type is void*.
    const PointerType *PT = FPT->getParamType(i)->getAs<PointerType>();
    if (!PT)
      return;

    if (PT->getPointeeType().getUnqualifiedType() != BR.getContext().VoidTy)
      return;
  }

  // Verify the third argument type is integer.
  if (!FPT->getParamType(2)->isIntegralOrUnscopedEnumerationType())
    return;

  // Issue a warning.
  PathDiagnosticLocation CELoc =
    PathDiagnosticLocation::createBegin(CE, BR.getSourceManager(), AC);
  BR.EmitBasicReport(AC->getDecl(), filter.checkName_bcopy,
                     "Use of deprecated function in call to 'bcopy()'",
                     "Security",
                     "The bcopy() function is obsoleted by memcpy() "
                     "or memmove().",
                     CELoc, CE->getCallee()->getSourceRange());
}

//===----------------------------------------------------------------------===//
// Check: Any use of bzero.
// CWE-477: Use of Obsolete Functions
// bzero was deprecated in POSIX.1-2008
//===----------------------------------------------------------------------===//

void WalkAST::checkCall_bzero(const CallExpr *CE, const FunctionDecl *FD) {
  if (!filter.check_bzero)
    return;

  const FunctionProtoType *FPT = FD->getType()->getAs<FunctionProtoType>();
  if (!FPT)
    return;

  // Verify that the function takes two arguments.
  if (FPT->getNumParams() != 2)
    return;

  // Verify the first argument type is void*.
  const PointerType *PT = FPT->getParamType(0)->getAs<PointerType>();
  if (!PT)
    return;

  if (PT->getPointeeType().getUnqualifiedType() != BR.getContext().VoidTy)
    return;

  // Verify the second argument type is integer.
  if (!FPT->getParamType(1)->isIntegralOrUnscopedEnumerationType())
    return;

  // Issue a warning.
  PathDiagnosticLocation CELoc =
    PathDiagnosticLocation::createBegin(CE, BR.getSourceManager(), AC);
  BR.EmitBasicReport(AC->getDecl(), filter.checkName_bzero,
                     "Use of deprecated function in call to 'bzero()'",
                     "Security",
                     "The bzero() function is obsoleted by memset().",
                     CELoc, CE->getCallee()->getSourceRange());
}


//===----------------------------------------------------------------------===//
// Check: Any use of 'gets' is insecure.
// Originally: <rdar://problem/6335715>
// Implements (part of): 300-BSI (buildsecurityin.us-cert.gov)
// CWE-242: Use of Inherently Dangerous Function
//===----------------------------------------------------------------------===//

void WalkAST::checkCall_gets(const CallExpr *CE, const FunctionDecl *FD) {
  if (!filter.check_gets)
    return;

  const FunctionProtoType *FPT = FD->getType()->getAs<FunctionProtoType>();
  if (!FPT)
    return;

  // Verify that the function takes a single argument.
  if (FPT->getNumParams() != 1)
    return;

  // Is the argument a 'char*'?
  const PointerType *PT = FPT->getParamType(0)->getAs<PointerType>();
  if (!PT)
    return;

  if (PT->getPointeeType().getUnqualifiedType() != BR.getContext().CharTy)
    return;

  // Issue a warning.
  PathDiagnosticLocation CELoc =
    PathDiagnosticLocation::createBegin(CE, BR.getSourceManager(), AC);
  BR.EmitBasicReport(AC->getDecl(), filter.checkName_gets,
                     "Potential buffer overflow in call to 'gets'",
                     "Security",
                     "Call to function 'gets' is extremely insecure as it can "
                     "always result in a buffer overflow",
                     CELoc, CE->getCallee()->getSourceRange());
}

//===----------------------------------------------------------------------===//
// Check: Any use of 'getpwd' is insecure.
// CWE-477: Use of Obsolete Functions
//===----------------------------------------------------------------------===//

void WalkAST::checkCall_getpw(const CallExpr *CE, const FunctionDecl *FD) {
  if (!filter.check_getpw)
    return;

  const FunctionProtoType *FPT = FD->getType()->getAs<FunctionProtoType>();
  if (!FPT)
    return;

  // Verify that the function takes two arguments.
  if (FPT->getNumParams() != 2)
    return;

  // Verify the first argument type is integer.
  if (!FPT->getParamType(0)->isIntegralOrUnscopedEnumerationType())
    return;

  // Verify the second argument type is char*.
  const PointerType *PT = FPT->getParamType(1)->getAs<PointerType>();
  if (!PT)
    return;

  if (PT->getPointeeType().getUnqualifiedType() != BR.getContext().CharTy)
    return;

  // Issue a warning.
  PathDiagnosticLocation CELoc =
    PathDiagnosticLocation::createBegin(CE, BR.getSourceManager(), AC);
  BR.EmitBasicReport(AC->getDecl(), filter.checkName_getpw,
                     "Potential buffer overflow in call to 'getpw'",
                     "Security",
                     "The getpw() function is dangerous as it may overflow the "
                     "provided buffer. It is obsoleted by getpwuid().",
                     CELoc, CE->getCallee()->getSourceRange());
}

//===----------------------------------------------------------------------===//
// Check: Any use of 'mktemp' is insecure.  It is obsoleted by mkstemp().
// CWE-377: Insecure Temporary File
//===----------------------------------------------------------------------===//

void WalkAST::checkCall_mktemp(const CallExpr *CE, const FunctionDecl *FD) {
  if (!filter.check_mktemp) {
    // Fall back to the security check of looking for enough 'X's in the
    // format string, since that is a less severe warning.
    checkCall_mkstemp(CE, FD);
    return;
  }

  const FunctionProtoType *FPT = FD->getType()->getAs<FunctionProtoType>();
  if(!FPT)
    return;

  // Verify that the function takes a single argument.
  if (FPT->getNumParams() != 1)
    return;

  // Verify that the argument is Pointer Type.
  const PointerType *PT = FPT->getParamType(0)->getAs<PointerType>();
  if (!PT)
    return;

  // Verify that the argument is a 'char*'.
  if (PT->getPointeeType().getUnqualifiedType() != BR.getContext().CharTy)
    return;

  // Issue a warning.
  PathDiagnosticLocation CELoc =
    PathDiagnosticLocation::createBegin(CE, BR.getSourceManager(), AC);
  BR.EmitBasicReport(AC->getDecl(), filter.checkName_mktemp,
                     "Potential insecure temporary file in call 'mktemp'",
                     "Security",
                     "Call to function 'mktemp' is insecure as it always "
                     "creates or uses insecure temporary file.  Use 'mkstemp' "
                     "instead",
                     CELoc, CE->getCallee()->getSourceRange());
}


//===----------------------------------------------------------------------===//
// Check: Use of 'mkstemp', 'mktemp', 'mkdtemp' should contain at least 6 X's.
//===----------------------------------------------------------------------===//

void WalkAST::checkCall_mkstemp(const CallExpr *CE, const FunctionDecl *FD) {
  if (!filter.check_mkstemp)
    return;

  StringRef Name = FD->getIdentifier()->getName();
  std::pair<signed, signed> ArgSuffix =
    llvm::StringSwitch<std::pair<signed, signed> >(Name)
      .Case("mktemp", std::make_pair(0,-1))
      .Case("mkstemp", std::make_pair(0,-1))
      .Case("mkdtemp", std::make_pair(0,-1))
      .Case("mkstemps", std::make_pair(0,1))
      .Default(std::make_pair(-1, -1));

  assert(ArgSuffix.first >= 0 && "Unsupported function");

  // Check if the number of arguments is consistent with out expectations.
  unsigned numArgs = CE->getNumArgs();
  if ((signed) numArgs <= ArgSuffix.first)
    return;

  const StringLiteral *strArg =
    dyn_cast<StringLiteral>(CE->getArg((unsigned)ArgSuffix.first)
                              ->IgnoreParenImpCasts());

  // Currently we only handle string literals.  It is possible to do better,
  // either by looking at references to const variables, or by doing real
  // flow analysis.
  if (!strArg || strArg->getCharByteWidth() != 1)
    return;

  // Count the number of X's, taking into account a possible cutoff suffix.
  StringRef str = strArg->getString();
  unsigned numX = 0;
  unsigned n = str.size();

  // Take into account the suffix.
  unsigned suffix = 0;
  if (ArgSuffix.second >= 0) {
    const Expr *suffixEx = CE->getArg((unsigned)ArgSuffix.second);
    Expr::EvalResult EVResult;
    if (!suffixEx->EvaluateAsInt(EVResult, BR.getContext()))
      return;
    llvm::APSInt Result = EVResult.Val.getInt();
    // FIXME: Issue a warning.
    if (Result.isNegative())
      return;
    suffix = (unsigned) Result.getZExtValue();
    n = (n > suffix) ? n - suffix : 0;
  }

  for (unsigned i = 0; i < n; ++i)
    if (str[i] == 'X') ++numX;

  if (numX >= 6)
    return;

  // Issue a warning.
  PathDiagnosticLocation CELoc =
    PathDiagnosticLocation::createBegin(CE, BR.getSourceManager(), AC);
  SmallString<512> buf;
  llvm::raw_svector_ostream out(buf);
  out << "Call to '" << Name << "' should have at least 6 'X's in the"
    " format string to be secure (" << numX << " 'X'";
  if (numX != 1)
    out << 's';
  out << " seen";
  if (suffix) {
    out << ", " << suffix << " character";
    if (suffix > 1)
      out << 's';
    out << " used as a suffix";
  }
  out << ')';
  BR.EmitBasicReport(AC->getDecl(), filter.checkName_mkstemp,
                     "Insecure temporary file creation", "Security",
                     out.str(), CELoc, strArg->getSourceRange());
}

//===----------------------------------------------------------------------===//
// Check: Any use of 'strcpy' is insecure.
//
// CWE-119: Improper Restriction of Operations within
// the Bounds of a Memory Buffer
//===----------------------------------------------------------------------===//
void WalkAST::checkCall_strcpy(const CallExpr *CE, const FunctionDecl *FD) {
  if (!filter.check_strcpy)
    return;

  if (!checkCall_strCommon(CE, FD))
    return;

  const auto *Target = CE->getArg(0)->IgnoreImpCasts(),
             *Source = CE->getArg(1)->IgnoreImpCasts();

  if (const auto *Array = dyn_cast<ConstantArrayType>(Target->getType())) {
    uint64_t ArraySize = BR.getContext().getTypeSize(Array) / 8;
    if (const auto *String = dyn_cast<StringLiteral>(Source)) {
      if (ArraySize >= String->getLength() + 1)
        return;
    }
  }

  // Issue a warning.
  PathDiagnosticLocation CELoc =
    PathDiagnosticLocation::createBegin(CE, BR.getSourceManager(), AC);
  BR.EmitBasicReport(AC->getDecl(), filter.checkName_strcpy,
                     "Potential insecure memory buffer bounds restriction in "
                     "call 'strcpy'",
                     "Security",
                     "Call to function 'strcpy' is insecure as it does not "
                     "provide bounding of the memory buffer. Replace "
                     "unbounded copy functions with analogous functions that "
                     "support length arguments such as 'strlcpy'. CWE-119.",
                     CELoc, CE->getCallee()->getSourceRange());
}

//===----------------------------------------------------------------------===//
// Check: Any use of 'strcat' is insecure.
//
// CWE-119: Improper Restriction of Operations within
// the Bounds of a Memory Buffer
//===----------------------------------------------------------------------===//
void WalkAST::checkCall_strcat(const CallExpr *CE, const FunctionDecl *FD) {
  if (!filter.check_strcpy)
    return;

  if (!checkCall_strCommon(CE, FD))
    return;

  // Issue a warning.
  PathDiagnosticLocation CELoc =
    PathDiagnosticLocation::createBegin(CE, BR.getSourceManager(), AC);
  BR.EmitBasicReport(AC->getDecl(), filter.checkName_strcpy,
                     "Potential insecure memory buffer bounds restriction in "
                     "call 'strcat'",
                     "Security",
                     "Call to function 'strcat' is insecure as it does not "
                     "provide bounding of the memory buffer. Replace "
                     "unbounded copy functions with analogous functions that "
                     "support length arguments such as 'strlcat'. CWE-119.",
                     CELoc, CE->getCallee()->getSourceRange());
}

//===----------------------------------------------------------------------===//
// Common check for str* functions with no bounds parameters.
//===----------------------------------------------------------------------===//
bool WalkAST::checkCall_strCommon(const CallExpr *CE, const FunctionDecl *FD) {
  const FunctionProtoType *FPT = FD->getType()->getAs<FunctionProtoType>();
  if (!FPT)
    return false;

  // Verify the function takes two arguments, three in the _chk version.
  int numArgs = FPT->getNumParams();
  if (numArgs != 2 && numArgs != 3)
    return false;

  // Verify the type for both arguments.
  for (int i = 0; i < 2; i++) {
    // Verify that the arguments are pointers.
    const PointerType *PT = FPT->getParamType(i)->getAs<PointerType>();
    if (!PT)
      return false;

    // Verify that the argument is a 'char*'.
    if (PT->getPointeeType().getUnqualifiedType() != BR.getContext().CharTy)
      return false;
  }

  return true;
}

//===----------------------------------------------------------------------===//
// Check: Linear congruent random number generators should not be used
// Originally: <rdar://problem/63371000>
// CWE-338: Use of cryptographically weak prng
//===----------------------------------------------------------------------===//

void WalkAST::checkCall_rand(const CallExpr *CE, const FunctionDecl *FD) {
  if (!filter.check_rand || !CheckRand)
    return;

  const FunctionProtoType *FTP = FD->getType()->getAs<FunctionProtoType>();
  if (!FTP)
    return;

  if (FTP->getNumParams() == 1) {
    // Is the argument an 'unsigned short *'?
    // (Actually any integer type is allowed.)
    const PointerType *PT = FTP->getParamType(0)->getAs<PointerType>();
    if (!PT)
      return;

    if (! PT->getPointeeType()->isIntegralOrUnscopedEnumerationType())
      return;
  } else if (FTP->getNumParams() != 0)
    return;

  // Issue a warning.
  SmallString<256> buf1;
  llvm::raw_svector_ostream os1(buf1);
  os1 << '\'' << *FD << "' is a poor random number generator";

  SmallString<256> buf2;
  llvm::raw_svector_ostream os2(buf2);
  os2 << "Function '" << *FD
      << "' is obsolete because it implements a poor random number generator."
      << "  Use 'arc4random' instead";

  PathDiagnosticLocation CELoc =
    PathDiagnosticLocation::createBegin(CE, BR.getSourceManager(), AC);
  BR.EmitBasicReport(AC->getDecl(), filter.checkName_rand, os1.str(),
                     "Security", os2.str(), CELoc,
                     CE->getCallee()->getSourceRange());
}

//===----------------------------------------------------------------------===//
// Check: 'random' should not be used
// Originally: <rdar://problem/63371000>
//===----------------------------------------------------------------------===//

void WalkAST::checkCall_random(const CallExpr *CE, const FunctionDecl *FD) {
  if (!CheckRand || !filter.check_rand)
    return;

  const FunctionProtoType *FTP = FD->getType()->getAs<FunctionProtoType>();
  if (!FTP)
    return;

  // Verify that the function takes no argument.
  if (FTP->getNumParams() != 0)
    return;

  // Issue a warning.
  PathDiagnosticLocation CELoc =
    PathDiagnosticLocation::createBegin(CE, BR.getSourceManager(), AC);
  BR.EmitBasicReport(AC->getDecl(), filter.checkName_rand,
                     "'random' is not a secure random number generator",
                     "Security",
                     "The 'random' function produces a sequence of values that "
                     "an adversary may be able to predict.  Use 'arc4random' "
                     "instead", CELoc, CE->getCallee()->getSourceRange());
}

//===----------------------------------------------------------------------===//
// Check: 'vfork' should not be used.
// POS33-C: Do not use vfork().
//===----------------------------------------------------------------------===//

void WalkAST::checkCall_vfork(const CallExpr *CE, const FunctionDecl *FD) {
  if (!filter.check_vfork)
    return;

  // All calls to vfork() are insecure, issue a warning.
  PathDiagnosticLocation CELoc =
    PathDiagnosticLocation::createBegin(CE, BR.getSourceManager(), AC);
  BR.EmitBasicReport(AC->getDecl(), filter.checkName_vfork,
                     "Potential insecure implementation-specific behavior in "
                     "call 'vfork'",
                     "Security",
                     "Call to function 'vfork' is insecure as it can lead to "
                     "denial of service situations in the parent process. "
                     "Replace calls to vfork with calls to the safer "
                     "'posix_spawn' function",
                     CELoc, CE->getCallee()->getSourceRange());
}

//===----------------------------------------------------------------------===//
// Check: Should check whether privileges are dropped successfully.
// Originally: <rdar://problem/6337132>
//===----------------------------------------------------------------------===//

void WalkAST::checkUncheckedReturnValue(CallExpr *CE) {
  if (!filter.check_UncheckedReturn)
    return;

  const FunctionDecl *FD = CE->getDirectCallee();
  if (!FD)
    return;

  if (II_setid[0] == nullptr) {
    static const char * const identifiers[num_setids] = {
      "setuid", "setgid", "seteuid", "setegid",
      "setreuid", "setregid"
    };

    for (size_t i = 0; i < num_setids; i++)
      II_setid[i] = &BR.getContext().Idents.get(identifiers[i]);
  }

  const IdentifierInfo *id = FD->getIdentifier();
  size_t identifierid;

  for (identifierid = 0; identifierid < num_setids; identifierid++)
    if (id == II_setid[identifierid])
      break;

  if (identifierid >= num_setids)
    return;

  const FunctionProtoType *FTP = FD->getType()->getAs<FunctionProtoType>();
  if (!FTP)
    return;

  // Verify that the function takes one or two arguments (depending on
  //   the function).
  if (FTP->getNumParams() != (identifierid < 4 ? 1 : 2))
    return;

  // The arguments must be integers.
  for (unsigned i = 0; i < FTP->getNumParams(); i++)
    if (!FTP->getParamType(i)->isIntegralOrUnscopedEnumerationType())
      return;

  // Issue a warning.
  SmallString<256> buf1;
  llvm::raw_svector_ostream os1(buf1);
  os1 << "Return value is not checked in call to '" << *FD << '\'';

  SmallString<256> buf2;
  llvm::raw_svector_ostream os2(buf2);
  os2 << "The return value from the call to '" << *FD
      << "' is not checked.  If an error occurs in '" << *FD
      << "', the following code may execute with unexpected privileges";

  PathDiagnosticLocation CELoc =
    PathDiagnosticLocation::createBegin(CE, BR.getSourceManager(), AC);
  BR.EmitBasicReport(AC->getDecl(), filter.checkName_UncheckedReturn, os1.str(),
                     "Security", os2.str(), CELoc,
                     CE->getCallee()->getSourceRange());
}

//===----------------------------------------------------------------------===//
// SecuritySyntaxChecker
//===----------------------------------------------------------------------===//

namespace {
class SecuritySyntaxChecker : public Checker<check::ASTCodeBody> {
public:
  ChecksFilter filter;

  void checkASTCodeBody(const Decl *D, AnalysisManager& mgr,
                        BugReporter &BR) const {
    WalkAST walker(BR, mgr.getAnalysisDeclContext(D), filter);
    walker.Visit(D->getBody());
  }
};
}

#define REGISTER_CHECKER(name)                                                 \
  void ento::register##name(CheckerManager &mgr) {                             \
    SecuritySyntaxChecker *checker =                                           \
        mgr.registerChecker<SecuritySyntaxChecker>();                          \
    checker->filter.check_##name = true;                                       \
    checker->filter.checkName_##name = mgr.getCurrentCheckName();              \
  }

REGISTER_CHECKER(bcmp)
REGISTER_CHECKER(bcopy)
REGISTER_CHECKER(bzero)
REGISTER_CHECKER(gets)
REGISTER_CHECKER(getpw)
REGISTER_CHECKER(mkstemp)
REGISTER_CHECKER(mktemp)
REGISTER_CHECKER(strcpy)
REGISTER_CHECKER(rand)
REGISTER_CHECKER(vfork)
REGISTER_CHECKER(FloatLoopCounter)
REGISTER_CHECKER(UncheckedReturn)


