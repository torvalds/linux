//= CStringChecker.cpp - Checks calls to C string functions --------*- C++ -*-//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This defines CStringChecker, which is an assortment of checks on calls
// to functions in <string.h>.
//
//===----------------------------------------------------------------------===//

#include "clang/StaticAnalyzer/Checkers/BuiltinCheckerRegistration.h"
#include "InterCheckerAPI.h"
#include "clang/Basic/CharInfo.h"
#include "clang/StaticAnalyzer/Core/BugReporter/BugType.h"
#include "clang/StaticAnalyzer/Core/Checker.h"
#include "clang/StaticAnalyzer/Core/CheckerManager.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/CheckerContext.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/ProgramStateTrait.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/Support/raw_ostream.h"

using namespace clang;
using namespace ento;

namespace {
class CStringChecker : public Checker< eval::Call,
                                         check::PreStmt<DeclStmt>,
                                         check::LiveSymbols,
                                         check::DeadSymbols,
                                         check::RegionChanges
                                         > {
  mutable std::unique_ptr<BugType> BT_Null, BT_Bounds, BT_Overlap,
      BT_NotCString, BT_AdditionOverflow;

  mutable const char *CurrentFunctionDescription;

public:
  /// The filter is used to filter out the diagnostics which are not enabled by
  /// the user.
  struct CStringChecksFilter {
    DefaultBool CheckCStringNullArg;
    DefaultBool CheckCStringOutOfBounds;
    DefaultBool CheckCStringBufferOverlap;
    DefaultBool CheckCStringNotNullTerm;

    CheckName CheckNameCStringNullArg;
    CheckName CheckNameCStringOutOfBounds;
    CheckName CheckNameCStringBufferOverlap;
    CheckName CheckNameCStringNotNullTerm;
  };

  CStringChecksFilter Filter;

  static void *getTag() { static int tag; return &tag; }

  bool evalCall(const CallExpr *CE, CheckerContext &C) const;
  void checkPreStmt(const DeclStmt *DS, CheckerContext &C) const;
  void checkLiveSymbols(ProgramStateRef state, SymbolReaper &SR) const;
  void checkDeadSymbols(SymbolReaper &SR, CheckerContext &C) const;

  ProgramStateRef
    checkRegionChanges(ProgramStateRef state,
                       const InvalidatedSymbols *,
                       ArrayRef<const MemRegion *> ExplicitRegions,
                       ArrayRef<const MemRegion *> Regions,
                       const LocationContext *LCtx,
                       const CallEvent *Call) const;

  typedef void (CStringChecker::*FnCheck)(CheckerContext &,
                                          const CallExpr *) const;

  void evalMemcpy(CheckerContext &C, const CallExpr *CE) const;
  void evalMempcpy(CheckerContext &C, const CallExpr *CE) const;
  void evalMemmove(CheckerContext &C, const CallExpr *CE) const;
  void evalBcopy(CheckerContext &C, const CallExpr *CE) const;
  void evalCopyCommon(CheckerContext &C, const CallExpr *CE,
                      ProgramStateRef state,
                      const Expr *Size,
                      const Expr *Source,
                      const Expr *Dest,
                      bool Restricted = false,
                      bool IsMempcpy = false) const;

  void evalMemcmp(CheckerContext &C, const CallExpr *CE) const;

  void evalstrLength(CheckerContext &C, const CallExpr *CE) const;
  void evalstrnLength(CheckerContext &C, const CallExpr *CE) const;
  void evalstrLengthCommon(CheckerContext &C,
                           const CallExpr *CE,
                           bool IsStrnlen = false) const;

  void evalStrcpy(CheckerContext &C, const CallExpr *CE) const;
  void evalStrncpy(CheckerContext &C, const CallExpr *CE) const;
  void evalStpcpy(CheckerContext &C, const CallExpr *CE) const;
  void evalStrlcpy(CheckerContext &C, const CallExpr *CE) const;
  void evalStrcpyCommon(CheckerContext &C,
                        const CallExpr *CE,
                        bool returnEnd,
                        bool isBounded,
                        bool isAppending,
                        bool returnPtr = true) const;

  void evalStrcat(CheckerContext &C, const CallExpr *CE) const;
  void evalStrncat(CheckerContext &C, const CallExpr *CE) const;
  void evalStrlcat(CheckerContext &C, const CallExpr *CE) const;

  void evalStrcmp(CheckerContext &C, const CallExpr *CE) const;
  void evalStrncmp(CheckerContext &C, const CallExpr *CE) const;
  void evalStrcasecmp(CheckerContext &C, const CallExpr *CE) const;
  void evalStrncasecmp(CheckerContext &C, const CallExpr *CE) const;
  void evalStrcmpCommon(CheckerContext &C,
                        const CallExpr *CE,
                        bool isBounded = false,
                        bool ignoreCase = false) const;

  void evalStrsep(CheckerContext &C, const CallExpr *CE) const;

  void evalStdCopy(CheckerContext &C, const CallExpr *CE) const;
  void evalStdCopyBackward(CheckerContext &C, const CallExpr *CE) const;
  void evalStdCopyCommon(CheckerContext &C, const CallExpr *CE) const;
  void evalMemset(CheckerContext &C, const CallExpr *CE) const;
  void evalBzero(CheckerContext &C, const CallExpr *CE) const;

  // Utility methods
  std::pair<ProgramStateRef , ProgramStateRef >
  static assumeZero(CheckerContext &C,
                    ProgramStateRef state, SVal V, QualType Ty);

  static ProgramStateRef setCStringLength(ProgramStateRef state,
                                              const MemRegion *MR,
                                              SVal strLength);
  static SVal getCStringLengthForRegion(CheckerContext &C,
                                        ProgramStateRef &state,
                                        const Expr *Ex,
                                        const MemRegion *MR,
                                        bool hypothetical);
  SVal getCStringLength(CheckerContext &C,
                        ProgramStateRef &state,
                        const Expr *Ex,
                        SVal Buf,
                        bool hypothetical = false) const;

  const StringLiteral *getCStringLiteral(CheckerContext &C,
                                         ProgramStateRef &state,
                                         const Expr *expr,
                                         SVal val) const;

  static ProgramStateRef InvalidateBuffer(CheckerContext &C,
                                          ProgramStateRef state,
                                          const Expr *Ex, SVal V,
                                          bool IsSourceBuffer,
                                          const Expr *Size);

  static bool SummarizeRegion(raw_ostream &os, ASTContext &Ctx,
                              const MemRegion *MR);

  static bool memsetAux(const Expr *DstBuffer, SVal CharE,
                        const Expr *Size, CheckerContext &C,
                        ProgramStateRef &State);

  // Re-usable checks
  ProgramStateRef checkNonNull(CheckerContext &C,
                                   ProgramStateRef state,
                                   const Expr *S,
                                   SVal l) const;
  ProgramStateRef CheckLocation(CheckerContext &C,
                                    ProgramStateRef state,
                                    const Expr *S,
                                    SVal l,
                                    const char *message = nullptr) const;
  ProgramStateRef CheckBufferAccess(CheckerContext &C,
                                        ProgramStateRef state,
                                        const Expr *Size,
                                        const Expr *FirstBuf,
                                        const Expr *SecondBuf,
                                        const char *firstMessage = nullptr,
                                        const char *secondMessage = nullptr,
                                        bool WarnAboutSize = false) const;

  ProgramStateRef CheckBufferAccess(CheckerContext &C,
                                        ProgramStateRef state,
                                        const Expr *Size,
                                        const Expr *Buf,
                                        const char *message = nullptr,
                                        bool WarnAboutSize = false) const {
    // This is a convenience overload.
    return CheckBufferAccess(C, state, Size, Buf, nullptr, message, nullptr,
                             WarnAboutSize);
  }
  ProgramStateRef CheckOverlap(CheckerContext &C,
                                   ProgramStateRef state,
                                   const Expr *Size,
                                   const Expr *First,
                                   const Expr *Second) const;
  void emitOverlapBug(CheckerContext &C,
                      ProgramStateRef state,
                      const Stmt *First,
                      const Stmt *Second) const;

  void emitNullArgBug(CheckerContext &C, ProgramStateRef State, const Stmt *S,
                      StringRef WarningMsg) const;
  void emitOutOfBoundsBug(CheckerContext &C, ProgramStateRef State,
                          const Stmt *S, StringRef WarningMsg) const;
  void emitNotCStringBug(CheckerContext &C, ProgramStateRef State,
                         const Stmt *S, StringRef WarningMsg) const;
  void emitAdditionOverflowBug(CheckerContext &C, ProgramStateRef State) const;

  ProgramStateRef checkAdditionOverflow(CheckerContext &C,
                                            ProgramStateRef state,
                                            NonLoc left,
                                            NonLoc right) const;

  // Return true if the destination buffer of the copy function may be in bound.
  // Expects SVal of Size to be positive and unsigned.
  // Expects SVal of FirstBuf to be a FieldRegion.
  static bool IsFirstBufInBound(CheckerContext &C,
                                ProgramStateRef state,
                                const Expr *FirstBuf,
                                const Expr *Size);
};

} //end anonymous namespace

REGISTER_MAP_WITH_PROGRAMSTATE(CStringLength, const MemRegion *, SVal)

//===----------------------------------------------------------------------===//
// Individual checks and utility methods.
//===----------------------------------------------------------------------===//

std::pair<ProgramStateRef , ProgramStateRef >
CStringChecker::assumeZero(CheckerContext &C, ProgramStateRef state, SVal V,
                           QualType Ty) {
  Optional<DefinedSVal> val = V.getAs<DefinedSVal>();
  if (!val)
    return std::pair<ProgramStateRef , ProgramStateRef >(state, state);

  SValBuilder &svalBuilder = C.getSValBuilder();
  DefinedOrUnknownSVal zero = svalBuilder.makeZeroVal(Ty);
  return state->assume(svalBuilder.evalEQ(state, *val, zero));
}

ProgramStateRef CStringChecker::checkNonNull(CheckerContext &C,
                                            ProgramStateRef state,
                                            const Expr *S, SVal l) const {
  // If a previous check has failed, propagate the failure.
  if (!state)
    return nullptr;

  ProgramStateRef stateNull, stateNonNull;
  std::tie(stateNull, stateNonNull) = assumeZero(C, state, l, S->getType());

  if (stateNull && !stateNonNull) {
    if (Filter.CheckCStringNullArg) {
      SmallString<80> buf;
      llvm::raw_svector_ostream os(buf);
      assert(CurrentFunctionDescription);
      os << "Null pointer argument in call to " << CurrentFunctionDescription;

      emitNullArgBug(C, stateNull, S, os.str());
    }
    return nullptr;
  }

  // From here on, assume that the value is non-null.
  assert(stateNonNull);
  return stateNonNull;
}

// FIXME: This was originally copied from ArrayBoundChecker.cpp. Refactor?
ProgramStateRef CStringChecker::CheckLocation(CheckerContext &C,
                                             ProgramStateRef state,
                                             const Expr *S, SVal l,
                                             const char *warningMsg) const {
  // If a previous check has failed, propagate the failure.
  if (!state)
    return nullptr;

  // Check for out of bound array element access.
  const MemRegion *R = l.getAsRegion();
  if (!R)
    return state;

  const ElementRegion *ER = dyn_cast<ElementRegion>(R);
  if (!ER)
    return state;

  if (ER->getValueType() != C.getASTContext().CharTy)
    return state;

  // Get the size of the array.
  const SubRegion *superReg = cast<SubRegion>(ER->getSuperRegion());
  SValBuilder &svalBuilder = C.getSValBuilder();
  SVal Extent =
    svalBuilder.convertToArrayIndex(superReg->getExtent(svalBuilder));
  DefinedOrUnknownSVal Size = Extent.castAs<DefinedOrUnknownSVal>();

  // Get the index of the accessed element.
  DefinedOrUnknownSVal Idx = ER->getIndex().castAs<DefinedOrUnknownSVal>();

  ProgramStateRef StInBound = state->assumeInBound(Idx, Size, true);
  ProgramStateRef StOutBound = state->assumeInBound(Idx, Size, false);
  if (StOutBound && !StInBound) {
    // These checks are either enabled by the CString out-of-bounds checker
    // explicitly or implicitly by the Malloc checker.
    // In the latter case we only do modeling but do not emit warning.
    if (!Filter.CheckCStringOutOfBounds)
      return nullptr;
    // Emit a bug report.
    if (warningMsg) {
      emitOutOfBoundsBug(C, StOutBound, S, warningMsg);
    } else {
      assert(CurrentFunctionDescription);
      assert(CurrentFunctionDescription[0] != '\0');

      SmallString<80> buf;
      llvm::raw_svector_ostream os(buf);
      os << toUppercase(CurrentFunctionDescription[0])
         << &CurrentFunctionDescription[1]
         << " accesses out-of-bound array element";
      emitOutOfBoundsBug(C, StOutBound, S, os.str());
    }
    return nullptr;
  }

  // Array bound check succeeded.  From this point forward the array bound
  // should always succeed.
  return StInBound;
}

ProgramStateRef CStringChecker::CheckBufferAccess(CheckerContext &C,
                                                 ProgramStateRef state,
                                                 const Expr *Size,
                                                 const Expr *FirstBuf,
                                                 const Expr *SecondBuf,
                                                 const char *firstMessage,
                                                 const char *secondMessage,
                                                 bool WarnAboutSize) const {
  // If a previous check has failed, propagate the failure.
  if (!state)
    return nullptr;

  SValBuilder &svalBuilder = C.getSValBuilder();
  ASTContext &Ctx = svalBuilder.getContext();
  const LocationContext *LCtx = C.getLocationContext();

  QualType sizeTy = Size->getType();
  QualType PtrTy = Ctx.getPointerType(Ctx.CharTy);

  // Check that the first buffer is non-null.
  SVal BufVal = C.getSVal(FirstBuf);
  state = checkNonNull(C, state, FirstBuf, BufVal);
  if (!state)
    return nullptr;

  // If out-of-bounds checking is turned off, skip the rest.
  if (!Filter.CheckCStringOutOfBounds)
    return state;

  // Get the access length and make sure it is known.
  // FIXME: This assumes the caller has already checked that the access length
  // is positive. And that it's unsigned.
  SVal LengthVal = C.getSVal(Size);
  Optional<NonLoc> Length = LengthVal.getAs<NonLoc>();
  if (!Length)
    return state;

  // Compute the offset of the last element to be accessed: size-1.
  NonLoc One = svalBuilder.makeIntVal(1, sizeTy).castAs<NonLoc>();
  SVal Offset = svalBuilder.evalBinOpNN(state, BO_Sub, *Length, One, sizeTy);
  if (Offset.isUnknown())
    return nullptr;
  NonLoc LastOffset = Offset.castAs<NonLoc>();

  // Check that the first buffer is sufficiently long.
  SVal BufStart = svalBuilder.evalCast(BufVal, PtrTy, FirstBuf->getType());
  if (Optional<Loc> BufLoc = BufStart.getAs<Loc>()) {
    const Expr *warningExpr = (WarnAboutSize ? Size : FirstBuf);

    SVal BufEnd = svalBuilder.evalBinOpLN(state, BO_Add, *BufLoc,
                                          LastOffset, PtrTy);
    state = CheckLocation(C, state, warningExpr, BufEnd, firstMessage);

    // If the buffer isn't large enough, abort.
    if (!state)
      return nullptr;
  }

  // If there's a second buffer, check it as well.
  if (SecondBuf) {
    BufVal = state->getSVal(SecondBuf, LCtx);
    state = checkNonNull(C, state, SecondBuf, BufVal);
    if (!state)
      return nullptr;

    BufStart = svalBuilder.evalCast(BufVal, PtrTy, SecondBuf->getType());
    if (Optional<Loc> BufLoc = BufStart.getAs<Loc>()) {
      const Expr *warningExpr = (WarnAboutSize ? Size : SecondBuf);

      SVal BufEnd = svalBuilder.evalBinOpLN(state, BO_Add, *BufLoc,
                                            LastOffset, PtrTy);
      state = CheckLocation(C, state, warningExpr, BufEnd, secondMessage);
    }
  }

  // Large enough or not, return this state!
  return state;
}

ProgramStateRef CStringChecker::CheckOverlap(CheckerContext &C,
                                            ProgramStateRef state,
                                            const Expr *Size,
                                            const Expr *First,
                                            const Expr *Second) const {
  if (!Filter.CheckCStringBufferOverlap)
    return state;

  // Do a simple check for overlap: if the two arguments are from the same
  // buffer, see if the end of the first is greater than the start of the second
  // or vice versa.

  // If a previous check has failed, propagate the failure.
  if (!state)
    return nullptr;

  ProgramStateRef stateTrue, stateFalse;

  // Get the buffer values and make sure they're known locations.
  const LocationContext *LCtx = C.getLocationContext();
  SVal firstVal = state->getSVal(First, LCtx);
  SVal secondVal = state->getSVal(Second, LCtx);

  Optional<Loc> firstLoc = firstVal.getAs<Loc>();
  if (!firstLoc)
    return state;

  Optional<Loc> secondLoc = secondVal.getAs<Loc>();
  if (!secondLoc)
    return state;

  // Are the two values the same?
  SValBuilder &svalBuilder = C.getSValBuilder();
  std::tie(stateTrue, stateFalse) =
    state->assume(svalBuilder.evalEQ(state, *firstLoc, *secondLoc));

  if (stateTrue && !stateFalse) {
    // If the values are known to be equal, that's automatically an overlap.
    emitOverlapBug(C, stateTrue, First, Second);
    return nullptr;
  }

  // assume the two expressions are not equal.
  assert(stateFalse);
  state = stateFalse;

  // Which value comes first?
  QualType cmpTy = svalBuilder.getConditionType();
  SVal reverse = svalBuilder.evalBinOpLL(state, BO_GT,
                                         *firstLoc, *secondLoc, cmpTy);
  Optional<DefinedOrUnknownSVal> reverseTest =
      reverse.getAs<DefinedOrUnknownSVal>();
  if (!reverseTest)
    return state;

  std::tie(stateTrue, stateFalse) = state->assume(*reverseTest);
  if (stateTrue) {
    if (stateFalse) {
      // If we don't know which one comes first, we can't perform this test.
      return state;
    } else {
      // Switch the values so that firstVal is before secondVal.
      std::swap(firstLoc, secondLoc);

      // Switch the Exprs as well, so that they still correspond.
      std::swap(First, Second);
    }
  }

  // Get the length, and make sure it too is known.
  SVal LengthVal = state->getSVal(Size, LCtx);
  Optional<NonLoc> Length = LengthVal.getAs<NonLoc>();
  if (!Length)
    return state;

  // Convert the first buffer's start address to char*.
  // Bail out if the cast fails.
  ASTContext &Ctx = svalBuilder.getContext();
  QualType CharPtrTy = Ctx.getPointerType(Ctx.CharTy);
  SVal FirstStart = svalBuilder.evalCast(*firstLoc, CharPtrTy,
                                         First->getType());
  Optional<Loc> FirstStartLoc = FirstStart.getAs<Loc>();
  if (!FirstStartLoc)
    return state;

  // Compute the end of the first buffer. Bail out if THAT fails.
  SVal FirstEnd = svalBuilder.evalBinOpLN(state, BO_Add,
                                 *FirstStartLoc, *Length, CharPtrTy);
  Optional<Loc> FirstEndLoc = FirstEnd.getAs<Loc>();
  if (!FirstEndLoc)
    return state;

  // Is the end of the first buffer past the start of the second buffer?
  SVal Overlap = svalBuilder.evalBinOpLL(state, BO_GT,
                                *FirstEndLoc, *secondLoc, cmpTy);
  Optional<DefinedOrUnknownSVal> OverlapTest =
      Overlap.getAs<DefinedOrUnknownSVal>();
  if (!OverlapTest)
    return state;

  std::tie(stateTrue, stateFalse) = state->assume(*OverlapTest);

  if (stateTrue && !stateFalse) {
    // Overlap!
    emitOverlapBug(C, stateTrue, First, Second);
    return nullptr;
  }

  // assume the two expressions don't overlap.
  assert(stateFalse);
  return stateFalse;
}

void CStringChecker::emitOverlapBug(CheckerContext &C, ProgramStateRef state,
                                  const Stmt *First, const Stmt *Second) const {
  ExplodedNode *N = C.generateErrorNode(state);
  if (!N)
    return;

  if (!BT_Overlap)
    BT_Overlap.reset(new BugType(Filter.CheckNameCStringBufferOverlap,
                                 categories::UnixAPI, "Improper arguments"));

  // Generate a report for this bug.
  auto report = llvm::make_unique<BugReport>(
      *BT_Overlap, "Arguments must not be overlapping buffers", N);
  report->addRange(First->getSourceRange());
  report->addRange(Second->getSourceRange());

  C.emitReport(std::move(report));
}

void CStringChecker::emitNullArgBug(CheckerContext &C, ProgramStateRef State,
                                    const Stmt *S, StringRef WarningMsg) const {
  if (ExplodedNode *N = C.generateErrorNode(State)) {
    if (!BT_Null)
      BT_Null.reset(new BuiltinBug(
          Filter.CheckNameCStringNullArg, categories::UnixAPI,
          "Null pointer argument in call to byte string function"));

    BuiltinBug *BT = static_cast<BuiltinBug *>(BT_Null.get());
    auto Report = llvm::make_unique<BugReport>(*BT, WarningMsg, N);
    Report->addRange(S->getSourceRange());
    if (const auto *Ex = dyn_cast<Expr>(S))
      bugreporter::trackExpressionValue(N, Ex, *Report);
    C.emitReport(std::move(Report));
  }
}

void CStringChecker::emitOutOfBoundsBug(CheckerContext &C,
                                        ProgramStateRef State, const Stmt *S,
                                        StringRef WarningMsg) const {
  if (ExplodedNode *N = C.generateErrorNode(State)) {
    if (!BT_Bounds)
      BT_Bounds.reset(new BuiltinBug(
          Filter.CheckCStringOutOfBounds ? Filter.CheckNameCStringOutOfBounds
                                         : Filter.CheckNameCStringNullArg,
          "Out-of-bound array access",
          "Byte string function accesses out-of-bound array element"));

    BuiltinBug *BT = static_cast<BuiltinBug *>(BT_Bounds.get());

    // FIXME: It would be nice to eventually make this diagnostic more clear,
    // e.g., by referencing the original declaration or by saying *why* this
    // reference is outside the range.
    auto Report = llvm::make_unique<BugReport>(*BT, WarningMsg, N);
    Report->addRange(S->getSourceRange());
    C.emitReport(std::move(Report));
  }
}

void CStringChecker::emitNotCStringBug(CheckerContext &C, ProgramStateRef State,
                                       const Stmt *S,
                                       StringRef WarningMsg) const {
  if (ExplodedNode *N = C.generateNonFatalErrorNode(State)) {
    if (!BT_NotCString)
      BT_NotCString.reset(new BuiltinBug(
          Filter.CheckNameCStringNotNullTerm, categories::UnixAPI,
          "Argument is not a null-terminated string."));

    auto Report = llvm::make_unique<BugReport>(*BT_NotCString, WarningMsg, N);

    Report->addRange(S->getSourceRange());
    C.emitReport(std::move(Report));
  }
}

void CStringChecker::emitAdditionOverflowBug(CheckerContext &C,
                                             ProgramStateRef State) const {
  if (ExplodedNode *N = C.generateErrorNode(State)) {
    if (!BT_NotCString)
      BT_NotCString.reset(
          new BuiltinBug(Filter.CheckNameCStringOutOfBounds, "API",
                         "Sum of expressions causes overflow."));

    // This isn't a great error message, but this should never occur in real
    // code anyway -- you'd have to create a buffer longer than a size_t can
    // represent, which is sort of a contradiction.
    const char *WarningMsg =
        "This expression will create a string whose length is too big to "
        "be represented as a size_t";

    auto Report = llvm::make_unique<BugReport>(*BT_NotCString, WarningMsg, N);
    C.emitReport(std::move(Report));
  }
}

ProgramStateRef CStringChecker::checkAdditionOverflow(CheckerContext &C,
                                                     ProgramStateRef state,
                                                     NonLoc left,
                                                     NonLoc right) const {
  // If out-of-bounds checking is turned off, skip the rest.
  if (!Filter.CheckCStringOutOfBounds)
    return state;

  // If a previous check has failed, propagate the failure.
  if (!state)
    return nullptr;

  SValBuilder &svalBuilder = C.getSValBuilder();
  BasicValueFactory &BVF = svalBuilder.getBasicValueFactory();

  QualType sizeTy = svalBuilder.getContext().getSizeType();
  const llvm::APSInt &maxValInt = BVF.getMaxValue(sizeTy);
  NonLoc maxVal = svalBuilder.makeIntVal(maxValInt);

  SVal maxMinusRight;
  if (right.getAs<nonloc::ConcreteInt>()) {
    maxMinusRight = svalBuilder.evalBinOpNN(state, BO_Sub, maxVal, right,
                                                 sizeTy);
  } else {
    // Try switching the operands. (The order of these two assignments is
    // important!)
    maxMinusRight = svalBuilder.evalBinOpNN(state, BO_Sub, maxVal, left,
                                            sizeTy);
    left = right;
  }

  if (Optional<NonLoc> maxMinusRightNL = maxMinusRight.getAs<NonLoc>()) {
    QualType cmpTy = svalBuilder.getConditionType();
    // If left > max - right, we have an overflow.
    SVal willOverflow = svalBuilder.evalBinOpNN(state, BO_GT, left,
                                                *maxMinusRightNL, cmpTy);

    ProgramStateRef stateOverflow, stateOkay;
    std::tie(stateOverflow, stateOkay) =
      state->assume(willOverflow.castAs<DefinedOrUnknownSVal>());

    if (stateOverflow && !stateOkay) {
      // We have an overflow. Emit a bug report.
      emitAdditionOverflowBug(C, stateOverflow);
      return nullptr;
    }

    // From now on, assume an overflow didn't occur.
    assert(stateOkay);
    state = stateOkay;
  }

  return state;
}

ProgramStateRef CStringChecker::setCStringLength(ProgramStateRef state,
                                                const MemRegion *MR,
                                                SVal strLength) {
  assert(!strLength.isUndef() && "Attempt to set an undefined string length");

  MR = MR->StripCasts();

  switch (MR->getKind()) {
  case MemRegion::StringRegionKind:
    // FIXME: This can happen if we strcpy() into a string region. This is
    // undefined [C99 6.4.5p6], but we should still warn about it.
    return state;

  case MemRegion::SymbolicRegionKind:
  case MemRegion::AllocaRegionKind:
  case MemRegion::VarRegionKind:
  case MemRegion::FieldRegionKind:
  case MemRegion::ObjCIvarRegionKind:
    // These are the types we can currently track string lengths for.
    break;

  case MemRegion::ElementRegionKind:
    // FIXME: Handle element regions by upper-bounding the parent region's
    // string length.
    return state;

  default:
    // Other regions (mostly non-data) can't have a reliable C string length.
    // For now, just ignore the change.
    // FIXME: These are rare but not impossible. We should output some kind of
    // warning for things like strcpy((char[]){'a', 0}, "b");
    return state;
  }

  if (strLength.isUnknown())
    return state->remove<CStringLength>(MR);

  return state->set<CStringLength>(MR, strLength);
}

SVal CStringChecker::getCStringLengthForRegion(CheckerContext &C,
                                               ProgramStateRef &state,
                                               const Expr *Ex,
                                               const MemRegion *MR,
                                               bool hypothetical) {
  if (!hypothetical) {
    // If there's a recorded length, go ahead and return it.
    const SVal *Recorded = state->get<CStringLength>(MR);
    if (Recorded)
      return *Recorded;
  }

  // Otherwise, get a new symbol and update the state.
  SValBuilder &svalBuilder = C.getSValBuilder();
  QualType sizeTy = svalBuilder.getContext().getSizeType();
  SVal strLength = svalBuilder.getMetadataSymbolVal(CStringChecker::getTag(),
                                                    MR, Ex, sizeTy,
                                                    C.getLocationContext(),
                                                    C.blockCount());

  if (!hypothetical) {
    if (Optional<NonLoc> strLn = strLength.getAs<NonLoc>()) {
      // In case of unbounded calls strlen etc bound the range to SIZE_MAX/4
      BasicValueFactory &BVF = svalBuilder.getBasicValueFactory();
      const llvm::APSInt &maxValInt = BVF.getMaxValue(sizeTy);
      llvm::APSInt fourInt = APSIntType(maxValInt).getValue(4);
      const llvm::APSInt *maxLengthInt = BVF.evalAPSInt(BO_Div, maxValInt,
                                                        fourInt);
      NonLoc maxLength = svalBuilder.makeIntVal(*maxLengthInt);
      SVal evalLength = svalBuilder.evalBinOpNN(state, BO_LE, *strLn,
                                                maxLength, sizeTy);
      state = state->assume(evalLength.castAs<DefinedOrUnknownSVal>(), true);
    }
    state = state->set<CStringLength>(MR, strLength);
  }

  return strLength;
}

SVal CStringChecker::getCStringLength(CheckerContext &C, ProgramStateRef &state,
                                      const Expr *Ex, SVal Buf,
                                      bool hypothetical) const {
  const MemRegion *MR = Buf.getAsRegion();
  if (!MR) {
    // If we can't get a region, see if it's something we /know/ isn't a
    // C string. In the context of locations, the only time we can issue such
    // a warning is for labels.
    if (Optional<loc::GotoLabel> Label = Buf.getAs<loc::GotoLabel>()) {
      if (Filter.CheckCStringNotNullTerm) {
        SmallString<120> buf;
        llvm::raw_svector_ostream os(buf);
        assert(CurrentFunctionDescription);
        os << "Argument to " << CurrentFunctionDescription
           << " is the address of the label '" << Label->getLabel()->getName()
           << "', which is not a null-terminated string";

        emitNotCStringBug(C, state, Ex, os.str());
      }
      return UndefinedVal();
    }

    // If it's not a region and not a label, give up.
    return UnknownVal();
  }

  // If we have a region, strip casts from it and see if we can figure out
  // its length. For anything we can't figure out, just return UnknownVal.
  MR = MR->StripCasts();

  switch (MR->getKind()) {
  case MemRegion::StringRegionKind: {
    // Modifying the contents of string regions is undefined [C99 6.4.5p6],
    // so we can assume that the byte length is the correct C string length.
    SValBuilder &svalBuilder = C.getSValBuilder();
    QualType sizeTy = svalBuilder.getContext().getSizeType();
    const StringLiteral *strLit = cast<StringRegion>(MR)->getStringLiteral();
    return svalBuilder.makeIntVal(strLit->getByteLength(), sizeTy);
  }
  case MemRegion::SymbolicRegionKind:
  case MemRegion::AllocaRegionKind:
  case MemRegion::VarRegionKind:
  case MemRegion::FieldRegionKind:
  case MemRegion::ObjCIvarRegionKind:
    return getCStringLengthForRegion(C, state, Ex, MR, hypothetical);
  case MemRegion::CompoundLiteralRegionKind:
    // FIXME: Can we track this? Is it necessary?
    return UnknownVal();
  case MemRegion::ElementRegionKind:
    // FIXME: How can we handle this? It's not good enough to subtract the
    // offset from the base string length; consider "123\x00567" and &a[5].
    return UnknownVal();
  default:
    // Other regions (mostly non-data) can't have a reliable C string length.
    // In this case, an error is emitted and UndefinedVal is returned.
    // The caller should always be prepared to handle this case.
    if (Filter.CheckCStringNotNullTerm) {
      SmallString<120> buf;
      llvm::raw_svector_ostream os(buf);

      assert(CurrentFunctionDescription);
      os << "Argument to " << CurrentFunctionDescription << " is ";

      if (SummarizeRegion(os, C.getASTContext(), MR))
        os << ", which is not a null-terminated string";
      else
        os << "not a null-terminated string";

      emitNotCStringBug(C, state, Ex, os.str());
    }
    return UndefinedVal();
  }
}

const StringLiteral *CStringChecker::getCStringLiteral(CheckerContext &C,
  ProgramStateRef &state, const Expr *expr, SVal val) const {

  // Get the memory region pointed to by the val.
  const MemRegion *bufRegion = val.getAsRegion();
  if (!bufRegion)
    return nullptr;

  // Strip casts off the memory region.
  bufRegion = bufRegion->StripCasts();

  // Cast the memory region to a string region.
  const StringRegion *strRegion= dyn_cast<StringRegion>(bufRegion);
  if (!strRegion)
    return nullptr;

  // Return the actual string in the string region.
  return strRegion->getStringLiteral();
}

bool CStringChecker::IsFirstBufInBound(CheckerContext &C,
                                       ProgramStateRef state,
                                       const Expr *FirstBuf,
                                       const Expr *Size) {
  // If we do not know that the buffer is long enough we return 'true'.
  // Otherwise the parent region of this field region would also get
  // invalidated, which would lead to warnings based on an unknown state.

  // Originally copied from CheckBufferAccess and CheckLocation.
  SValBuilder &svalBuilder = C.getSValBuilder();
  ASTContext &Ctx = svalBuilder.getContext();
  const LocationContext *LCtx = C.getLocationContext();

  QualType sizeTy = Size->getType();
  QualType PtrTy = Ctx.getPointerType(Ctx.CharTy);
  SVal BufVal = state->getSVal(FirstBuf, LCtx);

  SVal LengthVal = state->getSVal(Size, LCtx);
  Optional<NonLoc> Length = LengthVal.getAs<NonLoc>();
  if (!Length)
    return true; // cf top comment.

  // Compute the offset of the last element to be accessed: size-1.
  NonLoc One = svalBuilder.makeIntVal(1, sizeTy).castAs<NonLoc>();
  SVal Offset = svalBuilder.evalBinOpNN(state, BO_Sub, *Length, One, sizeTy);
  if (Offset.isUnknown())
    return true; // cf top comment
  NonLoc LastOffset = Offset.castAs<NonLoc>();

  // Check that the first buffer is sufficiently long.
  SVal BufStart = svalBuilder.evalCast(BufVal, PtrTy, FirstBuf->getType());
  Optional<Loc> BufLoc = BufStart.getAs<Loc>();
  if (!BufLoc)
    return true; // cf top comment.

  SVal BufEnd =
      svalBuilder.evalBinOpLN(state, BO_Add, *BufLoc, LastOffset, PtrTy);

  // Check for out of bound array element access.
  const MemRegion *R = BufEnd.getAsRegion();
  if (!R)
    return true; // cf top comment.

  const ElementRegion *ER = dyn_cast<ElementRegion>(R);
  if (!ER)
    return true; // cf top comment.

  // FIXME: Does this crash when a non-standard definition
  // of a library function is encountered?
  assert(ER->getValueType() == C.getASTContext().CharTy &&
         "IsFirstBufInBound should only be called with char* ElementRegions");

  // Get the size of the array.
  const SubRegion *superReg = cast<SubRegion>(ER->getSuperRegion());
  SVal Extent =
      svalBuilder.convertToArrayIndex(superReg->getExtent(svalBuilder));
  DefinedOrUnknownSVal ExtentSize = Extent.castAs<DefinedOrUnknownSVal>();

  // Get the index of the accessed element.
  DefinedOrUnknownSVal Idx = ER->getIndex().castAs<DefinedOrUnknownSVal>();

  ProgramStateRef StInBound = state->assumeInBound(Idx, ExtentSize, true);

  return static_cast<bool>(StInBound);
}

ProgramStateRef CStringChecker::InvalidateBuffer(CheckerContext &C,
                                                 ProgramStateRef state,
                                                 const Expr *E, SVal V,
                                                 bool IsSourceBuffer,
                                                 const Expr *Size) {
  Optional<Loc> L = V.getAs<Loc>();
  if (!L)
    return state;

  // FIXME: This is a simplified version of what's in CFRefCount.cpp -- it makes
  // some assumptions about the value that CFRefCount can't. Even so, it should
  // probably be refactored.
  if (Optional<loc::MemRegionVal> MR = L->getAs<loc::MemRegionVal>()) {
    const MemRegion *R = MR->getRegion()->StripCasts();

    // Are we dealing with an ElementRegion?  If so, we should be invalidating
    // the super-region.
    if (const ElementRegion *ER = dyn_cast<ElementRegion>(R)) {
      R = ER->getSuperRegion();
      // FIXME: What about layers of ElementRegions?
    }

    // Invalidate this region.
    const LocationContext *LCtx = C.getPredecessor()->getLocationContext();

    bool CausesPointerEscape = false;
    RegionAndSymbolInvalidationTraits ITraits;
    // Invalidate and escape only indirect regions accessible through the source
    // buffer.
    if (IsSourceBuffer) {
      ITraits.setTrait(R->getBaseRegion(),
                       RegionAndSymbolInvalidationTraits::TK_PreserveContents);
      ITraits.setTrait(R, RegionAndSymbolInvalidationTraits::TK_SuppressEscape);
      CausesPointerEscape = true;
    } else {
      const MemRegion::Kind& K = R->getKind();
      if (K == MemRegion::FieldRegionKind)
        if (Size && IsFirstBufInBound(C, state, E, Size)) {
          // If destination buffer is a field region and access is in bound,
          // do not invalidate its super region.
          ITraits.setTrait(
              R,
              RegionAndSymbolInvalidationTraits::TK_DoNotInvalidateSuperRegion);
        }
    }

    return state->invalidateRegions(R, E, C.blockCount(), LCtx,
                                    CausesPointerEscape, nullptr, nullptr,
                                    &ITraits);
  }

  // If we have a non-region value by chance, just remove the binding.
  // FIXME: is this necessary or correct? This handles the non-Region
  //  cases.  Is it ever valid to store to these?
  return state->killBinding(*L);
}

bool CStringChecker::SummarizeRegion(raw_ostream &os, ASTContext &Ctx,
                                     const MemRegion *MR) {
  const TypedValueRegion *TVR = dyn_cast<TypedValueRegion>(MR);

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
    os << "a block";
    return true;
  case MemRegion::CXXThisRegionKind:
  case MemRegion::CXXTempObjectRegionKind:
    os << "a C++ temp object of type " << TVR->getValueType().getAsString();
    return true;
  case MemRegion::VarRegionKind:
    os << "a variable of type" << TVR->getValueType().getAsString();
    return true;
  case MemRegion::FieldRegionKind:
    os << "a field of type " << TVR->getValueType().getAsString();
    return true;
  case MemRegion::ObjCIvarRegionKind:
    os << "an instance variable of type " << TVR->getValueType().getAsString();
    return true;
  default:
    return false;
  }
}

bool CStringChecker::memsetAux(const Expr *DstBuffer, SVal CharVal,
                               const Expr *Size, CheckerContext &C,
                               ProgramStateRef &State) {
  SVal MemVal = C.getSVal(DstBuffer);
  SVal SizeVal = C.getSVal(Size);
  const MemRegion *MR = MemVal.getAsRegion();
  if (!MR)
    return false;

  // We're about to model memset by producing a "default binding" in the Store.
  // Our current implementation - RegionStore - doesn't support default bindings
  // that don't cover the whole base region. So we should first get the offset
  // and the base region to figure out whether the offset of buffer is 0.
  RegionOffset Offset = MR->getAsOffset();
  const MemRegion *BR = Offset.getRegion();

  Optional<NonLoc> SizeNL = SizeVal.getAs<NonLoc>();
  if (!SizeNL)
    return false;

  SValBuilder &svalBuilder = C.getSValBuilder();
  ASTContext &Ctx = C.getASTContext();

  // void *memset(void *dest, int ch, size_t count);
  // For now we can only handle the case of offset is 0 and concrete char value.
  if (Offset.isValid() && !Offset.hasSymbolicOffset() &&
      Offset.getOffset() == 0) {
    // Get the base region's extent.
    auto *SubReg = cast<SubRegion>(BR);
    DefinedOrUnknownSVal Extent = SubReg->getExtent(svalBuilder);

    ProgramStateRef StateWholeReg, StateNotWholeReg;
    std::tie(StateWholeReg, StateNotWholeReg) =
        State->assume(svalBuilder.evalEQ(State, Extent, *SizeNL));

    // With the semantic of 'memset()', we should convert the CharVal to
    // unsigned char.
    CharVal = svalBuilder.evalCast(CharVal, Ctx.UnsignedCharTy, Ctx.IntTy);

    ProgramStateRef StateNullChar, StateNonNullChar;
    std::tie(StateNullChar, StateNonNullChar) =
        assumeZero(C, State, CharVal, Ctx.UnsignedCharTy);

    if (StateWholeReg && !StateNotWholeReg && StateNullChar &&
        !StateNonNullChar) {
      // If the 'memset()' acts on the whole region of destination buffer and
      // the value of the second argument of 'memset()' is zero, bind the second
      // argument's value to the destination buffer with 'default binding'.
      // FIXME: Since there is no perfect way to bind the non-zero character, we
      // can only deal with zero value here. In the future, we need to deal with
      // the binding of non-zero value in the case of whole region.
      State = State->bindDefaultZero(svalBuilder.makeLoc(BR),
                                     C.getLocationContext());
    } else {
      // If the destination buffer's extent is not equal to the value of
      // third argument, just invalidate buffer.
      State = InvalidateBuffer(C, State, DstBuffer, MemVal,
                               /*IsSourceBuffer*/ false, Size);
    }

    if (StateNullChar && !StateNonNullChar) {
      // If the value of the second argument of 'memset()' is zero, set the
      // string length of destination buffer to 0 directly.
      State = setCStringLength(State, MR,
                               svalBuilder.makeZeroVal(Ctx.getSizeType()));
    } else if (!StateNullChar && StateNonNullChar) {
      SVal NewStrLen = svalBuilder.getMetadataSymbolVal(
          CStringChecker::getTag(), MR, DstBuffer, Ctx.getSizeType(),
          C.getLocationContext(), C.blockCount());

      // If the value of second argument is not zero, then the string length
      // is at least the size argument.
      SVal NewStrLenGESize = svalBuilder.evalBinOp(
          State, BO_GE, NewStrLen, SizeVal, svalBuilder.getConditionType());

      State = setCStringLength(
          State->assume(NewStrLenGESize.castAs<DefinedOrUnknownSVal>(), true),
          MR, NewStrLen);
    }
  } else {
    // If the offset is not zero and char value is not concrete, we can do
    // nothing but invalidate the buffer.
    State = InvalidateBuffer(C, State, DstBuffer, MemVal,
                             /*IsSourceBuffer*/ false, Size);
  }
  return true;
}

//===----------------------------------------------------------------------===//
// evaluation of individual function calls.
//===----------------------------------------------------------------------===//

void CStringChecker::evalCopyCommon(CheckerContext &C,
                                    const CallExpr *CE,
                                    ProgramStateRef state,
                                    const Expr *Size, const Expr *Dest,
                                    const Expr *Source, bool Restricted,
                                    bool IsMempcpy) const {
  CurrentFunctionDescription = "memory copy function";

  // See if the size argument is zero.
  const LocationContext *LCtx = C.getLocationContext();
  SVal sizeVal = state->getSVal(Size, LCtx);
  QualType sizeTy = Size->getType();

  ProgramStateRef stateZeroSize, stateNonZeroSize;
  std::tie(stateZeroSize, stateNonZeroSize) =
    assumeZero(C, state, sizeVal, sizeTy);

  // Get the value of the Dest.
  SVal destVal = state->getSVal(Dest, LCtx);

  // If the size is zero, there won't be any actual memory access, so
  // just bind the return value to the destination buffer and return.
  if (stateZeroSize && !stateNonZeroSize) {
    stateZeroSize = stateZeroSize->BindExpr(CE, LCtx, destVal);
    C.addTransition(stateZeroSize);
    return;
  }

  // If the size can be nonzero, we have to check the other arguments.
  if (stateNonZeroSize) {
    state = stateNonZeroSize;

    // Ensure the destination is not null. If it is NULL there will be a
    // NULL pointer dereference.
    state = checkNonNull(C, state, Dest, destVal);
    if (!state)
      return;

    // Get the value of the Src.
    SVal srcVal = state->getSVal(Source, LCtx);

    // Ensure the source is not null. If it is NULL there will be a
    // NULL pointer dereference.
    state = checkNonNull(C, state, Source, srcVal);
    if (!state)
      return;

    // Ensure the accesses are valid and that the buffers do not overlap.
    const char * const writeWarning =
      "Memory copy function overflows destination buffer";
    state = CheckBufferAccess(C, state, Size, Dest, Source,
                              writeWarning, /* sourceWarning = */ nullptr);
    if (Restricted)
      state = CheckOverlap(C, state, Size, Dest, Source);

    if (!state)
      return;

    // If this is mempcpy, get the byte after the last byte copied and
    // bind the expr.
    if (IsMempcpy) {
      // Get the byte after the last byte copied.
      SValBuilder &SvalBuilder = C.getSValBuilder();
      ASTContext &Ctx = SvalBuilder.getContext();
      QualType CharPtrTy = Ctx.getPointerType(Ctx.CharTy);
      SVal DestRegCharVal =
          SvalBuilder.evalCast(destVal, CharPtrTy, Dest->getType());
      SVal lastElement = C.getSValBuilder().evalBinOp(
          state, BO_Add, DestRegCharVal, sizeVal, Dest->getType());
      // If we don't know how much we copied, we can at least
      // conjure a return value for later.
      if (lastElement.isUnknown())
        lastElement = C.getSValBuilder().conjureSymbolVal(nullptr, CE, LCtx,
                                                          C.blockCount());

      // The byte after the last byte copied is the return value.
      state = state->BindExpr(CE, LCtx, lastElement);
    } else {
      // All other copies return the destination buffer.
      // (Well, bcopy() has a void return type, but this won't hurt.)
      state = state->BindExpr(CE, LCtx, destVal);
    }

    // Invalidate the destination (regular invalidation without pointer-escaping
    // the address of the top-level region).
    // FIXME: Even if we can't perfectly model the copy, we should see if we
    // can use LazyCompoundVals to copy the source values into the destination.
    // This would probably remove any existing bindings past the end of the
    // copied region, but that's still an improvement over blank invalidation.
    state = InvalidateBuffer(C, state, Dest, C.getSVal(Dest),
                             /*IsSourceBuffer*/false, Size);

    // Invalidate the source (const-invalidation without const-pointer-escaping
    // the address of the top-level region).
    state = InvalidateBuffer(C, state, Source, C.getSVal(Source),
                             /*IsSourceBuffer*/true, nullptr);

    C.addTransition(state);
  }
}


void CStringChecker::evalMemcpy(CheckerContext &C, const CallExpr *CE) const {
  if (CE->getNumArgs() < 3)
    return;

  // void *memcpy(void *restrict dst, const void *restrict src, size_t n);
  // The return value is the address of the destination buffer.
  const Expr *Dest = CE->getArg(0);
  ProgramStateRef state = C.getState();

  evalCopyCommon(C, CE, state, CE->getArg(2), Dest, CE->getArg(1), true);
}

void CStringChecker::evalMempcpy(CheckerContext &C, const CallExpr *CE) const {
  if (CE->getNumArgs() < 3)
    return;

  // void *mempcpy(void *restrict dst, const void *restrict src, size_t n);
  // The return value is a pointer to the byte following the last written byte.
  const Expr *Dest = CE->getArg(0);
  ProgramStateRef state = C.getState();

  evalCopyCommon(C, CE, state, CE->getArg(2), Dest, CE->getArg(1), true, true);
}

void CStringChecker::evalMemmove(CheckerContext &C, const CallExpr *CE) const {
  if (CE->getNumArgs() < 3)
    return;

  // void *memmove(void *dst, const void *src, size_t n);
  // The return value is the address of the destination buffer.
  const Expr *Dest = CE->getArg(0);
  ProgramStateRef state = C.getState();

  evalCopyCommon(C, CE, state, CE->getArg(2), Dest, CE->getArg(1));
}

void CStringChecker::evalBcopy(CheckerContext &C, const CallExpr *CE) const {
  if (CE->getNumArgs() < 3)
    return;

  // void bcopy(const void *src, void *dst, size_t n);
  evalCopyCommon(C, CE, C.getState(),
                 CE->getArg(2), CE->getArg(1), CE->getArg(0));
}

void CStringChecker::evalMemcmp(CheckerContext &C, const CallExpr *CE) const {
  if (CE->getNumArgs() < 3)
    return;

  // int memcmp(const void *s1, const void *s2, size_t n);
  CurrentFunctionDescription = "memory comparison function";

  const Expr *Left = CE->getArg(0);
  const Expr *Right = CE->getArg(1);
  const Expr *Size = CE->getArg(2);

  ProgramStateRef state = C.getState();
  SValBuilder &svalBuilder = C.getSValBuilder();

  // See if the size argument is zero.
  const LocationContext *LCtx = C.getLocationContext();
  SVal sizeVal = state->getSVal(Size, LCtx);
  QualType sizeTy = Size->getType();

  ProgramStateRef stateZeroSize, stateNonZeroSize;
  std::tie(stateZeroSize, stateNonZeroSize) =
    assumeZero(C, state, sizeVal, sizeTy);

  // If the size can be zero, the result will be 0 in that case, and we don't
  // have to check either of the buffers.
  if (stateZeroSize) {
    state = stateZeroSize;
    state = state->BindExpr(CE, LCtx,
                            svalBuilder.makeZeroVal(CE->getType()));
    C.addTransition(state);
  }

  // If the size can be nonzero, we have to check the other arguments.
  if (stateNonZeroSize) {
    state = stateNonZeroSize;
    // If we know the two buffers are the same, we know the result is 0.
    // First, get the two buffers' addresses. Another checker will have already
    // made sure they're not undefined.
    DefinedOrUnknownSVal LV =
        state->getSVal(Left, LCtx).castAs<DefinedOrUnknownSVal>();
    DefinedOrUnknownSVal RV =
        state->getSVal(Right, LCtx).castAs<DefinedOrUnknownSVal>();

    // See if they are the same.
    DefinedOrUnknownSVal SameBuf = svalBuilder.evalEQ(state, LV, RV);
    ProgramStateRef StSameBuf, StNotSameBuf;
    std::tie(StSameBuf, StNotSameBuf) = state->assume(SameBuf);

    // If the two arguments might be the same buffer, we know the result is 0,
    // and we only need to check one size.
    if (StSameBuf) {
      state = StSameBuf;
      state = CheckBufferAccess(C, state, Size, Left);
      if (state) {
        state = StSameBuf->BindExpr(CE, LCtx,
                                    svalBuilder.makeZeroVal(CE->getType()));
        C.addTransition(state);
      }
    }

    // If the two arguments might be different buffers, we have to check the
    // size of both of them.
    if (StNotSameBuf) {
      state = StNotSameBuf;
      state = CheckBufferAccess(C, state, Size, Left, Right);
      if (state) {
        // The return value is the comparison result, which we don't know.
        SVal CmpV = svalBuilder.conjureSymbolVal(nullptr, CE, LCtx,
                                                 C.blockCount());
        state = state->BindExpr(CE, LCtx, CmpV);
        C.addTransition(state);
      }
    }
  }
}

void CStringChecker::evalstrLength(CheckerContext &C,
                                   const CallExpr *CE) const {
  if (CE->getNumArgs() < 1)
    return;

  // size_t strlen(const char *s);
  evalstrLengthCommon(C, CE, /* IsStrnlen = */ false);
}

void CStringChecker::evalstrnLength(CheckerContext &C,
                                    const CallExpr *CE) const {
  if (CE->getNumArgs() < 2)
    return;

  // size_t strnlen(const char *s, size_t maxlen);
  evalstrLengthCommon(C, CE, /* IsStrnlen = */ true);
}

void CStringChecker::evalstrLengthCommon(CheckerContext &C, const CallExpr *CE,
                                         bool IsStrnlen) const {
  CurrentFunctionDescription = "string length function";
  ProgramStateRef state = C.getState();
  const LocationContext *LCtx = C.getLocationContext();

  if (IsStrnlen) {
    const Expr *maxlenExpr = CE->getArg(1);
    SVal maxlenVal = state->getSVal(maxlenExpr, LCtx);

    ProgramStateRef stateZeroSize, stateNonZeroSize;
    std::tie(stateZeroSize, stateNonZeroSize) =
      assumeZero(C, state, maxlenVal, maxlenExpr->getType());

    // If the size can be zero, the result will be 0 in that case, and we don't
    // have to check the string itself.
    if (stateZeroSize) {
      SVal zero = C.getSValBuilder().makeZeroVal(CE->getType());
      stateZeroSize = stateZeroSize->BindExpr(CE, LCtx, zero);
      C.addTransition(stateZeroSize);
    }

    // If the size is GUARANTEED to be zero, we're done!
    if (!stateNonZeroSize)
      return;

    // Otherwise, record the assumption that the size is nonzero.
    state = stateNonZeroSize;
  }

  // Check that the string argument is non-null.
  const Expr *Arg = CE->getArg(0);
  SVal ArgVal = state->getSVal(Arg, LCtx);

  state = checkNonNull(C, state, Arg, ArgVal);

  if (!state)
    return;

  SVal strLength = getCStringLength(C, state, Arg, ArgVal);

  // If the argument isn't a valid C string, there's no valid state to
  // transition to.
  if (strLength.isUndef())
    return;

  DefinedOrUnknownSVal result = UnknownVal();

  // If the check is for strnlen() then bind the return value to no more than
  // the maxlen value.
  if (IsStrnlen) {
    QualType cmpTy = C.getSValBuilder().getConditionType();

    // It's a little unfortunate to be getting this again,
    // but it's not that expensive...
    const Expr *maxlenExpr = CE->getArg(1);
    SVal maxlenVal = state->getSVal(maxlenExpr, LCtx);

    Optional<NonLoc> strLengthNL = strLength.getAs<NonLoc>();
    Optional<NonLoc> maxlenValNL = maxlenVal.getAs<NonLoc>();

    if (strLengthNL && maxlenValNL) {
      ProgramStateRef stateStringTooLong, stateStringNotTooLong;

      // Check if the strLength is greater than the maxlen.
      std::tie(stateStringTooLong, stateStringNotTooLong) = state->assume(
          C.getSValBuilder()
              .evalBinOpNN(state, BO_GT, *strLengthNL, *maxlenValNL, cmpTy)
              .castAs<DefinedOrUnknownSVal>());

      if (stateStringTooLong && !stateStringNotTooLong) {
        // If the string is longer than maxlen, return maxlen.
        result = *maxlenValNL;
      } else if (stateStringNotTooLong && !stateStringTooLong) {
        // If the string is shorter than maxlen, return its length.
        result = *strLengthNL;
      }
    }

    if (result.isUnknown()) {
      // If we don't have enough information for a comparison, there's
      // no guarantee the full string length will actually be returned.
      // All we know is the return value is the min of the string length
      // and the limit. This is better than nothing.
      result = C.getSValBuilder().conjureSymbolVal(nullptr, CE, LCtx,
                                                   C.blockCount());
      NonLoc resultNL = result.castAs<NonLoc>();

      if (strLengthNL) {
        state = state->assume(C.getSValBuilder().evalBinOpNN(
                                  state, BO_LE, resultNL, *strLengthNL, cmpTy)
                                  .castAs<DefinedOrUnknownSVal>(), true);
      }

      if (maxlenValNL) {
        state = state->assume(C.getSValBuilder().evalBinOpNN(
                                  state, BO_LE, resultNL, *maxlenValNL, cmpTy)
                                  .castAs<DefinedOrUnknownSVal>(), true);
      }
    }

  } else {
    // This is a plain strlen(), not strnlen().
    result = strLength.castAs<DefinedOrUnknownSVal>();

    // If we don't know the length of the string, conjure a return
    // value, so it can be used in constraints, at least.
    if (result.isUnknown()) {
      result = C.getSValBuilder().conjureSymbolVal(nullptr, CE, LCtx,
                                                   C.blockCount());
    }
  }

  // Bind the return value.
  assert(!result.isUnknown() && "Should have conjured a value by now");
  state = state->BindExpr(CE, LCtx, result);
  C.addTransition(state);
}

void CStringChecker::evalStrcpy(CheckerContext &C, const CallExpr *CE) const {
  if (CE->getNumArgs() < 2)
    return;

  // char *strcpy(char *restrict dst, const char *restrict src);
  evalStrcpyCommon(C, CE,
                   /* returnEnd = */ false,
                   /* isBounded = */ false,
                   /* isAppending = */ false);
}

void CStringChecker::evalStrncpy(CheckerContext &C, const CallExpr *CE) const {
  if (CE->getNumArgs() < 3)
    return;

  // char *strncpy(char *restrict dst, const char *restrict src, size_t n);
  evalStrcpyCommon(C, CE,
                   /* returnEnd = */ false,
                   /* isBounded = */ true,
                   /* isAppending = */ false);
}

void CStringChecker::evalStpcpy(CheckerContext &C, const CallExpr *CE) const {
  if (CE->getNumArgs() < 2)
    return;

  // char *stpcpy(char *restrict dst, const char *restrict src);
  evalStrcpyCommon(C, CE,
                   /* returnEnd = */ true,
                   /* isBounded = */ false,
                   /* isAppending = */ false);
}

void CStringChecker::evalStrlcpy(CheckerContext &C, const CallExpr *CE) const {
  if (CE->getNumArgs() < 3)
    return;

  // char *strlcpy(char *dst, const char *src, size_t n);
  evalStrcpyCommon(C, CE,
                   /* returnEnd = */ true,
                   /* isBounded = */ true,
                   /* isAppending = */ false,
                   /* returnPtr = */ false);
}

void CStringChecker::evalStrcat(CheckerContext &C, const CallExpr *CE) const {
  if (CE->getNumArgs() < 2)
    return;

  //char *strcat(char *restrict s1, const char *restrict s2);
  evalStrcpyCommon(C, CE,
                   /* returnEnd = */ false,
                   /* isBounded = */ false,
                   /* isAppending = */ true);
}

void CStringChecker::evalStrncat(CheckerContext &C, const CallExpr *CE) const {
  if (CE->getNumArgs() < 3)
    return;

  //char *strncat(char *restrict s1, const char *restrict s2, size_t n);
  evalStrcpyCommon(C, CE,
                   /* returnEnd = */ false,
                   /* isBounded = */ true,
                   /* isAppending = */ true);
}

void CStringChecker::evalStrlcat(CheckerContext &C, const CallExpr *CE) const {
  if (CE->getNumArgs() < 3)
    return;

  //char *strlcat(char *s1, const char *s2, size_t n);
  evalStrcpyCommon(C, CE,
                   /* returnEnd = */ false,
                   /* isBounded = */ true,
                   /* isAppending = */ true,
                   /* returnPtr = */ false);
}

void CStringChecker::evalStrcpyCommon(CheckerContext &C, const CallExpr *CE,
                                      bool returnEnd, bool isBounded,
                                      bool isAppending, bool returnPtr) const {
  CurrentFunctionDescription = "string copy function";
  ProgramStateRef state = C.getState();
  const LocationContext *LCtx = C.getLocationContext();

  // Check that the destination is non-null.
  const Expr *Dst = CE->getArg(0);
  SVal DstVal = state->getSVal(Dst, LCtx);

  state = checkNonNull(C, state, Dst, DstVal);
  if (!state)
    return;

  // Check that the source is non-null.
  const Expr *srcExpr = CE->getArg(1);
  SVal srcVal = state->getSVal(srcExpr, LCtx);
  state = checkNonNull(C, state, srcExpr, srcVal);
  if (!state)
    return;

  // Get the string length of the source.
  SVal strLength = getCStringLength(C, state, srcExpr, srcVal);

  // If the source isn't a valid C string, give up.
  if (strLength.isUndef())
    return;

  SValBuilder &svalBuilder = C.getSValBuilder();
  QualType cmpTy = svalBuilder.getConditionType();
  QualType sizeTy = svalBuilder.getContext().getSizeType();

  // These two values allow checking two kinds of errors:
  // - actual overflows caused by a source that doesn't fit in the destination
  // - potential overflows caused by a bound that could exceed the destination
  SVal amountCopied = UnknownVal();
  SVal maxLastElementIndex = UnknownVal();
  const char *boundWarning = nullptr;

  state = CheckOverlap(C, state, isBounded ? CE->getArg(2) : CE->getArg(1), Dst, srcExpr);

  if (!state)
    return;

  // If the function is strncpy, strncat, etc... it is bounded.
  if (isBounded) {
    // Get the max number of characters to copy.
    const Expr *lenExpr = CE->getArg(2);
    SVal lenVal = state->getSVal(lenExpr, LCtx);

    // Protect against misdeclared strncpy().
    lenVal = svalBuilder.evalCast(lenVal, sizeTy, lenExpr->getType());

    Optional<NonLoc> strLengthNL = strLength.getAs<NonLoc>();
    Optional<NonLoc> lenValNL = lenVal.getAs<NonLoc>();

    // If we know both values, we might be able to figure out how much
    // we're copying.
    if (strLengthNL && lenValNL) {
      ProgramStateRef stateSourceTooLong, stateSourceNotTooLong;

      // Check if the max number to copy is less than the length of the src.
      // If the bound is equal to the source length, strncpy won't null-
      // terminate the result!
      std::tie(stateSourceTooLong, stateSourceNotTooLong) = state->assume(
          svalBuilder.evalBinOpNN(state, BO_GE, *strLengthNL, *lenValNL, cmpTy)
              .castAs<DefinedOrUnknownSVal>());

      if (stateSourceTooLong && !stateSourceNotTooLong) {
        // Max number to copy is less than the length of the src, so the actual
        // strLength copied is the max number arg.
        state = stateSourceTooLong;
        amountCopied = lenVal;

      } else if (!stateSourceTooLong && stateSourceNotTooLong) {
        // The source buffer entirely fits in the bound.
        state = stateSourceNotTooLong;
        amountCopied = strLength;
      }
    }

    // We still want to know if the bound is known to be too large.
    if (lenValNL) {
      if (isAppending) {
        // For strncat, the check is strlen(dst) + lenVal < sizeof(dst)

        // Get the string length of the destination. If the destination is
        // memory that can't have a string length, we shouldn't be copying
        // into it anyway.
        SVal dstStrLength = getCStringLength(C, state, Dst, DstVal);
        if (dstStrLength.isUndef())
          return;

        if (Optional<NonLoc> dstStrLengthNL = dstStrLength.getAs<NonLoc>()) {
          maxLastElementIndex = svalBuilder.evalBinOpNN(state, BO_Add,
                                                        *lenValNL,
                                                        *dstStrLengthNL,
                                                        sizeTy);
          boundWarning = "Size argument is greater than the free space in the "
                         "destination buffer";
        }

      } else {
        // For strncpy, this is just checking that lenVal <= sizeof(dst)
        // (Yes, strncpy and strncat differ in how they treat termination.
        // strncat ALWAYS terminates, but strncpy doesn't.)

        // We need a special case for when the copy size is zero, in which
        // case strncpy will do no work at all. Our bounds check uses n-1
        // as the last element accessed, so n == 0 is problematic.
        ProgramStateRef StateZeroSize, StateNonZeroSize;
        std::tie(StateZeroSize, StateNonZeroSize) =
          assumeZero(C, state, *lenValNL, sizeTy);

        // If the size is known to be zero, we're done.
        if (StateZeroSize && !StateNonZeroSize) {
          if (returnPtr) {
            StateZeroSize = StateZeroSize->BindExpr(CE, LCtx, DstVal);
          } else {
            StateZeroSize = StateZeroSize->BindExpr(CE, LCtx, *lenValNL);
          }
          C.addTransition(StateZeroSize);
          return;
        }

        // Otherwise, go ahead and figure out the last element we'll touch.
        // We don't record the non-zero assumption here because we can't
        // be sure. We won't warn on a possible zero.
        NonLoc one = svalBuilder.makeIntVal(1, sizeTy).castAs<NonLoc>();
        maxLastElementIndex = svalBuilder.evalBinOpNN(state, BO_Sub, *lenValNL,
                                                      one, sizeTy);
        boundWarning = "Size argument is greater than the length of the "
                       "destination buffer";
      }
    }

    // If we couldn't pin down the copy length, at least bound it.
    // FIXME: We should actually run this code path for append as well, but
    // right now it creates problems with constraints (since we can end up
    // trying to pass constraints from symbol to symbol).
    if (amountCopied.isUnknown() && !isAppending) {
      // Try to get a "hypothetical" string length symbol, which we can later
      // set as a real value if that turns out to be the case.
      amountCopied = getCStringLength(C, state, lenExpr, srcVal, true);
      assert(!amountCopied.isUndef());

      if (Optional<NonLoc> amountCopiedNL = amountCopied.getAs<NonLoc>()) {
        if (lenValNL) {
          // amountCopied <= lenVal
          SVal copiedLessThanBound = svalBuilder.evalBinOpNN(state, BO_LE,
                                                             *amountCopiedNL,
                                                             *lenValNL,
                                                             cmpTy);
          state = state->assume(
              copiedLessThanBound.castAs<DefinedOrUnknownSVal>(), true);
          if (!state)
            return;
        }

        if (strLengthNL) {
          // amountCopied <= strlen(source)
          SVal copiedLessThanSrc = svalBuilder.evalBinOpNN(state, BO_LE,
                                                           *amountCopiedNL,
                                                           *strLengthNL,
                                                           cmpTy);
          state = state->assume(
              copiedLessThanSrc.castAs<DefinedOrUnknownSVal>(), true);
          if (!state)
            return;
        }
      }
    }

  } else {
    // The function isn't bounded. The amount copied should match the length
    // of the source buffer.
    amountCopied = strLength;
  }

  assert(state);

  // This represents the number of characters copied into the destination
  // buffer. (It may not actually be the strlen if the destination buffer
  // is not terminated.)
  SVal finalStrLength = UnknownVal();

  // If this is an appending function (strcat, strncat...) then set the
  // string length to strlen(src) + strlen(dst) since the buffer will
  // ultimately contain both.
  if (isAppending) {
    // Get the string length of the destination. If the destination is memory
    // that can't have a string length, we shouldn't be copying into it anyway.
    SVal dstStrLength = getCStringLength(C, state, Dst, DstVal);
    if (dstStrLength.isUndef())
      return;

    Optional<NonLoc> srcStrLengthNL = amountCopied.getAs<NonLoc>();
    Optional<NonLoc> dstStrLengthNL = dstStrLength.getAs<NonLoc>();

    // If we know both string lengths, we might know the final string length.
    if (srcStrLengthNL && dstStrLengthNL) {
      // Make sure the two lengths together don't overflow a size_t.
      state = checkAdditionOverflow(C, state, *srcStrLengthNL, *dstStrLengthNL);
      if (!state)
        return;

      finalStrLength = svalBuilder.evalBinOpNN(state, BO_Add, *srcStrLengthNL,
                                               *dstStrLengthNL, sizeTy);
    }

    // If we couldn't get a single value for the final string length,
    // we can at least bound it by the individual lengths.
    if (finalStrLength.isUnknown()) {
      // Try to get a "hypothetical" string length symbol, which we can later
      // set as a real value if that turns out to be the case.
      finalStrLength = getCStringLength(C, state, CE, DstVal, true);
      assert(!finalStrLength.isUndef());

      if (Optional<NonLoc> finalStrLengthNL = finalStrLength.getAs<NonLoc>()) {
        if (srcStrLengthNL) {
          // finalStrLength >= srcStrLength
          SVal sourceInResult = svalBuilder.evalBinOpNN(state, BO_GE,
                                                        *finalStrLengthNL,
                                                        *srcStrLengthNL,
                                                        cmpTy);
          state = state->assume(sourceInResult.castAs<DefinedOrUnknownSVal>(),
                                true);
          if (!state)
            return;
        }

        if (dstStrLengthNL) {
          // finalStrLength >= dstStrLength
          SVal destInResult = svalBuilder.evalBinOpNN(state, BO_GE,
                                                      *finalStrLengthNL,
                                                      *dstStrLengthNL,
                                                      cmpTy);
          state =
              state->assume(destInResult.castAs<DefinedOrUnknownSVal>(), true);
          if (!state)
            return;
        }
      }
    }

  } else {
    // Otherwise, this is a copy-over function (strcpy, strncpy, ...), and
    // the final string length will match the input string length.
    finalStrLength = amountCopied;
  }

  SVal Result;

  if (returnPtr) {
    // The final result of the function will either be a pointer past the last
    // copied element, or a pointer to the start of the destination buffer.
    Result = (returnEnd ? UnknownVal() : DstVal);
  } else {
    Result = finalStrLength;
  }

  assert(state);

  // If the destination is a MemRegion, try to check for a buffer overflow and
  // record the new string length.
  if (Optional<loc::MemRegionVal> dstRegVal =
      DstVal.getAs<loc::MemRegionVal>()) {
    QualType ptrTy = Dst->getType();

    // If we have an exact value on a bounded copy, use that to check for
    // overflows, rather than our estimate about how much is actually copied.
    if (boundWarning) {
      if (Optional<NonLoc> maxLastNL = maxLastElementIndex.getAs<NonLoc>()) {
        SVal maxLastElement = svalBuilder.evalBinOpLN(state, BO_Add, *dstRegVal,
            *maxLastNL, ptrTy);
        state = CheckLocation(C, state, CE->getArg(2), maxLastElement,
            boundWarning);
        if (!state)
          return;
      }
    }

    // Then, if the final length is known...
    if (Optional<NonLoc> knownStrLength = finalStrLength.getAs<NonLoc>()) {
      SVal lastElement = svalBuilder.evalBinOpLN(state, BO_Add, *dstRegVal,
          *knownStrLength, ptrTy);

      // ...and we haven't checked the bound, we'll check the actual copy.
      if (!boundWarning) {
        const char * const warningMsg =
          "String copy function overflows destination buffer";
        state = CheckLocation(C, state, Dst, lastElement, warningMsg);
        if (!state)
          return;
      }

      // If this is a stpcpy-style copy, the last element is the return value.
      if (returnPtr && returnEnd)
        Result = lastElement;
    }

    // Invalidate the destination (regular invalidation without pointer-escaping
    // the address of the top-level region). This must happen before we set the
    // C string length because invalidation will clear the length.
    // FIXME: Even if we can't perfectly model the copy, we should see if we
    // can use LazyCompoundVals to copy the source values into the destination.
    // This would probably remove any existing bindings past the end of the
    // string, but that's still an improvement over blank invalidation.
    state = InvalidateBuffer(C, state, Dst, *dstRegVal,
        /*IsSourceBuffer*/false, nullptr);

    // Invalidate the source (const-invalidation without const-pointer-escaping
    // the address of the top-level region).
    state = InvalidateBuffer(C, state, srcExpr, srcVal, /*IsSourceBuffer*/true,
        nullptr);

    // Set the C string length of the destination, if we know it.
    if (isBounded && !isAppending) {
      // strncpy is annoying in that it doesn't guarantee to null-terminate
      // the result string. If the original string didn't fit entirely inside
      // the bound (including the null-terminator), we don't know how long the
      // result is.
      if (amountCopied != strLength)
        finalStrLength = UnknownVal();
    }
    state = setCStringLength(state, dstRegVal->getRegion(), finalStrLength);
  }

  assert(state);

  if (returnPtr) {
    // If this is a stpcpy-style copy, but we were unable to check for a buffer
    // overflow, we still need a result. Conjure a return value.
    if (returnEnd && Result.isUnknown()) {
      Result = svalBuilder.conjureSymbolVal(nullptr, CE, LCtx, C.blockCount());
    }
  }
  // Set the return value.
  state = state->BindExpr(CE, LCtx, Result);
  C.addTransition(state);
}

void CStringChecker::evalStrcmp(CheckerContext &C, const CallExpr *CE) const {
  if (CE->getNumArgs() < 2)
    return;

  //int strcmp(const char *s1, const char *s2);
  evalStrcmpCommon(C, CE, /* isBounded = */ false, /* ignoreCase = */ false);
}

void CStringChecker::evalStrncmp(CheckerContext &C, const CallExpr *CE) const {
  if (CE->getNumArgs() < 3)
    return;

  //int strncmp(const char *s1, const char *s2, size_t n);
  evalStrcmpCommon(C, CE, /* isBounded = */ true, /* ignoreCase = */ false);
}

void CStringChecker::evalStrcasecmp(CheckerContext &C,
    const CallExpr *CE) const {
  if (CE->getNumArgs() < 2)
    return;

  //int strcasecmp(const char *s1, const char *s2);
  evalStrcmpCommon(C, CE, /* isBounded = */ false, /* ignoreCase = */ true);
}

void CStringChecker::evalStrncasecmp(CheckerContext &C,
    const CallExpr *CE) const {
  if (CE->getNumArgs() < 3)
    return;

  //int strncasecmp(const char *s1, const char *s2, size_t n);
  evalStrcmpCommon(C, CE, /* isBounded = */ true, /* ignoreCase = */ true);
}

void CStringChecker::evalStrcmpCommon(CheckerContext &C, const CallExpr *CE,
    bool isBounded, bool ignoreCase) const {
  CurrentFunctionDescription = "string comparison function";
  ProgramStateRef state = C.getState();
  const LocationContext *LCtx = C.getLocationContext();

  // Check that the first string is non-null
  const Expr *s1 = CE->getArg(0);
  SVal s1Val = state->getSVal(s1, LCtx);
  state = checkNonNull(C, state, s1, s1Val);
  if (!state)
    return;

  // Check that the second string is non-null.
  const Expr *s2 = CE->getArg(1);
  SVal s2Val = state->getSVal(s2, LCtx);
  state = checkNonNull(C, state, s2, s2Val);
  if (!state)
    return;

  // Get the string length of the first string or give up.
  SVal s1Length = getCStringLength(C, state, s1, s1Val);
  if (s1Length.isUndef())
    return;

  // Get the string length of the second string or give up.
  SVal s2Length = getCStringLength(C, state, s2, s2Val);
  if (s2Length.isUndef())
    return;

  // If we know the two buffers are the same, we know the result is 0.
  // First, get the two buffers' addresses. Another checker will have already
  // made sure they're not undefined.
  DefinedOrUnknownSVal LV = s1Val.castAs<DefinedOrUnknownSVal>();
  DefinedOrUnknownSVal RV = s2Val.castAs<DefinedOrUnknownSVal>();

  // See if they are the same.
  SValBuilder &svalBuilder = C.getSValBuilder();
  DefinedOrUnknownSVal SameBuf = svalBuilder.evalEQ(state, LV, RV);
  ProgramStateRef StSameBuf, StNotSameBuf;
  std::tie(StSameBuf, StNotSameBuf) = state->assume(SameBuf);

  // If the two arguments might be the same buffer, we know the result is 0,
  // and we only need to check one size.
  if (StSameBuf) {
    StSameBuf = StSameBuf->BindExpr(CE, LCtx,
        svalBuilder.makeZeroVal(CE->getType()));
    C.addTransition(StSameBuf);

    // If the two arguments are GUARANTEED to be the same, we're done!
    if (!StNotSameBuf)
      return;
  }

  assert(StNotSameBuf);
  state = StNotSameBuf;

  // At this point we can go about comparing the two buffers.
  // For now, we only do this if they're both known string literals.

  // Attempt to extract string literals from both expressions.
  const StringLiteral *s1StrLiteral = getCStringLiteral(C, state, s1, s1Val);
  const StringLiteral *s2StrLiteral = getCStringLiteral(C, state, s2, s2Val);
  bool canComputeResult = false;
  SVal resultVal = svalBuilder.conjureSymbolVal(nullptr, CE, LCtx,
      C.blockCount());

  if (s1StrLiteral && s2StrLiteral) {
    StringRef s1StrRef = s1StrLiteral->getString();
    StringRef s2StrRef = s2StrLiteral->getString();

    if (isBounded) {
      // Get the max number of characters to compare.
      const Expr *lenExpr = CE->getArg(2);
      SVal lenVal = state->getSVal(lenExpr, LCtx);

      // If the length is known, we can get the right substrings.
      if (const llvm::APSInt *len = svalBuilder.getKnownValue(state, lenVal)) {
        // Create substrings of each to compare the prefix.
        s1StrRef = s1StrRef.substr(0, (size_t)len->getZExtValue());
        s2StrRef = s2StrRef.substr(0, (size_t)len->getZExtValue());
        canComputeResult = true;
      }
    } else {
      // This is a normal, unbounded strcmp.
      canComputeResult = true;
    }

    if (canComputeResult) {
      // Real strcmp stops at null characters.
      size_t s1Term = s1StrRef.find('\0');
      if (s1Term != StringRef::npos)
        s1StrRef = s1StrRef.substr(0, s1Term);

      size_t s2Term = s2StrRef.find('\0');
      if (s2Term != StringRef::npos)
        s2StrRef = s2StrRef.substr(0, s2Term);

      // Use StringRef's comparison methods to compute the actual result.
      int compareRes = ignoreCase ? s1StrRef.compare_lower(s2StrRef)
        : s1StrRef.compare(s2StrRef);

      // The strcmp function returns an integer greater than, equal to, or less
      // than zero, [c11, p7.24.4.2].
      if (compareRes == 0) {
        resultVal = svalBuilder.makeIntVal(compareRes, CE->getType());
      }
      else {
        DefinedSVal zeroVal = svalBuilder.makeIntVal(0, CE->getType());
        // Constrain strcmp's result range based on the result of StringRef's
        // comparison methods.
        BinaryOperatorKind op = (compareRes == 1) ? BO_GT : BO_LT;
        SVal compareWithZero =
          svalBuilder.evalBinOp(state, op, resultVal, zeroVal,
              svalBuilder.getConditionType());
        DefinedSVal compareWithZeroVal = compareWithZero.castAs<DefinedSVal>();
        state = state->assume(compareWithZeroVal, true);
      }
    }
  }

  state = state->BindExpr(CE, LCtx, resultVal);

  // Record this as a possible path.
  C.addTransition(state);
}

void CStringChecker::evalStrsep(CheckerContext &C, const CallExpr *CE) const {
  //char *strsep(char **stringp, const char *delim);
  if (CE->getNumArgs() < 2)
    return;

  // Sanity: does the search string parameter match the return type?
  const Expr *SearchStrPtr = CE->getArg(0);
  QualType CharPtrTy = SearchStrPtr->getType()->getPointeeType();
  if (CharPtrTy.isNull() ||
      CE->getType().getUnqualifiedType() != CharPtrTy.getUnqualifiedType())
    return;

  CurrentFunctionDescription = "strsep()";
  ProgramStateRef State = C.getState();
  const LocationContext *LCtx = C.getLocationContext();

  // Check that the search string pointer is non-null (though it may point to
  // a null string).
  SVal SearchStrVal = State->getSVal(SearchStrPtr, LCtx);
  State = checkNonNull(C, State, SearchStrPtr, SearchStrVal);
  if (!State)
    return;

  // Check that the delimiter string is non-null.
  const Expr *DelimStr = CE->getArg(1);
  SVal DelimStrVal = State->getSVal(DelimStr, LCtx);
  State = checkNonNull(C, State, DelimStr, DelimStrVal);
  if (!State)
    return;

  SValBuilder &SVB = C.getSValBuilder();
  SVal Result;
  if (Optional<Loc> SearchStrLoc = SearchStrVal.getAs<Loc>()) {
    // Get the current value of the search string pointer, as a char*.
    Result = State->getSVal(*SearchStrLoc, CharPtrTy);

    // Invalidate the search string, representing the change of one delimiter
    // character to NUL.
    State = InvalidateBuffer(C, State, SearchStrPtr, Result,
        /*IsSourceBuffer*/false, nullptr);

    // Overwrite the search string pointer. The new value is either an address
    // further along in the same string, or NULL if there are no more tokens.
    State = State->bindLoc(*SearchStrLoc,
        SVB.conjureSymbolVal(getTag(),
          CE,
          LCtx,
          CharPtrTy,
          C.blockCount()),
        LCtx);
  } else {
    assert(SearchStrVal.isUnknown());
    // Conjure a symbolic value. It's the best we can do.
    Result = SVB.conjureSymbolVal(nullptr, CE, LCtx, C.blockCount());
  }

  // Set the return value, and finish.
  State = State->BindExpr(CE, LCtx, Result);
  C.addTransition(State);
}

// These should probably be moved into a C++ standard library checker.
void CStringChecker::evalStdCopy(CheckerContext &C, const CallExpr *CE) const {
  evalStdCopyCommon(C, CE);
}

void CStringChecker::evalStdCopyBackward(CheckerContext &C,
    const CallExpr *CE) const {
  evalStdCopyCommon(C, CE);
}

void CStringChecker::evalStdCopyCommon(CheckerContext &C,
    const CallExpr *CE) const {
  if (CE->getNumArgs() < 3)
    return;

  ProgramStateRef State = C.getState();

  const LocationContext *LCtx = C.getLocationContext();

  // template <class _InputIterator, class _OutputIterator>
  // _OutputIterator
  // copy(_InputIterator __first, _InputIterator __last,
  //        _OutputIterator __result)

  // Invalidate the destination buffer
  const Expr *Dst = CE->getArg(2);
  SVal DstVal = State->getSVal(Dst, LCtx);
  State = InvalidateBuffer(C, State, Dst, DstVal, /*IsSource=*/false,
      /*Size=*/nullptr);

  SValBuilder &SVB = C.getSValBuilder();

  SVal ResultVal = SVB.conjureSymbolVal(nullptr, CE, LCtx, C.blockCount());
  State = State->BindExpr(CE, LCtx, ResultVal);

  C.addTransition(State);
}

void CStringChecker::evalMemset(CheckerContext &C, const CallExpr *CE) const {
  if (CE->getNumArgs() != 3)
    return;

  CurrentFunctionDescription = "memory set function";

  const Expr *Mem = CE->getArg(0);
  const Expr *CharE = CE->getArg(1);
  const Expr *Size = CE->getArg(2);
  ProgramStateRef State = C.getState();

  // See if the size argument is zero.
  const LocationContext *LCtx = C.getLocationContext();
  SVal SizeVal = State->getSVal(Size, LCtx);
  QualType SizeTy = Size->getType();

  ProgramStateRef StateZeroSize, StateNonZeroSize;
  std::tie(StateZeroSize, StateNonZeroSize) =
    assumeZero(C, State, SizeVal, SizeTy);

  // Get the value of the memory area.
  SVal MemVal = State->getSVal(Mem, LCtx);

  // If the size is zero, there won't be any actual memory access, so
  // just bind the return value to the Mem buffer and return.
  if (StateZeroSize && !StateNonZeroSize) {
    StateZeroSize = StateZeroSize->BindExpr(CE, LCtx, MemVal);
    C.addTransition(StateZeroSize);
    return;
  }

  // Ensure the memory area is not null.
  // If it is NULL there will be a NULL pointer dereference.
  State = checkNonNull(C, StateNonZeroSize, Mem, MemVal);
  if (!State)
    return;

  State = CheckBufferAccess(C, State, Size, Mem);
  if (!State)
    return;

  // According to the values of the arguments, bind the value of the second
  // argument to the destination buffer and set string length, or just
  // invalidate the destination buffer.
  if (!memsetAux(Mem, C.getSVal(CharE), Size, C, State))
    return;

  State = State->BindExpr(CE, LCtx, MemVal);
  C.addTransition(State);
}

void CStringChecker::evalBzero(CheckerContext &C, const CallExpr *CE) const {
  if (CE->getNumArgs() != 2)
    return;

  CurrentFunctionDescription = "memory clearance function";

  const Expr *Mem = CE->getArg(0);
  const Expr *Size = CE->getArg(1);
  SVal Zero = C.getSValBuilder().makeZeroVal(C.getASTContext().IntTy);

  ProgramStateRef State = C.getState();
  
  // See if the size argument is zero.
  SVal SizeVal = C.getSVal(Size);
  QualType SizeTy = Size->getType();

  ProgramStateRef StateZeroSize, StateNonZeroSize;
  std::tie(StateZeroSize, StateNonZeroSize) =
    assumeZero(C, State, SizeVal, SizeTy);

  // If the size is zero, there won't be any actual memory access,
  // In this case we just return.
  if (StateZeroSize && !StateNonZeroSize) {
    C.addTransition(StateZeroSize);
    return;
  }

  // Get the value of the memory area.
  SVal MemVal = C.getSVal(Mem);

  // Ensure the memory area is not null.
  // If it is NULL there will be a NULL pointer dereference.
  State = checkNonNull(C, StateNonZeroSize, Mem, MemVal);
  if (!State)
    return;

  State = CheckBufferAccess(C, State, Size, Mem);
  if (!State)
    return;

  if (!memsetAux(Mem, Zero, Size, C, State))
    return;

  C.addTransition(State);
}

static bool isCPPStdLibraryFunction(const FunctionDecl *FD, StringRef Name) {
  IdentifierInfo *II = FD->getIdentifier();
  if (!II)
    return false;

  if (!AnalysisDeclContext::isInStdNamespace(FD))
    return false;

  if (II->getName().equals(Name))
    return true;

  return false;
}
//===----------------------------------------------------------------------===//
// The driver method, and other Checker callbacks.
//===----------------------------------------------------------------------===//

static CStringChecker::FnCheck identifyCall(const CallExpr *CE,
                                            CheckerContext &C) {
  const FunctionDecl *FDecl = C.getCalleeDecl(CE);
  if (!FDecl)
    return nullptr;

  // Pro-actively check that argument types are safe to do arithmetic upon.
  // We do not want to crash if someone accidentally passes a structure
  // into, say, a C++ overload of any of these functions.
  if (isCPPStdLibraryFunction(FDecl, "copy")) {
    if (CE->getNumArgs() < 3 || !CE->getArg(2)->getType()->isPointerType())
      return nullptr;
    return &CStringChecker::evalStdCopy;
  } else if (isCPPStdLibraryFunction(FDecl, "copy_backward")) {
    if (CE->getNumArgs() < 3 || !CE->getArg(2)->getType()->isPointerType())
      return nullptr;
    return &CStringChecker::evalStdCopyBackward;
  } else {
    // An umbrella check for all C library functions.
    for (auto I: CE->arguments()) {
      QualType T = I->getType();
      if (!T->isIntegralOrEnumerationType() && !T->isPointerType())
        return nullptr;
    }
  }

  // FIXME: Poorly-factored string switches are slow.
  if (C.isCLibraryFunction(FDecl, "memcpy"))
    return &CStringChecker::evalMemcpy;
  else if (C.isCLibraryFunction(FDecl, "mempcpy"))
    return &CStringChecker::evalMempcpy;
  else if (C.isCLibraryFunction(FDecl, "memcmp"))
    return &CStringChecker::evalMemcmp;
  else if (C.isCLibraryFunction(FDecl, "memmove"))
    return &CStringChecker::evalMemmove;
  else if (C.isCLibraryFunction(FDecl, "memset") ||
           C.isCLibraryFunction(FDecl, "explicit_memset"))
    return &CStringChecker::evalMemset;
  else if (C.isCLibraryFunction(FDecl, "strcpy"))
    return &CStringChecker::evalStrcpy;
  else if (C.isCLibraryFunction(FDecl, "strncpy"))
    return &CStringChecker::evalStrncpy;
  else if (C.isCLibraryFunction(FDecl, "stpcpy"))
    return &CStringChecker::evalStpcpy;
  else if (C.isCLibraryFunction(FDecl, "strlcpy"))
    return &CStringChecker::evalStrlcpy;
  else if (C.isCLibraryFunction(FDecl, "strcat"))
    return &CStringChecker::evalStrcat;
  else if (C.isCLibraryFunction(FDecl, "strncat"))
    return &CStringChecker::evalStrncat;
  else if (C.isCLibraryFunction(FDecl, "strlcat"))
    return &CStringChecker::evalStrlcat;
  else if (C.isCLibraryFunction(FDecl, "strlen"))
    return &CStringChecker::evalstrLength;
  else if (C.isCLibraryFunction(FDecl, "strnlen"))
    return &CStringChecker::evalstrnLength;
  else if (C.isCLibraryFunction(FDecl, "strcmp"))
    return &CStringChecker::evalStrcmp;
  else if (C.isCLibraryFunction(FDecl, "strncmp"))
    return &CStringChecker::evalStrncmp;
  else if (C.isCLibraryFunction(FDecl, "strcasecmp"))
    return &CStringChecker::evalStrcasecmp;
  else if (C.isCLibraryFunction(FDecl, "strncasecmp"))
    return &CStringChecker::evalStrncasecmp;
  else if (C.isCLibraryFunction(FDecl, "strsep"))
    return &CStringChecker::evalStrsep;
  else if (C.isCLibraryFunction(FDecl, "bcopy"))
    return &CStringChecker::evalBcopy;
  else if (C.isCLibraryFunction(FDecl, "bcmp"))
    return &CStringChecker::evalMemcmp;
  else if (C.isCLibraryFunction(FDecl, "bzero") ||
           C.isCLibraryFunction(FDecl, "explicit_bzero"))
    return &CStringChecker::evalBzero;

  return nullptr;
}

bool CStringChecker::evalCall(const CallExpr *CE, CheckerContext &C) const {

  FnCheck evalFunction = identifyCall(CE, C);

  // If the callee isn't a string function, let another checker handle it.
  if (!evalFunction)
    return false;

  // Check and evaluate the call.
  (this->*evalFunction)(C, CE);

  // If the evaluate call resulted in no change, chain to the next eval call
  // handler.
  // Note, the custom CString evaluation calls assume that basic safety
  // properties are held. However, if the user chooses to turn off some of these
  // checks, we ignore the issues and leave the call evaluation to a generic
  // handler.
  return C.isDifferent();
}

void CStringChecker::checkPreStmt(const DeclStmt *DS, CheckerContext &C) const {
  // Record string length for char a[] = "abc";
  ProgramStateRef state = C.getState();

  for (const auto *I : DS->decls()) {
    const VarDecl *D = dyn_cast<VarDecl>(I);
    if (!D)
      continue;

    // FIXME: Handle array fields of structs.
    if (!D->getType()->isArrayType())
      continue;

    const Expr *Init = D->getInit();
    if (!Init)
      continue;
    if (!isa<StringLiteral>(Init))
      continue;

    Loc VarLoc = state->getLValue(D, C.getLocationContext());
    const MemRegion *MR = VarLoc.getAsRegion();
    if (!MR)
      continue;

    SVal StrVal = C.getSVal(Init);
    assert(StrVal.isValid() && "Initializer string is unknown or undefined");
    DefinedOrUnknownSVal strLength =
      getCStringLength(C, state, Init, StrVal).castAs<DefinedOrUnknownSVal>();

    state = state->set<CStringLength>(MR, strLength);
  }

  C.addTransition(state);
}

ProgramStateRef
CStringChecker::checkRegionChanges(ProgramStateRef state,
    const InvalidatedSymbols *,
    ArrayRef<const MemRegion *> ExplicitRegions,
    ArrayRef<const MemRegion *> Regions,
    const LocationContext *LCtx,
    const CallEvent *Call) const {
  CStringLengthTy Entries = state->get<CStringLength>();
  if (Entries.isEmpty())
    return state;

  llvm::SmallPtrSet<const MemRegion *, 8> Invalidated;
  llvm::SmallPtrSet<const MemRegion *, 32> SuperRegions;

  // First build sets for the changed regions and their super-regions.
  for (ArrayRef<const MemRegion *>::iterator
      I = Regions.begin(), E = Regions.end(); I != E; ++I) {
    const MemRegion *MR = *I;
    Invalidated.insert(MR);

    SuperRegions.insert(MR);
    while (const SubRegion *SR = dyn_cast<SubRegion>(MR)) {
      MR = SR->getSuperRegion();
      SuperRegions.insert(MR);
    }
  }

  CStringLengthTy::Factory &F = state->get_context<CStringLength>();

  // Then loop over the entries in the current state.
  for (CStringLengthTy::iterator I = Entries.begin(),
      E = Entries.end(); I != E; ++I) {
    const MemRegion *MR = I.getKey();

    // Is this entry for a super-region of a changed region?
    if (SuperRegions.count(MR)) {
      Entries = F.remove(Entries, MR);
      continue;
    }

    // Is this entry for a sub-region of a changed region?
    const MemRegion *Super = MR;
    while (const SubRegion *SR = dyn_cast<SubRegion>(Super)) {
      Super = SR->getSuperRegion();
      if (Invalidated.count(Super)) {
        Entries = F.remove(Entries, MR);
        break;
      }
    }
  }

  return state->set<CStringLength>(Entries);
}

void CStringChecker::checkLiveSymbols(ProgramStateRef state,
    SymbolReaper &SR) const {
  // Mark all symbols in our string length map as valid.
  CStringLengthTy Entries = state->get<CStringLength>();

  for (CStringLengthTy::iterator I = Entries.begin(), E = Entries.end();
      I != E; ++I) {
    SVal Len = I.getData();

    for (SymExpr::symbol_iterator si = Len.symbol_begin(),
        se = Len.symbol_end(); si != se; ++si)
      SR.markInUse(*si);
  }
}

void CStringChecker::checkDeadSymbols(SymbolReaper &SR,
    CheckerContext &C) const {
  ProgramStateRef state = C.getState();
  CStringLengthTy Entries = state->get<CStringLength>();
  if (Entries.isEmpty())
    return;

  CStringLengthTy::Factory &F = state->get_context<CStringLength>();
  for (CStringLengthTy::iterator I = Entries.begin(), E = Entries.end();
      I != E; ++I) {
    SVal Len = I.getData();
    if (SymbolRef Sym = Len.getAsSymbol()) {
      if (SR.isDead(Sym))
        Entries = F.remove(Entries, I.getKey());
    }
  }

  state = state->set<CStringLength>(Entries);
  C.addTransition(state);
}

#define REGISTER_CHECKER(name)                                                 \
  void ento::register##name(CheckerManager &mgr) {                             \
    CStringChecker *checker = mgr.registerChecker<CStringChecker>();           \
    checker->Filter.Check##name = true;                                        \
    checker->Filter.CheckName##name = mgr.getCurrentCheckName();               \
  }

  REGISTER_CHECKER(CStringNullArg)
  REGISTER_CHECKER(CStringOutOfBounds)
  REGISTER_CHECKER(CStringBufferOverlap)
REGISTER_CHECKER(CStringNotNullTerm)

  void ento::registerCStringCheckerBasic(CheckerManager &Mgr) {
    Mgr.registerChecker<CStringChecker>();
  }
