//=== StackAddrEscapeChecker.cpp ----------------------------------*- C++ -*--//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file defines stack address leak checker, which checks if an invalid
// stack address is stored into a global or heap location. See CERT DCL30-C.
//
//===----------------------------------------------------------------------===//

#include "clang/StaticAnalyzer/Checkers/BuiltinCheckerRegistration.h"
#include "clang/AST/ExprCXX.h"
#include "clang/Basic/SourceManager.h"
#include "clang/StaticAnalyzer/Core/BugReporter/BugType.h"
#include "clang/StaticAnalyzer/Core/Checker.h"
#include "clang/StaticAnalyzer/Core/CheckerManager.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/CallEvent.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/CheckerContext.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/ProgramState.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/Support/raw_ostream.h"
using namespace clang;
using namespace ento;

namespace {
class StackAddrEscapeChecker
    : public Checker<check::PreCall, check::PreStmt<ReturnStmt>,
                     check::EndFunction> {
  mutable IdentifierInfo *dispatch_semaphore_tII;
  mutable std::unique_ptr<BuiltinBug> BT_stackleak;
  mutable std::unique_ptr<BuiltinBug> BT_returnstack;
  mutable std::unique_ptr<BuiltinBug> BT_capturedstackasync;
  mutable std::unique_ptr<BuiltinBug> BT_capturedstackret;

public:
  enum CheckKind {
    CK_StackAddrEscapeChecker,
    CK_StackAddrAsyncEscapeChecker,
    CK_NumCheckKinds
  };

  DefaultBool ChecksEnabled[CK_NumCheckKinds];

  void checkPreCall(const CallEvent &Call, CheckerContext &C) const;
  void checkPreStmt(const ReturnStmt *RS, CheckerContext &C) const;
  void checkEndFunction(const ReturnStmt *RS, CheckerContext &Ctx) const;

private:
  void checkReturnedBlockCaptures(const BlockDataRegion &B,
                                  CheckerContext &C) const;
  void checkAsyncExecutedBlockCaptures(const BlockDataRegion &B,
                                       CheckerContext &C) const;
  void EmitStackError(CheckerContext &C, const MemRegion *R,
                      const Expr *RetE) const;
  bool isSemaphoreCaptured(const BlockDecl &B) const;
  static SourceRange genName(raw_ostream &os, const MemRegion *R,
                             ASTContext &Ctx);
  static SmallVector<const MemRegion *, 4>
  getCapturedStackRegions(const BlockDataRegion &B, CheckerContext &C);
  static bool isArcManagedBlock(const MemRegion *R, CheckerContext &C);
  static bool isNotInCurrentFrame(const MemRegion *R, CheckerContext &C);
};
} // namespace

SourceRange StackAddrEscapeChecker::genName(raw_ostream &os, const MemRegion *R,
                                            ASTContext &Ctx) {
  // Get the base region, stripping away fields and elements.
  R = R->getBaseRegion();
  SourceManager &SM = Ctx.getSourceManager();
  SourceRange range;
  os << "Address of ";

  // Check if the region is a compound literal.
  if (const auto *CR = dyn_cast<CompoundLiteralRegion>(R)) {
    const CompoundLiteralExpr *CL = CR->getLiteralExpr();
    os << "stack memory associated with a compound literal "
          "declared on line "
       << SM.getExpansionLineNumber(CL->getBeginLoc()) << " returned to caller";
    range = CL->getSourceRange();
  } else if (const auto *AR = dyn_cast<AllocaRegion>(R)) {
    const Expr *ARE = AR->getExpr();
    SourceLocation L = ARE->getBeginLoc();
    range = ARE->getSourceRange();
    os << "stack memory allocated by call to alloca() on line "
       << SM.getExpansionLineNumber(L);
  } else if (const auto *BR = dyn_cast<BlockDataRegion>(R)) {
    const BlockDecl *BD = BR->getCodeRegion()->getDecl();
    SourceLocation L = BD->getBeginLoc();
    range = BD->getSourceRange();
    os << "stack-allocated block declared on line "
       << SM.getExpansionLineNumber(L);
  } else if (const auto *VR = dyn_cast<VarRegion>(R)) {
    os << "stack memory associated with local variable '" << VR->getString()
       << '\'';
    range = VR->getDecl()->getSourceRange();
  } else if (const auto *TOR = dyn_cast<CXXTempObjectRegion>(R)) {
    QualType Ty = TOR->getValueType().getLocalUnqualifiedType();
    os << "stack memory associated with temporary object of type '";
    Ty.print(os, Ctx.getPrintingPolicy());
    os << "'";
    range = TOR->getExpr()->getSourceRange();
  } else {
    llvm_unreachable("Invalid region in ReturnStackAddressChecker.");
  }

  return range;
}

bool StackAddrEscapeChecker::isArcManagedBlock(const MemRegion *R,
                                               CheckerContext &C) {
  assert(R && "MemRegion should not be null");
  return C.getASTContext().getLangOpts().ObjCAutoRefCount &&
         isa<BlockDataRegion>(R);
}

bool StackAddrEscapeChecker::isNotInCurrentFrame(const MemRegion *R,
                                                 CheckerContext &C) {
  const StackSpaceRegion *S = cast<StackSpaceRegion>(R->getMemorySpace());
  return S->getStackFrame() != C.getStackFrame();
}

bool StackAddrEscapeChecker::isSemaphoreCaptured(const BlockDecl &B) const {
  if (!dispatch_semaphore_tII)
    dispatch_semaphore_tII = &B.getASTContext().Idents.get("dispatch_semaphore_t");
  for (const auto &C : B.captures()) {
    const auto *T = C.getVariable()->getType()->getAs<TypedefType>();
    if (T && T->getDecl()->getIdentifier() == dispatch_semaphore_tII)
      return true;
  }
  return false;
}

SmallVector<const MemRegion *, 4>
StackAddrEscapeChecker::getCapturedStackRegions(const BlockDataRegion &B,
                                                CheckerContext &C) {
  SmallVector<const MemRegion *, 4> Regions;
  BlockDataRegion::referenced_vars_iterator I = B.referenced_vars_begin();
  BlockDataRegion::referenced_vars_iterator E = B.referenced_vars_end();
  for (; I != E; ++I) {
    SVal Val = C.getState()->getSVal(I.getCapturedRegion());
    const MemRegion *Region = Val.getAsRegion();
    if (Region && isa<StackSpaceRegion>(Region->getMemorySpace()))
      Regions.push_back(Region);
  }
  return Regions;
}

void StackAddrEscapeChecker::EmitStackError(CheckerContext &C,
                                            const MemRegion *R,
                                            const Expr *RetE) const {
  ExplodedNode *N = C.generateNonFatalErrorNode();
  if (!N)
    return;
  if (!BT_returnstack)
    BT_returnstack = llvm::make_unique<BuiltinBug>(
        this, "Return of address to stack-allocated memory");
  // Generate a report for this bug.
  SmallString<128> buf;
  llvm::raw_svector_ostream os(buf);
  SourceRange range = genName(os, R, C.getASTContext());
  os << " returned to caller";
  auto report = llvm::make_unique<BugReport>(*BT_returnstack, os.str(), N);
  report->addRange(RetE->getSourceRange());
  if (range.isValid())
    report->addRange(range);
  C.emitReport(std::move(report));
}

void StackAddrEscapeChecker::checkAsyncExecutedBlockCaptures(
    const BlockDataRegion &B, CheckerContext &C) const {
  // There is a not-too-uncommon idiom
  // where a block passed to dispatch_async captures a semaphore
  // and then the thread (which called dispatch_async) is blocked on waiting
  // for the completion of the execution of the block
  // via dispatch_semaphore_wait. To avoid false-positives (for now)
  // we ignore all the blocks which have captured
  // a variable of the type "dispatch_semaphore_t".
  if (isSemaphoreCaptured(*B.getDecl()))
    return;
  for (const MemRegion *Region : getCapturedStackRegions(B, C)) {
    // The block passed to dispatch_async may capture another block
    // created on the stack. However, there is no leak in this situaton,
    // no matter if ARC or no ARC is enabled:
    // dispatch_async copies the passed "outer" block (via Block_copy)
    // and if the block has captured another "inner" block,
    // the "inner" block will be copied as well.
    if (isa<BlockDataRegion>(Region))
      continue;
    ExplodedNode *N = C.generateNonFatalErrorNode();
    if (!N)
      continue;
    if (!BT_capturedstackasync)
      BT_capturedstackasync = llvm::make_unique<BuiltinBug>(
          this, "Address of stack-allocated memory is captured");
    SmallString<128> Buf;
    llvm::raw_svector_ostream Out(Buf);
    SourceRange Range = genName(Out, Region, C.getASTContext());
    Out << " is captured by an asynchronously-executed block";
    auto Report =
        llvm::make_unique<BugReport>(*BT_capturedstackasync, Out.str(), N);
    if (Range.isValid())
      Report->addRange(Range);
    C.emitReport(std::move(Report));
  }
}

void StackAddrEscapeChecker::checkReturnedBlockCaptures(
    const BlockDataRegion &B, CheckerContext &C) const {
  for (const MemRegion *Region : getCapturedStackRegions(B, C)) {
    if (isArcManagedBlock(Region, C) || isNotInCurrentFrame(Region, C))
      continue;
    ExplodedNode *N = C.generateNonFatalErrorNode();
    if (!N)
      continue;
    if (!BT_capturedstackret)
      BT_capturedstackret = llvm::make_unique<BuiltinBug>(
          this, "Address of stack-allocated memory is captured");
    SmallString<128> Buf;
    llvm::raw_svector_ostream Out(Buf);
    SourceRange Range = genName(Out, Region, C.getASTContext());
    Out << " is captured by a returned block";
    auto Report =
        llvm::make_unique<BugReport>(*BT_capturedstackret, Out.str(), N);
    if (Range.isValid())
      Report->addRange(Range);
    C.emitReport(std::move(Report));
  }
}

void StackAddrEscapeChecker::checkPreCall(const CallEvent &Call,
                                          CheckerContext &C) const {
  if (!ChecksEnabled[CK_StackAddrAsyncEscapeChecker])
    return;
  if (!Call.isGlobalCFunction("dispatch_after") &&
      !Call.isGlobalCFunction("dispatch_async"))
    return;
  for (unsigned Idx = 0, NumArgs = Call.getNumArgs(); Idx < NumArgs; ++Idx) {
    if (const BlockDataRegion *B = dyn_cast_or_null<BlockDataRegion>(
            Call.getArgSVal(Idx).getAsRegion()))
      checkAsyncExecutedBlockCaptures(*B, C);
  }
}

void StackAddrEscapeChecker::checkPreStmt(const ReturnStmt *RS,
                                          CheckerContext &C) const {
  if (!ChecksEnabled[CK_StackAddrEscapeChecker])
    return;

  const Expr *RetE = RS->getRetValue();
  if (!RetE)
    return;
  RetE = RetE->IgnoreParens();

  SVal V = C.getSVal(RetE);
  const MemRegion *R = V.getAsRegion();
  if (!R)
    return;

  if (const BlockDataRegion *B = dyn_cast<BlockDataRegion>(R))
    checkReturnedBlockCaptures(*B, C);

  if (!isa<StackSpaceRegion>(R->getMemorySpace()) ||
      isNotInCurrentFrame(R, C) || isArcManagedBlock(R, C))
    return;

  // Returning a record by value is fine. (In this case, the returned
  // expression will be a copy-constructor, possibly wrapped in an
  // ExprWithCleanups node.)
  if (const ExprWithCleanups *Cleanup = dyn_cast<ExprWithCleanups>(RetE))
    RetE = Cleanup->getSubExpr();
  if (isa<CXXConstructExpr>(RetE) && RetE->getType()->isRecordType())
    return;

  // The CK_CopyAndAutoreleaseBlockObject cast causes the block to be copied
  // so the stack address is not escaping here.
  if (auto *ICE = dyn_cast<ImplicitCastExpr>(RetE)) {
    if (isa<BlockDataRegion>(R) &&
        ICE->getCastKind() == CK_CopyAndAutoreleaseBlockObject) {
      return;
    }
  }

  EmitStackError(C, R, RetE);
}

void StackAddrEscapeChecker::checkEndFunction(const ReturnStmt *RS,
                                              CheckerContext &Ctx) const {
  if (!ChecksEnabled[CK_StackAddrEscapeChecker])
    return;

  ProgramStateRef State = Ctx.getState();

  // Iterate over all bindings to global variables and see if it contains
  // a memory region in the stack space.
  class CallBack : public StoreManager::BindingsHandler {
  private:
    CheckerContext &Ctx;
    const StackFrameContext *CurSFC;

  public:
    SmallVector<std::pair<const MemRegion *, const MemRegion *>, 10> V;

    CallBack(CheckerContext &CC) : Ctx(CC), CurSFC(CC.getStackFrame()) {}

    bool HandleBinding(StoreManager &SMgr, Store S, const MemRegion *Region,
                       SVal Val) override {

      if (!isa<GlobalsSpaceRegion>(Region->getMemorySpace()))
        return true;
      const MemRegion *VR = Val.getAsRegion();
      if (VR && isa<StackSpaceRegion>(VR->getMemorySpace()) &&
          !isArcManagedBlock(VR, Ctx) && !isNotInCurrentFrame(VR, Ctx))
        V.emplace_back(Region, VR);
      return true;
    }
  };

  CallBack Cb(Ctx);
  State->getStateManager().getStoreManager().iterBindings(State->getStore(),
                                                          Cb);

  if (Cb.V.empty())
    return;

  // Generate an error node.
  ExplodedNode *N = Ctx.generateNonFatalErrorNode(State);
  if (!N)
    return;

  if (!BT_stackleak)
    BT_stackleak = llvm::make_unique<BuiltinBug>(
        this, "Stack address stored into global variable",
        "Stack address was saved into a global variable. "
        "This is dangerous because the address will become "
        "invalid after returning from the function");

  for (const auto &P : Cb.V) {
    // Generate a report for this bug.
    SmallString<128> Buf;
    llvm::raw_svector_ostream Out(Buf);
    SourceRange Range = genName(Out, P.second, Ctx.getASTContext());
    Out << " is still referred to by the ";
    if (isa<StaticGlobalSpaceRegion>(P.first->getMemorySpace()))
      Out << "static";
    else
      Out << "global";
    Out << " variable '";
    const VarRegion *VR = cast<VarRegion>(P.first->getBaseRegion());
    Out << *VR->getDecl()
        << "' upon returning to the caller.  This will be a dangling reference";
    auto Report = llvm::make_unique<BugReport>(*BT_stackleak, Out.str(), N);
    if (Range.isValid())
      Report->addRange(Range);

    Ctx.emitReport(std::move(Report));
  }
}

#define REGISTER_CHECKER(name) \
  void ento::register##name(CheckerManager &Mgr) { \
    StackAddrEscapeChecker *Chk = \
        Mgr.registerChecker<StackAddrEscapeChecker>(); \
    Chk->ChecksEnabled[StackAddrEscapeChecker::CK_##name] = true; \
  }

REGISTER_CHECKER(StackAddrEscapeChecker)
REGISTER_CHECKER(StackAddrAsyncEscapeChecker)
