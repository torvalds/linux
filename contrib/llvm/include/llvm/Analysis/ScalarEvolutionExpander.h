//===---- llvm/Analysis/ScalarEvolutionExpander.h - SCEV Exprs --*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file defines the classes used to generate code from scalar expressions.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_ANALYSIS_SCALAREVOLUTIONEXPANDER_H
#define LLVM_ANALYSIS_SCALAREVOLUTIONEXPANDER_H

#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/DenseSet.h"
#include "llvm/ADT/Optional.h"
#include "llvm/Analysis/ScalarEvolutionExpressions.h"
#include "llvm/Analysis/ScalarEvolutionNormalization.h"
#include "llvm/Analysis/TargetFolder.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/ValueHandle.h"

namespace llvm {
  class TargetTransformInfo;

  /// Return true if the given expression is safe to expand in the sense that
  /// all materialized values are safe to speculate anywhere their operands are
  /// defined.
  bool isSafeToExpand(const SCEV *S, ScalarEvolution &SE);

  /// Return true if the given expression is safe to expand in the sense that
  /// all materialized values are defined and safe to speculate at the specified
  /// location and their operands are defined at this location.
  bool isSafeToExpandAt(const SCEV *S, const Instruction *InsertionPoint,
                        ScalarEvolution &SE);

  /// This class uses information about analyze scalars to rewrite expressions
  /// in canonical form.
  ///
  /// Clients should create an instance of this class when rewriting is needed,
  /// and destroy it when finished to allow the release of the associated
  /// memory.
  class SCEVExpander : public SCEVVisitor<SCEVExpander, Value*> {
    ScalarEvolution &SE;
    const DataLayout &DL;

    // New instructions receive a name to identify them with the current pass.
    const char* IVName;

    // InsertedExpressions caches Values for reuse, so must track RAUW.
    DenseMap<std::pair<const SCEV *, Instruction *>, TrackingVH<Value>>
        InsertedExpressions;

    // InsertedValues only flags inserted instructions so needs no RAUW.
    DenseSet<AssertingVH<Value>> InsertedValues;
    DenseSet<AssertingVH<Value>> InsertedPostIncValues;

    /// A memoization of the "relevant" loop for a given SCEV.
    DenseMap<const SCEV *, const Loop *> RelevantLoops;

    /// Addrecs referring to any of the given loops are expanded in post-inc
    /// mode. For example, expanding {1,+,1}<L> in post-inc mode returns the add
    /// instruction that adds one to the phi for {0,+,1}<L>, as opposed to a new
    /// phi starting at 1. This is only supported in non-canonical mode.
    PostIncLoopSet PostIncLoops;

    /// When this is non-null, addrecs expanded in the loop it indicates should
    /// be inserted with increments at IVIncInsertPos.
    const Loop *IVIncInsertLoop;

    /// When expanding addrecs in the IVIncInsertLoop loop, insert the IV
    /// increment at this position.
    Instruction *IVIncInsertPos;

    /// Phis that complete an IV chain. Reuse
    DenseSet<AssertingVH<PHINode>> ChainedPhis;

    /// When true, expressions are expanded in "canonical" form. In particular,
    /// addrecs are expanded as arithmetic based on a canonical induction
    /// variable. When false, expression are expanded in a more literal form.
    bool CanonicalMode;

    /// When invoked from LSR, the expander is in "strength reduction" mode. The
    /// only difference is that phi's are only reused if they are already in
    /// "expanded" form.
    bool LSRMode;

    typedef IRBuilder<TargetFolder> BuilderType;
    BuilderType Builder;

    // RAII object that stores the current insertion point and restores it when
    // the object is destroyed. This includes the debug location.  Duplicated
    // from InsertPointGuard to add SetInsertPoint() which is used to updated
    // InsertPointGuards stack when insert points are moved during SCEV
    // expansion.
    class SCEVInsertPointGuard {
      IRBuilderBase &Builder;
      AssertingVH<BasicBlock> Block;
      BasicBlock::iterator Point;
      DebugLoc DbgLoc;
      SCEVExpander *SE;

      SCEVInsertPointGuard(const SCEVInsertPointGuard &) = delete;
      SCEVInsertPointGuard &operator=(const SCEVInsertPointGuard &) = delete;

    public:
      SCEVInsertPointGuard(IRBuilderBase &B, SCEVExpander *SE)
          : Builder(B), Block(B.GetInsertBlock()), Point(B.GetInsertPoint()),
            DbgLoc(B.getCurrentDebugLocation()), SE(SE) {
        SE->InsertPointGuards.push_back(this);
      }

      ~SCEVInsertPointGuard() {
        // These guards should always created/destroyed in FIFO order since they
        // are used to guard lexically scoped blocks of code in
        // ScalarEvolutionExpander.
        assert(SE->InsertPointGuards.back() == this);
        SE->InsertPointGuards.pop_back();
        Builder.restoreIP(IRBuilderBase::InsertPoint(Block, Point));
        Builder.SetCurrentDebugLocation(DbgLoc);
      }

      BasicBlock::iterator GetInsertPoint() const { return Point; }
      void SetInsertPoint(BasicBlock::iterator I) { Point = I; }
    };

    /// Stack of pointers to saved insert points, used to keep insert points
    /// consistent when instructions are moved.
    SmallVector<SCEVInsertPointGuard *, 8> InsertPointGuards;

#ifndef NDEBUG
    const char *DebugType;
#endif

    friend struct SCEVVisitor<SCEVExpander, Value*>;

  public:
    /// Construct a SCEVExpander in "canonical" mode.
    explicit SCEVExpander(ScalarEvolution &se, const DataLayout &DL,
                          const char *name)
        : SE(se), DL(DL), IVName(name), IVIncInsertLoop(nullptr),
          IVIncInsertPos(nullptr), CanonicalMode(true), LSRMode(false),
          Builder(se.getContext(), TargetFolder(DL)) {
#ifndef NDEBUG
      DebugType = "";
#endif
    }

    ~SCEVExpander() {
      // Make sure the insert point guard stack is consistent.
      assert(InsertPointGuards.empty());
    }

#ifndef NDEBUG
    void setDebugType(const char* s) { DebugType = s; }
#endif

    /// Erase the contents of the InsertedExpressions map so that users trying
    /// to expand the same expression into multiple BasicBlocks or different
    /// places within the same BasicBlock can do so.
    void clear() {
      InsertedExpressions.clear();
      InsertedValues.clear();
      InsertedPostIncValues.clear();
      ChainedPhis.clear();
    }

    /// Return true for expressions that may incur non-trivial cost to evaluate
    /// at runtime.
    ///
    /// At is an optional parameter which specifies point in code where user is
    /// going to expand this expression. Sometimes this knowledge can lead to a
    /// more accurate cost estimation.
    bool isHighCostExpansion(const SCEV *Expr, Loop *L,
                             const Instruction *At = nullptr) {
      SmallPtrSet<const SCEV *, 8> Processed;
      return isHighCostExpansionHelper(Expr, L, At, Processed);
    }

    /// This method returns the canonical induction variable of the specified
    /// type for the specified loop (inserting one if there is none).  A
    /// canonical induction variable starts at zero and steps by one on each
    /// iteration.
    PHINode *getOrInsertCanonicalInductionVariable(const Loop *L, Type *Ty);

    /// Return the induction variable increment's IV operand.
    Instruction *getIVIncOperand(Instruction *IncV, Instruction *InsertPos,
                                 bool allowScale);

    /// Utility for hoisting an IV increment.
    bool hoistIVInc(Instruction *IncV, Instruction *InsertPos);

    /// replace congruent phis with their most canonical representative. Return
    /// the number of phis eliminated.
    unsigned replaceCongruentIVs(Loop *L, const DominatorTree *DT,
                                 SmallVectorImpl<WeakTrackingVH> &DeadInsts,
                                 const TargetTransformInfo *TTI = nullptr);

    /// Insert code to directly compute the specified SCEV expression into the
    /// program.  The inserted code is inserted into the specified block.
    Value *expandCodeFor(const SCEV *SH, Type *Ty, Instruction *I);

    /// Insert code to directly compute the specified SCEV expression into the
    /// program.  The inserted code is inserted into the SCEVExpander's current
    /// insertion point. If a type is specified, the result will be expanded to
    /// have that type, with a cast if necessary.
    Value *expandCodeFor(const SCEV *SH, Type *Ty = nullptr);


    /// Generates a code sequence that evaluates this predicate.  The inserted
    /// instructions will be at position \p Loc.  The result will be of type i1
    /// and will have a value of 0 when the predicate is false and 1 otherwise.
    Value *expandCodeForPredicate(const SCEVPredicate *Pred, Instruction *Loc);

    /// A specialized variant of expandCodeForPredicate, handling the case when
    /// we are expanding code for a SCEVEqualPredicate.
    Value *expandEqualPredicate(const SCEVEqualPredicate *Pred,
                                Instruction *Loc);

    /// Generates code that evaluates if the \p AR expression will overflow.
    Value *generateOverflowCheck(const SCEVAddRecExpr *AR, Instruction *Loc,
                                 bool Signed);

    /// A specialized variant of expandCodeForPredicate, handling the case when
    /// we are expanding code for a SCEVWrapPredicate.
    Value *expandWrapPredicate(const SCEVWrapPredicate *P, Instruction *Loc);

    /// A specialized variant of expandCodeForPredicate, handling the case when
    /// we are expanding code for a SCEVUnionPredicate.
    Value *expandUnionPredicate(const SCEVUnionPredicate *Pred,
                                Instruction *Loc);

    /// Set the current IV increment loop and position.
    void setIVIncInsertPos(const Loop *L, Instruction *Pos) {
      assert(!CanonicalMode &&
             "IV increment positions are not supported in CanonicalMode");
      IVIncInsertLoop = L;
      IVIncInsertPos = Pos;
    }

    /// Enable post-inc expansion for addrecs referring to the given
    /// loops. Post-inc expansion is only supported in non-canonical mode.
    void setPostInc(const PostIncLoopSet &L) {
      assert(!CanonicalMode &&
             "Post-inc expansion is not supported in CanonicalMode");
      PostIncLoops = L;
    }

    /// Disable all post-inc expansion.
    void clearPostInc() {
      PostIncLoops.clear();

      // When we change the post-inc loop set, cached expansions may no
      // longer be valid.
      InsertedPostIncValues.clear();
    }

    /// Disable the behavior of expanding expressions in canonical form rather
    /// than in a more literal form. Non-canonical mode is useful for late
    /// optimization passes.
    void disableCanonicalMode() { CanonicalMode = false; }

    void enableLSRMode() { LSRMode = true; }

    /// Set the current insertion point. This is useful if multiple calls to
    /// expandCodeFor() are going to be made with the same insert point and the
    /// insert point may be moved during one of the expansions (e.g. if the
    /// insert point is not a block terminator).
    void setInsertPoint(Instruction *IP) {
      assert(IP);
      Builder.SetInsertPoint(IP);
    }

    /// Clear the current insertion point. This is useful if the instruction
    /// that had been serving as the insertion point may have been deleted.
    void clearInsertPoint() {
      Builder.ClearInsertionPoint();
    }

    /// Return true if the specified instruction was inserted by the code
    /// rewriter.  If so, the client should not modify the instruction.
    bool isInsertedInstruction(Instruction *I) const {
      return InsertedValues.count(I) || InsertedPostIncValues.count(I);
    }

    void setChainedPhi(PHINode *PN) { ChainedPhis.insert(PN); }

    /// Try to find existing LLVM IR value for S available at the point At.
    Value *getExactExistingExpansion(const SCEV *S, const Instruction *At,
                                     Loop *L);

    /// Try to find the ValueOffsetPair for S. The function is mainly used to
    /// check whether S can be expanded cheaply.  If this returns a non-None
    /// value, we know we can codegen the `ValueOffsetPair` into a suitable
    /// expansion identical with S so that S can be expanded cheaply.
    ///
    /// L is a hint which tells in which loop to look for the suitable value.
    /// On success return value which is equivalent to the expanded S at point
    /// At. Return nullptr if value was not found.
    ///
    /// Note that this function does not perform an exhaustive search. I.e if it
    /// didn't find any value it does not mean that there is no such value.
    ///
    Optional<ScalarEvolution::ValueOffsetPair>
    getRelatedExistingExpansion(const SCEV *S, const Instruction *At, Loop *L);

  private:
    LLVMContext &getContext() const { return SE.getContext(); }

    /// Recursive helper function for isHighCostExpansion.
    bool isHighCostExpansionHelper(const SCEV *S, Loop *L,
                                   const Instruction *At,
                                   SmallPtrSetImpl<const SCEV *> &Processed);

    /// Insert the specified binary operator, doing a small amount of work to
    /// avoid inserting an obviously redundant operation.
    Value *InsertBinop(Instruction::BinaryOps Opcode, Value *LHS, Value *RHS);

    /// Arrange for there to be a cast of V to Ty at IP, reusing an existing
    /// cast if a suitable one exists, moving an existing cast if a suitable one
    /// exists but isn't in the right place, or creating a new one.
    Value *ReuseOrCreateCast(Value *V, Type *Ty,
                             Instruction::CastOps Op,
                             BasicBlock::iterator IP);

    /// Insert a cast of V to the specified type, which must be possible with a
    /// noop cast, doing what we can to share the casts.
    Value *InsertNoopCastOfTo(Value *V, Type *Ty);

    /// Expand a SCEVAddExpr with a pointer type into a GEP instead of using
    /// ptrtoint+arithmetic+inttoptr.
    Value *expandAddToGEP(const SCEV *const *op_begin,
                          const SCEV *const *op_end,
                          PointerType *PTy, Type *Ty, Value *V);
    Value *expandAddToGEP(const SCEV *Op, PointerType *PTy, Type *Ty, Value *V);

    /// Find a previous Value in ExprValueMap for expand.
    ScalarEvolution::ValueOffsetPair
    FindValueInExprValueMap(const SCEV *S, const Instruction *InsertPt);

    Value *expand(const SCEV *S);

    /// Determine the most "relevant" loop for the given SCEV.
    const Loop *getRelevantLoop(const SCEV *);

    Value *visitConstant(const SCEVConstant *S) {
      return S->getValue();
    }

    Value *visitTruncateExpr(const SCEVTruncateExpr *S);

    Value *visitZeroExtendExpr(const SCEVZeroExtendExpr *S);

    Value *visitSignExtendExpr(const SCEVSignExtendExpr *S);

    Value *visitAddExpr(const SCEVAddExpr *S);

    Value *visitMulExpr(const SCEVMulExpr *S);

    Value *visitUDivExpr(const SCEVUDivExpr *S);

    Value *visitAddRecExpr(const SCEVAddRecExpr *S);

    Value *visitSMaxExpr(const SCEVSMaxExpr *S);

    Value *visitUMaxExpr(const SCEVUMaxExpr *S);

    Value *visitUnknown(const SCEVUnknown *S) {
      return S->getValue();
    }

    void rememberInstruction(Value *I);

    bool isNormalAddRecExprPHI(PHINode *PN, Instruction *IncV, const Loop *L);

    bool isExpandedAddRecExprPHI(PHINode *PN, Instruction *IncV, const Loop *L);

    Value *expandAddRecExprLiterally(const SCEVAddRecExpr *);
    PHINode *getAddRecExprPHILiterally(const SCEVAddRecExpr *Normalized,
                                       const Loop *L,
                                       Type *ExpandTy,
                                       Type *IntTy,
                                       Type *&TruncTy,
                                       bool &InvertStep);
    Value *expandIVInc(PHINode *PN, Value *StepV, const Loop *L,
                       Type *ExpandTy, Type *IntTy, bool useSubtract);

    void hoistBeforePos(DominatorTree *DT, Instruction *InstToHoist,
                        Instruction *Pos, PHINode *LoopPhi);

    void fixupInsertPoints(Instruction *I);
  };
}

#endif
