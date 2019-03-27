//===- LoopVectorize.cpp - A Loop Vectorizer ------------------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This is the LLVM loop vectorizer. This pass modifies 'vectorizable' loops
// and generates target-independent LLVM-IR.
// The vectorizer uses the TargetTransformInfo analysis to estimate the costs
// of instructions in order to estimate the profitability of vectorization.
//
// The loop vectorizer combines consecutive loop iterations into a single
// 'wide' iteration. After this transformation the index is incremented
// by the SIMD vector width, and not by one.
//
// This pass has three parts:
// 1. The main loop pass that drives the different parts.
// 2. LoopVectorizationLegality - A unit that checks for the legality
//    of the vectorization.
// 3. InnerLoopVectorizer - A unit that performs the actual
//    widening of instructions.
// 4. LoopVectorizationCostModel - A unit that checks for the profitability
//    of vectorization. It decides on the optimal vector width, which
//    can be one, if vectorization is not profitable.
//
// There is a development effort going on to migrate loop vectorizer to the
// VPlan infrastructure and to introduce outer loop vectorization support (see
// docs/Proposal/VectorizationPlan.rst and
// http://lists.llvm.org/pipermail/llvm-dev/2017-December/119523.html). For this
// purpose, we temporarily introduced the VPlan-native vectorization path: an
// alternative vectorization path that is natively implemented on top of the
// VPlan infrastructure. See EnableVPlanNativePath for enabling.
//
//===----------------------------------------------------------------------===//
//
// The reduction-variable vectorization is based on the paper:
//  D. Nuzman and R. Henderson. Multi-platform Auto-vectorization.
//
// Variable uniformity checks are inspired by:
//  Karrenberg, R. and Hack, S. Whole Function Vectorization.
//
// The interleaved access vectorization is based on the paper:
//  Dorit Nuzman, Ira Rosen and Ayal Zaks.  Auto-Vectorization of Interleaved
//  Data for SIMD
//
// Other ideas/concepts are from:
//  A. Zaks and D. Nuzman. Autovectorization in GCC-two years later.
//
//  S. Maleki, Y. Gao, M. Garzaran, T. Wong and D. Padua.  An Evaluation of
//  Vectorizing Compilers.
//
//===----------------------------------------------------------------------===//

#include "llvm/Transforms/Vectorize/LoopVectorize.h"
#include "LoopVectorizationPlanner.h"
#include "VPRecipeBuilder.h"
#include "VPlanHCFGBuilder.h"
#include "VPlanHCFGTransforms.h"
#include "llvm/ADT/APInt.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/DenseMapInfo.h"
#include "llvm/ADT/Hashing.h"
#include "llvm/ADT/MapVector.h"
#include "llvm/ADT/None.h"
#include "llvm/ADT/Optional.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SetVector.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/Twine.h"
#include "llvm/ADT/iterator_range.h"
#include "llvm/Analysis/AssumptionCache.h"
#include "llvm/Analysis/BasicAliasAnalysis.h"
#include "llvm/Analysis/BlockFrequencyInfo.h"
#include "llvm/Analysis/CFG.h"
#include "llvm/Analysis/CodeMetrics.h"
#include "llvm/Analysis/DemandedBits.h"
#include "llvm/Analysis/GlobalsModRef.h"
#include "llvm/Analysis/LoopAccessAnalysis.h"
#include "llvm/Analysis/LoopAnalysisManager.h"
#include "llvm/Analysis/LoopInfo.h"
#include "llvm/Analysis/LoopIterator.h"
#include "llvm/Analysis/OptimizationRemarkEmitter.h"
#include "llvm/Analysis/ScalarEvolution.h"
#include "llvm/Analysis/ScalarEvolutionExpander.h"
#include "llvm/Analysis/ScalarEvolutionExpressions.h"
#include "llvm/Analysis/TargetLibraryInfo.h"
#include "llvm/Analysis/TargetTransformInfo.h"
#include "llvm/Analysis/VectorUtils.h"
#include "llvm/IR/Attributes.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/CFG.h"
#include "llvm/IR/Constant.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/IR/DebugInfoMetadata.h"
#include "llvm/IR/DebugLoc.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/DiagnosticInfo.h"
#include "llvm/IR/Dominators.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/InstrTypes.h"
#include "llvm/IR/Instruction.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/IntrinsicInst.h"
#include "llvm/IR/Intrinsics.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Metadata.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Operator.h"
#include "llvm/IR/Type.h"
#include "llvm/IR/Use.h"
#include "llvm/IR/User.h"
#include "llvm/IR/Value.h"
#include "llvm/IR/ValueHandle.h"
#include "llvm/IR/Verifier.h"
#include "llvm/Pass.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/MathExtras.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Transforms/Utils/BasicBlockUtils.h"
#include "llvm/Transforms/Utils/LoopSimplify.h"
#include "llvm/Transforms/Utils/LoopUtils.h"
#include "llvm/Transforms/Utils/LoopVersioning.h"
#include "llvm/Transforms/Vectorize/LoopVectorizationLegality.h"
#include <algorithm>
#include <cassert>
#include <cstdint>
#include <cstdlib>
#include <functional>
#include <iterator>
#include <limits>
#include <memory>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

using namespace llvm;

#define LV_NAME "loop-vectorize"
#define DEBUG_TYPE LV_NAME

/// @{
/// Metadata attribute names
static const char *const LLVMLoopVectorizeFollowupAll =
    "llvm.loop.vectorize.followup_all";
static const char *const LLVMLoopVectorizeFollowupVectorized =
    "llvm.loop.vectorize.followup_vectorized";
static const char *const LLVMLoopVectorizeFollowupEpilogue =
    "llvm.loop.vectorize.followup_epilogue";
/// @}

STATISTIC(LoopsVectorized, "Number of loops vectorized");
STATISTIC(LoopsAnalyzed, "Number of loops analyzed for vectorization");

/// Loops with a known constant trip count below this number are vectorized only
/// if no scalar iteration overheads are incurred.
static cl::opt<unsigned> TinyTripCountVectorThreshold(
    "vectorizer-min-trip-count", cl::init(16), cl::Hidden,
    cl::desc("Loops with a constant trip count that is smaller than this "
             "value are vectorized only if no scalar iteration overheads "
             "are incurred."));

static cl::opt<bool> MaximizeBandwidth(
    "vectorizer-maximize-bandwidth", cl::init(false), cl::Hidden,
    cl::desc("Maximize bandwidth when selecting vectorization factor which "
             "will be determined by the smallest type in loop."));

static cl::opt<bool> EnableInterleavedMemAccesses(
    "enable-interleaved-mem-accesses", cl::init(false), cl::Hidden,
    cl::desc("Enable vectorization on interleaved memory accesses in a loop"));

/// An interleave-group may need masking if it resides in a block that needs
/// predication, or in order to mask away gaps. 
static cl::opt<bool> EnableMaskedInterleavedMemAccesses(
    "enable-masked-interleaved-mem-accesses", cl::init(false), cl::Hidden,
    cl::desc("Enable vectorization on masked interleaved memory accesses in a loop"));

/// We don't interleave loops with a known constant trip count below this
/// number.
static const unsigned TinyTripCountInterleaveThreshold = 128;

static cl::opt<unsigned> ForceTargetNumScalarRegs(
    "force-target-num-scalar-regs", cl::init(0), cl::Hidden,
    cl::desc("A flag that overrides the target's number of scalar registers."));

static cl::opt<unsigned> ForceTargetNumVectorRegs(
    "force-target-num-vector-regs", cl::init(0), cl::Hidden,
    cl::desc("A flag that overrides the target's number of vector registers."));

static cl::opt<unsigned> ForceTargetMaxScalarInterleaveFactor(
    "force-target-max-scalar-interleave", cl::init(0), cl::Hidden,
    cl::desc("A flag that overrides the target's max interleave factor for "
             "scalar loops."));

static cl::opt<unsigned> ForceTargetMaxVectorInterleaveFactor(
    "force-target-max-vector-interleave", cl::init(0), cl::Hidden,
    cl::desc("A flag that overrides the target's max interleave factor for "
             "vectorized loops."));

static cl::opt<unsigned> ForceTargetInstructionCost(
    "force-target-instruction-cost", cl::init(0), cl::Hidden,
    cl::desc("A flag that overrides the target's expected cost for "
             "an instruction to a single constant value. Mostly "
             "useful for getting consistent testing."));

static cl::opt<unsigned> SmallLoopCost(
    "small-loop-cost", cl::init(20), cl::Hidden,
    cl::desc(
        "The cost of a loop that is considered 'small' by the interleaver."));

static cl::opt<bool> LoopVectorizeWithBlockFrequency(
    "loop-vectorize-with-block-frequency", cl::init(true), cl::Hidden,
    cl::desc("Enable the use of the block frequency analysis to access PGO "
             "heuristics minimizing code growth in cold regions and being more "
             "aggressive in hot regions."));

// Runtime interleave loops for load/store throughput.
static cl::opt<bool> EnableLoadStoreRuntimeInterleave(
    "enable-loadstore-runtime-interleave", cl::init(true), cl::Hidden,
    cl::desc(
        "Enable runtime interleaving until load/store ports are saturated"));

/// The number of stores in a loop that are allowed to need predication.
static cl::opt<unsigned> NumberOfStoresToPredicate(
    "vectorize-num-stores-pred", cl::init(1), cl::Hidden,
    cl::desc("Max number of stores to be predicated behind an if."));

static cl::opt<bool> EnableIndVarRegisterHeur(
    "enable-ind-var-reg-heur", cl::init(true), cl::Hidden,
    cl::desc("Count the induction variable only once when interleaving"));

static cl::opt<bool> EnableCondStoresVectorization(
    "enable-cond-stores-vec", cl::init(true), cl::Hidden,
    cl::desc("Enable if predication of stores during vectorization."));

static cl::opt<unsigned> MaxNestedScalarReductionIC(
    "max-nested-scalar-reduction-interleave", cl::init(2), cl::Hidden,
    cl::desc("The maximum interleave count to use when interleaving a scalar "
             "reduction in a nested loop."));

cl::opt<bool> EnableVPlanNativePath(
    "enable-vplan-native-path", cl::init(false), cl::Hidden,
    cl::desc("Enable VPlan-native vectorization path with "
             "support for outer loop vectorization."));

// This flag enables the stress testing of the VPlan H-CFG construction in the
// VPlan-native vectorization path. It must be used in conjuction with
// -enable-vplan-native-path. -vplan-verify-hcfg can also be used to enable the
// verification of the H-CFGs built.
static cl::opt<bool> VPlanBuildStressTest(
    "vplan-build-stress-test", cl::init(false), cl::Hidden,
    cl::desc(
        "Build VPlan for every supported loop nest in the function and bail "
        "out right after the build (stress test the VPlan H-CFG construction "
        "in the VPlan-native vectorization path)."));

/// A helper function for converting Scalar types to vector types.
/// If the incoming type is void, we return void. If the VF is 1, we return
/// the scalar type.
static Type *ToVectorTy(Type *Scalar, unsigned VF) {
  if (Scalar->isVoidTy() || VF == 1)
    return Scalar;
  return VectorType::get(Scalar, VF);
}

/// A helper function that returns the type of loaded or stored value.
static Type *getMemInstValueType(Value *I) {
  assert((isa<LoadInst>(I) || isa<StoreInst>(I)) &&
         "Expected Load or Store instruction");
  if (auto *LI = dyn_cast<LoadInst>(I))
    return LI->getType();
  return cast<StoreInst>(I)->getValueOperand()->getType();
}

/// A helper function that returns true if the given type is irregular. The
/// type is irregular if its allocated size doesn't equal the store size of an
/// element of the corresponding vector type at the given vectorization factor.
static bool hasIrregularType(Type *Ty, const DataLayout &DL, unsigned VF) {
  // Determine if an array of VF elements of type Ty is "bitcast compatible"
  // with a <VF x Ty> vector.
  if (VF > 1) {
    auto *VectorTy = VectorType::get(Ty, VF);
    return VF * DL.getTypeAllocSize(Ty) != DL.getTypeStoreSize(VectorTy);
  }

  // If the vectorization factor is one, we just check if an array of type Ty
  // requires padding between elements.
  return DL.getTypeAllocSizeInBits(Ty) != DL.getTypeSizeInBits(Ty);
}

/// A helper function that returns the reciprocal of the block probability of
/// predicated blocks. If we return X, we are assuming the predicated block
/// will execute once for every X iterations of the loop header.
///
/// TODO: We should use actual block probability here, if available. Currently,
///       we always assume predicated blocks have a 50% chance of executing.
static unsigned getReciprocalPredBlockProb() { return 2; }

/// A helper function that adds a 'fast' flag to floating-point operations.
static Value *addFastMathFlag(Value *V) {
  if (isa<FPMathOperator>(V)) {
    FastMathFlags Flags;
    Flags.setFast();
    cast<Instruction>(V)->setFastMathFlags(Flags);
  }
  return V;
}

/// A helper function that returns an integer or floating-point constant with
/// value C.
static Constant *getSignedIntOrFpConstant(Type *Ty, int64_t C) {
  return Ty->isIntegerTy() ? ConstantInt::getSigned(Ty, C)
                           : ConstantFP::get(Ty, C);
}

namespace llvm {

/// InnerLoopVectorizer vectorizes loops which contain only one basic
/// block to a specified vectorization factor (VF).
/// This class performs the widening of scalars into vectors, or multiple
/// scalars. This class also implements the following features:
/// * It inserts an epilogue loop for handling loops that don't have iteration
///   counts that are known to be a multiple of the vectorization factor.
/// * It handles the code generation for reduction variables.
/// * Scalarization (implementation using scalars) of un-vectorizable
///   instructions.
/// InnerLoopVectorizer does not perform any vectorization-legality
/// checks, and relies on the caller to check for the different legality
/// aspects. The InnerLoopVectorizer relies on the
/// LoopVectorizationLegality class to provide information about the induction
/// and reduction variables that were found to a given vectorization factor.
class InnerLoopVectorizer {
public:
  InnerLoopVectorizer(Loop *OrigLoop, PredicatedScalarEvolution &PSE,
                      LoopInfo *LI, DominatorTree *DT,
                      const TargetLibraryInfo *TLI,
                      const TargetTransformInfo *TTI, AssumptionCache *AC,
                      OptimizationRemarkEmitter *ORE, unsigned VecWidth,
                      unsigned UnrollFactor, LoopVectorizationLegality *LVL,
                      LoopVectorizationCostModel *CM)
      : OrigLoop(OrigLoop), PSE(PSE), LI(LI), DT(DT), TLI(TLI), TTI(TTI),
        AC(AC), ORE(ORE), VF(VecWidth), UF(UnrollFactor),
        Builder(PSE.getSE()->getContext()),
        VectorLoopValueMap(UnrollFactor, VecWidth), Legal(LVL), Cost(CM) {}
  virtual ~InnerLoopVectorizer() = default;

  /// Create a new empty loop. Unlink the old loop and connect the new one.
  /// Return the pre-header block of the new loop.
  BasicBlock *createVectorizedLoopSkeleton();

  /// Widen a single instruction within the innermost loop.
  void widenInstruction(Instruction &I);

  /// Fix the vectorized code, taking care of header phi's, live-outs, and more.
  void fixVectorizedLoop();

  // Return true if any runtime check is added.
  bool areSafetyChecksAdded() { return AddedSafetyChecks; }

  /// A type for vectorized values in the new loop. Each value from the
  /// original loop, when vectorized, is represented by UF vector values in the
  /// new unrolled loop, where UF is the unroll factor.
  using VectorParts = SmallVector<Value *, 2>;

  /// Vectorize a single PHINode in a block. This method handles the induction
  /// variable canonicalization. It supports both VF = 1 for unrolled loops and
  /// arbitrary length vectors.
  void widenPHIInstruction(Instruction *PN, unsigned UF, unsigned VF);

  /// A helper function to scalarize a single Instruction in the innermost loop.
  /// Generates a sequence of scalar instances for each lane between \p MinLane
  /// and \p MaxLane, times each part between \p MinPart and \p MaxPart,
  /// inclusive..
  void scalarizeInstruction(Instruction *Instr, const VPIteration &Instance,
                            bool IfPredicateInstr);

  /// Widen an integer or floating-point induction variable \p IV. If \p Trunc
  /// is provided, the integer induction variable will first be truncated to
  /// the corresponding type.
  void widenIntOrFpInduction(PHINode *IV, TruncInst *Trunc = nullptr);

  /// getOrCreateVectorValue and getOrCreateScalarValue coordinate to generate a
  /// vector or scalar value on-demand if one is not yet available. When
  /// vectorizing a loop, we visit the definition of an instruction before its
  /// uses. When visiting the definition, we either vectorize or scalarize the
  /// instruction, creating an entry for it in the corresponding map. (In some
  /// cases, such as induction variables, we will create both vector and scalar
  /// entries.) Then, as we encounter uses of the definition, we derive values
  /// for each scalar or vector use unless such a value is already available.
  /// For example, if we scalarize a definition and one of its uses is vector,
  /// we build the required vector on-demand with an insertelement sequence
  /// when visiting the use. Otherwise, if the use is scalar, we can use the
  /// existing scalar definition.
  ///
  /// Return a value in the new loop corresponding to \p V from the original
  /// loop at unroll index \p Part. If the value has already been vectorized,
  /// the corresponding vector entry in VectorLoopValueMap is returned. If,
  /// however, the value has a scalar entry in VectorLoopValueMap, we construct
  /// a new vector value on-demand by inserting the scalar values into a vector
  /// with an insertelement sequence. If the value has been neither vectorized
  /// nor scalarized, it must be loop invariant, so we simply broadcast the
  /// value into a vector.
  Value *getOrCreateVectorValue(Value *V, unsigned Part);

  /// Return a value in the new loop corresponding to \p V from the original
  /// loop at unroll and vector indices \p Instance. If the value has been
  /// vectorized but not scalarized, the necessary extractelement instruction
  /// will be generated.
  Value *getOrCreateScalarValue(Value *V, const VPIteration &Instance);

  /// Construct the vector value of a scalarized value \p V one lane at a time.
  void packScalarIntoVectorValue(Value *V, const VPIteration &Instance);

  /// Try to vectorize the interleaved access group that \p Instr belongs to,
  /// optionally masking the vector operations if \p BlockInMask is non-null.
  void vectorizeInterleaveGroup(Instruction *Instr,
                                VectorParts *BlockInMask = nullptr);

  /// Vectorize Load and Store instructions, optionally masking the vector
  /// operations if \p BlockInMask is non-null.
  void vectorizeMemoryInstruction(Instruction *Instr,
                                  VectorParts *BlockInMask = nullptr);

  /// Set the debug location in the builder using the debug location in
  /// the instruction.
  void setDebugLocFromInst(IRBuilder<> &B, const Value *Ptr);

  /// Fix the non-induction PHIs in the OrigPHIsToFix vector.
  void fixNonInductionPHIs(void);

protected:
  friend class LoopVectorizationPlanner;

  /// A small list of PHINodes.
  using PhiVector = SmallVector<PHINode *, 4>;

  /// A type for scalarized values in the new loop. Each value from the
  /// original loop, when scalarized, is represented by UF x VF scalar values
  /// in the new unrolled loop, where UF is the unroll factor and VF is the
  /// vectorization factor.
  using ScalarParts = SmallVector<SmallVector<Value *, 4>, 2>;

  /// Set up the values of the IVs correctly when exiting the vector loop.
  void fixupIVUsers(PHINode *OrigPhi, const InductionDescriptor &II,
                    Value *CountRoundDown, Value *EndValue,
                    BasicBlock *MiddleBlock);

  /// Create a new induction variable inside L.
  PHINode *createInductionVariable(Loop *L, Value *Start, Value *End,
                                   Value *Step, Instruction *DL);

  /// Handle all cross-iteration phis in the header.
  void fixCrossIterationPHIs();

  /// Fix a first-order recurrence. This is the second phase of vectorizing
  /// this phi node.
  void fixFirstOrderRecurrence(PHINode *Phi);

  /// Fix a reduction cross-iteration phi. This is the second phase of
  /// vectorizing this phi node.
  void fixReduction(PHINode *Phi);

  /// The Loop exit block may have single value PHI nodes with some
  /// incoming value. While vectorizing we only handled real values
  /// that were defined inside the loop and we should have one value for
  /// each predecessor of its parent basic block. See PR14725.
  void fixLCSSAPHIs();

  /// Iteratively sink the scalarized operands of a predicated instruction into
  /// the block that was created for it.
  void sinkScalarOperands(Instruction *PredInst);

  /// Shrinks vector element sizes to the smallest bitwidth they can be legally
  /// represented as.
  void truncateToMinimalBitwidths();

  /// Insert the new loop to the loop hierarchy and pass manager
  /// and update the analysis passes.
  void updateAnalysis();

  /// Create a broadcast instruction. This method generates a broadcast
  /// instruction (shuffle) for loop invariant values and for the induction
  /// value. If this is the induction variable then we extend it to N, N+1, ...
  /// this is needed because each iteration in the loop corresponds to a SIMD
  /// element.
  virtual Value *getBroadcastInstrs(Value *V);

  /// This function adds (StartIdx, StartIdx + Step, StartIdx + 2*Step, ...)
  /// to each vector element of Val. The sequence starts at StartIndex.
  /// \p Opcode is relevant for FP induction variable.
  virtual Value *getStepVector(Value *Val, int StartIdx, Value *Step,
                               Instruction::BinaryOps Opcode =
                               Instruction::BinaryOpsEnd);

  /// Compute scalar induction steps. \p ScalarIV is the scalar induction
  /// variable on which to base the steps, \p Step is the size of the step, and
  /// \p EntryVal is the value from the original loop that maps to the steps.
  /// Note that \p EntryVal doesn't have to be an induction variable - it
  /// can also be a truncate instruction.
  void buildScalarSteps(Value *ScalarIV, Value *Step, Instruction *EntryVal,
                        const InductionDescriptor &ID);

  /// Create a vector induction phi node based on an existing scalar one. \p
  /// EntryVal is the value from the original loop that maps to the vector phi
  /// node, and \p Step is the loop-invariant step. If \p EntryVal is a
  /// truncate instruction, instead of widening the original IV, we widen a
  /// version of the IV truncated to \p EntryVal's type.
  void createVectorIntOrFpInductionPHI(const InductionDescriptor &II,
                                       Value *Step, Instruction *EntryVal);

  /// Returns true if an instruction \p I should be scalarized instead of
  /// vectorized for the chosen vectorization factor.
  bool shouldScalarizeInstruction(Instruction *I) const;

  /// Returns true if we should generate a scalar version of \p IV.
  bool needsScalarInduction(Instruction *IV) const;

  /// If there is a cast involved in the induction variable \p ID, which should
  /// be ignored in the vectorized loop body, this function records the
  /// VectorLoopValue of the respective Phi also as the VectorLoopValue of the
  /// cast. We had already proved that the casted Phi is equal to the uncasted
  /// Phi in the vectorized loop (under a runtime guard), and therefore
  /// there is no need to vectorize the cast - the same value can be used in the
  /// vector loop for both the Phi and the cast.
  /// If \p VectorLoopValue is a scalarized value, \p Lane is also specified,
  /// Otherwise, \p VectorLoopValue is a widened/vectorized value.
  ///
  /// \p EntryVal is the value from the original loop that maps to the vector
  /// phi node and is used to distinguish what is the IV currently being
  /// processed - original one (if \p EntryVal is a phi corresponding to the
  /// original IV) or the "newly-created" one based on the proof mentioned above
  /// (see also buildScalarSteps() and createVectorIntOrFPInductionPHI()). In the
  /// latter case \p EntryVal is a TruncInst and we must not record anything for
  /// that IV, but it's error-prone to expect callers of this routine to care
  /// about that, hence this explicit parameter.
  void recordVectorLoopValueForInductionCast(const InductionDescriptor &ID,
                                             const Instruction *EntryVal,
                                             Value *VectorLoopValue,
                                             unsigned Part,
                                             unsigned Lane = UINT_MAX);

  /// Generate a shuffle sequence that will reverse the vector Vec.
  virtual Value *reverseVector(Value *Vec);

  /// Returns (and creates if needed) the original loop trip count.
  Value *getOrCreateTripCount(Loop *NewLoop);

  /// Returns (and creates if needed) the trip count of the widened loop.
  Value *getOrCreateVectorTripCount(Loop *NewLoop);

  /// Returns a bitcasted value to the requested vector type.
  /// Also handles bitcasts of vector<float> <-> vector<pointer> types.
  Value *createBitOrPointerCast(Value *V, VectorType *DstVTy,
                                const DataLayout &DL);

  /// Emit a bypass check to see if the vector trip count is zero, including if
  /// it overflows.
  void emitMinimumIterationCountCheck(Loop *L, BasicBlock *Bypass);

  /// Emit a bypass check to see if all of the SCEV assumptions we've
  /// had to make are correct.
  void emitSCEVChecks(Loop *L, BasicBlock *Bypass);

  /// Emit bypass checks to check any memory assumptions we may have made.
  void emitMemRuntimeChecks(Loop *L, BasicBlock *Bypass);

  /// Compute the transformed value of Index at offset StartValue using step
  /// StepValue.
  /// For integer induction, returns StartValue + Index * StepValue.
  /// For pointer induction, returns StartValue[Index * StepValue].
  /// FIXME: The newly created binary instructions should contain nsw/nuw
  /// flags, which can be found from the original scalar operations.
  Value *emitTransformedIndex(IRBuilder<> &B, Value *Index, ScalarEvolution *SE,
                              const DataLayout &DL,
                              const InductionDescriptor &ID) const;

  /// Add additional metadata to \p To that was not present on \p Orig.
  ///
  /// Currently this is used to add the noalias annotations based on the
  /// inserted memchecks.  Use this for instructions that are *cloned* into the
  /// vector loop.
  void addNewMetadata(Instruction *To, const Instruction *Orig);

  /// Add metadata from one instruction to another.
  ///
  /// This includes both the original MDs from \p From and additional ones (\see
  /// addNewMetadata).  Use this for *newly created* instructions in the vector
  /// loop.
  void addMetadata(Instruction *To, Instruction *From);

  /// Similar to the previous function but it adds the metadata to a
  /// vector of instructions.
  void addMetadata(ArrayRef<Value *> To, Instruction *From);

  /// The original loop.
  Loop *OrigLoop;

  /// A wrapper around ScalarEvolution used to add runtime SCEV checks. Applies
  /// dynamic knowledge to simplify SCEV expressions and converts them to a
  /// more usable form.
  PredicatedScalarEvolution &PSE;

  /// Loop Info.
  LoopInfo *LI;

  /// Dominator Tree.
  DominatorTree *DT;

  /// Alias Analysis.
  AliasAnalysis *AA;

  /// Target Library Info.
  const TargetLibraryInfo *TLI;

  /// Target Transform Info.
  const TargetTransformInfo *TTI;

  /// Assumption Cache.
  AssumptionCache *AC;

  /// Interface to emit optimization remarks.
  OptimizationRemarkEmitter *ORE;

  /// LoopVersioning.  It's only set up (non-null) if memchecks were
  /// used.
  ///
  /// This is currently only used to add no-alias metadata based on the
  /// memchecks.  The actually versioning is performed manually.
  std::unique_ptr<LoopVersioning> LVer;

  /// The vectorization SIMD factor to use. Each vector will have this many
  /// vector elements.
  unsigned VF;

  /// The vectorization unroll factor to use. Each scalar is vectorized to this
  /// many different vector instructions.
  unsigned UF;

  /// The builder that we use
  IRBuilder<> Builder;

  // --- Vectorization state ---

  /// The vector-loop preheader.
  BasicBlock *LoopVectorPreHeader;

  /// The scalar-loop preheader.
  BasicBlock *LoopScalarPreHeader;

  /// Middle Block between the vector and the scalar.
  BasicBlock *LoopMiddleBlock;

  /// The ExitBlock of the scalar loop.
  BasicBlock *LoopExitBlock;

  /// The vector loop body.
  BasicBlock *LoopVectorBody;

  /// The scalar loop body.
  BasicBlock *LoopScalarBody;

  /// A list of all bypass blocks. The first block is the entry of the loop.
  SmallVector<BasicBlock *, 4> LoopBypassBlocks;

  /// The new Induction variable which was added to the new block.
  PHINode *Induction = nullptr;

  /// The induction variable of the old basic block.
  PHINode *OldInduction = nullptr;

  /// Maps values from the original loop to their corresponding values in the
  /// vectorized loop. A key value can map to either vector values, scalar
  /// values or both kinds of values, depending on whether the key was
  /// vectorized and scalarized.
  VectorizerValueMap VectorLoopValueMap;

  /// Store instructions that were predicated.
  SmallVector<Instruction *, 4> PredicatedInstructions;

  /// Trip count of the original loop.
  Value *TripCount = nullptr;

  /// Trip count of the widened loop (TripCount - TripCount % (VF*UF))
  Value *VectorTripCount = nullptr;

  /// The legality analysis.
  LoopVectorizationLegality *Legal;

  /// The profitablity analysis.
  LoopVectorizationCostModel *Cost;

  // Record whether runtime checks are added.
  bool AddedSafetyChecks = false;

  // Holds the end values for each induction variable. We save the end values
  // so we can later fix-up the external users of the induction variables.
  DenseMap<PHINode *, Value *> IVEndValues;

  // Vector of original scalar PHIs whose corresponding widened PHIs need to be
  // fixed up at the end of vector code generation.
  SmallVector<PHINode *, 8> OrigPHIsToFix;
};

class InnerLoopUnroller : public InnerLoopVectorizer {
public:
  InnerLoopUnroller(Loop *OrigLoop, PredicatedScalarEvolution &PSE,
                    LoopInfo *LI, DominatorTree *DT,
                    const TargetLibraryInfo *TLI,
                    const TargetTransformInfo *TTI, AssumptionCache *AC,
                    OptimizationRemarkEmitter *ORE, unsigned UnrollFactor,
                    LoopVectorizationLegality *LVL,
                    LoopVectorizationCostModel *CM)
      : InnerLoopVectorizer(OrigLoop, PSE, LI, DT, TLI, TTI, AC, ORE, 1,
                            UnrollFactor, LVL, CM) {}

private:
  Value *getBroadcastInstrs(Value *V) override;
  Value *getStepVector(Value *Val, int StartIdx, Value *Step,
                       Instruction::BinaryOps Opcode =
                       Instruction::BinaryOpsEnd) override;
  Value *reverseVector(Value *Vec) override;
};

} // end namespace llvm

/// Look for a meaningful debug location on the instruction or it's
/// operands.
static Instruction *getDebugLocFromInstOrOperands(Instruction *I) {
  if (!I)
    return I;

  DebugLoc Empty;
  if (I->getDebugLoc() != Empty)
    return I;

  for (User::op_iterator OI = I->op_begin(), OE = I->op_end(); OI != OE; ++OI) {
    if (Instruction *OpInst = dyn_cast<Instruction>(*OI))
      if (OpInst->getDebugLoc() != Empty)
        return OpInst;
  }

  return I;
}

void InnerLoopVectorizer::setDebugLocFromInst(IRBuilder<> &B, const Value *Ptr) {
  if (const Instruction *Inst = dyn_cast_or_null<Instruction>(Ptr)) {
    const DILocation *DIL = Inst->getDebugLoc();
    if (DIL && Inst->getFunction()->isDebugInfoForProfiling() &&
        !isa<DbgInfoIntrinsic>(Inst)) {
      auto NewDIL = DIL->cloneWithDuplicationFactor(UF * VF);
      if (NewDIL)
        B.SetCurrentDebugLocation(NewDIL.getValue());
      else
        LLVM_DEBUG(dbgs()
                   << "Failed to create new discriminator: "
                   << DIL->getFilename() << " Line: " << DIL->getLine());
    }
    else
      B.SetCurrentDebugLocation(DIL);
  } else
    B.SetCurrentDebugLocation(DebugLoc());
}

#ifndef NDEBUG
/// \return string containing a file name and a line # for the given loop.
static std::string getDebugLocString(const Loop *L) {
  std::string Result;
  if (L) {
    raw_string_ostream OS(Result);
    if (const DebugLoc LoopDbgLoc = L->getStartLoc())
      LoopDbgLoc.print(OS);
    else
      // Just print the module name.
      OS << L->getHeader()->getParent()->getParent()->getModuleIdentifier();
    OS.flush();
  }
  return Result;
}
#endif

void InnerLoopVectorizer::addNewMetadata(Instruction *To,
                                         const Instruction *Orig) {
  // If the loop was versioned with memchecks, add the corresponding no-alias
  // metadata.
  if (LVer && (isa<LoadInst>(Orig) || isa<StoreInst>(Orig)))
    LVer->annotateInstWithNoAlias(To, Orig);
}

void InnerLoopVectorizer::addMetadata(Instruction *To,
                                      Instruction *From) {
  propagateMetadata(To, From);
  addNewMetadata(To, From);
}

void InnerLoopVectorizer::addMetadata(ArrayRef<Value *> To,
                                      Instruction *From) {
  for (Value *V : To) {
    if (Instruction *I = dyn_cast<Instruction>(V))
      addMetadata(I, From);
  }
}

namespace llvm {

/// LoopVectorizationCostModel - estimates the expected speedups due to
/// vectorization.
/// In many cases vectorization is not profitable. This can happen because of
/// a number of reasons. In this class we mainly attempt to predict the
/// expected speedup/slowdowns due to the supported instruction set. We use the
/// TargetTransformInfo to query the different backends for the cost of
/// different operations.
class LoopVectorizationCostModel {
public:
  LoopVectorizationCostModel(Loop *L, PredicatedScalarEvolution &PSE,
                             LoopInfo *LI, LoopVectorizationLegality *Legal,
                             const TargetTransformInfo &TTI,
                             const TargetLibraryInfo *TLI, DemandedBits *DB,
                             AssumptionCache *AC,
                             OptimizationRemarkEmitter *ORE, const Function *F,
                             const LoopVectorizeHints *Hints,
                             InterleavedAccessInfo &IAI)
      : TheLoop(L), PSE(PSE), LI(LI), Legal(Legal), TTI(TTI), TLI(TLI), DB(DB),
    AC(AC), ORE(ORE), TheFunction(F), Hints(Hints), InterleaveInfo(IAI) {}

  /// \return An upper bound for the vectorization factor, or None if
  /// vectorization should be avoided up front.
  Optional<unsigned> computeMaxVF(bool OptForSize);

  /// \return The most profitable vectorization factor and the cost of that VF.
  /// This method checks every power of two up to MaxVF. If UserVF is not ZERO
  /// then this vectorization factor will be selected if vectorization is
  /// possible.
  VectorizationFactor selectVectorizationFactor(unsigned MaxVF);

  /// Setup cost-based decisions for user vectorization factor.
  void selectUserVectorizationFactor(unsigned UserVF) {
    collectUniformsAndScalars(UserVF);
    collectInstsToScalarize(UserVF);
  }

  /// \return The size (in bits) of the smallest and widest types in the code
  /// that needs to be vectorized. We ignore values that remain scalar such as
  /// 64 bit loop indices.
  std::pair<unsigned, unsigned> getSmallestAndWidestTypes();

  /// \return The desired interleave count.
  /// If interleave count has been specified by metadata it will be returned.
  /// Otherwise, the interleave count is computed and returned. VF and LoopCost
  /// are the selected vectorization factor and the cost of the selected VF.
  unsigned selectInterleaveCount(bool OptForSize, unsigned VF,
                                 unsigned LoopCost);

  /// Memory access instruction may be vectorized in more than one way.
  /// Form of instruction after vectorization depends on cost.
  /// This function takes cost-based decisions for Load/Store instructions
  /// and collects them in a map. This decisions map is used for building
  /// the lists of loop-uniform and loop-scalar instructions.
  /// The calculated cost is saved with widening decision in order to
  /// avoid redundant calculations.
  void setCostBasedWideningDecision(unsigned VF);

  /// A struct that represents some properties of the register usage
  /// of a loop.
  struct RegisterUsage {
    /// Holds the number of loop invariant values that are used in the loop.
    unsigned LoopInvariantRegs;

    /// Holds the maximum number of concurrent live intervals in the loop.
    unsigned MaxLocalUsers;
  };

  /// \return Returns information about the register usages of the loop for the
  /// given vectorization factors.
  SmallVector<RegisterUsage, 8> calculateRegisterUsage(ArrayRef<unsigned> VFs);

  /// Collect values we want to ignore in the cost model.
  void collectValuesToIgnore();

  /// \returns The smallest bitwidth each instruction can be represented with.
  /// The vector equivalents of these instructions should be truncated to this
  /// type.
  const MapVector<Instruction *, uint64_t> &getMinimalBitwidths() const {
    return MinBWs;
  }

  /// \returns True if it is more profitable to scalarize instruction \p I for
  /// vectorization factor \p VF.
  bool isProfitableToScalarize(Instruction *I, unsigned VF) const {
    assert(VF > 1 && "Profitable to scalarize relevant only for VF > 1.");

    // Cost model is not run in the VPlan-native path - return conservative
    // result until this changes.
    if (EnableVPlanNativePath)
      return false;

    auto Scalars = InstsToScalarize.find(VF);
    assert(Scalars != InstsToScalarize.end() &&
           "VF not yet analyzed for scalarization profitability");
    return Scalars->second.find(I) != Scalars->second.end();
  }

  /// Returns true if \p I is known to be uniform after vectorization.
  bool isUniformAfterVectorization(Instruction *I, unsigned VF) const {
    if (VF == 1)
      return true;

    // Cost model is not run in the VPlan-native path - return conservative
    // result until this changes.
    if (EnableVPlanNativePath)
      return false;

    auto UniformsPerVF = Uniforms.find(VF);
    assert(UniformsPerVF != Uniforms.end() &&
           "VF not yet analyzed for uniformity");
    return UniformsPerVF->second.find(I) != UniformsPerVF->second.end();
  }

  /// Returns true if \p I is known to be scalar after vectorization.
  bool isScalarAfterVectorization(Instruction *I, unsigned VF) const {
    if (VF == 1)
      return true;

    // Cost model is not run in the VPlan-native path - return conservative
    // result until this changes.
    if (EnableVPlanNativePath)
      return false;

    auto ScalarsPerVF = Scalars.find(VF);
    assert(ScalarsPerVF != Scalars.end() &&
           "Scalar values are not calculated for VF");
    return ScalarsPerVF->second.find(I) != ScalarsPerVF->second.end();
  }

  /// \returns True if instruction \p I can be truncated to a smaller bitwidth
  /// for vectorization factor \p VF.
  bool canTruncateToMinimalBitwidth(Instruction *I, unsigned VF) const {
    return VF > 1 && MinBWs.find(I) != MinBWs.end() &&
           !isProfitableToScalarize(I, VF) &&
           !isScalarAfterVectorization(I, VF);
  }

  /// Decision that was taken during cost calculation for memory instruction.
  enum InstWidening {
    CM_Unknown,
    CM_Widen,         // For consecutive accesses with stride +1.
    CM_Widen_Reverse, // For consecutive accesses with stride -1.
    CM_Interleave,
    CM_GatherScatter,
    CM_Scalarize
  };

  /// Save vectorization decision \p W and \p Cost taken by the cost model for
  /// instruction \p I and vector width \p VF.
  void setWideningDecision(Instruction *I, unsigned VF, InstWidening W,
                           unsigned Cost) {
    assert(VF >= 2 && "Expected VF >=2");
    WideningDecisions[std::make_pair(I, VF)] = std::make_pair(W, Cost);
  }

  /// Save vectorization decision \p W and \p Cost taken by the cost model for
  /// interleaving group \p Grp and vector width \p VF.
  void setWideningDecision(const InterleaveGroup<Instruction> *Grp, unsigned VF,
                           InstWidening W, unsigned Cost) {
    assert(VF >= 2 && "Expected VF >=2");
    /// Broadcast this decicion to all instructions inside the group.
    /// But the cost will be assigned to one instruction only.
    for (unsigned i = 0; i < Grp->getFactor(); ++i) {
      if (auto *I = Grp->getMember(i)) {
        if (Grp->getInsertPos() == I)
          WideningDecisions[std::make_pair(I, VF)] = std::make_pair(W, Cost);
        else
          WideningDecisions[std::make_pair(I, VF)] = std::make_pair(W, 0);
      }
    }
  }

  /// Return the cost model decision for the given instruction \p I and vector
  /// width \p VF. Return CM_Unknown if this instruction did not pass
  /// through the cost modeling.
  InstWidening getWideningDecision(Instruction *I, unsigned VF) {
    assert(VF >= 2 && "Expected VF >=2");

    // Cost model is not run in the VPlan-native path - return conservative
    // result until this changes.
    if (EnableVPlanNativePath)
      return CM_GatherScatter;

    std::pair<Instruction *, unsigned> InstOnVF = std::make_pair(I, VF);
    auto Itr = WideningDecisions.find(InstOnVF);
    if (Itr == WideningDecisions.end())
      return CM_Unknown;
    return Itr->second.first;
  }

  /// Return the vectorization cost for the given instruction \p I and vector
  /// width \p VF.
  unsigned getWideningCost(Instruction *I, unsigned VF) {
    assert(VF >= 2 && "Expected VF >=2");
    std::pair<Instruction *, unsigned> InstOnVF = std::make_pair(I, VF);
    assert(WideningDecisions.find(InstOnVF) != WideningDecisions.end() &&
           "The cost is not calculated");
    return WideningDecisions[InstOnVF].second;
  }

  /// Return True if instruction \p I is an optimizable truncate whose operand
  /// is an induction variable. Such a truncate will be removed by adding a new
  /// induction variable with the destination type.
  bool isOptimizableIVTruncate(Instruction *I, unsigned VF) {
    // If the instruction is not a truncate, return false.
    auto *Trunc = dyn_cast<TruncInst>(I);
    if (!Trunc)
      return false;

    // Get the source and destination types of the truncate.
    Type *SrcTy = ToVectorTy(cast<CastInst>(I)->getSrcTy(), VF);
    Type *DestTy = ToVectorTy(cast<CastInst>(I)->getDestTy(), VF);

    // If the truncate is free for the given types, return false. Replacing a
    // free truncate with an induction variable would add an induction variable
    // update instruction to each iteration of the loop. We exclude from this
    // check the primary induction variable since it will need an update
    // instruction regardless.
    Value *Op = Trunc->getOperand(0);
    if (Op != Legal->getPrimaryInduction() && TTI.isTruncateFree(SrcTy, DestTy))
      return false;

    // If the truncated value is not an induction variable, return false.
    return Legal->isInductionPhi(Op);
  }

  /// Collects the instructions to scalarize for each predicated instruction in
  /// the loop.
  void collectInstsToScalarize(unsigned VF);

  /// Collect Uniform and Scalar values for the given \p VF.
  /// The sets depend on CM decision for Load/Store instructions
  /// that may be vectorized as interleave, gather-scatter or scalarized.
  void collectUniformsAndScalars(unsigned VF) {
    // Do the analysis once.
    if (VF == 1 || Uniforms.find(VF) != Uniforms.end())
      return;
    setCostBasedWideningDecision(VF);
    collectLoopUniforms(VF);
    collectLoopScalars(VF);
  }

  /// Returns true if the target machine supports masked store operation
  /// for the given \p DataType and kind of access to \p Ptr.
  bool isLegalMaskedStore(Type *DataType, Value *Ptr) {
    return Legal->isConsecutivePtr(Ptr) && TTI.isLegalMaskedStore(DataType);
  }

  /// Returns true if the target machine supports masked load operation
  /// for the given \p DataType and kind of access to \p Ptr.
  bool isLegalMaskedLoad(Type *DataType, Value *Ptr) {
    return Legal->isConsecutivePtr(Ptr) && TTI.isLegalMaskedLoad(DataType);
  }

  /// Returns true if the target machine supports masked scatter operation
  /// for the given \p DataType.
  bool isLegalMaskedScatter(Type *DataType) {
    return TTI.isLegalMaskedScatter(DataType);
  }

  /// Returns true if the target machine supports masked gather operation
  /// for the given \p DataType.
  bool isLegalMaskedGather(Type *DataType) {
    return TTI.isLegalMaskedGather(DataType);
  }

  /// Returns true if the target machine can represent \p V as a masked gather
  /// or scatter operation.
  bool isLegalGatherOrScatter(Value *V) {
    bool LI = isa<LoadInst>(V);
    bool SI = isa<StoreInst>(V);
    if (!LI && !SI)
      return false;
    auto *Ty = getMemInstValueType(V);
    return (LI && isLegalMaskedGather(Ty)) || (SI && isLegalMaskedScatter(Ty));
  }

  /// Returns true if \p I is an instruction that will be scalarized with
  /// predication. Such instructions include conditional stores and
  /// instructions that may divide by zero.
  /// If a non-zero VF has been calculated, we check if I will be scalarized
  /// predication for that VF.
  bool isScalarWithPredication(Instruction *I, unsigned VF = 1);

  // Returns true if \p I is an instruction that will be predicated either
  // through scalar predication or masked load/store or masked gather/scatter.
  // Superset of instructions that return true for isScalarWithPredication.
  bool isPredicatedInst(Instruction *I) {
    if (!blockNeedsPredication(I->getParent()))
      return false;
    // Loads and stores that need some form of masked operation are predicated
    // instructions.
    if (isa<LoadInst>(I) || isa<StoreInst>(I))
      return Legal->isMaskRequired(I);
    return isScalarWithPredication(I);
  }

  /// Returns true if \p I is a memory instruction with consecutive memory
  /// access that can be widened.
  bool memoryInstructionCanBeWidened(Instruction *I, unsigned VF = 1);

  /// Returns true if \p I is a memory instruction in an interleaved-group
  /// of memory accesses that can be vectorized with wide vector loads/stores
  /// and shuffles.
  bool interleavedAccessCanBeWidened(Instruction *I, unsigned VF = 1);

  /// Check if \p Instr belongs to any interleaved access group.
  bool isAccessInterleaved(Instruction *Instr) {
    return InterleaveInfo.isInterleaved(Instr);
  }

  /// Get the interleaved access group that \p Instr belongs to.
  const InterleaveGroup<Instruction> *
  getInterleavedAccessGroup(Instruction *Instr) {
    return InterleaveInfo.getInterleaveGroup(Instr);
  }

  /// Returns true if an interleaved group requires a scalar iteration
  /// to handle accesses with gaps, and there is nothing preventing us from
  /// creating a scalar epilogue.
  bool requiresScalarEpilogue() const {
    return IsScalarEpilogueAllowed && InterleaveInfo.requiresScalarEpilogue();
  }

  /// Returns true if a scalar epilogue is not allowed due to optsize.
  bool isScalarEpilogueAllowed() const { return IsScalarEpilogueAllowed; }

  /// Returns true if all loop blocks should be masked to fold tail loop.
  bool foldTailByMasking() const { return FoldTailByMasking; }

  bool blockNeedsPredication(BasicBlock *BB) {
    return foldTailByMasking() || Legal->blockNeedsPredication(BB);
  }

private:
  unsigned NumPredStores = 0;

  /// \return An upper bound for the vectorization factor, larger than zero.
  /// One is returned if vectorization should best be avoided due to cost.
  unsigned computeFeasibleMaxVF(bool OptForSize, unsigned ConstTripCount);

  /// The vectorization cost is a combination of the cost itself and a boolean
  /// indicating whether any of the contributing operations will actually
  /// operate on
  /// vector values after type legalization in the backend. If this latter value
  /// is
  /// false, then all operations will be scalarized (i.e. no vectorization has
  /// actually taken place).
  using VectorizationCostTy = std::pair<unsigned, bool>;

  /// Returns the expected execution cost. The unit of the cost does
  /// not matter because we use the 'cost' units to compare different
  /// vector widths. The cost that is returned is *not* normalized by
  /// the factor width.
  VectorizationCostTy expectedCost(unsigned VF);

  /// Returns the execution time cost of an instruction for a given vector
  /// width. Vector width of one means scalar.
  VectorizationCostTy getInstructionCost(Instruction *I, unsigned VF);

  /// The cost-computation logic from getInstructionCost which provides
  /// the vector type as an output parameter.
  unsigned getInstructionCost(Instruction *I, unsigned VF, Type *&VectorTy);

  /// Calculate vectorization cost of memory instruction \p I.
  unsigned getMemoryInstructionCost(Instruction *I, unsigned VF);

  /// The cost computation for scalarized memory instruction.
  unsigned getMemInstScalarizationCost(Instruction *I, unsigned VF);

  /// The cost computation for interleaving group of memory instructions.
  unsigned getInterleaveGroupCost(Instruction *I, unsigned VF);

  /// The cost computation for Gather/Scatter instruction.
  unsigned getGatherScatterCost(Instruction *I, unsigned VF);

  /// The cost computation for widening instruction \p I with consecutive
  /// memory access.
  unsigned getConsecutiveMemOpCost(Instruction *I, unsigned VF);

  /// The cost calculation for Load/Store instruction \p I with uniform pointer -
  /// Load: scalar load + broadcast.
  /// Store: scalar store + (loop invariant value stored? 0 : extract of last
  /// element)
  unsigned getUniformMemOpCost(Instruction *I, unsigned VF);

  /// Returns whether the instruction is a load or store and will be a emitted
  /// as a vector operation.
  bool isConsecutiveLoadOrStore(Instruction *I);

  /// Returns true if an artificially high cost for emulated masked memrefs
  /// should be used.
  bool useEmulatedMaskMemRefHack(Instruction *I);

  /// Create an analysis remark that explains why vectorization failed
  ///
  /// \p RemarkName is the identifier for the remark.  \return the remark object
  /// that can be streamed to.
  OptimizationRemarkAnalysis createMissedAnalysis(StringRef RemarkName) {
    return createLVMissedAnalysis(Hints->vectorizeAnalysisPassName(),
                                  RemarkName, TheLoop);
  }

  /// Map of scalar integer values to the smallest bitwidth they can be legally
  /// represented as. The vector equivalents of these values should be truncated
  /// to this type.
  MapVector<Instruction *, uint64_t> MinBWs;

  /// A type representing the costs for instructions if they were to be
  /// scalarized rather than vectorized. The entries are Instruction-Cost
  /// pairs.
  using ScalarCostsTy = DenseMap<Instruction *, unsigned>;

  /// A set containing all BasicBlocks that are known to present after
  /// vectorization as a predicated block.
  SmallPtrSet<BasicBlock *, 4> PredicatedBBsAfterVectorization;

  /// Records whether it is allowed to have the original scalar loop execute at
  /// least once. This may be needed as a fallback loop in case runtime 
  /// aliasing/dependence checks fail, or to handle the tail/remainder
  /// iterations when the trip count is unknown or doesn't divide by the VF,
  /// or as a peel-loop to handle gaps in interleave-groups.
  /// Under optsize and when the trip count is very small we don't allow any
  /// iterations to execute in the scalar loop.
  bool IsScalarEpilogueAllowed = true;

  /// All blocks of loop are to be masked to fold tail of scalar iterations.
  bool FoldTailByMasking = false;

  /// A map holding scalar costs for different vectorization factors. The
  /// presence of a cost for an instruction in the mapping indicates that the
  /// instruction will be scalarized when vectorizing with the associated
  /// vectorization factor. The entries are VF-ScalarCostTy pairs.
  DenseMap<unsigned, ScalarCostsTy> InstsToScalarize;

  /// Holds the instructions known to be uniform after vectorization.
  /// The data is collected per VF.
  DenseMap<unsigned, SmallPtrSet<Instruction *, 4>> Uniforms;

  /// Holds the instructions known to be scalar after vectorization.
  /// The data is collected per VF.
  DenseMap<unsigned, SmallPtrSet<Instruction *, 4>> Scalars;

  /// Holds the instructions (address computations) that are forced to be
  /// scalarized.
  DenseMap<unsigned, SmallPtrSet<Instruction *, 4>> ForcedScalars;

  /// Returns the expected difference in cost from scalarizing the expression
  /// feeding a predicated instruction \p PredInst. The instructions to
  /// scalarize and their scalar costs are collected in \p ScalarCosts. A
  /// non-negative return value implies the expression will be scalarized.
  /// Currently, only single-use chains are considered for scalarization.
  int computePredInstDiscount(Instruction *PredInst, ScalarCostsTy &ScalarCosts,
                              unsigned VF);

  /// Collect the instructions that are uniform after vectorization. An
  /// instruction is uniform if we represent it with a single scalar value in
  /// the vectorized loop corresponding to each vector iteration. Examples of
  /// uniform instructions include pointer operands of consecutive or
  /// interleaved memory accesses. Note that although uniformity implies an
  /// instruction will be scalar, the reverse is not true. In general, a
  /// scalarized instruction will be represented by VF scalar values in the
  /// vectorized loop, each corresponding to an iteration of the original
  /// scalar loop.
  void collectLoopUniforms(unsigned VF);

  /// Collect the instructions that are scalar after vectorization. An
  /// instruction is scalar if it is known to be uniform or will be scalarized
  /// during vectorization. Non-uniform scalarized instructions will be
  /// represented by VF values in the vectorized loop, each corresponding to an
  /// iteration of the original scalar loop.
  void collectLoopScalars(unsigned VF);

  /// Keeps cost model vectorization decision and cost for instructions.
  /// Right now it is used for memory instructions only.
  using DecisionList = DenseMap<std::pair<Instruction *, unsigned>,
                                std::pair<InstWidening, unsigned>>;

  DecisionList WideningDecisions;

public:
  /// The loop that we evaluate.
  Loop *TheLoop;

  /// Predicated scalar evolution analysis.
  PredicatedScalarEvolution &PSE;

  /// Loop Info analysis.
  LoopInfo *LI;

  /// Vectorization legality.
  LoopVectorizationLegality *Legal;

  /// Vector target information.
  const TargetTransformInfo &TTI;

  /// Target Library Info.
  const TargetLibraryInfo *TLI;

  /// Demanded bits analysis.
  DemandedBits *DB;

  /// Assumption cache.
  AssumptionCache *AC;

  /// Interface to emit optimization remarks.
  OptimizationRemarkEmitter *ORE;

  const Function *TheFunction;

  /// Loop Vectorize Hint.
  const LoopVectorizeHints *Hints;

  /// The interleave access information contains groups of interleaved accesses
  /// with the same stride and close to each other.
  InterleavedAccessInfo &InterleaveInfo;

  /// Values to ignore in the cost model.
  SmallPtrSet<const Value *, 16> ValuesToIgnore;

  /// Values to ignore in the cost model when VF > 1.
  SmallPtrSet<const Value *, 16> VecValuesToIgnore;
};

} // end namespace llvm

// Return true if \p OuterLp is an outer loop annotated with hints for explicit
// vectorization. The loop needs to be annotated with #pragma omp simd
// simdlen(#) or #pragma clang vectorize(enable) vectorize_width(#). If the
// vector length information is not provided, vectorization is not considered
// explicit. Interleave hints are not allowed either. These limitations will be
// relaxed in the future.
// Please, note that we are currently forced to abuse the pragma 'clang
// vectorize' semantics. This pragma provides *auto-vectorization hints*
// (i.e., LV must check that vectorization is legal) whereas pragma 'omp simd'
// provides *explicit vectorization hints* (LV can bypass legal checks and
// assume that vectorization is legal). However, both hints are implemented
// using the same metadata (llvm.loop.vectorize, processed by
// LoopVectorizeHints). This will be fixed in the future when the native IR
// representation for pragma 'omp simd' is introduced.
static bool isExplicitVecOuterLoop(Loop *OuterLp,
                                   OptimizationRemarkEmitter *ORE) {
  assert(!OuterLp->empty() && "This is not an outer loop");
  LoopVectorizeHints Hints(OuterLp, true /*DisableInterleaving*/, *ORE);

  // Only outer loops with an explicit vectorization hint are supported.
  // Unannotated outer loops are ignored.
  if (Hints.getForce() == LoopVectorizeHints::FK_Undefined)
    return false;

  Function *Fn = OuterLp->getHeader()->getParent();
  if (!Hints.allowVectorization(Fn, OuterLp,
                                true /*VectorizeOnlyWhenForced*/)) {
    LLVM_DEBUG(dbgs() << "LV: Loop hints prevent outer loop vectorization.\n");
    return false;
  }

  if (!Hints.getWidth()) {
    LLVM_DEBUG(dbgs() << "LV: Not vectorizing: No user vector width.\n");
    Hints.emitRemarkWithHints();
    return false;
  }

  if (Hints.getInterleave() > 1) {
    // TODO: Interleave support is future work.
    LLVM_DEBUG(dbgs() << "LV: Not vectorizing: Interleave is not supported for "
                         "outer loops.\n");
    Hints.emitRemarkWithHints();
    return false;
  }

  return true;
}

static void collectSupportedLoops(Loop &L, LoopInfo *LI,
                                  OptimizationRemarkEmitter *ORE,
                                  SmallVectorImpl<Loop *> &V) {
  // Collect inner loops and outer loops without irreducible control flow. For
  // now, only collect outer loops that have explicit vectorization hints. If we
  // are stress testing the VPlan H-CFG construction, we collect the outermost
  // loop of every loop nest.
  if (L.empty() || VPlanBuildStressTest ||
      (EnableVPlanNativePath && isExplicitVecOuterLoop(&L, ORE))) {
    LoopBlocksRPO RPOT(&L);
    RPOT.perform(LI);
    if (!containsIrreducibleCFG<const BasicBlock *>(RPOT, *LI)) {
      V.push_back(&L);
      // TODO: Collect inner loops inside marked outer loops in case
      // vectorization fails for the outer loop. Do not invoke
      // 'containsIrreducibleCFG' again for inner loops when the outer loop is
      // already known to be reducible. We can use an inherited attribute for
      // that.
      return;
    }
  }
  for (Loop *InnerL : L)
    collectSupportedLoops(*InnerL, LI, ORE, V);
}

namespace {

/// The LoopVectorize Pass.
struct LoopVectorize : public FunctionPass {
  /// Pass identification, replacement for typeid
  static char ID;

  LoopVectorizePass Impl;

  explicit LoopVectorize(bool InterleaveOnlyWhenForced = false,
                         bool VectorizeOnlyWhenForced = false)
      : FunctionPass(ID) {
    Impl.InterleaveOnlyWhenForced = InterleaveOnlyWhenForced;
    Impl.VectorizeOnlyWhenForced = VectorizeOnlyWhenForced;
    initializeLoopVectorizePass(*PassRegistry::getPassRegistry());
  }

  bool runOnFunction(Function &F) override {
    if (skipFunction(F))
      return false;

    auto *SE = &getAnalysis<ScalarEvolutionWrapperPass>().getSE();
    auto *LI = &getAnalysis<LoopInfoWrapperPass>().getLoopInfo();
    auto *TTI = &getAnalysis<TargetTransformInfoWrapperPass>().getTTI(F);
    auto *DT = &getAnalysis<DominatorTreeWrapperPass>().getDomTree();
    auto *BFI = &getAnalysis<BlockFrequencyInfoWrapperPass>().getBFI();
    auto *TLIP = getAnalysisIfAvailable<TargetLibraryInfoWrapperPass>();
    auto *TLI = TLIP ? &TLIP->getTLI() : nullptr;
    auto *AA = &getAnalysis<AAResultsWrapperPass>().getAAResults();
    auto *AC = &getAnalysis<AssumptionCacheTracker>().getAssumptionCache(F);
    auto *LAA = &getAnalysis<LoopAccessLegacyAnalysis>();
    auto *DB = &getAnalysis<DemandedBitsWrapperPass>().getDemandedBits();
    auto *ORE = &getAnalysis<OptimizationRemarkEmitterWrapperPass>().getORE();

    std::function<const LoopAccessInfo &(Loop &)> GetLAA =
        [&](Loop &L) -> const LoopAccessInfo & { return LAA->getInfo(&L); };

    return Impl.runImpl(F, *SE, *LI, *TTI, *DT, *BFI, TLI, *DB, *AA, *AC,
                        GetLAA, *ORE);
  }

  void getAnalysisUsage(AnalysisUsage &AU) const override {
    AU.addRequired<AssumptionCacheTracker>();
    AU.addRequired<BlockFrequencyInfoWrapperPass>();
    AU.addRequired<DominatorTreeWrapperPass>();
    AU.addRequired<LoopInfoWrapperPass>();
    AU.addRequired<ScalarEvolutionWrapperPass>();
    AU.addRequired<TargetTransformInfoWrapperPass>();
    AU.addRequired<AAResultsWrapperPass>();
    AU.addRequired<LoopAccessLegacyAnalysis>();
    AU.addRequired<DemandedBitsWrapperPass>();
    AU.addRequired<OptimizationRemarkEmitterWrapperPass>();

    // We currently do not preserve loopinfo/dominator analyses with outer loop
    // vectorization. Until this is addressed, mark these analyses as preserved
    // only for non-VPlan-native path.
    // TODO: Preserve Loop and Dominator analyses for VPlan-native path.
    if (!EnableVPlanNativePath) {
      AU.addPreserved<LoopInfoWrapperPass>();
      AU.addPreserved<DominatorTreeWrapperPass>();
    }

    AU.addPreserved<BasicAAWrapperPass>();
    AU.addPreserved<GlobalsAAWrapperPass>();
  }
};

} // end anonymous namespace

//===----------------------------------------------------------------------===//
// Implementation of LoopVectorizationLegality, InnerLoopVectorizer and
// LoopVectorizationCostModel and LoopVectorizationPlanner.
//===----------------------------------------------------------------------===//

Value *InnerLoopVectorizer::getBroadcastInstrs(Value *V) {
  // We need to place the broadcast of invariant variables outside the loop,
  // but only if it's proven safe to do so. Else, broadcast will be inside
  // vector loop body.
  Instruction *Instr = dyn_cast<Instruction>(V);
  bool SafeToHoist = OrigLoop->isLoopInvariant(V) &&
                     (!Instr ||
                      DT->dominates(Instr->getParent(), LoopVectorPreHeader));
  // Place the code for broadcasting invariant variables in the new preheader.
  IRBuilder<>::InsertPointGuard Guard(Builder);
  if (SafeToHoist)
    Builder.SetInsertPoint(LoopVectorPreHeader->getTerminator());

  // Broadcast the scalar into all locations in the vector.
  Value *Shuf = Builder.CreateVectorSplat(VF, V, "broadcast");

  return Shuf;
}

void InnerLoopVectorizer::createVectorIntOrFpInductionPHI(
    const InductionDescriptor &II, Value *Step, Instruction *EntryVal) {
  assert((isa<PHINode>(EntryVal) || isa<TruncInst>(EntryVal)) &&
         "Expected either an induction phi-node or a truncate of it!");
  Value *Start = II.getStartValue();

  // Construct the initial value of the vector IV in the vector loop preheader
  auto CurrIP = Builder.saveIP();
  Builder.SetInsertPoint(LoopVectorPreHeader->getTerminator());
  if (isa<TruncInst>(EntryVal)) {
    assert(Start->getType()->isIntegerTy() &&
           "Truncation requires an integer type");
    auto *TruncType = cast<IntegerType>(EntryVal->getType());
    Step = Builder.CreateTrunc(Step, TruncType);
    Start = Builder.CreateCast(Instruction::Trunc, Start, TruncType);
  }
  Value *SplatStart = Builder.CreateVectorSplat(VF, Start);
  Value *SteppedStart =
      getStepVector(SplatStart, 0, Step, II.getInductionOpcode());

  // We create vector phi nodes for both integer and floating-point induction
  // variables. Here, we determine the kind of arithmetic we will perform.
  Instruction::BinaryOps AddOp;
  Instruction::BinaryOps MulOp;
  if (Step->getType()->isIntegerTy()) {
    AddOp = Instruction::Add;
    MulOp = Instruction::Mul;
  } else {
    AddOp = II.getInductionOpcode();
    MulOp = Instruction::FMul;
  }

  // Multiply the vectorization factor by the step using integer or
  // floating-point arithmetic as appropriate.
  Value *ConstVF = getSignedIntOrFpConstant(Step->getType(), VF);
  Value *Mul = addFastMathFlag(Builder.CreateBinOp(MulOp, Step, ConstVF));

  // Create a vector splat to use in the induction update.
  //
  // FIXME: If the step is non-constant, we create the vector splat with
  //        IRBuilder. IRBuilder can constant-fold the multiply, but it doesn't
  //        handle a constant vector splat.
  Value *SplatVF = isa<Constant>(Mul)
                       ? ConstantVector::getSplat(VF, cast<Constant>(Mul))
                       : Builder.CreateVectorSplat(VF, Mul);
  Builder.restoreIP(CurrIP);

  // We may need to add the step a number of times, depending on the unroll
  // factor. The last of those goes into the PHI.
  PHINode *VecInd = PHINode::Create(SteppedStart->getType(), 2, "vec.ind",
                                    &*LoopVectorBody->getFirstInsertionPt());
  VecInd->setDebugLoc(EntryVal->getDebugLoc());
  Instruction *LastInduction = VecInd;
  for (unsigned Part = 0; Part < UF; ++Part) {
    VectorLoopValueMap.setVectorValue(EntryVal, Part, LastInduction);

    if (isa<TruncInst>(EntryVal))
      addMetadata(LastInduction, EntryVal);
    recordVectorLoopValueForInductionCast(II, EntryVal, LastInduction, Part);

    LastInduction = cast<Instruction>(addFastMathFlag(
        Builder.CreateBinOp(AddOp, LastInduction, SplatVF, "step.add")));
    LastInduction->setDebugLoc(EntryVal->getDebugLoc());
  }

  // Move the last step to the end of the latch block. This ensures consistent
  // placement of all induction updates.
  auto *LoopVectorLatch = LI->getLoopFor(LoopVectorBody)->getLoopLatch();
  auto *Br = cast<BranchInst>(LoopVectorLatch->getTerminator());
  auto *ICmp = cast<Instruction>(Br->getCondition());
  LastInduction->moveBefore(ICmp);
  LastInduction->setName("vec.ind.next");

  VecInd->addIncoming(SteppedStart, LoopVectorPreHeader);
  VecInd->addIncoming(LastInduction, LoopVectorLatch);
}

bool InnerLoopVectorizer::shouldScalarizeInstruction(Instruction *I) const {
  return Cost->isScalarAfterVectorization(I, VF) ||
         Cost->isProfitableToScalarize(I, VF);
}

bool InnerLoopVectorizer::needsScalarInduction(Instruction *IV) const {
  if (shouldScalarizeInstruction(IV))
    return true;
  auto isScalarInst = [&](User *U) -> bool {
    auto *I = cast<Instruction>(U);
    return (OrigLoop->contains(I) && shouldScalarizeInstruction(I));
  };
  return llvm::any_of(IV->users(), isScalarInst);
}

void InnerLoopVectorizer::recordVectorLoopValueForInductionCast(
    const InductionDescriptor &ID, const Instruction *EntryVal,
    Value *VectorLoopVal, unsigned Part, unsigned Lane) {
  assert((isa<PHINode>(EntryVal) || isa<TruncInst>(EntryVal)) &&
         "Expected either an induction phi-node or a truncate of it!");

  // This induction variable is not the phi from the original loop but the
  // newly-created IV based on the proof that casted Phi is equal to the
  // uncasted Phi in the vectorized loop (under a runtime guard possibly). It
  // re-uses the same InductionDescriptor that original IV uses but we don't
  // have to do any recording in this case - that is done when original IV is
  // processed.
  if (isa<TruncInst>(EntryVal))
    return;

  const SmallVectorImpl<Instruction *> &Casts = ID.getCastInsts();
  if (Casts.empty())
    return;
  // Only the first Cast instruction in the Casts vector is of interest.
  // The rest of the Casts (if exist) have no uses outside the
  // induction update chain itself.
  Instruction *CastInst = *Casts.begin();
  if (Lane < UINT_MAX)
    VectorLoopValueMap.setScalarValue(CastInst, {Part, Lane}, VectorLoopVal);
  else
    VectorLoopValueMap.setVectorValue(CastInst, Part, VectorLoopVal);
}

void InnerLoopVectorizer::widenIntOrFpInduction(PHINode *IV, TruncInst *Trunc) {
  assert((IV->getType()->isIntegerTy() || IV != OldInduction) &&
         "Primary induction variable must have an integer type");

  auto II = Legal->getInductionVars()->find(IV);
  assert(II != Legal->getInductionVars()->end() && "IV is not an induction");

  auto ID = II->second;
  assert(IV->getType() == ID.getStartValue()->getType() && "Types must match");

  // The scalar value to broadcast. This will be derived from the canonical
  // induction variable.
  Value *ScalarIV = nullptr;

  // The value from the original loop to which we are mapping the new induction
  // variable.
  Instruction *EntryVal = Trunc ? cast<Instruction>(Trunc) : IV;

  // True if we have vectorized the induction variable.
  auto VectorizedIV = false;

  // Determine if we want a scalar version of the induction variable. This is
  // true if the induction variable itself is not widened, or if it has at
  // least one user in the loop that is not widened.
  auto NeedsScalarIV = VF > 1 && needsScalarInduction(EntryVal);

  // Generate code for the induction step. Note that induction steps are
  // required to be loop-invariant
  assert(PSE.getSE()->isLoopInvariant(ID.getStep(), OrigLoop) &&
         "Induction step should be loop invariant");
  auto &DL = OrigLoop->getHeader()->getModule()->getDataLayout();
  Value *Step = nullptr;
  if (PSE.getSE()->isSCEVable(IV->getType())) {
    SCEVExpander Exp(*PSE.getSE(), DL, "induction");
    Step = Exp.expandCodeFor(ID.getStep(), ID.getStep()->getType(),
                             LoopVectorPreHeader->getTerminator());
  } else {
    Step = cast<SCEVUnknown>(ID.getStep())->getValue();
  }

  // Try to create a new independent vector induction variable. If we can't
  // create the phi node, we will splat the scalar induction variable in each
  // loop iteration.
  if (VF > 1 && !shouldScalarizeInstruction(EntryVal)) {
    createVectorIntOrFpInductionPHI(ID, Step, EntryVal);
    VectorizedIV = true;
  }

  // If we haven't yet vectorized the induction variable, or if we will create
  // a scalar one, we need to define the scalar induction variable and step
  // values. If we were given a truncation type, truncate the canonical
  // induction variable and step. Otherwise, derive these values from the
  // induction descriptor.
  if (!VectorizedIV || NeedsScalarIV) {
    ScalarIV = Induction;
    if (IV != OldInduction) {
      ScalarIV = IV->getType()->isIntegerTy()
                     ? Builder.CreateSExtOrTrunc(Induction, IV->getType())
                     : Builder.CreateCast(Instruction::SIToFP, Induction,
                                          IV->getType());
      ScalarIV = emitTransformedIndex(Builder, ScalarIV, PSE.getSE(), DL, ID);
      ScalarIV->setName("offset.idx");
    }
    if (Trunc) {
      auto *TruncType = cast<IntegerType>(Trunc->getType());
      assert(Step->getType()->isIntegerTy() &&
             "Truncation requires an integer step");
      ScalarIV = Builder.CreateTrunc(ScalarIV, TruncType);
      Step = Builder.CreateTrunc(Step, TruncType);
    }
  }

  // If we haven't yet vectorized the induction variable, splat the scalar
  // induction variable, and build the necessary step vectors.
  // TODO: Don't do it unless the vectorized IV is really required.
  if (!VectorizedIV) {
    Value *Broadcasted = getBroadcastInstrs(ScalarIV);
    for (unsigned Part = 0; Part < UF; ++Part) {
      Value *EntryPart =
          getStepVector(Broadcasted, VF * Part, Step, ID.getInductionOpcode());
      VectorLoopValueMap.setVectorValue(EntryVal, Part, EntryPart);
      if (Trunc)
        addMetadata(EntryPart, Trunc);
      recordVectorLoopValueForInductionCast(ID, EntryVal, EntryPart, Part);
    }
  }

  // If an induction variable is only used for counting loop iterations or
  // calculating addresses, it doesn't need to be widened. Create scalar steps
  // that can be used by instructions we will later scalarize. Note that the
  // addition of the scalar steps will not increase the number of instructions
  // in the loop in the common case prior to InstCombine. We will be trading
  // one vector extract for each scalar step.
  if (NeedsScalarIV)
    buildScalarSteps(ScalarIV, Step, EntryVal, ID);
}

Value *InnerLoopVectorizer::getStepVector(Value *Val, int StartIdx, Value *Step,
                                          Instruction::BinaryOps BinOp) {
  // Create and check the types.
  assert(Val->getType()->isVectorTy() && "Must be a vector");
  int VLen = Val->getType()->getVectorNumElements();

  Type *STy = Val->getType()->getScalarType();
  assert((STy->isIntegerTy() || STy->isFloatingPointTy()) &&
         "Induction Step must be an integer or FP");
  assert(Step->getType() == STy && "Step has wrong type");

  SmallVector<Constant *, 8> Indices;

  if (STy->isIntegerTy()) {
    // Create a vector of consecutive numbers from zero to VF.
    for (int i = 0; i < VLen; ++i)
      Indices.push_back(ConstantInt::get(STy, StartIdx + i));

    // Add the consecutive indices to the vector value.
    Constant *Cv = ConstantVector::get(Indices);
    assert(Cv->getType() == Val->getType() && "Invalid consecutive vec");
    Step = Builder.CreateVectorSplat(VLen, Step);
    assert(Step->getType() == Val->getType() && "Invalid step vec");
    // FIXME: The newly created binary instructions should contain nsw/nuw flags,
    // which can be found from the original scalar operations.
    Step = Builder.CreateMul(Cv, Step);
    return Builder.CreateAdd(Val, Step, "induction");
  }

  // Floating point induction.
  assert((BinOp == Instruction::FAdd || BinOp == Instruction::FSub) &&
         "Binary Opcode should be specified for FP induction");
  // Create a vector of consecutive numbers from zero to VF.
  for (int i = 0; i < VLen; ++i)
    Indices.push_back(ConstantFP::get(STy, (double)(StartIdx + i)));

  // Add the consecutive indices to the vector value.
  Constant *Cv = ConstantVector::get(Indices);

  Step = Builder.CreateVectorSplat(VLen, Step);

  // Floating point operations had to be 'fast' to enable the induction.
  FastMathFlags Flags;
  Flags.setFast();

  Value *MulOp = Builder.CreateFMul(Cv, Step);
  if (isa<Instruction>(MulOp))
    // Have to check, MulOp may be a constant
    cast<Instruction>(MulOp)->setFastMathFlags(Flags);

  Value *BOp = Builder.CreateBinOp(BinOp, Val, MulOp, "induction");
  if (isa<Instruction>(BOp))
    cast<Instruction>(BOp)->setFastMathFlags(Flags);
  return BOp;
}

void InnerLoopVectorizer::buildScalarSteps(Value *ScalarIV, Value *Step,
                                           Instruction *EntryVal,
                                           const InductionDescriptor &ID) {
  // We shouldn't have to build scalar steps if we aren't vectorizing.
  assert(VF > 1 && "VF should be greater than one");

  // Get the value type and ensure it and the step have the same integer type.
  Type *ScalarIVTy = ScalarIV->getType()->getScalarType();
  assert(ScalarIVTy == Step->getType() &&
         "Val and Step should have the same type");

  // We build scalar steps for both integer and floating-point induction
  // variables. Here, we determine the kind of arithmetic we will perform.
  Instruction::BinaryOps AddOp;
  Instruction::BinaryOps MulOp;
  if (ScalarIVTy->isIntegerTy()) {
    AddOp = Instruction::Add;
    MulOp = Instruction::Mul;
  } else {
    AddOp = ID.getInductionOpcode();
    MulOp = Instruction::FMul;
  }

  // Determine the number of scalars we need to generate for each unroll
  // iteration. If EntryVal is uniform, we only need to generate the first
  // lane. Otherwise, we generate all VF values.
  unsigned Lanes =
      Cost->isUniformAfterVectorization(cast<Instruction>(EntryVal), VF) ? 1
                                                                         : VF;
  // Compute the scalar steps and save the results in VectorLoopValueMap.
  for (unsigned Part = 0; Part < UF; ++Part) {
    for (unsigned Lane = 0; Lane < Lanes; ++Lane) {
      auto *StartIdx = getSignedIntOrFpConstant(ScalarIVTy, VF * Part + Lane);
      auto *Mul = addFastMathFlag(Builder.CreateBinOp(MulOp, StartIdx, Step));
      auto *Add = addFastMathFlag(Builder.CreateBinOp(AddOp, ScalarIV, Mul));
      VectorLoopValueMap.setScalarValue(EntryVal, {Part, Lane}, Add);
      recordVectorLoopValueForInductionCast(ID, EntryVal, Add, Part, Lane);
    }
  }
}

Value *InnerLoopVectorizer::getOrCreateVectorValue(Value *V, unsigned Part) {
  assert(V != Induction && "The new induction variable should not be used.");
  assert(!V->getType()->isVectorTy() && "Can't widen a vector");
  assert(!V->getType()->isVoidTy() && "Type does not produce a value");

  // If we have a stride that is replaced by one, do it here. Defer this for
  // the VPlan-native path until we start running Legal checks in that path.
  if (!EnableVPlanNativePath && Legal->hasStride(V))
    V = ConstantInt::get(V->getType(), 1);

  // If we have a vector mapped to this value, return it.
  if (VectorLoopValueMap.hasVectorValue(V, Part))
    return VectorLoopValueMap.getVectorValue(V, Part);

  // If the value has not been vectorized, check if it has been scalarized
  // instead. If it has been scalarized, and we actually need the value in
  // vector form, we will construct the vector values on demand.
  if (VectorLoopValueMap.hasAnyScalarValue(V)) {
    Value *ScalarValue = VectorLoopValueMap.getScalarValue(V, {Part, 0});

    // If we've scalarized a value, that value should be an instruction.
    auto *I = cast<Instruction>(V);

    // If we aren't vectorizing, we can just copy the scalar map values over to
    // the vector map.
    if (VF == 1) {
      VectorLoopValueMap.setVectorValue(V, Part, ScalarValue);
      return ScalarValue;
    }

    // Get the last scalar instruction we generated for V and Part. If the value
    // is known to be uniform after vectorization, this corresponds to lane zero
    // of the Part unroll iteration. Otherwise, the last instruction is the one
    // we created for the last vector lane of the Part unroll iteration.
    unsigned LastLane = Cost->isUniformAfterVectorization(I, VF) ? 0 : VF - 1;
    auto *LastInst = cast<Instruction>(
        VectorLoopValueMap.getScalarValue(V, {Part, LastLane}));

    // Set the insert point after the last scalarized instruction. This ensures
    // the insertelement sequence will directly follow the scalar definitions.
    auto OldIP = Builder.saveIP();
    auto NewIP = std::next(BasicBlock::iterator(LastInst));
    Builder.SetInsertPoint(&*NewIP);

    // However, if we are vectorizing, we need to construct the vector values.
    // If the value is known to be uniform after vectorization, we can just
    // broadcast the scalar value corresponding to lane zero for each unroll
    // iteration. Otherwise, we construct the vector values using insertelement
    // instructions. Since the resulting vectors are stored in
    // VectorLoopValueMap, we will only generate the insertelements once.
    Value *VectorValue = nullptr;
    if (Cost->isUniformAfterVectorization(I, VF)) {
      VectorValue = getBroadcastInstrs(ScalarValue);
      VectorLoopValueMap.setVectorValue(V, Part, VectorValue);
    } else {
      // Initialize packing with insertelements to start from undef.
      Value *Undef = UndefValue::get(VectorType::get(V->getType(), VF));
      VectorLoopValueMap.setVectorValue(V, Part, Undef);
      for (unsigned Lane = 0; Lane < VF; ++Lane)
        packScalarIntoVectorValue(V, {Part, Lane});
      VectorValue = VectorLoopValueMap.getVectorValue(V, Part);
    }
    Builder.restoreIP(OldIP);
    return VectorValue;
  }

  // If this scalar is unknown, assume that it is a constant or that it is
  // loop invariant. Broadcast V and save the value for future uses.
  Value *B = getBroadcastInstrs(V);
  VectorLoopValueMap.setVectorValue(V, Part, B);
  return B;
}

Value *
InnerLoopVectorizer::getOrCreateScalarValue(Value *V,
                                            const VPIteration &Instance) {
  // If the value is not an instruction contained in the loop, it should
  // already be scalar.
  if (OrigLoop->isLoopInvariant(V))
    return V;

  assert(Instance.Lane > 0
             ? !Cost->isUniformAfterVectorization(cast<Instruction>(V), VF)
             : true && "Uniform values only have lane zero");

  // If the value from the original loop has not been vectorized, it is
  // represented by UF x VF scalar values in the new loop. Return the requested
  // scalar value.
  if (VectorLoopValueMap.hasScalarValue(V, Instance))
    return VectorLoopValueMap.getScalarValue(V, Instance);

  // If the value has not been scalarized, get its entry in VectorLoopValueMap
  // for the given unroll part. If this entry is not a vector type (i.e., the
  // vectorization factor is one), there is no need to generate an
  // extractelement instruction.
  auto *U = getOrCreateVectorValue(V, Instance.Part);
  if (!U->getType()->isVectorTy()) {
    assert(VF == 1 && "Value not scalarized has non-vector type");
    return U;
  }

  // Otherwise, the value from the original loop has been vectorized and is
  // represented by UF vector values. Extract and return the requested scalar
  // value from the appropriate vector lane.
  return Builder.CreateExtractElement(U, Builder.getInt32(Instance.Lane));
}

void InnerLoopVectorizer::packScalarIntoVectorValue(
    Value *V, const VPIteration &Instance) {
  assert(V != Induction && "The new induction variable should not be used.");
  assert(!V->getType()->isVectorTy() && "Can't pack a vector");
  assert(!V->getType()->isVoidTy() && "Type does not produce a value");

  Value *ScalarInst = VectorLoopValueMap.getScalarValue(V, Instance);
  Value *VectorValue = VectorLoopValueMap.getVectorValue(V, Instance.Part);
  VectorValue = Builder.CreateInsertElement(VectorValue, ScalarInst,
                                            Builder.getInt32(Instance.Lane));
  VectorLoopValueMap.resetVectorValue(V, Instance.Part, VectorValue);
}

Value *InnerLoopVectorizer::reverseVector(Value *Vec) {
  assert(Vec->getType()->isVectorTy() && "Invalid type");
  SmallVector<Constant *, 8> ShuffleMask;
  for (unsigned i = 0; i < VF; ++i)
    ShuffleMask.push_back(Builder.getInt32(VF - i - 1));

  return Builder.CreateShuffleVector(Vec, UndefValue::get(Vec->getType()),
                                     ConstantVector::get(ShuffleMask),
                                     "reverse");
}

// Return whether we allow using masked interleave-groups (for dealing with
// strided loads/stores that reside in predicated blocks, or for dealing
// with gaps).
static bool useMaskedInterleavedAccesses(const TargetTransformInfo &TTI) {
  // If an override option has been passed in for interleaved accesses, use it.
  if (EnableMaskedInterleavedMemAccesses.getNumOccurrences() > 0)
    return EnableMaskedInterleavedMemAccesses;

  return TTI.enableMaskedInterleavedAccessVectorization();
}

// Try to vectorize the interleave group that \p Instr belongs to.
//
// E.g. Translate following interleaved load group (factor = 3):
//   for (i = 0; i < N; i+=3) {
//     R = Pic[i];             // Member of index 0
//     G = Pic[i+1];           // Member of index 1
//     B = Pic[i+2];           // Member of index 2
//     ... // do something to R, G, B
//   }
// To:
//   %wide.vec = load <12 x i32>                       ; Read 4 tuples of R,G,B
//   %R.vec = shuffle %wide.vec, undef, <0, 3, 6, 9>   ; R elements
//   %G.vec = shuffle %wide.vec, undef, <1, 4, 7, 10>  ; G elements
//   %B.vec = shuffle %wide.vec, undef, <2, 5, 8, 11>  ; B elements
//
// Or translate following interleaved store group (factor = 3):
//   for (i = 0; i < N; i+=3) {
//     ... do something to R, G, B
//     Pic[i]   = R;           // Member of index 0
//     Pic[i+1] = G;           // Member of index 1
//     Pic[i+2] = B;           // Member of index 2
//   }
// To:
//   %R_G.vec = shuffle %R.vec, %G.vec, <0, 1, 2, ..., 7>
//   %B_U.vec = shuffle %B.vec, undef, <0, 1, 2, 3, u, u, u, u>
//   %interleaved.vec = shuffle %R_G.vec, %B_U.vec,
//        <0, 4, 8, 1, 5, 9, 2, 6, 10, 3, 7, 11>    ; Interleave R,G,B elements
//   store <12 x i32> %interleaved.vec              ; Write 4 tuples of R,G,B
void InnerLoopVectorizer::vectorizeInterleaveGroup(Instruction *Instr,
                                                   VectorParts *BlockInMask) {
  const InterleaveGroup<Instruction> *Group =
      Cost->getInterleavedAccessGroup(Instr);
  assert(Group && "Fail to get an interleaved access group.");

  // Skip if current instruction is not the insert position.
  if (Instr != Group->getInsertPos())
    return;

  const DataLayout &DL = Instr->getModule()->getDataLayout();
  Value *Ptr = getLoadStorePointerOperand(Instr);

  // Prepare for the vector type of the interleaved load/store.
  Type *ScalarTy = getMemInstValueType(Instr);
  unsigned InterleaveFactor = Group->getFactor();
  Type *VecTy = VectorType::get(ScalarTy, InterleaveFactor * VF);
  Type *PtrTy = VecTy->getPointerTo(getLoadStoreAddressSpace(Instr));

  // Prepare for the new pointers.
  setDebugLocFromInst(Builder, Ptr);
  SmallVector<Value *, 2> NewPtrs;
  unsigned Index = Group->getIndex(Instr);

  VectorParts Mask;
  bool IsMaskForCondRequired = BlockInMask;
  if (IsMaskForCondRequired) {
    Mask = *BlockInMask;
    // TODO: extend the masked interleaved-group support to reversed access.
    assert(!Group->isReverse() && "Reversed masked interleave-group "
                                  "not supported.");
  }

  // If the group is reverse, adjust the index to refer to the last vector lane
  // instead of the first. We adjust the index from the first vector lane,
  // rather than directly getting the pointer for lane VF - 1, because the
  // pointer operand of the interleaved access is supposed to be uniform. For
  // uniform instructions, we're only required to generate a value for the
  // first vector lane in each unroll iteration.
  if (Group->isReverse())
    Index += (VF - 1) * Group->getFactor();

  bool InBounds = false;
  if (auto *gep = dyn_cast<GetElementPtrInst>(Ptr->stripPointerCasts()))
    InBounds = gep->isInBounds();

  for (unsigned Part = 0; Part < UF; Part++) {
    Value *NewPtr = getOrCreateScalarValue(Ptr, {Part, 0});

    // Notice current instruction could be any index. Need to adjust the address
    // to the member of index 0.
    //
    // E.g.  a = A[i+1];     // Member of index 1 (Current instruction)
    //       b = A[i];       // Member of index 0
    // Current pointer is pointed to A[i+1], adjust it to A[i].
    //
    // E.g.  A[i+1] = a;     // Member of index 1
    //       A[i]   = b;     // Member of index 0
    //       A[i+2] = c;     // Member of index 2 (Current instruction)
    // Current pointer is pointed to A[i+2], adjust it to A[i].
    NewPtr = Builder.CreateGEP(NewPtr, Builder.getInt32(-Index));
    if (InBounds)
      cast<GetElementPtrInst>(NewPtr)->setIsInBounds(true);

    // Cast to the vector pointer type.
    NewPtrs.push_back(Builder.CreateBitCast(NewPtr, PtrTy));
  }

  setDebugLocFromInst(Builder, Instr);
  Value *UndefVec = UndefValue::get(VecTy);

  Value *MaskForGaps = nullptr;
  if (Group->requiresScalarEpilogue() && !Cost->isScalarEpilogueAllowed()) {
    MaskForGaps = createBitMaskForGaps(Builder, VF, *Group);
    assert(MaskForGaps && "Mask for Gaps is required but it is null");
  }

  // Vectorize the interleaved load group.
  if (isa<LoadInst>(Instr)) {
    // For each unroll part, create a wide load for the group.
    SmallVector<Value *, 2> NewLoads;
    for (unsigned Part = 0; Part < UF; Part++) {
      Instruction *NewLoad;
      if (IsMaskForCondRequired || MaskForGaps) {
        assert(useMaskedInterleavedAccesses(*TTI) &&
               "masked interleaved groups are not allowed.");
        Value *GroupMask = MaskForGaps;
        if (IsMaskForCondRequired) {
          auto *Undefs = UndefValue::get(Mask[Part]->getType());
          auto *RepMask = createReplicatedMask(Builder, InterleaveFactor, VF);
          Value *ShuffledMask = Builder.CreateShuffleVector(
              Mask[Part], Undefs, RepMask, "interleaved.mask");
          GroupMask = MaskForGaps
                          ? Builder.CreateBinOp(Instruction::And, ShuffledMask,
                                                MaskForGaps)
                          : ShuffledMask;
        }
        NewLoad =
            Builder.CreateMaskedLoad(NewPtrs[Part], Group->getAlignment(),
                                     GroupMask, UndefVec, "wide.masked.vec");
      }
      else
        NewLoad = Builder.CreateAlignedLoad(NewPtrs[Part], 
          Group->getAlignment(), "wide.vec");
      Group->addMetadata(NewLoad);
      NewLoads.push_back(NewLoad);
    }

    // For each member in the group, shuffle out the appropriate data from the
    // wide loads.
    for (unsigned I = 0; I < InterleaveFactor; ++I) {
      Instruction *Member = Group->getMember(I);

      // Skip the gaps in the group.
      if (!Member)
        continue;

      Constant *StrideMask = createStrideMask(Builder, I, InterleaveFactor, VF);
      for (unsigned Part = 0; Part < UF; Part++) {
        Value *StridedVec = Builder.CreateShuffleVector(
            NewLoads[Part], UndefVec, StrideMask, "strided.vec");

        // If this member has different type, cast the result type.
        if (Member->getType() != ScalarTy) {
          VectorType *OtherVTy = VectorType::get(Member->getType(), VF);
          StridedVec = createBitOrPointerCast(StridedVec, OtherVTy, DL);
        }

        if (Group->isReverse())
          StridedVec = reverseVector(StridedVec);

        VectorLoopValueMap.setVectorValue(Member, Part, StridedVec);
      }
    }
    return;
  }

  // The sub vector type for current instruction.
  VectorType *SubVT = VectorType::get(ScalarTy, VF);

  // Vectorize the interleaved store group.
  for (unsigned Part = 0; Part < UF; Part++) {
    // Collect the stored vector from each member.
    SmallVector<Value *, 4> StoredVecs;
    for (unsigned i = 0; i < InterleaveFactor; i++) {
      // Interleaved store group doesn't allow a gap, so each index has a member
      Instruction *Member = Group->getMember(i);
      assert(Member && "Fail to get a member from an interleaved store group");

      Value *StoredVec = getOrCreateVectorValue(
          cast<StoreInst>(Member)->getValueOperand(), Part);
      if (Group->isReverse())
        StoredVec = reverseVector(StoredVec);

      // If this member has different type, cast it to a unified type.

      if (StoredVec->getType() != SubVT)
        StoredVec = createBitOrPointerCast(StoredVec, SubVT, DL);

      StoredVecs.push_back(StoredVec);
    }

    // Concatenate all vectors into a wide vector.
    Value *WideVec = concatenateVectors(Builder, StoredVecs);

    // Interleave the elements in the wide vector.
    Constant *IMask = createInterleaveMask(Builder, VF, InterleaveFactor);
    Value *IVec = Builder.CreateShuffleVector(WideVec, UndefVec, IMask,
                                              "interleaved.vec");

    Instruction *NewStoreInstr;
    if (IsMaskForCondRequired) {
      auto *Undefs = UndefValue::get(Mask[Part]->getType());
      auto *RepMask = createReplicatedMask(Builder, InterleaveFactor, VF);
      Value *ShuffledMask = Builder.CreateShuffleVector(
          Mask[Part], Undefs, RepMask, "interleaved.mask");
      NewStoreInstr = Builder.CreateMaskedStore(
          IVec, NewPtrs[Part], Group->getAlignment(), ShuffledMask);
    }
    else
      NewStoreInstr = Builder.CreateAlignedStore(IVec, NewPtrs[Part], 
        Group->getAlignment());

    Group->addMetadata(NewStoreInstr);
  }
}

void InnerLoopVectorizer::vectorizeMemoryInstruction(Instruction *Instr,
                                                     VectorParts *BlockInMask) {
  // Attempt to issue a wide load.
  LoadInst *LI = dyn_cast<LoadInst>(Instr);
  StoreInst *SI = dyn_cast<StoreInst>(Instr);

  assert((LI || SI) && "Invalid Load/Store instruction");

  LoopVectorizationCostModel::InstWidening Decision =
      Cost->getWideningDecision(Instr, VF);
  assert(Decision != LoopVectorizationCostModel::CM_Unknown &&
         "CM decision should be taken at this point");
  if (Decision == LoopVectorizationCostModel::CM_Interleave)
    return vectorizeInterleaveGroup(Instr);

  Type *ScalarDataTy = getMemInstValueType(Instr);
  Type *DataTy = VectorType::get(ScalarDataTy, VF);
  Value *Ptr = getLoadStorePointerOperand(Instr);
  unsigned Alignment = getLoadStoreAlignment(Instr);
  // An alignment of 0 means target abi alignment. We need to use the scalar's
  // target abi alignment in such a case.
  const DataLayout &DL = Instr->getModule()->getDataLayout();
  if (!Alignment)
    Alignment = DL.getABITypeAlignment(ScalarDataTy);
  unsigned AddressSpace = getLoadStoreAddressSpace(Instr);

  // Determine if the pointer operand of the access is either consecutive or
  // reverse consecutive.
  bool Reverse = (Decision == LoopVectorizationCostModel::CM_Widen_Reverse);
  bool ConsecutiveStride =
      Reverse || (Decision == LoopVectorizationCostModel::CM_Widen);
  bool CreateGatherScatter =
      (Decision == LoopVectorizationCostModel::CM_GatherScatter);

  // Either Ptr feeds a vector load/store, or a vector GEP should feed a vector
  // gather/scatter. Otherwise Decision should have been to Scalarize.
  assert((ConsecutiveStride || CreateGatherScatter) &&
         "The instruction should be scalarized");

  // Handle consecutive loads/stores.
  if (ConsecutiveStride)
    Ptr = getOrCreateScalarValue(Ptr, {0, 0});

  VectorParts Mask;
  bool isMaskRequired = BlockInMask;
  if (isMaskRequired)
    Mask = *BlockInMask;

  bool InBounds = false;
  if (auto *gep = dyn_cast<GetElementPtrInst>(
          getLoadStorePointerOperand(Instr)->stripPointerCasts()))
    InBounds = gep->isInBounds();

  const auto CreateVecPtr = [&](unsigned Part, Value *Ptr) -> Value * {
    // Calculate the pointer for the specific unroll-part.
    GetElementPtrInst *PartPtr = nullptr;

    if (Reverse) {
      // If the address is consecutive but reversed, then the
      // wide store needs to start at the last vector element.
      PartPtr = cast<GetElementPtrInst>(
          Builder.CreateGEP(Ptr, Builder.getInt32(-Part * VF)));
      PartPtr->setIsInBounds(InBounds);
      PartPtr = cast<GetElementPtrInst>(
          Builder.CreateGEP(PartPtr, Builder.getInt32(1 - VF)));
      PartPtr->setIsInBounds(InBounds);
      if (isMaskRequired) // Reverse of a null all-one mask is a null mask.
        Mask[Part] = reverseVector(Mask[Part]);
    } else {
      PartPtr = cast<GetElementPtrInst>(
          Builder.CreateGEP(Ptr, Builder.getInt32(Part * VF)));
      PartPtr->setIsInBounds(InBounds);
    }

    return Builder.CreateBitCast(PartPtr, DataTy->getPointerTo(AddressSpace));
  };

  // Handle Stores:
  if (SI) {
    setDebugLocFromInst(Builder, SI);

    for (unsigned Part = 0; Part < UF; ++Part) {
      Instruction *NewSI = nullptr;
      Value *StoredVal = getOrCreateVectorValue(SI->getValueOperand(), Part);
      if (CreateGatherScatter) {
        Value *MaskPart = isMaskRequired ? Mask[Part] : nullptr;
        Value *VectorGep = getOrCreateVectorValue(Ptr, Part);
        NewSI = Builder.CreateMaskedScatter(StoredVal, VectorGep, Alignment,
                                            MaskPart);
      } else {
        if (Reverse) {
          // If we store to reverse consecutive memory locations, then we need
          // to reverse the order of elements in the stored value.
          StoredVal = reverseVector(StoredVal);
          // We don't want to update the value in the map as it might be used in
          // another expression. So don't call resetVectorValue(StoredVal).
        }
        auto *VecPtr = CreateVecPtr(Part, Ptr);
        if (isMaskRequired)
          NewSI = Builder.CreateMaskedStore(StoredVal, VecPtr, Alignment,
                                            Mask[Part]);
        else
          NewSI = Builder.CreateAlignedStore(StoredVal, VecPtr, Alignment);
      }
      addMetadata(NewSI, SI);
    }
    return;
  }

  // Handle loads.
  assert(LI && "Must have a load instruction");
  setDebugLocFromInst(Builder, LI);
  for (unsigned Part = 0; Part < UF; ++Part) {
    Value *NewLI;
    if (CreateGatherScatter) {
      Value *MaskPart = isMaskRequired ? Mask[Part] : nullptr;
      Value *VectorGep = getOrCreateVectorValue(Ptr, Part);
      NewLI = Builder.CreateMaskedGather(VectorGep, Alignment, MaskPart,
                                         nullptr, "wide.masked.gather");
      addMetadata(NewLI, LI);
    } else {
      auto *VecPtr = CreateVecPtr(Part, Ptr);
      if (isMaskRequired)
        NewLI = Builder.CreateMaskedLoad(VecPtr, Alignment, Mask[Part],
                                         UndefValue::get(DataTy),
                                         "wide.masked.load");
      else
        NewLI = Builder.CreateAlignedLoad(VecPtr, Alignment, "wide.load");

      // Add metadata to the load, but setVectorValue to the reverse shuffle.
      addMetadata(NewLI, LI);
      if (Reverse)
        NewLI = reverseVector(NewLI);
    }
    VectorLoopValueMap.setVectorValue(Instr, Part, NewLI);
  }
}

void InnerLoopVectorizer::scalarizeInstruction(Instruction *Instr,
                                               const VPIteration &Instance,
                                               bool IfPredicateInstr) {
  assert(!Instr->getType()->isAggregateType() && "Can't handle vectors");

  setDebugLocFromInst(Builder, Instr);

  // Does this instruction return a value ?
  bool IsVoidRetTy = Instr->getType()->isVoidTy();

  Instruction *Cloned = Instr->clone();
  if (!IsVoidRetTy)
    Cloned->setName(Instr->getName() + ".cloned");

  // Replace the operands of the cloned instructions with their scalar
  // equivalents in the new loop.
  for (unsigned op = 0, e = Instr->getNumOperands(); op != e; ++op) {
    auto *NewOp = getOrCreateScalarValue(Instr->getOperand(op), Instance);
    Cloned->setOperand(op, NewOp);
  }
  addNewMetadata(Cloned, Instr);

  // Place the cloned scalar in the new loop.
  Builder.Insert(Cloned);

  // Add the cloned scalar to the scalar map entry.
  VectorLoopValueMap.setScalarValue(Instr, Instance, Cloned);

  // If we just cloned a new assumption, add it the assumption cache.
  if (auto *II = dyn_cast<IntrinsicInst>(Cloned))
    if (II->getIntrinsicID() == Intrinsic::assume)
      AC->registerAssumption(II);

  // End if-block.
  if (IfPredicateInstr)
    PredicatedInstructions.push_back(Cloned);
}

PHINode *InnerLoopVectorizer::createInductionVariable(Loop *L, Value *Start,
                                                      Value *End, Value *Step,
                                                      Instruction *DL) {
  BasicBlock *Header = L->getHeader();
  BasicBlock *Latch = L->getLoopLatch();
  // As we're just creating this loop, it's possible no latch exists
  // yet. If so, use the header as this will be a single block loop.
  if (!Latch)
    Latch = Header;

  IRBuilder<> Builder(&*Header->getFirstInsertionPt());
  Instruction *OldInst = getDebugLocFromInstOrOperands(OldInduction);
  setDebugLocFromInst(Builder, OldInst);
  auto *Induction = Builder.CreatePHI(Start->getType(), 2, "index");

  Builder.SetInsertPoint(Latch->getTerminator());
  setDebugLocFromInst(Builder, OldInst);

  // Create i+1 and fill the PHINode.
  Value *Next = Builder.CreateAdd(Induction, Step, "index.next");
  Induction->addIncoming(Start, L->getLoopPreheader());
  Induction->addIncoming(Next, Latch);
  // Create the compare.
  Value *ICmp = Builder.CreateICmpEQ(Next, End);
  Builder.CreateCondBr(ICmp, L->getExitBlock(), Header);

  // Now we have two terminators. Remove the old one from the block.
  Latch->getTerminator()->eraseFromParent();

  return Induction;
}

Value *InnerLoopVectorizer::getOrCreateTripCount(Loop *L) {
  if (TripCount)
    return TripCount;

  assert(L && "Create Trip Count for null loop.");
  IRBuilder<> Builder(L->getLoopPreheader()->getTerminator());
  // Find the loop boundaries.
  ScalarEvolution *SE = PSE.getSE();
  const SCEV *BackedgeTakenCount = PSE.getBackedgeTakenCount();
  assert(BackedgeTakenCount != SE->getCouldNotCompute() &&
         "Invalid loop count");

  Type *IdxTy = Legal->getWidestInductionType();
  assert(IdxTy && "No type for induction");

  // The exit count might have the type of i64 while the phi is i32. This can
  // happen if we have an induction variable that is sign extended before the
  // compare. The only way that we get a backedge taken count is that the
  // induction variable was signed and as such will not overflow. In such a case
  // truncation is legal.
  if (BackedgeTakenCount->getType()->getPrimitiveSizeInBits() >
      IdxTy->getPrimitiveSizeInBits())
    BackedgeTakenCount = SE->getTruncateOrNoop(BackedgeTakenCount, IdxTy);
  BackedgeTakenCount = SE->getNoopOrZeroExtend(BackedgeTakenCount, IdxTy);

  // Get the total trip count from the count by adding 1.
  const SCEV *ExitCount = SE->getAddExpr(
      BackedgeTakenCount, SE->getOne(BackedgeTakenCount->getType()));

  const DataLayout &DL = L->getHeader()->getModule()->getDataLayout();

  // Expand the trip count and place the new instructions in the preheader.
  // Notice that the pre-header does not change, only the loop body.
  SCEVExpander Exp(*SE, DL, "induction");

  // Count holds the overall loop count (N).
  TripCount = Exp.expandCodeFor(ExitCount, ExitCount->getType(),
                                L->getLoopPreheader()->getTerminator());

  if (TripCount->getType()->isPointerTy())
    TripCount =
        CastInst::CreatePointerCast(TripCount, IdxTy, "exitcount.ptrcnt.to.int",
                                    L->getLoopPreheader()->getTerminator());

  return TripCount;
}

Value *InnerLoopVectorizer::getOrCreateVectorTripCount(Loop *L) {
  if (VectorTripCount)
    return VectorTripCount;

  Value *TC = getOrCreateTripCount(L);
  IRBuilder<> Builder(L->getLoopPreheader()->getTerminator());

  Type *Ty = TC->getType();
  Constant *Step = ConstantInt::get(Ty, VF * UF);

  // If the tail is to be folded by masking, round the number of iterations N
  // up to a multiple of Step instead of rounding down. This is done by first
  // adding Step-1 and then rounding down. Note that it's ok if this addition
  // overflows: the vector induction variable will eventually wrap to zero given
  // that it starts at zero and its Step is a power of two; the loop will then
  // exit, with the last early-exit vector comparison also producing all-true.
  if (Cost->foldTailByMasking()) {
    assert(isPowerOf2_32(VF * UF) &&
           "VF*UF must be a power of 2 when folding tail by masking");
    TC = Builder.CreateAdd(TC, ConstantInt::get(Ty, VF * UF - 1), "n.rnd.up");
  }

  // Now we need to generate the expression for the part of the loop that the
  // vectorized body will execute. This is equal to N - (N % Step) if scalar
  // iterations are not required for correctness, or N - Step, otherwise. Step
  // is equal to the vectorization factor (number of SIMD elements) times the
  // unroll factor (number of SIMD instructions).
  Value *R = Builder.CreateURem(TC, Step, "n.mod.vf");

  // If there is a non-reversed interleaved group that may speculatively access
  // memory out-of-bounds, we need to ensure that there will be at least one
  // iteration of the scalar epilogue loop. Thus, if the step evenly divides
  // the trip count, we set the remainder to be equal to the step. If the step
  // does not evenly divide the trip count, no adjustment is necessary since
  // there will already be scalar iterations. Note that the minimum iterations
  // check ensures that N >= Step.
  if (VF > 1 && Cost->requiresScalarEpilogue()) {
    auto *IsZero = Builder.CreateICmpEQ(R, ConstantInt::get(R->getType(), 0));
    R = Builder.CreateSelect(IsZero, Step, R);
  }

  VectorTripCount = Builder.CreateSub(TC, R, "n.vec");

  return VectorTripCount;
}

Value *InnerLoopVectorizer::createBitOrPointerCast(Value *V, VectorType *DstVTy,
                                                   const DataLayout &DL) {
  // Verify that V is a vector type with same number of elements as DstVTy.
  unsigned VF = DstVTy->getNumElements();
  VectorType *SrcVecTy = cast<VectorType>(V->getType());
  assert((VF == SrcVecTy->getNumElements()) && "Vector dimensions do not match");
  Type *SrcElemTy = SrcVecTy->getElementType();
  Type *DstElemTy = DstVTy->getElementType();
  assert((DL.getTypeSizeInBits(SrcElemTy) == DL.getTypeSizeInBits(DstElemTy)) &&
         "Vector elements must have same size");

  // Do a direct cast if element types are castable.
  if (CastInst::isBitOrNoopPointerCastable(SrcElemTy, DstElemTy, DL)) {
    return Builder.CreateBitOrPointerCast(V, DstVTy);
  }
  // V cannot be directly casted to desired vector type.
  // May happen when V is a floating point vector but DstVTy is a vector of
  // pointers or vice-versa. Handle this using a two-step bitcast using an
  // intermediate Integer type for the bitcast i.e. Ptr <-> Int <-> Float.
  assert((DstElemTy->isPointerTy() != SrcElemTy->isPointerTy()) &&
         "Only one type should be a pointer type");
  assert((DstElemTy->isFloatingPointTy() != SrcElemTy->isFloatingPointTy()) &&
         "Only one type should be a floating point type");
  Type *IntTy =
      IntegerType::getIntNTy(V->getContext(), DL.getTypeSizeInBits(SrcElemTy));
  VectorType *VecIntTy = VectorType::get(IntTy, VF);
  Value *CastVal = Builder.CreateBitOrPointerCast(V, VecIntTy);
  return Builder.CreateBitOrPointerCast(CastVal, DstVTy);
}

void InnerLoopVectorizer::emitMinimumIterationCountCheck(Loop *L,
                                                         BasicBlock *Bypass) {
  Value *Count = getOrCreateTripCount(L);
  BasicBlock *BB = L->getLoopPreheader();
  IRBuilder<> Builder(BB->getTerminator());

  // Generate code to check if the loop's trip count is less than VF * UF, or
  // equal to it in case a scalar epilogue is required; this implies that the
  // vector trip count is zero. This check also covers the case where adding one
  // to the backedge-taken count overflowed leading to an incorrect trip count
  // of zero. In this case we will also jump to the scalar loop.
  auto P = Cost->requiresScalarEpilogue() ? ICmpInst::ICMP_ULE
                                          : ICmpInst::ICMP_ULT;

  // If tail is to be folded, vector loop takes care of all iterations.
  Value *CheckMinIters = Builder.getFalse();
  if (!Cost->foldTailByMasking())
    CheckMinIters = Builder.CreateICmp(
        P, Count, ConstantInt::get(Count->getType(), VF * UF),
        "min.iters.check");

  BasicBlock *NewBB = BB->splitBasicBlock(BB->getTerminator(), "vector.ph");
  // Update dominator tree immediately if the generated block is a
  // LoopBypassBlock because SCEV expansions to generate loop bypass
  // checks may query it before the current function is finished.
  DT->addNewBlock(NewBB, BB);
  if (L->getParentLoop())
    L->getParentLoop()->addBasicBlockToLoop(NewBB, *LI);
  ReplaceInstWithInst(BB->getTerminator(),
                      BranchInst::Create(Bypass, NewBB, CheckMinIters));
  LoopBypassBlocks.push_back(BB);
}

void InnerLoopVectorizer::emitSCEVChecks(Loop *L, BasicBlock *Bypass) {
  BasicBlock *BB = L->getLoopPreheader();

  // Generate the code to check that the SCEV assumptions that we made.
  // We want the new basic block to start at the first instruction in a
  // sequence of instructions that form a check.
  SCEVExpander Exp(*PSE.getSE(), Bypass->getModule()->getDataLayout(),
                   "scev.check");
  Value *SCEVCheck =
      Exp.expandCodeForPredicate(&PSE.getUnionPredicate(), BB->getTerminator());

  if (auto *C = dyn_cast<ConstantInt>(SCEVCheck))
    if (C->isZero())
      return;

  assert(!Cost->foldTailByMasking() &&
         "Cannot SCEV check stride or overflow when folding tail");
  // Create a new block containing the stride check.
  BB->setName("vector.scevcheck");
  auto *NewBB = BB->splitBasicBlock(BB->getTerminator(), "vector.ph");
  // Update dominator tree immediately if the generated block is a
  // LoopBypassBlock because SCEV expansions to generate loop bypass
  // checks may query it before the current function is finished.
  DT->addNewBlock(NewBB, BB);
  if (L->getParentLoop())
    L->getParentLoop()->addBasicBlockToLoop(NewBB, *LI);
  ReplaceInstWithInst(BB->getTerminator(),
                      BranchInst::Create(Bypass, NewBB, SCEVCheck));
  LoopBypassBlocks.push_back(BB);
  AddedSafetyChecks = true;
}

void InnerLoopVectorizer::emitMemRuntimeChecks(Loop *L, BasicBlock *Bypass) {
  // VPlan-native path does not do any analysis for runtime checks currently.
  if (EnableVPlanNativePath)
    return;

  BasicBlock *BB = L->getLoopPreheader();

  // Generate the code that checks in runtime if arrays overlap. We put the
  // checks into a separate block to make the more common case of few elements
  // faster.
  Instruction *FirstCheckInst;
  Instruction *MemRuntimeCheck;
  std::tie(FirstCheckInst, MemRuntimeCheck) =
      Legal->getLAI()->addRuntimeChecks(BB->getTerminator());
  if (!MemRuntimeCheck)
    return;

  assert(!Cost->foldTailByMasking() && "Cannot check memory when folding tail");
  // Create a new block containing the memory check.
  BB->setName("vector.memcheck");
  auto *NewBB = BB->splitBasicBlock(BB->getTerminator(), "vector.ph");
  // Update dominator tree immediately if the generated block is a
  // LoopBypassBlock because SCEV expansions to generate loop bypass
  // checks may query it before the current function is finished.
  DT->addNewBlock(NewBB, BB);
  if (L->getParentLoop())
    L->getParentLoop()->addBasicBlockToLoop(NewBB, *LI);
  ReplaceInstWithInst(BB->getTerminator(),
                      BranchInst::Create(Bypass, NewBB, MemRuntimeCheck));
  LoopBypassBlocks.push_back(BB);
  AddedSafetyChecks = true;

  // We currently don't use LoopVersioning for the actual loop cloning but we
  // still use it to add the noalias metadata.
  LVer = llvm::make_unique<LoopVersioning>(*Legal->getLAI(), OrigLoop, LI, DT,
                                           PSE.getSE());
  LVer->prepareNoAliasMetadata();
}

Value *InnerLoopVectorizer::emitTransformedIndex(
    IRBuilder<> &B, Value *Index, ScalarEvolution *SE, const DataLayout &DL,
    const InductionDescriptor &ID) const {

  SCEVExpander Exp(*SE, DL, "induction");
  auto Step = ID.getStep();
  auto StartValue = ID.getStartValue();
  assert(Index->getType() == Step->getType() &&
         "Index type does not match StepValue type");

  // Note: the IR at this point is broken. We cannot use SE to create any new
  // SCEV and then expand it, hoping that SCEV's simplification will give us
  // a more optimal code. Unfortunately, attempt of doing so on invalid IR may
  // lead to various SCEV crashes. So all we can do is to use builder and rely
  // on InstCombine for future simplifications. Here we handle some trivial
  // cases only.
  auto CreateAdd = [&B](Value *X, Value *Y) {
    assert(X->getType() == Y->getType() && "Types don't match!");
    if (auto *CX = dyn_cast<ConstantInt>(X))
      if (CX->isZero())
        return Y;
    if (auto *CY = dyn_cast<ConstantInt>(Y))
      if (CY->isZero())
        return X;
    return B.CreateAdd(X, Y);
  };

  auto CreateMul = [&B](Value *X, Value *Y) {
    assert(X->getType() == Y->getType() && "Types don't match!");
    if (auto *CX = dyn_cast<ConstantInt>(X))
      if (CX->isOne())
        return Y;
    if (auto *CY = dyn_cast<ConstantInt>(Y))
      if (CY->isOne())
        return X;
    return B.CreateMul(X, Y);
  };

  switch (ID.getKind()) {
  case InductionDescriptor::IK_IntInduction: {
    assert(Index->getType() == StartValue->getType() &&
           "Index type does not match StartValue type");
    if (ID.getConstIntStepValue() && ID.getConstIntStepValue()->isMinusOne())
      return B.CreateSub(StartValue, Index);
    auto *Offset = CreateMul(
        Index, Exp.expandCodeFor(Step, Index->getType(), &*B.GetInsertPoint()));
    return CreateAdd(StartValue, Offset);
  }
  case InductionDescriptor::IK_PtrInduction: {
    assert(isa<SCEVConstant>(Step) &&
           "Expected constant step for pointer induction");
    return B.CreateGEP(
        nullptr, StartValue,
        CreateMul(Index, Exp.expandCodeFor(Step, Index->getType(),
                                           &*B.GetInsertPoint())));
  }
  case InductionDescriptor::IK_FpInduction: {
    assert(Step->getType()->isFloatingPointTy() && "Expected FP Step value");
    auto InductionBinOp = ID.getInductionBinOp();
    assert(InductionBinOp &&
           (InductionBinOp->getOpcode() == Instruction::FAdd ||
            InductionBinOp->getOpcode() == Instruction::FSub) &&
           "Original bin op should be defined for FP induction");

    Value *StepValue = cast<SCEVUnknown>(Step)->getValue();

    // Floating point operations had to be 'fast' to enable the induction.
    FastMathFlags Flags;
    Flags.setFast();

    Value *MulExp = B.CreateFMul(StepValue, Index);
    if (isa<Instruction>(MulExp))
      // We have to check, the MulExp may be a constant.
      cast<Instruction>(MulExp)->setFastMathFlags(Flags);

    Value *BOp = B.CreateBinOp(InductionBinOp->getOpcode(), StartValue, MulExp,
                               "induction");
    if (isa<Instruction>(BOp))
      cast<Instruction>(BOp)->setFastMathFlags(Flags);

    return BOp;
  }
  case InductionDescriptor::IK_NoInduction:
    return nullptr;
  }
  llvm_unreachable("invalid enum");
}

BasicBlock *InnerLoopVectorizer::createVectorizedLoopSkeleton() {
  /*
   In this function we generate a new loop. The new loop will contain
   the vectorized instructions while the old loop will continue to run the
   scalar remainder.

       [ ] <-- loop iteration number check.
    /   |
   /    v
  |    [ ] <-- vector loop bypass (may consist of multiple blocks).
  |  /  |
  | /   v
  ||   [ ]     <-- vector pre header.
  |/    |
  |     v
  |    [  ] \
  |    [  ]_|   <-- vector loop.
  |     |
  |     v
  |   -[ ]   <--- middle-block.
  |  /  |
  | /   v
  -|- >[ ]     <--- new preheader.
   |    |
   |    v
   |   [ ] \
   |   [ ]_|   <-- old scalar loop to handle remainder.
    \   |
     \  v
      >[ ]     <-- exit block.
   ...
   */

  BasicBlock *OldBasicBlock = OrigLoop->getHeader();
  BasicBlock *VectorPH = OrigLoop->getLoopPreheader();
  BasicBlock *ExitBlock = OrigLoop->getExitBlock();
  MDNode *OrigLoopID = OrigLoop->getLoopID();
  assert(VectorPH && "Invalid loop structure");
  assert(ExitBlock && "Must have an exit block");

  // Some loops have a single integer induction variable, while other loops
  // don't. One example is c++ iterators that often have multiple pointer
  // induction variables. In the code below we also support a case where we
  // don't have a single induction variable.
  //
  // We try to obtain an induction variable from the original loop as hard
  // as possible. However if we don't find one that:
  //   - is an integer
  //   - counts from zero, stepping by one
  //   - is the size of the widest induction variable type
  // then we create a new one.
  OldInduction = Legal->getPrimaryInduction();
  Type *IdxTy = Legal->getWidestInductionType();

  // Split the single block loop into the two loop structure described above.
  BasicBlock *VecBody =
      VectorPH->splitBasicBlock(VectorPH->getTerminator(), "vector.body");
  BasicBlock *MiddleBlock =
      VecBody->splitBasicBlock(VecBody->getTerminator(), "middle.block");
  BasicBlock *ScalarPH =
      MiddleBlock->splitBasicBlock(MiddleBlock->getTerminator(), "scalar.ph");

  // Create and register the new vector loop.
  Loop *Lp = LI->AllocateLoop();
  Loop *ParentLoop = OrigLoop->getParentLoop();

  // Insert the new loop into the loop nest and register the new basic blocks
  // before calling any utilities such as SCEV that require valid LoopInfo.
  if (ParentLoop) {
    ParentLoop->addChildLoop(Lp);
    ParentLoop->addBasicBlockToLoop(ScalarPH, *LI);
    ParentLoop->addBasicBlockToLoop(MiddleBlock, *LI);
  } else {
    LI->addTopLevelLoop(Lp);
  }
  Lp->addBasicBlockToLoop(VecBody, *LI);

  // Find the loop boundaries.
  Value *Count = getOrCreateTripCount(Lp);

  Value *StartIdx = ConstantInt::get(IdxTy, 0);

  // Now, compare the new count to zero. If it is zero skip the vector loop and
  // jump to the scalar loop. This check also covers the case where the
  // backedge-taken count is uint##_max: adding one to it will overflow leading
  // to an incorrect trip count of zero. In this (rare) case we will also jump
  // to the scalar loop.
  emitMinimumIterationCountCheck(Lp, ScalarPH);

  // Generate the code to check any assumptions that we've made for SCEV
  // expressions.
  emitSCEVChecks(Lp, ScalarPH);

  // Generate the code that checks in runtime if arrays overlap. We put the
  // checks into a separate block to make the more common case of few elements
  // faster.
  emitMemRuntimeChecks(Lp, ScalarPH);

  // Generate the induction variable.
  // The loop step is equal to the vectorization factor (num of SIMD elements)
  // times the unroll factor (num of SIMD instructions).
  Value *CountRoundDown = getOrCreateVectorTripCount(Lp);
  Constant *Step = ConstantInt::get(IdxTy, VF * UF);
  Induction =
      createInductionVariable(Lp, StartIdx, CountRoundDown, Step,
                              getDebugLocFromInstOrOperands(OldInduction));

  // We are going to resume the execution of the scalar loop.
  // Go over all of the induction variables that we found and fix the
  // PHIs that are left in the scalar version of the loop.
  // The starting values of PHI nodes depend on the counter of the last
  // iteration in the vectorized loop.
  // If we come from a bypass edge then we need to start from the original
  // start value.

  // This variable saves the new starting index for the scalar loop. It is used
  // to test if there are any tail iterations left once the vector loop has
  // completed.
  LoopVectorizationLegality::InductionList *List = Legal->getInductionVars();
  for (auto &InductionEntry : *List) {
    PHINode *OrigPhi = InductionEntry.first;
    InductionDescriptor II = InductionEntry.second;

    // Create phi nodes to merge from the  backedge-taken check block.
    PHINode *BCResumeVal = PHINode::Create(
        OrigPhi->getType(), 3, "bc.resume.val", ScalarPH->getTerminator());
    // Copy original phi DL over to the new one.
    BCResumeVal->setDebugLoc(OrigPhi->getDebugLoc());
    Value *&EndValue = IVEndValues[OrigPhi];
    if (OrigPhi == OldInduction) {
      // We know what the end value is.
      EndValue = CountRoundDown;
    } else {
      IRBuilder<> B(Lp->getLoopPreheader()->getTerminator());
      Type *StepType = II.getStep()->getType();
      Instruction::CastOps CastOp =
        CastInst::getCastOpcode(CountRoundDown, true, StepType, true);
      Value *CRD = B.CreateCast(CastOp, CountRoundDown, StepType, "cast.crd");
      const DataLayout &DL = OrigLoop->getHeader()->getModule()->getDataLayout();
      EndValue = emitTransformedIndex(B, CRD, PSE.getSE(), DL, II);
      EndValue->setName("ind.end");
    }

    // The new PHI merges the original incoming value, in case of a bypass,
    // or the value at the end of the vectorized loop.
    BCResumeVal->addIncoming(EndValue, MiddleBlock);

    // Fix the scalar body counter (PHI node).
    unsigned BlockIdx = OrigPhi->getBasicBlockIndex(ScalarPH);

    // The old induction's phi node in the scalar body needs the truncated
    // value.
    for (BasicBlock *BB : LoopBypassBlocks)
      BCResumeVal->addIncoming(II.getStartValue(), BB);
    OrigPhi->setIncomingValue(BlockIdx, BCResumeVal);
  }

  // Add a check in the middle block to see if we have completed
  // all of the iterations in the first vector loop.
  // If (N - N%VF) == N, then we *don't* need to run the remainder.
  // If tail is to be folded, we know we don't need to run the remainder.
  Value *CmpN = Builder.getTrue();
  if (!Cost->foldTailByMasking())
    CmpN =
        CmpInst::Create(Instruction::ICmp, CmpInst::ICMP_EQ, Count,
                        CountRoundDown, "cmp.n", MiddleBlock->getTerminator());
  ReplaceInstWithInst(MiddleBlock->getTerminator(),
                      BranchInst::Create(ExitBlock, ScalarPH, CmpN));

  // Get ready to start creating new instructions into the vectorized body.
  Builder.SetInsertPoint(&*VecBody->getFirstInsertionPt());

  // Save the state.
  LoopVectorPreHeader = Lp->getLoopPreheader();
  LoopScalarPreHeader = ScalarPH;
  LoopMiddleBlock = MiddleBlock;
  LoopExitBlock = ExitBlock;
  LoopVectorBody = VecBody;
  LoopScalarBody = OldBasicBlock;

  Optional<MDNode *> VectorizedLoopID =
      makeFollowupLoopID(OrigLoopID, {LLVMLoopVectorizeFollowupAll,
                                      LLVMLoopVectorizeFollowupVectorized});
  if (VectorizedLoopID.hasValue()) {
    Lp->setLoopID(VectorizedLoopID.getValue());

    // Do not setAlreadyVectorized if loop attributes have been defined
    // explicitly.
    return LoopVectorPreHeader;
  }

  // Keep all loop hints from the original loop on the vector loop (we'll
  // replace the vectorizer-specific hints below).
  if (MDNode *LID = OrigLoop->getLoopID())
    Lp->setLoopID(LID);

  LoopVectorizeHints Hints(Lp, true, *ORE);
  Hints.setAlreadyVectorized();

  return LoopVectorPreHeader;
}

// Fix up external users of the induction variable. At this point, we are
// in LCSSA form, with all external PHIs that use the IV having one input value,
// coming from the remainder loop. We need those PHIs to also have a correct
// value for the IV when arriving directly from the middle block.
void InnerLoopVectorizer::fixupIVUsers(PHINode *OrigPhi,
                                       const InductionDescriptor &II,
                                       Value *CountRoundDown, Value *EndValue,
                                       BasicBlock *MiddleBlock) {
  // There are two kinds of external IV usages - those that use the value
  // computed in the last iteration (the PHI) and those that use the penultimate
  // value (the value that feeds into the phi from the loop latch).
  // We allow both, but they, obviously, have different values.

  assert(OrigLoop->getExitBlock() && "Expected a single exit block");

  DenseMap<Value *, Value *> MissingVals;

  // An external user of the last iteration's value should see the value that
  // the remainder loop uses to initialize its own IV.
  Value *PostInc = OrigPhi->getIncomingValueForBlock(OrigLoop->getLoopLatch());
  for (User *U : PostInc->users()) {
    Instruction *UI = cast<Instruction>(U);
    if (!OrigLoop->contains(UI)) {
      assert(isa<PHINode>(UI) && "Expected LCSSA form");
      MissingVals[UI] = EndValue;
    }
  }

  // An external user of the penultimate value need to see EndValue - Step.
  // The simplest way to get this is to recompute it from the constituent SCEVs,
  // that is Start + (Step * (CRD - 1)).
  for (User *U : OrigPhi->users()) {
    auto *UI = cast<Instruction>(U);
    if (!OrigLoop->contains(UI)) {
      const DataLayout &DL =
          OrigLoop->getHeader()->getModule()->getDataLayout();
      assert(isa<PHINode>(UI) && "Expected LCSSA form");

      IRBuilder<> B(MiddleBlock->getTerminator());
      Value *CountMinusOne = B.CreateSub(
          CountRoundDown, ConstantInt::get(CountRoundDown->getType(), 1));
      Value *CMO =
          !II.getStep()->getType()->isIntegerTy()
              ? B.CreateCast(Instruction::SIToFP, CountMinusOne,
                             II.getStep()->getType())
              : B.CreateSExtOrTrunc(CountMinusOne, II.getStep()->getType());
      CMO->setName("cast.cmo");
      Value *Escape = emitTransformedIndex(B, CMO, PSE.getSE(), DL, II);
      Escape->setName("ind.escape");
      MissingVals[UI] = Escape;
    }
  }

  for (auto &I : MissingVals) {
    PHINode *PHI = cast<PHINode>(I.first);
    // One corner case we have to handle is two IVs "chasing" each-other,
    // that is %IV2 = phi [...], [ %IV1, %latch ]
    // In this case, if IV1 has an external use, we need to avoid adding both
    // "last value of IV1" and "penultimate value of IV2". So, verify that we
    // don't already have an incoming value for the middle block.
    if (PHI->getBasicBlockIndex(MiddleBlock) == -1)
      PHI->addIncoming(I.second, MiddleBlock);
  }
}

namespace {

struct CSEDenseMapInfo {
  static bool canHandle(const Instruction *I) {
    return isa<InsertElementInst>(I) || isa<ExtractElementInst>(I) ||
           isa<ShuffleVectorInst>(I) || isa<GetElementPtrInst>(I);
  }

  static inline Instruction *getEmptyKey() {
    return DenseMapInfo<Instruction *>::getEmptyKey();
  }

  static inline Instruction *getTombstoneKey() {
    return DenseMapInfo<Instruction *>::getTombstoneKey();
  }

  static unsigned getHashValue(const Instruction *I) {
    assert(canHandle(I) && "Unknown instruction!");
    return hash_combine(I->getOpcode(), hash_combine_range(I->value_op_begin(),
                                                           I->value_op_end()));
  }

  static bool isEqual(const Instruction *LHS, const Instruction *RHS) {
    if (LHS == getEmptyKey() || RHS == getEmptyKey() ||
        LHS == getTombstoneKey() || RHS == getTombstoneKey())
      return LHS == RHS;
    return LHS->isIdenticalTo(RHS);
  }
};

} // end anonymous namespace

///Perform cse of induction variable instructions.
static void cse(BasicBlock *BB) {
  // Perform simple cse.
  SmallDenseMap<Instruction *, Instruction *, 4, CSEDenseMapInfo> CSEMap;
  for (BasicBlock::iterator I = BB->begin(), E = BB->end(); I != E;) {
    Instruction *In = &*I++;

    if (!CSEDenseMapInfo::canHandle(In))
      continue;

    // Check if we can replace this instruction with any of the
    // visited instructions.
    if (Instruction *V = CSEMap.lookup(In)) {
      In->replaceAllUsesWith(V);
      In->eraseFromParent();
      continue;
    }

    CSEMap[In] = In;
  }
}

/// Estimate the overhead of scalarizing an instruction. This is a
/// convenience wrapper for the type-based getScalarizationOverhead API.
static unsigned getScalarizationOverhead(Instruction *I, unsigned VF,
                                         const TargetTransformInfo &TTI) {
  if (VF == 1)
    return 0;

  unsigned Cost = 0;
  Type *RetTy = ToVectorTy(I->getType(), VF);
  if (!RetTy->isVoidTy() &&
      (!isa<LoadInst>(I) ||
       !TTI.supportsEfficientVectorElementLoadStore()))
    Cost += TTI.getScalarizationOverhead(RetTy, true, false);

  // Some targets keep addresses scalar.
  if (isa<LoadInst>(I) && !TTI.prefersVectorizedAddressing())
    return Cost;

  if (CallInst *CI = dyn_cast<CallInst>(I)) {
    SmallVector<const Value *, 4> Operands(CI->arg_operands());
    Cost += TTI.getOperandsScalarizationOverhead(Operands, VF);
  }
  else if (!isa<StoreInst>(I) ||
           !TTI.supportsEfficientVectorElementLoadStore()) {
    SmallVector<const Value *, 4> Operands(I->operand_values());
    Cost += TTI.getOperandsScalarizationOverhead(Operands, VF);
  }

  return Cost;
}

// Estimate cost of a call instruction CI if it were vectorized with factor VF.
// Return the cost of the instruction, including scalarization overhead if it's
// needed. The flag NeedToScalarize shows if the call needs to be scalarized -
// i.e. either vector version isn't available, or is too expensive.
static unsigned getVectorCallCost(CallInst *CI, unsigned VF,
                                  const TargetTransformInfo &TTI,
                                  const TargetLibraryInfo *TLI,
                                  bool &NeedToScalarize) {
  Function *F = CI->getCalledFunction();
  StringRef FnName = CI->getCalledFunction()->getName();
  Type *ScalarRetTy = CI->getType();
  SmallVector<Type *, 4> Tys, ScalarTys;
  for (auto &ArgOp : CI->arg_operands())
    ScalarTys.push_back(ArgOp->getType());

  // Estimate cost of scalarized vector call. The source operands are assumed
  // to be vectors, so we need to extract individual elements from there,
  // execute VF scalar calls, and then gather the result into the vector return
  // value.
  unsigned ScalarCallCost = TTI.getCallInstrCost(F, ScalarRetTy, ScalarTys);
  if (VF == 1)
    return ScalarCallCost;

  // Compute corresponding vector type for return value and arguments.
  Type *RetTy = ToVectorTy(ScalarRetTy, VF);
  for (Type *ScalarTy : ScalarTys)
    Tys.push_back(ToVectorTy(ScalarTy, VF));

  // Compute costs of unpacking argument values for the scalar calls and
  // packing the return values to a vector.
  unsigned ScalarizationCost = getScalarizationOverhead(CI, VF, TTI);

  unsigned Cost = ScalarCallCost * VF + ScalarizationCost;

  // If we can't emit a vector call for this function, then the currently found
  // cost is the cost we need to return.
  NeedToScalarize = true;
  if (!TLI || !TLI->isFunctionVectorizable(FnName, VF) || CI->isNoBuiltin())
    return Cost;

  // If the corresponding vector cost is cheaper, return its cost.
  unsigned VectorCallCost = TTI.getCallInstrCost(nullptr, RetTy, Tys);
  if (VectorCallCost < Cost) {
    NeedToScalarize = false;
    return VectorCallCost;
  }
  return Cost;
}

// Estimate cost of an intrinsic call instruction CI if it were vectorized with
// factor VF.  Return the cost of the instruction, including scalarization
// overhead if it's needed.
static unsigned getVectorIntrinsicCost(CallInst *CI, unsigned VF,
                                       const TargetTransformInfo &TTI,
                                       const TargetLibraryInfo *TLI) {
  Intrinsic::ID ID = getVectorIntrinsicIDForCall(CI, TLI);
  assert(ID && "Expected intrinsic call!");

  FastMathFlags FMF;
  if (auto *FPMO = dyn_cast<FPMathOperator>(CI))
    FMF = FPMO->getFastMathFlags();

  SmallVector<Value *, 4> Operands(CI->arg_operands());
  return TTI.getIntrinsicInstrCost(ID, CI->getType(), Operands, FMF, VF);
}

static Type *smallestIntegerVectorType(Type *T1, Type *T2) {
  auto *I1 = cast<IntegerType>(T1->getVectorElementType());
  auto *I2 = cast<IntegerType>(T2->getVectorElementType());
  return I1->getBitWidth() < I2->getBitWidth() ? T1 : T2;
}
static Type *largestIntegerVectorType(Type *T1, Type *T2) {
  auto *I1 = cast<IntegerType>(T1->getVectorElementType());
  auto *I2 = cast<IntegerType>(T2->getVectorElementType());
  return I1->getBitWidth() > I2->getBitWidth() ? T1 : T2;
}

void InnerLoopVectorizer::truncateToMinimalBitwidths() {
  // For every instruction `I` in MinBWs, truncate the operands, create a
  // truncated version of `I` and reextend its result. InstCombine runs
  // later and will remove any ext/trunc pairs.
  SmallPtrSet<Value *, 4> Erased;
  for (const auto &KV : Cost->getMinimalBitwidths()) {
    // If the value wasn't vectorized, we must maintain the original scalar
    // type. The absence of the value from VectorLoopValueMap indicates that it
    // wasn't vectorized.
    if (!VectorLoopValueMap.hasAnyVectorValue(KV.first))
      continue;
    for (unsigned Part = 0; Part < UF; ++Part) {
      Value *I = getOrCreateVectorValue(KV.first, Part);
      if (Erased.find(I) != Erased.end() || I->use_empty() ||
          !isa<Instruction>(I))
        continue;
      Type *OriginalTy = I->getType();
      Type *ScalarTruncatedTy =
          IntegerType::get(OriginalTy->getContext(), KV.second);
      Type *TruncatedTy = VectorType::get(ScalarTruncatedTy,
                                          OriginalTy->getVectorNumElements());
      if (TruncatedTy == OriginalTy)
        continue;

      IRBuilder<> B(cast<Instruction>(I));
      auto ShrinkOperand = [&](Value *V) -> Value * {
        if (auto *ZI = dyn_cast<ZExtInst>(V))
          if (ZI->getSrcTy() == TruncatedTy)
            return ZI->getOperand(0);
        return B.CreateZExtOrTrunc(V, TruncatedTy);
      };

      // The actual instruction modification depends on the instruction type,
      // unfortunately.
      Value *NewI = nullptr;
      if (auto *BO = dyn_cast<BinaryOperator>(I)) {
        NewI = B.CreateBinOp(BO->getOpcode(), ShrinkOperand(BO->getOperand(0)),
                             ShrinkOperand(BO->getOperand(1)));

        // Any wrapping introduced by shrinking this operation shouldn't be
        // considered undefined behavior. So, we can't unconditionally copy
        // arithmetic wrapping flags to NewI.
        cast<BinaryOperator>(NewI)->copyIRFlags(I, /*IncludeWrapFlags=*/false);
      } else if (auto *CI = dyn_cast<ICmpInst>(I)) {
        NewI =
            B.CreateICmp(CI->getPredicate(), ShrinkOperand(CI->getOperand(0)),
                         ShrinkOperand(CI->getOperand(1)));
      } else if (auto *SI = dyn_cast<SelectInst>(I)) {
        NewI = B.CreateSelect(SI->getCondition(),
                              ShrinkOperand(SI->getTrueValue()),
                              ShrinkOperand(SI->getFalseValue()));
      } else if (auto *CI = dyn_cast<CastInst>(I)) {
        switch (CI->getOpcode()) {
        default:
          llvm_unreachable("Unhandled cast!");
        case Instruction::Trunc:
          NewI = ShrinkOperand(CI->getOperand(0));
          break;
        case Instruction::SExt:
          NewI = B.CreateSExtOrTrunc(
              CI->getOperand(0),
              smallestIntegerVectorType(OriginalTy, TruncatedTy));
          break;
        case Instruction::ZExt:
          NewI = B.CreateZExtOrTrunc(
              CI->getOperand(0),
              smallestIntegerVectorType(OriginalTy, TruncatedTy));
          break;
        }
      } else if (auto *SI = dyn_cast<ShuffleVectorInst>(I)) {
        auto Elements0 = SI->getOperand(0)->getType()->getVectorNumElements();
        auto *O0 = B.CreateZExtOrTrunc(
            SI->getOperand(0), VectorType::get(ScalarTruncatedTy, Elements0));
        auto Elements1 = SI->getOperand(1)->getType()->getVectorNumElements();
        auto *O1 = B.CreateZExtOrTrunc(
            SI->getOperand(1), VectorType::get(ScalarTruncatedTy, Elements1));

        NewI = B.CreateShuffleVector(O0, O1, SI->getMask());
      } else if (isa<LoadInst>(I) || isa<PHINode>(I)) {
        // Don't do anything with the operands, just extend the result.
        continue;
      } else if (auto *IE = dyn_cast<InsertElementInst>(I)) {
        auto Elements = IE->getOperand(0)->getType()->getVectorNumElements();
        auto *O0 = B.CreateZExtOrTrunc(
            IE->getOperand(0), VectorType::get(ScalarTruncatedTy, Elements));
        auto *O1 = B.CreateZExtOrTrunc(IE->getOperand(1), ScalarTruncatedTy);
        NewI = B.CreateInsertElement(O0, O1, IE->getOperand(2));
      } else if (auto *EE = dyn_cast<ExtractElementInst>(I)) {
        auto Elements = EE->getOperand(0)->getType()->getVectorNumElements();
        auto *O0 = B.CreateZExtOrTrunc(
            EE->getOperand(0), VectorType::get(ScalarTruncatedTy, Elements));
        NewI = B.CreateExtractElement(O0, EE->getOperand(2));
      } else {
        // If we don't know what to do, be conservative and don't do anything.
        continue;
      }

      // Lastly, extend the result.
      NewI->takeName(cast<Instruction>(I));
      Value *Res = B.CreateZExtOrTrunc(NewI, OriginalTy);
      I->replaceAllUsesWith(Res);
      cast<Instruction>(I)->eraseFromParent();
      Erased.insert(I);
      VectorLoopValueMap.resetVectorValue(KV.first, Part, Res);
    }
  }

  // We'll have created a bunch of ZExts that are now parentless. Clean up.
  for (const auto &KV : Cost->getMinimalBitwidths()) {
    // If the value wasn't vectorized, we must maintain the original scalar
    // type. The absence of the value from VectorLoopValueMap indicates that it
    // wasn't vectorized.
    if (!VectorLoopValueMap.hasAnyVectorValue(KV.first))
      continue;
    for (unsigned Part = 0; Part < UF; ++Part) {
      Value *I = getOrCreateVectorValue(KV.first, Part);
      ZExtInst *Inst = dyn_cast<ZExtInst>(I);
      if (Inst && Inst->use_empty()) {
        Value *NewI = Inst->getOperand(0);
        Inst->eraseFromParent();
        VectorLoopValueMap.resetVectorValue(KV.first, Part, NewI);
      }
    }
  }
}

void InnerLoopVectorizer::fixVectorizedLoop() {
  // Insert truncates and extends for any truncated instructions as hints to
  // InstCombine.
  if (VF > 1)
    truncateToMinimalBitwidths();

  // Fix widened non-induction PHIs by setting up the PHI operands.
  if (OrigPHIsToFix.size()) {
    assert(EnableVPlanNativePath &&
           "Unexpected non-induction PHIs for fixup in non VPlan-native path");
    fixNonInductionPHIs();
  }

  // At this point every instruction in the original loop is widened to a
  // vector form. Now we need to fix the recurrences in the loop. These PHI
  // nodes are currently empty because we did not want to introduce cycles.
  // This is the second stage of vectorizing recurrences.
  fixCrossIterationPHIs();

  // Update the dominator tree.
  //
  // FIXME: After creating the structure of the new loop, the dominator tree is
  //        no longer up-to-date, and it remains that way until we update it
  //        here. An out-of-date dominator tree is problematic for SCEV,
  //        because SCEVExpander uses it to guide code generation. The
  //        vectorizer use SCEVExpanders in several places. Instead, we should
  //        keep the dominator tree up-to-date as we go.
  updateAnalysis();

  // Fix-up external users of the induction variables.
  for (auto &Entry : *Legal->getInductionVars())
    fixupIVUsers(Entry.first, Entry.second,
                 getOrCreateVectorTripCount(LI->getLoopFor(LoopVectorBody)),
                 IVEndValues[Entry.first], LoopMiddleBlock);

  fixLCSSAPHIs();
  for (Instruction *PI : PredicatedInstructions)
    sinkScalarOperands(&*PI);

  // Remove redundant induction instructions.
  cse(LoopVectorBody);
}

void InnerLoopVectorizer::fixCrossIterationPHIs() {
  // In order to support recurrences we need to be able to vectorize Phi nodes.
  // Phi nodes have cycles, so we need to vectorize them in two stages. This is
  // stage #2: We now need to fix the recurrences by adding incoming edges to
  // the currently empty PHI nodes. At this point every instruction in the
  // original loop is widened to a vector form so we can use them to construct
  // the incoming edges.
  for (PHINode &Phi : OrigLoop->getHeader()->phis()) {
    // Handle first-order recurrences and reductions that need to be fixed.
    if (Legal->isFirstOrderRecurrence(&Phi))
      fixFirstOrderRecurrence(&Phi);
    else if (Legal->isReductionVariable(&Phi))
      fixReduction(&Phi);
  }
}

void InnerLoopVectorizer::fixFirstOrderRecurrence(PHINode *Phi) {
  // This is the second phase of vectorizing first-order recurrences. An
  // overview of the transformation is described below. Suppose we have the
  // following loop.
  //
  //   for (int i = 0; i < n; ++i)
  //     b[i] = a[i] - a[i - 1];
  //
  // There is a first-order recurrence on "a". For this loop, the shorthand
  // scalar IR looks like:
  //
  //   scalar.ph:
  //     s_init = a[-1]
  //     br scalar.body
  //
  //   scalar.body:
  //     i = phi [0, scalar.ph], [i+1, scalar.body]
  //     s1 = phi [s_init, scalar.ph], [s2, scalar.body]
  //     s2 = a[i]
  //     b[i] = s2 - s1
  //     br cond, scalar.body, ...
  //
  // In this example, s1 is a recurrence because it's value depends on the
  // previous iteration. In the first phase of vectorization, we created a
  // temporary value for s1. We now complete the vectorization and produce the
  // shorthand vector IR shown below (for VF = 4, UF = 1).
  //
  //   vector.ph:
  //     v_init = vector(..., ..., ..., a[-1])
  //     br vector.body
  //
  //   vector.body
  //     i = phi [0, vector.ph], [i+4, vector.body]
  //     v1 = phi [v_init, vector.ph], [v2, vector.body]
  //     v2 = a[i, i+1, i+2, i+3];
  //     v3 = vector(v1(3), v2(0, 1, 2))
  //     b[i, i+1, i+2, i+3] = v2 - v3
  //     br cond, vector.body, middle.block
  //
  //   middle.block:
  //     x = v2(3)
  //     br scalar.ph
  //
  //   scalar.ph:
  //     s_init = phi [x, middle.block], [a[-1], otherwise]
  //     br scalar.body
  //
  // After execution completes the vector loop, we extract the next value of
  // the recurrence (x) to use as the initial value in the scalar loop.

  // Get the original loop preheader and single loop latch.
  auto *Preheader = OrigLoop->getLoopPreheader();
  auto *Latch = OrigLoop->getLoopLatch();

  // Get the initial and previous values of the scalar recurrence.
  auto *ScalarInit = Phi->getIncomingValueForBlock(Preheader);
  auto *Previous = Phi->getIncomingValueForBlock(Latch);

  // Create a vector from the initial value.
  auto *VectorInit = ScalarInit;
  if (VF > 1) {
    Builder.SetInsertPoint(LoopVectorPreHeader->getTerminator());
    VectorInit = Builder.CreateInsertElement(
        UndefValue::get(VectorType::get(VectorInit->getType(), VF)), VectorInit,
        Builder.getInt32(VF - 1), "vector.recur.init");
  }

  // We constructed a temporary phi node in the first phase of vectorization.
  // This phi node will eventually be deleted.
  Builder.SetInsertPoint(
      cast<Instruction>(VectorLoopValueMap.getVectorValue(Phi, 0)));

  // Create a phi node for the new recurrence. The current value will either be
  // the initial value inserted into a vector or loop-varying vector value.
  auto *VecPhi = Builder.CreatePHI(VectorInit->getType(), 2, "vector.recur");
  VecPhi->addIncoming(VectorInit, LoopVectorPreHeader);

  // Get the vectorized previous value of the last part UF - 1. It appears last
  // among all unrolled iterations, due to the order of their construction.
  Value *PreviousLastPart = getOrCreateVectorValue(Previous, UF - 1);

  // Set the insertion point after the previous value if it is an instruction.
  // Note that the previous value may have been constant-folded so it is not
  // guaranteed to be an instruction in the vector loop. Also, if the previous
  // value is a phi node, we should insert after all the phi nodes to avoid
  // breaking basic block verification.
  if (LI->getLoopFor(LoopVectorBody)->isLoopInvariant(PreviousLastPart) ||
      isa<PHINode>(PreviousLastPart))
    Builder.SetInsertPoint(&*LoopVectorBody->getFirstInsertionPt());
  else
    Builder.SetInsertPoint(
        &*++BasicBlock::iterator(cast<Instruction>(PreviousLastPart)));

  // We will construct a vector for the recurrence by combining the values for
  // the current and previous iterations. This is the required shuffle mask.
  SmallVector<Constant *, 8> ShuffleMask(VF);
  ShuffleMask[0] = Builder.getInt32(VF - 1);
  for (unsigned I = 1; I < VF; ++I)
    ShuffleMask[I] = Builder.getInt32(I + VF - 1);

  // The vector from which to take the initial value for the current iteration
  // (actual or unrolled). Initially, this is the vector phi node.
  Value *Incoming = VecPhi;

  // Shuffle the current and previous vector and update the vector parts.
  for (unsigned Part = 0; Part < UF; ++Part) {
    Value *PreviousPart = getOrCreateVectorValue(Previous, Part);
    Value *PhiPart = VectorLoopValueMap.getVectorValue(Phi, Part);
    auto *Shuffle =
        VF > 1 ? Builder.CreateShuffleVector(Incoming, PreviousPart,
                                             ConstantVector::get(ShuffleMask))
               : Incoming;
    PhiPart->replaceAllUsesWith(Shuffle);
    cast<Instruction>(PhiPart)->eraseFromParent();
    VectorLoopValueMap.resetVectorValue(Phi, Part, Shuffle);
    Incoming = PreviousPart;
  }

  // Fix the latch value of the new recurrence in the vector loop.
  VecPhi->addIncoming(Incoming, LI->getLoopFor(LoopVectorBody)->getLoopLatch());

  // Extract the last vector element in the middle block. This will be the
  // initial value for the recurrence when jumping to the scalar loop.
  auto *ExtractForScalar = Incoming;
  if (VF > 1) {
    Builder.SetInsertPoint(LoopMiddleBlock->getTerminator());
    ExtractForScalar = Builder.CreateExtractElement(
        ExtractForScalar, Builder.getInt32(VF - 1), "vector.recur.extract");
  }
  // Extract the second last element in the middle block if the
  // Phi is used outside the loop. We need to extract the phi itself
  // and not the last element (the phi update in the current iteration). This
  // will be the value when jumping to the exit block from the LoopMiddleBlock,
  // when the scalar loop is not run at all.
  Value *ExtractForPhiUsedOutsideLoop = nullptr;
  if (VF > 1)
    ExtractForPhiUsedOutsideLoop = Builder.CreateExtractElement(
        Incoming, Builder.getInt32(VF - 2), "vector.recur.extract.for.phi");
  // When loop is unrolled without vectorizing, initialize
  // ExtractForPhiUsedOutsideLoop with the value just prior to unrolled value of
  // `Incoming`. This is analogous to the vectorized case above: extracting the
  // second last element when VF > 1.
  else if (UF > 1)
    ExtractForPhiUsedOutsideLoop = getOrCreateVectorValue(Previous, UF - 2);

  // Fix the initial value of the original recurrence in the scalar loop.
  Builder.SetInsertPoint(&*LoopScalarPreHeader->begin());
  auto *Start = Builder.CreatePHI(Phi->getType(), 2, "scalar.recur.init");
  for (auto *BB : predecessors(LoopScalarPreHeader)) {
    auto *Incoming = BB == LoopMiddleBlock ? ExtractForScalar : ScalarInit;
    Start->addIncoming(Incoming, BB);
  }

  Phi->setIncomingValue(Phi->getBasicBlockIndex(LoopScalarPreHeader), Start);
  Phi->setName("scalar.recur");

  // Finally, fix users of the recurrence outside the loop. The users will need
  // either the last value of the scalar recurrence or the last value of the
  // vector recurrence we extracted in the middle block. Since the loop is in
  // LCSSA form, we just need to find all the phi nodes for the original scalar
  // recurrence in the exit block, and then add an edge for the middle block.
  for (PHINode &LCSSAPhi : LoopExitBlock->phis()) {
    if (LCSSAPhi.getIncomingValue(0) == Phi) {
      LCSSAPhi.addIncoming(ExtractForPhiUsedOutsideLoop, LoopMiddleBlock);
    }
  }
}

void InnerLoopVectorizer::fixReduction(PHINode *Phi) {
  Constant *Zero = Builder.getInt32(0);

  // Get it's reduction variable descriptor.
  assert(Legal->isReductionVariable(Phi) &&
         "Unable to find the reduction variable");
  RecurrenceDescriptor RdxDesc = (*Legal->getReductionVars())[Phi];

  RecurrenceDescriptor::RecurrenceKind RK = RdxDesc.getRecurrenceKind();
  TrackingVH<Value> ReductionStartValue = RdxDesc.getRecurrenceStartValue();
  Instruction *LoopExitInst = RdxDesc.getLoopExitInstr();
  RecurrenceDescriptor::MinMaxRecurrenceKind MinMaxKind =
    RdxDesc.getMinMaxRecurrenceKind();
  setDebugLocFromInst(Builder, ReductionStartValue);

  // We need to generate a reduction vector from the incoming scalar.
  // To do so, we need to generate the 'identity' vector and override
  // one of the elements with the incoming scalar reduction. We need
  // to do it in the vector-loop preheader.
  Builder.SetInsertPoint(LoopVectorPreHeader->getTerminator());

  // This is the vector-clone of the value that leaves the loop.
  Type *VecTy = getOrCreateVectorValue(LoopExitInst, 0)->getType();

  // Find the reduction identity variable. Zero for addition, or, xor,
  // one for multiplication, -1 for And.
  Value *Identity;
  Value *VectorStart;
  if (RK == RecurrenceDescriptor::RK_IntegerMinMax ||
      RK == RecurrenceDescriptor::RK_FloatMinMax) {
    // MinMax reduction have the start value as their identify.
    if (VF == 1) {
      VectorStart = Identity = ReductionStartValue;
    } else {
      VectorStart = Identity =
        Builder.CreateVectorSplat(VF, ReductionStartValue, "minmax.ident");
    }
  } else {
    // Handle other reduction kinds:
    Constant *Iden = RecurrenceDescriptor::getRecurrenceIdentity(
        RK, VecTy->getScalarType());
    if (VF == 1) {
      Identity = Iden;
      // This vector is the Identity vector where the first element is the
      // incoming scalar reduction.
      VectorStart = ReductionStartValue;
    } else {
      Identity = ConstantVector::getSplat(VF, Iden);

      // This vector is the Identity vector where the first element is the
      // incoming scalar reduction.
      VectorStart =
        Builder.CreateInsertElement(Identity, ReductionStartValue, Zero);
    }
  }

  // Fix the vector-loop phi.

  // Reductions do not have to start at zero. They can start with
  // any loop invariant values.
  BasicBlock *Latch = OrigLoop->getLoopLatch();
  Value *LoopVal = Phi->getIncomingValueForBlock(Latch);
  for (unsigned Part = 0; Part < UF; ++Part) {
    Value *VecRdxPhi = getOrCreateVectorValue(Phi, Part);
    Value *Val = getOrCreateVectorValue(LoopVal, Part);
    // Make sure to add the reduction stat value only to the
    // first unroll part.
    Value *StartVal = (Part == 0) ? VectorStart : Identity;
    cast<PHINode>(VecRdxPhi)->addIncoming(StartVal, LoopVectorPreHeader);
    cast<PHINode>(VecRdxPhi)
      ->addIncoming(Val, LI->getLoopFor(LoopVectorBody)->getLoopLatch());
  }

  // Before each round, move the insertion point right between
  // the PHIs and the values we are going to write.
  // This allows us to write both PHINodes and the extractelement
  // instructions.
  Builder.SetInsertPoint(&*LoopMiddleBlock->getFirstInsertionPt());

  setDebugLocFromInst(Builder, LoopExitInst);

  // If the vector reduction can be performed in a smaller type, we truncate
  // then extend the loop exit value to enable InstCombine to evaluate the
  // entire expression in the smaller type.
  if (VF > 1 && Phi->getType() != RdxDesc.getRecurrenceType()) {
    Type *RdxVecTy = VectorType::get(RdxDesc.getRecurrenceType(), VF);
    Builder.SetInsertPoint(
        LI->getLoopFor(LoopVectorBody)->getLoopLatch()->getTerminator());
    VectorParts RdxParts(UF);
    for (unsigned Part = 0; Part < UF; ++Part) {
      RdxParts[Part] = VectorLoopValueMap.getVectorValue(LoopExitInst, Part);
      Value *Trunc = Builder.CreateTrunc(RdxParts[Part], RdxVecTy);
      Value *Extnd = RdxDesc.isSigned() ? Builder.CreateSExt(Trunc, VecTy)
                                        : Builder.CreateZExt(Trunc, VecTy);
      for (Value::user_iterator UI = RdxParts[Part]->user_begin();
           UI != RdxParts[Part]->user_end();)
        if (*UI != Trunc) {
          (*UI++)->replaceUsesOfWith(RdxParts[Part], Extnd);
          RdxParts[Part] = Extnd;
        } else {
          ++UI;
        }
    }
    Builder.SetInsertPoint(&*LoopMiddleBlock->getFirstInsertionPt());
    for (unsigned Part = 0; Part < UF; ++Part) {
      RdxParts[Part] = Builder.CreateTrunc(RdxParts[Part], RdxVecTy);
      VectorLoopValueMap.resetVectorValue(LoopExitInst, Part, RdxParts[Part]);
    }
  }

  // Reduce all of the unrolled parts into a single vector.
  Value *ReducedPartRdx = VectorLoopValueMap.getVectorValue(LoopExitInst, 0);
  unsigned Op = RecurrenceDescriptor::getRecurrenceBinOp(RK);
  setDebugLocFromInst(Builder, ReducedPartRdx);
  for (unsigned Part = 1; Part < UF; ++Part) {
    Value *RdxPart = VectorLoopValueMap.getVectorValue(LoopExitInst, Part);
    if (Op != Instruction::ICmp && Op != Instruction::FCmp)
      // Floating point operations had to be 'fast' to enable the reduction.
      ReducedPartRdx = addFastMathFlag(
          Builder.CreateBinOp((Instruction::BinaryOps)Op, RdxPart,
                              ReducedPartRdx, "bin.rdx"));
    else
      ReducedPartRdx = createMinMaxOp(Builder, MinMaxKind, ReducedPartRdx,
                                      RdxPart);
  }

  if (VF > 1) {
    bool NoNaN = Legal->hasFunNoNaNAttr();
    ReducedPartRdx =
        createTargetReduction(Builder, TTI, RdxDesc, ReducedPartRdx, NoNaN);
    // If the reduction can be performed in a smaller type, we need to extend
    // the reduction to the wider type before we branch to the original loop.
    if (Phi->getType() != RdxDesc.getRecurrenceType())
      ReducedPartRdx =
        RdxDesc.isSigned()
        ? Builder.CreateSExt(ReducedPartRdx, Phi->getType())
        : Builder.CreateZExt(ReducedPartRdx, Phi->getType());
  }

  // Create a phi node that merges control-flow from the backedge-taken check
  // block and the middle block.
  PHINode *BCBlockPhi = PHINode::Create(Phi->getType(), 2, "bc.merge.rdx",
                                        LoopScalarPreHeader->getTerminator());
  for (unsigned I = 0, E = LoopBypassBlocks.size(); I != E; ++I)
    BCBlockPhi->addIncoming(ReductionStartValue, LoopBypassBlocks[I]);
  BCBlockPhi->addIncoming(ReducedPartRdx, LoopMiddleBlock);

  // Now, we need to fix the users of the reduction variable
  // inside and outside of the scalar remainder loop.
  // We know that the loop is in LCSSA form. We need to update the
  // PHI nodes in the exit blocks.
  for (PHINode &LCSSAPhi : LoopExitBlock->phis()) {
    // All PHINodes need to have a single entry edge, or two if
    // we already fixed them.
    assert(LCSSAPhi.getNumIncomingValues() < 3 && "Invalid LCSSA PHI");

    // We found a reduction value exit-PHI. Update it with the
    // incoming bypass edge.
    if (LCSSAPhi.getIncomingValue(0) == LoopExitInst)
      LCSSAPhi.addIncoming(ReducedPartRdx, LoopMiddleBlock);
  } // end of the LCSSA phi scan.

    // Fix the scalar loop reduction variable with the incoming reduction sum
    // from the vector body and from the backedge value.
  int IncomingEdgeBlockIdx =
    Phi->getBasicBlockIndex(OrigLoop->getLoopLatch());
  assert(IncomingEdgeBlockIdx >= 0 && "Invalid block index");
  // Pick the other block.
  int SelfEdgeBlockIdx = (IncomingEdgeBlockIdx ? 0 : 1);
  Phi->setIncomingValue(SelfEdgeBlockIdx, BCBlockPhi);
  Phi->setIncomingValue(IncomingEdgeBlockIdx, LoopExitInst);
}

void InnerLoopVectorizer::fixLCSSAPHIs() {
  for (PHINode &LCSSAPhi : LoopExitBlock->phis()) {
    if (LCSSAPhi.getNumIncomingValues() == 1) {
      auto *IncomingValue = LCSSAPhi.getIncomingValue(0);
      // Non-instruction incoming values will have only one value.
      unsigned LastLane = 0;
      if (isa<Instruction>(IncomingValue)) 
          LastLane = Cost->isUniformAfterVectorization(
                         cast<Instruction>(IncomingValue), VF)
                         ? 0
                         : VF - 1;
      // Can be a loop invariant incoming value or the last scalar value to be
      // extracted from the vectorized loop.
      Builder.SetInsertPoint(LoopMiddleBlock->getTerminator());
      Value *lastIncomingValue =
          getOrCreateScalarValue(IncomingValue, { UF - 1, LastLane });
      LCSSAPhi.addIncoming(lastIncomingValue, LoopMiddleBlock);
    }
  }
}

void InnerLoopVectorizer::sinkScalarOperands(Instruction *PredInst) {
  // The basic block and loop containing the predicated instruction.
  auto *PredBB = PredInst->getParent();
  auto *VectorLoop = LI->getLoopFor(PredBB);

  // Initialize a worklist with the operands of the predicated instruction.
  SetVector<Value *> Worklist(PredInst->op_begin(), PredInst->op_end());

  // Holds instructions that we need to analyze again. An instruction may be
  // reanalyzed if we don't yet know if we can sink it or not.
  SmallVector<Instruction *, 8> InstsToReanalyze;

  // Returns true if a given use occurs in the predicated block. Phi nodes use
  // their operands in their corresponding predecessor blocks.
  auto isBlockOfUsePredicated = [&](Use &U) -> bool {
    auto *I = cast<Instruction>(U.getUser());
    BasicBlock *BB = I->getParent();
    if (auto *Phi = dyn_cast<PHINode>(I))
      BB = Phi->getIncomingBlock(
          PHINode::getIncomingValueNumForOperand(U.getOperandNo()));
    return BB == PredBB;
  };

  // Iteratively sink the scalarized operands of the predicated instruction
  // into the block we created for it. When an instruction is sunk, it's
  // operands are then added to the worklist. The algorithm ends after one pass
  // through the worklist doesn't sink a single instruction.
  bool Changed;
  do {
    // Add the instructions that need to be reanalyzed to the worklist, and
    // reset the changed indicator.
    Worklist.insert(InstsToReanalyze.begin(), InstsToReanalyze.end());
    InstsToReanalyze.clear();
    Changed = false;

    while (!Worklist.empty()) {
      auto *I = dyn_cast<Instruction>(Worklist.pop_back_val());

      // We can't sink an instruction if it is a phi node, is already in the
      // predicated block, is not in the loop, or may have side effects.
      if (!I || isa<PHINode>(I) || I->getParent() == PredBB ||
          !VectorLoop->contains(I) || I->mayHaveSideEffects())
        continue;

      // It's legal to sink the instruction if all its uses occur in the
      // predicated block. Otherwise, there's nothing to do yet, and we may
      // need to reanalyze the instruction.
      if (!llvm::all_of(I->uses(), isBlockOfUsePredicated)) {
        InstsToReanalyze.push_back(I);
        continue;
      }

      // Move the instruction to the beginning of the predicated block, and add
      // it's operands to the worklist.
      I->moveBefore(&*PredBB->getFirstInsertionPt());
      Worklist.insert(I->op_begin(), I->op_end());

      // The sinking may have enabled other instructions to be sunk, so we will
      // need to iterate.
      Changed = true;
    }
  } while (Changed);
}

void InnerLoopVectorizer::fixNonInductionPHIs() {
  for (PHINode *OrigPhi : OrigPHIsToFix) {
    PHINode *NewPhi =
        cast<PHINode>(VectorLoopValueMap.getVectorValue(OrigPhi, 0));
    unsigned NumIncomingValues = OrigPhi->getNumIncomingValues();

    SmallVector<BasicBlock *, 2> ScalarBBPredecessors(
        predecessors(OrigPhi->getParent()));
    SmallVector<BasicBlock *, 2> VectorBBPredecessors(
        predecessors(NewPhi->getParent()));
    assert(ScalarBBPredecessors.size() == VectorBBPredecessors.size() &&
           "Scalar and Vector BB should have the same number of predecessors");

    // The insertion point in Builder may be invalidated by the time we get
    // here. Force the Builder insertion point to something valid so that we do
    // not run into issues during insertion point restore in
    // getOrCreateVectorValue calls below.
    Builder.SetInsertPoint(NewPhi);

    // The predecessor order is preserved and we can rely on mapping between
    // scalar and vector block predecessors.
    for (unsigned i = 0; i < NumIncomingValues; ++i) {
      BasicBlock *NewPredBB = VectorBBPredecessors[i];

      // When looking up the new scalar/vector values to fix up, use incoming
      // values from original phi.
      Value *ScIncV =
          OrigPhi->getIncomingValueForBlock(ScalarBBPredecessors[i]);

      // Scalar incoming value may need a broadcast
      Value *NewIncV = getOrCreateVectorValue(ScIncV, 0);
      NewPhi->addIncoming(NewIncV, NewPredBB);
    }
  }
}

void InnerLoopVectorizer::widenPHIInstruction(Instruction *PN, unsigned UF,
                                              unsigned VF) {
  PHINode *P = cast<PHINode>(PN);
  if (EnableVPlanNativePath) {
    // Currently we enter here in the VPlan-native path for non-induction
    // PHIs where all control flow is uniform. We simply widen these PHIs.
    // Create a vector phi with no operands - the vector phi operands will be
    // set at the end of vector code generation.
    Type *VecTy =
        (VF == 1) ? PN->getType() : VectorType::get(PN->getType(), VF);
    Value *VecPhi = Builder.CreatePHI(VecTy, PN->getNumOperands(), "vec.phi");
    VectorLoopValueMap.setVectorValue(P, 0, VecPhi);
    OrigPHIsToFix.push_back(P);

    return;
  }

  assert(PN->getParent() == OrigLoop->getHeader() &&
         "Non-header phis should have been handled elsewhere");

  // In order to support recurrences we need to be able to vectorize Phi nodes.
  // Phi nodes have cycles, so we need to vectorize them in two stages. This is
  // stage #1: We create a new vector PHI node with no incoming edges. We'll use
  // this value when we vectorize all of the instructions that use the PHI.
  if (Legal->isReductionVariable(P) || Legal->isFirstOrderRecurrence(P)) {
    for (unsigned Part = 0; Part < UF; ++Part) {
      // This is phase one of vectorizing PHIs.
      Type *VecTy =
          (VF == 1) ? PN->getType() : VectorType::get(PN->getType(), VF);
      Value *EntryPart = PHINode::Create(
          VecTy, 2, "vec.phi", &*LoopVectorBody->getFirstInsertionPt());
      VectorLoopValueMap.setVectorValue(P, Part, EntryPart);
    }
    return;
  }

  setDebugLocFromInst(Builder, P);

  // This PHINode must be an induction variable.
  // Make sure that we know about it.
  assert(Legal->getInductionVars()->count(P) && "Not an induction variable");

  InductionDescriptor II = Legal->getInductionVars()->lookup(P);
  const DataLayout &DL = OrigLoop->getHeader()->getModule()->getDataLayout();

  // FIXME: The newly created binary instructions should contain nsw/nuw flags,
  // which can be found from the original scalar operations.
  switch (II.getKind()) {
  case InductionDescriptor::IK_NoInduction:
    llvm_unreachable("Unknown induction");
  case InductionDescriptor::IK_IntInduction:
  case InductionDescriptor::IK_FpInduction:
    llvm_unreachable("Integer/fp induction is handled elsewhere.");
  case InductionDescriptor::IK_PtrInduction: {
    // Handle the pointer induction variable case.
    assert(P->getType()->isPointerTy() && "Unexpected type.");
    // This is the normalized GEP that starts counting at zero.
    Value *PtrInd = Induction;
    PtrInd = Builder.CreateSExtOrTrunc(PtrInd, II.getStep()->getType());
    // Determine the number of scalars we need to generate for each unroll
    // iteration. If the instruction is uniform, we only need to generate the
    // first lane. Otherwise, we generate all VF values.
    unsigned Lanes = Cost->isUniformAfterVectorization(P, VF) ? 1 : VF;
    // These are the scalar results. Notice that we don't generate vector GEPs
    // because scalar GEPs result in better code.
    for (unsigned Part = 0; Part < UF; ++Part) {
      for (unsigned Lane = 0; Lane < Lanes; ++Lane) {
        Constant *Idx = ConstantInt::get(PtrInd->getType(), Lane + Part * VF);
        Value *GlobalIdx = Builder.CreateAdd(PtrInd, Idx);
        Value *SclrGep =
            emitTransformedIndex(Builder, GlobalIdx, PSE.getSE(), DL, II);
        SclrGep->setName("next.gep");
        VectorLoopValueMap.setScalarValue(P, {Part, Lane}, SclrGep);
      }
    }
    return;
  }
  }
}

/// A helper function for checking whether an integer division-related
/// instruction may divide by zero (in which case it must be predicated if
/// executed conditionally in the scalar code).
/// TODO: It may be worthwhile to generalize and check isKnownNonZero().
/// Non-zero divisors that are non compile-time constants will not be
/// converted into multiplication, so we will still end up scalarizing
/// the division, but can do so w/o predication.
static bool mayDivideByZero(Instruction &I) {
  assert((I.getOpcode() == Instruction::UDiv ||
          I.getOpcode() == Instruction::SDiv ||
          I.getOpcode() == Instruction::URem ||
          I.getOpcode() == Instruction::SRem) &&
         "Unexpected instruction");
  Value *Divisor = I.getOperand(1);
  auto *CInt = dyn_cast<ConstantInt>(Divisor);
  return !CInt || CInt->isZero();
}

void InnerLoopVectorizer::widenInstruction(Instruction &I) {
  switch (I.getOpcode()) {
  case Instruction::Br:
  case Instruction::PHI:
    llvm_unreachable("This instruction is handled by a different recipe.");
  case Instruction::GetElementPtr: {
    // Construct a vector GEP by widening the operands of the scalar GEP as
    // necessary. We mark the vector GEP 'inbounds' if appropriate. A GEP
    // results in a vector of pointers when at least one operand of the GEP
    // is vector-typed. Thus, to keep the representation compact, we only use
    // vector-typed operands for loop-varying values.
    auto *GEP = cast<GetElementPtrInst>(&I);

    if (VF > 1 && OrigLoop->hasLoopInvariantOperands(GEP)) {
      // If we are vectorizing, but the GEP has only loop-invariant operands,
      // the GEP we build (by only using vector-typed operands for
      // loop-varying values) would be a scalar pointer. Thus, to ensure we
      // produce a vector of pointers, we need to either arbitrarily pick an
      // operand to broadcast, or broadcast a clone of the original GEP.
      // Here, we broadcast a clone of the original.
      //
      // TODO: If at some point we decide to scalarize instructions having
      //       loop-invariant operands, this special case will no longer be
      //       required. We would add the scalarization decision to
      //       collectLoopScalars() and teach getVectorValue() to broadcast
      //       the lane-zero scalar value.
      auto *Clone = Builder.Insert(GEP->clone());
      for (unsigned Part = 0; Part < UF; ++Part) {
        Value *EntryPart = Builder.CreateVectorSplat(VF, Clone);
        VectorLoopValueMap.setVectorValue(&I, Part, EntryPart);
        addMetadata(EntryPart, GEP);
      }
    } else {
      // If the GEP has at least one loop-varying operand, we are sure to
      // produce a vector of pointers. But if we are only unrolling, we want
      // to produce a scalar GEP for each unroll part. Thus, the GEP we
      // produce with the code below will be scalar (if VF == 1) or vector
      // (otherwise). Note that for the unroll-only case, we still maintain
      // values in the vector mapping with initVector, as we do for other
      // instructions.
      for (unsigned Part = 0; Part < UF; ++Part) {
        // The pointer operand of the new GEP. If it's loop-invariant, we
        // won't broadcast it.
        auto *Ptr =
            OrigLoop->isLoopInvariant(GEP->getPointerOperand())
                ? GEP->getPointerOperand()
                : getOrCreateVectorValue(GEP->getPointerOperand(), Part);

        // Collect all the indices for the new GEP. If any index is
        // loop-invariant, we won't broadcast it.
        SmallVector<Value *, 4> Indices;
        for (auto &U : make_range(GEP->idx_begin(), GEP->idx_end())) {
          if (OrigLoop->isLoopInvariant(U.get()))
            Indices.push_back(U.get());
          else
            Indices.push_back(getOrCreateVectorValue(U.get(), Part));
        }

        // Create the new GEP. Note that this GEP may be a scalar if VF == 1,
        // but it should be a vector, otherwise.
        auto *NewGEP = GEP->isInBounds()
                           ? Builder.CreateInBoundsGEP(Ptr, Indices)
                           : Builder.CreateGEP(Ptr, Indices);
        assert((VF == 1 || NewGEP->getType()->isVectorTy()) &&
               "NewGEP is not a pointer vector");
        VectorLoopValueMap.setVectorValue(&I, Part, NewGEP);
        addMetadata(NewGEP, GEP);
      }
    }

    break;
  }
  case Instruction::UDiv:
  case Instruction::SDiv:
  case Instruction::SRem:
  case Instruction::URem:
  case Instruction::Add:
  case Instruction::FAdd:
  case Instruction::Sub:
  case Instruction::FSub:
  case Instruction::Mul:
  case Instruction::FMul:
  case Instruction::FDiv:
  case Instruction::FRem:
  case Instruction::Shl:
  case Instruction::LShr:
  case Instruction::AShr:
  case Instruction::And:
  case Instruction::Or:
  case Instruction::Xor: {
    // Just widen binops.
    auto *BinOp = cast<BinaryOperator>(&I);
    setDebugLocFromInst(Builder, BinOp);

    for (unsigned Part = 0; Part < UF; ++Part) {
      Value *A = getOrCreateVectorValue(BinOp->getOperand(0), Part);
      Value *B = getOrCreateVectorValue(BinOp->getOperand(1), Part);
      Value *V = Builder.CreateBinOp(BinOp->getOpcode(), A, B);

      if (BinaryOperator *VecOp = dyn_cast<BinaryOperator>(V))
        VecOp->copyIRFlags(BinOp);

      // Use this vector value for all users of the original instruction.
      VectorLoopValueMap.setVectorValue(&I, Part, V);
      addMetadata(V, BinOp);
    }

    break;
  }
  case Instruction::Select: {
    // Widen selects.
    // If the selector is loop invariant we can create a select
    // instruction with a scalar condition. Otherwise, use vector-select.
    auto *SE = PSE.getSE();
    bool InvariantCond =
        SE->isLoopInvariant(PSE.getSCEV(I.getOperand(0)), OrigLoop);
    setDebugLocFromInst(Builder, &I);

    // The condition can be loop invariant  but still defined inside the
    // loop. This means that we can't just use the original 'cond' value.
    // We have to take the 'vectorized' value and pick the first lane.
    // Instcombine will make this a no-op.

    auto *ScalarCond = getOrCreateScalarValue(I.getOperand(0), {0, 0});

    for (unsigned Part = 0; Part < UF; ++Part) {
      Value *Cond = getOrCreateVectorValue(I.getOperand(0), Part);
      Value *Op0 = getOrCreateVectorValue(I.getOperand(1), Part);
      Value *Op1 = getOrCreateVectorValue(I.getOperand(2), Part);
      Value *Sel =
          Builder.CreateSelect(InvariantCond ? ScalarCond : Cond, Op0, Op1);
      VectorLoopValueMap.setVectorValue(&I, Part, Sel);
      addMetadata(Sel, &I);
    }

    break;
  }

  case Instruction::ICmp:
  case Instruction::FCmp: {
    // Widen compares. Generate vector compares.
    bool FCmp = (I.getOpcode() == Instruction::FCmp);
    auto *Cmp = dyn_cast<CmpInst>(&I);
    setDebugLocFromInst(Builder, Cmp);
    for (unsigned Part = 0; Part < UF; ++Part) {
      Value *A = getOrCreateVectorValue(Cmp->getOperand(0), Part);
      Value *B = getOrCreateVectorValue(Cmp->getOperand(1), Part);
      Value *C = nullptr;
      if (FCmp) {
        // Propagate fast math flags.
        IRBuilder<>::FastMathFlagGuard FMFG(Builder);
        Builder.setFastMathFlags(Cmp->getFastMathFlags());
        C = Builder.CreateFCmp(Cmp->getPredicate(), A, B);
      } else {
        C = Builder.CreateICmp(Cmp->getPredicate(), A, B);
      }
      VectorLoopValueMap.setVectorValue(&I, Part, C);
      addMetadata(C, &I);
    }

    break;
  }

  case Instruction::ZExt:
  case Instruction::SExt:
  case Instruction::FPToUI:
  case Instruction::FPToSI:
  case Instruction::FPExt:
  case Instruction::PtrToInt:
  case Instruction::IntToPtr:
  case Instruction::SIToFP:
  case Instruction::UIToFP:
  case Instruction::Trunc:
  case Instruction::FPTrunc:
  case Instruction::BitCast: {
    auto *CI = dyn_cast<CastInst>(&I);
    setDebugLocFromInst(Builder, CI);

    /// Vectorize casts.
    Type *DestTy =
        (VF == 1) ? CI->getType() : VectorType::get(CI->getType(), VF);

    for (unsigned Part = 0; Part < UF; ++Part) {
      Value *A = getOrCreateVectorValue(CI->getOperand(0), Part);
      Value *Cast = Builder.CreateCast(CI->getOpcode(), A, DestTy);
      VectorLoopValueMap.setVectorValue(&I, Part, Cast);
      addMetadata(Cast, &I);
    }
    break;
  }

  case Instruction::Call: {
    // Ignore dbg intrinsics.
    if (isa<DbgInfoIntrinsic>(I))
      break;
    setDebugLocFromInst(Builder, &I);

    Module *M = I.getParent()->getParent()->getParent();
    auto *CI = cast<CallInst>(&I);

    StringRef FnName = CI->getCalledFunction()->getName();
    Function *F = CI->getCalledFunction();
    Type *RetTy = ToVectorTy(CI->getType(), VF);
    SmallVector<Type *, 4> Tys;
    for (Value *ArgOperand : CI->arg_operands())
      Tys.push_back(ToVectorTy(ArgOperand->getType(), VF));

    Intrinsic::ID ID = getVectorIntrinsicIDForCall(CI, TLI);

    // The flag shows whether we use Intrinsic or a usual Call for vectorized
    // version of the instruction.
    // Is it beneficial to perform intrinsic call compared to lib call?
    bool NeedToScalarize;
    unsigned CallCost = getVectorCallCost(CI, VF, *TTI, TLI, NeedToScalarize);
    bool UseVectorIntrinsic =
        ID && getVectorIntrinsicCost(CI, VF, *TTI, TLI) <= CallCost;
    assert((UseVectorIntrinsic || !NeedToScalarize) &&
           "Instruction should be scalarized elsewhere.");

    for (unsigned Part = 0; Part < UF; ++Part) {
      SmallVector<Value *, 4> Args;
      for (unsigned i = 0, ie = CI->getNumArgOperands(); i != ie; ++i) {
        Value *Arg = CI->getArgOperand(i);
        // Some intrinsics have a scalar argument - don't replace it with a
        // vector.
        if (!UseVectorIntrinsic || !hasVectorInstrinsicScalarOpd(ID, i))
          Arg = getOrCreateVectorValue(CI->getArgOperand(i), Part);
        Args.push_back(Arg);
      }

      Function *VectorF;
      if (UseVectorIntrinsic) {
        // Use vector version of the intrinsic.
        Type *TysForDecl[] = {CI->getType()};
        if (VF > 1)
          TysForDecl[0] = VectorType::get(CI->getType()->getScalarType(), VF);
        VectorF = Intrinsic::getDeclaration(M, ID, TysForDecl);
      } else {
        // Use vector version of the library call.
        StringRef VFnName = TLI->getVectorizedFunction(FnName, VF);
        assert(!VFnName.empty() && "Vector function name is empty.");
        VectorF = M->getFunction(VFnName);
        if (!VectorF) {
          // Generate a declaration
          FunctionType *FTy = FunctionType::get(RetTy, Tys, false);
          VectorF =
              Function::Create(FTy, Function::ExternalLinkage, VFnName, M);
          VectorF->copyAttributesFrom(F);
        }
      }
      assert(VectorF && "Can't create vector function.");

      SmallVector<OperandBundleDef, 1> OpBundles;
      CI->getOperandBundlesAsDefs(OpBundles);
      CallInst *V = Builder.CreateCall(VectorF, Args, OpBundles);

      if (isa<FPMathOperator>(V))
        V->copyFastMathFlags(CI);

      VectorLoopValueMap.setVectorValue(&I, Part, V);
      addMetadata(V, &I);
    }

    break;
  }

  default:
    // This instruction is not vectorized by simple widening.
    LLVM_DEBUG(dbgs() << "LV: Found an unhandled instruction: " << I);
    llvm_unreachable("Unhandled instruction!");
  } // end of switch.
}

void InnerLoopVectorizer::updateAnalysis() {
  // Forget the original basic block.
  PSE.getSE()->forgetLoop(OrigLoop);

  // DT is not kept up-to-date for outer loop vectorization
  if (EnableVPlanNativePath)
    return;

  // Update the dominator tree information.
  assert(DT->properlyDominates(LoopBypassBlocks.front(), LoopExitBlock) &&
         "Entry does not dominate exit.");

  DT->addNewBlock(LoopMiddleBlock,
                  LI->getLoopFor(LoopVectorBody)->getLoopLatch());
  DT->addNewBlock(LoopScalarPreHeader, LoopBypassBlocks[0]);
  DT->changeImmediateDominator(LoopScalarBody, LoopScalarPreHeader);
  DT->changeImmediateDominator(LoopExitBlock, LoopBypassBlocks[0]);
  assert(DT->verify(DominatorTree::VerificationLevel::Fast));
}

void LoopVectorizationCostModel::collectLoopScalars(unsigned VF) {
  // We should not collect Scalars more than once per VF. Right now, this
  // function is called from collectUniformsAndScalars(), which already does
  // this check. Collecting Scalars for VF=1 does not make any sense.
  assert(VF >= 2 && Scalars.find(VF) == Scalars.end() &&
         "This function should not be visited twice for the same VF");

  SmallSetVector<Instruction *, 8> Worklist;

  // These sets are used to seed the analysis with pointers used by memory
  // accesses that will remain scalar.
  SmallSetVector<Instruction *, 8> ScalarPtrs;
  SmallPtrSet<Instruction *, 8> PossibleNonScalarPtrs;

  // A helper that returns true if the use of Ptr by MemAccess will be scalar.
  // The pointer operands of loads and stores will be scalar as long as the
  // memory access is not a gather or scatter operation. The value operand of a
  // store will remain scalar if the store is scalarized.
  auto isScalarUse = [&](Instruction *MemAccess, Value *Ptr) {
    InstWidening WideningDecision = getWideningDecision(MemAccess, VF);
    assert(WideningDecision != CM_Unknown &&
           "Widening decision should be ready at this moment");
    if (auto *Store = dyn_cast<StoreInst>(MemAccess))
      if (Ptr == Store->getValueOperand())
        return WideningDecision == CM_Scalarize;
    assert(Ptr == getLoadStorePointerOperand(MemAccess) &&
           "Ptr is neither a value or pointer operand");
    return WideningDecision != CM_GatherScatter;
  };

  // A helper that returns true if the given value is a bitcast or
  // getelementptr instruction contained in the loop.
  auto isLoopVaryingBitCastOrGEP = [&](Value *V) {
    return ((isa<BitCastInst>(V) && V->getType()->isPointerTy()) ||
            isa<GetElementPtrInst>(V)) &&
           !TheLoop->isLoopInvariant(V);
  };

  // A helper that evaluates a memory access's use of a pointer. If the use
  // will be a scalar use, and the pointer is only used by memory accesses, we
  // place the pointer in ScalarPtrs. Otherwise, the pointer is placed in
  // PossibleNonScalarPtrs.
  auto evaluatePtrUse = [&](Instruction *MemAccess, Value *Ptr) {
    // We only care about bitcast and getelementptr instructions contained in
    // the loop.
    if (!isLoopVaryingBitCastOrGEP(Ptr))
      return;

    // If the pointer has already been identified as scalar (e.g., if it was
    // also identified as uniform), there's nothing to do.
    auto *I = cast<Instruction>(Ptr);
    if (Worklist.count(I))
      return;

    // If the use of the pointer will be a scalar use, and all users of the
    // pointer are memory accesses, place the pointer in ScalarPtrs. Otherwise,
    // place the pointer in PossibleNonScalarPtrs.
    if (isScalarUse(MemAccess, Ptr) && llvm::all_of(I->users(), [&](User *U) {
          return isa<LoadInst>(U) || isa<StoreInst>(U);
        }))
      ScalarPtrs.insert(I);
    else
      PossibleNonScalarPtrs.insert(I);
  };

  // We seed the scalars analysis with three classes of instructions: (1)
  // instructions marked uniform-after-vectorization, (2) bitcast and
  // getelementptr instructions used by memory accesses requiring a scalar use,
  // and (3) pointer induction variables and their update instructions (we
  // currently only scalarize these).
  //
  // (1) Add to the worklist all instructions that have been identified as
  // uniform-after-vectorization.
  Worklist.insert(Uniforms[VF].begin(), Uniforms[VF].end());

  // (2) Add to the worklist all bitcast and getelementptr instructions used by
  // memory accesses requiring a scalar use. The pointer operands of loads and
  // stores will be scalar as long as the memory accesses is not a gather or
  // scatter operation. The value operand of a store will remain scalar if the
  // store is scalarized.
  for (auto *BB : TheLoop->blocks())
    for (auto &I : *BB) {
      if (auto *Load = dyn_cast<LoadInst>(&I)) {
        evaluatePtrUse(Load, Load->getPointerOperand());
      } else if (auto *Store = dyn_cast<StoreInst>(&I)) {
        evaluatePtrUse(Store, Store->getPointerOperand());
        evaluatePtrUse(Store, Store->getValueOperand());
      }
    }
  for (auto *I : ScalarPtrs)
    if (PossibleNonScalarPtrs.find(I) == PossibleNonScalarPtrs.end()) {
      LLVM_DEBUG(dbgs() << "LV: Found scalar instruction: " << *I << "\n");
      Worklist.insert(I);
    }

  // (3) Add to the worklist all pointer induction variables and their update
  // instructions.
  //
  // TODO: Once we are able to vectorize pointer induction variables we should
  //       no longer insert them into the worklist here.
  auto *Latch = TheLoop->getLoopLatch();
  for (auto &Induction : *Legal->getInductionVars()) {
    auto *Ind = Induction.first;
    auto *IndUpdate = cast<Instruction>(Ind->getIncomingValueForBlock(Latch));
    if (Induction.second.getKind() != InductionDescriptor::IK_PtrInduction)
      continue;
    Worklist.insert(Ind);
    Worklist.insert(IndUpdate);
    LLVM_DEBUG(dbgs() << "LV: Found scalar instruction: " << *Ind << "\n");
    LLVM_DEBUG(dbgs() << "LV: Found scalar instruction: " << *IndUpdate
                      << "\n");
  }

  // Insert the forced scalars.
  // FIXME: Currently widenPHIInstruction() often creates a dead vector
  // induction variable when the PHI user is scalarized.
  auto ForcedScalar = ForcedScalars.find(VF);
  if (ForcedScalar != ForcedScalars.end())
    for (auto *I : ForcedScalar->second)
      Worklist.insert(I);

  // Expand the worklist by looking through any bitcasts and getelementptr
  // instructions we've already identified as scalar. This is similar to the
  // expansion step in collectLoopUniforms(); however, here we're only
  // expanding to include additional bitcasts and getelementptr instructions.
  unsigned Idx = 0;
  while (Idx != Worklist.size()) {
    Instruction *Dst = Worklist[Idx++];
    if (!isLoopVaryingBitCastOrGEP(Dst->getOperand(0)))
      continue;
    auto *Src = cast<Instruction>(Dst->getOperand(0));
    if (llvm::all_of(Src->users(), [&](User *U) -> bool {
          auto *J = cast<Instruction>(U);
          return !TheLoop->contains(J) || Worklist.count(J) ||
                 ((isa<LoadInst>(J) || isa<StoreInst>(J)) &&
                  isScalarUse(J, Src));
        })) {
      Worklist.insert(Src);
      LLVM_DEBUG(dbgs() << "LV: Found scalar instruction: " << *Src << "\n");
    }
  }

  // An induction variable will remain scalar if all users of the induction
  // variable and induction variable update remain scalar.
  for (auto &Induction : *Legal->getInductionVars()) {
    auto *Ind = Induction.first;
    auto *IndUpdate = cast<Instruction>(Ind->getIncomingValueForBlock(Latch));

    // We already considered pointer induction variables, so there's no reason
    // to look at their users again.
    //
    // TODO: Once we are able to vectorize pointer induction variables we
    //       should no longer skip over them here.
    if (Induction.second.getKind() == InductionDescriptor::IK_PtrInduction)
      continue;

    // Determine if all users of the induction variable are scalar after
    // vectorization.
    auto ScalarInd = llvm::all_of(Ind->users(), [&](User *U) -> bool {
      auto *I = cast<Instruction>(U);
      return I == IndUpdate || !TheLoop->contains(I) || Worklist.count(I);
    });
    if (!ScalarInd)
      continue;

    // Determine if all users of the induction variable update instruction are
    // scalar after vectorization.
    auto ScalarIndUpdate =
        llvm::all_of(IndUpdate->users(), [&](User *U) -> bool {
          auto *I = cast<Instruction>(U);
          return I == Ind || !TheLoop->contains(I) || Worklist.count(I);
        });
    if (!ScalarIndUpdate)
      continue;

    // The induction variable and its update instruction will remain scalar.
    Worklist.insert(Ind);
    Worklist.insert(IndUpdate);
    LLVM_DEBUG(dbgs() << "LV: Found scalar instruction: " << *Ind << "\n");
    LLVM_DEBUG(dbgs() << "LV: Found scalar instruction: " << *IndUpdate
                      << "\n");
  }

  Scalars[VF].insert(Worklist.begin(), Worklist.end());
}

bool LoopVectorizationCostModel::isScalarWithPredication(Instruction *I, unsigned VF) {
  if (!blockNeedsPredication(I->getParent()))
    return false;
  switch(I->getOpcode()) {
  default:
    break;
  case Instruction::Load:
  case Instruction::Store: {
    if (!Legal->isMaskRequired(I))
      return false;
    auto *Ptr = getLoadStorePointerOperand(I);
    auto *Ty = getMemInstValueType(I);
    // We have already decided how to vectorize this instruction, get that
    // result.
    if (VF > 1) {
      InstWidening WideningDecision = getWideningDecision(I, VF);
      assert(WideningDecision != CM_Unknown &&
             "Widening decision should be ready at this moment");
      return WideningDecision == CM_Scalarize;
    }
    return isa<LoadInst>(I) ?
        !(isLegalMaskedLoad(Ty, Ptr)  || isLegalMaskedGather(Ty))
      : !(isLegalMaskedStore(Ty, Ptr) || isLegalMaskedScatter(Ty));
  }
  case Instruction::UDiv:
  case Instruction::SDiv:
  case Instruction::SRem:
  case Instruction::URem:
    return mayDivideByZero(*I);
  }
  return false;
}

bool LoopVectorizationCostModel::interleavedAccessCanBeWidened(Instruction *I,
                                                               unsigned VF) {
  assert(isAccessInterleaved(I) && "Expecting interleaved access.");
  assert(getWideningDecision(I, VF) == CM_Unknown &&
         "Decision should not be set yet.");
  auto *Group = getInterleavedAccessGroup(I);
  assert(Group && "Must have a group.");

  // Check if masking is required.
  // A Group may need masking for one of two reasons: it resides in a block that
  // needs predication, or it was decided to use masking to deal with gaps.
  bool PredicatedAccessRequiresMasking = 
      Legal->blockNeedsPredication(I->getParent()) && Legal->isMaskRequired(I);
  bool AccessWithGapsRequiresMasking = 
      Group->requiresScalarEpilogue() && !IsScalarEpilogueAllowed;
  if (!PredicatedAccessRequiresMasking && !AccessWithGapsRequiresMasking)
    return true;

  // If masked interleaving is required, we expect that the user/target had
  // enabled it, because otherwise it either wouldn't have been created or
  // it should have been invalidated by the CostModel.
  assert(useMaskedInterleavedAccesses(TTI) &&
         "Masked interleave-groups for predicated accesses are not enabled.");

  auto *Ty = getMemInstValueType(I);
  return isa<LoadInst>(I) ? TTI.isLegalMaskedLoad(Ty)
                          : TTI.isLegalMaskedStore(Ty);
}

bool LoopVectorizationCostModel::memoryInstructionCanBeWidened(Instruction *I,
                                                               unsigned VF) {
  // Get and ensure we have a valid memory instruction.
  LoadInst *LI = dyn_cast<LoadInst>(I);
  StoreInst *SI = dyn_cast<StoreInst>(I);
  assert((LI || SI) && "Invalid memory instruction");

  auto *Ptr = getLoadStorePointerOperand(I);

  // In order to be widened, the pointer should be consecutive, first of all.
  if (!Legal->isConsecutivePtr(Ptr))
    return false;

  // If the instruction is a store located in a predicated block, it will be
  // scalarized.
  if (isScalarWithPredication(I))
    return false;

  // If the instruction's allocated size doesn't equal it's type size, it
  // requires padding and will be scalarized.
  auto &DL = I->getModule()->getDataLayout();
  auto *ScalarTy = LI ? LI->getType() : SI->getValueOperand()->getType();
  if (hasIrregularType(ScalarTy, DL, VF))
    return false;

  return true;
}

void LoopVectorizationCostModel::collectLoopUniforms(unsigned VF) {
  // We should not collect Uniforms more than once per VF. Right now,
  // this function is called from collectUniformsAndScalars(), which
  // already does this check. Collecting Uniforms for VF=1 does not make any
  // sense.

  assert(VF >= 2 && Uniforms.find(VF) == Uniforms.end() &&
         "This function should not be visited twice for the same VF");

  // Visit the list of Uniforms. If we'll not find any uniform value, we'll
  // not analyze again.  Uniforms.count(VF) will return 1.
  Uniforms[VF].clear();

  // We now know that the loop is vectorizable!
  // Collect instructions inside the loop that will remain uniform after
  // vectorization.

  // Global values, params and instructions outside of current loop are out of
  // scope.
  auto isOutOfScope = [&](Value *V) -> bool {
    Instruction *I = dyn_cast<Instruction>(V);
    return (!I || !TheLoop->contains(I));
  };

  SetVector<Instruction *> Worklist;
  BasicBlock *Latch = TheLoop->getLoopLatch();

  // Start with the conditional branch. If the branch condition is an
  // instruction contained in the loop that is only used by the branch, it is
  // uniform.
  auto *Cmp = dyn_cast<Instruction>(Latch->getTerminator()->getOperand(0));
  if (Cmp && TheLoop->contains(Cmp) && Cmp->hasOneUse()) {
    Worklist.insert(Cmp);
    LLVM_DEBUG(dbgs() << "LV: Found uniform instruction: " << *Cmp << "\n");
  }

  // Holds consecutive and consecutive-like pointers. Consecutive-like pointers
  // are pointers that are treated like consecutive pointers during
  // vectorization. The pointer operands of interleaved accesses are an
  // example.
  SmallSetVector<Instruction *, 8> ConsecutiveLikePtrs;

  // Holds pointer operands of instructions that are possibly non-uniform.
  SmallPtrSet<Instruction *, 8> PossibleNonUniformPtrs;

  auto isUniformDecision = [&](Instruction *I, unsigned VF) {
    InstWidening WideningDecision = getWideningDecision(I, VF);
    assert(WideningDecision != CM_Unknown &&
           "Widening decision should be ready at this moment");

    return (WideningDecision == CM_Widen ||
            WideningDecision == CM_Widen_Reverse ||
            WideningDecision == CM_Interleave);
  };
  // Iterate over the instructions in the loop, and collect all
  // consecutive-like pointer operands in ConsecutiveLikePtrs. If it's possible
  // that a consecutive-like pointer operand will be scalarized, we collect it
  // in PossibleNonUniformPtrs instead. We use two sets here because a single
  // getelementptr instruction can be used by both vectorized and scalarized
  // memory instructions. For example, if a loop loads and stores from the same
  // location, but the store is conditional, the store will be scalarized, and
  // the getelementptr won't remain uniform.
  for (auto *BB : TheLoop->blocks())
    for (auto &I : *BB) {
      // If there's no pointer operand, there's nothing to do.
      auto *Ptr = dyn_cast_or_null<Instruction>(getLoadStorePointerOperand(&I));
      if (!Ptr)
        continue;

      // True if all users of Ptr are memory accesses that have Ptr as their
      // pointer operand.
      auto UsersAreMemAccesses =
          llvm::all_of(Ptr->users(), [&](User *U) -> bool {
            return getLoadStorePointerOperand(U) == Ptr;
          });

      // Ensure the memory instruction will not be scalarized or used by
      // gather/scatter, making its pointer operand non-uniform. If the pointer
      // operand is used by any instruction other than a memory access, we
      // conservatively assume the pointer operand may be non-uniform.
      if (!UsersAreMemAccesses || !isUniformDecision(&I, VF))
        PossibleNonUniformPtrs.insert(Ptr);

      // If the memory instruction will be vectorized and its pointer operand
      // is consecutive-like, or interleaving - the pointer operand should
      // remain uniform.
      else
        ConsecutiveLikePtrs.insert(Ptr);
    }

  // Add to the Worklist all consecutive and consecutive-like pointers that
  // aren't also identified as possibly non-uniform.
  for (auto *V : ConsecutiveLikePtrs)
    if (PossibleNonUniformPtrs.find(V) == PossibleNonUniformPtrs.end()) {
      LLVM_DEBUG(dbgs() << "LV: Found uniform instruction: " << *V << "\n");
      Worklist.insert(V);
    }

  // Expand Worklist in topological order: whenever a new instruction
  // is added , its users should be already inside Worklist.  It ensures
  // a uniform instruction will only be used by uniform instructions.
  unsigned idx = 0;
  while (idx != Worklist.size()) {
    Instruction *I = Worklist[idx++];

    for (auto OV : I->operand_values()) {
      // isOutOfScope operands cannot be uniform instructions.
      if (isOutOfScope(OV))
        continue;
      // First order recurrence Phi's should typically be considered
      // non-uniform.
      auto *OP = dyn_cast<PHINode>(OV);
      if (OP && Legal->isFirstOrderRecurrence(OP))
        continue;
      // If all the users of the operand are uniform, then add the
      // operand into the uniform worklist.
      auto *OI = cast<Instruction>(OV);
      if (llvm::all_of(OI->users(), [&](User *U) -> bool {
            auto *J = cast<Instruction>(U);
            return Worklist.count(J) ||
                   (OI == getLoadStorePointerOperand(J) &&
                    isUniformDecision(J, VF));
          })) {
        Worklist.insert(OI);
        LLVM_DEBUG(dbgs() << "LV: Found uniform instruction: " << *OI << "\n");
      }
    }
  }

  // Returns true if Ptr is the pointer operand of a memory access instruction
  // I, and I is known to not require scalarization.
  auto isVectorizedMemAccessUse = [&](Instruction *I, Value *Ptr) -> bool {
    return getLoadStorePointerOperand(I) == Ptr && isUniformDecision(I, VF);
  };

  // For an instruction to be added into Worklist above, all its users inside
  // the loop should also be in Worklist. However, this condition cannot be
  // true for phi nodes that form a cyclic dependence. We must process phi
  // nodes separately. An induction variable will remain uniform if all users
  // of the induction variable and induction variable update remain uniform.
  // The code below handles both pointer and non-pointer induction variables.
  for (auto &Induction : *Legal->getInductionVars()) {
    auto *Ind = Induction.first;
    auto *IndUpdate = cast<Instruction>(Ind->getIncomingValueForBlock(Latch));

    // Determine if all users of the induction variable are uniform after
    // vectorization.
    auto UniformInd = llvm::all_of(Ind->users(), [&](User *U) -> bool {
      auto *I = cast<Instruction>(U);
      return I == IndUpdate || !TheLoop->contains(I) || Worklist.count(I) ||
             isVectorizedMemAccessUse(I, Ind);
    });
    if (!UniformInd)
      continue;

    // Determine if all users of the induction variable update instruction are
    // uniform after vectorization.
    auto UniformIndUpdate =
        llvm::all_of(IndUpdate->users(), [&](User *U) -> bool {
          auto *I = cast<Instruction>(U);
          return I == Ind || !TheLoop->contains(I) || Worklist.count(I) ||
                 isVectorizedMemAccessUse(I, IndUpdate);
        });
    if (!UniformIndUpdate)
      continue;

    // The induction variable and its update instruction will remain uniform.
    Worklist.insert(Ind);
    Worklist.insert(IndUpdate);
    LLVM_DEBUG(dbgs() << "LV: Found uniform instruction: " << *Ind << "\n");
    LLVM_DEBUG(dbgs() << "LV: Found uniform instruction: " << *IndUpdate
                      << "\n");
  }

  Uniforms[VF].insert(Worklist.begin(), Worklist.end());
}

Optional<unsigned> LoopVectorizationCostModel::computeMaxVF(bool OptForSize) {
  if (Legal->getRuntimePointerChecking()->Need && TTI.hasBranchDivergence()) {
    // TODO: It may by useful to do since it's still likely to be dynamically
    // uniform if the target can skip.
    LLVM_DEBUG(
        dbgs() << "LV: Not inserting runtime ptr check for divergent target");

    ORE->emit(
      createMissedAnalysis("CantVersionLoopWithDivergentTarget")
      << "runtime pointer checks needed. Not enabled for divergent target");

    return None;
  }

  unsigned TC = PSE.getSE()->getSmallConstantTripCount(TheLoop);
  if (!OptForSize) // Remaining checks deal with scalar loop when OptForSize.
    return computeFeasibleMaxVF(OptForSize, TC);

  if (Legal->getRuntimePointerChecking()->Need) {
    ORE->emit(createMissedAnalysis("CantVersionLoopWithOptForSize")
              << "runtime pointer checks needed. Enable vectorization of this "
                 "loop with '#pragma clang loop vectorize(enable)' when "
                 "compiling with -Os/-Oz");
    LLVM_DEBUG(
        dbgs()
        << "LV: Aborting. Runtime ptr check is required with -Os/-Oz.\n");
    return None;
  }

  if (!PSE.getUnionPredicate().getPredicates().empty()) {
    ORE->emit(createMissedAnalysis("CantVersionLoopWithOptForSize")
              << "runtime SCEV checks needed. Enable vectorization of this "
                 "loop with '#pragma clang loop vectorize(enable)' when "
                 "compiling with -Os/-Oz");
    LLVM_DEBUG(
        dbgs()
        << "LV: Aborting. Runtime SCEV check is required with -Os/-Oz.\n");
    return None;
  }

  // FIXME: Avoid specializing for stride==1 instead of bailing out.
  if (!Legal->getLAI()->getSymbolicStrides().empty()) {
    ORE->emit(createMissedAnalysis("CantVersionLoopWithOptForSize")
              << "runtime stride == 1 checks needed. Enable vectorization of "
                 "this loop with '#pragma clang loop vectorize(enable)' when "
                 "compiling with -Os/-Oz");
    LLVM_DEBUG(
        dbgs()
        << "LV: Aborting. Runtime stride check is required with -Os/-Oz.\n");
    return None;
  }

  // If we optimize the program for size, avoid creating the tail loop.
  LLVM_DEBUG(dbgs() << "LV: Found trip count: " << TC << '\n');

  if (TC == 1) {
    ORE->emit(createMissedAnalysis("SingleIterationLoop")
              << "loop trip count is one, irrelevant for vectorization");
    LLVM_DEBUG(dbgs() << "LV: Aborting, single iteration (non) loop.\n");
    return None;
  }

  // Record that scalar epilogue is not allowed.
  LLVM_DEBUG(dbgs() << "LV: Not allowing scalar epilogue due to -Os/-Oz.\n");

  IsScalarEpilogueAllowed = !OptForSize;

  // We don't create an epilogue when optimizing for size.
  // Invalidate interleave groups that require an epilogue if we can't mask
  // the interleave-group.
  if (!useMaskedInterleavedAccesses(TTI)) 
    InterleaveInfo.invalidateGroupsRequiringScalarEpilogue();

  unsigned MaxVF = computeFeasibleMaxVF(OptForSize, TC);

  if (TC > 0 && TC % MaxVF == 0) {
    LLVM_DEBUG(dbgs() << "LV: No tail will remain for any chosen VF.\n");
    return MaxVF;
  }

  // If we don't know the precise trip count, or if the trip count that we
  // found modulo the vectorization factor is not zero, try to fold the tail
  // by masking.
  // FIXME: look for a smaller MaxVF that does divide TC rather than masking.
  if (Legal->canFoldTailByMasking()) {
    FoldTailByMasking = true;
    return MaxVF;
  }

  if (TC == 0) {
    ORE->emit(
        createMissedAnalysis("UnknownLoopCountComplexCFG")
        << "unable to calculate the loop count due to complex control flow");
    return None;
  }

  ORE->emit(createMissedAnalysis("NoTailLoopWithOptForSize")
            << "cannot optimize for size and vectorize at the same time. "
               "Enable vectorization of this loop with '#pragma clang loop "
               "vectorize(enable)' when compiling with -Os/-Oz");
  return None;
}

unsigned
LoopVectorizationCostModel::computeFeasibleMaxVF(bool OptForSize,
                                                 unsigned ConstTripCount) {
  MinBWs = computeMinimumValueSizes(TheLoop->getBlocks(), *DB, &TTI);
  unsigned SmallestType, WidestType;
  std::tie(SmallestType, WidestType) = getSmallestAndWidestTypes();
  unsigned WidestRegister = TTI.getRegisterBitWidth(true);

  // Get the maximum safe dependence distance in bits computed by LAA.
  // It is computed by MaxVF * sizeOf(type) * 8, where type is taken from
  // the memory accesses that is most restrictive (involved in the smallest
  // dependence distance).
  unsigned MaxSafeRegisterWidth = Legal->getMaxSafeRegisterWidth();

  WidestRegister = std::min(WidestRegister, MaxSafeRegisterWidth);

  unsigned MaxVectorSize = WidestRegister / WidestType;

  LLVM_DEBUG(dbgs() << "LV: The Smallest and Widest types: " << SmallestType
                    << " / " << WidestType << " bits.\n");
  LLVM_DEBUG(dbgs() << "LV: The Widest register safe to use is: "
                    << WidestRegister << " bits.\n");

  assert(MaxVectorSize <= 256 && "Did not expect to pack so many elements"
                                 " into one vector!");
  if (MaxVectorSize == 0) {
    LLVM_DEBUG(dbgs() << "LV: The target has no vector registers.\n");
    MaxVectorSize = 1;
    return MaxVectorSize;
  } else if (ConstTripCount && ConstTripCount < MaxVectorSize &&
             isPowerOf2_32(ConstTripCount)) {
    // We need to clamp the VF to be the ConstTripCount. There is no point in
    // choosing a higher viable VF as done in the loop below.
    LLVM_DEBUG(dbgs() << "LV: Clamping the MaxVF to the constant trip count: "
                      << ConstTripCount << "\n");
    MaxVectorSize = ConstTripCount;
    return MaxVectorSize;
  }

  unsigned MaxVF = MaxVectorSize;
  if (TTI.shouldMaximizeVectorBandwidth(OptForSize) ||
      (MaximizeBandwidth && !OptForSize)) {
    // Collect all viable vectorization factors larger than the default MaxVF
    // (i.e. MaxVectorSize).
    SmallVector<unsigned, 8> VFs;
    unsigned NewMaxVectorSize = WidestRegister / SmallestType;
    for (unsigned VS = MaxVectorSize * 2; VS <= NewMaxVectorSize; VS *= 2)
      VFs.push_back(VS);

    // For each VF calculate its register usage.
    auto RUs = calculateRegisterUsage(VFs);

    // Select the largest VF which doesn't require more registers than existing
    // ones.
    unsigned TargetNumRegisters = TTI.getNumberOfRegisters(true);
    for (int i = RUs.size() - 1; i >= 0; --i) {
      if (RUs[i].MaxLocalUsers <= TargetNumRegisters) {
        MaxVF = VFs[i];
        break;
      }
    }
    if (unsigned MinVF = TTI.getMinimumVF(SmallestType)) {
      if (MaxVF < MinVF) {
        LLVM_DEBUG(dbgs() << "LV: Overriding calculated MaxVF(" << MaxVF
                          << ") with target's minimum: " << MinVF << '\n');
        MaxVF = MinVF;
      }
    }
  }
  return MaxVF;
}

VectorizationFactor
LoopVectorizationCostModel::selectVectorizationFactor(unsigned MaxVF) {
  float Cost = expectedCost(1).first;
  const float ScalarCost = Cost;
  unsigned Width = 1;
  LLVM_DEBUG(dbgs() << "LV: Scalar loop costs: " << (int)ScalarCost << ".\n");

  bool ForceVectorization = Hints->getForce() == LoopVectorizeHints::FK_Enabled;
  if (ForceVectorization && MaxVF > 1) {
    // Ignore scalar width, because the user explicitly wants vectorization.
    // Initialize cost to max so that VF = 2 is, at least, chosen during cost
    // evaluation.
    Cost = std::numeric_limits<float>::max();
  }

  for (unsigned i = 2; i <= MaxVF; i *= 2) {
    // Notice that the vector loop needs to be executed less times, so
    // we need to divide the cost of the vector loops by the width of
    // the vector elements.
    VectorizationCostTy C = expectedCost(i);
    float VectorCost = C.first / (float)i;
    LLVM_DEBUG(dbgs() << "LV: Vector loop of width " << i
                      << " costs: " << (int)VectorCost << ".\n");
    if (!C.second && !ForceVectorization) {
      LLVM_DEBUG(
          dbgs() << "LV: Not considering vector loop of width " << i
                 << " because it will not generate any vector instructions.\n");
      continue;
    }
    if (VectorCost < Cost) {
      Cost = VectorCost;
      Width = i;
    }
  }

  if (!EnableCondStoresVectorization && NumPredStores) {
    ORE->emit(createMissedAnalysis("ConditionalStore")
              << "store that is conditionally executed prevents vectorization");
    LLVM_DEBUG(
        dbgs() << "LV: No vectorization. There are conditional stores.\n");
    Width = 1;
    Cost = ScalarCost;
  }

  LLVM_DEBUG(if (ForceVectorization && Width > 1 && Cost >= ScalarCost) dbgs()
             << "LV: Vectorization seems to be not beneficial, "
             << "but was forced by a user.\n");
  LLVM_DEBUG(dbgs() << "LV: Selecting VF: " << Width << ".\n");
  VectorizationFactor Factor = {Width, (unsigned)(Width * Cost)};
  return Factor;
}

std::pair<unsigned, unsigned>
LoopVectorizationCostModel::getSmallestAndWidestTypes() {
  unsigned MinWidth = -1U;
  unsigned MaxWidth = 8;
  const DataLayout &DL = TheFunction->getParent()->getDataLayout();

  // For each block.
  for (BasicBlock *BB : TheLoop->blocks()) {
    // For each instruction in the loop.
    for (Instruction &I : BB->instructionsWithoutDebug()) {
      Type *T = I.getType();

      // Skip ignored values.
      if (ValuesToIgnore.find(&I) != ValuesToIgnore.end())
        continue;

      // Only examine Loads, Stores and PHINodes.
      if (!isa<LoadInst>(I) && !isa<StoreInst>(I) && !isa<PHINode>(I))
        continue;

      // Examine PHI nodes that are reduction variables. Update the type to
      // account for the recurrence type.
      if (auto *PN = dyn_cast<PHINode>(&I)) {
        if (!Legal->isReductionVariable(PN))
          continue;
        RecurrenceDescriptor RdxDesc = (*Legal->getReductionVars())[PN];
        T = RdxDesc.getRecurrenceType();
      }

      // Examine the stored values.
      if (auto *ST = dyn_cast<StoreInst>(&I))
        T = ST->getValueOperand()->getType();

      // Ignore loaded pointer types and stored pointer types that are not
      // vectorizable.
      //
      // FIXME: The check here attempts to predict whether a load or store will
      //        be vectorized. We only know this for certain after a VF has
      //        been selected. Here, we assume that if an access can be
      //        vectorized, it will be. We should also look at extending this
      //        optimization to non-pointer types.
      //
      if (T->isPointerTy() && !isConsecutiveLoadOrStore(&I) &&
          !isAccessInterleaved(&I) && !isLegalGatherOrScatter(&I))
        continue;

      MinWidth = std::min(MinWidth,
                          (unsigned)DL.getTypeSizeInBits(T->getScalarType()));
      MaxWidth = std::max(MaxWidth,
                          (unsigned)DL.getTypeSizeInBits(T->getScalarType()));
    }
  }

  return {MinWidth, MaxWidth};
}

unsigned LoopVectorizationCostModel::selectInterleaveCount(bool OptForSize,
                                                           unsigned VF,
                                                           unsigned LoopCost) {
  // -- The interleave heuristics --
  // We interleave the loop in order to expose ILP and reduce the loop overhead.
  // There are many micro-architectural considerations that we can't predict
  // at this level. For example, frontend pressure (on decode or fetch) due to
  // code size, or the number and capabilities of the execution ports.
  //
  // We use the following heuristics to select the interleave count:
  // 1. If the code has reductions, then we interleave to break the cross
  // iteration dependency.
  // 2. If the loop is really small, then we interleave to reduce the loop
  // overhead.
  // 3. We don't interleave if we think that we will spill registers to memory
  // due to the increased register pressure.

  // When we optimize for size, we don't interleave.
  if (OptForSize)
    return 1;

  // We used the distance for the interleave count.
  if (Legal->getMaxSafeDepDistBytes() != -1U)
    return 1;

  // Do not interleave loops with a relatively small trip count.
  unsigned TC = PSE.getSE()->getSmallConstantTripCount(TheLoop);
  if (TC > 1 && TC < TinyTripCountInterleaveThreshold)
    return 1;

  unsigned TargetNumRegisters = TTI.getNumberOfRegisters(VF > 1);
  LLVM_DEBUG(dbgs() << "LV: The target has " << TargetNumRegisters
                    << " registers\n");

  if (VF == 1) {
    if (ForceTargetNumScalarRegs.getNumOccurrences() > 0)
      TargetNumRegisters = ForceTargetNumScalarRegs;
  } else {
    if (ForceTargetNumVectorRegs.getNumOccurrences() > 0)
      TargetNumRegisters = ForceTargetNumVectorRegs;
  }

  RegisterUsage R = calculateRegisterUsage({VF})[0];
  // We divide by these constants so assume that we have at least one
  // instruction that uses at least one register.
  R.MaxLocalUsers = std::max(R.MaxLocalUsers, 1U);

  // We calculate the interleave count using the following formula.
  // Subtract the number of loop invariants from the number of available
  // registers. These registers are used by all of the interleaved instances.
  // Next, divide the remaining registers by the number of registers that is
  // required by the loop, in order to estimate how many parallel instances
  // fit without causing spills. All of this is rounded down if necessary to be
  // a power of two. We want power of two interleave count to simplify any
  // addressing operations or alignment considerations.
  // We also want power of two interleave counts to ensure that the induction
  // variable of the vector loop wraps to zero, when tail is folded by masking;
  // this currently happens when OptForSize, in which case IC is set to 1 above.
  unsigned IC = PowerOf2Floor((TargetNumRegisters - R.LoopInvariantRegs) /
                              R.MaxLocalUsers);

  // Don't count the induction variable as interleaved.
  if (EnableIndVarRegisterHeur)
    IC = PowerOf2Floor((TargetNumRegisters - R.LoopInvariantRegs - 1) /
                       std::max(1U, (R.MaxLocalUsers - 1)));

  // Clamp the interleave ranges to reasonable counts.
  unsigned MaxInterleaveCount = TTI.getMaxInterleaveFactor(VF);

  // Check if the user has overridden the max.
  if (VF == 1) {
    if (ForceTargetMaxScalarInterleaveFactor.getNumOccurrences() > 0)
      MaxInterleaveCount = ForceTargetMaxScalarInterleaveFactor;
  } else {
    if (ForceTargetMaxVectorInterleaveFactor.getNumOccurrences() > 0)
      MaxInterleaveCount = ForceTargetMaxVectorInterleaveFactor;
  }

  // If we did not calculate the cost for VF (because the user selected the VF)
  // then we calculate the cost of VF here.
  if (LoopCost == 0)
    LoopCost = expectedCost(VF).first;

  // Clamp the calculated IC to be between the 1 and the max interleave count
  // that the target allows.
  if (IC > MaxInterleaveCount)
    IC = MaxInterleaveCount;
  else if (IC < 1)
    IC = 1;

  // Interleave if we vectorized this loop and there is a reduction that could
  // benefit from interleaving.
  if (VF > 1 && !Legal->getReductionVars()->empty()) {
    LLVM_DEBUG(dbgs() << "LV: Interleaving because of reductions.\n");
    return IC;
  }

  // Note that if we've already vectorized the loop we will have done the
  // runtime check and so interleaving won't require further checks.
  bool InterleavingRequiresRuntimePointerCheck =
      (VF == 1 && Legal->getRuntimePointerChecking()->Need);

  // We want to interleave small loops in order to reduce the loop overhead and
  // potentially expose ILP opportunities.
  LLVM_DEBUG(dbgs() << "LV: Loop cost is " << LoopCost << '\n');
  if (!InterleavingRequiresRuntimePointerCheck && LoopCost < SmallLoopCost) {
    // We assume that the cost overhead is 1 and we use the cost model
    // to estimate the cost of the loop and interleave until the cost of the
    // loop overhead is about 5% of the cost of the loop.
    unsigned SmallIC =
        std::min(IC, (unsigned)PowerOf2Floor(SmallLoopCost / LoopCost));

    // Interleave until store/load ports (estimated by max interleave count) are
    // saturated.
    unsigned NumStores = Legal->getNumStores();
    unsigned NumLoads = Legal->getNumLoads();
    unsigned StoresIC = IC / (NumStores ? NumStores : 1);
    unsigned LoadsIC = IC / (NumLoads ? NumLoads : 1);

    // If we have a scalar reduction (vector reductions are already dealt with
    // by this point), we can increase the critical path length if the loop
    // we're interleaving is inside another loop. Limit, by default to 2, so the
    // critical path only gets increased by one reduction operation.
    if (!Legal->getReductionVars()->empty() && TheLoop->getLoopDepth() > 1) {
      unsigned F = static_cast<unsigned>(MaxNestedScalarReductionIC);
      SmallIC = std::min(SmallIC, F);
      StoresIC = std::min(StoresIC, F);
      LoadsIC = std::min(LoadsIC, F);
    }

    if (EnableLoadStoreRuntimeInterleave &&
        std::max(StoresIC, LoadsIC) > SmallIC) {
      LLVM_DEBUG(
          dbgs() << "LV: Interleaving to saturate store or load ports.\n");
      return std::max(StoresIC, LoadsIC);
    }

    LLVM_DEBUG(dbgs() << "LV: Interleaving to reduce branch cost.\n");
    return SmallIC;
  }

  // Interleave if this is a large loop (small loops are already dealt with by
  // this point) that could benefit from interleaving.
  bool HasReductions = !Legal->getReductionVars()->empty();
  if (TTI.enableAggressiveInterleaving(HasReductions)) {
    LLVM_DEBUG(dbgs() << "LV: Interleaving to expose ILP.\n");
    return IC;
  }

  LLVM_DEBUG(dbgs() << "LV: Not Interleaving.\n");
  return 1;
}

SmallVector<LoopVectorizationCostModel::RegisterUsage, 8>
LoopVectorizationCostModel::calculateRegisterUsage(ArrayRef<unsigned> VFs) {
  // This function calculates the register usage by measuring the highest number
  // of values that are alive at a single location. Obviously, this is a very
  // rough estimation. We scan the loop in a topological order in order and
  // assign a number to each instruction. We use RPO to ensure that defs are
  // met before their users. We assume that each instruction that has in-loop
  // users starts an interval. We record every time that an in-loop value is
  // used, so we have a list of the first and last occurrences of each
  // instruction. Next, we transpose this data structure into a multi map that
  // holds the list of intervals that *end* at a specific location. This multi
  // map allows us to perform a linear search. We scan the instructions linearly
  // and record each time that a new interval starts, by placing it in a set.
  // If we find this value in the multi-map then we remove it from the set.
  // The max register usage is the maximum size of the set.
  // We also search for instructions that are defined outside the loop, but are
  // used inside the loop. We need this number separately from the max-interval
  // usage number because when we unroll, loop-invariant values do not take
  // more register.
  LoopBlocksDFS DFS(TheLoop);
  DFS.perform(LI);

  RegisterUsage RU;

  // Each 'key' in the map opens a new interval. The values
  // of the map are the index of the 'last seen' usage of the
  // instruction that is the key.
  using IntervalMap = DenseMap<Instruction *, unsigned>;

  // Maps instruction to its index.
  SmallVector<Instruction *, 64> IdxToInstr;
  // Marks the end of each interval.
  IntervalMap EndPoint;
  // Saves the list of instruction indices that are used in the loop.
  SmallPtrSet<Instruction *, 8> Ends;
  // Saves the list of values that are used in the loop but are
  // defined outside the loop, such as arguments and constants.
  SmallPtrSet<Value *, 8> LoopInvariants;

  for (BasicBlock *BB : make_range(DFS.beginRPO(), DFS.endRPO())) {
    for (Instruction &I : BB->instructionsWithoutDebug()) {
      IdxToInstr.push_back(&I);

      // Save the end location of each USE.
      for (Value *U : I.operands()) {
        auto *Instr = dyn_cast<Instruction>(U);

        // Ignore non-instruction values such as arguments, constants, etc.
        if (!Instr)
          continue;

        // If this instruction is outside the loop then record it and continue.
        if (!TheLoop->contains(Instr)) {
          LoopInvariants.insert(Instr);
          continue;
        }

        // Overwrite previous end points.
        EndPoint[Instr] = IdxToInstr.size();
        Ends.insert(Instr);
      }
    }
  }

  // Saves the list of intervals that end with the index in 'key'.
  using InstrList = SmallVector<Instruction *, 2>;
  DenseMap<unsigned, InstrList> TransposeEnds;

  // Transpose the EndPoints to a list of values that end at each index.
  for (auto &Interval : EndPoint)
    TransposeEnds[Interval.second].push_back(Interval.first);

  SmallPtrSet<Instruction *, 8> OpenIntervals;

  // Get the size of the widest register.
  unsigned MaxSafeDepDist = -1U;
  if (Legal->getMaxSafeDepDistBytes() != -1U)
    MaxSafeDepDist = Legal->getMaxSafeDepDistBytes() * 8;
  unsigned WidestRegister =
      std::min(TTI.getRegisterBitWidth(true), MaxSafeDepDist);
  const DataLayout &DL = TheFunction->getParent()->getDataLayout();

  SmallVector<RegisterUsage, 8> RUs(VFs.size());
  SmallVector<unsigned, 8> MaxUsages(VFs.size(), 0);

  LLVM_DEBUG(dbgs() << "LV(REG): Calculating max register usage:\n");

  // A lambda that gets the register usage for the given type and VF.
  auto GetRegUsage = [&DL, WidestRegister](Type *Ty, unsigned VF) {
    if (Ty->isTokenTy())
      return 0U;
    unsigned TypeSize = DL.getTypeSizeInBits(Ty->getScalarType());
    return std::max<unsigned>(1, VF * TypeSize / WidestRegister);
  };

  for (unsigned int i = 0, s = IdxToInstr.size(); i < s; ++i) {
    Instruction *I = IdxToInstr[i];

    // Remove all of the instructions that end at this location.
    InstrList &List = TransposeEnds[i];
    for (Instruction *ToRemove : List)
      OpenIntervals.erase(ToRemove);

    // Ignore instructions that are never used within the loop.
    if (Ends.find(I) == Ends.end())
      continue;

    // Skip ignored values.
    if (ValuesToIgnore.find(I) != ValuesToIgnore.end())
      continue;

    // For each VF find the maximum usage of registers.
    for (unsigned j = 0, e = VFs.size(); j < e; ++j) {
      if (VFs[j] == 1) {
        MaxUsages[j] = std::max(MaxUsages[j], OpenIntervals.size());
        continue;
      }
      collectUniformsAndScalars(VFs[j]);
      // Count the number of live intervals.
      unsigned RegUsage = 0;
      for (auto Inst : OpenIntervals) {
        // Skip ignored values for VF > 1.
        if (VecValuesToIgnore.find(Inst) != VecValuesToIgnore.end() ||
            isScalarAfterVectorization(Inst, VFs[j]))
          continue;
        RegUsage += GetRegUsage(Inst->getType(), VFs[j]);
      }
      MaxUsages[j] = std::max(MaxUsages[j], RegUsage);
    }

    LLVM_DEBUG(dbgs() << "LV(REG): At #" << i << " Interval # "
                      << OpenIntervals.size() << '\n');

    // Add the current instruction to the list of open intervals.
    OpenIntervals.insert(I);
  }

  for (unsigned i = 0, e = VFs.size(); i < e; ++i) {
    unsigned Invariant = 0;
    if (VFs[i] == 1)
      Invariant = LoopInvariants.size();
    else {
      for (auto Inst : LoopInvariants)
        Invariant += GetRegUsage(Inst->getType(), VFs[i]);
    }

    LLVM_DEBUG(dbgs() << "LV(REG): VF = " << VFs[i] << '\n');
    LLVM_DEBUG(dbgs() << "LV(REG): Found max usage: " << MaxUsages[i] << '\n');
    LLVM_DEBUG(dbgs() << "LV(REG): Found invariant usage: " << Invariant
                      << '\n');

    RU.LoopInvariantRegs = Invariant;
    RU.MaxLocalUsers = MaxUsages[i];
    RUs[i] = RU;
  }

  return RUs;
}

bool LoopVectorizationCostModel::useEmulatedMaskMemRefHack(Instruction *I){
  // TODO: Cost model for emulated masked load/store is completely
  // broken. This hack guides the cost model to use an artificially
  // high enough value to practically disable vectorization with such
  // operations, except where previously deployed legality hack allowed
  // using very low cost values. This is to avoid regressions coming simply
  // from moving "masked load/store" check from legality to cost model.
  // Masked Load/Gather emulation was previously never allowed.
  // Limited number of Masked Store/Scatter emulation was allowed.
  assert(isPredicatedInst(I) && "Expecting a scalar emulated instruction");
  return isa<LoadInst>(I) ||
         (isa<StoreInst>(I) &&
          NumPredStores > NumberOfStoresToPredicate);
}

void LoopVectorizationCostModel::collectInstsToScalarize(unsigned VF) {
  // If we aren't vectorizing the loop, or if we've already collected the
  // instructions to scalarize, there's nothing to do. Collection may already
  // have occurred if we have a user-selected VF and are now computing the
  // expected cost for interleaving.
  if (VF < 2 || InstsToScalarize.find(VF) != InstsToScalarize.end())
    return;

  // Initialize a mapping for VF in InstsToScalalarize. If we find that it's
  // not profitable to scalarize any instructions, the presence of VF in the
  // map will indicate that we've analyzed it already.
  ScalarCostsTy &ScalarCostsVF = InstsToScalarize[VF];

  // Find all the instructions that are scalar with predication in the loop and
  // determine if it would be better to not if-convert the blocks they are in.
  // If so, we also record the instructions to scalarize.
  for (BasicBlock *BB : TheLoop->blocks()) {
    if (!blockNeedsPredication(BB))
      continue;
    for (Instruction &I : *BB)
      if (isScalarWithPredication(&I)) {
        ScalarCostsTy ScalarCosts;
        // Do not apply discount logic if hacked cost is needed
        // for emulated masked memrefs.
        if (!useEmulatedMaskMemRefHack(&I) &&
            computePredInstDiscount(&I, ScalarCosts, VF) >= 0)
          ScalarCostsVF.insert(ScalarCosts.begin(), ScalarCosts.end());
        // Remember that BB will remain after vectorization.
        PredicatedBBsAfterVectorization.insert(BB);
      }
  }
}

int LoopVectorizationCostModel::computePredInstDiscount(
    Instruction *PredInst, DenseMap<Instruction *, unsigned> &ScalarCosts,
    unsigned VF) {
  assert(!isUniformAfterVectorization(PredInst, VF) &&
         "Instruction marked uniform-after-vectorization will be predicated");

  // Initialize the discount to zero, meaning that the scalar version and the
  // vector version cost the same.
  int Discount = 0;

  // Holds instructions to analyze. The instructions we visit are mapped in
  // ScalarCosts. Those instructions are the ones that would be scalarized if
  // we find that the scalar version costs less.
  SmallVector<Instruction *, 8> Worklist;

  // Returns true if the given instruction can be scalarized.
  auto canBeScalarized = [&](Instruction *I) -> bool {
    // We only attempt to scalarize instructions forming a single-use chain
    // from the original predicated block that would otherwise be vectorized.
    // Although not strictly necessary, we give up on instructions we know will
    // already be scalar to avoid traversing chains that are unlikely to be
    // beneficial.
    if (!I->hasOneUse() || PredInst->getParent() != I->getParent() ||
        isScalarAfterVectorization(I, VF))
      return false;

    // If the instruction is scalar with predication, it will be analyzed
    // separately. We ignore it within the context of PredInst.
    if (isScalarWithPredication(I))
      return false;

    // If any of the instruction's operands are uniform after vectorization,
    // the instruction cannot be scalarized. This prevents, for example, a
    // masked load from being scalarized.
    //
    // We assume we will only emit a value for lane zero of an instruction
    // marked uniform after vectorization, rather than VF identical values.
    // Thus, if we scalarize an instruction that uses a uniform, we would
    // create uses of values corresponding to the lanes we aren't emitting code
    // for. This behavior can be changed by allowing getScalarValue to clone
    // the lane zero values for uniforms rather than asserting.
    for (Use &U : I->operands())
      if (auto *J = dyn_cast<Instruction>(U.get()))
        if (isUniformAfterVectorization(J, VF))
          return false;

    // Otherwise, we can scalarize the instruction.
    return true;
  };

  // Returns true if an operand that cannot be scalarized must be extracted
  // from a vector. We will account for this scalarization overhead below. Note
  // that the non-void predicated instructions are placed in their own blocks,
  // and their return values are inserted into vectors. Thus, an extract would
  // still be required.
  auto needsExtract = [&](Instruction *I) -> bool {
    return TheLoop->contains(I) && !isScalarAfterVectorization(I, VF);
  };

  // Compute the expected cost discount from scalarizing the entire expression
  // feeding the predicated instruction. We currently only consider expressions
  // that are single-use instruction chains.
  Worklist.push_back(PredInst);
  while (!Worklist.empty()) {
    Instruction *I = Worklist.pop_back_val();

    // If we've already analyzed the instruction, there's nothing to do.
    if (ScalarCosts.find(I) != ScalarCosts.end())
      continue;

    // Compute the cost of the vector instruction. Note that this cost already
    // includes the scalarization overhead of the predicated instruction.
    unsigned VectorCost = getInstructionCost(I, VF).first;

    // Compute the cost of the scalarized instruction. This cost is the cost of
    // the instruction as if it wasn't if-converted and instead remained in the
    // predicated block. We will scale this cost by block probability after
    // computing the scalarization overhead.
    unsigned ScalarCost = VF * getInstructionCost(I, 1).first;

    // Compute the scalarization overhead of needed insertelement instructions
    // and phi nodes.
    if (isScalarWithPredication(I) && !I->getType()->isVoidTy()) {
      ScalarCost += TTI.getScalarizationOverhead(ToVectorTy(I->getType(), VF),
                                                 true, false);
      ScalarCost += VF * TTI.getCFInstrCost(Instruction::PHI);
    }

    // Compute the scalarization overhead of needed extractelement
    // instructions. For each of the instruction's operands, if the operand can
    // be scalarized, add it to the worklist; otherwise, account for the
    // overhead.
    for (Use &U : I->operands())
      if (auto *J = dyn_cast<Instruction>(U.get())) {
        assert(VectorType::isValidElementType(J->getType()) &&
               "Instruction has non-scalar type");
        if (canBeScalarized(J))
          Worklist.push_back(J);
        else if (needsExtract(J))
          ScalarCost += TTI.getScalarizationOverhead(
                              ToVectorTy(J->getType(),VF), false, true);
      }

    // Scale the total scalar cost by block probability.
    ScalarCost /= getReciprocalPredBlockProb();

    // Compute the discount. A non-negative discount means the vector version
    // of the instruction costs more, and scalarizing would be beneficial.
    Discount += VectorCost - ScalarCost;
    ScalarCosts[I] = ScalarCost;
  }

  return Discount;
}

LoopVectorizationCostModel::VectorizationCostTy
LoopVectorizationCostModel::expectedCost(unsigned VF) {
  VectorizationCostTy Cost;

  // For each block.
  for (BasicBlock *BB : TheLoop->blocks()) {
    VectorizationCostTy BlockCost;

    // For each instruction in the old loop.
    for (Instruction &I : BB->instructionsWithoutDebug()) {
      // Skip ignored values.
      if (ValuesToIgnore.find(&I) != ValuesToIgnore.end() ||
          (VF > 1 && VecValuesToIgnore.find(&I) != VecValuesToIgnore.end()))
        continue;

      VectorizationCostTy C = getInstructionCost(&I, VF);

      // Check if we should override the cost.
      if (ForceTargetInstructionCost.getNumOccurrences() > 0)
        C.first = ForceTargetInstructionCost;

      BlockCost.first += C.first;
      BlockCost.second |= C.second;
      LLVM_DEBUG(dbgs() << "LV: Found an estimated cost of " << C.first
                        << " for VF " << VF << " For instruction: " << I
                        << '\n');
    }

    // If we are vectorizing a predicated block, it will have been
    // if-converted. This means that the block's instructions (aside from
    // stores and instructions that may divide by zero) will now be
    // unconditionally executed. For the scalar case, we may not always execute
    // the predicated block. Thus, scale the block's cost by the probability of
    // executing it.
    if (VF == 1 && blockNeedsPredication(BB))
      BlockCost.first /= getReciprocalPredBlockProb();

    Cost.first += BlockCost.first;
    Cost.second |= BlockCost.second;
  }

  return Cost;
}

/// Gets Address Access SCEV after verifying that the access pattern
/// is loop invariant except the induction variable dependence.
///
/// This SCEV can be sent to the Target in order to estimate the address
/// calculation cost.
static const SCEV *getAddressAccessSCEV(
              Value *Ptr,
              LoopVectorizationLegality *Legal,
              PredicatedScalarEvolution &PSE,
              const Loop *TheLoop) {

  auto *Gep = dyn_cast<GetElementPtrInst>(Ptr);
  if (!Gep)
    return nullptr;

  // We are looking for a gep with all loop invariant indices except for one
  // which should be an induction variable.
  auto SE = PSE.getSE();
  unsigned NumOperands = Gep->getNumOperands();
  for (unsigned i = 1; i < NumOperands; ++i) {
    Value *Opd = Gep->getOperand(i);
    if (!SE->isLoopInvariant(SE->getSCEV(Opd), TheLoop) &&
        !Legal->isInductionVariable(Opd))
      return nullptr;
  }

  // Now we know we have a GEP ptr, %inv, %ind, %inv. return the Ptr SCEV.
  return PSE.getSCEV(Ptr);
}

static bool isStrideMul(Instruction *I, LoopVectorizationLegality *Legal) {
  return Legal->hasStride(I->getOperand(0)) ||
         Legal->hasStride(I->getOperand(1));
}

unsigned LoopVectorizationCostModel::getMemInstScalarizationCost(Instruction *I,
                                                                 unsigned VF) {
  assert(VF > 1 && "Scalarization cost of instruction implies vectorization.");
  Type *ValTy = getMemInstValueType(I);
  auto SE = PSE.getSE();

  unsigned Alignment = getLoadStoreAlignment(I);
  unsigned AS = getLoadStoreAddressSpace(I);
  Value *Ptr = getLoadStorePointerOperand(I);
  Type *PtrTy = ToVectorTy(Ptr->getType(), VF);

  // Figure out whether the access is strided and get the stride value
  // if it's known in compile time
  const SCEV *PtrSCEV = getAddressAccessSCEV(Ptr, Legal, PSE, TheLoop);

  // Get the cost of the scalar memory instruction and address computation.
  unsigned Cost = VF * TTI.getAddressComputationCost(PtrTy, SE, PtrSCEV);

  // Don't pass *I here, since it is scalar but will actually be part of a
  // vectorized loop where the user of it is a vectorized instruction.
  Cost += VF *
          TTI.getMemoryOpCost(I->getOpcode(), ValTy->getScalarType(), Alignment,
                              AS);

  // Get the overhead of the extractelement and insertelement instructions
  // we might create due to scalarization.
  Cost += getScalarizationOverhead(I, VF, TTI);

  // If we have a predicated store, it may not be executed for each vector
  // lane. Scale the cost by the probability of executing the predicated
  // block.
  if (isPredicatedInst(I)) {
    Cost /= getReciprocalPredBlockProb();

    if (useEmulatedMaskMemRefHack(I))
      // Artificially setting to a high enough value to practically disable
      // vectorization with such operations.
      Cost = 3000000;
  }

  return Cost;
}

unsigned LoopVectorizationCostModel::getConsecutiveMemOpCost(Instruction *I,
                                                             unsigned VF) {
  Type *ValTy = getMemInstValueType(I);
  Type *VectorTy = ToVectorTy(ValTy, VF);
  unsigned Alignment = getLoadStoreAlignment(I);
  Value *Ptr = getLoadStorePointerOperand(I);
  unsigned AS = getLoadStoreAddressSpace(I);
  int ConsecutiveStride = Legal->isConsecutivePtr(Ptr);

  assert((ConsecutiveStride == 1 || ConsecutiveStride == -1) &&
         "Stride should be 1 or -1 for consecutive memory access");
  unsigned Cost = 0;
  if (Legal->isMaskRequired(I))
    Cost += TTI.getMaskedMemoryOpCost(I->getOpcode(), VectorTy, Alignment, AS);
  else
    Cost += TTI.getMemoryOpCost(I->getOpcode(), VectorTy, Alignment, AS, I);

  bool Reverse = ConsecutiveStride < 0;
  if (Reverse)
    Cost += TTI.getShuffleCost(TargetTransformInfo::SK_Reverse, VectorTy, 0);
  return Cost;
}

unsigned LoopVectorizationCostModel::getUniformMemOpCost(Instruction *I,
                                                         unsigned VF) {
  Type *ValTy = getMemInstValueType(I);
  Type *VectorTy = ToVectorTy(ValTy, VF);
  unsigned Alignment = getLoadStoreAlignment(I);
  unsigned AS = getLoadStoreAddressSpace(I);
  if (isa<LoadInst>(I)) {
    return TTI.getAddressComputationCost(ValTy) +
           TTI.getMemoryOpCost(Instruction::Load, ValTy, Alignment, AS) +
           TTI.getShuffleCost(TargetTransformInfo::SK_Broadcast, VectorTy);
  }
  StoreInst *SI = cast<StoreInst>(I);

  bool isLoopInvariantStoreValue = Legal->isUniform(SI->getValueOperand());
  return TTI.getAddressComputationCost(ValTy) +
         TTI.getMemoryOpCost(Instruction::Store, ValTy, Alignment, AS) +
         (isLoopInvariantStoreValue ? 0 : TTI.getVectorInstrCost(
                                               Instruction::ExtractElement,
                                               VectorTy, VF - 1));
}

unsigned LoopVectorizationCostModel::getGatherScatterCost(Instruction *I,
                                                          unsigned VF) {
  Type *ValTy = getMemInstValueType(I);
  Type *VectorTy = ToVectorTy(ValTy, VF);
  unsigned Alignment = getLoadStoreAlignment(I);
  Value *Ptr = getLoadStorePointerOperand(I);

  return TTI.getAddressComputationCost(VectorTy) +
         TTI.getGatherScatterOpCost(I->getOpcode(), VectorTy, Ptr,
                                    Legal->isMaskRequired(I), Alignment);
}

unsigned LoopVectorizationCostModel::getInterleaveGroupCost(Instruction *I,
                                                            unsigned VF) {
  Type *ValTy = getMemInstValueType(I);
  Type *VectorTy = ToVectorTy(ValTy, VF);
  unsigned AS = getLoadStoreAddressSpace(I);

  auto Group = getInterleavedAccessGroup(I);
  assert(Group && "Fail to get an interleaved access group.");

  unsigned InterleaveFactor = Group->getFactor();
  Type *WideVecTy = VectorType::get(ValTy, VF * InterleaveFactor);

  // Holds the indices of existing members in an interleaved load group.
  // An interleaved store group doesn't need this as it doesn't allow gaps.
  SmallVector<unsigned, 4> Indices;
  if (isa<LoadInst>(I)) {
    for (unsigned i = 0; i < InterleaveFactor; i++)
      if (Group->getMember(i))
        Indices.push_back(i);
  }

  // Calculate the cost of the whole interleaved group.
  bool UseMaskForGaps = 
      Group->requiresScalarEpilogue() && !IsScalarEpilogueAllowed;
  unsigned Cost = TTI.getInterleavedMemoryOpCost(
      I->getOpcode(), WideVecTy, Group->getFactor(), Indices,
      Group->getAlignment(), AS, Legal->isMaskRequired(I), UseMaskForGaps);

  if (Group->isReverse()) {
    // TODO: Add support for reversed masked interleaved access.
    assert(!Legal->isMaskRequired(I) &&
           "Reverse masked interleaved access not supported.");
    Cost += Group->getNumMembers() *
            TTI.getShuffleCost(TargetTransformInfo::SK_Reverse, VectorTy, 0);
  }
  return Cost;
}

unsigned LoopVectorizationCostModel::getMemoryInstructionCost(Instruction *I,
                                                              unsigned VF) {
  // Calculate scalar cost only. Vectorization cost should be ready at this
  // moment.
  if (VF == 1) {
    Type *ValTy = getMemInstValueType(I);
    unsigned Alignment = getLoadStoreAlignment(I);
    unsigned AS = getLoadStoreAddressSpace(I);

    return TTI.getAddressComputationCost(ValTy) +
           TTI.getMemoryOpCost(I->getOpcode(), ValTy, Alignment, AS, I);
  }
  return getWideningCost(I, VF);
}

LoopVectorizationCostModel::VectorizationCostTy
LoopVectorizationCostModel::getInstructionCost(Instruction *I, unsigned VF) {
  // If we know that this instruction will remain uniform, check the cost of
  // the scalar version.
  if (isUniformAfterVectorization(I, VF))
    VF = 1;

  if (VF > 1 && isProfitableToScalarize(I, VF))
    return VectorizationCostTy(InstsToScalarize[VF][I], false);

  // Forced scalars do not have any scalarization overhead.
  auto ForcedScalar = ForcedScalars.find(VF);
  if (VF > 1 && ForcedScalar != ForcedScalars.end()) {
    auto InstSet = ForcedScalar->second;
    if (InstSet.find(I) != InstSet.end())
      return VectorizationCostTy((getInstructionCost(I, 1).first * VF), false);
  }

  Type *VectorTy;
  unsigned C = getInstructionCost(I, VF, VectorTy);

  bool TypeNotScalarized =
      VF > 1 && VectorTy->isVectorTy() && TTI.getNumberOfParts(VectorTy) < VF;
  return VectorizationCostTy(C, TypeNotScalarized);
}

void LoopVectorizationCostModel::setCostBasedWideningDecision(unsigned VF) {
  if (VF == 1)
    return;
  NumPredStores = 0;
  for (BasicBlock *BB : TheLoop->blocks()) {
    // For each instruction in the old loop.
    for (Instruction &I : *BB) {
      Value *Ptr =  getLoadStorePointerOperand(&I);
      if (!Ptr)
        continue;

      // TODO: We should generate better code and update the cost model for
      // predicated uniform stores. Today they are treated as any other
      // predicated store (see added test cases in
      // invariant-store-vectorization.ll).
      if (isa<StoreInst>(&I) && isScalarWithPredication(&I))
        NumPredStores++;

      if (Legal->isUniform(Ptr) &&
          // Conditional loads and stores should be scalarized and predicated.
          // isScalarWithPredication cannot be used here since masked
          // gather/scatters are not considered scalar with predication.
          !Legal->blockNeedsPredication(I.getParent())) {
        // TODO: Avoid replicating loads and stores instead of
        // relying on instcombine to remove them.
        // Load: Scalar load + broadcast
        // Store: Scalar store + isLoopInvariantStoreValue ? 0 : extract
        unsigned Cost = getUniformMemOpCost(&I, VF);
        setWideningDecision(&I, VF, CM_Scalarize, Cost);
        continue;
      }

      // We assume that widening is the best solution when possible.
      if (memoryInstructionCanBeWidened(&I, VF)) {
        unsigned Cost = getConsecutiveMemOpCost(&I, VF);
        int ConsecutiveStride =
               Legal->isConsecutivePtr(getLoadStorePointerOperand(&I));
        assert((ConsecutiveStride == 1 || ConsecutiveStride == -1) &&
               "Expected consecutive stride.");
        InstWidening Decision =
            ConsecutiveStride == 1 ? CM_Widen : CM_Widen_Reverse;
        setWideningDecision(&I, VF, Decision, Cost);
        continue;
      }

      // Choose between Interleaving, Gather/Scatter or Scalarization.
      unsigned InterleaveCost = std::numeric_limits<unsigned>::max();
      unsigned NumAccesses = 1;
      if (isAccessInterleaved(&I)) {
        auto Group = getInterleavedAccessGroup(&I);
        assert(Group && "Fail to get an interleaved access group.");

        // Make one decision for the whole group.
        if (getWideningDecision(&I, VF) != CM_Unknown)
          continue;

        NumAccesses = Group->getNumMembers();
        if (interleavedAccessCanBeWidened(&I, VF))
          InterleaveCost = getInterleaveGroupCost(&I, VF);
      }

      unsigned GatherScatterCost =
          isLegalGatherOrScatter(&I)
              ? getGatherScatterCost(&I, VF) * NumAccesses
              : std::numeric_limits<unsigned>::max();

      unsigned ScalarizationCost =
          getMemInstScalarizationCost(&I, VF) * NumAccesses;

      // Choose better solution for the current VF,
      // write down this decision and use it during vectorization.
      unsigned Cost;
      InstWidening Decision;
      if (InterleaveCost <= GatherScatterCost &&
          InterleaveCost < ScalarizationCost) {
        Decision = CM_Interleave;
        Cost = InterleaveCost;
      } else if (GatherScatterCost < ScalarizationCost) {
        Decision = CM_GatherScatter;
        Cost = GatherScatterCost;
      } else {
        Decision = CM_Scalarize;
        Cost = ScalarizationCost;
      }
      // If the instructions belongs to an interleave group, the whole group
      // receives the same decision. The whole group receives the cost, but
      // the cost will actually be assigned to one instruction.
      if (auto Group = getInterleavedAccessGroup(&I))
        setWideningDecision(Group, VF, Decision, Cost);
      else
        setWideningDecision(&I, VF, Decision, Cost);
    }
  }

  // Make sure that any load of address and any other address computation
  // remains scalar unless there is gather/scatter support. This avoids
  // inevitable extracts into address registers, and also has the benefit of
  // activating LSR more, since that pass can't optimize vectorized
  // addresses.
  if (TTI.prefersVectorizedAddressing())
    return;

  // Start with all scalar pointer uses.
  SmallPtrSet<Instruction *, 8> AddrDefs;
  for (BasicBlock *BB : TheLoop->blocks())
    for (Instruction &I : *BB) {
      Instruction *PtrDef =
        dyn_cast_or_null<Instruction>(getLoadStorePointerOperand(&I));
      if (PtrDef && TheLoop->contains(PtrDef) &&
          getWideningDecision(&I, VF) != CM_GatherScatter)
        AddrDefs.insert(PtrDef);
    }

  // Add all instructions used to generate the addresses.
  SmallVector<Instruction *, 4> Worklist;
  for (auto *I : AddrDefs)
    Worklist.push_back(I);
  while (!Worklist.empty()) {
    Instruction *I = Worklist.pop_back_val();
    for (auto &Op : I->operands())
      if (auto *InstOp = dyn_cast<Instruction>(Op))
        if ((InstOp->getParent() == I->getParent()) && !isa<PHINode>(InstOp) &&
            AddrDefs.insert(InstOp).second)
          Worklist.push_back(InstOp);
  }

  for (auto *I : AddrDefs) {
    if (isa<LoadInst>(I)) {
      // Setting the desired widening decision should ideally be handled in
      // by cost functions, but since this involves the task of finding out
      // if the loaded register is involved in an address computation, it is
      // instead changed here when we know this is the case.
      InstWidening Decision = getWideningDecision(I, VF);
      if (Decision == CM_Widen || Decision == CM_Widen_Reverse)
        // Scalarize a widened load of address.
        setWideningDecision(I, VF, CM_Scalarize,
                            (VF * getMemoryInstructionCost(I, 1)));
      else if (auto Group = getInterleavedAccessGroup(I)) {
        // Scalarize an interleave group of address loads.
        for (unsigned I = 0; I < Group->getFactor(); ++I) {
          if (Instruction *Member = Group->getMember(I))
            setWideningDecision(Member, VF, CM_Scalarize,
                                (VF * getMemoryInstructionCost(Member, 1)));
        }
      }
    } else
      // Make sure I gets scalarized and a cost estimate without
      // scalarization overhead.
      ForcedScalars[VF].insert(I);
  }
}

unsigned LoopVectorizationCostModel::getInstructionCost(Instruction *I,
                                                        unsigned VF,
                                                        Type *&VectorTy) {
  Type *RetTy = I->getType();
  if (canTruncateToMinimalBitwidth(I, VF))
    RetTy = IntegerType::get(RetTy->getContext(), MinBWs[I]);
  VectorTy = isScalarAfterVectorization(I, VF) ? RetTy : ToVectorTy(RetTy, VF);
  auto SE = PSE.getSE();

  // TODO: We need to estimate the cost of intrinsic calls.
  switch (I->getOpcode()) {
  case Instruction::GetElementPtr:
    // We mark this instruction as zero-cost because the cost of GEPs in
    // vectorized code depends on whether the corresponding memory instruction
    // is scalarized or not. Therefore, we handle GEPs with the memory
    // instruction cost.
    return 0;
  case Instruction::Br: {
    // In cases of scalarized and predicated instructions, there will be VF
    // predicated blocks in the vectorized loop. Each branch around these
    // blocks requires also an extract of its vector compare i1 element.
    bool ScalarPredicatedBB = false;
    BranchInst *BI = cast<BranchInst>(I);
    if (VF > 1 && BI->isConditional() &&
        (PredicatedBBsAfterVectorization.find(BI->getSuccessor(0)) !=
             PredicatedBBsAfterVectorization.end() ||
         PredicatedBBsAfterVectorization.find(BI->getSuccessor(1)) !=
             PredicatedBBsAfterVectorization.end()))
      ScalarPredicatedBB = true;

    if (ScalarPredicatedBB) {
      // Return cost for branches around scalarized and predicated blocks.
      Type *Vec_i1Ty =
          VectorType::get(IntegerType::getInt1Ty(RetTy->getContext()), VF);
      return (TTI.getScalarizationOverhead(Vec_i1Ty, false, true) +
              (TTI.getCFInstrCost(Instruction::Br) * VF));
    } else if (I->getParent() == TheLoop->getLoopLatch() || VF == 1)
      // The back-edge branch will remain, as will all scalar branches.
      return TTI.getCFInstrCost(Instruction::Br);
    else
      // This branch will be eliminated by if-conversion.
      return 0;
    // Note: We currently assume zero cost for an unconditional branch inside
    // a predicated block since it will become a fall-through, although we
    // may decide in the future to call TTI for all branches.
  }
  case Instruction::PHI: {
    auto *Phi = cast<PHINode>(I);

    // First-order recurrences are replaced by vector shuffles inside the loop.
    // NOTE: Don't use ToVectorTy as SK_ExtractSubvector expects a vector type.
    if (VF > 1 && Legal->isFirstOrderRecurrence(Phi))
      return TTI.getShuffleCost(TargetTransformInfo::SK_ExtractSubvector,
                                VectorTy, VF - 1, VectorType::get(RetTy, 1));

    // Phi nodes in non-header blocks (not inductions, reductions, etc.) are
    // converted into select instructions. We require N - 1 selects per phi
    // node, where N is the number of incoming values.
    if (VF > 1 && Phi->getParent() != TheLoop->getHeader())
      return (Phi->getNumIncomingValues() - 1) *
             TTI.getCmpSelInstrCost(
                 Instruction::Select, ToVectorTy(Phi->getType(), VF),
                 ToVectorTy(Type::getInt1Ty(Phi->getContext()), VF));

    return TTI.getCFInstrCost(Instruction::PHI);
  }
  case Instruction::UDiv:
  case Instruction::SDiv:
  case Instruction::URem:
  case Instruction::SRem:
    // If we have a predicated instruction, it may not be executed for each
    // vector lane. Get the scalarization cost and scale this amount by the
    // probability of executing the predicated block. If the instruction is not
    // predicated, we fall through to the next case.
    if (VF > 1 && isScalarWithPredication(I)) {
      unsigned Cost = 0;

      // These instructions have a non-void type, so account for the phi nodes
      // that we will create. This cost is likely to be zero. The phi node
      // cost, if any, should be scaled by the block probability because it
      // models a copy at the end of each predicated block.
      Cost += VF * TTI.getCFInstrCost(Instruction::PHI);

      // The cost of the non-predicated instruction.
      Cost += VF * TTI.getArithmeticInstrCost(I->getOpcode(), RetTy);

      // The cost of insertelement and extractelement instructions needed for
      // scalarization.
      Cost += getScalarizationOverhead(I, VF, TTI);

      // Scale the cost by the probability of executing the predicated blocks.
      // This assumes the predicated block for each vector lane is equally
      // likely.
      return Cost / getReciprocalPredBlockProb();
    }
    LLVM_FALLTHROUGH;
  case Instruction::Add:
  case Instruction::FAdd:
  case Instruction::Sub:
  case Instruction::FSub:
  case Instruction::Mul:
  case Instruction::FMul:
  case Instruction::FDiv:
  case Instruction::FRem:
  case Instruction::Shl:
  case Instruction::LShr:
  case Instruction::AShr:
  case Instruction::And:
  case Instruction::Or:
  case Instruction::Xor: {
    // Since we will replace the stride by 1 the multiplication should go away.
    if (I->getOpcode() == Instruction::Mul && isStrideMul(I, Legal))
      return 0;
    // Certain instructions can be cheaper to vectorize if they have a constant
    // second vector operand. One example of this are shifts on x86.
    Value *Op2 = I->getOperand(1);
    TargetTransformInfo::OperandValueProperties Op2VP;
    TargetTransformInfo::OperandValueKind Op2VK =
        TTI.getOperandInfo(Op2, Op2VP);
    if (Op2VK == TargetTransformInfo::OK_AnyValue && Legal->isUniform(Op2))
      Op2VK = TargetTransformInfo::OK_UniformValue;

    SmallVector<const Value *, 4> Operands(I->operand_values());
    unsigned N = isScalarAfterVectorization(I, VF) ? VF : 1;
    return N * TTI.getArithmeticInstrCost(
                   I->getOpcode(), VectorTy, TargetTransformInfo::OK_AnyValue,
                   Op2VK, TargetTransformInfo::OP_None, Op2VP, Operands);
  }
  case Instruction::Select: {
    SelectInst *SI = cast<SelectInst>(I);
    const SCEV *CondSCEV = SE->getSCEV(SI->getCondition());
    bool ScalarCond = (SE->isLoopInvariant(CondSCEV, TheLoop));
    Type *CondTy = SI->getCondition()->getType();
    if (!ScalarCond)
      CondTy = VectorType::get(CondTy, VF);

    return TTI.getCmpSelInstrCost(I->getOpcode(), VectorTy, CondTy, I);
  }
  case Instruction::ICmp:
  case Instruction::FCmp: {
    Type *ValTy = I->getOperand(0)->getType();
    Instruction *Op0AsInstruction = dyn_cast<Instruction>(I->getOperand(0));
    if (canTruncateToMinimalBitwidth(Op0AsInstruction, VF))
      ValTy = IntegerType::get(ValTy->getContext(), MinBWs[Op0AsInstruction]);
    VectorTy = ToVectorTy(ValTy, VF);
    return TTI.getCmpSelInstrCost(I->getOpcode(), VectorTy, nullptr, I);
  }
  case Instruction::Store:
  case Instruction::Load: {
    unsigned Width = VF;
    if (Width > 1) {
      InstWidening Decision = getWideningDecision(I, Width);
      assert(Decision != CM_Unknown &&
             "CM decision should be taken at this point");
      if (Decision == CM_Scalarize)
        Width = 1;
    }
    VectorTy = ToVectorTy(getMemInstValueType(I), Width);
    return getMemoryInstructionCost(I, VF);
  }
  case Instruction::ZExt:
  case Instruction::SExt:
  case Instruction::FPToUI:
  case Instruction::FPToSI:
  case Instruction::FPExt:
  case Instruction::PtrToInt:
  case Instruction::IntToPtr:
  case Instruction::SIToFP:
  case Instruction::UIToFP:
  case Instruction::Trunc:
  case Instruction::FPTrunc:
  case Instruction::BitCast: {
    // We optimize the truncation of induction variables having constant
    // integer steps. The cost of these truncations is the same as the scalar
    // operation.
    if (isOptimizableIVTruncate(I, VF)) {
      auto *Trunc = cast<TruncInst>(I);
      return TTI.getCastInstrCost(Instruction::Trunc, Trunc->getDestTy(),
                                  Trunc->getSrcTy(), Trunc);
    }

    Type *SrcScalarTy = I->getOperand(0)->getType();
    Type *SrcVecTy =
        VectorTy->isVectorTy() ? ToVectorTy(SrcScalarTy, VF) : SrcScalarTy;
    if (canTruncateToMinimalBitwidth(I, VF)) {
      // This cast is going to be shrunk. This may remove the cast or it might
      // turn it into slightly different cast. For example, if MinBW == 16,
      // "zext i8 %1 to i32" becomes "zext i8 %1 to i16".
      //
      // Calculate the modified src and dest types.
      Type *MinVecTy = VectorTy;
      if (I->getOpcode() == Instruction::Trunc) {
        SrcVecTy = smallestIntegerVectorType(SrcVecTy, MinVecTy);
        VectorTy =
            largestIntegerVectorType(ToVectorTy(I->getType(), VF), MinVecTy);
      } else if (I->getOpcode() == Instruction::ZExt ||
                 I->getOpcode() == Instruction::SExt) {
        SrcVecTy = largestIntegerVectorType(SrcVecTy, MinVecTy);
        VectorTy =
            smallestIntegerVectorType(ToVectorTy(I->getType(), VF), MinVecTy);
      }
    }

    unsigned N = isScalarAfterVectorization(I, VF) ? VF : 1;
    return N * TTI.getCastInstrCost(I->getOpcode(), VectorTy, SrcVecTy, I);
  }
  case Instruction::Call: {
    bool NeedToScalarize;
    CallInst *CI = cast<CallInst>(I);
    unsigned CallCost = getVectorCallCost(CI, VF, TTI, TLI, NeedToScalarize);
    if (getVectorIntrinsicIDForCall(CI, TLI))
      return std::min(CallCost, getVectorIntrinsicCost(CI, VF, TTI, TLI));
    return CallCost;
  }
  default:
    // The cost of executing VF copies of the scalar instruction. This opcode
    // is unknown. Assume that it is the same as 'mul'.
    return VF * TTI.getArithmeticInstrCost(Instruction::Mul, VectorTy) +
           getScalarizationOverhead(I, VF, TTI);
  } // end of switch.
}

char LoopVectorize::ID = 0;

static const char lv_name[] = "Loop Vectorization";

INITIALIZE_PASS_BEGIN(LoopVectorize, LV_NAME, lv_name, false, false)
INITIALIZE_PASS_DEPENDENCY(TargetTransformInfoWrapperPass)
INITIALIZE_PASS_DEPENDENCY(BasicAAWrapperPass)
INITIALIZE_PASS_DEPENDENCY(AAResultsWrapperPass)
INITIALIZE_PASS_DEPENDENCY(GlobalsAAWrapperPass)
INITIALIZE_PASS_DEPENDENCY(AssumptionCacheTracker)
INITIALIZE_PASS_DEPENDENCY(BlockFrequencyInfoWrapperPass)
INITIALIZE_PASS_DEPENDENCY(DominatorTreeWrapperPass)
INITIALIZE_PASS_DEPENDENCY(ScalarEvolutionWrapperPass)
INITIALIZE_PASS_DEPENDENCY(LoopInfoWrapperPass)
INITIALIZE_PASS_DEPENDENCY(LoopAccessLegacyAnalysis)
INITIALIZE_PASS_DEPENDENCY(DemandedBitsWrapperPass)
INITIALIZE_PASS_DEPENDENCY(OptimizationRemarkEmitterWrapperPass)
INITIALIZE_PASS_END(LoopVectorize, LV_NAME, lv_name, false, false)

namespace llvm {

Pass *createLoopVectorizePass(bool InterleaveOnlyWhenForced,
                              bool VectorizeOnlyWhenForced) {
  return new LoopVectorize(InterleaveOnlyWhenForced, VectorizeOnlyWhenForced);
}

} // end namespace llvm

bool LoopVectorizationCostModel::isConsecutiveLoadOrStore(Instruction *Inst) {
  // Check if the pointer operand of a load or store instruction is
  // consecutive.
  if (auto *Ptr = getLoadStorePointerOperand(Inst))
    return Legal->isConsecutivePtr(Ptr);
  return false;
}

void LoopVectorizationCostModel::collectValuesToIgnore() {
  // Ignore ephemeral values.
  CodeMetrics::collectEphemeralValues(TheLoop, AC, ValuesToIgnore);

  // Ignore type-promoting instructions we identified during reduction
  // detection.
  for (auto &Reduction : *Legal->getReductionVars()) {
    RecurrenceDescriptor &RedDes = Reduction.second;
    SmallPtrSetImpl<Instruction *> &Casts = RedDes.getCastInsts();
    VecValuesToIgnore.insert(Casts.begin(), Casts.end());
  }
  // Ignore type-casting instructions we identified during induction
  // detection.
  for (auto &Induction : *Legal->getInductionVars()) {
    InductionDescriptor &IndDes = Induction.second;
    const SmallVectorImpl<Instruction *> &Casts = IndDes.getCastInsts();
    VecValuesToIgnore.insert(Casts.begin(), Casts.end());
  }
}

VectorizationFactor
LoopVectorizationPlanner::planInVPlanNativePath(bool OptForSize,
                                                unsigned UserVF) {
  // Width 1 means no vectorization, cost 0 means uncomputed cost.
  const VectorizationFactor NoVectorization = {1U, 0U};

  // Outer loop handling: They may require CFG and instruction level
  // transformations before even evaluating whether vectorization is profitable.
  // Since we cannot modify the incoming IR, we need to build VPlan upfront in
  // the vectorization pipeline.
  if (!OrigLoop->empty()) {
    // TODO: If UserVF is not provided, we set UserVF to 4 for stress testing.
    // This won't be necessary when UserVF is not required in the VPlan-native
    // path.
    if (VPlanBuildStressTest && !UserVF)
      UserVF = 4;

    assert(EnableVPlanNativePath && "VPlan-native path is not enabled.");
    assert(UserVF && "Expected UserVF for outer loop vectorization.");
    assert(isPowerOf2_32(UserVF) && "VF needs to be a power of two");
    LLVM_DEBUG(dbgs() << "LV: Using user VF " << UserVF << ".\n");
    buildVPlans(UserVF, UserVF);

    // For VPlan build stress testing, we bail out after VPlan construction.
    if (VPlanBuildStressTest)
      return NoVectorization;

    return {UserVF, 0};
  }

  LLVM_DEBUG(
      dbgs() << "LV: Not vectorizing. Inner loops aren't supported in the "
                "VPlan-native path.\n");
  return NoVectorization;
}

VectorizationFactor
LoopVectorizationPlanner::plan(bool OptForSize, unsigned UserVF) {
  assert(OrigLoop->empty() && "Inner loop expected.");
  // Width 1 means no vectorization, cost 0 means uncomputed cost.
  const VectorizationFactor NoVectorization = {1U, 0U};
  Optional<unsigned> MaybeMaxVF = CM.computeMaxVF(OptForSize);
  if (!MaybeMaxVF.hasValue()) // Cases considered too costly to vectorize.
    return NoVectorization;

  // Invalidate interleave groups if all blocks of loop will be predicated.
  if (CM.blockNeedsPredication(OrigLoop->getHeader()) &&
      !useMaskedInterleavedAccesses(*TTI)) {
    LLVM_DEBUG(
        dbgs()
        << "LV: Invalidate all interleaved groups due to fold-tail by masking "
           "which requires masked-interleaved support.\n");
    CM.InterleaveInfo.reset();
  }

  if (UserVF) {
    LLVM_DEBUG(dbgs() << "LV: Using user VF " << UserVF << ".\n");
    assert(isPowerOf2_32(UserVF) && "VF needs to be a power of two");
    // Collect the instructions (and their associated costs) that will be more
    // profitable to scalarize.
    CM.selectUserVectorizationFactor(UserVF);
    buildVPlansWithVPRecipes(UserVF, UserVF);
    LLVM_DEBUG(printPlans(dbgs()));
    return {UserVF, 0};
  }

  unsigned MaxVF = MaybeMaxVF.getValue();
  assert(MaxVF != 0 && "MaxVF is zero.");

  for (unsigned VF = 1; VF <= MaxVF; VF *= 2) {
    // Collect Uniform and Scalar instructions after vectorization with VF.
    CM.collectUniformsAndScalars(VF);

    // Collect the instructions (and their associated costs) that will be more
    // profitable to scalarize.
    if (VF > 1)
      CM.collectInstsToScalarize(VF);
  }

  buildVPlansWithVPRecipes(1, MaxVF);
  LLVM_DEBUG(printPlans(dbgs()));
  if (MaxVF == 1)
    return NoVectorization;

  // Select the optimal vectorization factor.
  return CM.selectVectorizationFactor(MaxVF);
}

void LoopVectorizationPlanner::setBestPlan(unsigned VF, unsigned UF) {
  LLVM_DEBUG(dbgs() << "Setting best plan to VF=" << VF << ", UF=" << UF
                    << '\n');
  BestVF = VF;
  BestUF = UF;

  erase_if(VPlans, [VF](const VPlanPtr &Plan) {
    return !Plan->hasVF(VF);
  });
  assert(VPlans.size() == 1 && "Best VF has not a single VPlan.");
}

void LoopVectorizationPlanner::executePlan(InnerLoopVectorizer &ILV,
                                           DominatorTree *DT) {
  // Perform the actual loop transformation.

  // 1. Create a new empty loop. Unlink the old loop and connect the new one.
  VPCallbackILV CallbackILV(ILV);

  VPTransformState State{BestVF, BestUF,      LI,
                         DT,     ILV.Builder, ILV.VectorLoopValueMap,
                         &ILV,   CallbackILV};
  State.CFG.PrevBB = ILV.createVectorizedLoopSkeleton();
  State.TripCount = ILV.getOrCreateTripCount(nullptr);

  //===------------------------------------------------===//
  //
  // Notice: any optimization or new instruction that go
  // into the code below should also be implemented in
  // the cost-model.
  //
  //===------------------------------------------------===//

  // 2. Copy and widen instructions from the old loop into the new loop.
  assert(VPlans.size() == 1 && "Not a single VPlan to execute.");
  VPlans.front()->execute(&State);

  // 3. Fix the vectorized code: take care of header phi's, live-outs,
  //    predication, updating analyses.
  ILV.fixVectorizedLoop();
}

void LoopVectorizationPlanner::collectTriviallyDeadInstructions(
    SmallPtrSetImpl<Instruction *> &DeadInstructions) {
  BasicBlock *Latch = OrigLoop->getLoopLatch();

  // We create new control-flow for the vectorized loop, so the original
  // condition will be dead after vectorization if it's only used by the
  // branch.
  auto *Cmp = dyn_cast<Instruction>(Latch->getTerminator()->getOperand(0));
  if (Cmp && Cmp->hasOneUse())
    DeadInstructions.insert(Cmp);

  // We create new "steps" for induction variable updates to which the original
  // induction variables map. An original update instruction will be dead if
  // all its users except the induction variable are dead.
  for (auto &Induction : *Legal->getInductionVars()) {
    PHINode *Ind = Induction.first;
    auto *IndUpdate = cast<Instruction>(Ind->getIncomingValueForBlock(Latch));
    if (llvm::all_of(IndUpdate->users(), [&](User *U) -> bool {
          return U == Ind || DeadInstructions.find(cast<Instruction>(U)) !=
                                 DeadInstructions.end();
        }))
      DeadInstructions.insert(IndUpdate);

    // We record as "Dead" also the type-casting instructions we had identified
    // during induction analysis. We don't need any handling for them in the
    // vectorized loop because we have proven that, under a proper runtime
    // test guarding the vectorized loop, the value of the phi, and the casted
    // value of the phi, are the same. The last instruction in this casting chain
    // will get its scalar/vector/widened def from the scalar/vector/widened def
    // of the respective phi node. Any other casts in the induction def-use chain
    // have no other uses outside the phi update chain, and will be ignored.
    InductionDescriptor &IndDes = Induction.second;
    const SmallVectorImpl<Instruction *> &Casts = IndDes.getCastInsts();
    DeadInstructions.insert(Casts.begin(), Casts.end());
  }
}

Value *InnerLoopUnroller::reverseVector(Value *Vec) { return Vec; }

Value *InnerLoopUnroller::getBroadcastInstrs(Value *V) { return V; }

Value *InnerLoopUnroller::getStepVector(Value *Val, int StartIdx, Value *Step,
                                        Instruction::BinaryOps BinOp) {
  // When unrolling and the VF is 1, we only need to add a simple scalar.
  Type *Ty = Val->getType();
  assert(!Ty->isVectorTy() && "Val must be a scalar");

  if (Ty->isFloatingPointTy()) {
    Constant *C = ConstantFP::get(Ty, (double)StartIdx);

    // Floating point operations had to be 'fast' to enable the unrolling.
    Value *MulOp = addFastMathFlag(Builder.CreateFMul(C, Step));
    return addFastMathFlag(Builder.CreateBinOp(BinOp, Val, MulOp));
  }
  Constant *C = ConstantInt::get(Ty, StartIdx);
  return Builder.CreateAdd(Val, Builder.CreateMul(C, Step), "induction");
}

static void AddRuntimeUnrollDisableMetaData(Loop *L) {
  SmallVector<Metadata *, 4> MDs;
  // Reserve first location for self reference to the LoopID metadata node.
  MDs.push_back(nullptr);
  bool IsUnrollMetadata = false;
  MDNode *LoopID = L->getLoopID();
  if (LoopID) {
    // First find existing loop unrolling disable metadata.
    for (unsigned i = 1, ie = LoopID->getNumOperands(); i < ie; ++i) {
      auto *MD = dyn_cast<MDNode>(LoopID->getOperand(i));
      if (MD) {
        const auto *S = dyn_cast<MDString>(MD->getOperand(0));
        IsUnrollMetadata =
            S && S->getString().startswith("llvm.loop.unroll.disable");
      }
      MDs.push_back(LoopID->getOperand(i));
    }
  }

  if (!IsUnrollMetadata) {
    // Add runtime unroll disable metadata.
    LLVMContext &Context = L->getHeader()->getContext();
    SmallVector<Metadata *, 1> DisableOperands;
    DisableOperands.push_back(
        MDString::get(Context, "llvm.loop.unroll.runtime.disable"));
    MDNode *DisableNode = MDNode::get(Context, DisableOperands);
    MDs.push_back(DisableNode);
    MDNode *NewLoopID = MDNode::get(Context, MDs);
    // Set operand 0 to refer to the loop id itself.
    NewLoopID->replaceOperandWith(0, NewLoopID);
    L->setLoopID(NewLoopID);
  }
}

bool LoopVectorizationPlanner::getDecisionAndClampRange(
    const std::function<bool(unsigned)> &Predicate, VFRange &Range) {
  assert(Range.End > Range.Start && "Trying to test an empty VF range.");
  bool PredicateAtRangeStart = Predicate(Range.Start);

  for (unsigned TmpVF = Range.Start * 2; TmpVF < Range.End; TmpVF *= 2)
    if (Predicate(TmpVF) != PredicateAtRangeStart) {
      Range.End = TmpVF;
      break;
    }

  return PredicateAtRangeStart;
}

/// Build VPlans for the full range of feasible VF's = {\p MinVF, 2 * \p MinVF,
/// 4 * \p MinVF, ..., \p MaxVF} by repeatedly building a VPlan for a sub-range
/// of VF's starting at a given VF and extending it as much as possible. Each
/// vectorization decision can potentially shorten this sub-range during
/// buildVPlan().
void LoopVectorizationPlanner::buildVPlans(unsigned MinVF, unsigned MaxVF) {
  for (unsigned VF = MinVF; VF < MaxVF + 1;) {
    VFRange SubRange = {VF, MaxVF + 1};
    VPlans.push_back(buildVPlan(SubRange));
    VF = SubRange.End;
  }
}

VPValue *VPRecipeBuilder::createEdgeMask(BasicBlock *Src, BasicBlock *Dst,
                                         VPlanPtr &Plan) {
  assert(is_contained(predecessors(Dst), Src) && "Invalid edge");

  // Look for cached value.
  std::pair<BasicBlock *, BasicBlock *> Edge(Src, Dst);
  EdgeMaskCacheTy::iterator ECEntryIt = EdgeMaskCache.find(Edge);
  if (ECEntryIt != EdgeMaskCache.end())
    return ECEntryIt->second;

  VPValue *SrcMask = createBlockInMask(Src, Plan);

  // The terminator has to be a branch inst!
  BranchInst *BI = dyn_cast<BranchInst>(Src->getTerminator());
  assert(BI && "Unexpected terminator found");

  if (!BI->isConditional())
    return EdgeMaskCache[Edge] = SrcMask;

  VPValue *EdgeMask = Plan->getVPValue(BI->getCondition());
  assert(EdgeMask && "No Edge Mask found for condition");

  if (BI->getSuccessor(0) != Dst)
    EdgeMask = Builder.createNot(EdgeMask);

  if (SrcMask) // Otherwise block in-mask is all-one, no need to AND.
    EdgeMask = Builder.createAnd(EdgeMask, SrcMask);

  return EdgeMaskCache[Edge] = EdgeMask;
}

VPValue *VPRecipeBuilder::createBlockInMask(BasicBlock *BB, VPlanPtr &Plan) {
  assert(OrigLoop->contains(BB) && "Block is not a part of a loop");

  // Look for cached value.
  BlockMaskCacheTy::iterator BCEntryIt = BlockMaskCache.find(BB);
  if (BCEntryIt != BlockMaskCache.end())
    return BCEntryIt->second;

  // All-one mask is modelled as no-mask following the convention for masked
  // load/store/gather/scatter. Initialize BlockMask to no-mask.
  VPValue *BlockMask = nullptr;

  if (OrigLoop->getHeader() == BB) {
    if (!CM.blockNeedsPredication(BB))
      return BlockMaskCache[BB] = BlockMask; // Loop incoming mask is all-one.

    // Introduce the early-exit compare IV <= BTC to form header block mask.
    // This is used instead of IV < TC because TC may wrap, unlike BTC.
    VPValue *IV = Plan->getVPValue(Legal->getPrimaryInduction());
    VPValue *BTC = Plan->getOrCreateBackedgeTakenCount();
    BlockMask = Builder.createNaryOp(VPInstruction::ICmpULE, {IV, BTC});
    return BlockMaskCache[BB] = BlockMask;
  }

  // This is the block mask. We OR all incoming edges.
  for (auto *Predecessor : predecessors(BB)) {
    VPValue *EdgeMask = createEdgeMask(Predecessor, BB, Plan);
    if (!EdgeMask) // Mask of predecessor is all-one so mask of block is too.
      return BlockMaskCache[BB] = EdgeMask;

    if (!BlockMask) { // BlockMask has its initialized nullptr value.
      BlockMask = EdgeMask;
      continue;
    }

    BlockMask = Builder.createOr(BlockMask, EdgeMask);
  }

  return BlockMaskCache[BB] = BlockMask;
}

VPInterleaveRecipe *VPRecipeBuilder::tryToInterleaveMemory(Instruction *I,
                                                           VFRange &Range,
                                                           VPlanPtr &Plan) {
  const InterleaveGroup<Instruction> *IG = CM.getInterleavedAccessGroup(I);
  if (!IG)
    return nullptr;

  // Now check if IG is relevant for VF's in the given range.
  auto isIGMember = [&](Instruction *I) -> std::function<bool(unsigned)> {
    return [=](unsigned VF) -> bool {
      return (VF >= 2 && // Query is illegal for VF == 1
              CM.getWideningDecision(I, VF) ==
                  LoopVectorizationCostModel::CM_Interleave);
    };
  };
  if (!LoopVectorizationPlanner::getDecisionAndClampRange(isIGMember(I), Range))
    return nullptr;

  // I is a member of an InterleaveGroup for VF's in the (possibly trimmed)
  // range. If it's the primary member of the IG construct a VPInterleaveRecipe.
  // Otherwise, it's an adjunct member of the IG, do not construct any Recipe.
  assert(I == IG->getInsertPos() &&
         "Generating a recipe for an adjunct member of an interleave group");

  VPValue *Mask = nullptr;
  if (Legal->isMaskRequired(I))
    Mask = createBlockInMask(I->getParent(), Plan);

  return new VPInterleaveRecipe(IG, Mask);
}

VPWidenMemoryInstructionRecipe *
VPRecipeBuilder::tryToWidenMemory(Instruction *I, VFRange &Range,
                                  VPlanPtr &Plan) {
  if (!isa<LoadInst>(I) && !isa<StoreInst>(I))
    return nullptr;

  auto willWiden = [&](unsigned VF) -> bool {
    if (VF == 1)
      return false;
    if (CM.isScalarAfterVectorization(I, VF) ||
        CM.isProfitableToScalarize(I, VF))
      return false;
    LoopVectorizationCostModel::InstWidening Decision =
        CM.getWideningDecision(I, VF);
    assert(Decision != LoopVectorizationCostModel::CM_Unknown &&
           "CM decision should be taken at this point.");
    assert(Decision != LoopVectorizationCostModel::CM_Interleave &&
           "Interleave memory opportunity should be caught earlier.");
    return Decision != LoopVectorizationCostModel::CM_Scalarize;
  };

  if (!LoopVectorizationPlanner::getDecisionAndClampRange(willWiden, Range))
    return nullptr;

  VPValue *Mask = nullptr;
  if (Legal->isMaskRequired(I))
    Mask = createBlockInMask(I->getParent(), Plan);

  return new VPWidenMemoryInstructionRecipe(*I, Mask);
}

VPWidenIntOrFpInductionRecipe *
VPRecipeBuilder::tryToOptimizeInduction(Instruction *I, VFRange &Range) {
  if (PHINode *Phi = dyn_cast<PHINode>(I)) {
    // Check if this is an integer or fp induction. If so, build the recipe that
    // produces its scalar and vector values.
    InductionDescriptor II = Legal->getInductionVars()->lookup(Phi);
    if (II.getKind() == InductionDescriptor::IK_IntInduction ||
        II.getKind() == InductionDescriptor::IK_FpInduction)
      return new VPWidenIntOrFpInductionRecipe(Phi);

    return nullptr;
  }

  // Optimize the special case where the source is a constant integer
  // induction variable. Notice that we can only optimize the 'trunc' case
  // because (a) FP conversions lose precision, (b) sext/zext may wrap, and
  // (c) other casts depend on pointer size.

  // Determine whether \p K is a truncation based on an induction variable that
  // can be optimized.
  auto isOptimizableIVTruncate =
      [&](Instruction *K) -> std::function<bool(unsigned)> {
    return
        [=](unsigned VF) -> bool { return CM.isOptimizableIVTruncate(K, VF); };
  };

  if (isa<TruncInst>(I) && LoopVectorizationPlanner::getDecisionAndClampRange(
                               isOptimizableIVTruncate(I), Range))
    return new VPWidenIntOrFpInductionRecipe(cast<PHINode>(I->getOperand(0)),
                                             cast<TruncInst>(I));
  return nullptr;
}

VPBlendRecipe *VPRecipeBuilder::tryToBlend(Instruction *I, VPlanPtr &Plan) {
  PHINode *Phi = dyn_cast<PHINode>(I);
  if (!Phi || Phi->getParent() == OrigLoop->getHeader())
    return nullptr;

  // We know that all PHIs in non-header blocks are converted into selects, so
  // we don't have to worry about the insertion order and we can just use the
  // builder. At this point we generate the predication tree. There may be
  // duplications since this is a simple recursive scan, but future
  // optimizations will clean it up.

  SmallVector<VPValue *, 2> Masks;
  unsigned NumIncoming = Phi->getNumIncomingValues();
  for (unsigned In = 0; In < NumIncoming; In++) {
    VPValue *EdgeMask =
      createEdgeMask(Phi->getIncomingBlock(In), Phi->getParent(), Plan);
    assert((EdgeMask || NumIncoming == 1) &&
           "Multiple predecessors with one having a full mask");
    if (EdgeMask)
      Masks.push_back(EdgeMask);
  }
  return new VPBlendRecipe(Phi, Masks);
}

bool VPRecipeBuilder::tryToWiden(Instruction *I, VPBasicBlock *VPBB,
                                 VFRange &Range) {

  bool IsPredicated = LoopVectorizationPlanner::getDecisionAndClampRange(
      [&](unsigned VF) { return CM.isScalarWithPredication(I, VF); }, Range);

  if (IsPredicated)
    return false;

  auto IsVectorizableOpcode = [](unsigned Opcode) {
    switch (Opcode) {
    case Instruction::Add:
    case Instruction::And:
    case Instruction::AShr:
    case Instruction::BitCast:
    case Instruction::Br:
    case Instruction::Call:
    case Instruction::FAdd:
    case Instruction::FCmp:
    case Instruction::FDiv:
    case Instruction::FMul:
    case Instruction::FPExt:
    case Instruction::FPToSI:
    case Instruction::FPToUI:
    case Instruction::FPTrunc:
    case Instruction::FRem:
    case Instruction::FSub:
    case Instruction::GetElementPtr:
    case Instruction::ICmp:
    case Instruction::IntToPtr:
    case Instruction::Load:
    case Instruction::LShr:
    case Instruction::Mul:
    case Instruction::Or:
    case Instruction::PHI:
    case Instruction::PtrToInt:
    case Instruction::SDiv:
    case Instruction::Select:
    case Instruction::SExt:
    case Instruction::Shl:
    case Instruction::SIToFP:
    case Instruction::SRem:
    case Instruction::Store:
    case Instruction::Sub:
    case Instruction::Trunc:
    case Instruction::UDiv:
    case Instruction::UIToFP:
    case Instruction::URem:
    case Instruction::Xor:
    case Instruction::ZExt:
      return true;
    }
    return false;
  };

  if (!IsVectorizableOpcode(I->getOpcode()))
    return false;

  if (CallInst *CI = dyn_cast<CallInst>(I)) {
    Intrinsic::ID ID = getVectorIntrinsicIDForCall(CI, TLI);
    if (ID && (ID == Intrinsic::assume || ID == Intrinsic::lifetime_end ||
               ID == Intrinsic::lifetime_start || ID == Intrinsic::sideeffect))
      return false;
  }

  auto willWiden = [&](unsigned VF) -> bool {
    if (!isa<PHINode>(I) && (CM.isScalarAfterVectorization(I, VF) ||
                             CM.isProfitableToScalarize(I, VF)))
      return false;
    if (CallInst *CI = dyn_cast<CallInst>(I)) {
      Intrinsic::ID ID = getVectorIntrinsicIDForCall(CI, TLI);
      // The following case may be scalarized depending on the VF.
      // The flag shows whether we use Intrinsic or a usual Call for vectorized
      // version of the instruction.
      // Is it beneficial to perform intrinsic call compared to lib call?
      bool NeedToScalarize;
      unsigned CallCost = getVectorCallCost(CI, VF, *TTI, TLI, NeedToScalarize);
      bool UseVectorIntrinsic =
          ID && getVectorIntrinsicCost(CI, VF, *TTI, TLI) <= CallCost;
      return UseVectorIntrinsic || !NeedToScalarize;
    }
    if (isa<LoadInst>(I) || isa<StoreInst>(I)) {
      assert(CM.getWideningDecision(I, VF) ==
                 LoopVectorizationCostModel::CM_Scalarize &&
             "Memory widening decisions should have been taken care by now");
      return false;
    }
    return true;
  };

  if (!LoopVectorizationPlanner::getDecisionAndClampRange(willWiden, Range))
    return false;

  // Success: widen this instruction. We optimize the common case where
  // consecutive instructions can be represented by a single recipe.
  if (!VPBB->empty()) {
    VPWidenRecipe *LastWidenRecipe = dyn_cast<VPWidenRecipe>(&VPBB->back());
    if (LastWidenRecipe && LastWidenRecipe->appendInstruction(I))
      return true;
  }

  VPBB->appendRecipe(new VPWidenRecipe(I));
  return true;
}

VPBasicBlock *VPRecipeBuilder::handleReplication(
    Instruction *I, VFRange &Range, VPBasicBlock *VPBB,
    DenseMap<Instruction *, VPReplicateRecipe *> &PredInst2Recipe,
    VPlanPtr &Plan) {
  bool IsUniform = LoopVectorizationPlanner::getDecisionAndClampRange(
      [&](unsigned VF) { return CM.isUniformAfterVectorization(I, VF); },
      Range);

  bool IsPredicated = LoopVectorizationPlanner::getDecisionAndClampRange(
      [&](unsigned VF) { return CM.isScalarWithPredication(I, VF); }, Range);

  auto *Recipe = new VPReplicateRecipe(I, IsUniform, IsPredicated);

  // Find if I uses a predicated instruction. If so, it will use its scalar
  // value. Avoid hoisting the insert-element which packs the scalar value into
  // a vector value, as that happens iff all users use the vector value.
  for (auto &Op : I->operands())
    if (auto *PredInst = dyn_cast<Instruction>(Op))
      if (PredInst2Recipe.find(PredInst) != PredInst2Recipe.end())
        PredInst2Recipe[PredInst]->setAlsoPack(false);

  // Finalize the recipe for Instr, first if it is not predicated.
  if (!IsPredicated) {
    LLVM_DEBUG(dbgs() << "LV: Scalarizing:" << *I << "\n");
    VPBB->appendRecipe(Recipe);
    return VPBB;
  }
  LLVM_DEBUG(dbgs() << "LV: Scalarizing and predicating:" << *I << "\n");
  assert(VPBB->getSuccessors().empty() &&
         "VPBB has successors when handling predicated replication.");
  // Record predicated instructions for above packing optimizations.
  PredInst2Recipe[I] = Recipe;
  VPBlockBase *Region = createReplicateRegion(I, Recipe, Plan);
  VPBlockUtils::insertBlockAfter(Region, VPBB);
  auto *RegSucc = new VPBasicBlock();
  VPBlockUtils::insertBlockAfter(RegSucc, Region);
  return RegSucc;
}

VPRegionBlock *VPRecipeBuilder::createReplicateRegion(Instruction *Instr,
                                                      VPRecipeBase *PredRecipe,
                                                      VPlanPtr &Plan) {
  // Instructions marked for predication are replicated and placed under an
  // if-then construct to prevent side-effects.

  // Generate recipes to compute the block mask for this region.
  VPValue *BlockInMask = createBlockInMask(Instr->getParent(), Plan);

  // Build the triangular if-then region.
  std::string RegionName = (Twine("pred.") + Instr->getOpcodeName()).str();
  assert(Instr->getParent() && "Predicated instruction not in any basic block");
  auto *BOMRecipe = new VPBranchOnMaskRecipe(BlockInMask);
  auto *Entry = new VPBasicBlock(Twine(RegionName) + ".entry", BOMRecipe);
  auto *PHIRecipe =
      Instr->getType()->isVoidTy() ? nullptr : new VPPredInstPHIRecipe(Instr);
  auto *Exit = new VPBasicBlock(Twine(RegionName) + ".continue", PHIRecipe);
  auto *Pred = new VPBasicBlock(Twine(RegionName) + ".if", PredRecipe);
  VPRegionBlock *Region = new VPRegionBlock(Entry, Exit, RegionName, true);

  // Note: first set Entry as region entry and then connect successors starting
  // from it in order, to propagate the "parent" of each VPBasicBlock.
  VPBlockUtils::insertTwoBlocksAfter(Pred, Exit, BlockInMask, Entry);
  VPBlockUtils::connectBlocks(Pred, Exit);

  return Region;
}

bool VPRecipeBuilder::tryToCreateRecipe(Instruction *Instr, VFRange &Range,
                                        VPlanPtr &Plan, VPBasicBlock *VPBB) {
  VPRecipeBase *Recipe = nullptr;
  // Check if Instr should belong to an interleave memory recipe, or already
  // does. In the latter case Instr is irrelevant.
  if ((Recipe = tryToInterleaveMemory(Instr, Range, Plan))) {
    VPBB->appendRecipe(Recipe);
    return true;
  }

  // Check if Instr is a memory operation that should be widened.
  if ((Recipe = tryToWidenMemory(Instr, Range, Plan))) {
    VPBB->appendRecipe(Recipe);
    return true;
  }

  // Check if Instr should form some PHI recipe.
  if ((Recipe = tryToOptimizeInduction(Instr, Range))) {
    VPBB->appendRecipe(Recipe);
    return true;
  }
  if ((Recipe = tryToBlend(Instr, Plan))) {
    VPBB->appendRecipe(Recipe);
    return true;
  }
  if (PHINode *Phi = dyn_cast<PHINode>(Instr)) {
    VPBB->appendRecipe(new VPWidenPHIRecipe(Phi));
    return true;
  }

  // Check if Instr is to be widened by a general VPWidenRecipe, after
  // having first checked for specific widening recipes that deal with
  // Interleave Groups, Inductions and Phi nodes.
  if (tryToWiden(Instr, VPBB, Range))
    return true;

  return false;
}

void LoopVectorizationPlanner::buildVPlansWithVPRecipes(unsigned MinVF,
                                                        unsigned MaxVF) {
  assert(OrigLoop->empty() && "Inner loop expected.");

  // Collect conditions feeding internal conditional branches; they need to be
  // represented in VPlan for it to model masking.
  SmallPtrSet<Value *, 1> NeedDef;

  auto *Latch = OrigLoop->getLoopLatch();
  for (BasicBlock *BB : OrigLoop->blocks()) {
    if (BB == Latch)
      continue;
    BranchInst *Branch = dyn_cast<BranchInst>(BB->getTerminator());
    if (Branch && Branch->isConditional())
      NeedDef.insert(Branch->getCondition());
  }

  // If the tail is to be folded by masking, the primary induction variable
  // needs to be represented in VPlan for it to model early-exit masking.
  if (CM.foldTailByMasking())
    NeedDef.insert(Legal->getPrimaryInduction());

  // Collect instructions from the original loop that will become trivially dead
  // in the vectorized loop. We don't need to vectorize these instructions. For
  // example, original induction update instructions can become dead because we
  // separately emit induction "steps" when generating code for the new loop.
  // Similarly, we create a new latch condition when setting up the structure
  // of the new loop, so the old one can become dead.
  SmallPtrSet<Instruction *, 4> DeadInstructions;
  collectTriviallyDeadInstructions(DeadInstructions);

  for (unsigned VF = MinVF; VF < MaxVF + 1;) {
    VFRange SubRange = {VF, MaxVF + 1};
    VPlans.push_back(
        buildVPlanWithVPRecipes(SubRange, NeedDef, DeadInstructions));
    VF = SubRange.End;
  }
}

LoopVectorizationPlanner::VPlanPtr
LoopVectorizationPlanner::buildVPlanWithVPRecipes(
    VFRange &Range, SmallPtrSetImpl<Value *> &NeedDef,
    SmallPtrSetImpl<Instruction *> &DeadInstructions) {
  // Hold a mapping from predicated instructions to their recipes, in order to
  // fix their AlsoPack behavior if a user is determined to replicate and use a
  // scalar instead of vector value.
  DenseMap<Instruction *, VPReplicateRecipe *> PredInst2Recipe;

  DenseMap<Instruction *, Instruction *> &SinkAfter = Legal->getSinkAfter();
  DenseMap<Instruction *, Instruction *> SinkAfterInverse;

  // Create a dummy pre-entry VPBasicBlock to start building the VPlan.
  VPBasicBlock *VPBB = new VPBasicBlock("Pre-Entry");
  auto Plan = llvm::make_unique<VPlan>(VPBB);

  VPRecipeBuilder RecipeBuilder(OrigLoop, TLI, TTI, Legal, CM, Builder);
  // Represent values that will have defs inside VPlan.
  for (Value *V : NeedDef)
    Plan->addVPValue(V);

  // Scan the body of the loop in a topological order to visit each basic block
  // after having visited its predecessor basic blocks.
  LoopBlocksDFS DFS(OrigLoop);
  DFS.perform(LI);

  for (BasicBlock *BB : make_range(DFS.beginRPO(), DFS.endRPO())) {
    // Relevant instructions from basic block BB will be grouped into VPRecipe
    // ingredients and fill a new VPBasicBlock.
    unsigned VPBBsForBB = 0;
    auto *FirstVPBBForBB = new VPBasicBlock(BB->getName());
    VPBlockUtils::insertBlockAfter(FirstVPBBForBB, VPBB);
    VPBB = FirstVPBBForBB;
    Builder.setInsertPoint(VPBB);

    std::vector<Instruction *> Ingredients;

    // Organize the ingredients to vectorize from current basic block in the
    // right order.
    for (Instruction &I : BB->instructionsWithoutDebug()) {
      Instruction *Instr = &I;

      // First filter out irrelevant instructions, to ensure no recipes are
      // built for them.
      if (isa<BranchInst>(Instr) ||
          DeadInstructions.find(Instr) != DeadInstructions.end())
        continue;

      // I is a member of an InterleaveGroup for Range.Start. If it's an adjunct
      // member of the IG, do not construct any Recipe for it.
      const InterleaveGroup<Instruction> *IG =
          CM.getInterleavedAccessGroup(Instr);
      if (IG && Instr != IG->getInsertPos() &&
          Range.Start >= 2 && // Query is illegal for VF == 1
          CM.getWideningDecision(Instr, Range.Start) ==
              LoopVectorizationCostModel::CM_Interleave) {
        auto SinkCandidate = SinkAfterInverse.find(Instr);
        if (SinkCandidate != SinkAfterInverse.end())
          Ingredients.push_back(SinkCandidate->second);
        continue;
      }

      // Move instructions to handle first-order recurrences, step 1: avoid
      // handling this instruction until after we've handled the instruction it
      // should follow.
      auto SAIt = SinkAfter.find(Instr);
      if (SAIt != SinkAfter.end()) {
        LLVM_DEBUG(dbgs() << "Sinking" << *SAIt->first << " after"
                          << *SAIt->second
                          << " to vectorize a 1st order recurrence.\n");
        SinkAfterInverse[SAIt->second] = Instr;
        continue;
      }

      Ingredients.push_back(Instr);

      // Move instructions to handle first-order recurrences, step 2: push the
      // instruction to be sunk at its insertion point.
      auto SAInvIt = SinkAfterInverse.find(Instr);
      if (SAInvIt != SinkAfterInverse.end())
        Ingredients.push_back(SAInvIt->second);
    }

    // Introduce each ingredient into VPlan.
    for (Instruction *Instr : Ingredients) {
      if (RecipeBuilder.tryToCreateRecipe(Instr, Range, Plan, VPBB))
        continue;

      // Otherwise, if all widening options failed, Instruction is to be
      // replicated. This may create a successor for VPBB.
      VPBasicBlock *NextVPBB = RecipeBuilder.handleReplication(
          Instr, Range, VPBB, PredInst2Recipe, Plan);
      if (NextVPBB != VPBB) {
        VPBB = NextVPBB;
        VPBB->setName(BB->hasName() ? BB->getName() + "." + Twine(VPBBsForBB++)
                                    : "");
      }
    }
  }

  // Discard empty dummy pre-entry VPBasicBlock. Note that other VPBasicBlocks
  // may also be empty, such as the last one VPBB, reflecting original
  // basic-blocks with no recipes.
  VPBasicBlock *PreEntry = cast<VPBasicBlock>(Plan->getEntry());
  assert(PreEntry->empty() && "Expecting empty pre-entry block.");
  VPBlockBase *Entry = Plan->setEntry(PreEntry->getSingleSuccessor());
  VPBlockUtils::disconnectBlocks(PreEntry, Entry);
  delete PreEntry;

  std::string PlanName;
  raw_string_ostream RSO(PlanName);
  unsigned VF = Range.Start;
  Plan->addVF(VF);
  RSO << "Initial VPlan for VF={" << VF;
  for (VF *= 2; VF < Range.End; VF *= 2) {
    Plan->addVF(VF);
    RSO << "," << VF;
  }
  RSO << "},UF>=1";
  RSO.flush();
  Plan->setName(PlanName);

  return Plan;
}

LoopVectorizationPlanner::VPlanPtr
LoopVectorizationPlanner::buildVPlan(VFRange &Range) {
  // Outer loop handling: They may require CFG and instruction level
  // transformations before even evaluating whether vectorization is profitable.
  // Since we cannot modify the incoming IR, we need to build VPlan upfront in
  // the vectorization pipeline.
  assert(!OrigLoop->empty());
  assert(EnableVPlanNativePath && "VPlan-native path is not enabled.");

  // Create new empty VPlan
  auto Plan = llvm::make_unique<VPlan>();

  // Build hierarchical CFG
  VPlanHCFGBuilder HCFGBuilder(OrigLoop, LI, *Plan);
  HCFGBuilder.buildHierarchicalCFG();

  SmallPtrSet<Instruction *, 1> DeadInstructions;
  VPlanHCFGTransforms::VPInstructionsToVPRecipes(
      Plan, Legal->getInductionVars(), DeadInstructions);

  for (unsigned VF = Range.Start; VF < Range.End; VF *= 2)
    Plan->addVF(VF);

  return Plan;
}

Value* LoopVectorizationPlanner::VPCallbackILV::
getOrCreateVectorValues(Value *V, unsigned Part) {
      return ILV.getOrCreateVectorValue(V, Part);
}

void VPInterleaveRecipe::print(raw_ostream &O, const Twine &Indent) const {
  O << " +\n"
    << Indent << "\"INTERLEAVE-GROUP with factor " << IG->getFactor() << " at ";
  IG->getInsertPos()->printAsOperand(O, false);
  if (User) {
    O << ", ";
    User->getOperand(0)->printAsOperand(O);
  }
  O << "\\l\"";
  for (unsigned i = 0; i < IG->getFactor(); ++i)
    if (Instruction *I = IG->getMember(i))
      O << " +\n"
        << Indent << "\"  " << VPlanIngredient(I) << " " << i << "\\l\"";
}

void VPWidenRecipe::execute(VPTransformState &State) {
  for (auto &Instr : make_range(Begin, End))
    State.ILV->widenInstruction(Instr);
}

void VPWidenIntOrFpInductionRecipe::execute(VPTransformState &State) {
  assert(!State.Instance && "Int or FP induction being replicated.");
  State.ILV->widenIntOrFpInduction(IV, Trunc);
}

void VPWidenPHIRecipe::execute(VPTransformState &State) {
  State.ILV->widenPHIInstruction(Phi, State.UF, State.VF);
}

void VPBlendRecipe::execute(VPTransformState &State) {
  State.ILV->setDebugLocFromInst(State.Builder, Phi);
  // We know that all PHIs in non-header blocks are converted into
  // selects, so we don't have to worry about the insertion order and we
  // can just use the builder.
  // At this point we generate the predication tree. There may be
  // duplications since this is a simple recursive scan, but future
  // optimizations will clean it up.

  unsigned NumIncoming = Phi->getNumIncomingValues();

  assert((User || NumIncoming == 1) &&
         "Multiple predecessors with predecessors having a full mask");
  // Generate a sequence of selects of the form:
  // SELECT(Mask3, In3,
  //      SELECT(Mask2, In2,
  //                   ( ...)))
  InnerLoopVectorizer::VectorParts Entry(State.UF);
  for (unsigned In = 0; In < NumIncoming; ++In) {
    for (unsigned Part = 0; Part < State.UF; ++Part) {
      // We might have single edge PHIs (blocks) - use an identity
      // 'select' for the first PHI operand.
      Value *In0 =
          State.ILV->getOrCreateVectorValue(Phi->getIncomingValue(In), Part);
      if (In == 0)
        Entry[Part] = In0; // Initialize with the first incoming value.
      else {
        // Select between the current value and the previous incoming edge
        // based on the incoming mask.
        Value *Cond = State.get(User->getOperand(In), Part);
        Entry[Part] =
            State.Builder.CreateSelect(Cond, In0, Entry[Part], "predphi");
      }
    }
  }
  for (unsigned Part = 0; Part < State.UF; ++Part)
    State.ValueMap.setVectorValue(Phi, Part, Entry[Part]);
}

void VPInterleaveRecipe::execute(VPTransformState &State) {
  assert(!State.Instance && "Interleave group being replicated.");
  if (!User)
    return State.ILV->vectorizeInterleaveGroup(IG->getInsertPos());

  // Last (and currently only) operand is a mask.
  InnerLoopVectorizer::VectorParts MaskValues(State.UF);
  VPValue *Mask = User->getOperand(User->getNumOperands() - 1);
  for (unsigned Part = 0; Part < State.UF; ++Part)
    MaskValues[Part] = State.get(Mask, Part);
  State.ILV->vectorizeInterleaveGroup(IG->getInsertPos(), &MaskValues);
}

void VPReplicateRecipe::execute(VPTransformState &State) {
  if (State.Instance) { // Generate a single instance.
    State.ILV->scalarizeInstruction(Ingredient, *State.Instance, IsPredicated);
    // Insert scalar instance packing it into a vector.
    if (AlsoPack && State.VF > 1) {
      // If we're constructing lane 0, initialize to start from undef.
      if (State.Instance->Lane == 0) {
        Value *Undef =
            UndefValue::get(VectorType::get(Ingredient->getType(), State.VF));
        State.ValueMap.setVectorValue(Ingredient, State.Instance->Part, Undef);
      }
      State.ILV->packScalarIntoVectorValue(Ingredient, *State.Instance);
    }
    return;
  }

  // Generate scalar instances for all VF lanes of all UF parts, unless the
  // instruction is uniform inwhich case generate only the first lane for each
  // of the UF parts.
  unsigned EndLane = IsUniform ? 1 : State.VF;
  for (unsigned Part = 0; Part < State.UF; ++Part)
    for (unsigned Lane = 0; Lane < EndLane; ++Lane)
      State.ILV->scalarizeInstruction(Ingredient, {Part, Lane}, IsPredicated);
}

void VPBranchOnMaskRecipe::execute(VPTransformState &State) {
  assert(State.Instance && "Branch on Mask works only on single instance.");

  unsigned Part = State.Instance->Part;
  unsigned Lane = State.Instance->Lane;

  Value *ConditionBit = nullptr;
  if (!User) // Block in mask is all-one.
    ConditionBit = State.Builder.getTrue();
  else {
    VPValue *BlockInMask = User->getOperand(0);
    ConditionBit = State.get(BlockInMask, Part);
    if (ConditionBit->getType()->isVectorTy())
      ConditionBit = State.Builder.CreateExtractElement(
          ConditionBit, State.Builder.getInt32(Lane));
  }

  // Replace the temporary unreachable terminator with a new conditional branch,
  // whose two destinations will be set later when they are created.
  auto *CurrentTerminator = State.CFG.PrevBB->getTerminator();
  assert(isa<UnreachableInst>(CurrentTerminator) &&
         "Expected to replace unreachable terminator with conditional branch.");
  auto *CondBr = BranchInst::Create(State.CFG.PrevBB, nullptr, ConditionBit);
  CondBr->setSuccessor(0, nullptr);
  ReplaceInstWithInst(CurrentTerminator, CondBr);
}

void VPPredInstPHIRecipe::execute(VPTransformState &State) {
  assert(State.Instance && "Predicated instruction PHI works per instance.");
  Instruction *ScalarPredInst = cast<Instruction>(
      State.ValueMap.getScalarValue(PredInst, *State.Instance));
  BasicBlock *PredicatedBB = ScalarPredInst->getParent();
  BasicBlock *PredicatingBB = PredicatedBB->getSinglePredecessor();
  assert(PredicatingBB && "Predicated block has no single predecessor.");

  // By current pack/unpack logic we need to generate only a single phi node: if
  // a vector value for the predicated instruction exists at this point it means
  // the instruction has vector users only, and a phi for the vector value is
  // needed. In this case the recipe of the predicated instruction is marked to
  // also do that packing, thereby "hoisting" the insert-element sequence.
  // Otherwise, a phi node for the scalar value is needed.
  unsigned Part = State.Instance->Part;
  if (State.ValueMap.hasVectorValue(PredInst, Part)) {
    Value *VectorValue = State.ValueMap.getVectorValue(PredInst, Part);
    InsertElementInst *IEI = cast<InsertElementInst>(VectorValue);
    PHINode *VPhi = State.Builder.CreatePHI(IEI->getType(), 2);
    VPhi->addIncoming(IEI->getOperand(0), PredicatingBB); // Unmodified vector.
    VPhi->addIncoming(IEI, PredicatedBB); // New vector with inserted element.
    State.ValueMap.resetVectorValue(PredInst, Part, VPhi); // Update cache.
  } else {
    Type *PredInstType = PredInst->getType();
    PHINode *Phi = State.Builder.CreatePHI(PredInstType, 2);
    Phi->addIncoming(UndefValue::get(ScalarPredInst->getType()), PredicatingBB);
    Phi->addIncoming(ScalarPredInst, PredicatedBB);
    State.ValueMap.resetScalarValue(PredInst, *State.Instance, Phi);
  }
}

void VPWidenMemoryInstructionRecipe::execute(VPTransformState &State) {
  if (!User)
    return State.ILV->vectorizeMemoryInstruction(&Instr);

  // Last (and currently only) operand is a mask.
  InnerLoopVectorizer::VectorParts MaskValues(State.UF);
  VPValue *Mask = User->getOperand(User->getNumOperands() - 1);
  for (unsigned Part = 0; Part < State.UF; ++Part)
    MaskValues[Part] = State.get(Mask, Part);
  State.ILV->vectorizeMemoryInstruction(&Instr, &MaskValues);
}

// Process the loop in the VPlan-native vectorization path. This path builds
// VPlan upfront in the vectorization pipeline, which allows to apply
// VPlan-to-VPlan transformations from the very beginning without modifying the
// input LLVM IR.
static bool processLoopInVPlanNativePath(
    Loop *L, PredicatedScalarEvolution &PSE, LoopInfo *LI, DominatorTree *DT,
    LoopVectorizationLegality *LVL, TargetTransformInfo *TTI,
    TargetLibraryInfo *TLI, DemandedBits *DB, AssumptionCache *AC,
    OptimizationRemarkEmitter *ORE, LoopVectorizeHints &Hints) {

  assert(EnableVPlanNativePath && "VPlan-native path is disabled.");
  Function *F = L->getHeader()->getParent();
  InterleavedAccessInfo IAI(PSE, L, DT, LI, LVL->getLAI());
  LoopVectorizationCostModel CM(L, PSE, LI, LVL, *TTI, TLI, DB, AC, ORE, F,
                                &Hints, IAI);
  // Use the planner for outer loop vectorization.
  // TODO: CM is not used at this point inside the planner. Turn CM into an
  // optional argument if we don't need it in the future.
  LoopVectorizationPlanner LVP(L, LI, TLI, TTI, LVL, CM);

  // Get user vectorization factor.
  unsigned UserVF = Hints.getWidth();

  // Check the function attributes to find out if this function should be
  // optimized for size.
  bool OptForSize =
      Hints.getForce() != LoopVectorizeHints::FK_Enabled && F->optForSize();

  // Plan how to best vectorize, return the best VF and its cost.
  VectorizationFactor VF = LVP.planInVPlanNativePath(OptForSize, UserVF);

  // If we are stress testing VPlan builds, do not attempt to generate vector
  // code.
  if (VPlanBuildStressTest)
    return false;

  LVP.setBestPlan(VF.Width, 1);

  InnerLoopVectorizer LB(L, PSE, LI, DT, TLI, TTI, AC, ORE, UserVF, 1, LVL,
                         &CM);
  LLVM_DEBUG(dbgs() << "Vectorizing outer loop in \""
                    << L->getHeader()->getParent()->getName() << "\"\n");
  LVP.executePlan(LB, DT);

  // Mark the loop as already vectorized to avoid vectorizing again.
  Hints.setAlreadyVectorized();

  LLVM_DEBUG(verifyFunction(*L->getHeader()->getParent()));
  return true;
}

bool LoopVectorizePass::processLoop(Loop *L) {
  assert((EnableVPlanNativePath || L->empty()) &&
         "VPlan-native path is not enabled. Only process inner loops.");

#ifndef NDEBUG
  const std::string DebugLocStr = getDebugLocString(L);
#endif /* NDEBUG */

  LLVM_DEBUG(dbgs() << "\nLV: Checking a loop in \""
                    << L->getHeader()->getParent()->getName() << "\" from "
                    << DebugLocStr << "\n");

  LoopVectorizeHints Hints(L, InterleaveOnlyWhenForced, *ORE);

  LLVM_DEBUG(
      dbgs() << "LV: Loop hints:"
             << " force="
             << (Hints.getForce() == LoopVectorizeHints::FK_Disabled
                     ? "disabled"
                     : (Hints.getForce() == LoopVectorizeHints::FK_Enabled
                            ? "enabled"
                            : "?"))
             << " width=" << Hints.getWidth()
             << " unroll=" << Hints.getInterleave() << "\n");

  // Function containing loop
  Function *F = L->getHeader()->getParent();

  // Looking at the diagnostic output is the only way to determine if a loop
  // was vectorized (other than looking at the IR or machine code), so it
  // is important to generate an optimization remark for each loop. Most of
  // these messages are generated as OptimizationRemarkAnalysis. Remarks
  // generated as OptimizationRemark and OptimizationRemarkMissed are
  // less verbose reporting vectorized loops and unvectorized loops that may
  // benefit from vectorization, respectively.

  if (!Hints.allowVectorization(F, L, VectorizeOnlyWhenForced)) {
    LLVM_DEBUG(dbgs() << "LV: Loop hints prevent vectorization.\n");
    return false;
  }

  PredicatedScalarEvolution PSE(*SE, *L);

  // Check if it is legal to vectorize the loop.
  LoopVectorizationRequirements Requirements(*ORE);
  LoopVectorizationLegality LVL(L, PSE, DT, TLI, AA, F, GetLAA, LI, ORE,
                                &Requirements, &Hints, DB, AC);
  if (!LVL.canVectorize(EnableVPlanNativePath)) {
    LLVM_DEBUG(dbgs() << "LV: Not vectorizing: Cannot prove legality.\n");
    Hints.emitRemarkWithHints();
    return false;
  }

  // Check the function attributes to find out if this function should be
  // optimized for size.
  bool OptForSize =
      Hints.getForce() != LoopVectorizeHints::FK_Enabled && F->optForSize();

  // Entrance to the VPlan-native vectorization path. Outer loops are processed
  // here. They may require CFG and instruction level transformations before
  // even evaluating whether vectorization is profitable. Since we cannot modify
  // the incoming IR, we need to build VPlan upfront in the vectorization
  // pipeline.
  if (!L->empty())
    return processLoopInVPlanNativePath(L, PSE, LI, DT, &LVL, TTI, TLI, DB, AC,
                                        ORE, Hints);

  assert(L->empty() && "Inner loop expected.");
  // Check the loop for a trip count threshold: vectorize loops with a tiny trip
  // count by optimizing for size, to minimize overheads.
  // Prefer constant trip counts over profile data, over upper bound estimate.
  unsigned ExpectedTC = 0;
  bool HasExpectedTC = false;
  if (const SCEVConstant *ConstExits =
      dyn_cast<SCEVConstant>(SE->getBackedgeTakenCount(L))) {
    const APInt &ExitsCount = ConstExits->getAPInt();
    // We are interested in small values for ExpectedTC. Skip over those that
    // can't fit an unsigned.
    if (ExitsCount.ult(std::numeric_limits<unsigned>::max())) {
      ExpectedTC = static_cast<unsigned>(ExitsCount.getZExtValue()) + 1;
      HasExpectedTC = true;
    }
  }
  // ExpectedTC may be large because it's bound by a variable. Check
  // profiling information to validate we should vectorize.
  if (!HasExpectedTC && LoopVectorizeWithBlockFrequency) {
    auto EstimatedTC = getLoopEstimatedTripCount(L);
    if (EstimatedTC) {
      ExpectedTC = *EstimatedTC;
      HasExpectedTC = true;
    }
  }
  if (!HasExpectedTC) {
    ExpectedTC = SE->getSmallConstantMaxTripCount(L);
    HasExpectedTC = (ExpectedTC > 0);
  }

  if (HasExpectedTC && ExpectedTC < TinyTripCountVectorThreshold) {
    LLVM_DEBUG(dbgs() << "LV: Found a loop with a very small trip count. "
                      << "This loop is worth vectorizing only if no scalar "
                      << "iteration overheads are incurred.");
    if (Hints.getForce() == LoopVectorizeHints::FK_Enabled)
      LLVM_DEBUG(dbgs() << " But vectorizing was explicitly forced.\n");
    else {
      LLVM_DEBUG(dbgs() << "\n");
      // Loops with a very small trip count are considered for vectorization
      // under OptForSize, thereby making sure the cost of their loop body is
      // dominant, free of runtime guards and scalar iteration overheads.
      OptForSize = true;
    }
  }

  // Check the function attributes to see if implicit floats are allowed.
  // FIXME: This check doesn't seem possibly correct -- what if the loop is
  // an integer loop and the vector instructions selected are purely integer
  // vector instructions?
  if (F->hasFnAttribute(Attribute::NoImplicitFloat)) {
    LLVM_DEBUG(dbgs() << "LV: Can't vectorize when the NoImplicitFloat"
                         "attribute is used.\n");
    ORE->emit(createLVMissedAnalysis(Hints.vectorizeAnalysisPassName(),
                                     "NoImplicitFloat", L)
              << "loop not vectorized due to NoImplicitFloat attribute");
    Hints.emitRemarkWithHints();
    return false;
  }

  // Check if the target supports potentially unsafe FP vectorization.
  // FIXME: Add a check for the type of safety issue (denormal, signaling)
  // for the target we're vectorizing for, to make sure none of the
  // additional fp-math flags can help.
  if (Hints.isPotentiallyUnsafe() &&
      TTI->isFPVectorizationPotentiallyUnsafe()) {
    LLVM_DEBUG(
        dbgs() << "LV: Potentially unsafe FP op prevents vectorization.\n");
    ORE->emit(
        createLVMissedAnalysis(Hints.vectorizeAnalysisPassName(), "UnsafeFP", L)
        << "loop not vectorized due to unsafe FP support.");
    Hints.emitRemarkWithHints();
    return false;
  }

  bool UseInterleaved = TTI->enableInterleavedAccessVectorization();
  InterleavedAccessInfo IAI(PSE, L, DT, LI, LVL.getLAI());

  // If an override option has been passed in for interleaved accesses, use it.
  if (EnableInterleavedMemAccesses.getNumOccurrences() > 0)
    UseInterleaved = EnableInterleavedMemAccesses;

  // Analyze interleaved memory accesses.
  if (UseInterleaved) {
    IAI.analyzeInterleaving(useMaskedInterleavedAccesses(*TTI));
  }

  // Use the cost model.
  LoopVectorizationCostModel CM(L, PSE, LI, &LVL, *TTI, TLI, DB, AC, ORE, F,
                                &Hints, IAI);
  CM.collectValuesToIgnore();

  // Use the planner for vectorization.
  LoopVectorizationPlanner LVP(L, LI, TLI, TTI, &LVL, CM);

  // Get user vectorization factor.
  unsigned UserVF = Hints.getWidth();

  // Plan how to best vectorize, return the best VF and its cost.
  VectorizationFactor VF = LVP.plan(OptForSize, UserVF);

  // Select the interleave count.
  unsigned IC = CM.selectInterleaveCount(OptForSize, VF.Width, VF.Cost);

  // Get user interleave count.
  unsigned UserIC = Hints.getInterleave();

  // Identify the diagnostic messages that should be produced.
  std::pair<StringRef, std::string> VecDiagMsg, IntDiagMsg;
  bool VectorizeLoop = true, InterleaveLoop = true;
  if (Requirements.doesNotMeet(F, L, Hints)) {
    LLVM_DEBUG(dbgs() << "LV: Not vectorizing: loop did not meet vectorization "
                         "requirements.\n");
    Hints.emitRemarkWithHints();
    return false;
  }

  if (VF.Width == 1) {
    LLVM_DEBUG(dbgs() << "LV: Vectorization is possible but not beneficial.\n");
    VecDiagMsg = std::make_pair(
        "VectorizationNotBeneficial",
        "the cost-model indicates that vectorization is not beneficial");
    VectorizeLoop = false;
  }

  if (IC == 1 && UserIC <= 1) {
    // Tell the user interleaving is not beneficial.
    LLVM_DEBUG(dbgs() << "LV: Interleaving is not beneficial.\n");
    IntDiagMsg = std::make_pair(
        "InterleavingNotBeneficial",
        "the cost-model indicates that interleaving is not beneficial");
    InterleaveLoop = false;
    if (UserIC == 1) {
      IntDiagMsg.first = "InterleavingNotBeneficialAndDisabled";
      IntDiagMsg.second +=
          " and is explicitly disabled or interleave count is set to 1";
    }
  } else if (IC > 1 && UserIC == 1) {
    // Tell the user interleaving is beneficial, but it explicitly disabled.
    LLVM_DEBUG(
        dbgs() << "LV: Interleaving is beneficial but is explicitly disabled.");
    IntDiagMsg = std::make_pair(
        "InterleavingBeneficialButDisabled",
        "the cost-model indicates that interleaving is beneficial "
        "but is explicitly disabled or interleave count is set to 1");
    InterleaveLoop = false;
  }

  // Override IC if user provided an interleave count.
  IC = UserIC > 0 ? UserIC : IC;

  // Emit diagnostic messages, if any.
  const char *VAPassName = Hints.vectorizeAnalysisPassName();
  if (!VectorizeLoop && !InterleaveLoop) {
    // Do not vectorize or interleaving the loop.
    ORE->emit([&]() {
      return OptimizationRemarkMissed(VAPassName, VecDiagMsg.first,
                                      L->getStartLoc(), L->getHeader())
             << VecDiagMsg.second;
    });
    ORE->emit([&]() {
      return OptimizationRemarkMissed(LV_NAME, IntDiagMsg.first,
                                      L->getStartLoc(), L->getHeader())
             << IntDiagMsg.second;
    });
    return false;
  } else if (!VectorizeLoop && InterleaveLoop) {
    LLVM_DEBUG(dbgs() << "LV: Interleave Count is " << IC << '\n');
    ORE->emit([&]() {
      return OptimizationRemarkAnalysis(VAPassName, VecDiagMsg.first,
                                        L->getStartLoc(), L->getHeader())
             << VecDiagMsg.second;
    });
  } else if (VectorizeLoop && !InterleaveLoop) {
    LLVM_DEBUG(dbgs() << "LV: Found a vectorizable loop (" << VF.Width
                      << ") in " << DebugLocStr << '\n');
    ORE->emit([&]() {
      return OptimizationRemarkAnalysis(LV_NAME, IntDiagMsg.first,
                                        L->getStartLoc(), L->getHeader())
             << IntDiagMsg.second;
    });
  } else if (VectorizeLoop && InterleaveLoop) {
    LLVM_DEBUG(dbgs() << "LV: Found a vectorizable loop (" << VF.Width
                      << ") in " << DebugLocStr << '\n');
    LLVM_DEBUG(dbgs() << "LV: Interleave Count is " << IC << '\n');
  }

  LVP.setBestPlan(VF.Width, IC);

  using namespace ore;
  bool DisableRuntimeUnroll = false;
  MDNode *OrigLoopID = L->getLoopID();

  if (!VectorizeLoop) {
    assert(IC > 1 && "interleave count should not be 1 or 0");
    // If we decided that it is not legal to vectorize the loop, then
    // interleave it.
    InnerLoopUnroller Unroller(L, PSE, LI, DT, TLI, TTI, AC, ORE, IC, &LVL,
                               &CM);
    LVP.executePlan(Unroller, DT);

    ORE->emit([&]() {
      return OptimizationRemark(LV_NAME, "Interleaved", L->getStartLoc(),
                                L->getHeader())
             << "interleaved loop (interleaved count: "
             << NV("InterleaveCount", IC) << ")";
    });
  } else {
    // If we decided that it is *legal* to vectorize the loop, then do it.
    InnerLoopVectorizer LB(L, PSE, LI, DT, TLI, TTI, AC, ORE, VF.Width, IC,
                           &LVL, &CM);
    LVP.executePlan(LB, DT);
    ++LoopsVectorized;

    // Add metadata to disable runtime unrolling a scalar loop when there are
    // no runtime checks about strides and memory. A scalar loop that is
    // rarely used is not worth unrolling.
    if (!LB.areSafetyChecksAdded())
      DisableRuntimeUnroll = true;

    // Report the vectorization decision.
    ORE->emit([&]() {
      return OptimizationRemark(LV_NAME, "Vectorized", L->getStartLoc(),
                                L->getHeader())
             << "vectorized loop (vectorization width: "
             << NV("VectorizationFactor", VF.Width)
             << ", interleaved count: " << NV("InterleaveCount", IC) << ")";
    });
  }

  Optional<MDNode *> RemainderLoopID =
      makeFollowupLoopID(OrigLoopID, {LLVMLoopVectorizeFollowupAll,
                                      LLVMLoopVectorizeFollowupEpilogue});
  if (RemainderLoopID.hasValue()) {
    L->setLoopID(RemainderLoopID.getValue());
  } else {
    if (DisableRuntimeUnroll)
      AddRuntimeUnrollDisableMetaData(L);

    // Mark the loop as already vectorized to avoid vectorizing again.
    Hints.setAlreadyVectorized();
  }

  LLVM_DEBUG(verifyFunction(*L->getHeader()->getParent()));
  return true;
}

bool LoopVectorizePass::runImpl(
    Function &F, ScalarEvolution &SE_, LoopInfo &LI_, TargetTransformInfo &TTI_,
    DominatorTree &DT_, BlockFrequencyInfo &BFI_, TargetLibraryInfo *TLI_,
    DemandedBits &DB_, AliasAnalysis &AA_, AssumptionCache &AC_,
    std::function<const LoopAccessInfo &(Loop &)> &GetLAA_,
    OptimizationRemarkEmitter &ORE_) {
  SE = &SE_;
  LI = &LI_;
  TTI = &TTI_;
  DT = &DT_;
  BFI = &BFI_;
  TLI = TLI_;
  AA = &AA_;
  AC = &AC_;
  GetLAA = &GetLAA_;
  DB = &DB_;
  ORE = &ORE_;

  // Don't attempt if
  // 1. the target claims to have no vector registers, and
  // 2. interleaving won't help ILP.
  //
  // The second condition is necessary because, even if the target has no
  // vector registers, loop vectorization may still enable scalar
  // interleaving.
  if (!TTI->getNumberOfRegisters(true) && TTI->getMaxInterleaveFactor(1) < 2)
    return false;

  bool Changed = false;

  // The vectorizer requires loops to be in simplified form.
  // Since simplification may add new inner loops, it has to run before the
  // legality and profitability checks. This means running the loop vectorizer
  // will simplify all loops, regardless of whether anything end up being
  // vectorized.
  for (auto &L : *LI)
    Changed |= simplifyLoop(L, DT, LI, SE, AC, false /* PreserveLCSSA */);

  // Build up a worklist of inner-loops to vectorize. This is necessary as
  // the act of vectorizing or partially unrolling a loop creates new loops
  // and can invalidate iterators across the loops.
  SmallVector<Loop *, 8> Worklist;

  for (Loop *L : *LI)
    collectSupportedLoops(*L, LI, ORE, Worklist);

  LoopsAnalyzed += Worklist.size();

  // Now walk the identified inner loops.
  while (!Worklist.empty()) {
    Loop *L = Worklist.pop_back_val();

    // For the inner loops we actually process, form LCSSA to simplify the
    // transform.
    Changed |= formLCSSARecursively(*L, *DT, LI, SE);

    Changed |= processLoop(L);
  }

  // Process each loop nest in the function.
  return Changed;
}

PreservedAnalyses LoopVectorizePass::run(Function &F,
                                         FunctionAnalysisManager &AM) {
    auto &SE = AM.getResult<ScalarEvolutionAnalysis>(F);
    auto &LI = AM.getResult<LoopAnalysis>(F);
    auto &TTI = AM.getResult<TargetIRAnalysis>(F);
    auto &DT = AM.getResult<DominatorTreeAnalysis>(F);
    auto &BFI = AM.getResult<BlockFrequencyAnalysis>(F);
    auto &TLI = AM.getResult<TargetLibraryAnalysis>(F);
    auto &AA = AM.getResult<AAManager>(F);
    auto &AC = AM.getResult<AssumptionAnalysis>(F);
    auto &DB = AM.getResult<DemandedBitsAnalysis>(F);
    auto &ORE = AM.getResult<OptimizationRemarkEmitterAnalysis>(F);

    auto &LAM = AM.getResult<LoopAnalysisManagerFunctionProxy>(F).getManager();
    std::function<const LoopAccessInfo &(Loop &)> GetLAA =
        [&](Loop &L) -> const LoopAccessInfo & {
      LoopStandardAnalysisResults AR = {AA, AC, DT, LI, SE, TLI, TTI, nullptr};
      return LAM.getResult<LoopAccessAnalysis>(L, AR);
    };
    bool Changed =
        runImpl(F, SE, LI, TTI, DT, BFI, &TLI, DB, AA, AC, GetLAA, ORE);
    if (!Changed)
      return PreservedAnalyses::all();
    PreservedAnalyses PA;

    // We currently do not preserve loopinfo/dominator analyses with outer loop
    // vectorization. Until this is addressed, mark these analyses as preserved
    // only for non-VPlan-native path.
    // TODO: Preserve Loop and Dominator analyses for VPlan-native path.
    if (!EnableVPlanNativePath) {
      PA.preserve<LoopAnalysis>();
      PA.preserve<DominatorTreeAnalysis>();
    }
    PA.preserve<BasicAA>();
    PA.preserve<GlobalsAA>();
    return PA;
}
