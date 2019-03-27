//===-- DependenceAnalysis.cpp - DA Implementation --------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// DependenceAnalysis is an LLVM pass that analyses dependences between memory
// accesses. Currently, it is an (incomplete) implementation of the approach
// described in
//
//            Practical Dependence Testing
//            Goff, Kennedy, Tseng
//            PLDI 1991
//
// There's a single entry point that analyzes the dependence between a pair
// of memory references in a function, returning either NULL, for no dependence,
// or a more-or-less detailed description of the dependence between them.
//
// Currently, the implementation cannot propagate constraints between
// coupled RDIV subscripts and lacks a multi-subscript MIV test.
// Both of these are conservative weaknesses;
// that is, not a source of correctness problems.
//
// Since Clang linearizes some array subscripts, the dependence
// analysis is using SCEV->delinearize to recover the representation of multiple
// subscripts, and thus avoid the more expensive and less precise MIV tests. The
// delinearization is controlled by the flag -da-delinearize.
//
// We should pay some careful attention to the possibility of integer overflow
// in the implementation of the various tests. This could happen with Add,
// Subtract, or Multiply, with both APInt's and SCEV's.
//
// Some non-linear subscript pairs can be handled by the GCD test
// (and perhaps other tests).
// Should explore how often these things occur.
//
// Finally, it seems like certain test cases expose weaknesses in the SCEV
// simplification, especially in the handling of sign and zero extensions.
// It could be useful to spend time exploring these.
//
// Please note that this is work in progress and the interface is subject to
// change.
//
//===----------------------------------------------------------------------===//
//                                                                            //
//                   In memory of Ken Kennedy, 1945 - 2007                    //
//                                                                            //
//===----------------------------------------------------------------------===//

#include "llvm/Analysis/DependenceAnalysis.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/Analysis/AliasAnalysis.h"
#include "llvm/Analysis/LoopInfo.h"
#include "llvm/Analysis/ScalarEvolution.h"
#include "llvm/Analysis/ScalarEvolutionExpressions.h"
#include "llvm/Analysis/ValueTracking.h"
#include "llvm/Config/llvm-config.h"
#include "llvm/IR/InstIterator.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Operator.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/raw_ostream.h"

using namespace llvm;

#define DEBUG_TYPE "da"

//===----------------------------------------------------------------------===//
// statistics

STATISTIC(TotalArrayPairs, "Array pairs tested");
STATISTIC(SeparableSubscriptPairs, "Separable subscript pairs");
STATISTIC(CoupledSubscriptPairs, "Coupled subscript pairs");
STATISTIC(NonlinearSubscriptPairs, "Nonlinear subscript pairs");
STATISTIC(ZIVapplications, "ZIV applications");
STATISTIC(ZIVindependence, "ZIV independence");
STATISTIC(StrongSIVapplications, "Strong SIV applications");
STATISTIC(StrongSIVsuccesses, "Strong SIV successes");
STATISTIC(StrongSIVindependence, "Strong SIV independence");
STATISTIC(WeakCrossingSIVapplications, "Weak-Crossing SIV applications");
STATISTIC(WeakCrossingSIVsuccesses, "Weak-Crossing SIV successes");
STATISTIC(WeakCrossingSIVindependence, "Weak-Crossing SIV independence");
STATISTIC(ExactSIVapplications, "Exact SIV applications");
STATISTIC(ExactSIVsuccesses, "Exact SIV successes");
STATISTIC(ExactSIVindependence, "Exact SIV independence");
STATISTIC(WeakZeroSIVapplications, "Weak-Zero SIV applications");
STATISTIC(WeakZeroSIVsuccesses, "Weak-Zero SIV successes");
STATISTIC(WeakZeroSIVindependence, "Weak-Zero SIV independence");
STATISTIC(ExactRDIVapplications, "Exact RDIV applications");
STATISTIC(ExactRDIVindependence, "Exact RDIV independence");
STATISTIC(SymbolicRDIVapplications, "Symbolic RDIV applications");
STATISTIC(SymbolicRDIVindependence, "Symbolic RDIV independence");
STATISTIC(DeltaApplications, "Delta applications");
STATISTIC(DeltaSuccesses, "Delta successes");
STATISTIC(DeltaIndependence, "Delta independence");
STATISTIC(DeltaPropagations, "Delta propagations");
STATISTIC(GCDapplications, "GCD applications");
STATISTIC(GCDsuccesses, "GCD successes");
STATISTIC(GCDindependence, "GCD independence");
STATISTIC(BanerjeeApplications, "Banerjee applications");
STATISTIC(BanerjeeIndependence, "Banerjee independence");
STATISTIC(BanerjeeSuccesses, "Banerjee successes");

static cl::opt<bool>
    Delinearize("da-delinearize", cl::init(true), cl::Hidden, cl::ZeroOrMore,
                cl::desc("Try to delinearize array references."));

//===----------------------------------------------------------------------===//
// basics

DependenceAnalysis::Result
DependenceAnalysis::run(Function &F, FunctionAnalysisManager &FAM) {
  auto &AA = FAM.getResult<AAManager>(F);
  auto &SE = FAM.getResult<ScalarEvolutionAnalysis>(F);
  auto &LI = FAM.getResult<LoopAnalysis>(F);
  return DependenceInfo(&F, &AA, &SE, &LI);
}

AnalysisKey DependenceAnalysis::Key;

INITIALIZE_PASS_BEGIN(DependenceAnalysisWrapperPass, "da",
                      "Dependence Analysis", true, true)
INITIALIZE_PASS_DEPENDENCY(LoopInfoWrapperPass)
INITIALIZE_PASS_DEPENDENCY(ScalarEvolutionWrapperPass)
INITIALIZE_PASS_DEPENDENCY(AAResultsWrapperPass)
INITIALIZE_PASS_END(DependenceAnalysisWrapperPass, "da", "Dependence Analysis",
                    true, true)

char DependenceAnalysisWrapperPass::ID = 0;

FunctionPass *llvm::createDependenceAnalysisWrapperPass() {
  return new DependenceAnalysisWrapperPass();
}

bool DependenceAnalysisWrapperPass::runOnFunction(Function &F) {
  auto &AA = getAnalysis<AAResultsWrapperPass>().getAAResults();
  auto &SE = getAnalysis<ScalarEvolutionWrapperPass>().getSE();
  auto &LI = getAnalysis<LoopInfoWrapperPass>().getLoopInfo();
  info.reset(new DependenceInfo(&F, &AA, &SE, &LI));
  return false;
}

DependenceInfo &DependenceAnalysisWrapperPass::getDI() const { return *info; }

void DependenceAnalysisWrapperPass::releaseMemory() { info.reset(); }

void DependenceAnalysisWrapperPass::getAnalysisUsage(AnalysisUsage &AU) const {
  AU.setPreservesAll();
  AU.addRequiredTransitive<AAResultsWrapperPass>();
  AU.addRequiredTransitive<ScalarEvolutionWrapperPass>();
  AU.addRequiredTransitive<LoopInfoWrapperPass>();
}


// Used to test the dependence analyzer.
// Looks through the function, noting loads and stores.
// Calls depends() on every possible pair and prints out the result.
// Ignores all other instructions.
static void dumpExampleDependence(raw_ostream &OS, DependenceInfo *DA) {
  auto *F = DA->getFunction();
  for (inst_iterator SrcI = inst_begin(F), SrcE = inst_end(F); SrcI != SrcE;
       ++SrcI) {
    if (isa<StoreInst>(*SrcI) || isa<LoadInst>(*SrcI)) {
      for (inst_iterator DstI = SrcI, DstE = inst_end(F);
           DstI != DstE; ++DstI) {
        if (isa<StoreInst>(*DstI) || isa<LoadInst>(*DstI)) {
          OS << "da analyze - ";
          if (auto D = DA->depends(&*SrcI, &*DstI, true)) {
            D->dump(OS);
            for (unsigned Level = 1; Level <= D->getLevels(); Level++) {
              if (D->isSplitable(Level)) {
                OS << "da analyze - split level = " << Level;
                OS << ", iteration = " << *DA->getSplitIteration(*D, Level);
                OS << "!\n";
              }
            }
          }
          else
            OS << "none!\n";
        }
      }
    }
  }
}

void DependenceAnalysisWrapperPass::print(raw_ostream &OS,
                                          const Module *) const {
  dumpExampleDependence(OS, info.get());
}

PreservedAnalyses
DependenceAnalysisPrinterPass::run(Function &F, FunctionAnalysisManager &FAM) {
  OS << "'Dependence Analysis' for function '" << F.getName() << "':\n";
  dumpExampleDependence(OS, &FAM.getResult<DependenceAnalysis>(F));
  return PreservedAnalyses::all();
}

//===----------------------------------------------------------------------===//
// Dependence methods

// Returns true if this is an input dependence.
bool Dependence::isInput() const {
  return Src->mayReadFromMemory() && Dst->mayReadFromMemory();
}


// Returns true if this is an output dependence.
bool Dependence::isOutput() const {
  return Src->mayWriteToMemory() && Dst->mayWriteToMemory();
}


// Returns true if this is an flow (aka true)  dependence.
bool Dependence::isFlow() const {
  return Src->mayWriteToMemory() && Dst->mayReadFromMemory();
}


// Returns true if this is an anti dependence.
bool Dependence::isAnti() const {
  return Src->mayReadFromMemory() && Dst->mayWriteToMemory();
}


// Returns true if a particular level is scalar; that is,
// if no subscript in the source or destination mention the induction
// variable associated with the loop at this level.
// Leave this out of line, so it will serve as a virtual method anchor
bool Dependence::isScalar(unsigned level) const {
  return false;
}


//===----------------------------------------------------------------------===//
// FullDependence methods

FullDependence::FullDependence(Instruction *Source, Instruction *Destination,
                               bool PossiblyLoopIndependent,
                               unsigned CommonLevels)
    : Dependence(Source, Destination), Levels(CommonLevels),
      LoopIndependent(PossiblyLoopIndependent) {
  Consistent = true;
  if (CommonLevels)
    DV = make_unique<DVEntry[]>(CommonLevels);
}

// The rest are simple getters that hide the implementation.

// getDirection - Returns the direction associated with a particular level.
unsigned FullDependence::getDirection(unsigned Level) const {
  assert(0 < Level && Level <= Levels && "Level out of range");
  return DV[Level - 1].Direction;
}


// Returns the distance (or NULL) associated with a particular level.
const SCEV *FullDependence::getDistance(unsigned Level) const {
  assert(0 < Level && Level <= Levels && "Level out of range");
  return DV[Level - 1].Distance;
}


// Returns true if a particular level is scalar; that is,
// if no subscript in the source or destination mention the induction
// variable associated with the loop at this level.
bool FullDependence::isScalar(unsigned Level) const {
  assert(0 < Level && Level <= Levels && "Level out of range");
  return DV[Level - 1].Scalar;
}


// Returns true if peeling the first iteration from this loop
// will break this dependence.
bool FullDependence::isPeelFirst(unsigned Level) const {
  assert(0 < Level && Level <= Levels && "Level out of range");
  return DV[Level - 1].PeelFirst;
}


// Returns true if peeling the last iteration from this loop
// will break this dependence.
bool FullDependence::isPeelLast(unsigned Level) const {
  assert(0 < Level && Level <= Levels && "Level out of range");
  return DV[Level - 1].PeelLast;
}


// Returns true if splitting this loop will break the dependence.
bool FullDependence::isSplitable(unsigned Level) const {
  assert(0 < Level && Level <= Levels && "Level out of range");
  return DV[Level - 1].Splitable;
}


//===----------------------------------------------------------------------===//
// DependenceInfo::Constraint methods

// If constraint is a point <X, Y>, returns X.
// Otherwise assert.
const SCEV *DependenceInfo::Constraint::getX() const {
  assert(Kind == Point && "Kind should be Point");
  return A;
}


// If constraint is a point <X, Y>, returns Y.
// Otherwise assert.
const SCEV *DependenceInfo::Constraint::getY() const {
  assert(Kind == Point && "Kind should be Point");
  return B;
}


// If constraint is a line AX + BY = C, returns A.
// Otherwise assert.
const SCEV *DependenceInfo::Constraint::getA() const {
  assert((Kind == Line || Kind == Distance) &&
         "Kind should be Line (or Distance)");
  return A;
}


// If constraint is a line AX + BY = C, returns B.
// Otherwise assert.
const SCEV *DependenceInfo::Constraint::getB() const {
  assert((Kind == Line || Kind == Distance) &&
         "Kind should be Line (or Distance)");
  return B;
}


// If constraint is a line AX + BY = C, returns C.
// Otherwise assert.
const SCEV *DependenceInfo::Constraint::getC() const {
  assert((Kind == Line || Kind == Distance) &&
         "Kind should be Line (or Distance)");
  return C;
}


// If constraint is a distance, returns D.
// Otherwise assert.
const SCEV *DependenceInfo::Constraint::getD() const {
  assert(Kind == Distance && "Kind should be Distance");
  return SE->getNegativeSCEV(C);
}


// Returns the loop associated with this constraint.
const Loop *DependenceInfo::Constraint::getAssociatedLoop() const {
  assert((Kind == Distance || Kind == Line || Kind == Point) &&
         "Kind should be Distance, Line, or Point");
  return AssociatedLoop;
}

void DependenceInfo::Constraint::setPoint(const SCEV *X, const SCEV *Y,
                                          const Loop *CurLoop) {
  Kind = Point;
  A = X;
  B = Y;
  AssociatedLoop = CurLoop;
}

void DependenceInfo::Constraint::setLine(const SCEV *AA, const SCEV *BB,
                                         const SCEV *CC, const Loop *CurLoop) {
  Kind = Line;
  A = AA;
  B = BB;
  C = CC;
  AssociatedLoop = CurLoop;
}

void DependenceInfo::Constraint::setDistance(const SCEV *D,
                                             const Loop *CurLoop) {
  Kind = Distance;
  A = SE->getOne(D->getType());
  B = SE->getNegativeSCEV(A);
  C = SE->getNegativeSCEV(D);
  AssociatedLoop = CurLoop;
}

void DependenceInfo::Constraint::setEmpty() { Kind = Empty; }

void DependenceInfo::Constraint::setAny(ScalarEvolution *NewSE) {
  SE = NewSE;
  Kind = Any;
}

#if !defined(NDEBUG) || defined(LLVM_ENABLE_DUMP)
// For debugging purposes. Dumps the constraint out to OS.
LLVM_DUMP_METHOD void DependenceInfo::Constraint::dump(raw_ostream &OS) const {
  if (isEmpty())
    OS << " Empty\n";
  else if (isAny())
    OS << " Any\n";
  else if (isPoint())
    OS << " Point is <" << *getX() << ", " << *getY() << ">\n";
  else if (isDistance())
    OS << " Distance is " << *getD() <<
      " (" << *getA() << "*X + " << *getB() << "*Y = " << *getC() << ")\n";
  else if (isLine())
    OS << " Line is " << *getA() << "*X + " <<
      *getB() << "*Y = " << *getC() << "\n";
  else
    llvm_unreachable("unknown constraint type in Constraint::dump");
}
#endif


// Updates X with the intersection
// of the Constraints X and Y. Returns true if X has changed.
// Corresponds to Figure 4 from the paper
//
//            Practical Dependence Testing
//            Goff, Kennedy, Tseng
//            PLDI 1991
bool DependenceInfo::intersectConstraints(Constraint *X, const Constraint *Y) {
  ++DeltaApplications;
  LLVM_DEBUG(dbgs() << "\tintersect constraints\n");
  LLVM_DEBUG(dbgs() << "\t    X ="; X->dump(dbgs()));
  LLVM_DEBUG(dbgs() << "\t    Y ="; Y->dump(dbgs()));
  assert(!Y->isPoint() && "Y must not be a Point");
  if (X->isAny()) {
    if (Y->isAny())
      return false;
    *X = *Y;
    return true;
  }
  if (X->isEmpty())
    return false;
  if (Y->isEmpty()) {
    X->setEmpty();
    return true;
  }

  if (X->isDistance() && Y->isDistance()) {
    LLVM_DEBUG(dbgs() << "\t    intersect 2 distances\n");
    if (isKnownPredicate(CmpInst::ICMP_EQ, X->getD(), Y->getD()))
      return false;
    if (isKnownPredicate(CmpInst::ICMP_NE, X->getD(), Y->getD())) {
      X->setEmpty();
      ++DeltaSuccesses;
      return true;
    }
    // Hmmm, interesting situation.
    // I guess if either is constant, keep it and ignore the other.
    if (isa<SCEVConstant>(Y->getD())) {
      *X = *Y;
      return true;
    }
    return false;
  }

  // At this point, the pseudo-code in Figure 4 of the paper
  // checks if (X->isPoint() && Y->isPoint()).
  // This case can't occur in our implementation,
  // since a Point can only arise as the result of intersecting
  // two Line constraints, and the right-hand value, Y, is never
  // the result of an intersection.
  assert(!(X->isPoint() && Y->isPoint()) &&
         "We shouldn't ever see X->isPoint() && Y->isPoint()");

  if (X->isLine() && Y->isLine()) {
    LLVM_DEBUG(dbgs() << "\t    intersect 2 lines\n");
    const SCEV *Prod1 = SE->getMulExpr(X->getA(), Y->getB());
    const SCEV *Prod2 = SE->getMulExpr(X->getB(), Y->getA());
    if (isKnownPredicate(CmpInst::ICMP_EQ, Prod1, Prod2)) {
      // slopes are equal, so lines are parallel
      LLVM_DEBUG(dbgs() << "\t\tsame slope\n");
      Prod1 = SE->getMulExpr(X->getC(), Y->getB());
      Prod2 = SE->getMulExpr(X->getB(), Y->getC());
      if (isKnownPredicate(CmpInst::ICMP_EQ, Prod1, Prod2))
        return false;
      if (isKnownPredicate(CmpInst::ICMP_NE, Prod1, Prod2)) {
        X->setEmpty();
        ++DeltaSuccesses;
        return true;
      }
      return false;
    }
    if (isKnownPredicate(CmpInst::ICMP_NE, Prod1, Prod2)) {
      // slopes differ, so lines intersect
      LLVM_DEBUG(dbgs() << "\t\tdifferent slopes\n");
      const SCEV *C1B2 = SE->getMulExpr(X->getC(), Y->getB());
      const SCEV *C1A2 = SE->getMulExpr(X->getC(), Y->getA());
      const SCEV *C2B1 = SE->getMulExpr(Y->getC(), X->getB());
      const SCEV *C2A1 = SE->getMulExpr(Y->getC(), X->getA());
      const SCEV *A1B2 = SE->getMulExpr(X->getA(), Y->getB());
      const SCEV *A2B1 = SE->getMulExpr(Y->getA(), X->getB());
      const SCEVConstant *C1A2_C2A1 =
        dyn_cast<SCEVConstant>(SE->getMinusSCEV(C1A2, C2A1));
      const SCEVConstant *C1B2_C2B1 =
        dyn_cast<SCEVConstant>(SE->getMinusSCEV(C1B2, C2B1));
      const SCEVConstant *A1B2_A2B1 =
        dyn_cast<SCEVConstant>(SE->getMinusSCEV(A1B2, A2B1));
      const SCEVConstant *A2B1_A1B2 =
        dyn_cast<SCEVConstant>(SE->getMinusSCEV(A2B1, A1B2));
      if (!C1B2_C2B1 || !C1A2_C2A1 ||
          !A1B2_A2B1 || !A2B1_A1B2)
        return false;
      APInt Xtop = C1B2_C2B1->getAPInt();
      APInt Xbot = A1B2_A2B1->getAPInt();
      APInt Ytop = C1A2_C2A1->getAPInt();
      APInt Ybot = A2B1_A1B2->getAPInt();
      LLVM_DEBUG(dbgs() << "\t\tXtop = " << Xtop << "\n");
      LLVM_DEBUG(dbgs() << "\t\tXbot = " << Xbot << "\n");
      LLVM_DEBUG(dbgs() << "\t\tYtop = " << Ytop << "\n");
      LLVM_DEBUG(dbgs() << "\t\tYbot = " << Ybot << "\n");
      APInt Xq = Xtop; // these need to be initialized, even
      APInt Xr = Xtop; // though they're just going to be overwritten
      APInt::sdivrem(Xtop, Xbot, Xq, Xr);
      APInt Yq = Ytop;
      APInt Yr = Ytop;
      APInt::sdivrem(Ytop, Ybot, Yq, Yr);
      if (Xr != 0 || Yr != 0) {
        X->setEmpty();
        ++DeltaSuccesses;
        return true;
      }
      LLVM_DEBUG(dbgs() << "\t\tX = " << Xq << ", Y = " << Yq << "\n");
      if (Xq.slt(0) || Yq.slt(0)) {
        X->setEmpty();
        ++DeltaSuccesses;
        return true;
      }
      if (const SCEVConstant *CUB =
          collectConstantUpperBound(X->getAssociatedLoop(), Prod1->getType())) {
        const APInt &UpperBound = CUB->getAPInt();
        LLVM_DEBUG(dbgs() << "\t\tupper bound = " << UpperBound << "\n");
        if (Xq.sgt(UpperBound) || Yq.sgt(UpperBound)) {
          X->setEmpty();
          ++DeltaSuccesses;
          return true;
        }
      }
      X->setPoint(SE->getConstant(Xq),
                  SE->getConstant(Yq),
                  X->getAssociatedLoop());
      ++DeltaSuccesses;
      return true;
    }
    return false;
  }

  // if (X->isLine() && Y->isPoint()) This case can't occur.
  assert(!(X->isLine() && Y->isPoint()) && "This case should never occur");

  if (X->isPoint() && Y->isLine()) {
    LLVM_DEBUG(dbgs() << "\t    intersect Point and Line\n");
    const SCEV *A1X1 = SE->getMulExpr(Y->getA(), X->getX());
    const SCEV *B1Y1 = SE->getMulExpr(Y->getB(), X->getY());
    const SCEV *Sum = SE->getAddExpr(A1X1, B1Y1);
    if (isKnownPredicate(CmpInst::ICMP_EQ, Sum, Y->getC()))
      return false;
    if (isKnownPredicate(CmpInst::ICMP_NE, Sum, Y->getC())) {
      X->setEmpty();
      ++DeltaSuccesses;
      return true;
    }
    return false;
  }

  llvm_unreachable("shouldn't reach the end of Constraint intersection");
  return false;
}


//===----------------------------------------------------------------------===//
// DependenceInfo methods

// For debugging purposes. Dumps a dependence to OS.
void Dependence::dump(raw_ostream &OS) const {
  bool Splitable = false;
  if (isConfused())
    OS << "confused";
  else {
    if (isConsistent())
      OS << "consistent ";
    if (isFlow())
      OS << "flow";
    else if (isOutput())
      OS << "output";
    else if (isAnti())
      OS << "anti";
    else if (isInput())
      OS << "input";
    unsigned Levels = getLevels();
    OS << " [";
    for (unsigned II = 1; II <= Levels; ++II) {
      if (isSplitable(II))
        Splitable = true;
      if (isPeelFirst(II))
        OS << 'p';
      const SCEV *Distance = getDistance(II);
      if (Distance)
        OS << *Distance;
      else if (isScalar(II))
        OS << "S";
      else {
        unsigned Direction = getDirection(II);
        if (Direction == DVEntry::ALL)
          OS << "*";
        else {
          if (Direction & DVEntry::LT)
            OS << "<";
          if (Direction & DVEntry::EQ)
            OS << "=";
          if (Direction & DVEntry::GT)
            OS << ">";
        }
      }
      if (isPeelLast(II))
        OS << 'p';
      if (II < Levels)
        OS << " ";
    }
    if (isLoopIndependent())
      OS << "|<";
    OS << "]";
    if (Splitable)
      OS << " splitable";
  }
  OS << "!\n";
}

// Returns NoAlias/MayAliass/MustAlias for two memory locations based upon their
// underlaying objects. If LocA and LocB are known to not alias (for any reason:
// tbaa, non-overlapping regions etc), then it is known there is no dependecy.
// Otherwise the underlying objects are checked to see if they point to
// different identifiable objects.
static AliasResult underlyingObjectsAlias(AliasAnalysis *AA,
                                          const DataLayout &DL,
                                          const MemoryLocation &LocA,
                                          const MemoryLocation &LocB) {
  // Check the original locations (minus size) for noalias, which can happen for
  // tbaa, incompatible underlying object locations, etc.
  MemoryLocation LocAS(LocA.Ptr, LocationSize::unknown(), LocA.AATags);
  MemoryLocation LocBS(LocB.Ptr, LocationSize::unknown(), LocB.AATags);
  if (AA->alias(LocAS, LocBS) == NoAlias)
    return NoAlias;

  // Check the underlying objects are the same
  const Value *AObj = GetUnderlyingObject(LocA.Ptr, DL);
  const Value *BObj = GetUnderlyingObject(LocB.Ptr, DL);

  // If the underlying objects are the same, they must alias
  if (AObj == BObj)
    return MustAlias;

  // We may have hit the recursion limit for underlying objects, or have
  // underlying objects where we don't know they will alias.
  if (!isIdentifiedObject(AObj) || !isIdentifiedObject(BObj))
    return MayAlias;

  // Otherwise we know the objects are different and both identified objects so
  // must not alias.
  return NoAlias;
}


// Returns true if the load or store can be analyzed. Atomic and volatile
// operations have properties which this analysis does not understand.
static
bool isLoadOrStore(const Instruction *I) {
  if (const LoadInst *LI = dyn_cast<LoadInst>(I))
    return LI->isUnordered();
  else if (const StoreInst *SI = dyn_cast<StoreInst>(I))
    return SI->isUnordered();
  return false;
}


// Examines the loop nesting of the Src and Dst
// instructions and establishes their shared loops. Sets the variables
// CommonLevels, SrcLevels, and MaxLevels.
// The source and destination instructions needn't be contained in the same
// loop. The routine establishNestingLevels finds the level of most deeply
// nested loop that contains them both, CommonLevels. An instruction that's
// not contained in a loop is at level = 0. MaxLevels is equal to the level
// of the source plus the level of the destination, minus CommonLevels.
// This lets us allocate vectors MaxLevels in length, with room for every
// distinct loop referenced in both the source and destination subscripts.
// The variable SrcLevels is the nesting depth of the source instruction.
// It's used to help calculate distinct loops referenced by the destination.
// Here's the map from loops to levels:
//            0 - unused
//            1 - outermost common loop
//          ... - other common loops
// CommonLevels - innermost common loop
//          ... - loops containing Src but not Dst
//    SrcLevels - innermost loop containing Src but not Dst
//          ... - loops containing Dst but not Src
//    MaxLevels - innermost loops containing Dst but not Src
// Consider the follow code fragment:
//   for (a = ...) {
//     for (b = ...) {
//       for (c = ...) {
//         for (d = ...) {
//           A[] = ...;
//         }
//       }
//       for (e = ...) {
//         for (f = ...) {
//           for (g = ...) {
//             ... = A[];
//           }
//         }
//       }
//     }
//   }
// If we're looking at the possibility of a dependence between the store
// to A (the Src) and the load from A (the Dst), we'll note that they
// have 2 loops in common, so CommonLevels will equal 2 and the direction
// vector for Result will have 2 entries. SrcLevels = 4 and MaxLevels = 7.
// A map from loop names to loop numbers would look like
//     a - 1
//     b - 2 = CommonLevels
//     c - 3
//     d - 4 = SrcLevels
//     e - 5
//     f - 6
//     g - 7 = MaxLevels
void DependenceInfo::establishNestingLevels(const Instruction *Src,
                                            const Instruction *Dst) {
  const BasicBlock *SrcBlock = Src->getParent();
  const BasicBlock *DstBlock = Dst->getParent();
  unsigned SrcLevel = LI->getLoopDepth(SrcBlock);
  unsigned DstLevel = LI->getLoopDepth(DstBlock);
  const Loop *SrcLoop = LI->getLoopFor(SrcBlock);
  const Loop *DstLoop = LI->getLoopFor(DstBlock);
  SrcLevels = SrcLevel;
  MaxLevels = SrcLevel + DstLevel;
  while (SrcLevel > DstLevel) {
    SrcLoop = SrcLoop->getParentLoop();
    SrcLevel--;
  }
  while (DstLevel > SrcLevel) {
    DstLoop = DstLoop->getParentLoop();
    DstLevel--;
  }
  while (SrcLoop != DstLoop) {
    SrcLoop = SrcLoop->getParentLoop();
    DstLoop = DstLoop->getParentLoop();
    SrcLevel--;
  }
  CommonLevels = SrcLevel;
  MaxLevels -= CommonLevels;
}


// Given one of the loops containing the source, return
// its level index in our numbering scheme.
unsigned DependenceInfo::mapSrcLoop(const Loop *SrcLoop) const {
  return SrcLoop->getLoopDepth();
}


// Given one of the loops containing the destination,
// return its level index in our numbering scheme.
unsigned DependenceInfo::mapDstLoop(const Loop *DstLoop) const {
  unsigned D = DstLoop->getLoopDepth();
  if (D > CommonLevels)
    return D - CommonLevels + SrcLevels;
  else
    return D;
}


// Returns true if Expression is loop invariant in LoopNest.
bool DependenceInfo::isLoopInvariant(const SCEV *Expression,
                                     const Loop *LoopNest) const {
  if (!LoopNest)
    return true;
  return SE->isLoopInvariant(Expression, LoopNest) &&
    isLoopInvariant(Expression, LoopNest->getParentLoop());
}



// Finds the set of loops from the LoopNest that
// have a level <= CommonLevels and are referred to by the SCEV Expression.
void DependenceInfo::collectCommonLoops(const SCEV *Expression,
                                        const Loop *LoopNest,
                                        SmallBitVector &Loops) const {
  while (LoopNest) {
    unsigned Level = LoopNest->getLoopDepth();
    if (Level <= CommonLevels && !SE->isLoopInvariant(Expression, LoopNest))
      Loops.set(Level);
    LoopNest = LoopNest->getParentLoop();
  }
}

void DependenceInfo::unifySubscriptType(ArrayRef<Subscript *> Pairs) {

  unsigned widestWidthSeen = 0;
  Type *widestType;

  // Go through each pair and find the widest bit to which we need
  // to extend all of them.
  for (Subscript *Pair : Pairs) {
    const SCEV *Src = Pair->Src;
    const SCEV *Dst = Pair->Dst;
    IntegerType *SrcTy = dyn_cast<IntegerType>(Src->getType());
    IntegerType *DstTy = dyn_cast<IntegerType>(Dst->getType());
    if (SrcTy == nullptr || DstTy == nullptr) {
      assert(SrcTy == DstTy && "This function only unify integer types and "
             "expect Src and Dst share the same type "
             "otherwise.");
      continue;
    }
    if (SrcTy->getBitWidth() > widestWidthSeen) {
      widestWidthSeen = SrcTy->getBitWidth();
      widestType = SrcTy;
    }
    if (DstTy->getBitWidth() > widestWidthSeen) {
      widestWidthSeen = DstTy->getBitWidth();
      widestType = DstTy;
    }
  }


  assert(widestWidthSeen > 0);

  // Now extend each pair to the widest seen.
  for (Subscript *Pair : Pairs) {
    const SCEV *Src = Pair->Src;
    const SCEV *Dst = Pair->Dst;
    IntegerType *SrcTy = dyn_cast<IntegerType>(Src->getType());
    IntegerType *DstTy = dyn_cast<IntegerType>(Dst->getType());
    if (SrcTy == nullptr || DstTy == nullptr) {
      assert(SrcTy == DstTy && "This function only unify integer types and "
             "expect Src and Dst share the same type "
             "otherwise.");
      continue;
    }
    if (SrcTy->getBitWidth() < widestWidthSeen)
      // Sign-extend Src to widestType
      Pair->Src = SE->getSignExtendExpr(Src, widestType);
    if (DstTy->getBitWidth() < widestWidthSeen) {
      // Sign-extend Dst to widestType
      Pair->Dst = SE->getSignExtendExpr(Dst, widestType);
    }
  }
}

// removeMatchingExtensions - Examines a subscript pair.
// If the source and destination are identically sign (or zero)
// extended, it strips off the extension in an effect to simplify
// the actual analysis.
void DependenceInfo::removeMatchingExtensions(Subscript *Pair) {
  const SCEV *Src = Pair->Src;
  const SCEV *Dst = Pair->Dst;
  if ((isa<SCEVZeroExtendExpr>(Src) && isa<SCEVZeroExtendExpr>(Dst)) ||
      (isa<SCEVSignExtendExpr>(Src) && isa<SCEVSignExtendExpr>(Dst))) {
    const SCEVCastExpr *SrcCast = cast<SCEVCastExpr>(Src);
    const SCEVCastExpr *DstCast = cast<SCEVCastExpr>(Dst);
    const SCEV *SrcCastOp = SrcCast->getOperand();
    const SCEV *DstCastOp = DstCast->getOperand();
    if (SrcCastOp->getType() == DstCastOp->getType()) {
      Pair->Src = SrcCastOp;
      Pair->Dst = DstCastOp;
    }
  }
}


// Examine the scev and return true iff it's linear.
// Collect any loops mentioned in the set of "Loops".
bool DependenceInfo::checkSrcSubscript(const SCEV *Src, const Loop *LoopNest,
                                       SmallBitVector &Loops) {
  const SCEVAddRecExpr *AddRec = dyn_cast<SCEVAddRecExpr>(Src);
  if (!AddRec)
    return isLoopInvariant(Src, LoopNest);
  const SCEV *Start = AddRec->getStart();
  const SCEV *Step = AddRec->getStepRecurrence(*SE);
  const SCEV *UB = SE->getBackedgeTakenCount(AddRec->getLoop());
  if (!isa<SCEVCouldNotCompute>(UB)) {
    if (SE->getTypeSizeInBits(Start->getType()) <
        SE->getTypeSizeInBits(UB->getType())) {
      if (!AddRec->getNoWrapFlags())
        return false;
    }
  }
  if (!isLoopInvariant(Step, LoopNest))
    return false;
  Loops.set(mapSrcLoop(AddRec->getLoop()));
  return checkSrcSubscript(Start, LoopNest, Loops);
}



// Examine the scev and return true iff it's linear.
// Collect any loops mentioned in the set of "Loops".
bool DependenceInfo::checkDstSubscript(const SCEV *Dst, const Loop *LoopNest,
                                       SmallBitVector &Loops) {
  const SCEVAddRecExpr *AddRec = dyn_cast<SCEVAddRecExpr>(Dst);
  if (!AddRec)
    return isLoopInvariant(Dst, LoopNest);
  const SCEV *Start = AddRec->getStart();
  const SCEV *Step = AddRec->getStepRecurrence(*SE);
  const SCEV *UB = SE->getBackedgeTakenCount(AddRec->getLoop());
  if (!isa<SCEVCouldNotCompute>(UB)) {
    if (SE->getTypeSizeInBits(Start->getType()) <
        SE->getTypeSizeInBits(UB->getType())) {
      if (!AddRec->getNoWrapFlags())
        return false;
    }
  }
  if (!isLoopInvariant(Step, LoopNest))
    return false;
  Loops.set(mapDstLoop(AddRec->getLoop()));
  return checkDstSubscript(Start, LoopNest, Loops);
}


// Examines the subscript pair (the Src and Dst SCEVs)
// and classifies it as either ZIV, SIV, RDIV, MIV, or Nonlinear.
// Collects the associated loops in a set.
DependenceInfo::Subscript::ClassificationKind
DependenceInfo::classifyPair(const SCEV *Src, const Loop *SrcLoopNest,
                             const SCEV *Dst, const Loop *DstLoopNest,
                             SmallBitVector &Loops) {
  SmallBitVector SrcLoops(MaxLevels + 1);
  SmallBitVector DstLoops(MaxLevels + 1);
  if (!checkSrcSubscript(Src, SrcLoopNest, SrcLoops))
    return Subscript::NonLinear;
  if (!checkDstSubscript(Dst, DstLoopNest, DstLoops))
    return Subscript::NonLinear;
  Loops = SrcLoops;
  Loops |= DstLoops;
  unsigned N = Loops.count();
  if (N == 0)
    return Subscript::ZIV;
  if (N == 1)
    return Subscript::SIV;
  if (N == 2 && (SrcLoops.count() == 0 ||
                 DstLoops.count() == 0 ||
                 (SrcLoops.count() == 1 && DstLoops.count() == 1)))
    return Subscript::RDIV;
  return Subscript::MIV;
}


// A wrapper around SCEV::isKnownPredicate.
// Looks for cases where we're interested in comparing for equality.
// If both X and Y have been identically sign or zero extended,
// it strips off the (confusing) extensions before invoking
// SCEV::isKnownPredicate. Perhaps, someday, the ScalarEvolution package
// will be similarly updated.
//
// If SCEV::isKnownPredicate can't prove the predicate,
// we try simple subtraction, which seems to help in some cases
// involving symbolics.
bool DependenceInfo::isKnownPredicate(ICmpInst::Predicate Pred, const SCEV *X,
                                      const SCEV *Y) const {
  if (Pred == CmpInst::ICMP_EQ ||
      Pred == CmpInst::ICMP_NE) {
    if ((isa<SCEVSignExtendExpr>(X) &&
         isa<SCEVSignExtendExpr>(Y)) ||
        (isa<SCEVZeroExtendExpr>(X) &&
         isa<SCEVZeroExtendExpr>(Y))) {
      const SCEVCastExpr *CX = cast<SCEVCastExpr>(X);
      const SCEVCastExpr *CY = cast<SCEVCastExpr>(Y);
      const SCEV *Xop = CX->getOperand();
      const SCEV *Yop = CY->getOperand();
      if (Xop->getType() == Yop->getType()) {
        X = Xop;
        Y = Yop;
      }
    }
  }
  if (SE->isKnownPredicate(Pred, X, Y))
    return true;
  // If SE->isKnownPredicate can't prove the condition,
  // we try the brute-force approach of subtracting
  // and testing the difference.
  // By testing with SE->isKnownPredicate first, we avoid
  // the possibility of overflow when the arguments are constants.
  const SCEV *Delta = SE->getMinusSCEV(X, Y);
  switch (Pred) {
  case CmpInst::ICMP_EQ:
    return Delta->isZero();
  case CmpInst::ICMP_NE:
    return SE->isKnownNonZero(Delta);
  case CmpInst::ICMP_SGE:
    return SE->isKnownNonNegative(Delta);
  case CmpInst::ICMP_SLE:
    return SE->isKnownNonPositive(Delta);
  case CmpInst::ICMP_SGT:
    return SE->isKnownPositive(Delta);
  case CmpInst::ICMP_SLT:
    return SE->isKnownNegative(Delta);
  default:
    llvm_unreachable("unexpected predicate in isKnownPredicate");
  }
}

/// Compare to see if S is less than Size, using isKnownNegative(S - max(Size, 1))
/// with some extra checking if S is an AddRec and we can prove less-than using
/// the loop bounds.
bool DependenceInfo::isKnownLessThan(const SCEV *S, const SCEV *Size) const {
  // First unify to the same type
  auto *SType = dyn_cast<IntegerType>(S->getType());
  auto *SizeType = dyn_cast<IntegerType>(Size->getType());
  if (!SType || !SizeType)
    return false;
  Type *MaxType =
      (SType->getBitWidth() >= SizeType->getBitWidth()) ? SType : SizeType;
  S = SE->getTruncateOrZeroExtend(S, MaxType);
  Size = SE->getTruncateOrZeroExtend(Size, MaxType);

  // Special check for addrecs using BE taken count
  const SCEV *Bound = SE->getMinusSCEV(S, Size);
  if (const SCEVAddRecExpr *AddRec = dyn_cast<SCEVAddRecExpr>(Bound)) {
    if (AddRec->isAffine()) {
      const SCEV *BECount = SE->getBackedgeTakenCount(AddRec->getLoop());
      if (!isa<SCEVCouldNotCompute>(BECount)) {
        const SCEV *Limit = AddRec->evaluateAtIteration(BECount, *SE);
        if (SE->isKnownNegative(Limit))
          return true;
      }
    }
  }

  // Check using normal isKnownNegative
  const SCEV *LimitedBound =
      SE->getMinusSCEV(S, SE->getSMaxExpr(Size, SE->getOne(Size->getType())));
  return SE->isKnownNegative(LimitedBound);
}

bool DependenceInfo::isKnownNonNegative(const SCEV *S, const Value *Ptr) const {
  bool Inbounds = false;
  if (auto *SrcGEP = dyn_cast<GetElementPtrInst>(Ptr))
    Inbounds = SrcGEP->isInBounds();
  if (Inbounds) {
    if (const SCEVAddRecExpr *AddRec = dyn_cast<SCEVAddRecExpr>(S)) {
      if (AddRec->isAffine()) {
        // We know S is for Ptr, the operand on a load/store, so doesn't wrap.
        // If both parts are NonNegative, the end result will be NonNegative
        if (SE->isKnownNonNegative(AddRec->getStart()) &&
            SE->isKnownNonNegative(AddRec->getOperand(1)))
          return true;
      }
    }
  }

  return SE->isKnownNonNegative(S);
}

// All subscripts are all the same type.
// Loop bound may be smaller (e.g., a char).
// Should zero extend loop bound, since it's always >= 0.
// This routine collects upper bound and extends or truncates if needed.
// Truncating is safe when subscripts are known not to wrap. Cases without
// nowrap flags should have been rejected earlier.
// Return null if no bound available.
const SCEV *DependenceInfo::collectUpperBound(const Loop *L, Type *T) const {
  if (SE->hasLoopInvariantBackedgeTakenCount(L)) {
    const SCEV *UB = SE->getBackedgeTakenCount(L);
    return SE->getTruncateOrZeroExtend(UB, T);
  }
  return nullptr;
}


// Calls collectUpperBound(), then attempts to cast it to SCEVConstant.
// If the cast fails, returns NULL.
const SCEVConstant *DependenceInfo::collectConstantUpperBound(const Loop *L,
                                                              Type *T) const {
  if (const SCEV *UB = collectUpperBound(L, T))
    return dyn_cast<SCEVConstant>(UB);
  return nullptr;
}


// testZIV -
// When we have a pair of subscripts of the form [c1] and [c2],
// where c1 and c2 are both loop invariant, we attack it using
// the ZIV test. Basically, we test by comparing the two values,
// but there are actually three possible results:
// 1) the values are equal, so there's a dependence
// 2) the values are different, so there's no dependence
// 3) the values might be equal, so we have to assume a dependence.
//
// Return true if dependence disproved.
bool DependenceInfo::testZIV(const SCEV *Src, const SCEV *Dst,
                             FullDependence &Result) const {
  LLVM_DEBUG(dbgs() << "    src = " << *Src << "\n");
  LLVM_DEBUG(dbgs() << "    dst = " << *Dst << "\n");
  ++ZIVapplications;
  if (isKnownPredicate(CmpInst::ICMP_EQ, Src, Dst)) {
    LLVM_DEBUG(dbgs() << "    provably dependent\n");
    return false; // provably dependent
  }
  if (isKnownPredicate(CmpInst::ICMP_NE, Src, Dst)) {
    LLVM_DEBUG(dbgs() << "    provably independent\n");
    ++ZIVindependence;
    return true; // provably independent
  }
  LLVM_DEBUG(dbgs() << "    possibly dependent\n");
  Result.Consistent = false;
  return false; // possibly dependent
}


// strongSIVtest -
// From the paper, Practical Dependence Testing, Section 4.2.1
//
// When we have a pair of subscripts of the form [c1 + a*i] and [c2 + a*i],
// where i is an induction variable, c1 and c2 are loop invariant,
//  and a is a constant, we can solve it exactly using the Strong SIV test.
//
// Can prove independence. Failing that, can compute distance (and direction).
// In the presence of symbolic terms, we can sometimes make progress.
//
// If there's a dependence,
//
//    c1 + a*i = c2 + a*i'
//
// The dependence distance is
//
//    d = i' - i = (c1 - c2)/a
//
// A dependence only exists if d is an integer and abs(d) <= U, where U is the
// loop's upper bound. If a dependence exists, the dependence direction is
// defined as
//
//                { < if d > 0
//    direction = { = if d = 0
//                { > if d < 0
//
// Return true if dependence disproved.
bool DependenceInfo::strongSIVtest(const SCEV *Coeff, const SCEV *SrcConst,
                                   const SCEV *DstConst, const Loop *CurLoop,
                                   unsigned Level, FullDependence &Result,
                                   Constraint &NewConstraint) const {
  LLVM_DEBUG(dbgs() << "\tStrong SIV test\n");
  LLVM_DEBUG(dbgs() << "\t    Coeff = " << *Coeff);
  LLVM_DEBUG(dbgs() << ", " << *Coeff->getType() << "\n");
  LLVM_DEBUG(dbgs() << "\t    SrcConst = " << *SrcConst);
  LLVM_DEBUG(dbgs() << ", " << *SrcConst->getType() << "\n");
  LLVM_DEBUG(dbgs() << "\t    DstConst = " << *DstConst);
  LLVM_DEBUG(dbgs() << ", " << *DstConst->getType() << "\n");
  ++StrongSIVapplications;
  assert(0 < Level && Level <= CommonLevels && "level out of range");
  Level--;

  const SCEV *Delta = SE->getMinusSCEV(SrcConst, DstConst);
  LLVM_DEBUG(dbgs() << "\t    Delta = " << *Delta);
  LLVM_DEBUG(dbgs() << ", " << *Delta->getType() << "\n");

  // check that |Delta| < iteration count
  if (const SCEV *UpperBound = collectUpperBound(CurLoop, Delta->getType())) {
    LLVM_DEBUG(dbgs() << "\t    UpperBound = " << *UpperBound);
    LLVM_DEBUG(dbgs() << ", " << *UpperBound->getType() << "\n");
    const SCEV *AbsDelta =
      SE->isKnownNonNegative(Delta) ? Delta : SE->getNegativeSCEV(Delta);
    const SCEV *AbsCoeff =
      SE->isKnownNonNegative(Coeff) ? Coeff : SE->getNegativeSCEV(Coeff);
    const SCEV *Product = SE->getMulExpr(UpperBound, AbsCoeff);
    if (isKnownPredicate(CmpInst::ICMP_SGT, AbsDelta, Product)) {
      // Distance greater than trip count - no dependence
      ++StrongSIVindependence;
      ++StrongSIVsuccesses;
      return true;
    }
  }

  // Can we compute distance?
  if (isa<SCEVConstant>(Delta) && isa<SCEVConstant>(Coeff)) {
    APInt ConstDelta = cast<SCEVConstant>(Delta)->getAPInt();
    APInt ConstCoeff = cast<SCEVConstant>(Coeff)->getAPInt();
    APInt Distance  = ConstDelta; // these need to be initialized
    APInt Remainder = ConstDelta;
    APInt::sdivrem(ConstDelta, ConstCoeff, Distance, Remainder);
    LLVM_DEBUG(dbgs() << "\t    Distance = " << Distance << "\n");
    LLVM_DEBUG(dbgs() << "\t    Remainder = " << Remainder << "\n");
    // Make sure Coeff divides Delta exactly
    if (Remainder != 0) {
      // Coeff doesn't divide Distance, no dependence
      ++StrongSIVindependence;
      ++StrongSIVsuccesses;
      return true;
    }
    Result.DV[Level].Distance = SE->getConstant(Distance);
    NewConstraint.setDistance(SE->getConstant(Distance), CurLoop);
    if (Distance.sgt(0))
      Result.DV[Level].Direction &= Dependence::DVEntry::LT;
    else if (Distance.slt(0))
      Result.DV[Level].Direction &= Dependence::DVEntry::GT;
    else
      Result.DV[Level].Direction &= Dependence::DVEntry::EQ;
    ++StrongSIVsuccesses;
  }
  else if (Delta->isZero()) {
    // since 0/X == 0
    Result.DV[Level].Distance = Delta;
    NewConstraint.setDistance(Delta, CurLoop);
    Result.DV[Level].Direction &= Dependence::DVEntry::EQ;
    ++StrongSIVsuccesses;
  }
  else {
    if (Coeff->isOne()) {
      LLVM_DEBUG(dbgs() << "\t    Distance = " << *Delta << "\n");
      Result.DV[Level].Distance = Delta; // since X/1 == X
      NewConstraint.setDistance(Delta, CurLoop);
    }
    else {
      Result.Consistent = false;
      NewConstraint.setLine(Coeff,
                            SE->getNegativeSCEV(Coeff),
                            SE->getNegativeSCEV(Delta), CurLoop);
    }

    // maybe we can get a useful direction
    bool DeltaMaybeZero     = !SE->isKnownNonZero(Delta);
    bool DeltaMaybePositive = !SE->isKnownNonPositive(Delta);
    bool DeltaMaybeNegative = !SE->isKnownNonNegative(Delta);
    bool CoeffMaybePositive = !SE->isKnownNonPositive(Coeff);
    bool CoeffMaybeNegative = !SE->isKnownNonNegative(Coeff);
    // The double negatives above are confusing.
    // It helps to read !SE->isKnownNonZero(Delta)
    // as "Delta might be Zero"
    unsigned NewDirection = Dependence::DVEntry::NONE;
    if ((DeltaMaybePositive && CoeffMaybePositive) ||
        (DeltaMaybeNegative && CoeffMaybeNegative))
      NewDirection = Dependence::DVEntry::LT;
    if (DeltaMaybeZero)
      NewDirection |= Dependence::DVEntry::EQ;
    if ((DeltaMaybeNegative && CoeffMaybePositive) ||
        (DeltaMaybePositive && CoeffMaybeNegative))
      NewDirection |= Dependence::DVEntry::GT;
    if (NewDirection < Result.DV[Level].Direction)
      ++StrongSIVsuccesses;
    Result.DV[Level].Direction &= NewDirection;
  }
  return false;
}


// weakCrossingSIVtest -
// From the paper, Practical Dependence Testing, Section 4.2.2
//
// When we have a pair of subscripts of the form [c1 + a*i] and [c2 - a*i],
// where i is an induction variable, c1 and c2 are loop invariant,
// and a is a constant, we can solve it exactly using the
// Weak-Crossing SIV test.
//
// Given c1 + a*i = c2 - a*i', we can look for the intersection of
// the two lines, where i = i', yielding
//
//    c1 + a*i = c2 - a*i
//    2a*i = c2 - c1
//    i = (c2 - c1)/2a
//
// If i < 0, there is no dependence.
// If i > upperbound, there is no dependence.
// If i = 0 (i.e., if c1 = c2), there's a dependence with distance = 0.
// If i = upperbound, there's a dependence with distance = 0.
// If i is integral, there's a dependence (all directions).
// If the non-integer part = 1/2, there's a dependence (<> directions).
// Otherwise, there's no dependence.
//
// Can prove independence. Failing that,
// can sometimes refine the directions.
// Can determine iteration for splitting.
//
// Return true if dependence disproved.
bool DependenceInfo::weakCrossingSIVtest(
    const SCEV *Coeff, const SCEV *SrcConst, const SCEV *DstConst,
    const Loop *CurLoop, unsigned Level, FullDependence &Result,
    Constraint &NewConstraint, const SCEV *&SplitIter) const {
  LLVM_DEBUG(dbgs() << "\tWeak-Crossing SIV test\n");
  LLVM_DEBUG(dbgs() << "\t    Coeff = " << *Coeff << "\n");
  LLVM_DEBUG(dbgs() << "\t    SrcConst = " << *SrcConst << "\n");
  LLVM_DEBUG(dbgs() << "\t    DstConst = " << *DstConst << "\n");
  ++WeakCrossingSIVapplications;
  assert(0 < Level && Level <= CommonLevels && "Level out of range");
  Level--;
  Result.Consistent = false;
  const SCEV *Delta = SE->getMinusSCEV(DstConst, SrcConst);
  LLVM_DEBUG(dbgs() << "\t    Delta = " << *Delta << "\n");
  NewConstraint.setLine(Coeff, Coeff, Delta, CurLoop);
  if (Delta->isZero()) {
    Result.DV[Level].Direction &= unsigned(~Dependence::DVEntry::LT);
    Result.DV[Level].Direction &= unsigned(~Dependence::DVEntry::GT);
    ++WeakCrossingSIVsuccesses;
    if (!Result.DV[Level].Direction) {
      ++WeakCrossingSIVindependence;
      return true;
    }
    Result.DV[Level].Distance = Delta; // = 0
    return false;
  }
  const SCEVConstant *ConstCoeff = dyn_cast<SCEVConstant>(Coeff);
  if (!ConstCoeff)
    return false;

  Result.DV[Level].Splitable = true;
  if (SE->isKnownNegative(ConstCoeff)) {
    ConstCoeff = dyn_cast<SCEVConstant>(SE->getNegativeSCEV(ConstCoeff));
    assert(ConstCoeff &&
           "dynamic cast of negative of ConstCoeff should yield constant");
    Delta = SE->getNegativeSCEV(Delta);
  }
  assert(SE->isKnownPositive(ConstCoeff) && "ConstCoeff should be positive");

  // compute SplitIter for use by DependenceInfo::getSplitIteration()
  SplitIter = SE->getUDivExpr(
      SE->getSMaxExpr(SE->getZero(Delta->getType()), Delta),
      SE->getMulExpr(SE->getConstant(Delta->getType(), 2), ConstCoeff));
  LLVM_DEBUG(dbgs() << "\t    Split iter = " << *SplitIter << "\n");

  const SCEVConstant *ConstDelta = dyn_cast<SCEVConstant>(Delta);
  if (!ConstDelta)
    return false;

  // We're certain that ConstCoeff > 0; therefore,
  // if Delta < 0, then no dependence.
  LLVM_DEBUG(dbgs() << "\t    Delta = " << *Delta << "\n");
  LLVM_DEBUG(dbgs() << "\t    ConstCoeff = " << *ConstCoeff << "\n");
  if (SE->isKnownNegative(Delta)) {
    // No dependence, Delta < 0
    ++WeakCrossingSIVindependence;
    ++WeakCrossingSIVsuccesses;
    return true;
  }

  // We're certain that Delta > 0 and ConstCoeff > 0.
  // Check Delta/(2*ConstCoeff) against upper loop bound
  if (const SCEV *UpperBound = collectUpperBound(CurLoop, Delta->getType())) {
    LLVM_DEBUG(dbgs() << "\t    UpperBound = " << *UpperBound << "\n");
    const SCEV *ConstantTwo = SE->getConstant(UpperBound->getType(), 2);
    const SCEV *ML = SE->getMulExpr(SE->getMulExpr(ConstCoeff, UpperBound),
                                    ConstantTwo);
    LLVM_DEBUG(dbgs() << "\t    ML = " << *ML << "\n");
    if (isKnownPredicate(CmpInst::ICMP_SGT, Delta, ML)) {
      // Delta too big, no dependence
      ++WeakCrossingSIVindependence;
      ++WeakCrossingSIVsuccesses;
      return true;
    }
    if (isKnownPredicate(CmpInst::ICMP_EQ, Delta, ML)) {
      // i = i' = UB
      Result.DV[Level].Direction &= unsigned(~Dependence::DVEntry::LT);
      Result.DV[Level].Direction &= unsigned(~Dependence::DVEntry::GT);
      ++WeakCrossingSIVsuccesses;
      if (!Result.DV[Level].Direction) {
        ++WeakCrossingSIVindependence;
        return true;
      }
      Result.DV[Level].Splitable = false;
      Result.DV[Level].Distance = SE->getZero(Delta->getType());
      return false;
    }
  }

  // check that Coeff divides Delta
  APInt APDelta = ConstDelta->getAPInt();
  APInt APCoeff = ConstCoeff->getAPInt();
  APInt Distance = APDelta; // these need to be initialzed
  APInt Remainder = APDelta;
  APInt::sdivrem(APDelta, APCoeff, Distance, Remainder);
  LLVM_DEBUG(dbgs() << "\t    Remainder = " << Remainder << "\n");
  if (Remainder != 0) {
    // Coeff doesn't divide Delta, no dependence
    ++WeakCrossingSIVindependence;
    ++WeakCrossingSIVsuccesses;
    return true;
  }
  LLVM_DEBUG(dbgs() << "\t    Distance = " << Distance << "\n");

  // if 2*Coeff doesn't divide Delta, then the equal direction isn't possible
  APInt Two = APInt(Distance.getBitWidth(), 2, true);
  Remainder = Distance.srem(Two);
  LLVM_DEBUG(dbgs() << "\t    Remainder = " << Remainder << "\n");
  if (Remainder != 0) {
    // Equal direction isn't possible
    Result.DV[Level].Direction &= unsigned(~Dependence::DVEntry::EQ);
    ++WeakCrossingSIVsuccesses;
  }
  return false;
}


// Kirch's algorithm, from
//
//        Optimizing Supercompilers for Supercomputers
//        Michael Wolfe
//        MIT Press, 1989
//
// Program 2.1, page 29.
// Computes the GCD of AM and BM.
// Also finds a solution to the equation ax - by = gcd(a, b).
// Returns true if dependence disproved; i.e., gcd does not divide Delta.
static bool findGCD(unsigned Bits, const APInt &AM, const APInt &BM,
                    const APInt &Delta, APInt &G, APInt &X, APInt &Y) {
  APInt A0(Bits, 1, true), A1(Bits, 0, true);
  APInt B0(Bits, 0, true), B1(Bits, 1, true);
  APInt G0 = AM.abs();
  APInt G1 = BM.abs();
  APInt Q = G0; // these need to be initialized
  APInt R = G0;
  APInt::sdivrem(G0, G1, Q, R);
  while (R != 0) {
    APInt A2 = A0 - Q*A1; A0 = A1; A1 = A2;
    APInt B2 = B0 - Q*B1; B0 = B1; B1 = B2;
    G0 = G1; G1 = R;
    APInt::sdivrem(G0, G1, Q, R);
  }
  G = G1;
  LLVM_DEBUG(dbgs() << "\t    GCD = " << G << "\n");
  X = AM.slt(0) ? -A1 : A1;
  Y = BM.slt(0) ? B1 : -B1;

  // make sure gcd divides Delta
  R = Delta.srem(G);
  if (R != 0)
    return true; // gcd doesn't divide Delta, no dependence
  Q = Delta.sdiv(G);
  X *= Q;
  Y *= Q;
  return false;
}

static APInt floorOfQuotient(const APInt &A, const APInt &B) {
  APInt Q = A; // these need to be initialized
  APInt R = A;
  APInt::sdivrem(A, B, Q, R);
  if (R == 0)
    return Q;
  if ((A.sgt(0) && B.sgt(0)) ||
      (A.slt(0) && B.slt(0)))
    return Q;
  else
    return Q - 1;
}

static APInt ceilingOfQuotient(const APInt &A, const APInt &B) {
  APInt Q = A; // these need to be initialized
  APInt R = A;
  APInt::sdivrem(A, B, Q, R);
  if (R == 0)
    return Q;
  if ((A.sgt(0) && B.sgt(0)) ||
      (A.slt(0) && B.slt(0)))
    return Q + 1;
  else
    return Q;
}


static
APInt maxAPInt(APInt A, APInt B) {
  return A.sgt(B) ? A : B;
}


static
APInt minAPInt(APInt A, APInt B) {
  return A.slt(B) ? A : B;
}


// exactSIVtest -
// When we have a pair of subscripts of the form [c1 + a1*i] and [c2 + a2*i],
// where i is an induction variable, c1 and c2 are loop invariant, and a1
// and a2 are constant, we can solve it exactly using an algorithm developed
// by Banerjee and Wolfe. See Section 2.5.3 in
//
//        Optimizing Supercompilers for Supercomputers
//        Michael Wolfe
//        MIT Press, 1989
//
// It's slower than the specialized tests (strong SIV, weak-zero SIV, etc),
// so use them if possible. They're also a bit better with symbolics and,
// in the case of the strong SIV test, can compute Distances.
//
// Return true if dependence disproved.
bool DependenceInfo::exactSIVtest(const SCEV *SrcCoeff, const SCEV *DstCoeff,
                                  const SCEV *SrcConst, const SCEV *DstConst,
                                  const Loop *CurLoop, unsigned Level,
                                  FullDependence &Result,
                                  Constraint &NewConstraint) const {
  LLVM_DEBUG(dbgs() << "\tExact SIV test\n");
  LLVM_DEBUG(dbgs() << "\t    SrcCoeff = " << *SrcCoeff << " = AM\n");
  LLVM_DEBUG(dbgs() << "\t    DstCoeff = " << *DstCoeff << " = BM\n");
  LLVM_DEBUG(dbgs() << "\t    SrcConst = " << *SrcConst << "\n");
  LLVM_DEBUG(dbgs() << "\t    DstConst = " << *DstConst << "\n");
  ++ExactSIVapplications;
  assert(0 < Level && Level <= CommonLevels && "Level out of range");
  Level--;
  Result.Consistent = false;
  const SCEV *Delta = SE->getMinusSCEV(DstConst, SrcConst);
  LLVM_DEBUG(dbgs() << "\t    Delta = " << *Delta << "\n");
  NewConstraint.setLine(SrcCoeff, SE->getNegativeSCEV(DstCoeff),
                        Delta, CurLoop);
  const SCEVConstant *ConstDelta = dyn_cast<SCEVConstant>(Delta);
  const SCEVConstant *ConstSrcCoeff = dyn_cast<SCEVConstant>(SrcCoeff);
  const SCEVConstant *ConstDstCoeff = dyn_cast<SCEVConstant>(DstCoeff);
  if (!ConstDelta || !ConstSrcCoeff || !ConstDstCoeff)
    return false;

  // find gcd
  APInt G, X, Y;
  APInt AM = ConstSrcCoeff->getAPInt();
  APInt BM = ConstDstCoeff->getAPInt();
  unsigned Bits = AM.getBitWidth();
  if (findGCD(Bits, AM, BM, ConstDelta->getAPInt(), G, X, Y)) {
    // gcd doesn't divide Delta, no dependence
    ++ExactSIVindependence;
    ++ExactSIVsuccesses;
    return true;
  }

  LLVM_DEBUG(dbgs() << "\t    X = " << X << ", Y = " << Y << "\n");

  // since SCEV construction normalizes, LM = 0
  APInt UM(Bits, 1, true);
  bool UMvalid = false;
  // UM is perhaps unavailable, let's check
  if (const SCEVConstant *CUB =
      collectConstantUpperBound(CurLoop, Delta->getType())) {
    UM = CUB->getAPInt();
    LLVM_DEBUG(dbgs() << "\t    UM = " << UM << "\n");
    UMvalid = true;
  }

  APInt TU(APInt::getSignedMaxValue(Bits));
  APInt TL(APInt::getSignedMinValue(Bits));

  // test(BM/G, LM-X) and test(-BM/G, X-UM)
  APInt TMUL = BM.sdiv(G);
  if (TMUL.sgt(0)) {
    TL = maxAPInt(TL, ceilingOfQuotient(-X, TMUL));
    LLVM_DEBUG(dbgs() << "\t    TL = " << TL << "\n");
    if (UMvalid) {
      TU = minAPInt(TU, floorOfQuotient(UM - X, TMUL));
      LLVM_DEBUG(dbgs() << "\t    TU = " << TU << "\n");
    }
  }
  else {
    TU = minAPInt(TU, floorOfQuotient(-X, TMUL));
    LLVM_DEBUG(dbgs() << "\t    TU = " << TU << "\n");
    if (UMvalid) {
      TL = maxAPInt(TL, ceilingOfQuotient(UM - X, TMUL));
      LLVM_DEBUG(dbgs() << "\t    TL = " << TL << "\n");
    }
  }

  // test(AM/G, LM-Y) and test(-AM/G, Y-UM)
  TMUL = AM.sdiv(G);
  if (TMUL.sgt(0)) {
    TL = maxAPInt(TL, ceilingOfQuotient(-Y, TMUL));
    LLVM_DEBUG(dbgs() << "\t    TL = " << TL << "\n");
    if (UMvalid) {
      TU = minAPInt(TU, floorOfQuotient(UM - Y, TMUL));
      LLVM_DEBUG(dbgs() << "\t    TU = " << TU << "\n");
    }
  }
  else {
    TU = minAPInt(TU, floorOfQuotient(-Y, TMUL));
    LLVM_DEBUG(dbgs() << "\t    TU = " << TU << "\n");
    if (UMvalid) {
      TL = maxAPInt(TL, ceilingOfQuotient(UM - Y, TMUL));
      LLVM_DEBUG(dbgs() << "\t    TL = " << TL << "\n");
    }
  }
  if (TL.sgt(TU)) {
    ++ExactSIVindependence;
    ++ExactSIVsuccesses;
    return true;
  }

  // explore directions
  unsigned NewDirection = Dependence::DVEntry::NONE;

  // less than
  APInt SaveTU(TU); // save these
  APInt SaveTL(TL);
  LLVM_DEBUG(dbgs() << "\t    exploring LT direction\n");
  TMUL = AM - BM;
  if (TMUL.sgt(0)) {
    TL = maxAPInt(TL, ceilingOfQuotient(X - Y + 1, TMUL));
    LLVM_DEBUG(dbgs() << "\t\t    TL = " << TL << "\n");
  }
  else {
    TU = minAPInt(TU, floorOfQuotient(X - Y + 1, TMUL));
    LLVM_DEBUG(dbgs() << "\t\t    TU = " << TU << "\n");
  }
  if (TL.sle(TU)) {
    NewDirection |= Dependence::DVEntry::LT;
    ++ExactSIVsuccesses;
  }

  // equal
  TU = SaveTU; // restore
  TL = SaveTL;
  LLVM_DEBUG(dbgs() << "\t    exploring EQ direction\n");
  if (TMUL.sgt(0)) {
    TL = maxAPInt(TL, ceilingOfQuotient(X - Y, TMUL));
    LLVM_DEBUG(dbgs() << "\t\t    TL = " << TL << "\n");
  }
  else {
    TU = minAPInt(TU, floorOfQuotient(X - Y, TMUL));
    LLVM_DEBUG(dbgs() << "\t\t    TU = " << TU << "\n");
  }
  TMUL = BM - AM;
  if (TMUL.sgt(0)) {
    TL = maxAPInt(TL, ceilingOfQuotient(Y - X, TMUL));
    LLVM_DEBUG(dbgs() << "\t\t    TL = " << TL << "\n");
  }
  else {
    TU = minAPInt(TU, floorOfQuotient(Y - X, TMUL));
    LLVM_DEBUG(dbgs() << "\t\t    TU = " << TU << "\n");
  }
  if (TL.sle(TU)) {
    NewDirection |= Dependence::DVEntry::EQ;
    ++ExactSIVsuccesses;
  }

  // greater than
  TU = SaveTU; // restore
  TL = SaveTL;
  LLVM_DEBUG(dbgs() << "\t    exploring GT direction\n");
  if (TMUL.sgt(0)) {
    TL = maxAPInt(TL, ceilingOfQuotient(Y - X + 1, TMUL));
    LLVM_DEBUG(dbgs() << "\t\t    TL = " << TL << "\n");
  }
  else {
    TU = minAPInt(TU, floorOfQuotient(Y - X + 1, TMUL));
    LLVM_DEBUG(dbgs() << "\t\t    TU = " << TU << "\n");
  }
  if (TL.sle(TU)) {
    NewDirection |= Dependence::DVEntry::GT;
    ++ExactSIVsuccesses;
  }

  // finished
  Result.DV[Level].Direction &= NewDirection;
  if (Result.DV[Level].Direction == Dependence::DVEntry::NONE)
    ++ExactSIVindependence;
  return Result.DV[Level].Direction == Dependence::DVEntry::NONE;
}



// Return true if the divisor evenly divides the dividend.
static
bool isRemainderZero(const SCEVConstant *Dividend,
                     const SCEVConstant *Divisor) {
  const APInt &ConstDividend = Dividend->getAPInt();
  const APInt &ConstDivisor = Divisor->getAPInt();
  return ConstDividend.srem(ConstDivisor) == 0;
}


// weakZeroSrcSIVtest -
// From the paper, Practical Dependence Testing, Section 4.2.2
//
// When we have a pair of subscripts of the form [c1] and [c2 + a*i],
// where i is an induction variable, c1 and c2 are loop invariant,
// and a is a constant, we can solve it exactly using the
// Weak-Zero SIV test.
//
// Given
//
//    c1 = c2 + a*i
//
// we get
//
//    (c1 - c2)/a = i
//
// If i is not an integer, there's no dependence.
// If i < 0 or > UB, there's no dependence.
// If i = 0, the direction is >= and peeling the
// 1st iteration will break the dependence.
// If i = UB, the direction is <= and peeling the
// last iteration will break the dependence.
// Otherwise, the direction is *.
//
// Can prove independence. Failing that, we can sometimes refine
// the directions. Can sometimes show that first or last
// iteration carries all the dependences (so worth peeling).
//
// (see also weakZeroDstSIVtest)
//
// Return true if dependence disproved.
bool DependenceInfo::weakZeroSrcSIVtest(const SCEV *DstCoeff,
                                        const SCEV *SrcConst,
                                        const SCEV *DstConst,
                                        const Loop *CurLoop, unsigned Level,
                                        FullDependence &Result,
                                        Constraint &NewConstraint) const {
  // For the WeakSIV test, it's possible the loop isn't common to
  // the Src and Dst loops. If it isn't, then there's no need to
  // record a direction.
  LLVM_DEBUG(dbgs() << "\tWeak-Zero (src) SIV test\n");
  LLVM_DEBUG(dbgs() << "\t    DstCoeff = " << *DstCoeff << "\n");
  LLVM_DEBUG(dbgs() << "\t    SrcConst = " << *SrcConst << "\n");
  LLVM_DEBUG(dbgs() << "\t    DstConst = " << *DstConst << "\n");
  ++WeakZeroSIVapplications;
  assert(0 < Level && Level <= MaxLevels && "Level out of range");
  Level--;
  Result.Consistent = false;
  const SCEV *Delta = SE->getMinusSCEV(SrcConst, DstConst);
  NewConstraint.setLine(SE->getZero(Delta->getType()), DstCoeff, Delta,
                        CurLoop);
  LLVM_DEBUG(dbgs() << "\t    Delta = " << *Delta << "\n");
  if (isKnownPredicate(CmpInst::ICMP_EQ, SrcConst, DstConst)) {
    if (Level < CommonLevels) {
      Result.DV[Level].Direction &= Dependence::DVEntry::GE;
      Result.DV[Level].PeelFirst = true;
      ++WeakZeroSIVsuccesses;
    }
    return false; // dependences caused by first iteration
  }
  const SCEVConstant *ConstCoeff = dyn_cast<SCEVConstant>(DstCoeff);
  if (!ConstCoeff)
    return false;
  const SCEV *AbsCoeff =
    SE->isKnownNegative(ConstCoeff) ?
    SE->getNegativeSCEV(ConstCoeff) : ConstCoeff;
  const SCEV *NewDelta =
    SE->isKnownNegative(ConstCoeff) ? SE->getNegativeSCEV(Delta) : Delta;

  // check that Delta/SrcCoeff < iteration count
  // really check NewDelta < count*AbsCoeff
  if (const SCEV *UpperBound = collectUpperBound(CurLoop, Delta->getType())) {
    LLVM_DEBUG(dbgs() << "\t    UpperBound = " << *UpperBound << "\n");
    const SCEV *Product = SE->getMulExpr(AbsCoeff, UpperBound);
    if (isKnownPredicate(CmpInst::ICMP_SGT, NewDelta, Product)) {
      ++WeakZeroSIVindependence;
      ++WeakZeroSIVsuccesses;
      return true;
    }
    if (isKnownPredicate(CmpInst::ICMP_EQ, NewDelta, Product)) {
      // dependences caused by last iteration
      if (Level < CommonLevels) {
        Result.DV[Level].Direction &= Dependence::DVEntry::LE;
        Result.DV[Level].PeelLast = true;
        ++WeakZeroSIVsuccesses;
      }
      return false;
    }
  }

  // check that Delta/SrcCoeff >= 0
  // really check that NewDelta >= 0
  if (SE->isKnownNegative(NewDelta)) {
    // No dependence, newDelta < 0
    ++WeakZeroSIVindependence;
    ++WeakZeroSIVsuccesses;
    return true;
  }

  // if SrcCoeff doesn't divide Delta, then no dependence
  if (isa<SCEVConstant>(Delta) &&
      !isRemainderZero(cast<SCEVConstant>(Delta), ConstCoeff)) {
    ++WeakZeroSIVindependence;
    ++WeakZeroSIVsuccesses;
    return true;
  }
  return false;
}


// weakZeroDstSIVtest -
// From the paper, Practical Dependence Testing, Section 4.2.2
//
// When we have a pair of subscripts of the form [c1 + a*i] and [c2],
// where i is an induction variable, c1 and c2 are loop invariant,
// and a is a constant, we can solve it exactly using the
// Weak-Zero SIV test.
//
// Given
//
//    c1 + a*i = c2
//
// we get
//
//    i = (c2 - c1)/a
//
// If i is not an integer, there's no dependence.
// If i < 0 or > UB, there's no dependence.
// If i = 0, the direction is <= and peeling the
// 1st iteration will break the dependence.
// If i = UB, the direction is >= and peeling the
// last iteration will break the dependence.
// Otherwise, the direction is *.
//
// Can prove independence. Failing that, we can sometimes refine
// the directions. Can sometimes show that first or last
// iteration carries all the dependences (so worth peeling).
//
// (see also weakZeroSrcSIVtest)
//
// Return true if dependence disproved.
bool DependenceInfo::weakZeroDstSIVtest(const SCEV *SrcCoeff,
                                        const SCEV *SrcConst,
                                        const SCEV *DstConst,
                                        const Loop *CurLoop, unsigned Level,
                                        FullDependence &Result,
                                        Constraint &NewConstraint) const {
  // For the WeakSIV test, it's possible the loop isn't common to the
  // Src and Dst loops. If it isn't, then there's no need to record a direction.
  LLVM_DEBUG(dbgs() << "\tWeak-Zero (dst) SIV test\n");
  LLVM_DEBUG(dbgs() << "\t    SrcCoeff = " << *SrcCoeff << "\n");
  LLVM_DEBUG(dbgs() << "\t    SrcConst = " << *SrcConst << "\n");
  LLVM_DEBUG(dbgs() << "\t    DstConst = " << *DstConst << "\n");
  ++WeakZeroSIVapplications;
  assert(0 < Level && Level <= SrcLevels && "Level out of range");
  Level--;
  Result.Consistent = false;
  const SCEV *Delta = SE->getMinusSCEV(DstConst, SrcConst);
  NewConstraint.setLine(SrcCoeff, SE->getZero(Delta->getType()), Delta,
                        CurLoop);
  LLVM_DEBUG(dbgs() << "\t    Delta = " << *Delta << "\n");
  if (isKnownPredicate(CmpInst::ICMP_EQ, DstConst, SrcConst)) {
    if (Level < CommonLevels) {
      Result.DV[Level].Direction &= Dependence::DVEntry::LE;
      Result.DV[Level].PeelFirst = true;
      ++WeakZeroSIVsuccesses;
    }
    return false; // dependences caused by first iteration
  }
  const SCEVConstant *ConstCoeff = dyn_cast<SCEVConstant>(SrcCoeff);
  if (!ConstCoeff)
    return false;
  const SCEV *AbsCoeff =
    SE->isKnownNegative(ConstCoeff) ?
    SE->getNegativeSCEV(ConstCoeff) : ConstCoeff;
  const SCEV *NewDelta =
    SE->isKnownNegative(ConstCoeff) ? SE->getNegativeSCEV(Delta) : Delta;

  // check that Delta/SrcCoeff < iteration count
  // really check NewDelta < count*AbsCoeff
  if (const SCEV *UpperBound = collectUpperBound(CurLoop, Delta->getType())) {
    LLVM_DEBUG(dbgs() << "\t    UpperBound = " << *UpperBound << "\n");
    const SCEV *Product = SE->getMulExpr(AbsCoeff, UpperBound);
    if (isKnownPredicate(CmpInst::ICMP_SGT, NewDelta, Product)) {
      ++WeakZeroSIVindependence;
      ++WeakZeroSIVsuccesses;
      return true;
    }
    if (isKnownPredicate(CmpInst::ICMP_EQ, NewDelta, Product)) {
      // dependences caused by last iteration
      if (Level < CommonLevels) {
        Result.DV[Level].Direction &= Dependence::DVEntry::GE;
        Result.DV[Level].PeelLast = true;
        ++WeakZeroSIVsuccesses;
      }
      return false;
    }
  }

  // check that Delta/SrcCoeff >= 0
  // really check that NewDelta >= 0
  if (SE->isKnownNegative(NewDelta)) {
    // No dependence, newDelta < 0
    ++WeakZeroSIVindependence;
    ++WeakZeroSIVsuccesses;
    return true;
  }

  // if SrcCoeff doesn't divide Delta, then no dependence
  if (isa<SCEVConstant>(Delta) &&
      !isRemainderZero(cast<SCEVConstant>(Delta), ConstCoeff)) {
    ++WeakZeroSIVindependence;
    ++WeakZeroSIVsuccesses;
    return true;
  }
  return false;
}


// exactRDIVtest - Tests the RDIV subscript pair for dependence.
// Things of the form [c1 + a*i] and [c2 + b*j],
// where i and j are induction variable, c1 and c2 are loop invariant,
// and a and b are constants.
// Returns true if any possible dependence is disproved.
// Marks the result as inconsistent.
// Works in some cases that symbolicRDIVtest doesn't, and vice versa.
bool DependenceInfo::exactRDIVtest(const SCEV *SrcCoeff, const SCEV *DstCoeff,
                                   const SCEV *SrcConst, const SCEV *DstConst,
                                   const Loop *SrcLoop, const Loop *DstLoop,
                                   FullDependence &Result) const {
  LLVM_DEBUG(dbgs() << "\tExact RDIV test\n");
  LLVM_DEBUG(dbgs() << "\t    SrcCoeff = " << *SrcCoeff << " = AM\n");
  LLVM_DEBUG(dbgs() << "\t    DstCoeff = " << *DstCoeff << " = BM\n");
  LLVM_DEBUG(dbgs() << "\t    SrcConst = " << *SrcConst << "\n");
  LLVM_DEBUG(dbgs() << "\t    DstConst = " << *DstConst << "\n");
  ++ExactRDIVapplications;
  Result.Consistent = false;
  const SCEV *Delta = SE->getMinusSCEV(DstConst, SrcConst);
  LLVM_DEBUG(dbgs() << "\t    Delta = " << *Delta << "\n");
  const SCEVConstant *ConstDelta = dyn_cast<SCEVConstant>(Delta);
  const SCEVConstant *ConstSrcCoeff = dyn_cast<SCEVConstant>(SrcCoeff);
  const SCEVConstant *ConstDstCoeff = dyn_cast<SCEVConstant>(DstCoeff);
  if (!ConstDelta || !ConstSrcCoeff || !ConstDstCoeff)
    return false;

  // find gcd
  APInt G, X, Y;
  APInt AM = ConstSrcCoeff->getAPInt();
  APInt BM = ConstDstCoeff->getAPInt();
  unsigned Bits = AM.getBitWidth();
  if (findGCD(Bits, AM, BM, ConstDelta->getAPInt(), G, X, Y)) {
    // gcd doesn't divide Delta, no dependence
    ++ExactRDIVindependence;
    return true;
  }

  LLVM_DEBUG(dbgs() << "\t    X = " << X << ", Y = " << Y << "\n");

  // since SCEV construction seems to normalize, LM = 0
  APInt SrcUM(Bits, 1, true);
  bool SrcUMvalid = false;
  // SrcUM is perhaps unavailable, let's check
  if (const SCEVConstant *UpperBound =
      collectConstantUpperBound(SrcLoop, Delta->getType())) {
    SrcUM = UpperBound->getAPInt();
    LLVM_DEBUG(dbgs() << "\t    SrcUM = " << SrcUM << "\n");
    SrcUMvalid = true;
  }

  APInt DstUM(Bits, 1, true);
  bool DstUMvalid = false;
  // UM is perhaps unavailable, let's check
  if (const SCEVConstant *UpperBound =
      collectConstantUpperBound(DstLoop, Delta->getType())) {
    DstUM = UpperBound->getAPInt();
    LLVM_DEBUG(dbgs() << "\t    DstUM = " << DstUM << "\n");
    DstUMvalid = true;
  }

  APInt TU(APInt::getSignedMaxValue(Bits));
  APInt TL(APInt::getSignedMinValue(Bits));

  // test(BM/G, LM-X) and test(-BM/G, X-UM)
  APInt TMUL = BM.sdiv(G);
  if (TMUL.sgt(0)) {
    TL = maxAPInt(TL, ceilingOfQuotient(-X, TMUL));
    LLVM_DEBUG(dbgs() << "\t    TL = " << TL << "\n");
    if (SrcUMvalid) {
      TU = minAPInt(TU, floorOfQuotient(SrcUM - X, TMUL));
      LLVM_DEBUG(dbgs() << "\t    TU = " << TU << "\n");
    }
  }
  else {
    TU = minAPInt(TU, floorOfQuotient(-X, TMUL));
    LLVM_DEBUG(dbgs() << "\t    TU = " << TU << "\n");
    if (SrcUMvalid) {
      TL = maxAPInt(TL, ceilingOfQuotient(SrcUM - X, TMUL));
      LLVM_DEBUG(dbgs() << "\t    TL = " << TL << "\n");
    }
  }

  // test(AM/G, LM-Y) and test(-AM/G, Y-UM)
  TMUL = AM.sdiv(G);
  if (TMUL.sgt(0)) {
    TL = maxAPInt(TL, ceilingOfQuotient(-Y, TMUL));
    LLVM_DEBUG(dbgs() << "\t    TL = " << TL << "\n");
    if (DstUMvalid) {
      TU = minAPInt(TU, floorOfQuotient(DstUM - Y, TMUL));
      LLVM_DEBUG(dbgs() << "\t    TU = " << TU << "\n");
    }
  }
  else {
    TU = minAPInt(TU, floorOfQuotient(-Y, TMUL));
    LLVM_DEBUG(dbgs() << "\t    TU = " << TU << "\n");
    if (DstUMvalid) {
      TL = maxAPInt(TL, ceilingOfQuotient(DstUM - Y, TMUL));
      LLVM_DEBUG(dbgs() << "\t    TL = " << TL << "\n");
    }
  }
  if (TL.sgt(TU))
    ++ExactRDIVindependence;
  return TL.sgt(TU);
}


// symbolicRDIVtest -
// In Section 4.5 of the Practical Dependence Testing paper,the authors
// introduce a special case of Banerjee's Inequalities (also called the
// Extreme-Value Test) that can handle some of the SIV and RDIV cases,
// particularly cases with symbolics. Since it's only able to disprove
// dependence (not compute distances or directions), we'll use it as a
// fall back for the other tests.
//
// When we have a pair of subscripts of the form [c1 + a1*i] and [c2 + a2*j]
// where i and j are induction variables and c1 and c2 are loop invariants,
// we can use the symbolic tests to disprove some dependences, serving as a
// backup for the RDIV test. Note that i and j can be the same variable,
// letting this test serve as a backup for the various SIV tests.
//
// For a dependence to exist, c1 + a1*i must equal c2 + a2*j for some
//  0 <= i <= N1 and some 0 <= j <= N2, where N1 and N2 are the (normalized)
// loop bounds for the i and j loops, respectively. So, ...
//
// c1 + a1*i = c2 + a2*j
// a1*i - a2*j = c2 - c1
//
// To test for a dependence, we compute c2 - c1 and make sure it's in the
// range of the maximum and minimum possible values of a1*i - a2*j.
// Considering the signs of a1 and a2, we have 4 possible cases:
//
// 1) If a1 >= 0 and a2 >= 0, then
//        a1*0 - a2*N2 <= c2 - c1 <= a1*N1 - a2*0
//              -a2*N2 <= c2 - c1 <= a1*N1
//
// 2) If a1 >= 0 and a2 <= 0, then
//        a1*0 - a2*0 <= c2 - c1 <= a1*N1 - a2*N2
//                  0 <= c2 - c1 <= a1*N1 - a2*N2
//
// 3) If a1 <= 0 and a2 >= 0, then
//        a1*N1 - a2*N2 <= c2 - c1 <= a1*0 - a2*0
//        a1*N1 - a2*N2 <= c2 - c1 <= 0
//
// 4) If a1 <= 0 and a2 <= 0, then
//        a1*N1 - a2*0  <= c2 - c1 <= a1*0 - a2*N2
//        a1*N1         <= c2 - c1 <=       -a2*N2
//
// return true if dependence disproved
bool DependenceInfo::symbolicRDIVtest(const SCEV *A1, const SCEV *A2,
                                      const SCEV *C1, const SCEV *C2,
                                      const Loop *Loop1,
                                      const Loop *Loop2) const {
  ++SymbolicRDIVapplications;
  LLVM_DEBUG(dbgs() << "\ttry symbolic RDIV test\n");
  LLVM_DEBUG(dbgs() << "\t    A1 = " << *A1);
  LLVM_DEBUG(dbgs() << ", type = " << *A1->getType() << "\n");
  LLVM_DEBUG(dbgs() << "\t    A2 = " << *A2 << "\n");
  LLVM_DEBUG(dbgs() << "\t    C1 = " << *C1 << "\n");
  LLVM_DEBUG(dbgs() << "\t    C2 = " << *C2 << "\n");
  const SCEV *N1 = collectUpperBound(Loop1, A1->getType());
  const SCEV *N2 = collectUpperBound(Loop2, A1->getType());
  LLVM_DEBUG(if (N1) dbgs() << "\t    N1 = " << *N1 << "\n");
  LLVM_DEBUG(if (N2) dbgs() << "\t    N2 = " << *N2 << "\n");
  const SCEV *C2_C1 = SE->getMinusSCEV(C2, C1);
  const SCEV *C1_C2 = SE->getMinusSCEV(C1, C2);
  LLVM_DEBUG(dbgs() << "\t    C2 - C1 = " << *C2_C1 << "\n");
  LLVM_DEBUG(dbgs() << "\t    C1 - C2 = " << *C1_C2 << "\n");
  if (SE->isKnownNonNegative(A1)) {
    if (SE->isKnownNonNegative(A2)) {
      // A1 >= 0 && A2 >= 0
      if (N1) {
        // make sure that c2 - c1 <= a1*N1
        const SCEV *A1N1 = SE->getMulExpr(A1, N1);
        LLVM_DEBUG(dbgs() << "\t    A1*N1 = " << *A1N1 << "\n");
        if (isKnownPredicate(CmpInst::ICMP_SGT, C2_C1, A1N1)) {
          ++SymbolicRDIVindependence;
          return true;
        }
      }
      if (N2) {
        // make sure that -a2*N2 <= c2 - c1, or a2*N2 >= c1 - c2
        const SCEV *A2N2 = SE->getMulExpr(A2, N2);
        LLVM_DEBUG(dbgs() << "\t    A2*N2 = " << *A2N2 << "\n");
        if (isKnownPredicate(CmpInst::ICMP_SLT, A2N2, C1_C2)) {
          ++SymbolicRDIVindependence;
          return true;
        }
      }
    }
    else if (SE->isKnownNonPositive(A2)) {
      // a1 >= 0 && a2 <= 0
      if (N1 && N2) {
        // make sure that c2 - c1 <= a1*N1 - a2*N2
        const SCEV *A1N1 = SE->getMulExpr(A1, N1);
        const SCEV *A2N2 = SE->getMulExpr(A2, N2);
        const SCEV *A1N1_A2N2 = SE->getMinusSCEV(A1N1, A2N2);
        LLVM_DEBUG(dbgs() << "\t    A1*N1 - A2*N2 = " << *A1N1_A2N2 << "\n");
        if (isKnownPredicate(CmpInst::ICMP_SGT, C2_C1, A1N1_A2N2)) {
          ++SymbolicRDIVindependence;
          return true;
        }
      }
      // make sure that 0 <= c2 - c1
      if (SE->isKnownNegative(C2_C1)) {
        ++SymbolicRDIVindependence;
        return true;
      }
    }
  }
  else if (SE->isKnownNonPositive(A1)) {
    if (SE->isKnownNonNegative(A2)) {
      // a1 <= 0 && a2 >= 0
      if (N1 && N2) {
        // make sure that a1*N1 - a2*N2 <= c2 - c1
        const SCEV *A1N1 = SE->getMulExpr(A1, N1);
        const SCEV *A2N2 = SE->getMulExpr(A2, N2);
        const SCEV *A1N1_A2N2 = SE->getMinusSCEV(A1N1, A2N2);
        LLVM_DEBUG(dbgs() << "\t    A1*N1 - A2*N2 = " << *A1N1_A2N2 << "\n");
        if (isKnownPredicate(CmpInst::ICMP_SGT, A1N1_A2N2, C2_C1)) {
          ++SymbolicRDIVindependence;
          return true;
        }
      }
      // make sure that c2 - c1 <= 0
      if (SE->isKnownPositive(C2_C1)) {
        ++SymbolicRDIVindependence;
        return true;
      }
    }
    else if (SE->isKnownNonPositive(A2)) {
      // a1 <= 0 && a2 <= 0
      if (N1) {
        // make sure that a1*N1 <= c2 - c1
        const SCEV *A1N1 = SE->getMulExpr(A1, N1);
        LLVM_DEBUG(dbgs() << "\t    A1*N1 = " << *A1N1 << "\n");
        if (isKnownPredicate(CmpInst::ICMP_SGT, A1N1, C2_C1)) {
          ++SymbolicRDIVindependence;
          return true;
        }
      }
      if (N2) {
        // make sure that c2 - c1 <= -a2*N2, or c1 - c2 >= a2*N2
        const SCEV *A2N2 = SE->getMulExpr(A2, N2);
        LLVM_DEBUG(dbgs() << "\t    A2*N2 = " << *A2N2 << "\n");
        if (isKnownPredicate(CmpInst::ICMP_SLT, C1_C2, A2N2)) {
          ++SymbolicRDIVindependence;
          return true;
        }
      }
    }
  }
  return false;
}


// testSIV -
// When we have a pair of subscripts of the form [c1 + a1*i] and [c2 - a2*i]
// where i is an induction variable, c1 and c2 are loop invariant, and a1 and
// a2 are constant, we attack it with an SIV test. While they can all be
// solved with the Exact SIV test, it's worthwhile to use simpler tests when
// they apply; they're cheaper and sometimes more precise.
//
// Return true if dependence disproved.
bool DependenceInfo::testSIV(const SCEV *Src, const SCEV *Dst, unsigned &Level,
                             FullDependence &Result, Constraint &NewConstraint,
                             const SCEV *&SplitIter) const {
  LLVM_DEBUG(dbgs() << "    src = " << *Src << "\n");
  LLVM_DEBUG(dbgs() << "    dst = " << *Dst << "\n");
  const SCEVAddRecExpr *SrcAddRec = dyn_cast<SCEVAddRecExpr>(Src);
  const SCEVAddRecExpr *DstAddRec = dyn_cast<SCEVAddRecExpr>(Dst);
  if (SrcAddRec && DstAddRec) {
    const SCEV *SrcConst = SrcAddRec->getStart();
    const SCEV *DstConst = DstAddRec->getStart();
    const SCEV *SrcCoeff = SrcAddRec->getStepRecurrence(*SE);
    const SCEV *DstCoeff = DstAddRec->getStepRecurrence(*SE);
    const Loop *CurLoop = SrcAddRec->getLoop();
    assert(CurLoop == DstAddRec->getLoop() &&
           "both loops in SIV should be same");
    Level = mapSrcLoop(CurLoop);
    bool disproven;
    if (SrcCoeff == DstCoeff)
      disproven = strongSIVtest(SrcCoeff, SrcConst, DstConst, CurLoop,
                                Level, Result, NewConstraint);
    else if (SrcCoeff == SE->getNegativeSCEV(DstCoeff))
      disproven = weakCrossingSIVtest(SrcCoeff, SrcConst, DstConst, CurLoop,
                                      Level, Result, NewConstraint, SplitIter);
    else
      disproven = exactSIVtest(SrcCoeff, DstCoeff, SrcConst, DstConst, CurLoop,
                               Level, Result, NewConstraint);
    return disproven ||
      gcdMIVtest(Src, Dst, Result) ||
      symbolicRDIVtest(SrcCoeff, DstCoeff, SrcConst, DstConst, CurLoop, CurLoop);
  }
  if (SrcAddRec) {
    const SCEV *SrcConst = SrcAddRec->getStart();
    const SCEV *SrcCoeff = SrcAddRec->getStepRecurrence(*SE);
    const SCEV *DstConst = Dst;
    const Loop *CurLoop = SrcAddRec->getLoop();
    Level = mapSrcLoop(CurLoop);
    return weakZeroDstSIVtest(SrcCoeff, SrcConst, DstConst, CurLoop,
                              Level, Result, NewConstraint) ||
      gcdMIVtest(Src, Dst, Result);
  }
  if (DstAddRec) {
    const SCEV *DstConst = DstAddRec->getStart();
    const SCEV *DstCoeff = DstAddRec->getStepRecurrence(*SE);
    const SCEV *SrcConst = Src;
    const Loop *CurLoop = DstAddRec->getLoop();
    Level = mapDstLoop(CurLoop);
    return weakZeroSrcSIVtest(DstCoeff, SrcConst, DstConst,
                              CurLoop, Level, Result, NewConstraint) ||
      gcdMIVtest(Src, Dst, Result);
  }
  llvm_unreachable("SIV test expected at least one AddRec");
  return false;
}


// testRDIV -
// When we have a pair of subscripts of the form [c1 + a1*i] and [c2 + a2*j]
// where i and j are induction variables, c1 and c2 are loop invariant,
// and a1 and a2 are constant, we can solve it exactly with an easy adaptation
// of the Exact SIV test, the Restricted Double Index Variable (RDIV) test.
// It doesn't make sense to talk about distance or direction in this case,
// so there's no point in making special versions of the Strong SIV test or
// the Weak-crossing SIV test.
//
// With minor algebra, this test can also be used for things like
// [c1 + a1*i + a2*j][c2].
//
// Return true if dependence disproved.
bool DependenceInfo::testRDIV(const SCEV *Src, const SCEV *Dst,
                              FullDependence &Result) const {
  // we have 3 possible situations here:
  //   1) [a*i + b] and [c*j + d]
  //   2) [a*i + c*j + b] and [d]
  //   3) [b] and [a*i + c*j + d]
  // We need to find what we've got and get organized

  const SCEV *SrcConst, *DstConst;
  const SCEV *SrcCoeff, *DstCoeff;
  const Loop *SrcLoop, *DstLoop;

  LLVM_DEBUG(dbgs() << "    src = " << *Src << "\n");
  LLVM_DEBUG(dbgs() << "    dst = " << *Dst << "\n");
  const SCEVAddRecExpr *SrcAddRec = dyn_cast<SCEVAddRecExpr>(Src);
  const SCEVAddRecExpr *DstAddRec = dyn_cast<SCEVAddRecExpr>(Dst);
  if (SrcAddRec && DstAddRec) {
    SrcConst = SrcAddRec->getStart();
    SrcCoeff = SrcAddRec->getStepRecurrence(*SE);
    SrcLoop = SrcAddRec->getLoop();
    DstConst = DstAddRec->getStart();
    DstCoeff = DstAddRec->getStepRecurrence(*SE);
    DstLoop = DstAddRec->getLoop();
  }
  else if (SrcAddRec) {
    if (const SCEVAddRecExpr *tmpAddRec =
        dyn_cast<SCEVAddRecExpr>(SrcAddRec->getStart())) {
      SrcConst = tmpAddRec->getStart();
      SrcCoeff = tmpAddRec->getStepRecurrence(*SE);
      SrcLoop = tmpAddRec->getLoop();
      DstConst = Dst;
      DstCoeff = SE->getNegativeSCEV(SrcAddRec->getStepRecurrence(*SE));
      DstLoop = SrcAddRec->getLoop();
    }
    else
      llvm_unreachable("RDIV reached by surprising SCEVs");
  }
  else if (DstAddRec) {
    if (const SCEVAddRecExpr *tmpAddRec =
        dyn_cast<SCEVAddRecExpr>(DstAddRec->getStart())) {
      DstConst = tmpAddRec->getStart();
      DstCoeff = tmpAddRec->getStepRecurrence(*SE);
      DstLoop = tmpAddRec->getLoop();
      SrcConst = Src;
      SrcCoeff = SE->getNegativeSCEV(DstAddRec->getStepRecurrence(*SE));
      SrcLoop = DstAddRec->getLoop();
    }
    else
      llvm_unreachable("RDIV reached by surprising SCEVs");
  }
  else
    llvm_unreachable("RDIV expected at least one AddRec");
  return exactRDIVtest(SrcCoeff, DstCoeff,
                       SrcConst, DstConst,
                       SrcLoop, DstLoop,
                       Result) ||
    gcdMIVtest(Src, Dst, Result) ||
    symbolicRDIVtest(SrcCoeff, DstCoeff,
                     SrcConst, DstConst,
                     SrcLoop, DstLoop);
}


// Tests the single-subscript MIV pair (Src and Dst) for dependence.
// Return true if dependence disproved.
// Can sometimes refine direction vectors.
bool DependenceInfo::testMIV(const SCEV *Src, const SCEV *Dst,
                             const SmallBitVector &Loops,
                             FullDependence &Result) const {
  LLVM_DEBUG(dbgs() << "    src = " << *Src << "\n");
  LLVM_DEBUG(dbgs() << "    dst = " << *Dst << "\n");
  Result.Consistent = false;
  return gcdMIVtest(Src, Dst, Result) ||
    banerjeeMIVtest(Src, Dst, Loops, Result);
}


// Given a product, e.g., 10*X*Y, returns the first constant operand,
// in this case 10. If there is no constant part, returns NULL.
static
const SCEVConstant *getConstantPart(const SCEV *Expr) {
  if (const auto *Constant = dyn_cast<SCEVConstant>(Expr))
    return Constant;
  else if (const auto *Product = dyn_cast<SCEVMulExpr>(Expr))
    if (const auto *Constant = dyn_cast<SCEVConstant>(Product->getOperand(0)))
      return Constant;
  return nullptr;
}


//===----------------------------------------------------------------------===//
// gcdMIVtest -
// Tests an MIV subscript pair for dependence.
// Returns true if any possible dependence is disproved.
// Marks the result as inconsistent.
// Can sometimes disprove the equal direction for 1 or more loops,
// as discussed in Michael Wolfe's book,
// High Performance Compilers for Parallel Computing, page 235.
//
// We spend some effort (code!) to handle cases like
// [10*i + 5*N*j + 15*M + 6], where i and j are induction variables,
// but M and N are just loop-invariant variables.
// This should help us handle linearized subscripts;
// also makes this test a useful backup to the various SIV tests.
//
// It occurs to me that the presence of loop-invariant variables
// changes the nature of the test from "greatest common divisor"
// to "a common divisor".
bool DependenceInfo::gcdMIVtest(const SCEV *Src, const SCEV *Dst,
                                FullDependence &Result) const {
  LLVM_DEBUG(dbgs() << "starting gcd\n");
  ++GCDapplications;
  unsigned BitWidth = SE->getTypeSizeInBits(Src->getType());
  APInt RunningGCD = APInt::getNullValue(BitWidth);

  // Examine Src coefficients.
  // Compute running GCD and record source constant.
  // Because we're looking for the constant at the end of the chain,
  // we can't quit the loop just because the GCD == 1.
  const SCEV *Coefficients = Src;
  while (const SCEVAddRecExpr *AddRec =
         dyn_cast<SCEVAddRecExpr>(Coefficients)) {
    const SCEV *Coeff = AddRec->getStepRecurrence(*SE);
    // If the coefficient is the product of a constant and other stuff,
    // we can use the constant in the GCD computation.
    const auto *Constant = getConstantPart(Coeff);
    if (!Constant)
      return false;
    APInt ConstCoeff = Constant->getAPInt();
    RunningGCD = APIntOps::GreatestCommonDivisor(RunningGCD, ConstCoeff.abs());
    Coefficients = AddRec->getStart();
  }
  const SCEV *SrcConst = Coefficients;

  // Examine Dst coefficients.
  // Compute running GCD and record destination constant.
  // Because we're looking for the constant at the end of the chain,
  // we can't quit the loop just because the GCD == 1.
  Coefficients = Dst;
  while (const SCEVAddRecExpr *AddRec =
         dyn_cast<SCEVAddRecExpr>(Coefficients)) {
    const SCEV *Coeff = AddRec->getStepRecurrence(*SE);
    // If the coefficient is the product of a constant and other stuff,
    // we can use the constant in the GCD computation.
    const auto *Constant = getConstantPart(Coeff);
    if (!Constant)
      return false;
    APInt ConstCoeff = Constant->getAPInt();
    RunningGCD = APIntOps::GreatestCommonDivisor(RunningGCD, ConstCoeff.abs());
    Coefficients = AddRec->getStart();
  }
  const SCEV *DstConst = Coefficients;

  APInt ExtraGCD = APInt::getNullValue(BitWidth);
  const SCEV *Delta = SE->getMinusSCEV(DstConst, SrcConst);
  LLVM_DEBUG(dbgs() << "    Delta = " << *Delta << "\n");
  const SCEVConstant *Constant = dyn_cast<SCEVConstant>(Delta);
  if (const SCEVAddExpr *Sum = dyn_cast<SCEVAddExpr>(Delta)) {
    // If Delta is a sum of products, we may be able to make further progress.
    for (unsigned Op = 0, Ops = Sum->getNumOperands(); Op < Ops; Op++) {
      const SCEV *Operand = Sum->getOperand(Op);
      if (isa<SCEVConstant>(Operand)) {
        assert(!Constant && "Surprised to find multiple constants");
        Constant = cast<SCEVConstant>(Operand);
      }
      else if (const SCEVMulExpr *Product = dyn_cast<SCEVMulExpr>(Operand)) {
        // Search for constant operand to participate in GCD;
        // If none found; return false.
        const SCEVConstant *ConstOp = getConstantPart(Product);
        if (!ConstOp)
          return false;
        APInt ConstOpValue = ConstOp->getAPInt();
        ExtraGCD = APIntOps::GreatestCommonDivisor(ExtraGCD,
                                                   ConstOpValue.abs());
      }
      else
        return false;
    }
  }
  if (!Constant)
    return false;
  APInt ConstDelta = cast<SCEVConstant>(Constant)->getAPInt();
  LLVM_DEBUG(dbgs() << "    ConstDelta = " << ConstDelta << "\n");
  if (ConstDelta == 0)
    return false;
  RunningGCD = APIntOps::GreatestCommonDivisor(RunningGCD, ExtraGCD);
  LLVM_DEBUG(dbgs() << "    RunningGCD = " << RunningGCD << "\n");
  APInt Remainder = ConstDelta.srem(RunningGCD);
  if (Remainder != 0) {
    ++GCDindependence;
    return true;
  }

  // Try to disprove equal directions.
  // For example, given a subscript pair [3*i + 2*j] and [i' + 2*j' - 1],
  // the code above can't disprove the dependence because the GCD = 1.
  // So we consider what happen if i = i' and what happens if j = j'.
  // If i = i', we can simplify the subscript to [2*i + 2*j] and [2*j' - 1],
  // which is infeasible, so we can disallow the = direction for the i level.
  // Setting j = j' doesn't help matters, so we end up with a direction vector
  // of [<>, *]
  //
  // Given A[5*i + 10*j*M + 9*M*N] and A[15*i + 20*j*M - 21*N*M + 5],
  // we need to remember that the constant part is 5 and the RunningGCD should
  // be initialized to ExtraGCD = 30.
  LLVM_DEBUG(dbgs() << "    ExtraGCD = " << ExtraGCD << '\n');

  bool Improved = false;
  Coefficients = Src;
  while (const SCEVAddRecExpr *AddRec =
         dyn_cast<SCEVAddRecExpr>(Coefficients)) {
    Coefficients = AddRec->getStart();
    const Loop *CurLoop = AddRec->getLoop();
    RunningGCD = ExtraGCD;
    const SCEV *SrcCoeff = AddRec->getStepRecurrence(*SE);
    const SCEV *DstCoeff = SE->getMinusSCEV(SrcCoeff, SrcCoeff);
    const SCEV *Inner = Src;
    while (RunningGCD != 1 && isa<SCEVAddRecExpr>(Inner)) {
      AddRec = cast<SCEVAddRecExpr>(Inner);
      const SCEV *Coeff = AddRec->getStepRecurrence(*SE);
      if (CurLoop == AddRec->getLoop())
        ; // SrcCoeff == Coeff
      else {
        // If the coefficient is the product of a constant and other stuff,
        // we can use the constant in the GCD computation.
        Constant = getConstantPart(Coeff);
        if (!Constant)
          return false;
        APInt ConstCoeff = Constant->getAPInt();
        RunningGCD = APIntOps::GreatestCommonDivisor(RunningGCD, ConstCoeff.abs());
      }
      Inner = AddRec->getStart();
    }
    Inner = Dst;
    while (RunningGCD != 1 && isa<SCEVAddRecExpr>(Inner)) {
      AddRec = cast<SCEVAddRecExpr>(Inner);
      const SCEV *Coeff = AddRec->getStepRecurrence(*SE);
      if (CurLoop == AddRec->getLoop())
        DstCoeff = Coeff;
      else {
        // If the coefficient is the product of a constant and other stuff,
        // we can use the constant in the GCD computation.
        Constant = getConstantPart(Coeff);
        if (!Constant)
          return false;
        APInt ConstCoeff = Constant->getAPInt();
        RunningGCD = APIntOps::GreatestCommonDivisor(RunningGCD, ConstCoeff.abs());
      }
      Inner = AddRec->getStart();
    }
    Delta = SE->getMinusSCEV(SrcCoeff, DstCoeff);
    // If the coefficient is the product of a constant and other stuff,
    // we can use the constant in the GCD computation.
    Constant = getConstantPart(Delta);
    if (!Constant)
      // The difference of the two coefficients might not be a product
      // or constant, in which case we give up on this direction.
      continue;
    APInt ConstCoeff = Constant->getAPInt();
    RunningGCD = APIntOps::GreatestCommonDivisor(RunningGCD, ConstCoeff.abs());
    LLVM_DEBUG(dbgs() << "\tRunningGCD = " << RunningGCD << "\n");
    if (RunningGCD != 0) {
      Remainder = ConstDelta.srem(RunningGCD);
      LLVM_DEBUG(dbgs() << "\tRemainder = " << Remainder << "\n");
      if (Remainder != 0) {
        unsigned Level = mapSrcLoop(CurLoop);
        Result.DV[Level - 1].Direction &= unsigned(~Dependence::DVEntry::EQ);
        Improved = true;
      }
    }
  }
  if (Improved)
    ++GCDsuccesses;
  LLVM_DEBUG(dbgs() << "all done\n");
  return false;
}


//===----------------------------------------------------------------------===//
// banerjeeMIVtest -
// Use Banerjee's Inequalities to test an MIV subscript pair.
// (Wolfe, in the race-car book, calls this the Extreme Value Test.)
// Generally follows the discussion in Section 2.5.2 of
//
//    Optimizing Supercompilers for Supercomputers
//    Michael Wolfe
//
// The inequalities given on page 25 are simplified in that loops are
// normalized so that the lower bound is always 0 and the stride is always 1.
// For example, Wolfe gives
//
//     LB^<_k = (A^-_k - B_k)^- (U_k - L_k - N_k) + (A_k - B_k)L_k - B_k N_k
//
// where A_k is the coefficient of the kth index in the source subscript,
// B_k is the coefficient of the kth index in the destination subscript,
// U_k is the upper bound of the kth index, L_k is the lower bound of the Kth
// index, and N_k is the stride of the kth index. Since all loops are normalized
// by the SCEV package, N_k = 1 and L_k = 0, allowing us to simplify the
// equation to
//
//     LB^<_k = (A^-_k - B_k)^- (U_k - 0 - 1) + (A_k - B_k)0 - B_k 1
//            = (A^-_k - B_k)^- (U_k - 1)  - B_k
//
// Similar simplifications are possible for the other equations.
//
// When we can't determine the number of iterations for a loop,
// we use NULL as an indicator for the worst case, infinity.
// When computing the upper bound, NULL denotes +inf;
// for the lower bound, NULL denotes -inf.
//
// Return true if dependence disproved.
bool DependenceInfo::banerjeeMIVtest(const SCEV *Src, const SCEV *Dst,
                                     const SmallBitVector &Loops,
                                     FullDependence &Result) const {
  LLVM_DEBUG(dbgs() << "starting Banerjee\n");
  ++BanerjeeApplications;
  LLVM_DEBUG(dbgs() << "    Src = " << *Src << '\n');
  const SCEV *A0;
  CoefficientInfo *A = collectCoeffInfo(Src, true, A0);
  LLVM_DEBUG(dbgs() << "    Dst = " << *Dst << '\n');
  const SCEV *B0;
  CoefficientInfo *B = collectCoeffInfo(Dst, false, B0);
  BoundInfo *Bound = new BoundInfo[MaxLevels + 1];
  const SCEV *Delta = SE->getMinusSCEV(B0, A0);
  LLVM_DEBUG(dbgs() << "\tDelta = " << *Delta << '\n');

  // Compute bounds for all the * directions.
  LLVM_DEBUG(dbgs() << "\tBounds[*]\n");
  for (unsigned K = 1; K <= MaxLevels; ++K) {
    Bound[K].Iterations = A[K].Iterations ? A[K].Iterations : B[K].Iterations;
    Bound[K].Direction = Dependence::DVEntry::ALL;
    Bound[K].DirSet = Dependence::DVEntry::NONE;
    findBoundsALL(A, B, Bound, K);
#ifndef NDEBUG
    LLVM_DEBUG(dbgs() << "\t    " << K << '\t');
    if (Bound[K].Lower[Dependence::DVEntry::ALL])
      LLVM_DEBUG(dbgs() << *Bound[K].Lower[Dependence::DVEntry::ALL] << '\t');
    else
      LLVM_DEBUG(dbgs() << "-inf\t");
    if (Bound[K].Upper[Dependence::DVEntry::ALL])
      LLVM_DEBUG(dbgs() << *Bound[K].Upper[Dependence::DVEntry::ALL] << '\n');
    else
      LLVM_DEBUG(dbgs() << "+inf\n");
#endif
  }

  // Test the *, *, *, ... case.
  bool Disproved = false;
  if (testBounds(Dependence::DVEntry::ALL, 0, Bound, Delta)) {
    // Explore the direction vector hierarchy.
    unsigned DepthExpanded = 0;
    unsigned NewDeps = exploreDirections(1, A, B, Bound,
                                         Loops, DepthExpanded, Delta);
    if (NewDeps > 0) {
      bool Improved = false;
      for (unsigned K = 1; K <= CommonLevels; ++K) {
        if (Loops[K]) {
          unsigned Old = Result.DV[K - 1].Direction;
          Result.DV[K - 1].Direction = Old & Bound[K].DirSet;
          Improved |= Old != Result.DV[K - 1].Direction;
          if (!Result.DV[K - 1].Direction) {
            Improved = false;
            Disproved = true;
            break;
          }
        }
      }
      if (Improved)
        ++BanerjeeSuccesses;
    }
    else {
      ++BanerjeeIndependence;
      Disproved = true;
    }
  }
  else {
    ++BanerjeeIndependence;
    Disproved = true;
  }
  delete [] Bound;
  delete [] A;
  delete [] B;
  return Disproved;
}


// Hierarchically expands the direction vector
// search space, combining the directions of discovered dependences
// in the DirSet field of Bound. Returns the number of distinct
// dependences discovered. If the dependence is disproved,
// it will return 0.
unsigned DependenceInfo::exploreDirections(unsigned Level, CoefficientInfo *A,
                                           CoefficientInfo *B, BoundInfo *Bound,
                                           const SmallBitVector &Loops,
                                           unsigned &DepthExpanded,
                                           const SCEV *Delta) const {
  if (Level > CommonLevels) {
    // record result
    LLVM_DEBUG(dbgs() << "\t[");
    for (unsigned K = 1; K <= CommonLevels; ++K) {
      if (Loops[K]) {
        Bound[K].DirSet |= Bound[K].Direction;
#ifndef NDEBUG
        switch (Bound[K].Direction) {
        case Dependence::DVEntry::LT:
          LLVM_DEBUG(dbgs() << " <");
          break;
        case Dependence::DVEntry::EQ:
          LLVM_DEBUG(dbgs() << " =");
          break;
        case Dependence::DVEntry::GT:
          LLVM_DEBUG(dbgs() << " >");
          break;
        case Dependence::DVEntry::ALL:
          LLVM_DEBUG(dbgs() << " *");
          break;
        default:
          llvm_unreachable("unexpected Bound[K].Direction");
        }
#endif
      }
    }
    LLVM_DEBUG(dbgs() << " ]\n");
    return 1;
  }
  if (Loops[Level]) {
    if (Level > DepthExpanded) {
      DepthExpanded = Level;
      // compute bounds for <, =, > at current level
      findBoundsLT(A, B, Bound, Level);
      findBoundsGT(A, B, Bound, Level);
      findBoundsEQ(A, B, Bound, Level);
#ifndef NDEBUG
      LLVM_DEBUG(dbgs() << "\tBound for level = " << Level << '\n');
      LLVM_DEBUG(dbgs() << "\t    <\t");
      if (Bound[Level].Lower[Dependence::DVEntry::LT])
        LLVM_DEBUG(dbgs() << *Bound[Level].Lower[Dependence::DVEntry::LT]
                          << '\t');
      else
        LLVM_DEBUG(dbgs() << "-inf\t");
      if (Bound[Level].Upper[Dependence::DVEntry::LT])
        LLVM_DEBUG(dbgs() << *Bound[Level].Upper[Dependence::DVEntry::LT]
                          << '\n');
      else
        LLVM_DEBUG(dbgs() << "+inf\n");
      LLVM_DEBUG(dbgs() << "\t    =\t");
      if (Bound[Level].Lower[Dependence::DVEntry::EQ])
        LLVM_DEBUG(dbgs() << *Bound[Level].Lower[Dependence::DVEntry::EQ]
                          << '\t');
      else
        LLVM_DEBUG(dbgs() << "-inf\t");
      if (Bound[Level].Upper[Dependence::DVEntry::EQ])
        LLVM_DEBUG(dbgs() << *Bound[Level].Upper[Dependence::DVEntry::EQ]
                          << '\n');
      else
        LLVM_DEBUG(dbgs() << "+inf\n");
      LLVM_DEBUG(dbgs() << "\t    >\t");
      if (Bound[Level].Lower[Dependence::DVEntry::GT])
        LLVM_DEBUG(dbgs() << *Bound[Level].Lower[Dependence::DVEntry::GT]
                          << '\t');
      else
        LLVM_DEBUG(dbgs() << "-inf\t");
      if (Bound[Level].Upper[Dependence::DVEntry::GT])
        LLVM_DEBUG(dbgs() << *Bound[Level].Upper[Dependence::DVEntry::GT]
                          << '\n');
      else
        LLVM_DEBUG(dbgs() << "+inf\n");
#endif
    }

    unsigned NewDeps = 0;

    // test bounds for <, *, *, ...
    if (testBounds(Dependence::DVEntry::LT, Level, Bound, Delta))
      NewDeps += exploreDirections(Level + 1, A, B, Bound,
                                   Loops, DepthExpanded, Delta);

    // Test bounds for =, *, *, ...
    if (testBounds(Dependence::DVEntry::EQ, Level, Bound, Delta))
      NewDeps += exploreDirections(Level + 1, A, B, Bound,
                                   Loops, DepthExpanded, Delta);

    // test bounds for >, *, *, ...
    if (testBounds(Dependence::DVEntry::GT, Level, Bound, Delta))
      NewDeps += exploreDirections(Level + 1, A, B, Bound,
                                   Loops, DepthExpanded, Delta);

    Bound[Level].Direction = Dependence::DVEntry::ALL;
    return NewDeps;
  }
  else
    return exploreDirections(Level + 1, A, B, Bound, Loops, DepthExpanded, Delta);
}


// Returns true iff the current bounds are plausible.
bool DependenceInfo::testBounds(unsigned char DirKind, unsigned Level,
                                BoundInfo *Bound, const SCEV *Delta) const {
  Bound[Level].Direction = DirKind;
  if (const SCEV *LowerBound = getLowerBound(Bound))
    if (isKnownPredicate(CmpInst::ICMP_SGT, LowerBound, Delta))
      return false;
  if (const SCEV *UpperBound = getUpperBound(Bound))
    if (isKnownPredicate(CmpInst::ICMP_SGT, Delta, UpperBound))
      return false;
  return true;
}


// Computes the upper and lower bounds for level K
// using the * direction. Records them in Bound.
// Wolfe gives the equations
//
//    LB^*_k = (A^-_k - B^+_k)(U_k - L_k) + (A_k - B_k)L_k
//    UB^*_k = (A^+_k - B^-_k)(U_k - L_k) + (A_k - B_k)L_k
//
// Since we normalize loops, we can simplify these equations to
//
//    LB^*_k = (A^-_k - B^+_k)U_k
//    UB^*_k = (A^+_k - B^-_k)U_k
//
// We must be careful to handle the case where the upper bound is unknown.
// Note that the lower bound is always <= 0
// and the upper bound is always >= 0.
void DependenceInfo::findBoundsALL(CoefficientInfo *A, CoefficientInfo *B,
                                   BoundInfo *Bound, unsigned K) const {
  Bound[K].Lower[Dependence::DVEntry::ALL] = nullptr; // Default value = -infinity.
  Bound[K].Upper[Dependence::DVEntry::ALL] = nullptr; // Default value = +infinity.
  if (Bound[K].Iterations) {
    Bound[K].Lower[Dependence::DVEntry::ALL] =
      SE->getMulExpr(SE->getMinusSCEV(A[K].NegPart, B[K].PosPart),
                     Bound[K].Iterations);
    Bound[K].Upper[Dependence::DVEntry::ALL] =
      SE->getMulExpr(SE->getMinusSCEV(A[K].PosPart, B[K].NegPart),
                     Bound[K].Iterations);
  }
  else {
    // If the difference is 0, we won't need to know the number of iterations.
    if (isKnownPredicate(CmpInst::ICMP_EQ, A[K].NegPart, B[K].PosPart))
      Bound[K].Lower[Dependence::DVEntry::ALL] =
          SE->getZero(A[K].Coeff->getType());
    if (isKnownPredicate(CmpInst::ICMP_EQ, A[K].PosPart, B[K].NegPart))
      Bound[K].Upper[Dependence::DVEntry::ALL] =
          SE->getZero(A[K].Coeff->getType());
  }
}


// Computes the upper and lower bounds for level K
// using the = direction. Records them in Bound.
// Wolfe gives the equations
//
//    LB^=_k = (A_k - B_k)^- (U_k - L_k) + (A_k - B_k)L_k
//    UB^=_k = (A_k - B_k)^+ (U_k - L_k) + (A_k - B_k)L_k
//
// Since we normalize loops, we can simplify these equations to
//
//    LB^=_k = (A_k - B_k)^- U_k
//    UB^=_k = (A_k - B_k)^+ U_k
//
// We must be careful to handle the case where the upper bound is unknown.
// Note that the lower bound is always <= 0
// and the upper bound is always >= 0.
void DependenceInfo::findBoundsEQ(CoefficientInfo *A, CoefficientInfo *B,
                                  BoundInfo *Bound, unsigned K) const {
  Bound[K].Lower[Dependence::DVEntry::EQ] = nullptr; // Default value = -infinity.
  Bound[K].Upper[Dependence::DVEntry::EQ] = nullptr; // Default value = +infinity.
  if (Bound[K].Iterations) {
    const SCEV *Delta = SE->getMinusSCEV(A[K].Coeff, B[K].Coeff);
    const SCEV *NegativePart = getNegativePart(Delta);
    Bound[K].Lower[Dependence::DVEntry::EQ] =
      SE->getMulExpr(NegativePart, Bound[K].Iterations);
    const SCEV *PositivePart = getPositivePart(Delta);
    Bound[K].Upper[Dependence::DVEntry::EQ] =
      SE->getMulExpr(PositivePart, Bound[K].Iterations);
  }
  else {
    // If the positive/negative part of the difference is 0,
    // we won't need to know the number of iterations.
    const SCEV *Delta = SE->getMinusSCEV(A[K].Coeff, B[K].Coeff);
    const SCEV *NegativePart = getNegativePart(Delta);
    if (NegativePart->isZero())
      Bound[K].Lower[Dependence::DVEntry::EQ] = NegativePart; // Zero
    const SCEV *PositivePart = getPositivePart(Delta);
    if (PositivePart->isZero())
      Bound[K].Upper[Dependence::DVEntry::EQ] = PositivePart; // Zero
  }
}


// Computes the upper and lower bounds for level K
// using the < direction. Records them in Bound.
// Wolfe gives the equations
//
//    LB^<_k = (A^-_k - B_k)^- (U_k - L_k - N_k) + (A_k - B_k)L_k - B_k N_k
//    UB^<_k = (A^+_k - B_k)^+ (U_k - L_k - N_k) + (A_k - B_k)L_k - B_k N_k
//
// Since we normalize loops, we can simplify these equations to
//
//    LB^<_k = (A^-_k - B_k)^- (U_k - 1) - B_k
//    UB^<_k = (A^+_k - B_k)^+ (U_k - 1) - B_k
//
// We must be careful to handle the case where the upper bound is unknown.
void DependenceInfo::findBoundsLT(CoefficientInfo *A, CoefficientInfo *B,
                                  BoundInfo *Bound, unsigned K) const {
  Bound[K].Lower[Dependence::DVEntry::LT] = nullptr; // Default value = -infinity.
  Bound[K].Upper[Dependence::DVEntry::LT] = nullptr; // Default value = +infinity.
  if (Bound[K].Iterations) {
    const SCEV *Iter_1 = SE->getMinusSCEV(
        Bound[K].Iterations, SE->getOne(Bound[K].Iterations->getType()));
    const SCEV *NegPart =
      getNegativePart(SE->getMinusSCEV(A[K].NegPart, B[K].Coeff));
    Bound[K].Lower[Dependence::DVEntry::LT] =
      SE->getMinusSCEV(SE->getMulExpr(NegPart, Iter_1), B[K].Coeff);
    const SCEV *PosPart =
      getPositivePart(SE->getMinusSCEV(A[K].PosPart, B[K].Coeff));
    Bound[K].Upper[Dependence::DVEntry::LT] =
      SE->getMinusSCEV(SE->getMulExpr(PosPart, Iter_1), B[K].Coeff);
  }
  else {
    // If the positive/negative part of the difference is 0,
    // we won't need to know the number of iterations.
    const SCEV *NegPart =
      getNegativePart(SE->getMinusSCEV(A[K].NegPart, B[K].Coeff));
    if (NegPart->isZero())
      Bound[K].Lower[Dependence::DVEntry::LT] = SE->getNegativeSCEV(B[K].Coeff);
    const SCEV *PosPart =
      getPositivePart(SE->getMinusSCEV(A[K].PosPart, B[K].Coeff));
    if (PosPart->isZero())
      Bound[K].Upper[Dependence::DVEntry::LT] = SE->getNegativeSCEV(B[K].Coeff);
  }
}


// Computes the upper and lower bounds for level K
// using the > direction. Records them in Bound.
// Wolfe gives the equations
//
//    LB^>_k = (A_k - B^+_k)^- (U_k - L_k - N_k) + (A_k - B_k)L_k + A_k N_k
//    UB^>_k = (A_k - B^-_k)^+ (U_k - L_k - N_k) + (A_k - B_k)L_k + A_k N_k
//
// Since we normalize loops, we can simplify these equations to
//
//    LB^>_k = (A_k - B^+_k)^- (U_k - 1) + A_k
//    UB^>_k = (A_k - B^-_k)^+ (U_k - 1) + A_k
//
// We must be careful to handle the case where the upper bound is unknown.
void DependenceInfo::findBoundsGT(CoefficientInfo *A, CoefficientInfo *B,
                                  BoundInfo *Bound, unsigned K) const {
  Bound[K].Lower[Dependence::DVEntry::GT] = nullptr; // Default value = -infinity.
  Bound[K].Upper[Dependence::DVEntry::GT] = nullptr; // Default value = +infinity.
  if (Bound[K].Iterations) {
    const SCEV *Iter_1 = SE->getMinusSCEV(
        Bound[K].Iterations, SE->getOne(Bound[K].Iterations->getType()));
    const SCEV *NegPart =
      getNegativePart(SE->getMinusSCEV(A[K].Coeff, B[K].PosPart));
    Bound[K].Lower[Dependence::DVEntry::GT] =
      SE->getAddExpr(SE->getMulExpr(NegPart, Iter_1), A[K].Coeff);
    const SCEV *PosPart =
      getPositivePart(SE->getMinusSCEV(A[K].Coeff, B[K].NegPart));
    Bound[K].Upper[Dependence::DVEntry::GT] =
      SE->getAddExpr(SE->getMulExpr(PosPart, Iter_1), A[K].Coeff);
  }
  else {
    // If the positive/negative part of the difference is 0,
    // we won't need to know the number of iterations.
    const SCEV *NegPart = getNegativePart(SE->getMinusSCEV(A[K].Coeff, B[K].PosPart));
    if (NegPart->isZero())
      Bound[K].Lower[Dependence::DVEntry::GT] = A[K].Coeff;
    const SCEV *PosPart = getPositivePart(SE->getMinusSCEV(A[K].Coeff, B[K].NegPart));
    if (PosPart->isZero())
      Bound[K].Upper[Dependence::DVEntry::GT] = A[K].Coeff;
  }
}


// X^+ = max(X, 0)
const SCEV *DependenceInfo::getPositivePart(const SCEV *X) const {
  return SE->getSMaxExpr(X, SE->getZero(X->getType()));
}


// X^- = min(X, 0)
const SCEV *DependenceInfo::getNegativePart(const SCEV *X) const {
  return SE->getSMinExpr(X, SE->getZero(X->getType()));
}


// Walks through the subscript,
// collecting each coefficient, the associated loop bounds,
// and recording its positive and negative parts for later use.
DependenceInfo::CoefficientInfo *
DependenceInfo::collectCoeffInfo(const SCEV *Subscript, bool SrcFlag,
                                 const SCEV *&Constant) const {
  const SCEV *Zero = SE->getZero(Subscript->getType());
  CoefficientInfo *CI = new CoefficientInfo[MaxLevels + 1];
  for (unsigned K = 1; K <= MaxLevels; ++K) {
    CI[K].Coeff = Zero;
    CI[K].PosPart = Zero;
    CI[K].NegPart = Zero;
    CI[K].Iterations = nullptr;
  }
  while (const SCEVAddRecExpr *AddRec = dyn_cast<SCEVAddRecExpr>(Subscript)) {
    const Loop *L = AddRec->getLoop();
    unsigned K = SrcFlag ? mapSrcLoop(L) : mapDstLoop(L);
    CI[K].Coeff = AddRec->getStepRecurrence(*SE);
    CI[K].PosPart = getPositivePart(CI[K].Coeff);
    CI[K].NegPart = getNegativePart(CI[K].Coeff);
    CI[K].Iterations = collectUpperBound(L, Subscript->getType());
    Subscript = AddRec->getStart();
  }
  Constant = Subscript;
#ifndef NDEBUG
  LLVM_DEBUG(dbgs() << "\tCoefficient Info\n");
  for (unsigned K = 1; K <= MaxLevels; ++K) {
    LLVM_DEBUG(dbgs() << "\t    " << K << "\t" << *CI[K].Coeff);
    LLVM_DEBUG(dbgs() << "\tPos Part = ");
    LLVM_DEBUG(dbgs() << *CI[K].PosPart);
    LLVM_DEBUG(dbgs() << "\tNeg Part = ");
    LLVM_DEBUG(dbgs() << *CI[K].NegPart);
    LLVM_DEBUG(dbgs() << "\tUpper Bound = ");
    if (CI[K].Iterations)
      LLVM_DEBUG(dbgs() << *CI[K].Iterations);
    else
      LLVM_DEBUG(dbgs() << "+inf");
    LLVM_DEBUG(dbgs() << '\n');
  }
  LLVM_DEBUG(dbgs() << "\t    Constant = " << *Subscript << '\n');
#endif
  return CI;
}


// Looks through all the bounds info and
// computes the lower bound given the current direction settings
// at each level. If the lower bound for any level is -inf,
// the result is -inf.
const SCEV *DependenceInfo::getLowerBound(BoundInfo *Bound) const {
  const SCEV *Sum = Bound[1].Lower[Bound[1].Direction];
  for (unsigned K = 2; Sum && K <= MaxLevels; ++K) {
    if (Bound[K].Lower[Bound[K].Direction])
      Sum = SE->getAddExpr(Sum, Bound[K].Lower[Bound[K].Direction]);
    else
      Sum = nullptr;
  }
  return Sum;
}


// Looks through all the bounds info and
// computes the upper bound given the current direction settings
// at each level. If the upper bound at any level is +inf,
// the result is +inf.
const SCEV *DependenceInfo::getUpperBound(BoundInfo *Bound) const {
  const SCEV *Sum = Bound[1].Upper[Bound[1].Direction];
  for (unsigned K = 2; Sum && K <= MaxLevels; ++K) {
    if (Bound[K].Upper[Bound[K].Direction])
      Sum = SE->getAddExpr(Sum, Bound[K].Upper[Bound[K].Direction]);
    else
      Sum = nullptr;
  }
  return Sum;
}


//===----------------------------------------------------------------------===//
// Constraint manipulation for Delta test.

// Given a linear SCEV,
// return the coefficient (the step)
// corresponding to the specified loop.
// If there isn't one, return 0.
// For example, given a*i + b*j + c*k, finding the coefficient
// corresponding to the j loop would yield b.
const SCEV *DependenceInfo::findCoefficient(const SCEV *Expr,
                                            const Loop *TargetLoop) const {
  const SCEVAddRecExpr *AddRec = dyn_cast<SCEVAddRecExpr>(Expr);
  if (!AddRec)
    return SE->getZero(Expr->getType());
  if (AddRec->getLoop() == TargetLoop)
    return AddRec->getStepRecurrence(*SE);
  return findCoefficient(AddRec->getStart(), TargetLoop);
}


// Given a linear SCEV,
// return the SCEV given by zeroing out the coefficient
// corresponding to the specified loop.
// For example, given a*i + b*j + c*k, zeroing the coefficient
// corresponding to the j loop would yield a*i + c*k.
const SCEV *DependenceInfo::zeroCoefficient(const SCEV *Expr,
                                            const Loop *TargetLoop) const {
  const SCEVAddRecExpr *AddRec = dyn_cast<SCEVAddRecExpr>(Expr);
  if (!AddRec)
    return Expr; // ignore
  if (AddRec->getLoop() == TargetLoop)
    return AddRec->getStart();
  return SE->getAddRecExpr(zeroCoefficient(AddRec->getStart(), TargetLoop),
                           AddRec->getStepRecurrence(*SE),
                           AddRec->getLoop(),
                           AddRec->getNoWrapFlags());
}


// Given a linear SCEV Expr,
// return the SCEV given by adding some Value to the
// coefficient corresponding to the specified TargetLoop.
// For example, given a*i + b*j + c*k, adding 1 to the coefficient
// corresponding to the j loop would yield a*i + (b+1)*j + c*k.
const SCEV *DependenceInfo::addToCoefficient(const SCEV *Expr,
                                             const Loop *TargetLoop,
                                             const SCEV *Value) const {
  const SCEVAddRecExpr *AddRec = dyn_cast<SCEVAddRecExpr>(Expr);
  if (!AddRec) // create a new addRec
    return SE->getAddRecExpr(Expr,
                             Value,
                             TargetLoop,
                             SCEV::FlagAnyWrap); // Worst case, with no info.
  if (AddRec->getLoop() == TargetLoop) {
    const SCEV *Sum = SE->getAddExpr(AddRec->getStepRecurrence(*SE), Value);
    if (Sum->isZero())
      return AddRec->getStart();
    return SE->getAddRecExpr(AddRec->getStart(),
                             Sum,
                             AddRec->getLoop(),
                             AddRec->getNoWrapFlags());
  }
  if (SE->isLoopInvariant(AddRec, TargetLoop))
    return SE->getAddRecExpr(AddRec, Value, TargetLoop, SCEV::FlagAnyWrap);
  return SE->getAddRecExpr(
      addToCoefficient(AddRec->getStart(), TargetLoop, Value),
      AddRec->getStepRecurrence(*SE), AddRec->getLoop(),
      AddRec->getNoWrapFlags());
}


// Review the constraints, looking for opportunities
// to simplify a subscript pair (Src and Dst).
// Return true if some simplification occurs.
// If the simplification isn't exact (that is, if it is conservative
// in terms of dependence), set consistent to false.
// Corresponds to Figure 5 from the paper
//
//            Practical Dependence Testing
//            Goff, Kennedy, Tseng
//            PLDI 1991
bool DependenceInfo::propagate(const SCEV *&Src, const SCEV *&Dst,
                               SmallBitVector &Loops,
                               SmallVectorImpl<Constraint> &Constraints,
                               bool &Consistent) {
  bool Result = false;
  for (unsigned LI : Loops.set_bits()) {
    LLVM_DEBUG(dbgs() << "\t    Constraint[" << LI << "] is");
    LLVM_DEBUG(Constraints[LI].dump(dbgs()));
    if (Constraints[LI].isDistance())
      Result |= propagateDistance(Src, Dst, Constraints[LI], Consistent);
    else if (Constraints[LI].isLine())
      Result |= propagateLine(Src, Dst, Constraints[LI], Consistent);
    else if (Constraints[LI].isPoint())
      Result |= propagatePoint(Src, Dst, Constraints[LI]);
  }
  return Result;
}


// Attempt to propagate a distance
// constraint into a subscript pair (Src and Dst).
// Return true if some simplification occurs.
// If the simplification isn't exact (that is, if it is conservative
// in terms of dependence), set consistent to false.
bool DependenceInfo::propagateDistance(const SCEV *&Src, const SCEV *&Dst,
                                       Constraint &CurConstraint,
                                       bool &Consistent) {
  const Loop *CurLoop = CurConstraint.getAssociatedLoop();
  LLVM_DEBUG(dbgs() << "\t\tSrc is " << *Src << "\n");
  const SCEV *A_K = findCoefficient(Src, CurLoop);
  if (A_K->isZero())
    return false;
  const SCEV *DA_K = SE->getMulExpr(A_K, CurConstraint.getD());
  Src = SE->getMinusSCEV(Src, DA_K);
  Src = zeroCoefficient(Src, CurLoop);
  LLVM_DEBUG(dbgs() << "\t\tnew Src is " << *Src << "\n");
  LLVM_DEBUG(dbgs() << "\t\tDst is " << *Dst << "\n");
  Dst = addToCoefficient(Dst, CurLoop, SE->getNegativeSCEV(A_K));
  LLVM_DEBUG(dbgs() << "\t\tnew Dst is " << *Dst << "\n");
  if (!findCoefficient(Dst, CurLoop)->isZero())
    Consistent = false;
  return true;
}


// Attempt to propagate a line
// constraint into a subscript pair (Src and Dst).
// Return true if some simplification occurs.
// If the simplification isn't exact (that is, if it is conservative
// in terms of dependence), set consistent to false.
bool DependenceInfo::propagateLine(const SCEV *&Src, const SCEV *&Dst,
                                   Constraint &CurConstraint,
                                   bool &Consistent) {
  const Loop *CurLoop = CurConstraint.getAssociatedLoop();
  const SCEV *A = CurConstraint.getA();
  const SCEV *B = CurConstraint.getB();
  const SCEV *C = CurConstraint.getC();
  LLVM_DEBUG(dbgs() << "\t\tA = " << *A << ", B = " << *B << ", C = " << *C
                    << "\n");
  LLVM_DEBUG(dbgs() << "\t\tSrc = " << *Src << "\n");
  LLVM_DEBUG(dbgs() << "\t\tDst = " << *Dst << "\n");
  if (A->isZero()) {
    const SCEVConstant *Bconst = dyn_cast<SCEVConstant>(B);
    const SCEVConstant *Cconst = dyn_cast<SCEVConstant>(C);
    if (!Bconst || !Cconst) return false;
    APInt Beta = Bconst->getAPInt();
    APInt Charlie = Cconst->getAPInt();
    APInt CdivB = Charlie.sdiv(Beta);
    assert(Charlie.srem(Beta) == 0 && "C should be evenly divisible by B");
    const SCEV *AP_K = findCoefficient(Dst, CurLoop);
    //    Src = SE->getAddExpr(Src, SE->getMulExpr(AP_K, SE->getConstant(CdivB)));
    Src = SE->getMinusSCEV(Src, SE->getMulExpr(AP_K, SE->getConstant(CdivB)));
    Dst = zeroCoefficient(Dst, CurLoop);
    if (!findCoefficient(Src, CurLoop)->isZero())
      Consistent = false;
  }
  else if (B->isZero()) {
    const SCEVConstant *Aconst = dyn_cast<SCEVConstant>(A);
    const SCEVConstant *Cconst = dyn_cast<SCEVConstant>(C);
    if (!Aconst || !Cconst) return false;
    APInt Alpha = Aconst->getAPInt();
    APInt Charlie = Cconst->getAPInt();
    APInt CdivA = Charlie.sdiv(Alpha);
    assert(Charlie.srem(Alpha) == 0 && "C should be evenly divisible by A");
    const SCEV *A_K = findCoefficient(Src, CurLoop);
    Src = SE->getAddExpr(Src, SE->getMulExpr(A_K, SE->getConstant(CdivA)));
    Src = zeroCoefficient(Src, CurLoop);
    if (!findCoefficient(Dst, CurLoop)->isZero())
      Consistent = false;
  }
  else if (isKnownPredicate(CmpInst::ICMP_EQ, A, B)) {
    const SCEVConstant *Aconst = dyn_cast<SCEVConstant>(A);
    const SCEVConstant *Cconst = dyn_cast<SCEVConstant>(C);
    if (!Aconst || !Cconst) return false;
    APInt Alpha = Aconst->getAPInt();
    APInt Charlie = Cconst->getAPInt();
    APInt CdivA = Charlie.sdiv(Alpha);
    assert(Charlie.srem(Alpha) == 0 && "C should be evenly divisible by A");
    const SCEV *A_K = findCoefficient(Src, CurLoop);
    Src = SE->getAddExpr(Src, SE->getMulExpr(A_K, SE->getConstant(CdivA)));
    Src = zeroCoefficient(Src, CurLoop);
    Dst = addToCoefficient(Dst, CurLoop, A_K);
    if (!findCoefficient(Dst, CurLoop)->isZero())
      Consistent = false;
  }
  else {
    // paper is incorrect here, or perhaps just misleading
    const SCEV *A_K = findCoefficient(Src, CurLoop);
    Src = SE->getMulExpr(Src, A);
    Dst = SE->getMulExpr(Dst, A);
    Src = SE->getAddExpr(Src, SE->getMulExpr(A_K, C));
    Src = zeroCoefficient(Src, CurLoop);
    Dst = addToCoefficient(Dst, CurLoop, SE->getMulExpr(A_K, B));
    if (!findCoefficient(Dst, CurLoop)->isZero())
      Consistent = false;
  }
  LLVM_DEBUG(dbgs() << "\t\tnew Src = " << *Src << "\n");
  LLVM_DEBUG(dbgs() << "\t\tnew Dst = " << *Dst << "\n");
  return true;
}


// Attempt to propagate a point
// constraint into a subscript pair (Src and Dst).
// Return true if some simplification occurs.
bool DependenceInfo::propagatePoint(const SCEV *&Src, const SCEV *&Dst,
                                    Constraint &CurConstraint) {
  const Loop *CurLoop = CurConstraint.getAssociatedLoop();
  const SCEV *A_K = findCoefficient(Src, CurLoop);
  const SCEV *AP_K = findCoefficient(Dst, CurLoop);
  const SCEV *XA_K = SE->getMulExpr(A_K, CurConstraint.getX());
  const SCEV *YAP_K = SE->getMulExpr(AP_K, CurConstraint.getY());
  LLVM_DEBUG(dbgs() << "\t\tSrc is " << *Src << "\n");
  Src = SE->getAddExpr(Src, SE->getMinusSCEV(XA_K, YAP_K));
  Src = zeroCoefficient(Src, CurLoop);
  LLVM_DEBUG(dbgs() << "\t\tnew Src is " << *Src << "\n");
  LLVM_DEBUG(dbgs() << "\t\tDst is " << *Dst << "\n");
  Dst = zeroCoefficient(Dst, CurLoop);
  LLVM_DEBUG(dbgs() << "\t\tnew Dst is " << *Dst << "\n");
  return true;
}


// Update direction vector entry based on the current constraint.
void DependenceInfo::updateDirection(Dependence::DVEntry &Level,
                                     const Constraint &CurConstraint) const {
  LLVM_DEBUG(dbgs() << "\tUpdate direction, constraint =");
  LLVM_DEBUG(CurConstraint.dump(dbgs()));
  if (CurConstraint.isAny())
    ; // use defaults
  else if (CurConstraint.isDistance()) {
    // this one is consistent, the others aren't
    Level.Scalar = false;
    Level.Distance = CurConstraint.getD();
    unsigned NewDirection = Dependence::DVEntry::NONE;
    if (!SE->isKnownNonZero(Level.Distance)) // if may be zero
      NewDirection = Dependence::DVEntry::EQ;
    if (!SE->isKnownNonPositive(Level.Distance)) // if may be positive
      NewDirection |= Dependence::DVEntry::LT;
    if (!SE->isKnownNonNegative(Level.Distance)) // if may be negative
      NewDirection |= Dependence::DVEntry::GT;
    Level.Direction &= NewDirection;
  }
  else if (CurConstraint.isLine()) {
    Level.Scalar = false;
    Level.Distance = nullptr;
    // direction should be accurate
  }
  else if (CurConstraint.isPoint()) {
    Level.Scalar = false;
    Level.Distance = nullptr;
    unsigned NewDirection = Dependence::DVEntry::NONE;
    if (!isKnownPredicate(CmpInst::ICMP_NE,
                          CurConstraint.getY(),
                          CurConstraint.getX()))
      // if X may be = Y
      NewDirection |= Dependence::DVEntry::EQ;
    if (!isKnownPredicate(CmpInst::ICMP_SLE,
                          CurConstraint.getY(),
                          CurConstraint.getX()))
      // if Y may be > X
      NewDirection |= Dependence::DVEntry::LT;
    if (!isKnownPredicate(CmpInst::ICMP_SGE,
                          CurConstraint.getY(),
                          CurConstraint.getX()))
      // if Y may be < X
      NewDirection |= Dependence::DVEntry::GT;
    Level.Direction &= NewDirection;
  }
  else
    llvm_unreachable("constraint has unexpected kind");
}

/// Check if we can delinearize the subscripts. If the SCEVs representing the
/// source and destination array references are recurrences on a nested loop,
/// this function flattens the nested recurrences into separate recurrences
/// for each loop level.
bool DependenceInfo::tryDelinearize(Instruction *Src, Instruction *Dst,
                                    SmallVectorImpl<Subscript> &Pair) {
  assert(isLoadOrStore(Src) && "instruction is not load or store");
  assert(isLoadOrStore(Dst) && "instruction is not load or store");
  Value *SrcPtr = getLoadStorePointerOperand(Src);
  Value *DstPtr = getLoadStorePointerOperand(Dst);

  Loop *SrcLoop = LI->getLoopFor(Src->getParent());
  Loop *DstLoop = LI->getLoopFor(Dst->getParent());

  // Below code mimics the code in Delinearization.cpp
  const SCEV *SrcAccessFn =
    SE->getSCEVAtScope(SrcPtr, SrcLoop);
  const SCEV *DstAccessFn =
    SE->getSCEVAtScope(DstPtr, DstLoop);

  const SCEVUnknown *SrcBase =
      dyn_cast<SCEVUnknown>(SE->getPointerBase(SrcAccessFn));
  const SCEVUnknown *DstBase =
      dyn_cast<SCEVUnknown>(SE->getPointerBase(DstAccessFn));

  if (!SrcBase || !DstBase || SrcBase != DstBase)
    return false;

  const SCEV *ElementSize = SE->getElementSize(Src);
  if (ElementSize != SE->getElementSize(Dst))
    return false;

  const SCEV *SrcSCEV = SE->getMinusSCEV(SrcAccessFn, SrcBase);
  const SCEV *DstSCEV = SE->getMinusSCEV(DstAccessFn, DstBase);

  const SCEVAddRecExpr *SrcAR = dyn_cast<SCEVAddRecExpr>(SrcSCEV);
  const SCEVAddRecExpr *DstAR = dyn_cast<SCEVAddRecExpr>(DstSCEV);
  if (!SrcAR || !DstAR || !SrcAR->isAffine() || !DstAR->isAffine())
    return false;

  // First step: collect parametric terms in both array references.
  SmallVector<const SCEV *, 4> Terms;
  SE->collectParametricTerms(SrcAR, Terms);
  SE->collectParametricTerms(DstAR, Terms);

  // Second step: find subscript sizes.
  SmallVector<const SCEV *, 4> Sizes;
  SE->findArrayDimensions(Terms, Sizes, ElementSize);

  // Third step: compute the access functions for each subscript.
  SmallVector<const SCEV *, 4> SrcSubscripts, DstSubscripts;
  SE->computeAccessFunctions(SrcAR, SrcSubscripts, Sizes);
  SE->computeAccessFunctions(DstAR, DstSubscripts, Sizes);

  // Fail when there is only a subscript: that's a linearized access function.
  if (SrcSubscripts.size() < 2 || DstSubscripts.size() < 2 ||
      SrcSubscripts.size() != DstSubscripts.size())
    return false;

  int size = SrcSubscripts.size();

  // Statically check that the array bounds are in-range. The first subscript we
  // don't have a size for and it cannot overflow into another subscript, so is
  // always safe. The others need to be 0 <= subscript[i] < bound, for both src
  // and dst.
  // FIXME: It may be better to record these sizes and add them as constraints
  // to the dependency checks.
  for (int i = 1; i < size; ++i) {
    if (!isKnownNonNegative(SrcSubscripts[i], SrcPtr))
      return false;

    if (!isKnownLessThan(SrcSubscripts[i], Sizes[i - 1]))
      return false;

    if (!isKnownNonNegative(DstSubscripts[i], DstPtr))
      return false;

    if (!isKnownLessThan(DstSubscripts[i], Sizes[i - 1]))
      return false;
  }

  LLVM_DEBUG({
    dbgs() << "\nSrcSubscripts: ";
    for (int i = 0; i < size; i++)
      dbgs() << *SrcSubscripts[i];
    dbgs() << "\nDstSubscripts: ";
    for (int i = 0; i < size; i++)
      dbgs() << *DstSubscripts[i];
  });

  // The delinearization transforms a single-subscript MIV dependence test into
  // a multi-subscript SIV dependence test that is easier to compute. So we
  // resize Pair to contain as many pairs of subscripts as the delinearization
  // has found, and then initialize the pairs following the delinearization.
  Pair.resize(size);
  for (int i = 0; i < size; ++i) {
    Pair[i].Src = SrcSubscripts[i];
    Pair[i].Dst = DstSubscripts[i];
    unifySubscriptType(&Pair[i]);
  }

  return true;
}

//===----------------------------------------------------------------------===//

#ifndef NDEBUG
// For debugging purposes, dump a small bit vector to dbgs().
static void dumpSmallBitVector(SmallBitVector &BV) {
  dbgs() << "{";
  for (unsigned VI : BV.set_bits()) {
    dbgs() << VI;
    if (BV.find_next(VI) >= 0)
      dbgs() << ' ';
  }
  dbgs() << "}\n";
}
#endif

// depends -
// Returns NULL if there is no dependence.
// Otherwise, return a Dependence with as many details as possible.
// Corresponds to Section 3.1 in the paper
//
//            Practical Dependence Testing
//            Goff, Kennedy, Tseng
//            PLDI 1991
//
// Care is required to keep the routine below, getSplitIteration(),
// up to date with respect to this routine.
std::unique_ptr<Dependence>
DependenceInfo::depends(Instruction *Src, Instruction *Dst,
                        bool PossiblyLoopIndependent) {
  if (Src == Dst)
    PossiblyLoopIndependent = false;

  if ((!Src->mayReadFromMemory() && !Src->mayWriteToMemory()) ||
      (!Dst->mayReadFromMemory() && !Dst->mayWriteToMemory()))
    // if both instructions don't reference memory, there's no dependence
    return nullptr;

  if (!isLoadOrStore(Src) || !isLoadOrStore(Dst)) {
    // can only analyze simple loads and stores, i.e., no calls, invokes, etc.
    LLVM_DEBUG(dbgs() << "can only handle simple loads and stores\n");
    return make_unique<Dependence>(Src, Dst);
  }

  assert(isLoadOrStore(Src) && "instruction is not load or store");
  assert(isLoadOrStore(Dst) && "instruction is not load or store");
  Value *SrcPtr = getLoadStorePointerOperand(Src);
  Value *DstPtr = getLoadStorePointerOperand(Dst);

  switch (underlyingObjectsAlias(AA, F->getParent()->getDataLayout(),
                                 MemoryLocation::get(Dst),
                                 MemoryLocation::get(Src))) {
  case MayAlias:
  case PartialAlias:
    // cannot analyse objects if we don't understand their aliasing.
    LLVM_DEBUG(dbgs() << "can't analyze may or partial alias\n");
    return make_unique<Dependence>(Src, Dst);
  case NoAlias:
    // If the objects noalias, they are distinct, accesses are independent.
    LLVM_DEBUG(dbgs() << "no alias\n");
    return nullptr;
  case MustAlias:
    break; // The underlying objects alias; test accesses for dependence.
  }

  // establish loop nesting levels
  establishNestingLevels(Src, Dst);
  LLVM_DEBUG(dbgs() << "    common nesting levels = " << CommonLevels << "\n");
  LLVM_DEBUG(dbgs() << "    maximum nesting levels = " << MaxLevels << "\n");

  FullDependence Result(Src, Dst, PossiblyLoopIndependent, CommonLevels);
  ++TotalArrayPairs;

  unsigned Pairs = 1;
  SmallVector<Subscript, 2> Pair(Pairs);
  const SCEV *SrcSCEV = SE->getSCEV(SrcPtr);
  const SCEV *DstSCEV = SE->getSCEV(DstPtr);
  LLVM_DEBUG(dbgs() << "    SrcSCEV = " << *SrcSCEV << "\n");
  LLVM_DEBUG(dbgs() << "    DstSCEV = " << *DstSCEV << "\n");
  Pair[0].Src = SrcSCEV;
  Pair[0].Dst = DstSCEV;

  if (Delinearize) {
    if (tryDelinearize(Src, Dst, Pair)) {
      LLVM_DEBUG(dbgs() << "    delinearized\n");
      Pairs = Pair.size();
    }
  }

  for (unsigned P = 0; P < Pairs; ++P) {
    Pair[P].Loops.resize(MaxLevels + 1);
    Pair[P].GroupLoops.resize(MaxLevels + 1);
    Pair[P].Group.resize(Pairs);
    removeMatchingExtensions(&Pair[P]);
    Pair[P].Classification =
      classifyPair(Pair[P].Src, LI->getLoopFor(Src->getParent()),
                   Pair[P].Dst, LI->getLoopFor(Dst->getParent()),
                   Pair[P].Loops);
    Pair[P].GroupLoops = Pair[P].Loops;
    Pair[P].Group.set(P);
    LLVM_DEBUG(dbgs() << "    subscript " << P << "\n");
    LLVM_DEBUG(dbgs() << "\tsrc = " << *Pair[P].Src << "\n");
    LLVM_DEBUG(dbgs() << "\tdst = " << *Pair[P].Dst << "\n");
    LLVM_DEBUG(dbgs() << "\tclass = " << Pair[P].Classification << "\n");
    LLVM_DEBUG(dbgs() << "\tloops = ");
    LLVM_DEBUG(dumpSmallBitVector(Pair[P].Loops));
  }

  SmallBitVector Separable(Pairs);
  SmallBitVector Coupled(Pairs);

  // Partition subscripts into separable and minimally-coupled groups
  // Algorithm in paper is algorithmically better;
  // this may be faster in practice. Check someday.
  //
  // Here's an example of how it works. Consider this code:
  //
  //   for (i = ...) {
  //     for (j = ...) {
  //       for (k = ...) {
  //         for (l = ...) {
  //           for (m = ...) {
  //             A[i][j][k][m] = ...;
  //             ... = A[0][j][l][i + j];
  //           }
  //         }
  //       }
  //     }
  //   }
  //
  // There are 4 subscripts here:
  //    0 [i] and [0]
  //    1 [j] and [j]
  //    2 [k] and [l]
  //    3 [m] and [i + j]
  //
  // We've already classified each subscript pair as ZIV, SIV, etc.,
  // and collected all the loops mentioned by pair P in Pair[P].Loops.
  // In addition, we've initialized Pair[P].GroupLoops to Pair[P].Loops
  // and set Pair[P].Group = {P}.
  //
  //      Src Dst    Classification Loops  GroupLoops Group
  //    0 [i] [0]         SIV       {1}      {1}        {0}
  //    1 [j] [j]         SIV       {2}      {2}        {1}
  //    2 [k] [l]         RDIV      {3,4}    {3,4}      {2}
  //    3 [m] [i + j]     MIV       {1,2,5}  {1,2,5}    {3}
  //
  // For each subscript SI 0 .. 3, we consider each remaining subscript, SJ.
  // So, 0 is compared against 1, 2, and 3; 1 is compared against 2 and 3, etc.
  //
  // We begin by comparing 0 and 1. The intersection of the GroupLoops is empty.
  // Next, 0 and 2. Again, the intersection of their GroupLoops is empty.
  // Next 0 and 3. The intersection of their GroupLoop = {1}, not empty,
  // so Pair[3].Group = {0,3} and Done = false (that is, 0 will not be added
  // to either Separable or Coupled).
  //
  // Next, we consider 1 and 2. The intersection of the GroupLoops is empty.
  // Next, 1 and 3. The intersectionof their GroupLoops = {2}, not empty,
  // so Pair[3].Group = {0, 1, 3} and Done = false.
  //
  // Next, we compare 2 against 3. The intersection of the GroupLoops is empty.
  // Since Done remains true, we add 2 to the set of Separable pairs.
  //
  // Finally, we consider 3. There's nothing to compare it with,
  // so Done remains true and we add it to the Coupled set.
  // Pair[3].Group = {0, 1, 3} and GroupLoops = {1, 2, 5}.
  //
  // In the end, we've got 1 separable subscript and 1 coupled group.
  for (unsigned SI = 0; SI < Pairs; ++SI) {
    if (Pair[SI].Classification == Subscript::NonLinear) {
      // ignore these, but collect loops for later
      ++NonlinearSubscriptPairs;
      collectCommonLoops(Pair[SI].Src,
                         LI->getLoopFor(Src->getParent()),
                         Pair[SI].Loops);
      collectCommonLoops(Pair[SI].Dst,
                         LI->getLoopFor(Dst->getParent()),
                         Pair[SI].Loops);
      Result.Consistent = false;
    } else if (Pair[SI].Classification == Subscript::ZIV) {
      // always separable
      Separable.set(SI);
    }
    else {
      // SIV, RDIV, or MIV, so check for coupled group
      bool Done = true;
      for (unsigned SJ = SI + 1; SJ < Pairs; ++SJ) {
        SmallBitVector Intersection = Pair[SI].GroupLoops;
        Intersection &= Pair[SJ].GroupLoops;
        if (Intersection.any()) {
          // accumulate set of all the loops in group
          Pair[SJ].GroupLoops |= Pair[SI].GroupLoops;
          // accumulate set of all subscripts in group
          Pair[SJ].Group |= Pair[SI].Group;
          Done = false;
        }
      }
      if (Done) {
        if (Pair[SI].Group.count() == 1) {
          Separable.set(SI);
          ++SeparableSubscriptPairs;
        }
        else {
          Coupled.set(SI);
          ++CoupledSubscriptPairs;
        }
      }
    }
  }

  LLVM_DEBUG(dbgs() << "    Separable = ");
  LLVM_DEBUG(dumpSmallBitVector(Separable));
  LLVM_DEBUG(dbgs() << "    Coupled = ");
  LLVM_DEBUG(dumpSmallBitVector(Coupled));

  Constraint NewConstraint;
  NewConstraint.setAny(SE);

  // test separable subscripts
  for (unsigned SI : Separable.set_bits()) {
    LLVM_DEBUG(dbgs() << "testing subscript " << SI);
    switch (Pair[SI].Classification) {
    case Subscript::ZIV:
      LLVM_DEBUG(dbgs() << ", ZIV\n");
      if (testZIV(Pair[SI].Src, Pair[SI].Dst, Result))
        return nullptr;
      break;
    case Subscript::SIV: {
      LLVM_DEBUG(dbgs() << ", SIV\n");
      unsigned Level;
      const SCEV *SplitIter = nullptr;
      if (testSIV(Pair[SI].Src, Pair[SI].Dst, Level, Result, NewConstraint,
                  SplitIter))
        return nullptr;
      break;
    }
    case Subscript::RDIV:
      LLVM_DEBUG(dbgs() << ", RDIV\n");
      if (testRDIV(Pair[SI].Src, Pair[SI].Dst, Result))
        return nullptr;
      break;
    case Subscript::MIV:
      LLVM_DEBUG(dbgs() << ", MIV\n");
      if (testMIV(Pair[SI].Src, Pair[SI].Dst, Pair[SI].Loops, Result))
        return nullptr;
      break;
    default:
      llvm_unreachable("subscript has unexpected classification");
    }
  }

  if (Coupled.count()) {
    // test coupled subscript groups
    LLVM_DEBUG(dbgs() << "starting on coupled subscripts\n");
    LLVM_DEBUG(dbgs() << "MaxLevels + 1 = " << MaxLevels + 1 << "\n");
    SmallVector<Constraint, 4> Constraints(MaxLevels + 1);
    for (unsigned II = 0; II <= MaxLevels; ++II)
      Constraints[II].setAny(SE);
    for (unsigned SI : Coupled.set_bits()) {
      LLVM_DEBUG(dbgs() << "testing subscript group " << SI << " { ");
      SmallBitVector Group(Pair[SI].Group);
      SmallBitVector Sivs(Pairs);
      SmallBitVector Mivs(Pairs);
      SmallBitVector ConstrainedLevels(MaxLevels + 1);
      SmallVector<Subscript *, 4> PairsInGroup;
      for (unsigned SJ : Group.set_bits()) {
        LLVM_DEBUG(dbgs() << SJ << " ");
        if (Pair[SJ].Classification == Subscript::SIV)
          Sivs.set(SJ);
        else
          Mivs.set(SJ);
        PairsInGroup.push_back(&Pair[SJ]);
      }
      unifySubscriptType(PairsInGroup);
      LLVM_DEBUG(dbgs() << "}\n");
      while (Sivs.any()) {
        bool Changed = false;
        for (unsigned SJ : Sivs.set_bits()) {
          LLVM_DEBUG(dbgs() << "testing subscript " << SJ << ", SIV\n");
          // SJ is an SIV subscript that's part of the current coupled group
          unsigned Level;
          const SCEV *SplitIter = nullptr;
          LLVM_DEBUG(dbgs() << "SIV\n");
          if (testSIV(Pair[SJ].Src, Pair[SJ].Dst, Level, Result, NewConstraint,
                      SplitIter))
            return nullptr;
          ConstrainedLevels.set(Level);
          if (intersectConstraints(&Constraints[Level], &NewConstraint)) {
            if (Constraints[Level].isEmpty()) {
              ++DeltaIndependence;
              return nullptr;
            }
            Changed = true;
          }
          Sivs.reset(SJ);
        }
        if (Changed) {
          // propagate, possibly creating new SIVs and ZIVs
          LLVM_DEBUG(dbgs() << "    propagating\n");
          LLVM_DEBUG(dbgs() << "\tMivs = ");
          LLVM_DEBUG(dumpSmallBitVector(Mivs));
          for (unsigned SJ : Mivs.set_bits()) {
            // SJ is an MIV subscript that's part of the current coupled group
            LLVM_DEBUG(dbgs() << "\tSJ = " << SJ << "\n");
            if (propagate(Pair[SJ].Src, Pair[SJ].Dst, Pair[SJ].Loops,
                          Constraints, Result.Consistent)) {
              LLVM_DEBUG(dbgs() << "\t    Changed\n");
              ++DeltaPropagations;
              Pair[SJ].Classification =
                classifyPair(Pair[SJ].Src, LI->getLoopFor(Src->getParent()),
                             Pair[SJ].Dst, LI->getLoopFor(Dst->getParent()),
                             Pair[SJ].Loops);
              switch (Pair[SJ].Classification) {
              case Subscript::ZIV:
                LLVM_DEBUG(dbgs() << "ZIV\n");
                if (testZIV(Pair[SJ].Src, Pair[SJ].Dst, Result))
                  return nullptr;
                Mivs.reset(SJ);
                break;
              case Subscript::SIV:
                Sivs.set(SJ);
                Mivs.reset(SJ);
                break;
              case Subscript::RDIV:
              case Subscript::MIV:
                break;
              default:
                llvm_unreachable("bad subscript classification");
              }
            }
          }
        }
      }

      // test & propagate remaining RDIVs
      for (unsigned SJ : Mivs.set_bits()) {
        if (Pair[SJ].Classification == Subscript::RDIV) {
          LLVM_DEBUG(dbgs() << "RDIV test\n");
          if (testRDIV(Pair[SJ].Src, Pair[SJ].Dst, Result))
            return nullptr;
          // I don't yet understand how to propagate RDIV results
          Mivs.reset(SJ);
        }
      }

      // test remaining MIVs
      // This code is temporary.
      // Better to somehow test all remaining subscripts simultaneously.
      for (unsigned SJ : Mivs.set_bits()) {
        if (Pair[SJ].Classification == Subscript::MIV) {
          LLVM_DEBUG(dbgs() << "MIV test\n");
          if (testMIV(Pair[SJ].Src, Pair[SJ].Dst, Pair[SJ].Loops, Result))
            return nullptr;
        }
        else
          llvm_unreachable("expected only MIV subscripts at this point");
      }

      // update Result.DV from constraint vector
      LLVM_DEBUG(dbgs() << "    updating\n");
      for (unsigned SJ : ConstrainedLevels.set_bits()) {
        if (SJ > CommonLevels)
          break;
        updateDirection(Result.DV[SJ - 1], Constraints[SJ]);
        if (Result.DV[SJ - 1].Direction == Dependence::DVEntry::NONE)
          return nullptr;
      }
    }
  }

  // Make sure the Scalar flags are set correctly.
  SmallBitVector CompleteLoops(MaxLevels + 1);
  for (unsigned SI = 0; SI < Pairs; ++SI)
    CompleteLoops |= Pair[SI].Loops;
  for (unsigned II = 1; II <= CommonLevels; ++II)
    if (CompleteLoops[II])
      Result.DV[II - 1].Scalar = false;

  if (PossiblyLoopIndependent) {
    // Make sure the LoopIndependent flag is set correctly.
    // All directions must include equal, otherwise no
    // loop-independent dependence is possible.
    for (unsigned II = 1; II <= CommonLevels; ++II) {
      if (!(Result.getDirection(II) & Dependence::DVEntry::EQ)) {
        Result.LoopIndependent = false;
        break;
      }
    }
  }
  else {
    // On the other hand, if all directions are equal and there's no
    // loop-independent dependence possible, then no dependence exists.
    bool AllEqual = true;
    for (unsigned II = 1; II <= CommonLevels; ++II) {
      if (Result.getDirection(II) != Dependence::DVEntry::EQ) {
        AllEqual = false;
        break;
      }
    }
    if (AllEqual)
      return nullptr;
  }

  return make_unique<FullDependence>(std::move(Result));
}



//===----------------------------------------------------------------------===//
// getSplitIteration -
// Rather than spend rarely-used space recording the splitting iteration
// during the Weak-Crossing SIV test, we re-compute it on demand.
// The re-computation is basically a repeat of the entire dependence test,
// though simplified since we know that the dependence exists.
// It's tedious, since we must go through all propagations, etc.
//
// Care is required to keep this code up to date with respect to the routine
// above, depends().
//
// Generally, the dependence analyzer will be used to build
// a dependence graph for a function (basically a map from instructions
// to dependences). Looking for cycles in the graph shows us loops
// that cannot be trivially vectorized/parallelized.
//
// We can try to improve the situation by examining all the dependences
// that make up the cycle, looking for ones we can break.
// Sometimes, peeling the first or last iteration of a loop will break
// dependences, and we've got flags for those possibilities.
// Sometimes, splitting a loop at some other iteration will do the trick,
// and we've got a flag for that case. Rather than waste the space to
// record the exact iteration (since we rarely know), we provide
// a method that calculates the iteration. It's a drag that it must work
// from scratch, but wonderful in that it's possible.
//
// Here's an example:
//
//    for (i = 0; i < 10; i++)
//        A[i] = ...
//        ... = A[11 - i]
//
// There's a loop-carried flow dependence from the store to the load,
// found by the weak-crossing SIV test. The dependence will have a flag,
// indicating that the dependence can be broken by splitting the loop.
// Calling getSplitIteration will return 5.
// Splitting the loop breaks the dependence, like so:
//
//    for (i = 0; i <= 5; i++)
//        A[i] = ...
//        ... = A[11 - i]
//    for (i = 6; i < 10; i++)
//        A[i] = ...
//        ... = A[11 - i]
//
// breaks the dependence and allows us to vectorize/parallelize
// both loops.
const SCEV *DependenceInfo::getSplitIteration(const Dependence &Dep,
                                              unsigned SplitLevel) {
  assert(Dep.isSplitable(SplitLevel) &&
         "Dep should be splitable at SplitLevel");
  Instruction *Src = Dep.getSrc();
  Instruction *Dst = Dep.getDst();
  assert(Src->mayReadFromMemory() || Src->mayWriteToMemory());
  assert(Dst->mayReadFromMemory() || Dst->mayWriteToMemory());
  assert(isLoadOrStore(Src));
  assert(isLoadOrStore(Dst));
  Value *SrcPtr = getLoadStorePointerOperand(Src);
  Value *DstPtr = getLoadStorePointerOperand(Dst);
  assert(underlyingObjectsAlias(AA, F->getParent()->getDataLayout(),
                                MemoryLocation::get(Dst),
                                MemoryLocation::get(Src)) == MustAlias);

  // establish loop nesting levels
  establishNestingLevels(Src, Dst);

  FullDependence Result(Src, Dst, false, CommonLevels);

  unsigned Pairs = 1;
  SmallVector<Subscript, 2> Pair(Pairs);
  const SCEV *SrcSCEV = SE->getSCEV(SrcPtr);
  const SCEV *DstSCEV = SE->getSCEV(DstPtr);
  Pair[0].Src = SrcSCEV;
  Pair[0].Dst = DstSCEV;

  if (Delinearize) {
    if (tryDelinearize(Src, Dst, Pair)) {
      LLVM_DEBUG(dbgs() << "    delinearized\n");
      Pairs = Pair.size();
    }
  }

  for (unsigned P = 0; P < Pairs; ++P) {
    Pair[P].Loops.resize(MaxLevels + 1);
    Pair[P].GroupLoops.resize(MaxLevels + 1);
    Pair[P].Group.resize(Pairs);
    removeMatchingExtensions(&Pair[P]);
    Pair[P].Classification =
      classifyPair(Pair[P].Src, LI->getLoopFor(Src->getParent()),
                   Pair[P].Dst, LI->getLoopFor(Dst->getParent()),
                   Pair[P].Loops);
    Pair[P].GroupLoops = Pair[P].Loops;
    Pair[P].Group.set(P);
  }

  SmallBitVector Separable(Pairs);
  SmallBitVector Coupled(Pairs);

  // partition subscripts into separable and minimally-coupled groups
  for (unsigned SI = 0; SI < Pairs; ++SI) {
    if (Pair[SI].Classification == Subscript::NonLinear) {
      // ignore these, but collect loops for later
      collectCommonLoops(Pair[SI].Src,
                         LI->getLoopFor(Src->getParent()),
                         Pair[SI].Loops);
      collectCommonLoops(Pair[SI].Dst,
                         LI->getLoopFor(Dst->getParent()),
                         Pair[SI].Loops);
      Result.Consistent = false;
    }
    else if (Pair[SI].Classification == Subscript::ZIV)
      Separable.set(SI);
    else {
      // SIV, RDIV, or MIV, so check for coupled group
      bool Done = true;
      for (unsigned SJ = SI + 1; SJ < Pairs; ++SJ) {
        SmallBitVector Intersection = Pair[SI].GroupLoops;
        Intersection &= Pair[SJ].GroupLoops;
        if (Intersection.any()) {
          // accumulate set of all the loops in group
          Pair[SJ].GroupLoops |= Pair[SI].GroupLoops;
          // accumulate set of all subscripts in group
          Pair[SJ].Group |= Pair[SI].Group;
          Done = false;
        }
      }
      if (Done) {
        if (Pair[SI].Group.count() == 1)
          Separable.set(SI);
        else
          Coupled.set(SI);
      }
    }
  }

  Constraint NewConstraint;
  NewConstraint.setAny(SE);

  // test separable subscripts
  for (unsigned SI : Separable.set_bits()) {
    switch (Pair[SI].Classification) {
    case Subscript::SIV: {
      unsigned Level;
      const SCEV *SplitIter = nullptr;
      (void) testSIV(Pair[SI].Src, Pair[SI].Dst, Level,
                     Result, NewConstraint, SplitIter);
      if (Level == SplitLevel) {
        assert(SplitIter != nullptr);
        return SplitIter;
      }
      break;
    }
    case Subscript::ZIV:
    case Subscript::RDIV:
    case Subscript::MIV:
      break;
    default:
      llvm_unreachable("subscript has unexpected classification");
    }
  }

  if (Coupled.count()) {
    // test coupled subscript groups
    SmallVector<Constraint, 4> Constraints(MaxLevels + 1);
    for (unsigned II = 0; II <= MaxLevels; ++II)
      Constraints[II].setAny(SE);
    for (unsigned SI : Coupled.set_bits()) {
      SmallBitVector Group(Pair[SI].Group);
      SmallBitVector Sivs(Pairs);
      SmallBitVector Mivs(Pairs);
      SmallBitVector ConstrainedLevels(MaxLevels + 1);
      for (unsigned SJ : Group.set_bits()) {
        if (Pair[SJ].Classification == Subscript::SIV)
          Sivs.set(SJ);
        else
          Mivs.set(SJ);
      }
      while (Sivs.any()) {
        bool Changed = false;
        for (unsigned SJ : Sivs.set_bits()) {
          // SJ is an SIV subscript that's part of the current coupled group
          unsigned Level;
          const SCEV *SplitIter = nullptr;
          (void) testSIV(Pair[SJ].Src, Pair[SJ].Dst, Level,
                         Result, NewConstraint, SplitIter);
          if (Level == SplitLevel && SplitIter)
            return SplitIter;
          ConstrainedLevels.set(Level);
          if (intersectConstraints(&Constraints[Level], &NewConstraint))
            Changed = true;
          Sivs.reset(SJ);
        }
        if (Changed) {
          // propagate, possibly creating new SIVs and ZIVs
          for (unsigned SJ : Mivs.set_bits()) {
            // SJ is an MIV subscript that's part of the current coupled group
            if (propagate(Pair[SJ].Src, Pair[SJ].Dst,
                          Pair[SJ].Loops, Constraints, Result.Consistent)) {
              Pair[SJ].Classification =
                classifyPair(Pair[SJ].Src, LI->getLoopFor(Src->getParent()),
                             Pair[SJ].Dst, LI->getLoopFor(Dst->getParent()),
                             Pair[SJ].Loops);
              switch (Pair[SJ].Classification) {
              case Subscript::ZIV:
                Mivs.reset(SJ);
                break;
              case Subscript::SIV:
                Sivs.set(SJ);
                Mivs.reset(SJ);
                break;
              case Subscript::RDIV:
              case Subscript::MIV:
                break;
              default:
                llvm_unreachable("bad subscript classification");
              }
            }
          }
        }
      }
    }
  }
  llvm_unreachable("somehow reached end of routine");
  return nullptr;
}
