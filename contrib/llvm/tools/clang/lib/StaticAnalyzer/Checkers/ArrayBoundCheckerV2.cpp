//== ArrayBoundCheckerV2.cpp ------------------------------------*- C++ -*--==//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file defines ArrayBoundCheckerV2, which is a path-sensitive check
// which looks for an out-of-bound array element access.
//
//===----------------------------------------------------------------------===//

#include "clang/StaticAnalyzer/Checkers/BuiltinCheckerRegistration.h"
#include "clang/AST/CharUnits.h"
#include "clang/StaticAnalyzer/Core/BugReporter/BugType.h"
#include "clang/StaticAnalyzer/Core/Checker.h"
#include "clang/StaticAnalyzer/Core/CheckerManager.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/APSIntType.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/CheckerContext.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/ExprEngine.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/Support/raw_ostream.h"

using namespace clang;
using namespace ento;

namespace {
class ArrayBoundCheckerV2 :
    public Checker<check::Location> {
  mutable std::unique_ptr<BuiltinBug> BT;

  enum OOB_Kind { OOB_Precedes, OOB_Excedes, OOB_Tainted };

  void reportOOB(CheckerContext &C, ProgramStateRef errorState, OOB_Kind kind,
                 std::unique_ptr<BugReporterVisitor> Visitor = nullptr) const;

public:
  void checkLocation(SVal l, bool isLoad, const Stmt*S,
                     CheckerContext &C) const;
};

// FIXME: Eventually replace RegionRawOffset with this class.
class RegionRawOffsetV2 {
private:
  const SubRegion *baseRegion;
  SVal byteOffset;

  RegionRawOffsetV2()
    : baseRegion(nullptr), byteOffset(UnknownVal()) {}

public:
  RegionRawOffsetV2(const SubRegion* base, SVal offset)
    : baseRegion(base), byteOffset(offset) {}

  NonLoc getByteOffset() const { return byteOffset.castAs<NonLoc>(); }
  const SubRegion *getRegion() const { return baseRegion; }

  static RegionRawOffsetV2 computeOffset(ProgramStateRef state,
                                         SValBuilder &svalBuilder,
                                         SVal location);

  void dump() const;
  void dumpToStream(raw_ostream &os) const;
};
}

static SVal computeExtentBegin(SValBuilder &svalBuilder,
                               const MemRegion *region) {
  const MemSpaceRegion *SR = region->getMemorySpace();
  if (SR->getKind() == MemRegion::UnknownSpaceRegionKind)
    return UnknownVal();
  else
    return svalBuilder.makeZeroArrayIndex();
}

// TODO: once the constraint manager is smart enough to handle non simplified
// symbolic expressions remove this function. Note that this can not be used in
// the constraint manager as is, since this does not handle overflows. It is
// safe to assume, however, that memory offsets will not overflow.
static std::pair<NonLoc, nonloc::ConcreteInt>
getSimplifiedOffsets(NonLoc offset, nonloc::ConcreteInt extent,
                     SValBuilder &svalBuilder) {
  Optional<nonloc::SymbolVal> SymVal = offset.getAs<nonloc::SymbolVal>();
  if (SymVal && SymVal->isExpression()) {
    if (const SymIntExpr *SIE = dyn_cast<SymIntExpr>(SymVal->getSymbol())) {
      llvm::APSInt constant =
          APSIntType(extent.getValue()).convert(SIE->getRHS());
      switch (SIE->getOpcode()) {
      case BO_Mul:
        // The constant should never be 0 here, since it the result of scaling
        // based on the size of a type which is never 0.
        if ((extent.getValue() % constant) != 0)
          return std::pair<NonLoc, nonloc::ConcreteInt>(offset, extent);
        else
          return getSimplifiedOffsets(
              nonloc::SymbolVal(SIE->getLHS()),
              svalBuilder.makeIntVal(extent.getValue() / constant),
              svalBuilder);
      case BO_Add:
        return getSimplifiedOffsets(
            nonloc::SymbolVal(SIE->getLHS()),
            svalBuilder.makeIntVal(extent.getValue() - constant), svalBuilder);
      default:
        break;
      }
    }
  }

  return std::pair<NonLoc, nonloc::ConcreteInt>(offset, extent);
}

void ArrayBoundCheckerV2::checkLocation(SVal location, bool isLoad,
                                        const Stmt* LoadS,
                                        CheckerContext &checkerContext) const {

  // NOTE: Instead of using ProgramState::assumeInBound(), we are prototyping
  // some new logic here that reasons directly about memory region extents.
  // Once that logic is more mature, we can bring it back to assumeInBound()
  // for all clients to use.
  //
  // The algorithm we are using here for bounds checking is to see if the
  // memory access is within the extent of the base region.  Since we
  // have some flexibility in defining the base region, we can achieve
  // various levels of conservatism in our buffer overflow checking.
  ProgramStateRef state = checkerContext.getState();

  SValBuilder &svalBuilder = checkerContext.getSValBuilder();
  const RegionRawOffsetV2 &rawOffset =
    RegionRawOffsetV2::computeOffset(state, svalBuilder, location);

  if (!rawOffset.getRegion())
    return;

  NonLoc rawOffsetVal = rawOffset.getByteOffset();

  // CHECK LOWER BOUND: Is byteOffset < extent begin?
  //  If so, we are doing a load/store
  //  before the first valid offset in the memory region.

  SVal extentBegin = computeExtentBegin(svalBuilder, rawOffset.getRegion());

  if (Optional<NonLoc> NV = extentBegin.getAs<NonLoc>()) {
    if (NV->getAs<nonloc::ConcreteInt>()) {
      std::pair<NonLoc, nonloc::ConcreteInt> simplifiedOffsets =
          getSimplifiedOffsets(rawOffset.getByteOffset(),
                               NV->castAs<nonloc::ConcreteInt>(),
                               svalBuilder);
      rawOffsetVal = simplifiedOffsets.first;
      *NV = simplifiedOffsets.second;
    }

    SVal lowerBound = svalBuilder.evalBinOpNN(state, BO_LT, rawOffsetVal, *NV,
                                              svalBuilder.getConditionType());

    Optional<NonLoc> lowerBoundToCheck = lowerBound.getAs<NonLoc>();
    if (!lowerBoundToCheck)
      return;

    ProgramStateRef state_precedesLowerBound, state_withinLowerBound;
    std::tie(state_precedesLowerBound, state_withinLowerBound) =
      state->assume(*lowerBoundToCheck);

    // Are we constrained enough to definitely precede the lower bound?
    if (state_precedesLowerBound && !state_withinLowerBound) {
      reportOOB(checkerContext, state_precedesLowerBound, OOB_Precedes);
      return;
    }

    // Otherwise, assume the constraint of the lower bound.
    assert(state_withinLowerBound);
    state = state_withinLowerBound;
  }

  do {
    // CHECK UPPER BOUND: Is byteOffset >= extent(baseRegion)?  If so,
    // we are doing a load/store after the last valid offset.
    DefinedOrUnknownSVal extentVal =
      rawOffset.getRegion()->getExtent(svalBuilder);
    if (!extentVal.getAs<NonLoc>())
      break;

    if (extentVal.getAs<nonloc::ConcreteInt>()) {
      std::pair<NonLoc, nonloc::ConcreteInt> simplifiedOffsets =
          getSimplifiedOffsets(rawOffset.getByteOffset(),
                               extentVal.castAs<nonloc::ConcreteInt>(),
                               svalBuilder);
      rawOffsetVal = simplifiedOffsets.first;
      extentVal = simplifiedOffsets.second;
    }

    SVal upperbound = svalBuilder.evalBinOpNN(state, BO_GE, rawOffsetVal,
                                              extentVal.castAs<NonLoc>(),
                                              svalBuilder.getConditionType());

    Optional<NonLoc> upperboundToCheck = upperbound.getAs<NonLoc>();
    if (!upperboundToCheck)
      break;

    ProgramStateRef state_exceedsUpperBound, state_withinUpperBound;
    std::tie(state_exceedsUpperBound, state_withinUpperBound) =
      state->assume(*upperboundToCheck);

    // If we are under constrained and the index variables are tainted, report.
    if (state_exceedsUpperBound && state_withinUpperBound) {
      SVal ByteOffset = rawOffset.getByteOffset();
      if (state->isTainted(ByteOffset)) {
        reportOOB(checkerContext, state_exceedsUpperBound, OOB_Tainted,
                  llvm::make_unique<TaintBugVisitor>(ByteOffset));
        return;
      }
    } else if (state_exceedsUpperBound) {
      // If we are constrained enough to definitely exceed the upper bound,
      // report.
      assert(!state_withinUpperBound);
      reportOOB(checkerContext, state_exceedsUpperBound, OOB_Excedes);
      return;
    }

    assert(state_withinUpperBound);
    state = state_withinUpperBound;
  }
  while (false);

  checkerContext.addTransition(state);
}

void ArrayBoundCheckerV2::reportOOB(
    CheckerContext &checkerContext, ProgramStateRef errorState, OOB_Kind kind,
    std::unique_ptr<BugReporterVisitor> Visitor) const {

  ExplodedNode *errorNode = checkerContext.generateErrorNode(errorState);
  if (!errorNode)
    return;

  if (!BT)
    BT.reset(new BuiltinBug(this, "Out-of-bound access"));

  // FIXME: This diagnostics are preliminary.  We should get far better
  // diagnostics for explaining buffer overruns.

  SmallString<256> buf;
  llvm::raw_svector_ostream os(buf);
  os << "Out of bound memory access ";
  switch (kind) {
  case OOB_Precedes:
    os << "(accessed memory precedes memory block)";
    break;
  case OOB_Excedes:
    os << "(access exceeds upper limit of memory block)";
    break;
  case OOB_Tainted:
    os << "(index is tainted)";
    break;
  }

  auto BR = llvm::make_unique<BugReport>(*BT, os.str(), errorNode);
  BR->addVisitor(std::move(Visitor));
  checkerContext.emitReport(std::move(BR));
}

#ifndef NDEBUG
LLVM_DUMP_METHOD void RegionRawOffsetV2::dump() const {
  dumpToStream(llvm::errs());
}

void RegionRawOffsetV2::dumpToStream(raw_ostream &os) const {
  os << "raw_offset_v2{" << getRegion() << ',' << getByteOffset() << '}';
}
#endif

// Lazily computes a value to be used by 'computeOffset'.  If 'val'
// is unknown or undefined, we lazily substitute '0'.  Otherwise,
// return 'val'.
static inline SVal getValue(SVal val, SValBuilder &svalBuilder) {
  return val.getAs<UndefinedVal>() ? svalBuilder.makeArrayIndex(0) : val;
}

// Scale a base value by a scaling factor, and return the scaled
// value as an SVal.  Used by 'computeOffset'.
static inline SVal scaleValue(ProgramStateRef state,
                              NonLoc baseVal, CharUnits scaling,
                              SValBuilder &sb) {
  return sb.evalBinOpNN(state, BO_Mul, baseVal,
                        sb.makeArrayIndex(scaling.getQuantity()),
                        sb.getArrayIndexType());
}

// Add an SVal to another, treating unknown and undefined values as
// summing to UnknownVal.  Used by 'computeOffset'.
static SVal addValue(ProgramStateRef state, SVal x, SVal y,
                     SValBuilder &svalBuilder) {
  // We treat UnknownVals and UndefinedVals the same here because we
  // only care about computing offsets.
  if (x.isUnknownOrUndef() || y.isUnknownOrUndef())
    return UnknownVal();

  return svalBuilder.evalBinOpNN(state, BO_Add, x.castAs<NonLoc>(),
                                 y.castAs<NonLoc>(),
                                 svalBuilder.getArrayIndexType());
}

/// Compute a raw byte offset from a base region.  Used for array bounds
/// checking.
RegionRawOffsetV2 RegionRawOffsetV2::computeOffset(ProgramStateRef state,
                                                   SValBuilder &svalBuilder,
                                                   SVal location)
{
  const MemRegion *region = location.getAsRegion();
  SVal offset = UndefinedVal();

  while (region) {
    switch (region->getKind()) {
      default: {
        if (const SubRegion *subReg = dyn_cast<SubRegion>(region)) {
          offset = getValue(offset, svalBuilder);
          if (!offset.isUnknownOrUndef())
            return RegionRawOffsetV2(subReg, offset);
        }
        return RegionRawOffsetV2();
      }
      case MemRegion::ElementRegionKind: {
        const ElementRegion *elemReg = cast<ElementRegion>(region);
        SVal index = elemReg->getIndex();
        if (!index.getAs<NonLoc>())
          return RegionRawOffsetV2();
        QualType elemType = elemReg->getElementType();
        // If the element is an incomplete type, go no further.
        ASTContext &astContext = svalBuilder.getContext();
        if (elemType->isIncompleteType())
          return RegionRawOffsetV2();

        // Update the offset.
        offset = addValue(state,
                          getValue(offset, svalBuilder),
                          scaleValue(state,
                          index.castAs<NonLoc>(),
                          astContext.getTypeSizeInChars(elemType),
                          svalBuilder),
                          svalBuilder);

        if (offset.isUnknownOrUndef())
          return RegionRawOffsetV2();

        region = elemReg->getSuperRegion();
        continue;
      }
    }
  }
  return RegionRawOffsetV2();
}

void ento::registerArrayBoundCheckerV2(CheckerManager &mgr) {
  mgr.registerChecker<ArrayBoundCheckerV2>();
}
