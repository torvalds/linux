//=== StdLibraryFunctionsChecker.cpp - Model standard functions -*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This checker improves modeling of a few simple library functions.
// It does not generate warnings.
//
// This checker provides a specification format - `FunctionSummaryTy' - and
// contains descriptions of some library functions in this format. Each
// specification contains a list of branches for splitting the program state
// upon call, and range constraints on argument and return-value symbols that
// are satisfied on each branch. This spec can be expanded to include more
// items, like external effects of the function.
//
// The main difference between this approach and the body farms technique is
// in more explicit control over how many branches are produced. For example,
// consider standard C function `ispunct(int x)', which returns a non-zero value
// iff `x' is a punctuation character, that is, when `x' is in range
//   ['!', '/']   [':', '@']  U  ['[', '\`']  U  ['{', '~'].
// `FunctionSummaryTy' provides only two branches for this function. However,
// any attempt to describe this range with if-statements in the body farm
// would result in many more branches. Because each branch needs to be analyzed
// independently, this significantly reduces performance. Additionally,
// once we consider a branch on which `x' is in range, say, ['!', '/'],
// we assume that such branch is an important separate path through the program,
// which may lead to false positives because considering this particular path
// was not consciously intended, and therefore it might have been unreachable.
//
// This checker uses eval::Call for modeling "pure" functions, for which
// their `FunctionSummaryTy' is a precise model. This avoids unnecessary
// invalidation passes. Conflicts with other checkers are unlikely because
// if the function has no other effects, other checkers would probably never
// want to improve upon the modeling done by this checker.
//
// Non-"pure" functions, for which only partial improvement over the default
// behavior is expected, are modeled via check::PostCall, non-intrusively.
//
// The following standard C functions are currently supported:
//
//   fgetc      getline   isdigit   isupper
//   fread      isalnum   isgraph   isxdigit
//   fwrite     isalpha   islower   read
//   getc       isascii   isprint   write
//   getchar    isblank   ispunct
//   getdelim   iscntrl   isspace
//
//===----------------------------------------------------------------------===//

#include "clang/StaticAnalyzer/Checkers/BuiltinCheckerRegistration.h"
#include "clang/StaticAnalyzer/Core/Checker.h"
#include "clang/StaticAnalyzer/Core/CheckerManager.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/CallEvent.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/CheckerContext.h"

using namespace clang;
using namespace clang::ento;

namespace {
class StdLibraryFunctionsChecker : public Checker<check::PostCall, eval::Call> {
  /// Below is a series of typedefs necessary to define function specs.
  /// We avoid nesting types here because each additional qualifier
  /// would need to be repeated in every function spec.
  struct FunctionSummaryTy;

  /// Specify how much the analyzer engine should entrust modeling this function
  /// to us. If he doesn't, he performs additional invalidations.
  enum InvalidationKindTy { NoEvalCall, EvalCallAsPure };

  /// A pair of ValueRangeKindTy and IntRangeVectorTy would describe a range
  /// imposed on a particular argument or return value symbol.
  ///
  /// Given a range, should the argument stay inside or outside this range?
  /// The special `ComparesToArgument' value indicates that we should
  /// impose a constraint that involves other argument or return value symbols.
  enum ValueRangeKindTy { OutOfRange, WithinRange, ComparesToArgument };

  // The universal integral type to use in value range descriptions.
  // Unsigned to make sure overflows are well-defined.
  typedef uint64_t RangeIntTy;

  /// Normally, describes a single range constraint, eg. {{0, 1}, {3, 4}} is
  /// a non-negative integer, which less than 5 and not equal to 2. For
  /// `ComparesToArgument', holds information about how exactly to compare to
  /// the argument.
  typedef std::vector<std::pair<RangeIntTy, RangeIntTy>> IntRangeVectorTy;

  /// A reference to an argument or return value by its number.
  /// ArgNo in CallExpr and CallEvent is defined as Unsigned, but
  /// obviously uint32_t should be enough for all practical purposes.
  typedef uint32_t ArgNoTy;
  static const ArgNoTy Ret = std::numeric_limits<ArgNoTy>::max();

  /// Incapsulates a single range on a single symbol within a branch.
  class ValueRange {
    ArgNoTy ArgNo; // Argument to which we apply the range.
    ValueRangeKindTy Kind; // Kind of range definition.
    IntRangeVectorTy Args; // Polymorphic arguments.

  public:
    ValueRange(ArgNoTy ArgNo, ValueRangeKindTy Kind,
               const IntRangeVectorTy &Args)
        : ArgNo(ArgNo), Kind(Kind), Args(Args) {}

    ArgNoTy getArgNo() const { return ArgNo; }
    ValueRangeKindTy getKind() const { return Kind; }

    BinaryOperator::Opcode getOpcode() const {
      assert(Kind == ComparesToArgument);
      assert(Args.size() == 1);
      BinaryOperator::Opcode Op =
          static_cast<BinaryOperator::Opcode>(Args[0].first);
      assert(BinaryOperator::isComparisonOp(Op) &&
             "Only comparison ops are supported for ComparesToArgument");
      return Op;
    }

    ArgNoTy getOtherArgNo() const {
      assert(Kind == ComparesToArgument);
      assert(Args.size() == 1);
      return static_cast<ArgNoTy>(Args[0].second);
    }

    const IntRangeVectorTy &getRanges() const {
      assert(Kind != ComparesToArgument);
      return Args;
    }

    // We avoid creating a virtual apply() method because
    // it makes initializer lists harder to write.
  private:
    ProgramStateRef
    applyAsOutOfRange(ProgramStateRef State, const CallEvent &Call,
                      const FunctionSummaryTy &Summary) const;
    ProgramStateRef
    applyAsWithinRange(ProgramStateRef State, const CallEvent &Call,
                       const FunctionSummaryTy &Summary) const;
    ProgramStateRef
    applyAsComparesToArgument(ProgramStateRef State, const CallEvent &Call,
                              const FunctionSummaryTy &Summary) const;

  public:
    ProgramStateRef apply(ProgramStateRef State, const CallEvent &Call,
                          const FunctionSummaryTy &Summary) const {
      switch (Kind) {
      case OutOfRange:
        return applyAsOutOfRange(State, Call, Summary);
      case WithinRange:
        return applyAsWithinRange(State, Call, Summary);
      case ComparesToArgument:
        return applyAsComparesToArgument(State, Call, Summary);
      }
      llvm_unreachable("Unknown ValueRange kind!");
    }
  };

  /// The complete list of ranges that defines a single branch.
  typedef std::vector<ValueRange> ValueRangeSet;

  /// Includes information about function prototype (which is necessary to
  /// ensure we're modeling the right function and casting values properly),
  /// approach to invalidation, and a list of branches - essentially, a list
  /// of list of ranges - essentially, a list of lists of lists of segments.
  struct FunctionSummaryTy {
    const std::vector<QualType> ArgTypes;
    const QualType RetType;
    const InvalidationKindTy InvalidationKind;
    const std::vector<ValueRangeSet> Ranges;

  private:
    static void assertTypeSuitableForSummary(QualType T) {
      assert(!T->isVoidType() &&
             "We should have had no significant void types in the spec");
      assert(T.isCanonical() &&
             "We should only have canonical types in the spec");
      // FIXME: lift this assert (but not the ones above!)
      assert(T->isIntegralOrEnumerationType() &&
             "We only support integral ranges in the spec");
    }

  public:
    QualType getArgType(ArgNoTy ArgNo) const {
      QualType T = (ArgNo == Ret) ? RetType : ArgTypes[ArgNo];
      assertTypeSuitableForSummary(T);
      return T;
    }

    /// Try our best to figure out if the call expression is the call of
    /// *the* library function to which this specification applies.
    bool matchesCall(const CallExpr *CE) const;
  };

  // The same function (as in, function identifier) may have different
  // summaries assigned to it, with different argument and return value types.
  // We call these "variants" of the function. This can be useful for handling
  // C++ function overloads, and also it can be used when the same function
  // may have different definitions on different platforms.
  typedef std::vector<FunctionSummaryTy> FunctionVariantsTy;

  // The map of all functions supported by the checker. It is initialized
  // lazily, and it doesn't change after initialization.
  typedef llvm::StringMap<FunctionVariantsTy> FunctionSummaryMapTy;
  mutable FunctionSummaryMapTy FunctionSummaryMap;

  // Auxiliary functions to support ArgNoTy within all structures
  // in a unified manner.
  static QualType getArgType(const FunctionSummaryTy &Summary, ArgNoTy ArgNo) {
    return Summary.getArgType(ArgNo);
  }
  static QualType getArgType(const CallEvent &Call, ArgNoTy ArgNo) {
    return ArgNo == Ret ? Call.getResultType().getCanonicalType()
                        : Call.getArgExpr(ArgNo)->getType().getCanonicalType();
  }
  static QualType getArgType(const CallExpr *CE, ArgNoTy ArgNo) {
    return ArgNo == Ret ? CE->getType().getCanonicalType()
                        : CE->getArg(ArgNo)->getType().getCanonicalType();
  }
  static SVal getArgSVal(const CallEvent &Call, ArgNoTy ArgNo) {
    return ArgNo == Ret ? Call.getReturnValue() : Call.getArgSVal(ArgNo);
  }

public:
  void checkPostCall(const CallEvent &Call, CheckerContext &C) const;
  bool evalCall(const CallExpr *CE, CheckerContext &C) const;

private:
  Optional<FunctionSummaryTy> findFunctionSummary(const FunctionDecl *FD,
                                          const CallExpr *CE,
                                          CheckerContext &C) const;

  void initFunctionSummaries(BasicValueFactory &BVF) const;
};
} // end of anonymous namespace

ProgramStateRef StdLibraryFunctionsChecker::ValueRange::applyAsOutOfRange(
    ProgramStateRef State, const CallEvent &Call,
    const FunctionSummaryTy &Summary) const {

  ProgramStateManager &Mgr = State->getStateManager();
  SValBuilder &SVB = Mgr.getSValBuilder();
  BasicValueFactory &BVF = SVB.getBasicValueFactory();
  ConstraintManager &CM = Mgr.getConstraintManager();
  QualType T = getArgType(Summary, getArgNo());
  SVal V = getArgSVal(Call, getArgNo());

  if (auto N = V.getAs<NonLoc>()) {
    const IntRangeVectorTy &R = getRanges();
    size_t E = R.size();
    for (size_t I = 0; I != E; ++I) {
      const llvm::APSInt &Min = BVF.getValue(R[I].first, T);
      const llvm::APSInt &Max = BVF.getValue(R[I].second, T);
      assert(Min <= Max);
      State = CM.assumeInclusiveRange(State, *N, Min, Max, false);
      if (!State)
        break;
    }
  }

  return State;
}

ProgramStateRef
StdLibraryFunctionsChecker::ValueRange::applyAsWithinRange(
    ProgramStateRef State, const CallEvent &Call,
    const FunctionSummaryTy &Summary) const {

  ProgramStateManager &Mgr = State->getStateManager();
  SValBuilder &SVB = Mgr.getSValBuilder();
  BasicValueFactory &BVF = SVB.getBasicValueFactory();
  ConstraintManager &CM = Mgr.getConstraintManager();
  QualType T = getArgType(Summary, getArgNo());
  SVal V = getArgSVal(Call, getArgNo());

  // "WithinRange R" is treated as "outside [T_MIN, T_MAX] \ R".
  // We cut off [T_MIN, min(R) - 1] and [max(R) + 1, T_MAX] if necessary,
  // and then cut away all holes in R one by one.
  if (auto N = V.getAs<NonLoc>()) {
    const IntRangeVectorTy &R = getRanges();
    size_t E = R.size();

    const llvm::APSInt &MinusInf = BVF.getMinValue(T);
    const llvm::APSInt &PlusInf = BVF.getMaxValue(T);

    const llvm::APSInt &Left = BVF.getValue(R[0].first - 1ULL, T);
    if (Left != PlusInf) {
      assert(MinusInf <= Left);
      State = CM.assumeInclusiveRange(State, *N, MinusInf, Left, false);
      if (!State)
        return nullptr;
    }

    const llvm::APSInt &Right = BVF.getValue(R[E - 1].second + 1ULL, T);
    if (Right != MinusInf) {
      assert(Right <= PlusInf);
      State = CM.assumeInclusiveRange(State, *N, Right, PlusInf, false);
      if (!State)
        return nullptr;
    }

    for (size_t I = 1; I != E; ++I) {
      const llvm::APSInt &Min = BVF.getValue(R[I - 1].second + 1ULL, T);
      const llvm::APSInt &Max = BVF.getValue(R[I].first - 1ULL, T);
      assert(Min <= Max);
      State = CM.assumeInclusiveRange(State, *N, Min, Max, false);
      if (!State)
        return nullptr;
    }
  }

  return State;
}

ProgramStateRef
StdLibraryFunctionsChecker::ValueRange::applyAsComparesToArgument(
    ProgramStateRef State, const CallEvent &Call,
    const FunctionSummaryTy &Summary) const {

  ProgramStateManager &Mgr = State->getStateManager();
  SValBuilder &SVB = Mgr.getSValBuilder();
  QualType CondT = SVB.getConditionType();
  QualType T = getArgType(Summary, getArgNo());
  SVal V = getArgSVal(Call, getArgNo());

  BinaryOperator::Opcode Op = getOpcode();
  ArgNoTy OtherArg = getOtherArgNo();
  SVal OtherV = getArgSVal(Call, OtherArg);
  QualType OtherT = getArgType(Call, OtherArg);
  // Note: we avoid integral promotion for comparison.
  OtherV = SVB.evalCast(OtherV, T, OtherT);
  if (auto CompV = SVB.evalBinOp(State, Op, V, OtherV, CondT)
                       .getAs<DefinedOrUnknownSVal>())
    State = State->assume(*CompV, true);
  return State;
}

void StdLibraryFunctionsChecker::checkPostCall(const CallEvent &Call,
                                               CheckerContext &C) const {
  const FunctionDecl *FD = dyn_cast_or_null<FunctionDecl>(Call.getDecl());
  if (!FD)
    return;

  const CallExpr *CE = dyn_cast_or_null<CallExpr>(Call.getOriginExpr());
  if (!CE)
    return;

  Optional<FunctionSummaryTy> FoundSummary = findFunctionSummary(FD, CE, C);
  if (!FoundSummary)
    return;

  // Now apply ranges.
  const FunctionSummaryTy &Summary = *FoundSummary;
  ProgramStateRef State = C.getState();

  for (const auto &VRS: Summary.Ranges) {
    ProgramStateRef NewState = State;
    for (const auto &VR: VRS) {
      NewState = VR.apply(NewState, Call, Summary);
      if (!NewState)
        break;
    }

    if (NewState && NewState != State)
      C.addTransition(NewState);
  }
}

bool StdLibraryFunctionsChecker::evalCall(const CallExpr *CE,
                                          CheckerContext &C) const {
  const FunctionDecl *FD = dyn_cast_or_null<FunctionDecl>(CE->getCalleeDecl());
  if (!FD)
    return false;

  Optional<FunctionSummaryTy> FoundSummary = findFunctionSummary(FD, CE, C);
  if (!FoundSummary)
    return false;

  const FunctionSummaryTy &Summary = *FoundSummary;
  switch (Summary.InvalidationKind) {
  case EvalCallAsPure: {
    ProgramStateRef State = C.getState();
    const LocationContext *LC = C.getLocationContext();
    SVal V = C.getSValBuilder().conjureSymbolVal(
        CE, LC, CE->getType().getCanonicalType(), C.blockCount());
    State = State->BindExpr(CE, LC, V);
    C.addTransition(State);
    return true;
  }
  case NoEvalCall:
    // Summary tells us to avoid performing eval::Call. The function is possibly
    // evaluated by another checker, or evaluated conservatively.
    return false;
  }
  llvm_unreachable("Unknown invalidation kind!");
}

bool StdLibraryFunctionsChecker::FunctionSummaryTy::matchesCall(
    const CallExpr *CE) const {
  // Check number of arguments:
  if (CE->getNumArgs() != ArgTypes.size())
    return false;

  // Check return type if relevant:
  if (!RetType.isNull() && RetType != CE->getType().getCanonicalType())
    return false;

  // Check argument types when relevant:
  for (size_t I = 0, E = ArgTypes.size(); I != E; ++I) {
    QualType FormalT = ArgTypes[I];
    // Null type marks irrelevant arguments.
    if (FormalT.isNull())
      continue;

    assertTypeSuitableForSummary(FormalT);

    QualType ActualT = StdLibraryFunctionsChecker::getArgType(CE, I);
    assert(ActualT.isCanonical());
    if (ActualT != FormalT)
      return false;
  }

  return true;
}

Optional<StdLibraryFunctionsChecker::FunctionSummaryTy>
StdLibraryFunctionsChecker::findFunctionSummary(const FunctionDecl *FD,
                                                const CallExpr *CE,
                                                CheckerContext &C) const {
  // Note: we cannot always obtain FD from CE
  // (eg. virtual call, or call by pointer).
  assert(CE);

  if (!FD)
    return None;

  SValBuilder &SVB = C.getSValBuilder();
  BasicValueFactory &BVF = SVB.getBasicValueFactory();
  initFunctionSummaries(BVF);

  IdentifierInfo *II = FD->getIdentifier();
  if (!II)
    return None;
  StringRef Name = II->getName();
  if (Name.empty() || !C.isCLibraryFunction(FD, Name))
    return None;

  auto FSMI = FunctionSummaryMap.find(Name);
  if (FSMI == FunctionSummaryMap.end())
    return None;

  // Verify that function signature matches the spec in advance.
  // Otherwise we might be modeling the wrong function.
  // Strict checking is important because we will be conducting
  // very integral-type-sensitive operations on arguments and
  // return values.
  const FunctionVariantsTy &SpecVariants = FSMI->second;
  for (const FunctionSummaryTy &Spec : SpecVariants)
    if (Spec.matchesCall(CE))
      return Spec;

  return None;
}

void StdLibraryFunctionsChecker::initFunctionSummaries(
    BasicValueFactory &BVF) const {
  if (!FunctionSummaryMap.empty())
    return;

  ASTContext &ACtx = BVF.getContext();

  // These types are useful for writing specifications quickly,
  // New specifications should probably introduce more types.
  // Some types are hard to obtain from the AST, eg. "ssize_t".
  // In such cases it should be possible to provide multiple variants
  // of function summary for common cases (eg. ssize_t could be int or long
  // or long long, so three summary variants would be enough).
  // Of course, function variants are also useful for C++ overloads.
  QualType Irrelevant; // A placeholder, whenever we do not care about the type.
  QualType IntTy = ACtx.IntTy;
  QualType LongTy = ACtx.LongTy;
  QualType LongLongTy = ACtx.LongLongTy;
  QualType SizeTy = ACtx.getSizeType();

  RangeIntTy IntMax = BVF.getMaxValue(IntTy).getLimitedValue();
  RangeIntTy LongMax = BVF.getMaxValue(LongTy).getLimitedValue();
  RangeIntTy LongLongMax = BVF.getMaxValue(LongLongTy).getLimitedValue();

  // We are finally ready to define specifications for all supported functions.
  //
  // The signature needs to have the correct number of arguments.
  // However, we insert `Irrelevant' when the type is insignificant.
  //
  // Argument ranges should always cover all variants. If return value
  // is completely unknown, omit it from the respective range set.
  //
  // All types in the spec need to be canonical.
  //
  // Every item in the list of range sets represents a particular
  // execution path the analyzer would need to explore once
  // the call is modeled - a new program state is constructed
  // for every range set, and each range line in the range set
  // corresponds to a specific constraint within this state.
  //
  // Upon comparing to another argument, the other argument is casted
  // to the current argument's type. This avoids proper promotion but
  // seems useful. For example, read() receives size_t argument,
  // and its return value, which is of type ssize_t, cannot be greater
  // than this argument. If we made a promotion, and the size argument
  // is equal to, say, 10, then we'd impose a range of [0, 10] on the
  // return value, however the correct range is [-1, 10].
  //
  // Please update the list of functions in the header after editing!
  //
  // The format is as follows:
  //
  //{ "function name",
  //  { spec:
  //    { argument types list, ... },
  //    return type, purity, { range set list:
  //      { range list:
  //        { argument index, within or out of, {{from, to}, ...} },
  //        { argument index, compares to argument, {{how, which}} },
  //        ...
  //      }
  //    }
  //  }
  //}

#define SUMMARY_WITH_VARIANTS(identifier) {#identifier, {
#define END_SUMMARY_WITH_VARIANTS }},
#define VARIANT(argument_types, return_type, invalidation_approach)            \
  { argument_types, return_type, invalidation_approach, {
#define END_VARIANT } },
#define SUMMARY(identifier, argument_types, return_type,                       \
                invalidation_approach)                                         \
  { #identifier, { { argument_types, return_type, invalidation_approach, {
#define END_SUMMARY } } } },
#define ARGUMENT_TYPES(...) { __VA_ARGS__ }
#define RETURN_TYPE(x) x
#define INVALIDATION_APPROACH(x) x
#define CASE {
#define END_CASE },
#define ARGUMENT_CONDITION(argument_number, condition_kind)                    \
  { argument_number, condition_kind, {
#define END_ARGUMENT_CONDITION }},
#define RETURN_VALUE_CONDITION(condition_kind)                                 \
  { Ret, condition_kind, {
#define END_RETURN_VALUE_CONDITION }},
#define ARG_NO(x) x##U
#define RANGE(x, y) { x, y },
#define SINGLE_VALUE(x) RANGE(x, x)
#define IS_LESS_THAN(arg) { BO_LE, arg }

  FunctionSummaryMap = {
    // The isascii() family of functions.
    SUMMARY(isalnum, ARGUMENT_TYPES(IntTy), RETURN_TYPE(IntTy),
            INVALIDATION_APPROACH(EvalCallAsPure))
      CASE // Boils down to isupper() or islower() or isdigit()
        ARGUMENT_CONDITION(ARG_NO(0), WithinRange)
          RANGE('0', '9')
          RANGE('A', 'Z')
          RANGE('a', 'z')
        END_ARGUMENT_CONDITION
        RETURN_VALUE_CONDITION(OutOfRange)
          SINGLE_VALUE(0)
        END_RETURN_VALUE_CONDITION
      END_CASE
      CASE // The locale-specific range.
        ARGUMENT_CONDITION(ARG_NO(0), WithinRange)
          RANGE(128, 255)
        END_ARGUMENT_CONDITION
        // No post-condition. We are completely unaware of
        // locale-specific return values.
      END_CASE
      CASE
        ARGUMENT_CONDITION(ARG_NO(0), OutOfRange)
          RANGE('0', '9')
          RANGE('A', 'Z')
          RANGE('a', 'z')
          RANGE(128, 255)
        END_ARGUMENT_CONDITION
        RETURN_VALUE_CONDITION(WithinRange)
          SINGLE_VALUE(0)
        END_RETURN_VALUE_CONDITION
      END_CASE
    END_SUMMARY
    SUMMARY(isalpha, ARGUMENT_TYPES(IntTy), RETURN_TYPE(IntTy),
            INVALIDATION_APPROACH(EvalCallAsPure))
      CASE // isupper() or islower(). Note that 'Z' is less than 'a'.
        ARGUMENT_CONDITION(ARG_NO(0), WithinRange)
          RANGE('A', 'Z')
          RANGE('a', 'z')
        END_ARGUMENT_CONDITION
        RETURN_VALUE_CONDITION(OutOfRange)
          SINGLE_VALUE(0)
        END_RETURN_VALUE_CONDITION
      END_CASE
      CASE // The locale-specific range.
        ARGUMENT_CONDITION(ARG_NO(0), WithinRange)
          RANGE(128, 255)
        END_ARGUMENT_CONDITION
      END_CASE
      CASE // Other.
        ARGUMENT_CONDITION(ARG_NO(0), OutOfRange)
          RANGE('A', 'Z')
          RANGE('a', 'z')
          RANGE(128, 255)
        END_ARGUMENT_CONDITION
        RETURN_VALUE_CONDITION(WithinRange)
          SINGLE_VALUE(0)
        END_RETURN_VALUE_CONDITION
      END_CASE
    END_SUMMARY
    SUMMARY(isascii, ARGUMENT_TYPES(IntTy), RETURN_TYPE(IntTy),
            INVALIDATION_APPROACH(EvalCallAsPure))
      CASE // Is ASCII.
        ARGUMENT_CONDITION(ARG_NO(0), WithinRange)
          RANGE(0, 127)
        END_ARGUMENT_CONDITION
        RETURN_VALUE_CONDITION(OutOfRange)
          SINGLE_VALUE(0)
        END_RETURN_VALUE_CONDITION
      END_CASE
      CASE
        ARGUMENT_CONDITION(ARG_NO(0), OutOfRange)
          RANGE(0, 127)
        END_ARGUMENT_CONDITION
        RETURN_VALUE_CONDITION(WithinRange)
          SINGLE_VALUE(0)
        END_RETURN_VALUE_CONDITION
      END_CASE
    END_SUMMARY
    SUMMARY(isblank, ARGUMENT_TYPES(IntTy), RETURN_TYPE(IntTy),
            INVALIDATION_APPROACH(EvalCallAsPure))
      CASE
        ARGUMENT_CONDITION(ARG_NO(0), WithinRange)
          SINGLE_VALUE('\t')
          SINGLE_VALUE(' ')
        END_ARGUMENT_CONDITION
        RETURN_VALUE_CONDITION(OutOfRange)
          SINGLE_VALUE(0)
        END_RETURN_VALUE_CONDITION
      END_CASE
      CASE
        ARGUMENT_CONDITION(ARG_NO(0), OutOfRange)
          SINGLE_VALUE('\t')
          SINGLE_VALUE(' ')
        END_ARGUMENT_CONDITION
        RETURN_VALUE_CONDITION(WithinRange)
          SINGLE_VALUE(0)
        END_RETURN_VALUE_CONDITION
      END_CASE
    END_SUMMARY
    SUMMARY(iscntrl, ARGUMENT_TYPES(IntTy), RETURN_TYPE(IntTy),
            INVALIDATION_APPROACH(EvalCallAsPure))
      CASE // 0..31 or 127
        ARGUMENT_CONDITION(ARG_NO(0), WithinRange)
          RANGE(0, 32)
          SINGLE_VALUE(127)
        END_ARGUMENT_CONDITION
        RETURN_VALUE_CONDITION(OutOfRange)
          SINGLE_VALUE(0)
        END_RETURN_VALUE_CONDITION
      END_CASE
      CASE
        ARGUMENT_CONDITION(ARG_NO(0), OutOfRange)
          RANGE(0, 32)
          SINGLE_VALUE(127)
        END_ARGUMENT_CONDITION
        RETURN_VALUE_CONDITION(WithinRange)
          SINGLE_VALUE(0)
        END_RETURN_VALUE_CONDITION
      END_CASE
    END_SUMMARY
    SUMMARY(isdigit, ARGUMENT_TYPES(IntTy), RETURN_TYPE(IntTy),
            INVALIDATION_APPROACH(EvalCallAsPure))
      CASE // Is a digit.
        ARGUMENT_CONDITION(ARG_NO(0), WithinRange)
          RANGE('0', '9')
        END_ARGUMENT_CONDITION
        RETURN_VALUE_CONDITION(OutOfRange)
          SINGLE_VALUE(0)
        END_RETURN_VALUE_CONDITION
      END_CASE
      CASE
        ARGUMENT_CONDITION(ARG_NO(0), OutOfRange)
          RANGE('0', '9')
        END_ARGUMENT_CONDITION
        RETURN_VALUE_CONDITION(WithinRange)
          SINGLE_VALUE(0)
        END_RETURN_VALUE_CONDITION
      END_CASE
    END_SUMMARY
    SUMMARY(isgraph, ARGUMENT_TYPES(IntTy), RETURN_TYPE(IntTy),
            INVALIDATION_APPROACH(EvalCallAsPure))
      CASE
        ARGUMENT_CONDITION(ARG_NO(0), WithinRange)
          RANGE(33, 126)
        END_ARGUMENT_CONDITION
        RETURN_VALUE_CONDITION(OutOfRange)
          SINGLE_VALUE(0)
        END_RETURN_VALUE_CONDITION
      END_CASE
      CASE
        ARGUMENT_CONDITION(ARG_NO(0), OutOfRange)
          RANGE(33, 126)
        END_ARGUMENT_CONDITION
        RETURN_VALUE_CONDITION(WithinRange)
          SINGLE_VALUE(0)
        END_RETURN_VALUE_CONDITION
      END_CASE
    END_SUMMARY
    SUMMARY(islower, ARGUMENT_TYPES(IntTy), RETURN_TYPE(IntTy),
            INVALIDATION_APPROACH(EvalCallAsPure))
      CASE // Is certainly lowercase.
        ARGUMENT_CONDITION(ARG_NO(0), WithinRange)
          RANGE('a', 'z')
        END_ARGUMENT_CONDITION
        RETURN_VALUE_CONDITION(OutOfRange)
          SINGLE_VALUE(0)
        END_RETURN_VALUE_CONDITION
      END_CASE
      CASE // Is ascii but not lowercase.
        ARGUMENT_CONDITION(ARG_NO(0), WithinRange)
          RANGE(0, 127)
        END_ARGUMENT_CONDITION
        ARGUMENT_CONDITION(ARG_NO(0), OutOfRange)
          RANGE('a', 'z')
        END_ARGUMENT_CONDITION
        RETURN_VALUE_CONDITION(WithinRange)
          SINGLE_VALUE(0)
        END_RETURN_VALUE_CONDITION
      END_CASE
      CASE // The locale-specific range.
        ARGUMENT_CONDITION(ARG_NO(0), WithinRange)
          RANGE(128, 255)
        END_ARGUMENT_CONDITION
      END_CASE
      CASE // Is not an unsigned char.
        ARGUMENT_CONDITION(ARG_NO(0), OutOfRange)
          RANGE(0, 255)
        END_ARGUMENT_CONDITION
        RETURN_VALUE_CONDITION(WithinRange)
          SINGLE_VALUE(0)
        END_RETURN_VALUE_CONDITION
      END_CASE
    END_SUMMARY
    SUMMARY(isprint, ARGUMENT_TYPES(IntTy), RETURN_TYPE(IntTy),
            INVALIDATION_APPROACH(EvalCallAsPure))
      CASE
        ARGUMENT_CONDITION(ARG_NO(0), WithinRange)
          RANGE(32, 126)
        END_ARGUMENT_CONDITION
        RETURN_VALUE_CONDITION(OutOfRange)
          SINGLE_VALUE(0)
        END_RETURN_VALUE_CONDITION
      END_CASE
      CASE
        ARGUMENT_CONDITION(ARG_NO(0), OutOfRange)
          RANGE(32, 126)
        END_ARGUMENT_CONDITION
        RETURN_VALUE_CONDITION(WithinRange)
          SINGLE_VALUE(0)
        END_RETURN_VALUE_CONDITION
      END_CASE
    END_SUMMARY
    SUMMARY(ispunct, ARGUMENT_TYPES(IntTy), RETURN_TYPE(IntTy),
            INVALIDATION_APPROACH(EvalCallAsPure))
      CASE
        ARGUMENT_CONDITION(ARG_NO(0), WithinRange)
          RANGE('!', '/')
          RANGE(':', '@')
          RANGE('[', '`')
          RANGE('{', '~')
        END_ARGUMENT_CONDITION
        RETURN_VALUE_CONDITION(OutOfRange)
          SINGLE_VALUE(0)
        END_RETURN_VALUE_CONDITION
      END_CASE
      CASE
        ARGUMENT_CONDITION(ARG_NO(0), OutOfRange)
          RANGE('!', '/')
          RANGE(':', '@')
          RANGE('[', '`')
          RANGE('{', '~')
        END_ARGUMENT_CONDITION
        RETURN_VALUE_CONDITION(WithinRange)
          SINGLE_VALUE(0)
        END_RETURN_VALUE_CONDITION
      END_CASE
    END_SUMMARY
    SUMMARY(isspace, ARGUMENT_TYPES(IntTy), RETURN_TYPE(IntTy),
            INVALIDATION_APPROACH(EvalCallAsPure))
      CASE // Space, '\f', '\n', '\r', '\t', '\v'.
        ARGUMENT_CONDITION(ARG_NO(0), WithinRange)
          RANGE(9, 13)
          SINGLE_VALUE(' ')
        END_ARGUMENT_CONDITION
        RETURN_VALUE_CONDITION(OutOfRange)
          SINGLE_VALUE(0)
        END_RETURN_VALUE_CONDITION
      END_CASE
      CASE // The locale-specific range.
        ARGUMENT_CONDITION(ARG_NO(0), WithinRange)
          RANGE(128, 255)
        END_ARGUMENT_CONDITION
      END_CASE
      CASE
        ARGUMENT_CONDITION(ARG_NO(0), OutOfRange)
          RANGE(9, 13)
          SINGLE_VALUE(' ')
          RANGE(128, 255)
        END_ARGUMENT_CONDITION
        RETURN_VALUE_CONDITION(WithinRange)
          SINGLE_VALUE(0)
        END_RETURN_VALUE_CONDITION
      END_CASE
    END_SUMMARY
    SUMMARY(isupper, ARGUMENT_TYPES(IntTy), RETURN_TYPE (IntTy),
            INVALIDATION_APPROACH(EvalCallAsPure))
      CASE // Is certainly uppercase.
        ARGUMENT_CONDITION(ARG_NO(0), WithinRange)
          RANGE('A', 'Z')
        END_ARGUMENT_CONDITION
        RETURN_VALUE_CONDITION(OutOfRange)
          SINGLE_VALUE(0)
        END_RETURN_VALUE_CONDITION
      END_CASE
      CASE // The locale-specific range.
        ARGUMENT_CONDITION(ARG_NO(0), WithinRange)
          RANGE(128, 255)
        END_ARGUMENT_CONDITION
      END_CASE
      CASE // Other.
        ARGUMENT_CONDITION(ARG_NO(0), OutOfRange)
          RANGE('A', 'Z') RANGE(128, 255)
        END_ARGUMENT_CONDITION
        RETURN_VALUE_CONDITION(WithinRange)
          SINGLE_VALUE(0)
        END_RETURN_VALUE_CONDITION
      END_CASE
    END_SUMMARY
    SUMMARY(isxdigit, ARGUMENT_TYPES(IntTy), RETURN_TYPE(IntTy),
            INVALIDATION_APPROACH(EvalCallAsPure))
      CASE
        ARGUMENT_CONDITION(ARG_NO(0), WithinRange)
          RANGE('0', '9')
          RANGE('A', 'F')
          RANGE('a', 'f')
        END_ARGUMENT_CONDITION
        RETURN_VALUE_CONDITION(OutOfRange)
          SINGLE_VALUE(0)
        END_RETURN_VALUE_CONDITION
      END_CASE
      CASE
        ARGUMENT_CONDITION(ARG_NO(0), OutOfRange)
          RANGE('0', '9')
          RANGE('A', 'F')
          RANGE('a', 'f')
        END_ARGUMENT_CONDITION
        RETURN_VALUE_CONDITION(WithinRange)
          SINGLE_VALUE(0)
        END_RETURN_VALUE_CONDITION
      END_CASE
    END_SUMMARY

    // The getc() family of functions that returns either a char or an EOF.
    SUMMARY(getc, ARGUMENT_TYPES(Irrelevant), RETURN_TYPE(IntTy),
            INVALIDATION_APPROACH(NoEvalCall))
      CASE // FIXME: EOF is assumed to be defined as -1.
        RETURN_VALUE_CONDITION(WithinRange)
          RANGE(-1, 255)
        END_RETURN_VALUE_CONDITION
      END_CASE
    END_SUMMARY
    SUMMARY(fgetc, ARGUMENT_TYPES(Irrelevant), RETURN_TYPE(IntTy),
            INVALIDATION_APPROACH(NoEvalCall))
      CASE // FIXME: EOF is assumed to be defined as -1.
        RETURN_VALUE_CONDITION(WithinRange)
          RANGE(-1, 255)
        END_RETURN_VALUE_CONDITION
      END_CASE
    END_SUMMARY
    SUMMARY(getchar, ARGUMENT_TYPES(), RETURN_TYPE(IntTy),
            INVALIDATION_APPROACH(NoEvalCall))
      CASE // FIXME: EOF is assumed to be defined as -1.
        RETURN_VALUE_CONDITION(WithinRange)
          RANGE(-1, 255)
        END_RETURN_VALUE_CONDITION
      END_CASE
    END_SUMMARY

    // read()-like functions that never return more than buffer size.
    // We are not sure how ssize_t is defined on every platform, so we provide
    // three variants that should cover common cases.
    SUMMARY_WITH_VARIANTS(read)
      VARIANT(ARGUMENT_TYPES(Irrelevant, Irrelevant, SizeTy),
              RETURN_TYPE(IntTy), INVALIDATION_APPROACH(NoEvalCall))
        CASE
          RETURN_VALUE_CONDITION(ComparesToArgument)
            IS_LESS_THAN(ARG_NO(2))
          END_RETURN_VALUE_CONDITION
          RETURN_VALUE_CONDITION(WithinRange)
            RANGE(-1, IntMax)
          END_RETURN_VALUE_CONDITION
        END_CASE
      END_VARIANT
      VARIANT(ARGUMENT_TYPES(Irrelevant, Irrelevant, SizeTy),
              RETURN_TYPE(LongTy), INVALIDATION_APPROACH(NoEvalCall))
        CASE
          RETURN_VALUE_CONDITION(ComparesToArgument)
            IS_LESS_THAN(ARG_NO(2))
          END_RETURN_VALUE_CONDITION
          RETURN_VALUE_CONDITION(WithinRange)
            RANGE(-1, LongMax)
          END_RETURN_VALUE_CONDITION
        END_CASE
      END_VARIANT
      VARIANT(ARGUMENT_TYPES(Irrelevant, Irrelevant, SizeTy),
              RETURN_TYPE(LongLongTy), INVALIDATION_APPROACH(NoEvalCall))
        CASE
          RETURN_VALUE_CONDITION(ComparesToArgument)
            IS_LESS_THAN(ARG_NO(2))
          END_RETURN_VALUE_CONDITION
          RETURN_VALUE_CONDITION(WithinRange)
            RANGE(-1, LongLongMax)
          END_RETURN_VALUE_CONDITION
        END_CASE
      END_VARIANT
    END_SUMMARY_WITH_VARIANTS
    SUMMARY_WITH_VARIANTS(write)
      // Again, due to elusive nature of ssize_t, we have duplicate
      // our summaries to cover different variants.
      VARIANT(ARGUMENT_TYPES(Irrelevant, Irrelevant, SizeTy),
              RETURN_TYPE(IntTy), INVALIDATION_APPROACH(NoEvalCall))
        CASE
          RETURN_VALUE_CONDITION(ComparesToArgument)
            IS_LESS_THAN(ARG_NO(2))
          END_RETURN_VALUE_CONDITION
          RETURN_VALUE_CONDITION(WithinRange)
            RANGE(-1, IntMax)
          END_RETURN_VALUE_CONDITION
        END_CASE
      END_VARIANT
      VARIANT(ARGUMENT_TYPES(Irrelevant, Irrelevant, SizeTy),
              RETURN_TYPE(LongTy), INVALIDATION_APPROACH(NoEvalCall))
        CASE
          RETURN_VALUE_CONDITION(ComparesToArgument)
            IS_LESS_THAN(ARG_NO(2))
          END_RETURN_VALUE_CONDITION
          RETURN_VALUE_CONDITION(WithinRange)
            RANGE(-1, LongMax)
          END_RETURN_VALUE_CONDITION
        END_CASE
      END_VARIANT
      VARIANT(ARGUMENT_TYPES(Irrelevant, Irrelevant, SizeTy),
              RETURN_TYPE(LongLongTy), INVALIDATION_APPROACH(NoEvalCall))
        CASE
          RETURN_VALUE_CONDITION(ComparesToArgument)
            IS_LESS_THAN(ARG_NO(2))
          END_RETURN_VALUE_CONDITION
          RETURN_VALUE_CONDITION(WithinRange)
            RANGE(-1, LongLongMax)
          END_RETURN_VALUE_CONDITION
        END_CASE
      END_VARIANT
    END_SUMMARY_WITH_VARIANTS
    SUMMARY(fread,
            ARGUMENT_TYPES(Irrelevant, Irrelevant, SizeTy, Irrelevant),
            RETURN_TYPE(SizeTy), INVALIDATION_APPROACH(NoEvalCall))
      CASE
        RETURN_VALUE_CONDITION(ComparesToArgument)
          IS_LESS_THAN(ARG_NO(2))
        END_RETURN_VALUE_CONDITION
      END_CASE
    END_SUMMARY
    SUMMARY(fwrite,
            ARGUMENT_TYPES(Irrelevant, Irrelevant, SizeTy, Irrelevant),
            RETURN_TYPE(SizeTy), INVALIDATION_APPROACH(NoEvalCall))
      CASE
        RETURN_VALUE_CONDITION(ComparesToArgument)
          IS_LESS_THAN(ARG_NO(2))
        END_RETURN_VALUE_CONDITION
      END_CASE
    END_SUMMARY

    // getline()-like functions either fail or read at least the delimiter.
    SUMMARY_WITH_VARIANTS(getline)
      VARIANT(ARGUMENT_TYPES(Irrelevant, Irrelevant, Irrelevant),
              RETURN_TYPE(IntTy), INVALIDATION_APPROACH(NoEvalCall))
        CASE
          RETURN_VALUE_CONDITION(WithinRange)
            SINGLE_VALUE(-1)
            RANGE(1, IntMax)
          END_RETURN_VALUE_CONDITION
        END_CASE
      END_VARIANT
      VARIANT(ARGUMENT_TYPES(Irrelevant, Irrelevant, Irrelevant),
              RETURN_TYPE(LongTy), INVALIDATION_APPROACH(NoEvalCall))
        CASE
          RETURN_VALUE_CONDITION(WithinRange)
            SINGLE_VALUE(-1)
            RANGE(1, LongMax)
          END_RETURN_VALUE_CONDITION
        END_CASE
      END_VARIANT
      VARIANT(ARGUMENT_TYPES(Irrelevant, Irrelevant, Irrelevant),
              RETURN_TYPE(LongLongTy), INVALIDATION_APPROACH(NoEvalCall))
        CASE
          RETURN_VALUE_CONDITION(WithinRange)
            SINGLE_VALUE(-1)
            RANGE(1, LongLongMax)
          END_RETURN_VALUE_CONDITION
        END_CASE
      END_VARIANT
    END_SUMMARY_WITH_VARIANTS
    SUMMARY_WITH_VARIANTS(getdelim)
      VARIANT(ARGUMENT_TYPES(Irrelevant, Irrelevant, Irrelevant, Irrelevant),
            RETURN_TYPE(IntTy), INVALIDATION_APPROACH(NoEvalCall))
        CASE
          RETURN_VALUE_CONDITION(WithinRange)
            SINGLE_VALUE(-1)
            RANGE(1, IntMax)
          END_RETURN_VALUE_CONDITION
        END_CASE
      END_VARIANT
      VARIANT(ARGUMENT_TYPES(Irrelevant, Irrelevant, Irrelevant, Irrelevant),
            RETURN_TYPE(LongTy), INVALIDATION_APPROACH(NoEvalCall))
        CASE
          RETURN_VALUE_CONDITION(WithinRange)
            SINGLE_VALUE(-1)
            RANGE(1, LongMax)
          END_RETURN_VALUE_CONDITION
        END_CASE
      END_VARIANT
      VARIANT(ARGUMENT_TYPES(Irrelevant, Irrelevant, Irrelevant, Irrelevant),
            RETURN_TYPE(LongLongTy), INVALIDATION_APPROACH(NoEvalCall))
        CASE
          RETURN_VALUE_CONDITION(WithinRange)
            SINGLE_VALUE(-1)
            RANGE(1, LongLongMax)
          END_RETURN_VALUE_CONDITION
        END_CASE
      END_VARIANT
    END_SUMMARY_WITH_VARIANTS
  };
}

void ento::registerStdCLibraryFunctionsChecker(CheckerManager &mgr) {
  // If this checker grows large enough to support C++, Objective-C, or other
  // standard libraries, we could use multiple register...Checker() functions,
  // which would register various checkers with the help of the same Checker
  // class, turning on different function summaries.
  mgr.registerChecker<StdLibraryFunctionsChecker>();
}
