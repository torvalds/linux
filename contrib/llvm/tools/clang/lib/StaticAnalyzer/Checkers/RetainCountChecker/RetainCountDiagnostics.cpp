// RetainCountDiagnostics.cpp - Checks for leaks and other issues -*- C++ -*--//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
//  This file defines diagnostics for RetainCountChecker, which implements
//  a reference count checker for Core Foundation and Cocoa on (Mac OS X).
//
//===----------------------------------------------------------------------===//

#include "RetainCountDiagnostics.h"
#include "RetainCountChecker.h"

using namespace clang;
using namespace ento;
using namespace retaincountchecker;

static bool isNumericLiteralExpression(const Expr *E) {
  // FIXME: This set of cases was copied from SemaExprObjC.
  return isa<IntegerLiteral>(E) ||
         isa<CharacterLiteral>(E) ||
         isa<FloatingLiteral>(E) ||
         isa<ObjCBoolLiteralExpr>(E) ||
         isa<CXXBoolLiteralExpr>(E);
}

/// If type represents a pointer to CXXRecordDecl,
/// and is not a typedef, return the decl name.
/// Otherwise, return the serialization of type.
static std::string getPrettyTypeName(QualType QT) {
  QualType PT = QT->getPointeeType();
  if (!PT.isNull() && !QT->getAs<TypedefType>())
    if (const auto *RD = PT->getAsCXXRecordDecl())
      return RD->getName();
  return QT.getAsString();
}

/// Write information about the type state change to {@code os},
/// return whether the note should be generated.
static bool shouldGenerateNote(llvm::raw_string_ostream &os,
                               const RefVal *PrevT, const RefVal &CurrV,
                               bool DeallocSent) {
  // Get the previous type state.
  RefVal PrevV = *PrevT;

  // Specially handle -dealloc.
  if (DeallocSent) {
    // Determine if the object's reference count was pushed to zero.
    assert(!PrevV.hasSameState(CurrV) && "The state should have changed.");
    // We may not have transitioned to 'release' if we hit an error.
    // This case is handled elsewhere.
    if (CurrV.getKind() == RefVal::Released) {
      assert(CurrV.getCombinedCounts() == 0);
      os << "Object released by directly sending the '-dealloc' message";
      return true;
    }
  }

  // Determine if the typestate has changed.
  if (!PrevV.hasSameState(CurrV))
    switch (CurrV.getKind()) {
    case RefVal::Owned:
    case RefVal::NotOwned:
      if (PrevV.getCount() == CurrV.getCount()) {
        // Did an autorelease message get sent?
        if (PrevV.getAutoreleaseCount() == CurrV.getAutoreleaseCount())
          return false;

        assert(PrevV.getAutoreleaseCount() < CurrV.getAutoreleaseCount());
        os << "Object autoreleased";
        return true;
      }

      if (PrevV.getCount() > CurrV.getCount())
        os << "Reference count decremented.";
      else
        os << "Reference count incremented.";

      if (unsigned Count = CurrV.getCount())
        os << " The object now has a +" << Count << " retain count.";

      return true;

    case RefVal::Released:
      if (CurrV.getIvarAccessHistory() ==
              RefVal::IvarAccessHistory::ReleasedAfterDirectAccess &&
          CurrV.getIvarAccessHistory() != PrevV.getIvarAccessHistory()) {
        os << "Strong instance variable relinquished. ";
      }
      os << "Object released.";
      return true;

    case RefVal::ReturnedOwned:
      // Autoreleases can be applied after marking a node ReturnedOwned.
      if (CurrV.getAutoreleaseCount())
        return false;

      os << "Object returned to caller as an owning reference (single "
            "retain count transferred to caller)";
      return true;

    case RefVal::ReturnedNotOwned:
      os << "Object returned to caller with a +0 retain count";
      return true;

    default:
      return false;
    }
  return true;
}

/// Finds argument index of the out paramter in the call {@code S}
/// corresponding to the symbol {@code Sym}.
/// If none found, returns None.
static Optional<unsigned> findArgIdxOfSymbol(ProgramStateRef CurrSt,
                                             const LocationContext *LCtx,
                                             SymbolRef &Sym,
                                             Optional<CallEventRef<>> CE) {
  if (!CE)
    return None;

  for (unsigned Idx = 0; Idx < (*CE)->getNumArgs(); Idx++)
    if (const MemRegion *MR = (*CE)->getArgSVal(Idx).getAsRegion())
      if (const auto *TR = dyn_cast<TypedValueRegion>(MR))
        if (CurrSt->getSVal(MR, TR->getValueType()).getAsSymExpr() == Sym)
          return Idx;

  return None;
}

static void generateDiagnosticsForCallLike(ProgramStateRef CurrSt,
                                           const LocationContext *LCtx,
                                           const RefVal &CurrV, SymbolRef &Sym,
                                           const Stmt *S,
                                           llvm::raw_string_ostream &os) {
  CallEventManager &Mgr = CurrSt->getStateManager().getCallEventManager();
  if (const CallExpr *CE = dyn_cast<CallExpr>(S)) {
    // Get the name of the callee (if it is available)
    // from the tracked SVal.
    SVal X = CurrSt->getSValAsScalarOrLoc(CE->getCallee(), LCtx);
    const FunctionDecl *FD = X.getAsFunctionDecl();

    // If failed, try to get it from AST.
    if (!FD)
      FD = dyn_cast<FunctionDecl>(CE->getCalleeDecl());

    if (const auto *MD = dyn_cast<CXXMethodDecl>(CE->getCalleeDecl())) {
      os << "Call to method '" << MD->getQualifiedNameAsString() << '\'';
    } else if (FD) {
      os << "Call to function '" << FD->getQualifiedNameAsString() << '\'';
    } else {
      os << "function call";
    }
  } else if (isa<CXXNewExpr>(S)) {
    os << "Operator 'new'";
  } else {
    assert(isa<ObjCMessageExpr>(S));
    CallEventRef<ObjCMethodCall> Call =
        Mgr.getObjCMethodCall(cast<ObjCMessageExpr>(S), CurrSt, LCtx);

    switch (Call->getMessageKind()) {
    case OCM_Message:
      os << "Method";
      break;
    case OCM_PropertyAccess:
      os << "Property";
      break;
    case OCM_Subscript:
      os << "Subscript";
      break;
    }
  }

  Optional<CallEventRef<>> CE = Mgr.getCall(S, CurrSt, LCtx);
  auto Idx = findArgIdxOfSymbol(CurrSt, LCtx, Sym, CE);

  // If index is not found, we assume that the symbol was returned.
  if (!Idx) {
    os << " returns ";
  } else {
    os << " writes ";
  }

  if (CurrV.getObjKind() == ObjKind::CF) {
    os << "a Core Foundation object of type '"
       << Sym->getType().getAsString() << "' with a ";
  } else if (CurrV.getObjKind() == ObjKind::OS) {
    os << "an OSObject of type '" << getPrettyTypeName(Sym->getType())
       << "' with a ";
  } else if (CurrV.getObjKind() == ObjKind::Generalized) {
    os << "an object of type '" << Sym->getType().getAsString()
       << "' with a ";
  } else {
    assert(CurrV.getObjKind() == ObjKind::ObjC);
    QualType T = Sym->getType();
    if (!isa<ObjCObjectPointerType>(T)) {
      os << "an Objective-C object with a ";
    } else {
      const ObjCObjectPointerType *PT = cast<ObjCObjectPointerType>(T);
      os << "an instance of " << PT->getPointeeType().getAsString()
         << " with a ";
    }
  }

  if (CurrV.isOwned()) {
    os << "+1 retain count";
  } else {
    assert(CurrV.isNotOwned());
    os << "+0 retain count";
  }

  if (Idx) {
    os << " into an out parameter '";
    const ParmVarDecl *PVD = (*CE)->parameters()[*Idx];
    PVD->getNameForDiagnostic(os, PVD->getASTContext().getPrintingPolicy(),
                              /*Qualified=*/false);
    os << "'";

    QualType RT = (*CE)->getResultType();
    if (!RT.isNull() && !RT->isVoidType()) {
      SVal RV = (*CE)->getReturnValue();
      if (CurrSt->isNull(RV).isConstrainedTrue()) {
        os << " (assuming the call returns zero)";
      } else if (CurrSt->isNonNull(RV).isConstrainedTrue()) {
        os << " (assuming the call returns non-zero)";
      }

    }
  }
}

namespace clang {
namespace ento {
namespace retaincountchecker {

class RefCountReportVisitor : public BugReporterVisitor {
protected:
  SymbolRef Sym;

public:
  RefCountReportVisitor(SymbolRef sym) : Sym(sym) {}

  void Profile(llvm::FoldingSetNodeID &ID) const override {
    static int x = 0;
    ID.AddPointer(&x);
    ID.AddPointer(Sym);
  }

  std::shared_ptr<PathDiagnosticPiece> VisitNode(const ExplodedNode *N,
                                                 BugReporterContext &BRC,
                                                 BugReport &BR) override;

  std::shared_ptr<PathDiagnosticPiece> getEndPath(BugReporterContext &BRC,
                                                  const ExplodedNode *N,
                                                  BugReport &BR) override;
};

class RefLeakReportVisitor : public RefCountReportVisitor {
public:
  RefLeakReportVisitor(SymbolRef sym) : RefCountReportVisitor(sym) {}

  std::shared_ptr<PathDiagnosticPiece> getEndPath(BugReporterContext &BRC,
                                                  const ExplodedNode *N,
                                                  BugReport &BR) override;
};

} // end namespace retaincountchecker
} // end namespace ento
} // end namespace clang


/// Find the first node with the parent stack frame.
static const ExplodedNode *getCalleeNode(const ExplodedNode *Pred) {
  const StackFrameContext *SC = Pred->getStackFrame();
  if (SC->inTopFrame())
    return nullptr;
  const StackFrameContext *PC = SC->getParent()->getStackFrame();
  if (!PC)
    return nullptr;

  const ExplodedNode *N = Pred;
  while (N && N->getStackFrame() != PC) {
    N = N->getFirstPred();
  }
  return N;
}


/// Insert a diagnostic piece at function exit
/// if a function parameter is annotated as "os_consumed",
/// but it does not actually consume the reference.
static std::shared_ptr<PathDiagnosticEventPiece>
annotateConsumedSummaryMismatch(const ExplodedNode *N,
                                CallExitBegin &CallExitLoc,
                                const SourceManager &SM,
                                CallEventManager &CEMgr) {

  const ExplodedNode *CN = getCalleeNode(N);
  if (!CN)
    return nullptr;

  CallEventRef<> Call = CEMgr.getCaller(N->getStackFrame(), N->getState());

  std::string sbuf;
  llvm::raw_string_ostream os(sbuf);
  ArrayRef<const ParmVarDecl *> Parameters = Call->parameters();
  for (unsigned I=0; I < Call->getNumArgs() && I < Parameters.size(); ++I) {
    const ParmVarDecl *PVD = Parameters[I];

    if (!PVD->hasAttr<OSConsumedAttr>())
      continue;

    if (SymbolRef SR = Call->getArgSVal(I).getAsLocSymbol()) {
      const RefVal *CountBeforeCall = getRefBinding(CN->getState(), SR);
      const RefVal *CountAtExit = getRefBinding(N->getState(), SR);

      if (!CountBeforeCall || !CountAtExit)
        continue;

      unsigned CountBefore = CountBeforeCall->getCount();
      unsigned CountAfter = CountAtExit->getCount();

      bool AsExpected = CountBefore > 0 && CountAfter == CountBefore - 1;
      if (!AsExpected) {
        os << "Parameter '";
        PVD->getNameForDiagnostic(os, PVD->getASTContext().getPrintingPolicy(),
                                  /*Qualified=*/false);
        os << "' is marked as consuming, but the function did not consume "
           << "the reference\n";
      }
    }
  }

  if (os.str().empty())
    return nullptr;

  // FIXME: remove the code duplication with NoStoreFuncVisitor.
  PathDiagnosticLocation L;
  if (const ReturnStmt *RS = CallExitLoc.getReturnStmt()) {
    L = PathDiagnosticLocation::createBegin(RS, SM, N->getLocationContext());
  } else {
    L = PathDiagnosticLocation(
        Call->getRuntimeDefinition().getDecl()->getSourceRange().getEnd(), SM);
  }

  return std::make_shared<PathDiagnosticEventPiece>(L, os.str());
}

std::shared_ptr<PathDiagnosticPiece>
RefCountReportVisitor::VisitNode(const ExplodedNode *N,
                              BugReporterContext &BRC, BugReport &BR) {

  const SourceManager &SM = BRC.getSourceManager();
  CallEventManager &CEMgr = BRC.getStateManager().getCallEventManager();
  if (auto CE = N->getLocationAs<CallExitBegin>())
    if (auto PD = annotateConsumedSummaryMismatch(N, *CE, SM, CEMgr))
      return PD;

  // FIXME: We will eventually need to handle non-statement-based events
  // (__attribute__((cleanup))).
  if (!N->getLocation().getAs<StmtPoint>())
    return nullptr;

  // Check if the type state has changed.
  const ExplodedNode *PrevNode = N->getFirstPred();
  ProgramStateRef PrevSt = PrevNode->getState();
  ProgramStateRef CurrSt = N->getState();
  const LocationContext *LCtx = N->getLocationContext();

  const RefVal* CurrT = getRefBinding(CurrSt, Sym);
  if (!CurrT) return nullptr;

  const RefVal &CurrV = *CurrT;
  const RefVal *PrevT = getRefBinding(PrevSt, Sym);

  // Create a string buffer to constain all the useful things we want
  // to tell the user.
  std::string sbuf;
  llvm::raw_string_ostream os(sbuf);

  // This is the allocation site since the previous node had no bindings
  // for this symbol.
  if (!PrevT) {
    const Stmt *S = N->getLocation().castAs<StmtPoint>().getStmt();

    if (isa<ObjCIvarRefExpr>(S) &&
        isSynthesizedAccessor(LCtx->getStackFrame())) {
      S = LCtx->getStackFrame()->getCallSite();
    }

    if (isa<ObjCArrayLiteral>(S)) {
      os << "NSArray literal is an object with a +0 retain count";
    } else if (isa<ObjCDictionaryLiteral>(S)) {
      os << "NSDictionary literal is an object with a +0 retain count";
    } else if (const ObjCBoxedExpr *BL = dyn_cast<ObjCBoxedExpr>(S)) {
      if (isNumericLiteralExpression(BL->getSubExpr()))
        os << "NSNumber literal is an object with a +0 retain count";
      else {
        const ObjCInterfaceDecl *BoxClass = nullptr;
        if (const ObjCMethodDecl *Method = BL->getBoxingMethod())
          BoxClass = Method->getClassInterface();

        // We should always be able to find the boxing class interface,
        // but consider this future-proofing.
        if (BoxClass) {
          os << *BoxClass << " b";
        } else {
          os << "B";
        }

        os << "oxed expression produces an object with a +0 retain count";
      }
    } else if (isa<ObjCIvarRefExpr>(S)) {
      os << "Object loaded from instance variable";
    } else {
      generateDiagnosticsForCallLike(CurrSt, LCtx, CurrV, Sym, S, os);
    }

    PathDiagnosticLocation Pos(S, SM, N->getLocationContext());
    return std::make_shared<PathDiagnosticEventPiece>(Pos, os.str());
  }

  // Gather up the effects that were performed on the object at this
  // program point
  bool DeallocSent = false;

  if (N->getLocation().getTag() &&
      N->getLocation().getTag()->getTagDescription().contains(
          RetainCountChecker::DeallocTagDescription)) {
    // We only have summaries attached to nodes after evaluating CallExpr and
    // ObjCMessageExprs.
    const Stmt *S = N->getLocation().castAs<StmtPoint>().getStmt();

    if (const CallExpr *CE = dyn_cast<CallExpr>(S)) {
      // Iterate through the parameter expressions and see if the symbol
      // was ever passed as an argument.
      unsigned i = 0;

      for (auto AI=CE->arg_begin(), AE=CE->arg_end(); AI!=AE; ++AI, ++i) {

        // Retrieve the value of the argument.  Is it the symbol
        // we are interested in?
        if (CurrSt->getSValAsScalarOrLoc(*AI, LCtx).getAsLocSymbol() != Sym)
          continue;

        // We have an argument.  Get the effect!
        DeallocSent = true;
      }
    } else if (const ObjCMessageExpr *ME = dyn_cast<ObjCMessageExpr>(S)) {
      if (const Expr *receiver = ME->getInstanceReceiver()) {
        if (CurrSt->getSValAsScalarOrLoc(receiver, LCtx)
              .getAsLocSymbol() == Sym) {
          // The symbol we are tracking is the receiver.
          DeallocSent = true;
        }
      }
    }
  }

  if (!shouldGenerateNote(os, PrevT, CurrV, DeallocSent))
    return nullptr;

  if (os.str().empty())
    return nullptr; // We have nothing to say!

  const Stmt *S = N->getLocation().castAs<StmtPoint>().getStmt();
  PathDiagnosticLocation Pos(S, BRC.getSourceManager(),
                                N->getLocationContext());
  auto P = std::make_shared<PathDiagnosticEventPiece>(Pos, os.str());

  // Add the range by scanning the children of the statement for any bindings
  // to Sym.
  for (const Stmt *Child : S->children())
    if (const Expr *Exp = dyn_cast_or_null<Expr>(Child))
      if (CurrSt->getSValAsScalarOrLoc(Exp, LCtx).getAsLocSymbol() == Sym) {
        P->addRange(Exp->getSourceRange());
        break;
      }

  return std::move(P);
}

static Optional<std::string> describeRegion(const MemRegion *MR) {
  if (const auto *VR = dyn_cast_or_null<VarRegion>(MR))
    return std::string(VR->getDecl()->getName());
  // Once we support more storage locations for bindings,
  // this would need to be improved.
  return None;
}

namespace {
// Find the first node in the current function context that referred to the
// tracked symbol and the memory location that value was stored to. Note, the
// value is only reported if the allocation occurred in the same function as
// the leak. The function can also return a location context, which should be
// treated as interesting.
struct AllocationInfo {
  const ExplodedNode* N;
  const MemRegion *R;
  const LocationContext *InterestingMethodContext;
  AllocationInfo(const ExplodedNode *InN,
                 const MemRegion *InR,
                 const LocationContext *InInterestingMethodContext) :
    N(InN), R(InR), InterestingMethodContext(InInterestingMethodContext) {}
};
} // end anonymous namespace

static AllocationInfo GetAllocationSite(ProgramStateManager &StateMgr,
                                        const ExplodedNode *N, SymbolRef Sym) {
  const ExplodedNode *AllocationNode = N;
  const ExplodedNode *AllocationNodeInCurrentOrParentContext = N;
  const MemRegion *FirstBinding = nullptr;
  const LocationContext *LeakContext = N->getLocationContext();

  // The location context of the init method called on the leaked object, if
  // available.
  const LocationContext *InitMethodContext = nullptr;

  while (N) {
    ProgramStateRef St = N->getState();
    const LocationContext *NContext = N->getLocationContext();

    if (!getRefBinding(St, Sym))
      break;

    StoreManager::FindUniqueBinding FB(Sym);
    StateMgr.iterBindings(St, FB);

    if (FB) {
      const MemRegion *R = FB.getRegion();
      // Do not show local variables belonging to a function other than
      // where the error is reported.
      if (auto MR = dyn_cast<StackSpaceRegion>(R->getMemorySpace()))
        if (MR->getStackFrame() == LeakContext->getStackFrame())
          FirstBinding = R;
    }

    // AllocationNode is the last node in which the symbol was tracked.
    AllocationNode = N;

    // AllocationNodeInCurrentContext, is the last node in the current or
    // parent context in which the symbol was tracked.
    //
    // Note that the allocation site might be in the parent context. For example,
    // the case where an allocation happens in a block that captures a reference
    // to it and that reference is overwritten/dropped by another call to
    // the block.
    if (NContext == LeakContext || NContext->isParentOf(LeakContext))
      AllocationNodeInCurrentOrParentContext = N;

    // Find the last init that was called on the given symbol and store the
    // init method's location context.
    if (!InitMethodContext)
      if (auto CEP = N->getLocation().getAs<CallEnter>()) {
        const Stmt *CE = CEP->getCallExpr();
        if (const auto *ME = dyn_cast_or_null<ObjCMessageExpr>(CE)) {
          const Stmt *RecExpr = ME->getInstanceReceiver();
          if (RecExpr) {
            SVal RecV = St->getSVal(RecExpr, NContext);
            if (ME->getMethodFamily() == OMF_init && RecV.getAsSymbol() == Sym)
              InitMethodContext = CEP->getCalleeContext();
          }
        }
      }

    N = N->getFirstPred();
  }

  // If we are reporting a leak of the object that was allocated with alloc,
  // mark its init method as interesting.
  const LocationContext *InterestingMethodContext = nullptr;
  if (InitMethodContext) {
    const ProgramPoint AllocPP = AllocationNode->getLocation();
    if (Optional<StmtPoint> SP = AllocPP.getAs<StmtPoint>())
      if (const ObjCMessageExpr *ME = SP->getStmtAs<ObjCMessageExpr>())
        if (ME->getMethodFamily() == OMF_alloc)
          InterestingMethodContext = InitMethodContext;
  }

  // If allocation happened in a function different from the leak node context,
  // do not report the binding.
  assert(N && "Could not find allocation node");

  if (AllocationNodeInCurrentOrParentContext &&
      AllocationNodeInCurrentOrParentContext->getLocationContext() !=
          LeakContext)
    FirstBinding = nullptr;

  return AllocationInfo(AllocationNodeInCurrentOrParentContext,
                        FirstBinding,
                        InterestingMethodContext);
}

std::shared_ptr<PathDiagnosticPiece>
RefCountReportVisitor::getEndPath(BugReporterContext &BRC,
                               const ExplodedNode *EndN, BugReport &BR) {
  BR.markInteresting(Sym);
  return BugReporterVisitor::getDefaultEndPath(BRC, EndN, BR);
}

std::shared_ptr<PathDiagnosticPiece>
RefLeakReportVisitor::getEndPath(BugReporterContext &BRC,
                                   const ExplodedNode *EndN, BugReport &BR) {

  // Tell the BugReporterContext to report cases when the tracked symbol is
  // assigned to different variables, etc.
  BR.markInteresting(Sym);

  // We are reporting a leak.  Walk up the graph to get to the first node where
  // the symbol appeared, and also get the first VarDecl that tracked object
  // is stored to.
  AllocationInfo AllocI = GetAllocationSite(BRC.getStateManager(), EndN, Sym);

  const MemRegion* FirstBinding = AllocI.R;
  BR.markInteresting(AllocI.InterestingMethodContext);

  SourceManager& SM = BRC.getSourceManager();

  // Compute an actual location for the leak.  Sometimes a leak doesn't
  // occur at an actual statement (e.g., transition between blocks; end
  // of function) so we need to walk the graph and compute a real location.
  const ExplodedNode *LeakN = EndN;
  PathDiagnosticLocation L = PathDiagnosticLocation::createEndOfPath(LeakN, SM);

  std::string sbuf;
  llvm::raw_string_ostream os(sbuf);

  os << "Object leaked: ";

  Optional<std::string> RegionDescription = describeRegion(FirstBinding);
  if (RegionDescription) {
    os << "object allocated and stored into '" << *RegionDescription << '\'';
  } else {
    os << "allocated object of type '" << getPrettyTypeName(Sym->getType())
       << "'";
  }

  // Get the retain count.
  const RefVal* RV = getRefBinding(EndN->getState(), Sym);
  assert(RV);

  if (RV->getKind() == RefVal::ErrorLeakReturned) {
    // FIXME: Per comments in rdar://6320065, "create" only applies to CF
    // objects.  Only "copy", "alloc", "retain" and "new" transfer ownership
    // to the caller for NS objects.
    const Decl *D = &EndN->getCodeDecl();

    os << (isa<ObjCMethodDecl>(D) ? " is returned from a method "
                                  : " is returned from a function ");

    if (D->hasAttr<CFReturnsNotRetainedAttr>()) {
      os << "that is annotated as CF_RETURNS_NOT_RETAINED";
    } else if (D->hasAttr<NSReturnsNotRetainedAttr>()) {
      os << "that is annotated as NS_RETURNS_NOT_RETAINED";
    } else if (D->hasAttr<OSReturnsNotRetainedAttr>()) {
      os << "that is annotated as OS_RETURNS_NOT_RETAINED";
    } else {
      if (const ObjCMethodDecl *MD = dyn_cast<ObjCMethodDecl>(D)) {
        if (BRC.getASTContext().getLangOpts().ObjCAutoRefCount) {
          os << "managed by Automatic Reference Counting";
        } else {
          os << "whose name ('" << MD->getSelector().getAsString()
             << "') does not start with "
                "'copy', 'mutableCopy', 'alloc' or 'new'."
                "  This violates the naming convention rules"
                " given in the Memory Management Guide for Cocoa";
        }
      } else {
        const FunctionDecl *FD = cast<FunctionDecl>(D);
        os << "whose name ('" << *FD
           << "') does not contain 'Copy' or 'Create'.  This violates the naming"
              " convention rules given in the Memory Management Guide for Core"
              " Foundation";
      }
    }
  } else {
    os << " is not referenced later in this execution path and has a retain "
          "count of +" << RV->getCount();
  }

  return std::make_shared<PathDiagnosticEventPiece>(L, os.str());
}

RefCountReport::RefCountReport(RefCountBug &D, const LangOptions &LOpts,
                               ExplodedNode *n, SymbolRef sym,
                               bool registerVisitor)
    : BugReport(D, D.getDescription(), n), Sym(sym) {
  if (registerVisitor)
    addVisitor(llvm::make_unique<RefCountReportVisitor>(sym));
}

RefCountReport::RefCountReport(RefCountBug &D, const LangOptions &LOpts,
                               ExplodedNode *n, SymbolRef sym,
                               StringRef endText)
    : BugReport(D, D.getDescription(), endText, n) {

  addVisitor(llvm::make_unique<RefCountReportVisitor>(sym));
}

void RefLeakReport::deriveParamLocation(CheckerContext &Ctx, SymbolRef sym) {
  const SourceManager& SMgr = Ctx.getSourceManager();

  if (!sym->getOriginRegion())
    return;

  auto *Region = dyn_cast<DeclRegion>(sym->getOriginRegion());
  if (Region) {
    const Decl *PDecl = Region->getDecl();
    if (PDecl && isa<ParmVarDecl>(PDecl)) {
      PathDiagnosticLocation ParamLocation =
          PathDiagnosticLocation::create(PDecl, SMgr);
      Location = ParamLocation;
      UniqueingLocation = ParamLocation;
      UniqueingDecl = Ctx.getLocationContext()->getDecl();
    }
  }
}

void RefLeakReport::deriveAllocLocation(CheckerContext &Ctx,
                                          SymbolRef sym) {
  // Most bug reports are cached at the location where they occurred.
  // With leaks, we want to unique them by the location where they were
  // allocated, and only report a single path.  To do this, we need to find
  // the allocation site of a piece of tracked memory, which we do via a
  // call to GetAllocationSite.  This will walk the ExplodedGraph backwards.
  // Note that this is *not* the trimmed graph; we are guaranteed, however,
  // that all ancestor nodes that represent the allocation site have the
  // same SourceLocation.
  const ExplodedNode *AllocNode = nullptr;

  const SourceManager& SMgr = Ctx.getSourceManager();

  AllocationInfo AllocI =
      GetAllocationSite(Ctx.getStateManager(), getErrorNode(), sym);

  AllocNode = AllocI.N;
  AllocBinding = AllocI.R;
  markInteresting(AllocI.InterestingMethodContext);

  // Get the SourceLocation for the allocation site.
  // FIXME: This will crash the analyzer if an allocation comes from an
  // implicit call (ex: a destructor call).
  // (Currently there are no such allocations in Cocoa, though.)
  AllocStmt = PathDiagnosticLocation::getStmt(AllocNode);

  if (!AllocStmt) {
    AllocBinding = nullptr;
    return;
  }

  PathDiagnosticLocation AllocLocation =
    PathDiagnosticLocation::createBegin(AllocStmt, SMgr,
                                        AllocNode->getLocationContext());
  Location = AllocLocation;

  // Set uniqieing info, which will be used for unique the bug reports. The
  // leaks should be uniqued on the allocation site.
  UniqueingLocation = AllocLocation;
  UniqueingDecl = AllocNode->getLocationContext()->getDecl();
}

void RefLeakReport::createDescription(CheckerContext &Ctx) {
  assert(Location.isValid() && UniqueingDecl && UniqueingLocation.isValid());
  Description.clear();
  llvm::raw_string_ostream os(Description);
  os << "Potential leak of an object";

  Optional<std::string> RegionDescription = describeRegion(AllocBinding);
  if (RegionDescription) {
    os << " stored into '" << *RegionDescription << '\'';
  } else {

    // If we can't figure out the name, just supply the type information.
    os << " of type '" << getPrettyTypeName(Sym->getType()) << "'";
  }
}

RefLeakReport::RefLeakReport(RefCountBug &D, const LangOptions &LOpts,
                             ExplodedNode *n, SymbolRef sym,
                             CheckerContext &Ctx)
    : RefCountReport(D, LOpts, n, sym, false) {

  deriveAllocLocation(Ctx, sym);
  if (!AllocBinding)
    deriveParamLocation(Ctx, sym);

  createDescription(Ctx);

  addVisitor(llvm::make_unique<RefLeakReportVisitor>(sym));
}
