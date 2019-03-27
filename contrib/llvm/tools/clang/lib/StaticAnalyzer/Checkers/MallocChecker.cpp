//=== MallocChecker.cpp - A malloc/free checker -------------------*- C++ -*--//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file defines malloc/free checker, which checks for potential memory
// leaks, double free, and use-after-free problems.
//
//===----------------------------------------------------------------------===//

#include "clang/StaticAnalyzer/Checkers/BuiltinCheckerRegistration.h"
#include "InterCheckerAPI.h"
#include "clang/AST/Attr.h"
#include "clang/AST/ParentMap.h"
#include "clang/Basic/SourceManager.h"
#include "clang/Basic/TargetInfo.h"
#include "clang/StaticAnalyzer/Core/BugReporter/BugType.h"
#include "clang/StaticAnalyzer/Core/BugReporter/CommonBugCategories.h"
#include "clang/StaticAnalyzer/Core/Checker.h"
#include "clang/StaticAnalyzer/Core/CheckerManager.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/CallEvent.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/CheckerContext.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/ProgramState.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/ProgramStateTrait.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/SymbolManager.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/ADT/StringExtras.h"
#include "AllocationState.h"
#include <climits>
#include <utility>

using namespace clang;
using namespace ento;

namespace {

// Used to check correspondence between allocators and deallocators.
enum AllocationFamily {
  AF_None,
  AF_Malloc,
  AF_CXXNew,
  AF_CXXNewArray,
  AF_IfNameIndex,
  AF_Alloca,
  AF_InnerBuffer
};

class RefState {
  enum Kind { // Reference to allocated memory.
              Allocated,
              // Reference to zero-allocated memory.
              AllocatedOfSizeZero,
              // Reference to released/freed memory.
              Released,
              // The responsibility for freeing resources has transferred from
              // this reference. A relinquished symbol should not be freed.
              Relinquished,
              // We are no longer guaranteed to have observed all manipulations
              // of this pointer/memory. For example, it could have been
              // passed as a parameter to an opaque function.
              Escaped
  };

  const Stmt *S;
  unsigned K : 3; // Kind enum, but stored as a bitfield.
  unsigned Family : 29; // Rest of 32-bit word, currently just an allocation
                        // family.

  RefState(Kind k, const Stmt *s, unsigned family)
    : S(s), K(k), Family(family) {
    assert(family != AF_None);
  }
public:
  bool isAllocated() const { return K == Allocated; }
  bool isAllocatedOfSizeZero() const { return K == AllocatedOfSizeZero; }
  bool isReleased() const { return K == Released; }
  bool isRelinquished() const { return K == Relinquished; }
  bool isEscaped() const { return K == Escaped; }
  AllocationFamily getAllocationFamily() const {
    return (AllocationFamily)Family;
  }
  const Stmt *getStmt() const { return S; }

  bool operator==(const RefState &X) const {
    return K == X.K && S == X.S && Family == X.Family;
  }

  static RefState getAllocated(unsigned family, const Stmt *s) {
    return RefState(Allocated, s, family);
  }
  static RefState getAllocatedOfSizeZero(const RefState *RS) {
    return RefState(AllocatedOfSizeZero, RS->getStmt(),
                    RS->getAllocationFamily());
  }
  static RefState getReleased(unsigned family, const Stmt *s) {
    return RefState(Released, s, family);
  }
  static RefState getRelinquished(unsigned family, const Stmt *s) {
    return RefState(Relinquished, s, family);
  }
  static RefState getEscaped(const RefState *RS) {
    return RefState(Escaped, RS->getStmt(), RS->getAllocationFamily());
  }

  void Profile(llvm::FoldingSetNodeID &ID) const {
    ID.AddInteger(K);
    ID.AddPointer(S);
    ID.AddInteger(Family);
  }

  void dump(raw_ostream &OS) const {
    switch (static_cast<Kind>(K)) {
#define CASE(ID) case ID: OS << #ID; break;
    CASE(Allocated)
    CASE(AllocatedOfSizeZero)
    CASE(Released)
    CASE(Relinquished)
    CASE(Escaped)
    }
  }

  LLVM_DUMP_METHOD void dump() const { dump(llvm::errs()); }
};

enum ReallocPairKind {
  RPToBeFreedAfterFailure,
  // The symbol has been freed when reallocation failed.
  RPIsFreeOnFailure,
  // The symbol does not need to be freed after reallocation fails.
  RPDoNotTrackAfterFailure
};

/// \class ReallocPair
/// Stores information about the symbol being reallocated by a call to
/// 'realloc' to allow modeling failed reallocation later in the path.
struct ReallocPair {
  // The symbol which realloc reallocated.
  SymbolRef ReallocatedSym;
  ReallocPairKind Kind;

  ReallocPair(SymbolRef S, ReallocPairKind K) :
    ReallocatedSym(S), Kind(K) {}
  void Profile(llvm::FoldingSetNodeID &ID) const {
    ID.AddInteger(Kind);
    ID.AddPointer(ReallocatedSym);
  }
  bool operator==(const ReallocPair &X) const {
    return ReallocatedSym == X.ReallocatedSym &&
           Kind == X.Kind;
  }
};

typedef std::pair<const ExplodedNode*, const MemRegion*> LeakInfo;

class MallocChecker : public Checker<check::DeadSymbols,
                                     check::PointerEscape,
                                     check::ConstPointerEscape,
                                     check::PreStmt<ReturnStmt>,
                                     check::EndFunction,
                                     check::PreCall,
                                     check::PostStmt<CallExpr>,
                                     check::PostStmt<CXXNewExpr>,
                                     check::NewAllocator,
                                     check::PreStmt<CXXDeleteExpr>,
                                     check::PostStmt<BlockExpr>,
                                     check::PostObjCMessage,
                                     check::Location,
                                     eval::Assume>
{
public:
  MallocChecker()
      : II_alloca(nullptr), II_win_alloca(nullptr), II_malloc(nullptr),
        II_free(nullptr), II_realloc(nullptr), II_calloc(nullptr),
        II_valloc(nullptr), II_reallocf(nullptr), II_strndup(nullptr),
        II_strdup(nullptr), II_win_strdup(nullptr), II_kmalloc(nullptr),
        II_if_nameindex(nullptr), II_if_freenameindex(nullptr),
        II_wcsdup(nullptr), II_win_wcsdup(nullptr), II_g_malloc(nullptr),
        II_g_malloc0(nullptr), II_g_realloc(nullptr), II_g_try_malloc(nullptr),
        II_g_try_malloc0(nullptr), II_g_try_realloc(nullptr),
        II_g_free(nullptr), II_g_memdup(nullptr), II_g_malloc_n(nullptr),
        II_g_malloc0_n(nullptr), II_g_realloc_n(nullptr),
        II_g_try_malloc_n(nullptr), II_g_try_malloc0_n(nullptr),
        II_g_try_realloc_n(nullptr) {}

  /// In pessimistic mode, the checker assumes that it does not know which
  /// functions might free the memory.
  enum CheckKind {
    CK_MallocChecker,
    CK_NewDeleteChecker,
    CK_NewDeleteLeaksChecker,
    CK_MismatchedDeallocatorChecker,
    CK_InnerPointerChecker,
    CK_NumCheckKinds
  };

  enum class MemoryOperationKind {
    MOK_Allocate,
    MOK_Free,
    MOK_Any
  };

  DefaultBool IsOptimistic;

  DefaultBool ChecksEnabled[CK_NumCheckKinds];
  CheckName CheckNames[CK_NumCheckKinds];

  void checkPreCall(const CallEvent &Call, CheckerContext &C) const;
  void checkPostStmt(const CallExpr *CE, CheckerContext &C) const;
  void checkPostStmt(const CXXNewExpr *NE, CheckerContext &C) const;
  void checkNewAllocator(const CXXNewExpr *NE, SVal Target,
                         CheckerContext &C) const;
  void checkPreStmt(const CXXDeleteExpr *DE, CheckerContext &C) const;
  void checkPostObjCMessage(const ObjCMethodCall &Call, CheckerContext &C) const;
  void checkPostStmt(const BlockExpr *BE, CheckerContext &C) const;
  void checkDeadSymbols(SymbolReaper &SymReaper, CheckerContext &C) const;
  void checkPreStmt(const ReturnStmt *S, CheckerContext &C) const;
  void checkEndFunction(const ReturnStmt *S, CheckerContext &C) const;
  ProgramStateRef evalAssume(ProgramStateRef state, SVal Cond,
                            bool Assumption) const;
  void checkLocation(SVal l, bool isLoad, const Stmt *S,
                     CheckerContext &C) const;

  ProgramStateRef checkPointerEscape(ProgramStateRef State,
                                    const InvalidatedSymbols &Escaped,
                                    const CallEvent *Call,
                                    PointerEscapeKind Kind) const;
  ProgramStateRef checkConstPointerEscape(ProgramStateRef State,
                                          const InvalidatedSymbols &Escaped,
                                          const CallEvent *Call,
                                          PointerEscapeKind Kind) const;

  void printState(raw_ostream &Out, ProgramStateRef State,
                  const char *NL, const char *Sep) const override;

private:
  mutable std::unique_ptr<BugType> BT_DoubleFree[CK_NumCheckKinds];
  mutable std::unique_ptr<BugType> BT_DoubleDelete;
  mutable std::unique_ptr<BugType> BT_Leak[CK_NumCheckKinds];
  mutable std::unique_ptr<BugType> BT_UseFree[CK_NumCheckKinds];
  mutable std::unique_ptr<BugType> BT_BadFree[CK_NumCheckKinds];
  mutable std::unique_ptr<BugType> BT_FreeAlloca[CK_NumCheckKinds];
  mutable std::unique_ptr<BugType> BT_MismatchedDealloc;
  mutable std::unique_ptr<BugType> BT_OffsetFree[CK_NumCheckKinds];
  mutable std::unique_ptr<BugType> BT_UseZerroAllocated[CK_NumCheckKinds];
  mutable IdentifierInfo *II_alloca, *II_win_alloca, *II_malloc, *II_free,
                         *II_realloc, *II_calloc, *II_valloc, *II_reallocf,
                         *II_strndup, *II_strdup, *II_win_strdup, *II_kmalloc,
                         *II_if_nameindex, *II_if_freenameindex, *II_wcsdup,
                         *II_win_wcsdup, *II_g_malloc, *II_g_malloc0,
                         *II_g_realloc, *II_g_try_malloc, *II_g_try_malloc0,
                         *II_g_try_realloc, *II_g_free, *II_g_memdup,
                         *II_g_malloc_n, *II_g_malloc0_n, *II_g_realloc_n,
                         *II_g_try_malloc_n, *II_g_try_malloc0_n,
                         *II_g_try_realloc_n;
  mutable Optional<uint64_t> KernelZeroFlagVal;

  void initIdentifierInfo(ASTContext &C) const;

  /// Determine family of a deallocation expression.
  AllocationFamily getAllocationFamily(CheckerContext &C, const Stmt *S) const;

  /// Print names of allocators and deallocators.
  ///
  /// \returns true on success.
  bool printAllocDeallocName(raw_ostream &os, CheckerContext &C,
                             const Expr *E) const;

  /// Print expected name of an allocator based on the deallocator's
  /// family derived from the DeallocExpr.
  void printExpectedAllocName(raw_ostream &os, CheckerContext &C,
                              const Expr *DeallocExpr) const;
  /// Print expected name of a deallocator based on the allocator's
  /// family.
  void printExpectedDeallocName(raw_ostream &os, AllocationFamily Family) const;

  ///@{
  /// Check if this is one of the functions which can allocate/reallocate memory
  /// pointed to by one of its arguments.
  bool isMemFunction(const FunctionDecl *FD, ASTContext &C) const;
  bool isCMemFunction(const FunctionDecl *FD,
                      ASTContext &C,
                      AllocationFamily Family,
                      MemoryOperationKind MemKind) const;
  bool isStandardNewDelete(const FunctionDecl *FD, ASTContext &C) const;
  ///@}

  /// Process C++ operator new()'s allocation, which is the part of C++
  /// new-expression that goes before the constructor.
  void processNewAllocation(const CXXNewExpr *NE, CheckerContext &C,
                            SVal Target) const;

  /// Perform a zero-allocation check.
  /// The optional \p RetVal parameter specifies the newly allocated pointer
  /// value; if unspecified, the value of expression \p E is used.
  ProgramStateRef ProcessZeroAllocation(CheckerContext &C, const Expr *E,
                                        const unsigned AllocationSizeArg,
                                        ProgramStateRef State,
                                        Optional<SVal> RetVal = None) const;

  ProgramStateRef MallocMemReturnsAttr(CheckerContext &C,
                                       const CallExpr *CE,
                                       const OwnershipAttr* Att,
                                       ProgramStateRef State) const;
  static ProgramStateRef MallocMemAux(CheckerContext &C, const CallExpr *CE,
                                      const Expr *SizeEx, SVal Init,
                                      ProgramStateRef State,
                                      AllocationFamily Family = AF_Malloc);
  static ProgramStateRef MallocMemAux(CheckerContext &C, const CallExpr *CE,
                                      SVal SizeEx, SVal Init,
                                      ProgramStateRef State,
                                      AllocationFamily Family = AF_Malloc);

  static ProgramStateRef addExtentSize(CheckerContext &C, const CXXNewExpr *NE,
                                       ProgramStateRef State, SVal Target);

  // Check if this malloc() for special flags. At present that means M_ZERO or
  // __GFP_ZERO (in which case, treat it like calloc).
  llvm::Optional<ProgramStateRef>
  performKernelMalloc(const CallExpr *CE, CheckerContext &C,
                      const ProgramStateRef &State) const;

  /// Update the RefState to reflect the new memory allocation.
  /// The optional \p RetVal parameter specifies the newly allocated pointer
  /// value; if unspecified, the value of expression \p E is used.
  static ProgramStateRef
  MallocUpdateRefState(CheckerContext &C, const Expr *E, ProgramStateRef State,
                       AllocationFamily Family = AF_Malloc,
                       Optional<SVal> RetVal = None);

  ProgramStateRef FreeMemAttr(CheckerContext &C, const CallExpr *CE,
                              const OwnershipAttr* Att,
                              ProgramStateRef State) const;
  ProgramStateRef FreeMemAux(CheckerContext &C, const CallExpr *CE,
                             ProgramStateRef state, unsigned Num,
                             bool Hold,
                             bool &ReleasedAllocated,
                             bool ReturnsNullOnFailure = false) const;
  ProgramStateRef FreeMemAux(CheckerContext &C, const Expr *Arg,
                             const Expr *ParentExpr,
                             ProgramStateRef State,
                             bool Hold,
                             bool &ReleasedAllocated,
                             bool ReturnsNullOnFailure = false) const;

  ProgramStateRef ReallocMemAux(CheckerContext &C, const CallExpr *CE,
                                bool FreesMemOnFailure,
                                ProgramStateRef State,
                                bool SuffixWithN = false) const;
  static SVal evalMulForBufferSize(CheckerContext &C, const Expr *Blocks,
                                   const Expr *BlockBytes);
  static ProgramStateRef CallocMem(CheckerContext &C, const CallExpr *CE,
                                   ProgramStateRef State);

  /// Check if the memory associated with this symbol was released.
  bool isReleased(SymbolRef Sym, CheckerContext &C) const;

  bool checkUseAfterFree(SymbolRef Sym, CheckerContext &C, const Stmt *S) const;

  void checkUseZeroAllocated(SymbolRef Sym, CheckerContext &C,
                             const Stmt *S) const;

  bool checkDoubleDelete(SymbolRef Sym, CheckerContext &C) const;

  /// Check if the function is known free memory, or if it is
  /// "interesting" and should be modeled explicitly.
  ///
  /// \param [out] EscapingSymbol A function might not free memory in general,
  ///   but could be known to free a particular symbol. In this case, false is
  ///   returned and the single escaping symbol is returned through the out
  ///   parameter.
  ///
  /// We assume that pointers do not escape through calls to system functions
  /// not handled by this checker.
  bool mayFreeAnyEscapedMemoryOrIsModeledExplicitly(const CallEvent *Call,
                                   ProgramStateRef State,
                                   SymbolRef &EscapingSymbol) const;

  // Implementation of the checkPointerEscape callbacks.
  ProgramStateRef checkPointerEscapeAux(ProgramStateRef State,
                                  const InvalidatedSymbols &Escaped,
                                  const CallEvent *Call,
                                  PointerEscapeKind Kind,
                                  bool(*CheckRefState)(const RefState*)) const;

  // Implementation of the checkPreStmt and checkEndFunction callbacks.
  void checkEscapeOnReturn(const ReturnStmt *S, CheckerContext &C) const;

  ///@{
  /// Tells if a given family/call/symbol is tracked by the current checker.
  /// Sets CheckKind to the kind of the checker responsible for this
  /// family/call/symbol.
  Optional<CheckKind> getCheckIfTracked(AllocationFamily Family,
                                        bool IsALeakCheck = false) const;
  Optional<CheckKind> getCheckIfTracked(CheckerContext &C,
                                        const Stmt *AllocDeallocStmt,
                                        bool IsALeakCheck = false) const;
  Optional<CheckKind> getCheckIfTracked(CheckerContext &C, SymbolRef Sym,
                                        bool IsALeakCheck = false) const;
  ///@}
  static bool SummarizeValue(raw_ostream &os, SVal V);
  static bool SummarizeRegion(raw_ostream &os, const MemRegion *MR);
  void ReportBadFree(CheckerContext &C, SVal ArgVal, SourceRange Range,
                     const Expr *DeallocExpr) const;
  void ReportFreeAlloca(CheckerContext &C, SVal ArgVal,
                        SourceRange Range) const;
  void ReportMismatchedDealloc(CheckerContext &C, SourceRange Range,
                               const Expr *DeallocExpr, const RefState *RS,
                               SymbolRef Sym, bool OwnershipTransferred) const;
  void ReportOffsetFree(CheckerContext &C, SVal ArgVal, SourceRange Range,
                        const Expr *DeallocExpr,
                        const Expr *AllocExpr = nullptr) const;
  void ReportUseAfterFree(CheckerContext &C, SourceRange Range,
                          SymbolRef Sym) const;
  void ReportDoubleFree(CheckerContext &C, SourceRange Range, bool Released,
                        SymbolRef Sym, SymbolRef PrevSym) const;

  void ReportDoubleDelete(CheckerContext &C, SymbolRef Sym) const;

  void ReportUseZeroAllocated(CheckerContext &C, SourceRange Range,
                              SymbolRef Sym) const;

  void ReportFunctionPointerFree(CheckerContext &C, SVal ArgVal,
                                 SourceRange Range, const Expr *FreeExpr) const;

  /// Find the location of the allocation for Sym on the path leading to the
  /// exploded node N.
  LeakInfo getAllocationSite(const ExplodedNode *N, SymbolRef Sym,
                             CheckerContext &C) const;

  void reportLeak(SymbolRef Sym, ExplodedNode *N, CheckerContext &C) const;

  /// The bug visitor which allows us to print extra diagnostics along the
  /// BugReport path. For example, showing the allocation site of the leaked
  /// region.
  class MallocBugVisitor final : public BugReporterVisitor {
  protected:
    enum NotificationMode {
      Normal,
      ReallocationFailed
    };

    // The allocated region symbol tracked by the main analysis.
    SymbolRef Sym;

    // The mode we are in, i.e. what kind of diagnostics will be emitted.
    NotificationMode Mode;

    // A symbol from when the primary region should have been reallocated.
    SymbolRef FailedReallocSymbol;

    // A C++ destructor stack frame in which memory was released. Used for
    // miscellaneous false positive suppression.
    const StackFrameContext *ReleaseDestructorLC;

    bool IsLeak;

  public:
    MallocBugVisitor(SymbolRef S, bool isLeak = false)
        : Sym(S), Mode(Normal), FailedReallocSymbol(nullptr),
          ReleaseDestructorLC(nullptr), IsLeak(isLeak) {}

    static void *getTag() {
      static int Tag = 0;
      return &Tag;
    }

    void Profile(llvm::FoldingSetNodeID &ID) const override {
      ID.AddPointer(getTag());
      ID.AddPointer(Sym);
    }

    inline bool isAllocated(const RefState *S, const RefState *SPrev,
                            const Stmt *Stmt) {
      // Did not track -> allocated. Other state (released) -> allocated.
      return (Stmt && (isa<CallExpr>(Stmt) || isa<CXXNewExpr>(Stmt)) &&
              (S && (S->isAllocated() || S->isAllocatedOfSizeZero())) &&
              (!SPrev || !(SPrev->isAllocated() ||
                           SPrev->isAllocatedOfSizeZero())));
    }

    inline bool isReleased(const RefState *S, const RefState *SPrev,
                           const Stmt *Stmt) {
      // Did not track -> released. Other state (allocated) -> released.
      // The statement associated with the release might be missing.
      bool IsReleased = (S && S->isReleased()) &&
                        (!SPrev || !SPrev->isReleased());
      assert(!IsReleased ||
             (Stmt && (isa<CallExpr>(Stmt) || isa<CXXDeleteExpr>(Stmt))) ||
             (!Stmt && S->getAllocationFamily() == AF_InnerBuffer));
      return IsReleased;
    }

    inline bool isRelinquished(const RefState *S, const RefState *SPrev,
                               const Stmt *Stmt) {
      // Did not track -> relinquished. Other state (allocated) -> relinquished.
      return (Stmt && (isa<CallExpr>(Stmt) || isa<ObjCMessageExpr>(Stmt) ||
                                              isa<ObjCPropertyRefExpr>(Stmt)) &&
              (S && S->isRelinquished()) &&
              (!SPrev || !SPrev->isRelinquished()));
    }

    inline bool isReallocFailedCheck(const RefState *S, const RefState *SPrev,
                                     const Stmt *Stmt) {
      // If the expression is not a call, and the state change is
      // released -> allocated, it must be the realloc return value
      // check. If we have to handle more cases here, it might be cleaner just
      // to track this extra bit in the state itself.
      return ((!Stmt || !isa<CallExpr>(Stmt)) &&
              (S && (S->isAllocated() || S->isAllocatedOfSizeZero())) &&
              (SPrev && !(SPrev->isAllocated() ||
                          SPrev->isAllocatedOfSizeZero())));
    }

    std::shared_ptr<PathDiagnosticPiece> VisitNode(const ExplodedNode *N,
                                                   BugReporterContext &BRC,
                                                   BugReport &BR) override;

    std::shared_ptr<PathDiagnosticPiece>
    getEndPath(BugReporterContext &BRC, const ExplodedNode *EndPathNode,
               BugReport &BR) override {
      if (!IsLeak)
        return nullptr;

      PathDiagnosticLocation L =
        PathDiagnosticLocation::createEndOfPath(EndPathNode,
                                                BRC.getSourceManager());
      // Do not add the statement itself as a range in case of leak.
      return std::make_shared<PathDiagnosticEventPiece>(L, BR.getDescription(),
                                                         false);
    }

  private:
    class StackHintGeneratorForReallocationFailed
        : public StackHintGeneratorForSymbol {
    public:
      StackHintGeneratorForReallocationFailed(SymbolRef S, StringRef M)
        : StackHintGeneratorForSymbol(S, M) {}

      std::string getMessageForArg(const Expr *ArgE,
                                   unsigned ArgIndex) override {
        // Printed parameters start at 1, not 0.
        ++ArgIndex;

        SmallString<200> buf;
        llvm::raw_svector_ostream os(buf);

        os << "Reallocation of " << ArgIndex << llvm::getOrdinalSuffix(ArgIndex)
           << " parameter failed";

        return os.str();
      }

      std::string getMessageForReturn(const CallExpr *CallExpr) override {
        return "Reallocation of returned value failed";
      }
    };
  };
};
} // end anonymous namespace

REGISTER_MAP_WITH_PROGRAMSTATE(RegionState, SymbolRef, RefState)
REGISTER_MAP_WITH_PROGRAMSTATE(ReallocPairs, SymbolRef, ReallocPair)
REGISTER_SET_WITH_PROGRAMSTATE(ReallocSizeZeroSymbols, SymbolRef)

// A map from the freed symbol to the symbol representing the return value of
// the free function.
REGISTER_MAP_WITH_PROGRAMSTATE(FreeReturnValue, SymbolRef, SymbolRef)

namespace {
class StopTrackingCallback final : public SymbolVisitor {
  ProgramStateRef state;
public:
  StopTrackingCallback(ProgramStateRef st) : state(std::move(st)) {}
  ProgramStateRef getState() const { return state; }

  bool VisitSymbol(SymbolRef sym) override {
    state = state->remove<RegionState>(sym);
    return true;
  }
};
} // end anonymous namespace

void MallocChecker::initIdentifierInfo(ASTContext &Ctx) const {
  if (II_malloc)
    return;
  II_alloca = &Ctx.Idents.get("alloca");
  II_malloc = &Ctx.Idents.get("malloc");
  II_free = &Ctx.Idents.get("free");
  II_realloc = &Ctx.Idents.get("realloc");
  II_reallocf = &Ctx.Idents.get("reallocf");
  II_calloc = &Ctx.Idents.get("calloc");
  II_valloc = &Ctx.Idents.get("valloc");
  II_strdup = &Ctx.Idents.get("strdup");
  II_strndup = &Ctx.Idents.get("strndup");
  II_wcsdup = &Ctx.Idents.get("wcsdup");
  II_kmalloc = &Ctx.Idents.get("kmalloc");
  II_if_nameindex = &Ctx.Idents.get("if_nameindex");
  II_if_freenameindex = &Ctx.Idents.get("if_freenameindex");

  //MSVC uses `_`-prefixed instead, so we check for them too.
  II_win_strdup = &Ctx.Idents.get("_strdup");
  II_win_wcsdup = &Ctx.Idents.get("_wcsdup");
  II_win_alloca = &Ctx.Idents.get("_alloca");

  // Glib
  II_g_malloc = &Ctx.Idents.get("g_malloc");
  II_g_malloc0 = &Ctx.Idents.get("g_malloc0");
  II_g_realloc = &Ctx.Idents.get("g_realloc");
  II_g_try_malloc = &Ctx.Idents.get("g_try_malloc");
  II_g_try_malloc0 = &Ctx.Idents.get("g_try_malloc0");
  II_g_try_realloc = &Ctx.Idents.get("g_try_realloc");
  II_g_free = &Ctx.Idents.get("g_free");
  II_g_memdup = &Ctx.Idents.get("g_memdup");
  II_g_malloc_n = &Ctx.Idents.get("g_malloc_n");
  II_g_malloc0_n = &Ctx.Idents.get("g_malloc0_n");
  II_g_realloc_n = &Ctx.Idents.get("g_realloc_n");
  II_g_try_malloc_n = &Ctx.Idents.get("g_try_malloc_n");
  II_g_try_malloc0_n = &Ctx.Idents.get("g_try_malloc0_n");
  II_g_try_realloc_n = &Ctx.Idents.get("g_try_realloc_n");
}

bool MallocChecker::isMemFunction(const FunctionDecl *FD, ASTContext &C) const {
  if (isCMemFunction(FD, C, AF_Malloc, MemoryOperationKind::MOK_Any))
    return true;

  if (isCMemFunction(FD, C, AF_IfNameIndex, MemoryOperationKind::MOK_Any))
    return true;

  if (isCMemFunction(FD, C, AF_Alloca, MemoryOperationKind::MOK_Any))
    return true;

  if (isStandardNewDelete(FD, C))
    return true;

  return false;
}

bool MallocChecker::isCMemFunction(const FunctionDecl *FD,
                                   ASTContext &C,
                                   AllocationFamily Family,
                                   MemoryOperationKind MemKind) const {
  if (!FD)
    return false;

  bool CheckFree = (MemKind == MemoryOperationKind::MOK_Any ||
                    MemKind == MemoryOperationKind::MOK_Free);
  bool CheckAlloc = (MemKind == MemoryOperationKind::MOK_Any ||
                     MemKind == MemoryOperationKind::MOK_Allocate);

  if (FD->getKind() == Decl::Function) {
    const IdentifierInfo *FunI = FD->getIdentifier();
    initIdentifierInfo(C);

    if (Family == AF_Malloc && CheckFree) {
      if (FunI == II_free || FunI == II_realloc || FunI == II_reallocf ||
          FunI == II_g_free)
        return true;
    }

    if (Family == AF_Malloc && CheckAlloc) {
      if (FunI == II_malloc || FunI == II_realloc || FunI == II_reallocf ||
          FunI == II_calloc || FunI == II_valloc || FunI == II_strdup ||
          FunI == II_win_strdup || FunI == II_strndup || FunI == II_wcsdup ||
          FunI == II_win_wcsdup || FunI == II_kmalloc ||
          FunI == II_g_malloc || FunI == II_g_malloc0 ||
          FunI == II_g_realloc || FunI == II_g_try_malloc ||
          FunI == II_g_try_malloc0 || FunI == II_g_try_realloc ||
          FunI == II_g_memdup || FunI == II_g_malloc_n ||
          FunI == II_g_malloc0_n || FunI == II_g_realloc_n ||
          FunI == II_g_try_malloc_n || FunI == II_g_try_malloc0_n ||
          FunI == II_g_try_realloc_n)
        return true;
    }

    if (Family == AF_IfNameIndex && CheckFree) {
      if (FunI == II_if_freenameindex)
        return true;
    }

    if (Family == AF_IfNameIndex && CheckAlloc) {
      if (FunI == II_if_nameindex)
        return true;
    }

    if (Family == AF_Alloca && CheckAlloc) {
      if (FunI == II_alloca || FunI == II_win_alloca)
        return true;
    }
  }

  if (Family != AF_Malloc)
    return false;

  if (IsOptimistic && FD->hasAttrs()) {
    for (const auto *I : FD->specific_attrs<OwnershipAttr>()) {
      OwnershipAttr::OwnershipKind OwnKind = I->getOwnKind();
      if(OwnKind == OwnershipAttr::Takes || OwnKind == OwnershipAttr::Holds) {
        if (CheckFree)
          return true;
      } else if (OwnKind == OwnershipAttr::Returns) {
        if (CheckAlloc)
          return true;
      }
    }
  }

  return false;
}

// Tells if the callee is one of the builtin new/delete operators, including
// placement operators and other standard overloads.
bool MallocChecker::isStandardNewDelete(const FunctionDecl *FD,
                                        ASTContext &C) const {
  if (!FD)
    return false;

  OverloadedOperatorKind Kind = FD->getOverloadedOperator();
  if (Kind != OO_New && Kind != OO_Array_New &&
      Kind != OO_Delete && Kind != OO_Array_Delete)
    return false;

  // This is standard if and only if it's not defined in a user file.
  SourceLocation L = FD->getLocation();
  // If the header for operator delete is not included, it's still defined
  // in an invalid source location. Check to make sure we don't crash.
  return !L.isValid() || C.getSourceManager().isInSystemHeader(L);
}

llvm::Optional<ProgramStateRef> MallocChecker::performKernelMalloc(
  const CallExpr *CE, CheckerContext &C, const ProgramStateRef &State) const {
  // 3-argument malloc(), as commonly used in {Free,Net,Open}BSD Kernels:
  //
  // void *malloc(unsigned long size, struct malloc_type *mtp, int flags);
  //
  // One of the possible flags is M_ZERO, which means 'give me back an
  // allocation which is already zeroed', like calloc.

  // 2-argument kmalloc(), as used in the Linux kernel:
  //
  // void *kmalloc(size_t size, gfp_t flags);
  //
  // Has the similar flag value __GFP_ZERO.

  // This logic is largely cloned from O_CREAT in UnixAPIChecker, maybe some
  // code could be shared.

  ASTContext &Ctx = C.getASTContext();
  llvm::Triple::OSType OS = Ctx.getTargetInfo().getTriple().getOS();

  if (!KernelZeroFlagVal.hasValue()) {
    if (OS == llvm::Triple::FreeBSD)
      KernelZeroFlagVal = 0x0100;
    else if (OS == llvm::Triple::NetBSD)
      KernelZeroFlagVal = 0x0002;
    else if (OS == llvm::Triple::OpenBSD)
      KernelZeroFlagVal = 0x0008;
    else if (OS == llvm::Triple::Linux)
      // __GFP_ZERO
      KernelZeroFlagVal = 0x8000;
    else
      // FIXME: We need a more general way of getting the M_ZERO value.
      // See also: O_CREAT in UnixAPIChecker.cpp.

      // Fall back to normal malloc behavior on platforms where we don't
      // know M_ZERO.
      return None;
  }

  // We treat the last argument as the flags argument, and callers fall-back to
  // normal malloc on a None return. This works for the FreeBSD kernel malloc
  // as well as Linux kmalloc.
  if (CE->getNumArgs() < 2)
    return None;

  const Expr *FlagsEx = CE->getArg(CE->getNumArgs() - 1);
  const SVal V = C.getSVal(FlagsEx);
  if (!V.getAs<NonLoc>()) {
    // The case where 'V' can be a location can only be due to a bad header,
    // so in this case bail out.
    return None;
  }

  NonLoc Flags = V.castAs<NonLoc>();
  NonLoc ZeroFlag = C.getSValBuilder()
      .makeIntVal(KernelZeroFlagVal.getValue(), FlagsEx->getType())
      .castAs<NonLoc>();
  SVal MaskedFlagsUC = C.getSValBuilder().evalBinOpNN(State, BO_And,
                                                      Flags, ZeroFlag,
                                                      FlagsEx->getType());
  if (MaskedFlagsUC.isUnknownOrUndef())
    return None;
  DefinedSVal MaskedFlags = MaskedFlagsUC.castAs<DefinedSVal>();

  // Check if maskedFlags is non-zero.
  ProgramStateRef TrueState, FalseState;
  std::tie(TrueState, FalseState) = State->assume(MaskedFlags);

  // If M_ZERO is set, treat this like calloc (initialized).
  if (TrueState && !FalseState) {
    SVal ZeroVal = C.getSValBuilder().makeZeroVal(Ctx.CharTy);
    return MallocMemAux(C, CE, CE->getArg(0), ZeroVal, TrueState);
  }

  return None;
}

SVal MallocChecker::evalMulForBufferSize(CheckerContext &C, const Expr *Blocks,
                                         const Expr *BlockBytes) {
  SValBuilder &SB = C.getSValBuilder();
  SVal BlocksVal = C.getSVal(Blocks);
  SVal BlockBytesVal = C.getSVal(BlockBytes);
  ProgramStateRef State = C.getState();
  SVal TotalSize = SB.evalBinOp(State, BO_Mul, BlocksVal, BlockBytesVal,
                                SB.getContext().getSizeType());
  return TotalSize;
}

void MallocChecker::checkPostStmt(const CallExpr *CE, CheckerContext &C) const {
  if (C.wasInlined)
    return;

  const FunctionDecl *FD = C.getCalleeDecl(CE);
  if (!FD)
    return;

  ProgramStateRef State = C.getState();
  bool ReleasedAllocatedMemory = false;

  if (FD->getKind() == Decl::Function) {
    initIdentifierInfo(C.getASTContext());
    IdentifierInfo *FunI = FD->getIdentifier();

    if (FunI == II_malloc || FunI == II_g_malloc || FunI == II_g_try_malloc) {
      if (CE->getNumArgs() < 1)
        return;
      if (CE->getNumArgs() < 3) {
        State = MallocMemAux(C, CE, CE->getArg(0), UndefinedVal(), State);
        if (CE->getNumArgs() == 1)
          State = ProcessZeroAllocation(C, CE, 0, State);
      } else if (CE->getNumArgs() == 3) {
        llvm::Optional<ProgramStateRef> MaybeState =
          performKernelMalloc(CE, C, State);
        if (MaybeState.hasValue())
          State = MaybeState.getValue();
        else
          State = MallocMemAux(C, CE, CE->getArg(0), UndefinedVal(), State);
      }
    } else if (FunI == II_kmalloc) {
      if (CE->getNumArgs() < 1)
        return;
      llvm::Optional<ProgramStateRef> MaybeState =
        performKernelMalloc(CE, C, State);
      if (MaybeState.hasValue())
        State = MaybeState.getValue();
      else
        State = MallocMemAux(C, CE, CE->getArg(0), UndefinedVal(), State);
    } else if (FunI == II_valloc) {
      if (CE->getNumArgs() < 1)
        return;
      State = MallocMemAux(C, CE, CE->getArg(0), UndefinedVal(), State);
      State = ProcessZeroAllocation(C, CE, 0, State);
    } else if (FunI == II_realloc || FunI == II_g_realloc ||
               FunI == II_g_try_realloc) {
      State = ReallocMemAux(C, CE, false, State);
      State = ProcessZeroAllocation(C, CE, 1, State);
    } else if (FunI == II_reallocf) {
      State = ReallocMemAux(C, CE, true, State);
      State = ProcessZeroAllocation(C, CE, 1, State);
    } else if (FunI == II_calloc) {
      State = CallocMem(C, CE, State);
      State = ProcessZeroAllocation(C, CE, 0, State);
      State = ProcessZeroAllocation(C, CE, 1, State);
    } else if (FunI == II_free || FunI == II_g_free) {
      State = FreeMemAux(C, CE, State, 0, false, ReleasedAllocatedMemory);
    } else if (FunI == II_strdup || FunI == II_win_strdup ||
               FunI == II_wcsdup || FunI == II_win_wcsdup) {
      State = MallocUpdateRefState(C, CE, State);
    } else if (FunI == II_strndup) {
      State = MallocUpdateRefState(C, CE, State);
    } else if (FunI == II_alloca || FunI == II_win_alloca) {
      if (CE->getNumArgs() < 1)
        return;
      State = MallocMemAux(C, CE, CE->getArg(0), UndefinedVal(), State,
                           AF_Alloca);
      State = ProcessZeroAllocation(C, CE, 0, State);
    } else if (isStandardNewDelete(FD, C.getASTContext())) {
      // Process direct calls to operator new/new[]/delete/delete[] functions
      // as distinct from new/new[]/delete/delete[] expressions that are
      // processed by the checkPostStmt callbacks for CXXNewExpr and
      // CXXDeleteExpr.
      OverloadedOperatorKind K = FD->getOverloadedOperator();
      if (K == OO_New) {
        State = MallocMemAux(C, CE, CE->getArg(0), UndefinedVal(), State,
                             AF_CXXNew);
        State = ProcessZeroAllocation(C, CE, 0, State);
      }
      else if (K == OO_Array_New) {
        State = MallocMemAux(C, CE, CE->getArg(0), UndefinedVal(), State,
                             AF_CXXNewArray);
        State = ProcessZeroAllocation(C, CE, 0, State);
      }
      else if (K == OO_Delete || K == OO_Array_Delete)
        State = FreeMemAux(C, CE, State, 0, false, ReleasedAllocatedMemory);
      else
        llvm_unreachable("not a new/delete operator");
    } else if (FunI == II_if_nameindex) {
      // Should we model this differently? We can allocate a fixed number of
      // elements with zeros in the last one.
      State = MallocMemAux(C, CE, UnknownVal(), UnknownVal(), State,
                           AF_IfNameIndex);
    } else if (FunI == II_if_freenameindex) {
      State = FreeMemAux(C, CE, State, 0, false, ReleasedAllocatedMemory);
    } else if (FunI == II_g_malloc0 || FunI == II_g_try_malloc0) {
      if (CE->getNumArgs() < 1)
        return;
      SValBuilder &svalBuilder = C.getSValBuilder();
      SVal zeroVal = svalBuilder.makeZeroVal(svalBuilder.getContext().CharTy);
      State = MallocMemAux(C, CE, CE->getArg(0), zeroVal, State);
      State = ProcessZeroAllocation(C, CE, 0, State);
    } else if (FunI == II_g_memdup) {
      if (CE->getNumArgs() < 2)
        return;
      State = MallocMemAux(C, CE, CE->getArg(1), UndefinedVal(), State);
      State = ProcessZeroAllocation(C, CE, 1, State);
    } else if (FunI == II_g_malloc_n || FunI == II_g_try_malloc_n ||
               FunI == II_g_malloc0_n || FunI == II_g_try_malloc0_n) {
      if (CE->getNumArgs() < 2)
        return;
      SVal Init = UndefinedVal();
      if (FunI == II_g_malloc0_n || FunI == II_g_try_malloc0_n) {
        SValBuilder &SB = C.getSValBuilder();
        Init = SB.makeZeroVal(SB.getContext().CharTy);
      }
      SVal TotalSize = evalMulForBufferSize(C, CE->getArg(0), CE->getArg(1));
      State = MallocMemAux(C, CE, TotalSize, Init, State);
      State = ProcessZeroAllocation(C, CE, 0, State);
      State = ProcessZeroAllocation(C, CE, 1, State);
    } else if (FunI == II_g_realloc_n || FunI == II_g_try_realloc_n) {
      if (CE->getNumArgs() < 3)
        return;
      State = ReallocMemAux(C, CE, false, State, true);
      State = ProcessZeroAllocation(C, CE, 1, State);
      State = ProcessZeroAllocation(C, CE, 2, State);
    }
  }

  if (IsOptimistic || ChecksEnabled[CK_MismatchedDeallocatorChecker]) {
    // Check all the attributes, if there are any.
    // There can be multiple of these attributes.
    if (FD->hasAttrs())
      for (const auto *I : FD->specific_attrs<OwnershipAttr>()) {
        switch (I->getOwnKind()) {
        case OwnershipAttr::Returns:
          State = MallocMemReturnsAttr(C, CE, I, State);
          break;
        case OwnershipAttr::Takes:
        case OwnershipAttr::Holds:
          State = FreeMemAttr(C, CE, I, State);
          break;
        }
      }
  }
  C.addTransition(State);
}

// Performs a 0-sized allocations check.
ProgramStateRef MallocChecker::ProcessZeroAllocation(
    CheckerContext &C, const Expr *E, const unsigned AllocationSizeArg,
    ProgramStateRef State, Optional<SVal> RetVal) const {
  if (!State)
    return nullptr;

  if (!RetVal)
    RetVal = C.getSVal(E);

  const Expr *Arg = nullptr;

  if (const CallExpr *CE = dyn_cast<CallExpr>(E)) {
    Arg = CE->getArg(AllocationSizeArg);
  }
  else if (const CXXNewExpr *NE = dyn_cast<CXXNewExpr>(E)) {
    if (NE->isArray())
      Arg = NE->getArraySize();
    else
      return State;
  }
  else
    llvm_unreachable("not a CallExpr or CXXNewExpr");

  assert(Arg);

  Optional<DefinedSVal> DefArgVal = C.getSVal(Arg).getAs<DefinedSVal>();

  if (!DefArgVal)
    return State;

  // Check if the allocation size is 0.
  ProgramStateRef TrueState, FalseState;
  SValBuilder &SvalBuilder = C.getSValBuilder();
  DefinedSVal Zero =
      SvalBuilder.makeZeroVal(Arg->getType()).castAs<DefinedSVal>();

  std::tie(TrueState, FalseState) =
      State->assume(SvalBuilder.evalEQ(State, *DefArgVal, Zero));

  if (TrueState && !FalseState) {
    SymbolRef Sym = RetVal->getAsLocSymbol();
    if (!Sym)
      return State;

    const RefState *RS = State->get<RegionState>(Sym);
    if (RS) {
      if (RS->isAllocated())
        return TrueState->set<RegionState>(Sym,
                                          RefState::getAllocatedOfSizeZero(RS));
      else
        return State;
    } else {
      // Case of zero-size realloc. Historically 'realloc(ptr, 0)' is treated as
      // 'free(ptr)' and the returned value from 'realloc(ptr, 0)' is not
      // tracked. Add zero-reallocated Sym to the state to catch references
      // to zero-allocated memory.
      return TrueState->add<ReallocSizeZeroSymbols>(Sym);
    }
  }

  // Assume the value is non-zero going forward.
  assert(FalseState);
  return FalseState;
}

static QualType getDeepPointeeType(QualType T) {
  QualType Result = T, PointeeType = T->getPointeeType();
  while (!PointeeType.isNull()) {
    Result = PointeeType;
    PointeeType = PointeeType->getPointeeType();
  }
  return Result;
}

static bool treatUnusedNewEscaped(const CXXNewExpr *NE) {

  const CXXConstructExpr *ConstructE = NE->getConstructExpr();
  if (!ConstructE)
    return false;

  if (!NE->getAllocatedType()->getAsCXXRecordDecl())
    return false;

  const CXXConstructorDecl *CtorD = ConstructE->getConstructor();

  // Iterate over the constructor parameters.
  for (const auto *CtorParam : CtorD->parameters()) {

    QualType CtorParamPointeeT = CtorParam->getType()->getPointeeType();
    if (CtorParamPointeeT.isNull())
      continue;

    CtorParamPointeeT = getDeepPointeeType(CtorParamPointeeT);

    if (CtorParamPointeeT->getAsCXXRecordDecl())
      return true;
  }

  return false;
}

void MallocChecker::processNewAllocation(const CXXNewExpr *NE,
                                         CheckerContext &C,
                                         SVal Target) const {
  if (!isStandardNewDelete(NE->getOperatorNew(), C.getASTContext()))
    return;

  ParentMap &PM = C.getLocationContext()->getParentMap();
  if (!PM.isConsumedExpr(NE) && treatUnusedNewEscaped(NE))
    return;

  ProgramStateRef State = C.getState();
  // The return value from operator new is bound to a specified initialization
  // value (if any) and we don't want to loose this value. So we call
  // MallocUpdateRefState() instead of MallocMemAux() which breaks the
  // existing binding.
  State = MallocUpdateRefState(C, NE, State, NE->isArray() ? AF_CXXNewArray
                                                           : AF_CXXNew, Target);
  State = addExtentSize(C, NE, State, Target);
  State = ProcessZeroAllocation(C, NE, 0, State, Target);
  C.addTransition(State);
}

void MallocChecker::checkPostStmt(const CXXNewExpr *NE,
                                  CheckerContext &C) const {
  if (!C.getAnalysisManager().getAnalyzerOptions().MayInlineCXXAllocator)
    processNewAllocation(NE, C, C.getSVal(NE));
}

void MallocChecker::checkNewAllocator(const CXXNewExpr *NE, SVal Target,
                                      CheckerContext &C) const {
  if (!C.wasInlined)
    processNewAllocation(NE, C, Target);
}

// Sets the extent value of the MemRegion allocated by
// new expression NE to its size in Bytes.
//
ProgramStateRef MallocChecker::addExtentSize(CheckerContext &C,
                                             const CXXNewExpr *NE,
                                             ProgramStateRef State,
                                             SVal Target) {
  if (!State)
    return nullptr;
  SValBuilder &svalBuilder = C.getSValBuilder();
  SVal ElementCount;
  const SubRegion *Region;
  if (NE->isArray()) {
    const Expr *SizeExpr = NE->getArraySize();
    ElementCount = C.getSVal(SizeExpr);
    // Store the extent size for the (symbolic)region
    // containing the elements.
    Region = Target.getAsRegion()
                 ->getAs<SubRegion>()
                 ->StripCasts()
                 ->getAs<SubRegion>();
  } else {
    ElementCount = svalBuilder.makeIntVal(1, true);
    Region = Target.getAsRegion()->getAs<SubRegion>();
  }
  assert(Region);

  // Set the region's extent equal to the Size in Bytes.
  QualType ElementType = NE->getAllocatedType();
  ASTContext &AstContext = C.getASTContext();
  CharUnits TypeSize = AstContext.getTypeSizeInChars(ElementType);

  if (ElementCount.getAs<NonLoc>()) {
    DefinedOrUnknownSVal Extent = Region->getExtent(svalBuilder);
    // size in Bytes = ElementCount*TypeSize
    SVal SizeInBytes = svalBuilder.evalBinOpNN(
        State, BO_Mul, ElementCount.castAs<NonLoc>(),
        svalBuilder.makeArrayIndex(TypeSize.getQuantity()),
        svalBuilder.getArrayIndexType());
    DefinedOrUnknownSVal extentMatchesSize = svalBuilder.evalEQ(
        State, Extent, SizeInBytes.castAs<DefinedOrUnknownSVal>());
    State = State->assume(extentMatchesSize, true);
  }
  return State;
}

void MallocChecker::checkPreStmt(const CXXDeleteExpr *DE,
                                 CheckerContext &C) const {

  if (!ChecksEnabled[CK_NewDeleteChecker])
    if (SymbolRef Sym = C.getSVal(DE->getArgument()).getAsSymbol())
      checkUseAfterFree(Sym, C, DE->getArgument());

  if (!isStandardNewDelete(DE->getOperatorDelete(), C.getASTContext()))
    return;

  ProgramStateRef State = C.getState();
  bool ReleasedAllocated;
  State = FreeMemAux(C, DE->getArgument(), DE, State,
                     /*Hold*/false, ReleasedAllocated);

  C.addTransition(State);
}

static bool isKnownDeallocObjCMethodName(const ObjCMethodCall &Call) {
  // If the first selector piece is one of the names below, assume that the
  // object takes ownership of the memory, promising to eventually deallocate it
  // with free().
  // Ex:  [NSData dataWithBytesNoCopy:bytes length:10];
  // (...unless a 'freeWhenDone' parameter is false, but that's checked later.)
  StringRef FirstSlot = Call.getSelector().getNameForSlot(0);
  return FirstSlot == "dataWithBytesNoCopy" ||
         FirstSlot == "initWithBytesNoCopy" ||
         FirstSlot == "initWithCharactersNoCopy";
}

static Optional<bool> getFreeWhenDoneArg(const ObjCMethodCall &Call) {
  Selector S = Call.getSelector();

  // FIXME: We should not rely on fully-constrained symbols being folded.
  for (unsigned i = 1; i < S.getNumArgs(); ++i)
    if (S.getNameForSlot(i).equals("freeWhenDone"))
      return !Call.getArgSVal(i).isZeroConstant();

  return None;
}

void MallocChecker::checkPostObjCMessage(const ObjCMethodCall &Call,
                                         CheckerContext &C) const {
  if (C.wasInlined)
    return;

  if (!isKnownDeallocObjCMethodName(Call))
    return;

  if (Optional<bool> FreeWhenDone = getFreeWhenDoneArg(Call))
    if (!*FreeWhenDone)
      return;

  bool ReleasedAllocatedMemory;
  ProgramStateRef State = FreeMemAux(C, Call.getArgExpr(0),
                                     Call.getOriginExpr(), C.getState(),
                                     /*Hold=*/true, ReleasedAllocatedMemory,
                                     /*RetNullOnFailure=*/true);

  C.addTransition(State);
}

ProgramStateRef
MallocChecker::MallocMemReturnsAttr(CheckerContext &C, const CallExpr *CE,
                                    const OwnershipAttr *Att,
                                    ProgramStateRef State) const {
  if (!State)
    return nullptr;

  if (Att->getModule() != II_malloc)
    return nullptr;

  OwnershipAttr::args_iterator I = Att->args_begin(), E = Att->args_end();
  if (I != E) {
    return MallocMemAux(C, CE, CE->getArg(I->getASTIndex()), UndefinedVal(),
                        State);
  }
  return MallocMemAux(C, CE, UnknownVal(), UndefinedVal(), State);
}

ProgramStateRef MallocChecker::MallocMemAux(CheckerContext &C,
                                            const CallExpr *CE,
                                            const Expr *SizeEx, SVal Init,
                                            ProgramStateRef State,
                                            AllocationFamily Family) {
  if (!State)
    return nullptr;

  return MallocMemAux(C, CE, C.getSVal(SizeEx), Init, State, Family);
}

ProgramStateRef MallocChecker::MallocMemAux(CheckerContext &C,
                                           const CallExpr *CE,
                                           SVal Size, SVal Init,
                                           ProgramStateRef State,
                                           AllocationFamily Family) {
  if (!State)
    return nullptr;

  // We expect the malloc functions to return a pointer.
  if (!Loc::isLocType(CE->getType()))
    return nullptr;

  // Bind the return value to the symbolic value from the heap region.
  // TODO: We could rewrite post visit to eval call; 'malloc' does not have
  // side effects other than what we model here.
  unsigned Count = C.blockCount();
  SValBuilder &svalBuilder = C.getSValBuilder();
  const LocationContext *LCtx = C.getPredecessor()->getLocationContext();
  DefinedSVal RetVal = svalBuilder.getConjuredHeapSymbolVal(CE, LCtx, Count)
      .castAs<DefinedSVal>();
  State = State->BindExpr(CE, C.getLocationContext(), RetVal);

  // Fill the region with the initialization value.
  State = State->bindDefaultInitial(RetVal, Init, LCtx);

  // Set the region's extent equal to the Size parameter.
  const SymbolicRegion *R =
      dyn_cast_or_null<SymbolicRegion>(RetVal.getAsRegion());
  if (!R)
    return nullptr;
  if (Optional<DefinedOrUnknownSVal> DefinedSize =
          Size.getAs<DefinedOrUnknownSVal>()) {
    SValBuilder &svalBuilder = C.getSValBuilder();
    DefinedOrUnknownSVal Extent = R->getExtent(svalBuilder);
    DefinedOrUnknownSVal extentMatchesSize =
        svalBuilder.evalEQ(State, Extent, *DefinedSize);

    State = State->assume(extentMatchesSize, true);
    assert(State);
  }

  return MallocUpdateRefState(C, CE, State, Family);
}

ProgramStateRef MallocChecker::MallocUpdateRefState(CheckerContext &C,
                                                    const Expr *E,
                                                    ProgramStateRef State,
                                                    AllocationFamily Family,
                                                    Optional<SVal> RetVal) {
  if (!State)
    return nullptr;

  // Get the return value.
  if (!RetVal)
    RetVal = C.getSVal(E);

  // We expect the malloc functions to return a pointer.
  if (!RetVal->getAs<Loc>())
    return nullptr;

  SymbolRef Sym = RetVal->getAsLocSymbol();
  // This is a return value of a function that was not inlined, such as malloc()
  // or new(). We've checked that in the caller. Therefore, it must be a symbol.
  assert(Sym);

  // Set the symbol's state to Allocated.
  return State->set<RegionState>(Sym, RefState::getAllocated(Family, E));
}

ProgramStateRef MallocChecker::FreeMemAttr(CheckerContext &C,
                                           const CallExpr *CE,
                                           const OwnershipAttr *Att,
                                           ProgramStateRef State) const {
  if (!State)
    return nullptr;

  if (Att->getModule() != II_malloc)
    return nullptr;

  bool ReleasedAllocated = false;

  for (const auto &Arg : Att->args()) {
    ProgramStateRef StateI = FreeMemAux(
        C, CE, State, Arg.getASTIndex(),
        Att->getOwnKind() == OwnershipAttr::Holds, ReleasedAllocated);
    if (StateI)
      State = StateI;
  }
  return State;
}

ProgramStateRef MallocChecker::FreeMemAux(CheckerContext &C,
                                          const CallExpr *CE,
                                          ProgramStateRef State,
                                          unsigned Num,
                                          bool Hold,
                                          bool &ReleasedAllocated,
                                          bool ReturnsNullOnFailure) const {
  if (!State)
    return nullptr;

  if (CE->getNumArgs() < (Num + 1))
    return nullptr;

  return FreeMemAux(C, CE->getArg(Num), CE, State, Hold,
                    ReleasedAllocated, ReturnsNullOnFailure);
}

/// Checks if the previous call to free on the given symbol failed - if free
/// failed, returns true. Also, returns the corresponding return value symbol.
static bool didPreviousFreeFail(ProgramStateRef State,
                                SymbolRef Sym, SymbolRef &RetStatusSymbol) {
  const SymbolRef *Ret = State->get<FreeReturnValue>(Sym);
  if (Ret) {
    assert(*Ret && "We should not store the null return symbol");
    ConstraintManager &CMgr = State->getConstraintManager();
    ConditionTruthVal FreeFailed = CMgr.isNull(State, *Ret);
    RetStatusSymbol = *Ret;
    return FreeFailed.isConstrainedTrue();
  }
  return false;
}

AllocationFamily MallocChecker::getAllocationFamily(CheckerContext &C,
                                                    const Stmt *S) const {
  if (!S)
    return AF_None;

  if (const CallExpr *CE = dyn_cast<CallExpr>(S)) {
    const FunctionDecl *FD = C.getCalleeDecl(CE);

    if (!FD)
      FD = dyn_cast<FunctionDecl>(CE->getCalleeDecl());

    ASTContext &Ctx = C.getASTContext();

    if (isCMemFunction(FD, Ctx, AF_Malloc, MemoryOperationKind::MOK_Any))
      return AF_Malloc;

    if (isStandardNewDelete(FD, Ctx)) {
      OverloadedOperatorKind Kind = FD->getOverloadedOperator();
      if (Kind == OO_New || Kind == OO_Delete)
        return AF_CXXNew;
      else if (Kind == OO_Array_New || Kind == OO_Array_Delete)
        return AF_CXXNewArray;
    }

    if (isCMemFunction(FD, Ctx, AF_IfNameIndex, MemoryOperationKind::MOK_Any))
      return AF_IfNameIndex;

    if (isCMemFunction(FD, Ctx, AF_Alloca, MemoryOperationKind::MOK_Any))
      return AF_Alloca;

    return AF_None;
  }

  if (const CXXNewExpr *NE = dyn_cast<CXXNewExpr>(S))
    return NE->isArray() ? AF_CXXNewArray : AF_CXXNew;

  if (const CXXDeleteExpr *DE = dyn_cast<CXXDeleteExpr>(S))
    return DE->isArrayForm() ? AF_CXXNewArray : AF_CXXNew;

  if (isa<ObjCMessageExpr>(S))
    return AF_Malloc;

  return AF_None;
}

bool MallocChecker::printAllocDeallocName(raw_ostream &os, CheckerContext &C,
                                          const Expr *E) const {
  if (const CallExpr *CE = dyn_cast<CallExpr>(E)) {
    // FIXME: This doesn't handle indirect calls.
    const FunctionDecl *FD = CE->getDirectCallee();
    if (!FD)
      return false;

    os << *FD;
    if (!FD->isOverloadedOperator())
      os << "()";
    return true;
  }

  if (const ObjCMessageExpr *Msg = dyn_cast<ObjCMessageExpr>(E)) {
    if (Msg->isInstanceMessage())
      os << "-";
    else
      os << "+";
    Msg->getSelector().print(os);
    return true;
  }

  if (const CXXNewExpr *NE = dyn_cast<CXXNewExpr>(E)) {
    os << "'"
       << getOperatorSpelling(NE->getOperatorNew()->getOverloadedOperator())
       << "'";
    return true;
  }

  if (const CXXDeleteExpr *DE = dyn_cast<CXXDeleteExpr>(E)) {
    os << "'"
       << getOperatorSpelling(DE->getOperatorDelete()->getOverloadedOperator())
       << "'";
    return true;
  }

  return false;
}

void MallocChecker::printExpectedAllocName(raw_ostream &os, CheckerContext &C,
                                           const Expr *E) const {
  AllocationFamily Family = getAllocationFamily(C, E);

  switch(Family) {
    case AF_Malloc: os << "malloc()"; return;
    case AF_CXXNew: os << "'new'"; return;
    case AF_CXXNewArray: os << "'new[]'"; return;
    case AF_IfNameIndex: os << "'if_nameindex()'"; return;
    case AF_InnerBuffer: os << "container-specific allocator"; return;
    case AF_Alloca:
    case AF_None: llvm_unreachable("not a deallocation expression");
  }
}

void MallocChecker::printExpectedDeallocName(raw_ostream &os,
                                             AllocationFamily Family) const {
  switch(Family) {
    case AF_Malloc: os << "free()"; return;
    case AF_CXXNew: os << "'delete'"; return;
    case AF_CXXNewArray: os << "'delete[]'"; return;
    case AF_IfNameIndex: os << "'if_freenameindex()'"; return;
    case AF_InnerBuffer: os << "container-specific deallocator"; return;
    case AF_Alloca:
    case AF_None: llvm_unreachable("suspicious argument");
  }
}

ProgramStateRef MallocChecker::FreeMemAux(CheckerContext &C,
                                          const Expr *ArgExpr,
                                          const Expr *ParentExpr,
                                          ProgramStateRef State,
                                          bool Hold,
                                          bool &ReleasedAllocated,
                                          bool ReturnsNullOnFailure) const {

  if (!State)
    return nullptr;

  SVal ArgVal = C.getSVal(ArgExpr);
  if (!ArgVal.getAs<DefinedOrUnknownSVal>())
    return nullptr;
  DefinedOrUnknownSVal location = ArgVal.castAs<DefinedOrUnknownSVal>();

  // Check for null dereferences.
  if (!location.getAs<Loc>())
    return nullptr;

  // The explicit NULL case, no operation is performed.
  ProgramStateRef notNullState, nullState;
  std::tie(notNullState, nullState) = State->assume(location);
  if (nullState && !notNullState)
    return nullptr;

  // Unknown values could easily be okay
  // Undefined values are handled elsewhere
  if (ArgVal.isUnknownOrUndef())
    return nullptr;

  const MemRegion *R = ArgVal.getAsRegion();

  // Nonlocs can't be freed, of course.
  // Non-region locations (labels and fixed addresses) also shouldn't be freed.
  if (!R) {
    ReportBadFree(C, ArgVal, ArgExpr->getSourceRange(), ParentExpr);
    return nullptr;
  }

  R = R->StripCasts();

  // Blocks might show up as heap data, but should not be free()d
  if (isa<BlockDataRegion>(R)) {
    ReportBadFree(C, ArgVal, ArgExpr->getSourceRange(), ParentExpr);
    return nullptr;
  }

  const MemSpaceRegion *MS = R->getMemorySpace();

  // Parameters, locals, statics, globals, and memory returned by
  // __builtin_alloca() shouldn't be freed.
  if (!(isa<UnknownSpaceRegion>(MS) || isa<HeapSpaceRegion>(MS))) {
    // FIXME: at the time this code was written, malloc() regions were
    // represented by conjured symbols, which are all in UnknownSpaceRegion.
    // This means that there isn't actually anything from HeapSpaceRegion
    // that should be freed, even though we allow it here.
    // Of course, free() can work on memory allocated outside the current
    // function, so UnknownSpaceRegion is always a possibility.
    // False negatives are better than false positives.

    if (isa<AllocaRegion>(R))
      ReportFreeAlloca(C, ArgVal, ArgExpr->getSourceRange());
    else
      ReportBadFree(C, ArgVal, ArgExpr->getSourceRange(), ParentExpr);

    return nullptr;
  }

  const SymbolicRegion *SrBase = dyn_cast<SymbolicRegion>(R->getBaseRegion());
  // Various cases could lead to non-symbol values here.
  // For now, ignore them.
  if (!SrBase)
    return nullptr;

  SymbolRef SymBase = SrBase->getSymbol();
  const RefState *RsBase = State->get<RegionState>(SymBase);
  SymbolRef PreviousRetStatusSymbol = nullptr;

  if (RsBase) {

    // Memory returned by alloca() shouldn't be freed.
    if (RsBase->getAllocationFamily() == AF_Alloca) {
      ReportFreeAlloca(C, ArgVal, ArgExpr->getSourceRange());
      return nullptr;
    }

    // Check for double free first.
    if ((RsBase->isReleased() || RsBase->isRelinquished()) &&
        !didPreviousFreeFail(State, SymBase, PreviousRetStatusSymbol)) {
      ReportDoubleFree(C, ParentExpr->getSourceRange(), RsBase->isReleased(),
                       SymBase, PreviousRetStatusSymbol);
      return nullptr;

    // If the pointer is allocated or escaped, but we are now trying to free it,
    // check that the call to free is proper.
    } else if (RsBase->isAllocated() || RsBase->isAllocatedOfSizeZero() ||
               RsBase->isEscaped()) {

      // Check if an expected deallocation function matches the real one.
      bool DeallocMatchesAlloc =
        RsBase->getAllocationFamily() == getAllocationFamily(C, ParentExpr);
      if (!DeallocMatchesAlloc) {
        ReportMismatchedDealloc(C, ArgExpr->getSourceRange(),
                                ParentExpr, RsBase, SymBase, Hold);
        return nullptr;
      }

      // Check if the memory location being freed is the actual location
      // allocated, or an offset.
      RegionOffset Offset = R->getAsOffset();
      if (Offset.isValid() &&
          !Offset.hasSymbolicOffset() &&
          Offset.getOffset() != 0) {
        const Expr *AllocExpr = cast<Expr>(RsBase->getStmt());
        ReportOffsetFree(C, ArgVal, ArgExpr->getSourceRange(), ParentExpr,
                         AllocExpr);
        return nullptr;
      }
    }
  }

  if (SymBase->getType()->isFunctionPointerType()) {
    ReportFunctionPointerFree(C, ArgVal, ArgExpr->getSourceRange(), ParentExpr);
    return nullptr;
  }

  ReleasedAllocated = (RsBase != nullptr) && (RsBase->isAllocated() ||
                                              RsBase->isAllocatedOfSizeZero());

  // Clean out the info on previous call to free return info.
  State = State->remove<FreeReturnValue>(SymBase);

  // Keep track of the return value. If it is NULL, we will know that free
  // failed.
  if (ReturnsNullOnFailure) {
    SVal RetVal = C.getSVal(ParentExpr);
    SymbolRef RetStatusSymbol = RetVal.getAsSymbol();
    if (RetStatusSymbol) {
      C.getSymbolManager().addSymbolDependency(SymBase, RetStatusSymbol);
      State = State->set<FreeReturnValue>(SymBase, RetStatusSymbol);
    }
  }

  AllocationFamily Family = RsBase ? RsBase->getAllocationFamily()
                                   : getAllocationFamily(C, ParentExpr);
  // Normal free.
  if (Hold)
    return State->set<RegionState>(SymBase,
                                   RefState::getRelinquished(Family,
                                                             ParentExpr));

  return State->set<RegionState>(SymBase,
                                 RefState::getReleased(Family, ParentExpr));
}

Optional<MallocChecker::CheckKind>
MallocChecker::getCheckIfTracked(AllocationFamily Family,
                                 bool IsALeakCheck) const {
  switch (Family) {
  case AF_Malloc:
  case AF_Alloca:
  case AF_IfNameIndex: {
    if (ChecksEnabled[CK_MallocChecker])
      return CK_MallocChecker;
    return None;
  }
  case AF_CXXNew:
  case AF_CXXNewArray: {
    if (IsALeakCheck) {
      if (ChecksEnabled[CK_NewDeleteLeaksChecker])
        return CK_NewDeleteLeaksChecker;
    }
    else {
      if (ChecksEnabled[CK_NewDeleteChecker])
        return CK_NewDeleteChecker;
    }
    return None;
  }
  case AF_InnerBuffer: {
    if (ChecksEnabled[CK_InnerPointerChecker])
      return CK_InnerPointerChecker;
    return None;
  }
  case AF_None: {
    llvm_unreachable("no family");
  }
  }
  llvm_unreachable("unhandled family");
}

Optional<MallocChecker::CheckKind>
MallocChecker::getCheckIfTracked(CheckerContext &C,
                                 const Stmt *AllocDeallocStmt,
                                 bool IsALeakCheck) const {
  return getCheckIfTracked(getAllocationFamily(C, AllocDeallocStmt),
                           IsALeakCheck);
}

Optional<MallocChecker::CheckKind>
MallocChecker::getCheckIfTracked(CheckerContext &C, SymbolRef Sym,
                                 bool IsALeakCheck) const {
  if (C.getState()->contains<ReallocSizeZeroSymbols>(Sym))
    return CK_MallocChecker;

  const RefState *RS = C.getState()->get<RegionState>(Sym);
  assert(RS);
  return getCheckIfTracked(RS->getAllocationFamily(), IsALeakCheck);
}

bool MallocChecker::SummarizeValue(raw_ostream &os, SVal V) {
  if (Optional<nonloc::ConcreteInt> IntVal = V.getAs<nonloc::ConcreteInt>())
    os << "an integer (" << IntVal->getValue() << ")";
  else if (Optional<loc::ConcreteInt> ConstAddr = V.getAs<loc::ConcreteInt>())
    os << "a constant address (" << ConstAddr->getValue() << ")";
  else if (Optional<loc::GotoLabel> Label = V.getAs<loc::GotoLabel>())
    os << "the address of the label '" << Label->getLabel()->getName() << "'";
  else
    return false;

  return true;
}

bool MallocChecker::SummarizeRegion(raw_ostream &os,
                                    const MemRegion *MR) {
  switch (MR->getKind()) {
  case MemRegion::FunctionCodeRegionKind: {
    const NamedDecl *FD = cast<FunctionCodeRegion>(MR)->getDecl();
    if (FD)
      os << "the address of the function '" << *FD << '\'';
    else
      os << "the address of a function";
    return true;
  }
  case MemRegion::BlockCodeRegionKind:
    os << "block text";
    return true;
  case MemRegion::BlockDataRegionKind:
    // FIXME: where the block came from?
    os << "a block";
    return true;
  default: {
    const MemSpaceRegion *MS = MR->getMemorySpace();

    if (isa<StackLocalsSpaceRegion>(MS)) {
      const VarRegion *VR = dyn_cast<VarRegion>(MR);
      const VarDecl *VD;
      if (VR)
        VD = VR->getDecl();
      else
        VD = nullptr;

      if (VD)
        os << "the address of the local variable '" << VD->getName() << "'";
      else
        os << "the address of a local stack variable";
      return true;
    }

    if (isa<StackArgumentsSpaceRegion>(MS)) {
      const VarRegion *VR = dyn_cast<VarRegion>(MR);
      const VarDecl *VD;
      if (VR)
        VD = VR->getDecl();
      else
        VD = nullptr;

      if (VD)
        os << "the address of the parameter '" << VD->getName() << "'";
      else
        os << "the address of a parameter";
      return true;
    }

    if (isa<GlobalsSpaceRegion>(MS)) {
      const VarRegion *VR = dyn_cast<VarRegion>(MR);
      const VarDecl *VD;
      if (VR)
        VD = VR->getDecl();
      else
        VD = nullptr;

      if (VD) {
        if (VD->isStaticLocal())
          os << "the address of the static variable '" << VD->getName() << "'";
        else
          os << "the address of the global variable '" << VD->getName() << "'";
      } else
        os << "the address of a global variable";
      return true;
    }

    return false;
  }
  }
}

void MallocChecker::ReportBadFree(CheckerContext &C, SVal ArgVal,
                                  SourceRange Range,
                                  const Expr *DeallocExpr) const {

  if (!ChecksEnabled[CK_MallocChecker] &&
      !ChecksEnabled[CK_NewDeleteChecker])
    return;

  Optional<MallocChecker::CheckKind> CheckKind =
      getCheckIfTracked(C, DeallocExpr);
  if (!CheckKind.hasValue())
    return;

  if (ExplodedNode *N = C.generateErrorNode()) {
    if (!BT_BadFree[*CheckKind])
      BT_BadFree[*CheckKind].reset(new BugType(
          CheckNames[*CheckKind], "Bad free", categories::MemoryError));

    SmallString<100> buf;
    llvm::raw_svector_ostream os(buf);

    const MemRegion *MR = ArgVal.getAsRegion();
    while (const ElementRegion *ER = dyn_cast_or_null<ElementRegion>(MR))
      MR = ER->getSuperRegion();

    os << "Argument to ";
    if (!printAllocDeallocName(os, C, DeallocExpr))
      os << "deallocator";

    os << " is ";
    bool Summarized = MR ? SummarizeRegion(os, MR)
                         : SummarizeValue(os, ArgVal);
    if (Summarized)
      os << ", which is not memory allocated by ";
    else
      os << "not memory allocated by ";

    printExpectedAllocName(os, C, DeallocExpr);

    auto R = llvm::make_unique<BugReport>(*BT_BadFree[*CheckKind], os.str(), N);
    R->markInteresting(MR);
    R->addRange(Range);
    C.emitReport(std::move(R));
  }
}

void MallocChecker::ReportFreeAlloca(CheckerContext &C, SVal ArgVal,
                                     SourceRange Range) const {

  Optional<MallocChecker::CheckKind> CheckKind;

  if (ChecksEnabled[CK_MallocChecker])
    CheckKind = CK_MallocChecker;
  else if (ChecksEnabled[CK_MismatchedDeallocatorChecker])
    CheckKind = CK_MismatchedDeallocatorChecker;
  else
    return;

  if (ExplodedNode *N = C.generateErrorNode()) {
    if (!BT_FreeAlloca[*CheckKind])
      BT_FreeAlloca[*CheckKind].reset(new BugType(
          CheckNames[*CheckKind], "Free alloca()", categories::MemoryError));

    auto R = llvm::make_unique<BugReport>(
        *BT_FreeAlloca[*CheckKind],
        "Memory allocated by alloca() should not be deallocated", N);
    R->markInteresting(ArgVal.getAsRegion());
    R->addRange(Range);
    C.emitReport(std::move(R));
  }
}

void MallocChecker::ReportMismatchedDealloc(CheckerContext &C,
                                            SourceRange Range,
                                            const Expr *DeallocExpr,
                                            const RefState *RS,
                                            SymbolRef Sym,
                                            bool OwnershipTransferred) const {

  if (!ChecksEnabled[CK_MismatchedDeallocatorChecker])
    return;

  if (ExplodedNode *N = C.generateErrorNode()) {
    if (!BT_MismatchedDealloc)
      BT_MismatchedDealloc.reset(
          new BugType(CheckNames[CK_MismatchedDeallocatorChecker],
                      "Bad deallocator", categories::MemoryError));

    SmallString<100> buf;
    llvm::raw_svector_ostream os(buf);

    const Expr *AllocExpr = cast<Expr>(RS->getStmt());
    SmallString<20> AllocBuf;
    llvm::raw_svector_ostream AllocOs(AllocBuf);
    SmallString<20> DeallocBuf;
    llvm::raw_svector_ostream DeallocOs(DeallocBuf);

    if (OwnershipTransferred) {
      if (printAllocDeallocName(DeallocOs, C, DeallocExpr))
        os << DeallocOs.str() << " cannot";
      else
        os << "Cannot";

      os << " take ownership of memory";

      if (printAllocDeallocName(AllocOs, C, AllocExpr))
        os << " allocated by " << AllocOs.str();
    } else {
      os << "Memory";
      if (printAllocDeallocName(AllocOs, C, AllocExpr))
        os << " allocated by " << AllocOs.str();

      os << " should be deallocated by ";
        printExpectedDeallocName(os, RS->getAllocationFamily());

      if (printAllocDeallocName(DeallocOs, C, DeallocExpr))
        os << ", not " << DeallocOs.str();
    }

    auto R = llvm::make_unique<BugReport>(*BT_MismatchedDealloc, os.str(), N);
    R->markInteresting(Sym);
    R->addRange(Range);
    R->addVisitor(llvm::make_unique<MallocBugVisitor>(Sym));
    C.emitReport(std::move(R));
  }
}

void MallocChecker::ReportOffsetFree(CheckerContext &C, SVal ArgVal,
                                     SourceRange Range, const Expr *DeallocExpr,
                                     const Expr *AllocExpr) const {


  if (!ChecksEnabled[CK_MallocChecker] &&
      !ChecksEnabled[CK_NewDeleteChecker])
    return;

  Optional<MallocChecker::CheckKind> CheckKind =
      getCheckIfTracked(C, AllocExpr);
  if (!CheckKind.hasValue())
    return;

  ExplodedNode *N = C.generateErrorNode();
  if (!N)
    return;

  if (!BT_OffsetFree[*CheckKind])
    BT_OffsetFree[*CheckKind].reset(new BugType(
        CheckNames[*CheckKind], "Offset free", categories::MemoryError));

  SmallString<100> buf;
  llvm::raw_svector_ostream os(buf);
  SmallString<20> AllocNameBuf;
  llvm::raw_svector_ostream AllocNameOs(AllocNameBuf);

  const MemRegion *MR = ArgVal.getAsRegion();
  assert(MR && "Only MemRegion based symbols can have offset free errors");

  RegionOffset Offset = MR->getAsOffset();
  assert((Offset.isValid() &&
          !Offset.hasSymbolicOffset() &&
          Offset.getOffset() != 0) &&
         "Only symbols with a valid offset can have offset free errors");

  int offsetBytes = Offset.getOffset() / C.getASTContext().getCharWidth();

  os << "Argument to ";
  if (!printAllocDeallocName(os, C, DeallocExpr))
    os << "deallocator";
  os << " is offset by "
     << offsetBytes
     << " "
     << ((abs(offsetBytes) > 1) ? "bytes" : "byte")
     << " from the start of ";
  if (AllocExpr && printAllocDeallocName(AllocNameOs, C, AllocExpr))
    os << "memory allocated by " << AllocNameOs.str();
  else
    os << "allocated memory";

  auto R = llvm::make_unique<BugReport>(*BT_OffsetFree[*CheckKind], os.str(), N);
  R->markInteresting(MR->getBaseRegion());
  R->addRange(Range);
  C.emitReport(std::move(R));
}

void MallocChecker::ReportUseAfterFree(CheckerContext &C, SourceRange Range,
                                       SymbolRef Sym) const {

  if (!ChecksEnabled[CK_MallocChecker] &&
      !ChecksEnabled[CK_NewDeleteChecker] &&
      !ChecksEnabled[CK_InnerPointerChecker])
    return;

  Optional<MallocChecker::CheckKind> CheckKind = getCheckIfTracked(C, Sym);
  if (!CheckKind.hasValue())
    return;

  if (ExplodedNode *N = C.generateErrorNode()) {
    if (!BT_UseFree[*CheckKind])
      BT_UseFree[*CheckKind].reset(new BugType(
          CheckNames[*CheckKind], "Use-after-free", categories::MemoryError));

    AllocationFamily AF =
        C.getState()->get<RegionState>(Sym)->getAllocationFamily();

    auto R = llvm::make_unique<BugReport>(*BT_UseFree[*CheckKind],
        AF == AF_InnerBuffer
              ? "Inner pointer of container used after re/deallocation"
              : "Use of memory after it is freed",
        N);

    R->markInteresting(Sym);
    R->addRange(Range);
    R->addVisitor(llvm::make_unique<MallocBugVisitor>(Sym));

    if (AF == AF_InnerBuffer)
      R->addVisitor(allocation_state::getInnerPointerBRVisitor(Sym));

    C.emitReport(std::move(R));
  }
}

void MallocChecker::ReportDoubleFree(CheckerContext &C, SourceRange Range,
                                     bool Released, SymbolRef Sym,
                                     SymbolRef PrevSym) const {

  if (!ChecksEnabled[CK_MallocChecker] &&
      !ChecksEnabled[CK_NewDeleteChecker])
    return;

  Optional<MallocChecker::CheckKind> CheckKind = getCheckIfTracked(C, Sym);
  if (!CheckKind.hasValue())
    return;

  if (ExplodedNode *N = C.generateErrorNode()) {
    if (!BT_DoubleFree[*CheckKind])
      BT_DoubleFree[*CheckKind].reset(new BugType(
          CheckNames[*CheckKind], "Double free", categories::MemoryError));

    auto R = llvm::make_unique<BugReport>(
        *BT_DoubleFree[*CheckKind],
        (Released ? "Attempt to free released memory"
                  : "Attempt to free non-owned memory"),
        N);
    R->addRange(Range);
    R->markInteresting(Sym);
    if (PrevSym)
      R->markInteresting(PrevSym);
    R->addVisitor(llvm::make_unique<MallocBugVisitor>(Sym));
    C.emitReport(std::move(R));
  }
}

void MallocChecker::ReportDoubleDelete(CheckerContext &C, SymbolRef Sym) const {

  if (!ChecksEnabled[CK_NewDeleteChecker])
    return;

  Optional<MallocChecker::CheckKind> CheckKind = getCheckIfTracked(C, Sym);
  if (!CheckKind.hasValue())
    return;

  if (ExplodedNode *N = C.generateErrorNode()) {
    if (!BT_DoubleDelete)
      BT_DoubleDelete.reset(new BugType(CheckNames[CK_NewDeleteChecker],
                                        "Double delete",
                                        categories::MemoryError));

    auto R = llvm::make_unique<BugReport>(
        *BT_DoubleDelete, "Attempt to delete released memory", N);

    R->markInteresting(Sym);
    R->addVisitor(llvm::make_unique<MallocBugVisitor>(Sym));
    C.emitReport(std::move(R));
  }
}

void MallocChecker::ReportUseZeroAllocated(CheckerContext &C,
                                           SourceRange Range,
                                           SymbolRef Sym) const {

  if (!ChecksEnabled[CK_MallocChecker] &&
      !ChecksEnabled[CK_NewDeleteChecker])
    return;

  Optional<MallocChecker::CheckKind> CheckKind = getCheckIfTracked(C, Sym);

  if (!CheckKind.hasValue())
    return;

  if (ExplodedNode *N = C.generateErrorNode()) {
    if (!BT_UseZerroAllocated[*CheckKind])
      BT_UseZerroAllocated[*CheckKind].reset(
          new BugType(CheckNames[*CheckKind], "Use of zero allocated",
                      categories::MemoryError));

    auto R = llvm::make_unique<BugReport>(*BT_UseZerroAllocated[*CheckKind],
                                         "Use of zero-allocated memory", N);

    R->addRange(Range);
    if (Sym) {
      R->markInteresting(Sym);
      R->addVisitor(llvm::make_unique<MallocBugVisitor>(Sym));
    }
    C.emitReport(std::move(R));
  }
}

void MallocChecker::ReportFunctionPointerFree(CheckerContext &C, SVal ArgVal,
                                              SourceRange Range,
                                              const Expr *FreeExpr) const {
  if (!ChecksEnabled[CK_MallocChecker])
    return;

  Optional<MallocChecker::CheckKind> CheckKind = getCheckIfTracked(C, FreeExpr);
  if (!CheckKind.hasValue())
    return;

  if (ExplodedNode *N = C.generateErrorNode()) {
    if (!BT_BadFree[*CheckKind])
      BT_BadFree[*CheckKind].reset(new BugType(
          CheckNames[*CheckKind], "Bad free", categories::MemoryError));

    SmallString<100> Buf;
    llvm::raw_svector_ostream Os(Buf);

    const MemRegion *MR = ArgVal.getAsRegion();
    while (const ElementRegion *ER = dyn_cast_or_null<ElementRegion>(MR))
      MR = ER->getSuperRegion();

    Os << "Argument to ";
    if (!printAllocDeallocName(Os, C, FreeExpr))
      Os << "deallocator";

    Os << " is a function pointer";

    auto R = llvm::make_unique<BugReport>(*BT_BadFree[*CheckKind], Os.str(), N);
    R->markInteresting(MR);
    R->addRange(Range);
    C.emitReport(std::move(R));
  }
}

ProgramStateRef MallocChecker::ReallocMemAux(CheckerContext &C,
                                             const CallExpr *CE,
                                             bool FreesOnFail,
                                             ProgramStateRef State,
                                             bool SuffixWithN) const {
  if (!State)
    return nullptr;

  if (SuffixWithN && CE->getNumArgs() < 3)
    return nullptr;
  else if (CE->getNumArgs() < 2)
    return nullptr;

  const Expr *arg0Expr = CE->getArg(0);
  SVal Arg0Val = C.getSVal(arg0Expr);
  if (!Arg0Val.getAs<DefinedOrUnknownSVal>())
    return nullptr;
  DefinedOrUnknownSVal arg0Val = Arg0Val.castAs<DefinedOrUnknownSVal>();

  SValBuilder &svalBuilder = C.getSValBuilder();

  DefinedOrUnknownSVal PtrEQ =
    svalBuilder.evalEQ(State, arg0Val, svalBuilder.makeNull());

  // Get the size argument.
  const Expr *Arg1 = CE->getArg(1);

  // Get the value of the size argument.
  SVal TotalSize = C.getSVal(Arg1);
  if (SuffixWithN)
    TotalSize = evalMulForBufferSize(C, Arg1, CE->getArg(2));
  if (!TotalSize.getAs<DefinedOrUnknownSVal>())
    return nullptr;

  // Compare the size argument to 0.
  DefinedOrUnknownSVal SizeZero =
    svalBuilder.evalEQ(State, TotalSize.castAs<DefinedOrUnknownSVal>(),
                       svalBuilder.makeIntValWithPtrWidth(0, false));

  ProgramStateRef StatePtrIsNull, StatePtrNotNull;
  std::tie(StatePtrIsNull, StatePtrNotNull) = State->assume(PtrEQ);
  ProgramStateRef StateSizeIsZero, StateSizeNotZero;
  std::tie(StateSizeIsZero, StateSizeNotZero) = State->assume(SizeZero);
  // We only assume exceptional states if they are definitely true; if the
  // state is under-constrained, assume regular realloc behavior.
  bool PrtIsNull = StatePtrIsNull && !StatePtrNotNull;
  bool SizeIsZero = StateSizeIsZero && !StateSizeNotZero;

  // If the ptr is NULL and the size is not 0, the call is equivalent to
  // malloc(size).
  if (PrtIsNull && !SizeIsZero) {
    ProgramStateRef stateMalloc = MallocMemAux(C, CE, TotalSize,
                                               UndefinedVal(), StatePtrIsNull);
    return stateMalloc;
  }

  if (PrtIsNull && SizeIsZero)
    return State;

  // Get the from and to pointer symbols as in toPtr = realloc(fromPtr, size).
  assert(!PrtIsNull);
  SymbolRef FromPtr = arg0Val.getAsSymbol();
  SVal RetVal = C.getSVal(CE);
  SymbolRef ToPtr = RetVal.getAsSymbol();
  if (!FromPtr || !ToPtr)
    return nullptr;

  bool ReleasedAllocated = false;

  // If the size is 0, free the memory.
  if (SizeIsZero)
    if (ProgramStateRef stateFree = FreeMemAux(C, CE, StateSizeIsZero, 0,
                                               false, ReleasedAllocated)){
      // The semantics of the return value are:
      // If size was equal to 0, either NULL or a pointer suitable to be passed
      // to free() is returned. We just free the input pointer and do not add
      // any constrains on the output pointer.
      return stateFree;
    }

  // Default behavior.
  if (ProgramStateRef stateFree =
        FreeMemAux(C, CE, State, 0, false, ReleasedAllocated)) {

    ProgramStateRef stateRealloc = MallocMemAux(C, CE, TotalSize,
                                                UnknownVal(), stateFree);
    if (!stateRealloc)
      return nullptr;

    ReallocPairKind Kind = RPToBeFreedAfterFailure;
    if (FreesOnFail)
      Kind = RPIsFreeOnFailure;
    else if (!ReleasedAllocated)
      Kind = RPDoNotTrackAfterFailure;

    // Record the info about the reallocated symbol so that we could properly
    // process failed reallocation.
    stateRealloc = stateRealloc->set<ReallocPairs>(ToPtr,
                                                   ReallocPair(FromPtr, Kind));
    // The reallocated symbol should stay alive for as long as the new symbol.
    C.getSymbolManager().addSymbolDependency(ToPtr, FromPtr);
    return stateRealloc;
  }
  return nullptr;
}

ProgramStateRef MallocChecker::CallocMem(CheckerContext &C, const CallExpr *CE,
                                         ProgramStateRef State) {
  if (!State)
    return nullptr;

  if (CE->getNumArgs() < 2)
    return nullptr;

  SValBuilder &svalBuilder = C.getSValBuilder();
  SVal zeroVal = svalBuilder.makeZeroVal(svalBuilder.getContext().CharTy);
  SVal TotalSize = evalMulForBufferSize(C, CE->getArg(0), CE->getArg(1));

  return MallocMemAux(C, CE, TotalSize, zeroVal, State);
}

LeakInfo
MallocChecker::getAllocationSite(const ExplodedNode *N, SymbolRef Sym,
                                 CheckerContext &C) const {
  const LocationContext *LeakContext = N->getLocationContext();
  // Walk the ExplodedGraph backwards and find the first node that referred to
  // the tracked symbol.
  const ExplodedNode *AllocNode = N;
  const MemRegion *ReferenceRegion = nullptr;

  while (N) {
    ProgramStateRef State = N->getState();
    if (!State->get<RegionState>(Sym))
      break;

    // Find the most recent expression bound to the symbol in the current
    // context.
      if (!ReferenceRegion) {
        if (const MemRegion *MR = C.getLocationRegionIfPostStore(N)) {
          SVal Val = State->getSVal(MR);
          if (Val.getAsLocSymbol() == Sym) {
            const VarRegion* VR = MR->getBaseRegion()->getAs<VarRegion>();
            // Do not show local variables belonging to a function other than
            // where the error is reported.
            if (!VR ||
                (VR->getStackFrame() == LeakContext->getStackFrame()))
              ReferenceRegion = MR;
          }
        }
      }

    // Allocation node, is the last node in the current or parent context in
    // which the symbol was tracked.
    const LocationContext *NContext = N->getLocationContext();
    if (NContext == LeakContext ||
        NContext->isParentOf(LeakContext))
      AllocNode = N;
    N = N->pred_empty() ? nullptr : *(N->pred_begin());
  }

  return LeakInfo(AllocNode, ReferenceRegion);
}

void MallocChecker::reportLeak(SymbolRef Sym, ExplodedNode *N,
                               CheckerContext &C) const {

  if (!ChecksEnabled[CK_MallocChecker] &&
      !ChecksEnabled[CK_NewDeleteLeaksChecker])
    return;

  const RefState *RS = C.getState()->get<RegionState>(Sym);
  assert(RS && "cannot leak an untracked symbol");
  AllocationFamily Family = RS->getAllocationFamily();

  if (Family == AF_Alloca)
    return;

  Optional<MallocChecker::CheckKind>
      CheckKind = getCheckIfTracked(Family, true);

  if (!CheckKind.hasValue())
    return;

  assert(N);
  if (!BT_Leak[*CheckKind]) {
    BT_Leak[*CheckKind].reset(new BugType(CheckNames[*CheckKind], "Memory leak",
                                          categories::MemoryError));
    // Leaks should not be reported if they are post-dominated by a sink:
    // (1) Sinks are higher importance bugs.
    // (2) NoReturnFunctionChecker uses sink nodes to represent paths ending
    //     with __noreturn functions such as assert() or exit(). We choose not
    //     to report leaks on such paths.
    BT_Leak[*CheckKind]->setSuppressOnSink(true);
  }

  // Most bug reports are cached at the location where they occurred.
  // With leaks, we want to unique them by the location where they were
  // allocated, and only report a single path.
  PathDiagnosticLocation LocUsedForUniqueing;
  const ExplodedNode *AllocNode = nullptr;
  const MemRegion *Region = nullptr;
  std::tie(AllocNode, Region) = getAllocationSite(N, Sym, C);

  const Stmt *AllocationStmt = PathDiagnosticLocation::getStmt(AllocNode);
  if (AllocationStmt)
    LocUsedForUniqueing = PathDiagnosticLocation::createBegin(AllocationStmt,
                                              C.getSourceManager(),
                                              AllocNode->getLocationContext());

  SmallString<200> buf;
  llvm::raw_svector_ostream os(buf);
  if (Region && Region->canPrintPretty()) {
    os << "Potential leak of memory pointed to by ";
    Region->printPretty(os);
  } else {
    os << "Potential memory leak";
  }

  auto R = llvm::make_unique<BugReport>(
      *BT_Leak[*CheckKind], os.str(), N, LocUsedForUniqueing,
      AllocNode->getLocationContext()->getDecl());
  R->markInteresting(Sym);
  R->addVisitor(llvm::make_unique<MallocBugVisitor>(Sym, true));
  C.emitReport(std::move(R));
}

void MallocChecker::checkDeadSymbols(SymbolReaper &SymReaper,
                                     CheckerContext &C) const
{
  ProgramStateRef state = C.getState();
  RegionStateTy OldRS = state->get<RegionState>();
  RegionStateTy::Factory &F = state->get_context<RegionState>();

  RegionStateTy RS = OldRS;
  SmallVector<SymbolRef, 2> Errors;
  for (RegionStateTy::iterator I = RS.begin(), E = RS.end(); I != E; ++I) {
    if (SymReaper.isDead(I->first)) {
      if (I->second.isAllocated() || I->second.isAllocatedOfSizeZero())
        Errors.push_back(I->first);
      // Remove the dead symbol from the map.
      RS = F.remove(RS, I->first);
    }
  }

  if (RS == OldRS) {
    // We shouldn't have touched other maps yet.
    assert(state->get<ReallocPairs>() ==
           C.getState()->get<ReallocPairs>());
    assert(state->get<FreeReturnValue>() ==
           C.getState()->get<FreeReturnValue>());
    return;
  }

  // Cleanup the Realloc Pairs Map.
  ReallocPairsTy RP = state->get<ReallocPairs>();
  for (ReallocPairsTy::iterator I = RP.begin(), E = RP.end(); I != E; ++I) {
    if (SymReaper.isDead(I->first) ||
        SymReaper.isDead(I->second.ReallocatedSym)) {
      state = state->remove<ReallocPairs>(I->first);
    }
  }

  // Cleanup the FreeReturnValue Map.
  FreeReturnValueTy FR = state->get<FreeReturnValue>();
  for (FreeReturnValueTy::iterator I = FR.begin(), E = FR.end(); I != E; ++I) {
    if (SymReaper.isDead(I->first) ||
        SymReaper.isDead(I->second)) {
      state = state->remove<FreeReturnValue>(I->first);
    }
  }

  // Generate leak node.
  ExplodedNode *N = C.getPredecessor();
  if (!Errors.empty()) {
    static CheckerProgramPointTag Tag("MallocChecker", "DeadSymbolsLeak");
    N = C.generateNonFatalErrorNode(C.getState(), &Tag);
    if (N) {
      for (SmallVectorImpl<SymbolRef>::iterator
           I = Errors.begin(), E = Errors.end(); I != E; ++I) {
        reportLeak(*I, N, C);
      }
    }
  }

  C.addTransition(state->set<RegionState>(RS), N);
}

void MallocChecker::checkPreCall(const CallEvent &Call,
                                 CheckerContext &C) const {

  if (const CXXDestructorCall *DC = dyn_cast<CXXDestructorCall>(&Call)) {
    SymbolRef Sym = DC->getCXXThisVal().getAsSymbol();
    if (!Sym || checkDoubleDelete(Sym, C))
      return;
  }

  // We will check for double free in the post visit.
  if (const AnyFunctionCall *FC = dyn_cast<AnyFunctionCall>(&Call)) {
    const FunctionDecl *FD = FC->getDecl();
    if (!FD)
      return;

    ASTContext &Ctx = C.getASTContext();
    if (ChecksEnabled[CK_MallocChecker] &&
        (isCMemFunction(FD, Ctx, AF_Malloc, MemoryOperationKind::MOK_Free) ||
         isCMemFunction(FD, Ctx, AF_IfNameIndex,
                        MemoryOperationKind::MOK_Free)))
      return;
  }

  // Check if the callee of a method is deleted.
  if (const CXXInstanceCall *CC = dyn_cast<CXXInstanceCall>(&Call)) {
    SymbolRef Sym = CC->getCXXThisVal().getAsSymbol();
    if (!Sym || checkUseAfterFree(Sym, C, CC->getCXXThisExpr()))
      return;
  }

  // Check arguments for being used after free.
  for (unsigned I = 0, E = Call.getNumArgs(); I != E; ++I) {
    SVal ArgSVal = Call.getArgSVal(I);
    if (ArgSVal.getAs<Loc>()) {
      SymbolRef Sym = ArgSVal.getAsSymbol();
      if (!Sym)
        continue;
      if (checkUseAfterFree(Sym, C, Call.getArgExpr(I)))
        return;
    }
  }
}

void MallocChecker::checkPreStmt(const ReturnStmt *S,
                                 CheckerContext &C) const {
  checkEscapeOnReturn(S, C);
}

// In the CFG, automatic destructors come after the return statement.
// This callback checks for returning memory that is freed by automatic
// destructors, as those cannot be reached in checkPreStmt().
void MallocChecker::checkEndFunction(const ReturnStmt *S,
                                     CheckerContext &C) const {
  checkEscapeOnReturn(S, C);
}

void MallocChecker::checkEscapeOnReturn(const ReturnStmt *S,
                                        CheckerContext &C) const {
  if (!S)
    return;

  const Expr *E = S->getRetValue();
  if (!E)
    return;

  // Check if we are returning a symbol.
  ProgramStateRef State = C.getState();
  SVal RetVal = C.getSVal(E);
  SymbolRef Sym = RetVal.getAsSymbol();
  if (!Sym)
    // If we are returning a field of the allocated struct or an array element,
    // the callee could still free the memory.
    // TODO: This logic should be a part of generic symbol escape callback.
    if (const MemRegion *MR = RetVal.getAsRegion())
      if (isa<FieldRegion>(MR) || isa<ElementRegion>(MR))
        if (const SymbolicRegion *BMR =
              dyn_cast<SymbolicRegion>(MR->getBaseRegion()))
          Sym = BMR->getSymbol();

  // Check if we are returning freed memory.
  if (Sym)
    checkUseAfterFree(Sym, C, E);
}

// TODO: Blocks should be either inlined or should call invalidate regions
// upon invocation. After that's in place, special casing here will not be
// needed.
void MallocChecker::checkPostStmt(const BlockExpr *BE,
                                  CheckerContext &C) const {

  // Scan the BlockDecRefExprs for any object the retain count checker
  // may be tracking.
  if (!BE->getBlockDecl()->hasCaptures())
    return;

  ProgramStateRef state = C.getState();
  const BlockDataRegion *R =
    cast<BlockDataRegion>(C.getSVal(BE).getAsRegion());

  BlockDataRegion::referenced_vars_iterator I = R->referenced_vars_begin(),
                                            E = R->referenced_vars_end();

  if (I == E)
    return;

  SmallVector<const MemRegion*, 10> Regions;
  const LocationContext *LC = C.getLocationContext();
  MemRegionManager &MemMgr = C.getSValBuilder().getRegionManager();

  for ( ; I != E; ++I) {
    const VarRegion *VR = I.getCapturedRegion();
    if (VR->getSuperRegion() == R) {
      VR = MemMgr.getVarRegion(VR->getDecl(), LC);
    }
    Regions.push_back(VR);
  }

  state =
    state->scanReachableSymbols<StopTrackingCallback>(Regions).getState();
  C.addTransition(state);
}

bool MallocChecker::isReleased(SymbolRef Sym, CheckerContext &C) const {
  assert(Sym);
  const RefState *RS = C.getState()->get<RegionState>(Sym);
  return (RS && RS->isReleased());
}

bool MallocChecker::checkUseAfterFree(SymbolRef Sym, CheckerContext &C,
                                      const Stmt *S) const {

  if (isReleased(Sym, C)) {
    ReportUseAfterFree(C, S->getSourceRange(), Sym);
    return true;
  }

  return false;
}

void MallocChecker::checkUseZeroAllocated(SymbolRef Sym, CheckerContext &C,
                                          const Stmt *S) const {
  assert(Sym);

  if (const RefState *RS = C.getState()->get<RegionState>(Sym)) {
    if (RS->isAllocatedOfSizeZero())
      ReportUseZeroAllocated(C, RS->getStmt()->getSourceRange(), Sym);
  }
  else if (C.getState()->contains<ReallocSizeZeroSymbols>(Sym)) {
    ReportUseZeroAllocated(C, S->getSourceRange(), Sym);
  }
}

bool MallocChecker::checkDoubleDelete(SymbolRef Sym, CheckerContext &C) const {

  if (isReleased(Sym, C)) {
    ReportDoubleDelete(C, Sym);
    return true;
  }
  return false;
}

// Check if the location is a freed symbolic region.
void MallocChecker::checkLocation(SVal l, bool isLoad, const Stmt *S,
                                  CheckerContext &C) const {
  SymbolRef Sym = l.getLocSymbolInBase();
  if (Sym) {
    checkUseAfterFree(Sym, C, S);
    checkUseZeroAllocated(Sym, C, S);
  }
}

// If a symbolic region is assumed to NULL (or another constant), stop tracking
// it - assuming that allocation failed on this path.
ProgramStateRef MallocChecker::evalAssume(ProgramStateRef state,
                                              SVal Cond,
                                              bool Assumption) const {
  RegionStateTy RS = state->get<RegionState>();
  for (RegionStateTy::iterator I = RS.begin(), E = RS.end(); I != E; ++I) {
    // If the symbol is assumed to be NULL, remove it from consideration.
    ConstraintManager &CMgr = state->getConstraintManager();
    ConditionTruthVal AllocFailed = CMgr.isNull(state, I.getKey());
    if (AllocFailed.isConstrainedTrue())
      state = state->remove<RegionState>(I.getKey());
  }

  // Realloc returns 0 when reallocation fails, which means that we should
  // restore the state of the pointer being reallocated.
  ReallocPairsTy RP = state->get<ReallocPairs>();
  for (ReallocPairsTy::iterator I = RP.begin(), E = RP.end(); I != E; ++I) {
    // If the symbol is assumed to be NULL, remove it from consideration.
    ConstraintManager &CMgr = state->getConstraintManager();
    ConditionTruthVal AllocFailed = CMgr.isNull(state, I.getKey());
    if (!AllocFailed.isConstrainedTrue())
      continue;

    SymbolRef ReallocSym = I.getData().ReallocatedSym;
    if (const RefState *RS = state->get<RegionState>(ReallocSym)) {
      if (RS->isReleased()) {
        if (I.getData().Kind == RPToBeFreedAfterFailure)
          state = state->set<RegionState>(ReallocSym,
              RefState::getAllocated(RS->getAllocationFamily(), RS->getStmt()));
        else if (I.getData().Kind == RPDoNotTrackAfterFailure)
          state = state->remove<RegionState>(ReallocSym);
        else
          assert(I.getData().Kind == RPIsFreeOnFailure);
      }
    }
    state = state->remove<ReallocPairs>(I.getKey());
  }

  return state;
}

bool MallocChecker::mayFreeAnyEscapedMemoryOrIsModeledExplicitly(
                                              const CallEvent *Call,
                                              ProgramStateRef State,
                                              SymbolRef &EscapingSymbol) const {
  assert(Call);
  EscapingSymbol = nullptr;

  // For now, assume that any C++ or block call can free memory.
  // TODO: If we want to be more optimistic here, we'll need to make sure that
  // regions escape to C++ containers. They seem to do that even now, but for
  // mysterious reasons.
  if (!(isa<SimpleFunctionCall>(Call) || isa<ObjCMethodCall>(Call)))
    return true;

  // Check Objective-C messages by selector name.
  if (const ObjCMethodCall *Msg = dyn_cast<ObjCMethodCall>(Call)) {
    // If it's not a framework call, or if it takes a callback, assume it
    // can free memory.
    if (!Call->isInSystemHeader() || Call->argumentsMayEscape())
      return true;

    // If it's a method we know about, handle it explicitly post-call.
    // This should happen before the "freeWhenDone" check below.
    if (isKnownDeallocObjCMethodName(*Msg))
      return false;

    // If there's a "freeWhenDone" parameter, but the method isn't one we know
    // about, we can't be sure that the object will use free() to deallocate the
    // memory, so we can't model it explicitly. The best we can do is use it to
    // decide whether the pointer escapes.
    if (Optional<bool> FreeWhenDone = getFreeWhenDoneArg(*Msg))
      return *FreeWhenDone;

    // If the first selector piece ends with "NoCopy", and there is no
    // "freeWhenDone" parameter set to zero, we know ownership is being
    // transferred. Again, though, we can't be sure that the object will use
    // free() to deallocate the memory, so we can't model it explicitly.
    StringRef FirstSlot = Msg->getSelector().getNameForSlot(0);
    if (FirstSlot.endswith("NoCopy"))
      return true;

    // If the first selector starts with addPointer, insertPointer,
    // or replacePointer, assume we are dealing with NSPointerArray or similar.
    // This is similar to C++ containers (vector); we still might want to check
    // that the pointers get freed by following the container itself.
    if (FirstSlot.startswith("addPointer") ||
        FirstSlot.startswith("insertPointer") ||
        FirstSlot.startswith("replacePointer") ||
        FirstSlot.equals("valueWithPointer")) {
      return true;
    }

    // We should escape receiver on call to 'init'. This is especially relevant
    // to the receiver, as the corresponding symbol is usually not referenced
    // after the call.
    if (Msg->getMethodFamily() == OMF_init) {
      EscapingSymbol = Msg->getReceiverSVal().getAsSymbol();
      return true;
    }

    // Otherwise, assume that the method does not free memory.
    // Most framework methods do not free memory.
    return false;
  }

  // At this point the only thing left to handle is straight function calls.
  const FunctionDecl *FD = cast<SimpleFunctionCall>(Call)->getDecl();
  if (!FD)
    return true;

  ASTContext &ASTC = State->getStateManager().getContext();

  // If it's one of the allocation functions we can reason about, we model
  // its behavior explicitly.
  if (isMemFunction(FD, ASTC))
    return false;

  // If it's not a system call, assume it frees memory.
  if (!Call->isInSystemHeader())
    return true;

  // White list the system functions whose arguments escape.
  const IdentifierInfo *II = FD->getIdentifier();
  if (!II)
    return true;
  StringRef FName = II->getName();

  // White list the 'XXXNoCopy' CoreFoundation functions.
  // We specifically check these before
  if (FName.endswith("NoCopy")) {
    // Look for the deallocator argument. We know that the memory ownership
    // is not transferred only if the deallocator argument is
    // 'kCFAllocatorNull'.
    for (unsigned i = 1; i < Call->getNumArgs(); ++i) {
      const Expr *ArgE = Call->getArgExpr(i)->IgnoreParenCasts();
      if (const DeclRefExpr *DE = dyn_cast<DeclRefExpr>(ArgE)) {
        StringRef DeallocatorName = DE->getFoundDecl()->getName();
        if (DeallocatorName == "kCFAllocatorNull")
          return false;
      }
    }
    return true;
  }

  // Associating streams with malloced buffers. The pointer can escape if
  // 'closefn' is specified (and if that function does free memory),
  // but it will not if closefn is not specified.
  // Currently, we do not inspect the 'closefn' function (PR12101).
  if (FName == "funopen")
    if (Call->getNumArgs() >= 4 && Call->getArgSVal(4).isConstant(0))
      return false;

  // Do not warn on pointers passed to 'setbuf' when used with std streams,
  // these leaks might be intentional when setting the buffer for stdio.
  // http://stackoverflow.com/questions/2671151/who-frees-setvbuf-buffer
  if (FName == "setbuf" || FName =="setbuffer" ||
      FName == "setlinebuf" || FName == "setvbuf") {
    if (Call->getNumArgs() >= 1) {
      const Expr *ArgE = Call->getArgExpr(0)->IgnoreParenCasts();
      if (const DeclRefExpr *ArgDRE = dyn_cast<DeclRefExpr>(ArgE))
        if (const VarDecl *D = dyn_cast<VarDecl>(ArgDRE->getDecl()))
          if (D->getCanonicalDecl()->getName().find("std") != StringRef::npos)
            return true;
    }
  }

  // A bunch of other functions which either take ownership of a pointer or
  // wrap the result up in a struct or object, meaning it can be freed later.
  // (See RetainCountChecker.) Not all the parameters here are invalidated,
  // but the Malloc checker cannot differentiate between them. The right way
  // of doing this would be to implement a pointer escapes callback.
  if (FName == "CGBitmapContextCreate" ||
      FName == "CGBitmapContextCreateWithData" ||
      FName == "CVPixelBufferCreateWithBytes" ||
      FName == "CVPixelBufferCreateWithPlanarBytes" ||
      FName == "OSAtomicEnqueue") {
    return true;
  }

  if (FName == "postEvent" &&
      FD->getQualifiedNameAsString() == "QCoreApplication::postEvent") {
    return true;
  }

  if (FName == "postEvent" &&
      FD->getQualifiedNameAsString() == "QCoreApplication::postEvent") {
    return true;
  }

  if (FName == "connectImpl" &&
      FD->getQualifiedNameAsString() == "QObject::connectImpl") {
    return true;
  }

  // Handle cases where we know a buffer's /address/ can escape.
  // Note that the above checks handle some special cases where we know that
  // even though the address escapes, it's still our responsibility to free the
  // buffer.
  if (Call->argumentsMayEscape())
    return true;

  // Otherwise, assume that the function does not free memory.
  // Most system calls do not free the memory.
  return false;
}

static bool retTrue(const RefState *RS) {
  return true;
}

static bool checkIfNewOrNewArrayFamily(const RefState *RS) {
  return (RS->getAllocationFamily() == AF_CXXNewArray ||
          RS->getAllocationFamily() == AF_CXXNew);
}

ProgramStateRef MallocChecker::checkPointerEscape(ProgramStateRef State,
                                             const InvalidatedSymbols &Escaped,
                                             const CallEvent *Call,
                                             PointerEscapeKind Kind) const {
  return checkPointerEscapeAux(State, Escaped, Call, Kind, &retTrue);
}

ProgramStateRef MallocChecker::checkConstPointerEscape(ProgramStateRef State,
                                              const InvalidatedSymbols &Escaped,
                                              const CallEvent *Call,
                                              PointerEscapeKind Kind) const {
  return checkPointerEscapeAux(State, Escaped, Call, Kind,
                               &checkIfNewOrNewArrayFamily);
}

ProgramStateRef MallocChecker::checkPointerEscapeAux(ProgramStateRef State,
                                              const InvalidatedSymbols &Escaped,
                                              const CallEvent *Call,
                                              PointerEscapeKind Kind,
                                  bool(*CheckRefState)(const RefState*)) const {
  // If we know that the call does not free memory, or we want to process the
  // call later, keep tracking the top level arguments.
  SymbolRef EscapingSymbol = nullptr;
  if (Kind == PSK_DirectEscapeOnCall &&
      !mayFreeAnyEscapedMemoryOrIsModeledExplicitly(Call, State,
                                                    EscapingSymbol) &&
      !EscapingSymbol) {
    return State;
  }

  for (InvalidatedSymbols::const_iterator I = Escaped.begin(),
       E = Escaped.end();
       I != E; ++I) {
    SymbolRef sym = *I;

    if (EscapingSymbol && EscapingSymbol != sym)
      continue;

    if (const RefState *RS = State->get<RegionState>(sym)) {
      if ((RS->isAllocated() || RS->isAllocatedOfSizeZero()) &&
          CheckRefState(RS)) {
        State = State->remove<RegionState>(sym);
        State = State->set<RegionState>(sym, RefState::getEscaped(RS));
      }
    }
  }
  return State;
}

static SymbolRef findFailedReallocSymbol(ProgramStateRef currState,
                                         ProgramStateRef prevState) {
  ReallocPairsTy currMap = currState->get<ReallocPairs>();
  ReallocPairsTy prevMap = prevState->get<ReallocPairs>();

  for (ReallocPairsTy::iterator I = prevMap.begin(), E = prevMap.end();
       I != E; ++I) {
    SymbolRef sym = I.getKey();
    if (!currMap.lookup(sym))
      return sym;
  }

  return nullptr;
}

static bool isReferenceCountingPointerDestructor(const CXXDestructorDecl *DD) {
  if (const IdentifierInfo *II = DD->getParent()->getIdentifier()) {
    StringRef N = II->getName();
    if (N.contains_lower("ptr") || N.contains_lower("pointer")) {
      if (N.contains_lower("ref") || N.contains_lower("cnt") ||
          N.contains_lower("intrusive") || N.contains_lower("shared")) {
        return true;
      }
    }
  }
  return false;
}

std::shared_ptr<PathDiagnosticPiece> MallocChecker::MallocBugVisitor::VisitNode(
    const ExplodedNode *N, BugReporterContext &BRC, BugReport &BR) {

  ProgramStateRef state = N->getState();
  ProgramStateRef statePrev = N->getFirstPred()->getState();

  const RefState *RS = state->get<RegionState>(Sym);
  const RefState *RSPrev = statePrev->get<RegionState>(Sym);

  const Stmt *S = PathDiagnosticLocation::getStmt(N);
  // When dealing with containers, we sometimes want to give a note
  // even if the statement is missing.
  if (!S && (!RS || RS->getAllocationFamily() != AF_InnerBuffer))
    return nullptr;

  const LocationContext *CurrentLC = N->getLocationContext();

  // If we find an atomic fetch_add or fetch_sub within the destructor in which
  // the pointer was released (before the release), this is likely a destructor
  // of a shared pointer.
  // Because we don't model atomics, and also because we don't know that the
  // original reference count is positive, we should not report use-after-frees
  // on objects deleted in such destructors. This can probably be improved
  // through better shared pointer modeling.
  if (ReleaseDestructorLC) {
    if (const auto *AE = dyn_cast<AtomicExpr>(S)) {
      AtomicExpr::AtomicOp Op = AE->getOp();
      if (Op == AtomicExpr::AO__c11_atomic_fetch_add ||
          Op == AtomicExpr::AO__c11_atomic_fetch_sub) {
        if (ReleaseDestructorLC == CurrentLC ||
            ReleaseDestructorLC->isParentOf(CurrentLC)) {
          BR.markInvalid(getTag(), S);
        }
      }
    }
  }

  // FIXME: We will eventually need to handle non-statement-based events
  // (__attribute__((cleanup))).

  // Find out if this is an interesting point and what is the kind.
  StringRef Msg;
  StackHintGeneratorForSymbol *StackHint = nullptr;
  SmallString<256> Buf;
  llvm::raw_svector_ostream OS(Buf);

  if (Mode == Normal) {
    if (isAllocated(RS, RSPrev, S)) {
      Msg = "Memory is allocated";
      StackHint = new StackHintGeneratorForSymbol(Sym,
                                                  "Returned allocated memory");
    } else if (isReleased(RS, RSPrev, S)) {
      const auto Family = RS->getAllocationFamily();
      switch (Family) {
        case AF_Alloca:
        case AF_Malloc:
        case AF_CXXNew:
        case AF_CXXNewArray:
        case AF_IfNameIndex:
          Msg = "Memory is released";
          StackHint = new StackHintGeneratorForSymbol(Sym,
                                              "Returning; memory was released");
          break;
        case AF_InnerBuffer: {
          const MemRegion *ObjRegion =
              allocation_state::getContainerObjRegion(statePrev, Sym);
          const auto *TypedRegion = cast<TypedValueRegion>(ObjRegion);
          QualType ObjTy = TypedRegion->getValueType();
          OS << "Inner buffer of '" << ObjTy.getAsString() << "' ";

          if (N->getLocation().getKind() == ProgramPoint::PostImplicitCallKind) {
            OS << "deallocated by call to destructor";
            StackHint = new StackHintGeneratorForSymbol(Sym,
                                      "Returning; inner buffer was deallocated");
          } else {
            OS << "reallocated by call to '";
            const Stmt *S = RS->getStmt();
            if (const auto *MemCallE = dyn_cast<CXXMemberCallExpr>(S)) {
              OS << MemCallE->getMethodDecl()->getNameAsString();
            } else if (const auto *OpCallE = dyn_cast<CXXOperatorCallExpr>(S)) {
              OS << OpCallE->getDirectCallee()->getNameAsString();
            } else if (const auto *CallE = dyn_cast<CallExpr>(S)) {
              auto &CEMgr = BRC.getStateManager().getCallEventManager();
              CallEventRef<> Call = CEMgr.getSimpleCall(CallE, state, CurrentLC);
              const auto *D = dyn_cast_or_null<NamedDecl>(Call->getDecl());
              OS << (D ? D->getNameAsString() : "unknown");
            }
            OS << "'";
            StackHint = new StackHintGeneratorForSymbol(Sym,
                                      "Returning; inner buffer was reallocated");
          }
          Msg = OS.str();
          break;
        }
        case AF_None:
          llvm_unreachable("Unhandled allocation family!");
      }

      // See if we're releasing memory while inlining a destructor
      // (or one of its callees). This turns on various common
      // false positive suppressions.
      bool FoundAnyDestructor = false;
      for (const LocationContext *LC = CurrentLC; LC; LC = LC->getParent()) {
        if (const auto *DD = dyn_cast<CXXDestructorDecl>(LC->getDecl())) {
          if (isReferenceCountingPointerDestructor(DD)) {
            // This immediately looks like a reference-counting destructor.
            // We're bad at guessing the original reference count of the object,
            // so suppress the report for now.
            BR.markInvalid(getTag(), DD);
          } else if (!FoundAnyDestructor) {
            assert(!ReleaseDestructorLC &&
                   "There can be only one release point!");
            // Suspect that it's a reference counting pointer destructor.
            // On one of the next nodes might find out that it has atomic
            // reference counting operations within it (see the code above),
            // and if so, we'd conclude that it likely is a reference counting
            // pointer destructor.
            ReleaseDestructorLC = LC->getStackFrame();
            // It is unlikely that releasing memory is delegated to a destructor
            // inside a destructor of a shared pointer, because it's fairly hard
            // to pass the information that the pointer indeed needs to be
            // released into it. So we're only interested in the innermost
            // destructor.
            FoundAnyDestructor = true;
          }
        }
      }
    } else if (isRelinquished(RS, RSPrev, S)) {
      Msg = "Memory ownership is transferred";
      StackHint = new StackHintGeneratorForSymbol(Sym, "");
    } else if (isReallocFailedCheck(RS, RSPrev, S)) {
      Mode = ReallocationFailed;
      Msg = "Reallocation failed";
      StackHint = new StackHintGeneratorForReallocationFailed(Sym,
                                                       "Reallocation failed");

      if (SymbolRef sym = findFailedReallocSymbol(state, statePrev)) {
        // Is it possible to fail two reallocs WITHOUT testing in between?
        assert((!FailedReallocSymbol || FailedReallocSymbol == sym) &&
          "We only support one failed realloc at a time.");
        BR.markInteresting(sym);
        FailedReallocSymbol = sym;
      }
    }

  // We are in a special mode if a reallocation failed later in the path.
  } else if (Mode == ReallocationFailed) {
    assert(FailedReallocSymbol && "No symbol to look for.");

    // Is this is the first appearance of the reallocated symbol?
    if (!statePrev->get<RegionState>(FailedReallocSymbol)) {
      // We're at the reallocation point.
      Msg = "Attempt to reallocate memory";
      StackHint = new StackHintGeneratorForSymbol(Sym,
                                                 "Returned reallocated memory");
      FailedReallocSymbol = nullptr;
      Mode = Normal;
    }
  }

  if (Msg.empty())
    return nullptr;
  assert(StackHint);

  // Generate the extra diagnostic.
  PathDiagnosticLocation Pos;
  if (!S) {
    assert(RS->getAllocationFamily() == AF_InnerBuffer);
    auto PostImplCall = N->getLocation().getAs<PostImplicitCall>();
    if (!PostImplCall)
      return nullptr;
    Pos = PathDiagnosticLocation(PostImplCall->getLocation(),
                                 BRC.getSourceManager());
  } else {
    Pos = PathDiagnosticLocation(S, BRC.getSourceManager(),
                                 N->getLocationContext());
  }

  return std::make_shared<PathDiagnosticEventPiece>(Pos, Msg, true, StackHint);
}

void MallocChecker::printState(raw_ostream &Out, ProgramStateRef State,
                               const char *NL, const char *Sep) const {

  RegionStateTy RS = State->get<RegionState>();

  if (!RS.isEmpty()) {
    Out << Sep << "MallocChecker :" << NL;
    for (RegionStateTy::iterator I = RS.begin(), E = RS.end(); I != E; ++I) {
      const RefState *RefS = State->get<RegionState>(I.getKey());
      AllocationFamily Family = RefS->getAllocationFamily();
      Optional<MallocChecker::CheckKind> CheckKind = getCheckIfTracked(Family);
      if (!CheckKind.hasValue())
         CheckKind = getCheckIfTracked(Family, true);

      I.getKey()->dumpToStream(Out);
      Out << " : ";
      I.getData().dump(Out);
      if (CheckKind.hasValue())
        Out << " (" << CheckNames[*CheckKind].getName() << ")";
      Out << NL;
    }
  }
}

namespace clang {
namespace ento {
namespace allocation_state {

ProgramStateRef
markReleased(ProgramStateRef State, SymbolRef Sym, const Expr *Origin) {
  AllocationFamily Family = AF_InnerBuffer;
  return State->set<RegionState>(Sym, RefState::getReleased(Family, Origin));
}

} // end namespace allocation_state
} // end namespace ento
} // end namespace clang

void ento::registerNewDeleteLeaksChecker(CheckerManager &mgr) {
  registerCStringCheckerBasic(mgr);
  MallocChecker *checker = mgr.registerChecker<MallocChecker>();
  checker->IsOptimistic = mgr.getAnalyzerOptions().getCheckerBooleanOption(
      "Optimistic", false, checker);
  checker->ChecksEnabled[MallocChecker::CK_NewDeleteLeaksChecker] = true;
  checker->CheckNames[MallocChecker::CK_NewDeleteLeaksChecker] =
      mgr.getCurrentCheckName();
  // We currently treat NewDeleteLeaks checker as a subchecker of NewDelete
  // checker.
  if (!checker->ChecksEnabled[MallocChecker::CK_NewDeleteChecker]) {
    checker->ChecksEnabled[MallocChecker::CK_NewDeleteChecker] = true;
    // FIXME: This does not set the correct name, but without this workaround
    //        no name will be set at all.
    checker->CheckNames[MallocChecker::CK_NewDeleteChecker] =
        mgr.getCurrentCheckName();
  }
}

// Intended to be used in InnerPointerChecker to register the part of
// MallocChecker connected to it.
void ento::registerInnerPointerCheckerAux(CheckerManager &mgr) {
    registerCStringCheckerBasic(mgr);
    MallocChecker *checker = mgr.registerChecker<MallocChecker>();
    checker->IsOptimistic = mgr.getAnalyzerOptions().getCheckerBooleanOption(
        "Optimistic", false, checker);
    checker->ChecksEnabled[MallocChecker::CK_InnerPointerChecker] = true;
    checker->CheckNames[MallocChecker::CK_InnerPointerChecker] =
        mgr.getCurrentCheckName();
}

#define REGISTER_CHECKER(name)                                                 \
  void ento::register##name(CheckerManager &mgr) {                             \
    registerCStringCheckerBasic(mgr);                                          \
    MallocChecker *checker = mgr.registerChecker<MallocChecker>();             \
    checker->IsOptimistic = mgr.getAnalyzerOptions().getCheckerBooleanOption(  \
        "Optimistic", false, checker);                                         \
    checker->ChecksEnabled[MallocChecker::CK_##name] = true;                   \
    checker->CheckNames[MallocChecker::CK_##name] = mgr.getCurrentCheckName(); \
  }

REGISTER_CHECKER(MallocChecker)
REGISTER_CHECKER(NewDeleteChecker)
REGISTER_CHECKER(MismatchedDeallocatorChecker)
