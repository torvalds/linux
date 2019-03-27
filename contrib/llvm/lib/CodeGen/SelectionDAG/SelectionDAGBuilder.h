//===- SelectionDAGBuilder.h - Selection-DAG building -----------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This implements routines for translating from LLVM IR into SelectionDAG IR.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_CODEGEN_SELECTIONDAG_SELECTIONDAGBUILDER_H
#define LLVM_LIB_CODEGEN_SELECTIONDAG_SELECTIONDAGBUILDER_H

#include "StatepointLowering.h"
#include "llvm/ADT/APInt.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/Analysis/AliasAnalysis.h"
#include "llvm/CodeGen/ISDOpcodes.h"
#include "llvm/CodeGen/SelectionDAG.h"
#include "llvm/CodeGen/SelectionDAGNodes.h"
#include "llvm/CodeGen/TargetLowering.h"
#include "llvm/CodeGen/ValueTypes.h"
#include "llvm/IR/CallSite.h"
#include "llvm/IR/DebugLoc.h"
#include "llvm/IR/Instruction.h"
#include "llvm/IR/Statepoint.h"
#include "llvm/Support/BranchProbability.h"
#include "llvm/Support/CodeGen.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/MachineValueType.h"
#include <algorithm>
#include <cassert>
#include <cstdint>
#include <utility>
#include <vector>

namespace llvm {

class AllocaInst;
class AtomicCmpXchgInst;
class AtomicRMWInst;
class BasicBlock;
class BranchInst;
class CallInst;
class CatchPadInst;
class CatchReturnInst;
class CatchSwitchInst;
class CleanupPadInst;
class CleanupReturnInst;
class Constant;
class ConstantInt;
class ConstrainedFPIntrinsic;
class DbgValueInst;
class DataLayout;
class DIExpression;
class DILocalVariable;
class DILocation;
class FenceInst;
class FunctionLoweringInfo;
class GCFunctionInfo;
class GCRelocateInst;
class GCResultInst;
class IndirectBrInst;
class InvokeInst;
class LandingPadInst;
class LLVMContext;
class LoadInst;
class MachineBasicBlock;
class PHINode;
class ResumeInst;
class ReturnInst;
class SDDbgValue;
class StoreInst;
class SwitchInst;
class TargetLibraryInfo;
class TargetMachine;
class Type;
class VAArgInst;
class UnreachableInst;
class Use;
class User;
class Value;

//===----------------------------------------------------------------------===//
/// SelectionDAGBuilder - This is the common target-independent lowering
/// implementation that is parameterized by a TargetLowering object.
///
class SelectionDAGBuilder {
  /// CurInst - The current instruction being visited
  const Instruction *CurInst = nullptr;

  DenseMap<const Value*, SDValue> NodeMap;

  /// UnusedArgNodeMap - Maps argument value for unused arguments. This is used
  /// to preserve debug information for incoming arguments.
  DenseMap<const Value*, SDValue> UnusedArgNodeMap;

  /// DanglingDebugInfo - Helper type for DanglingDebugInfoMap.
  class DanglingDebugInfo {
    const DbgValueInst* DI = nullptr;
    DebugLoc dl;
    unsigned SDNodeOrder = 0;

  public:
    DanglingDebugInfo() = default;
    DanglingDebugInfo(const DbgValueInst *di, DebugLoc DL, unsigned SDNO)
        : DI(di), dl(std::move(DL)), SDNodeOrder(SDNO) {}

    const DbgValueInst* getDI() { return DI; }
    DebugLoc getdl() { return dl; }
    unsigned getSDNodeOrder() { return SDNodeOrder; }
  };

  /// DanglingDebugInfoVector - Helper type for DanglingDebugInfoMap.
  typedef std::vector<DanglingDebugInfo> DanglingDebugInfoVector;

  /// DanglingDebugInfoMap - Keeps track of dbg_values for which we have not
  /// yet seen the referent.  We defer handling these until we do see it.
  DenseMap<const Value*, DanglingDebugInfoVector> DanglingDebugInfoMap;

public:
  /// PendingLoads - Loads are not emitted to the program immediately.  We bunch
  /// them up and then emit token factor nodes when possible.  This allows us to
  /// get simple disambiguation between loads without worrying about alias
  /// analysis.
  SmallVector<SDValue, 8> PendingLoads;

  /// State used while lowering a statepoint sequence (gc_statepoint,
  /// gc_relocate, and gc_result).  See StatepointLowering.hpp/cpp for details.
  StatepointLoweringState StatepointLowering;

private:
  /// PendingExports - CopyToReg nodes that copy values to virtual registers
  /// for export to other blocks need to be emitted before any terminator
  /// instruction, but they have no other ordering requirements. We bunch them
  /// up and the emit a single tokenfactor for them just before terminator
  /// instructions.
  SmallVector<SDValue, 8> PendingExports;

  /// SDNodeOrder - A unique monotonically increasing number used to order the
  /// SDNodes we create.
  unsigned SDNodeOrder;

  enum CaseClusterKind {
    /// A cluster of adjacent case labels with the same destination, or just one
    /// case.
    CC_Range,
    /// A cluster of cases suitable for jump table lowering.
    CC_JumpTable,
    /// A cluster of cases suitable for bit test lowering.
    CC_BitTests
  };

  /// A cluster of case labels.
  struct CaseCluster {
    CaseClusterKind Kind;
    const ConstantInt *Low, *High;
    union {
      MachineBasicBlock *MBB;
      unsigned JTCasesIndex;
      unsigned BTCasesIndex;
    };
    BranchProbability Prob;

    static CaseCluster range(const ConstantInt *Low, const ConstantInt *High,
                             MachineBasicBlock *MBB, BranchProbability Prob) {
      CaseCluster C;
      C.Kind = CC_Range;
      C.Low = Low;
      C.High = High;
      C.MBB = MBB;
      C.Prob = Prob;
      return C;
    }

    static CaseCluster jumpTable(const ConstantInt *Low,
                                 const ConstantInt *High, unsigned JTCasesIndex,
                                 BranchProbability Prob) {
      CaseCluster C;
      C.Kind = CC_JumpTable;
      C.Low = Low;
      C.High = High;
      C.JTCasesIndex = JTCasesIndex;
      C.Prob = Prob;
      return C;
    }

    static CaseCluster bitTests(const ConstantInt *Low, const ConstantInt *High,
                                unsigned BTCasesIndex, BranchProbability Prob) {
      CaseCluster C;
      C.Kind = CC_BitTests;
      C.Low = Low;
      C.High = High;
      C.BTCasesIndex = BTCasesIndex;
      C.Prob = Prob;
      return C;
    }
  };

  using CaseClusterVector = std::vector<CaseCluster>;
  using CaseClusterIt = CaseClusterVector::iterator;

  struct CaseBits {
    uint64_t Mask = 0;
    MachineBasicBlock* BB = nullptr;
    unsigned Bits = 0;
    BranchProbability ExtraProb;

    CaseBits() = default;
    CaseBits(uint64_t mask, MachineBasicBlock* bb, unsigned bits,
             BranchProbability Prob):
      Mask(mask), BB(bb), Bits(bits), ExtraProb(Prob) {}
  };

  using CaseBitsVector = std::vector<CaseBits>;

  /// Sort Clusters and merge adjacent cases.
  void sortAndRangeify(CaseClusterVector &Clusters);

  /// CaseBlock - This structure is used to communicate between
  /// SelectionDAGBuilder and SDISel for the code generation of additional basic
  /// blocks needed by multi-case switch statements.
  struct CaseBlock {
    // CC - the condition code to use for the case block's setcc node
    ISD::CondCode CC;

    // CmpLHS/CmpRHS/CmpMHS - The LHS/MHS/RHS of the comparison to emit.
    // Emit by default LHS op RHS. MHS is used for range comparisons:
    // If MHS is not null: (LHS <= MHS) and (MHS <= RHS).
    const Value *CmpLHS, *CmpMHS, *CmpRHS;

    // TrueBB/FalseBB - the block to branch to if the setcc is true/false.
    MachineBasicBlock *TrueBB, *FalseBB;

    // ThisBB - the block into which to emit the code for the setcc and branches
    MachineBasicBlock *ThisBB;

    /// The debug location of the instruction this CaseBlock was
    /// produced from.
    SDLoc DL;

    // TrueProb/FalseProb - branch weights.
    BranchProbability TrueProb, FalseProb;

    CaseBlock(ISD::CondCode cc, const Value *cmplhs, const Value *cmprhs,
              const Value *cmpmiddle, MachineBasicBlock *truebb,
              MachineBasicBlock *falsebb, MachineBasicBlock *me,
              SDLoc dl,
              BranchProbability trueprob = BranchProbability::getUnknown(),
              BranchProbability falseprob = BranchProbability::getUnknown())
        : CC(cc), CmpLHS(cmplhs), CmpMHS(cmpmiddle), CmpRHS(cmprhs),
          TrueBB(truebb), FalseBB(falsebb), ThisBB(me), DL(dl),
          TrueProb(trueprob), FalseProb(falseprob) {}
  };

  struct JumpTable {
    /// Reg - the virtual register containing the index of the jump table entry
    //. to jump to.
    unsigned Reg;
    /// JTI - the JumpTableIndex for this jump table in the function.
    unsigned JTI;
    /// MBB - the MBB into which to emit the code for the indirect jump.
    MachineBasicBlock *MBB;
    /// Default - the MBB of the default bb, which is a successor of the range
    /// check MBB.  This is when updating PHI nodes in successors.
    MachineBasicBlock *Default;

    JumpTable(unsigned R, unsigned J, MachineBasicBlock *M,
              MachineBasicBlock *D): Reg(R), JTI(J), MBB(M), Default(D) {}
  };
  struct JumpTableHeader {
    APInt First;
    APInt Last;
    const Value *SValue;
    MachineBasicBlock *HeaderBB;
    bool Emitted;

    JumpTableHeader(APInt F, APInt L, const Value *SV, MachineBasicBlock *H,
                    bool E = false)
        : First(std::move(F)), Last(std::move(L)), SValue(SV), HeaderBB(H),
          Emitted(E) {}
  };
  using JumpTableBlock = std::pair<JumpTableHeader, JumpTable>;

  struct BitTestCase {
    uint64_t Mask;
    MachineBasicBlock *ThisBB;
    MachineBasicBlock *TargetBB;
    BranchProbability ExtraProb;

    BitTestCase(uint64_t M, MachineBasicBlock* T, MachineBasicBlock* Tr,
                BranchProbability Prob):
      Mask(M), ThisBB(T), TargetBB(Tr), ExtraProb(Prob) {}
  };

  using BitTestInfo = SmallVector<BitTestCase, 3>;

  struct BitTestBlock {
    APInt First;
    APInt Range;
    const Value *SValue;
    unsigned Reg;
    MVT RegVT;
    bool Emitted;
    bool ContiguousRange;
    MachineBasicBlock *Parent;
    MachineBasicBlock *Default;
    BitTestInfo Cases;
    BranchProbability Prob;
    BranchProbability DefaultProb;

    BitTestBlock(APInt F, APInt R, const Value *SV, unsigned Rg, MVT RgVT,
                 bool E, bool CR, MachineBasicBlock *P, MachineBasicBlock *D,
                 BitTestInfo C, BranchProbability Pr)
        : First(std::move(F)), Range(std::move(R)), SValue(SV), Reg(Rg),
          RegVT(RgVT), Emitted(E), ContiguousRange(CR), Parent(P), Default(D),
          Cases(std::move(C)), Prob(Pr) {}
  };

  /// Return the range of value in [First..Last].
  uint64_t getJumpTableRange(const CaseClusterVector &Clusters, unsigned First,
                             unsigned Last) const;

  /// Return the number of cases in [First..Last].
  uint64_t getJumpTableNumCases(const SmallVectorImpl<unsigned> &TotalCases,
                                unsigned First, unsigned Last) const;

  /// Build a jump table cluster from Clusters[First..Last]. Returns false if it
  /// decides it's not a good idea.
  bool buildJumpTable(const CaseClusterVector &Clusters, unsigned First,
                      unsigned Last, const SwitchInst *SI,
                      MachineBasicBlock *DefaultMBB, CaseCluster &JTCluster);

  /// Find clusters of cases suitable for jump table lowering.
  void findJumpTables(CaseClusterVector &Clusters, const SwitchInst *SI,
                      MachineBasicBlock *DefaultMBB);

  /// Build a bit test cluster from Clusters[First..Last]. Returns false if it
  /// decides it's not a good idea.
  bool buildBitTests(CaseClusterVector &Clusters, unsigned First, unsigned Last,
                     const SwitchInst *SI, CaseCluster &BTCluster);

  /// Find clusters of cases suitable for bit test lowering.
  void findBitTestClusters(CaseClusterVector &Clusters, const SwitchInst *SI);

  struct SwitchWorkListItem {
    MachineBasicBlock *MBB;
    CaseClusterIt FirstCluster;
    CaseClusterIt LastCluster;
    const ConstantInt *GE;
    const ConstantInt *LT;
    BranchProbability DefaultProb;
  };
  using SwitchWorkList = SmallVector<SwitchWorkListItem, 4>;

  /// Determine the rank by weight of CC in [First,Last]. If CC has more weight
  /// than each cluster in the range, its rank is 0.
  static unsigned caseClusterRank(const CaseCluster &CC, CaseClusterIt First,
                                  CaseClusterIt Last);

  /// Emit comparison and split W into two subtrees.
  void splitWorkItem(SwitchWorkList &WorkList, const SwitchWorkListItem &W,
                     Value *Cond, MachineBasicBlock *SwitchMBB);

  /// Lower W.
  void lowerWorkItem(SwitchWorkListItem W, Value *Cond,
                     MachineBasicBlock *SwitchMBB,
                     MachineBasicBlock *DefaultMBB);

  /// Peel the top probability case if it exceeds the threshold
  MachineBasicBlock *peelDominantCaseCluster(const SwitchInst &SI,
                                             CaseClusterVector &Clusters,
                                             BranchProbability &PeeledCaseProb);

  /// A class which encapsulates all of the information needed to generate a
  /// stack protector check and signals to isel via its state being initialized
  /// that a stack protector needs to be generated.
  ///
  /// *NOTE* The following is a high level documentation of SelectionDAG Stack
  /// Protector Generation. The reason that it is placed here is for a lack of
  /// other good places to stick it.
  ///
  /// High Level Overview of SelectionDAG Stack Protector Generation:
  ///
  /// Previously, generation of stack protectors was done exclusively in the
  /// pre-SelectionDAG Codegen LLVM IR Pass "Stack Protector". This necessitated
  /// splitting basic blocks at the IR level to create the success/failure basic
  /// blocks in the tail of the basic block in question. As a result of this,
  /// calls that would have qualified for the sibling call optimization were no
  /// longer eligible for optimization since said calls were no longer right in
  /// the "tail position" (i.e. the immediate predecessor of a ReturnInst
  /// instruction).
  ///
  /// Then it was noticed that since the sibling call optimization causes the
  /// callee to reuse the caller's stack, if we could delay the generation of
  /// the stack protector check until later in CodeGen after the sibling call
  /// decision was made, we get both the tail call optimization and the stack
  /// protector check!
  ///
  /// A few goals in solving this problem were:
  ///
  ///   1. Preserve the architecture independence of stack protector generation.
  ///
  ///   2. Preserve the normal IR level stack protector check for platforms like
  ///      OpenBSD for which we support platform-specific stack protector
  ///      generation.
  ///
  /// The main problem that guided the present solution is that one can not
  /// solve this problem in an architecture independent manner at the IR level
  /// only. This is because:
  ///
  ///   1. The decision on whether or not to perform a sibling call on certain
  ///      platforms (for instance i386) requires lower level information
  ///      related to available registers that can not be known at the IR level.
  ///
  ///   2. Even if the previous point were not true, the decision on whether to
  ///      perform a tail call is done in LowerCallTo in SelectionDAG which
  ///      occurs after the Stack Protector Pass. As a result, one would need to
  ///      put the relevant callinst into the stack protector check success
  ///      basic block (where the return inst is placed) and then move it back
  ///      later at SelectionDAG/MI time before the stack protector check if the
  ///      tail call optimization failed. The MI level option was nixed
  ///      immediately since it would require platform-specific pattern
  ///      matching. The SelectionDAG level option was nixed because
  ///      SelectionDAG only processes one IR level basic block at a time
  ///      implying one could not create a DAG Combine to move the callinst.
  ///
  /// To get around this problem a few things were realized:
  ///
  ///   1. While one can not handle multiple IR level basic blocks at the
  ///      SelectionDAG Level, one can generate multiple machine basic blocks
  ///      for one IR level basic block. This is how we handle bit tests and
  ///      switches.
  ///
  ///   2. At the MI level, tail calls are represented via a special return
  ///      MIInst called "tcreturn". Thus if we know the basic block in which we
  ///      wish to insert the stack protector check, we get the correct behavior
  ///      by always inserting the stack protector check right before the return
  ///      statement. This is a "magical transformation" since no matter where
  ///      the stack protector check intrinsic is, we always insert the stack
  ///      protector check code at the end of the BB.
  ///
  /// Given the aforementioned constraints, the following solution was devised:
  ///
  ///   1. On platforms that do not support SelectionDAG stack protector check
  ///      generation, allow for the normal IR level stack protector check
  ///      generation to continue.
  ///
  ///   2. On platforms that do support SelectionDAG stack protector check
  ///      generation:
  ///
  ///     a. Use the IR level stack protector pass to decide if a stack
  ///        protector is required/which BB we insert the stack protector check
  ///        in by reusing the logic already therein. If we wish to generate a
  ///        stack protector check in a basic block, we place a special IR
  ///        intrinsic called llvm.stackprotectorcheck right before the BB's
  ///        returninst or if there is a callinst that could potentially be
  ///        sibling call optimized, before the call inst.
  ///
  ///     b. Then when a BB with said intrinsic is processed, we codegen the BB
  ///        normally via SelectBasicBlock. In said process, when we visit the
  ///        stack protector check, we do not actually emit anything into the
  ///        BB. Instead, we just initialize the stack protector descriptor
  ///        class (which involves stashing information/creating the success
  ///        mbbb and the failure mbb if we have not created one for this
  ///        function yet) and export the guard variable that we are going to
  ///        compare.
  ///
  ///     c. After we finish selecting the basic block, in FinishBasicBlock if
  ///        the StackProtectorDescriptor attached to the SelectionDAGBuilder is
  ///        initialized, we produce the validation code with one of these
  ///        techniques:
  ///          1) with a call to a guard check function
  ///          2) with inlined instrumentation
  ///
  ///        1) We insert a call to the check function before the terminator.
  ///
  ///        2) We first find a splice point in the parent basic block
  ///        before the terminator and then splice the terminator of said basic
  ///        block into the success basic block. Then we code-gen a new tail for
  ///        the parent basic block consisting of the two loads, the comparison,
  ///        and finally two branches to the success/failure basic blocks. We
  ///        conclude by code-gening the failure basic block if we have not
  ///        code-gened it already (all stack protector checks we generate in
  ///        the same function, use the same failure basic block).
  class StackProtectorDescriptor {
  public:
    StackProtectorDescriptor() = default;

    /// Returns true if all fields of the stack protector descriptor are
    /// initialized implying that we should/are ready to emit a stack protector.
    bool shouldEmitStackProtector() const {
      return ParentMBB && SuccessMBB && FailureMBB;
    }

    bool shouldEmitFunctionBasedCheckStackProtector() const {
      return ParentMBB && !SuccessMBB && !FailureMBB;
    }

    /// Initialize the stack protector descriptor structure for a new basic
    /// block.
    void initialize(const BasicBlock *BB, MachineBasicBlock *MBB,
                    bool FunctionBasedInstrumentation) {
      // Make sure we are not initialized yet.
      assert(!shouldEmitStackProtector() && "Stack Protector Descriptor is "
             "already initialized!");
      ParentMBB = MBB;
      if (!FunctionBasedInstrumentation) {
        SuccessMBB = AddSuccessorMBB(BB, MBB, /* IsLikely */ true);
        FailureMBB = AddSuccessorMBB(BB, MBB, /* IsLikely */ false, FailureMBB);
      }
    }

    /// Reset state that changes when we handle different basic blocks.
    ///
    /// This currently includes:
    ///
    /// 1. The specific basic block we are generating a
    /// stack protector for (ParentMBB).
    ///
    /// 2. The successor machine basic block that will contain the tail of
    /// parent mbb after we create the stack protector check (SuccessMBB). This
    /// BB is visited only on stack protector check success.
    void resetPerBBState() {
      ParentMBB = nullptr;
      SuccessMBB = nullptr;
    }

    /// Reset state that only changes when we switch functions.
    ///
    /// This currently includes:
    ///
    /// 1. FailureMBB since we reuse the failure code path for all stack
    /// protector checks created in an individual function.
    ///
    /// 2.The guard variable since the guard variable we are checking against is
    /// always the same.
    void resetPerFunctionState() {
      FailureMBB = nullptr;
    }

    MachineBasicBlock *getParentMBB() { return ParentMBB; }
    MachineBasicBlock *getSuccessMBB() { return SuccessMBB; }
    MachineBasicBlock *getFailureMBB() { return FailureMBB; }

  private:
    /// The basic block for which we are generating the stack protector.
    ///
    /// As a result of stack protector generation, we will splice the
    /// terminators of this basic block into the successor mbb SuccessMBB and
    /// replace it with a compare/branch to the successor mbbs
    /// SuccessMBB/FailureMBB depending on whether or not the stack protector
    /// was violated.
    MachineBasicBlock *ParentMBB = nullptr;

    /// A basic block visited on stack protector check success that contains the
    /// terminators of ParentMBB.
    MachineBasicBlock *SuccessMBB = nullptr;

    /// This basic block visited on stack protector check failure that will
    /// contain a call to __stack_chk_fail().
    MachineBasicBlock *FailureMBB = nullptr;

    /// Add a successor machine basic block to ParentMBB. If the successor mbb
    /// has not been created yet (i.e. if SuccMBB = 0), then the machine basic
    /// block will be created. Assign a large weight if IsLikely is true.
    MachineBasicBlock *AddSuccessorMBB(const BasicBlock *BB,
                                       MachineBasicBlock *ParentMBB,
                                       bool IsLikely,
                                       MachineBasicBlock *SuccMBB = nullptr);
  };

private:
  const TargetMachine &TM;

public:
  /// Lowest valid SDNodeOrder. The special case 0 is reserved for scheduling
  /// nodes without a corresponding SDNode.
  static const unsigned LowestSDNodeOrder = 1;

  SelectionDAG &DAG;
  const DataLayout *DL = nullptr;
  AliasAnalysis *AA = nullptr;
  const TargetLibraryInfo *LibInfo;

  /// SwitchCases - Vector of CaseBlock structures used to communicate
  /// SwitchInst code generation information.
  std::vector<CaseBlock> SwitchCases;

  /// JTCases - Vector of JumpTable structures used to communicate
  /// SwitchInst code generation information.
  std::vector<JumpTableBlock> JTCases;

  /// BitTestCases - Vector of BitTestBlock structures used to communicate
  /// SwitchInst code generation information.
  std::vector<BitTestBlock> BitTestCases;

  /// A StackProtectorDescriptor structure used to communicate stack protector
  /// information in between SelectBasicBlock and FinishBasicBlock.
  StackProtectorDescriptor SPDescriptor;

  // Emit PHI-node-operand constants only once even if used by multiple
  // PHI nodes.
  DenseMap<const Constant *, unsigned> ConstantsOut;

  /// FuncInfo - Information about the function as a whole.
  ///
  FunctionLoweringInfo &FuncInfo;

  /// GFI - Garbage collection metadata for the function.
  GCFunctionInfo *GFI;

  /// LPadToCallSiteMap - Map a landing pad to the call site indexes.
  DenseMap<MachineBasicBlock *, SmallVector<unsigned, 4>> LPadToCallSiteMap;

  /// HasTailCall - This is set to true if a call in the current
  /// block has been translated as a tail call. In this case,
  /// no subsequent DAG nodes should be created.
  bool HasTailCall = false;

  LLVMContext *Context;

  SelectionDAGBuilder(SelectionDAG &dag, FunctionLoweringInfo &funcinfo,
                      CodeGenOpt::Level ol)
    : SDNodeOrder(LowestSDNodeOrder), TM(dag.getTarget()), DAG(dag),
      FuncInfo(funcinfo) {}

  void init(GCFunctionInfo *gfi, AliasAnalysis *AA,
            const TargetLibraryInfo *li);

  /// Clear out the current SelectionDAG and the associated state and prepare
  /// this SelectionDAGBuilder object to be used for a new block. This doesn't
  /// clear out information about additional blocks that are needed to complete
  /// switch lowering or PHI node updating; that information is cleared out as
  /// it is consumed.
  void clear();

  /// Clear the dangling debug information map. This function is separated from
  /// the clear so that debug information that is dangling in a basic block can
  /// be properly resolved in a different basic block. This allows the
  /// SelectionDAG to resolve dangling debug information attached to PHI nodes.
  void clearDanglingDebugInfo();

  /// Return the current virtual root of the Selection DAG, flushing any
  /// PendingLoad items. This must be done before emitting a store or any other
  /// node that may need to be ordered after any prior load instructions.
  SDValue getRoot();

  /// Similar to getRoot, but instead of flushing all the PendingLoad items,
  /// flush all the PendingExports items. It is necessary to do this before
  /// emitting a terminator instruction.
  SDValue getControlRoot();

  SDLoc getCurSDLoc() const {
    return SDLoc(CurInst, SDNodeOrder);
  }

  DebugLoc getCurDebugLoc() const {
    return CurInst ? CurInst->getDebugLoc() : DebugLoc();
  }

  void CopyValueToVirtualRegister(const Value *V, unsigned Reg);

  void visit(const Instruction &I);

  void visit(unsigned Opcode, const User &I);

  /// getCopyFromRegs - If there was virtual register allocated for the value V
  /// emit CopyFromReg of the specified type Ty. Return empty SDValue() otherwise.
  SDValue getCopyFromRegs(const Value *V, Type *Ty);

  /// If we have dangling debug info that describes \p Variable, or an
  /// overlapping part of variable considering the \p Expr, then this method
  /// weill drop that debug info as it isn't valid any longer.
  void dropDanglingDebugInfo(const DILocalVariable *Variable,
                             const DIExpression *Expr);

  // resolveDanglingDebugInfo - if we saw an earlier dbg_value referring to V,
  // generate the debug data structures now that we've seen its definition.
  void resolveDanglingDebugInfo(const Value *V, SDValue Val);

  SDValue getValue(const Value *V);
  bool findValue(const Value *V) const;

  /// Return the SDNode for the specified IR value if it exists.
  SDNode *getNodeForIRValue(const Value *V) {
    if (NodeMap.find(V) == NodeMap.end())
      return nullptr;
    return NodeMap[V].getNode();
  }

  SDValue getNonRegisterValue(const Value *V);
  SDValue getValueImpl(const Value *V);

  void setValue(const Value *V, SDValue NewN) {
    SDValue &N = NodeMap[V];
    assert(!N.getNode() && "Already set a value for this node!");
    N = NewN;
  }

  void setUnusedArgValue(const Value *V, SDValue NewN) {
    SDValue &N = UnusedArgNodeMap[V];
    assert(!N.getNode() && "Already set a value for this node!");
    N = NewN;
  }

  void FindMergedConditions(const Value *Cond, MachineBasicBlock *TBB,
                            MachineBasicBlock *FBB, MachineBasicBlock *CurBB,
                            MachineBasicBlock *SwitchBB,
                            Instruction::BinaryOps Opc, BranchProbability TProb,
                            BranchProbability FProb, bool InvertCond);
  void EmitBranchForMergedCondition(const Value *Cond, MachineBasicBlock *TBB,
                                    MachineBasicBlock *FBB,
                                    MachineBasicBlock *CurBB,
                                    MachineBasicBlock *SwitchBB,
                                    BranchProbability TProb, BranchProbability FProb,
                                    bool InvertCond);
  bool ShouldEmitAsBranches(const std::vector<CaseBlock> &Cases);
  bool isExportableFromCurrentBlock(const Value *V, const BasicBlock *FromBB);
  void CopyToExportRegsIfNeeded(const Value *V);
  void ExportFromCurrentBlock(const Value *V);
  void LowerCallTo(ImmutableCallSite CS, SDValue Callee, bool IsTailCall,
                   const BasicBlock *EHPadBB = nullptr);

  // Lower range metadata from 0 to N to assert zext to an integer of nearest
  // floor power of two.
  SDValue lowerRangeToAssertZExt(SelectionDAG &DAG, const Instruction &I,
                                 SDValue Op);

  void populateCallLoweringInfo(TargetLowering::CallLoweringInfo &CLI,
                                ImmutableCallSite CS, unsigned ArgIdx,
                                unsigned NumArgs, SDValue Callee,
                                Type *ReturnTy, bool IsPatchPoint);

  std::pair<SDValue, SDValue>
  lowerInvokable(TargetLowering::CallLoweringInfo &CLI,
                 const BasicBlock *EHPadBB = nullptr);

  /// UpdateSplitBlock - When an MBB was split during scheduling, update the
  /// references that need to refer to the last resulting block.
  void UpdateSplitBlock(MachineBasicBlock *First, MachineBasicBlock *Last);

  /// Describes a gc.statepoint or a gc.statepoint like thing for the purposes
  /// of lowering into a STATEPOINT node.
  struct StatepointLoweringInfo {
    /// Bases[i] is the base pointer for Ptrs[i].  Together they denote the set
    /// of gc pointers this STATEPOINT has to relocate.
    SmallVector<const Value *, 16> Bases;
    SmallVector<const Value *, 16> Ptrs;

    /// The set of gc.relocate calls associated with this gc.statepoint.
    SmallVector<const GCRelocateInst *, 16> GCRelocates;

    /// The full list of gc arguments to the gc.statepoint being lowered.
    ArrayRef<const Use> GCArgs;

    /// The gc.statepoint instruction.
    const Instruction *StatepointInstr = nullptr;

    /// The list of gc transition arguments present in the gc.statepoint being
    /// lowered.
    ArrayRef<const Use> GCTransitionArgs;

    /// The ID that the resulting STATEPOINT instruction has to report.
    unsigned ID = -1;

    /// Information regarding the underlying call instruction.
    TargetLowering::CallLoweringInfo CLI;

    /// The deoptimization state associated with this gc.statepoint call, if
    /// any.
    ArrayRef<const Use> DeoptState;

    /// Flags associated with the meta arguments being lowered.
    uint64_t StatepointFlags = -1;

    /// The number of patchable bytes the call needs to get lowered into.
    unsigned NumPatchBytes = -1;

    /// The exception handling unwind destination, in case this represents an
    /// invoke of gc.statepoint.
    const BasicBlock *EHPadBB = nullptr;

    explicit StatepointLoweringInfo(SelectionDAG &DAG) : CLI(DAG) {}
  };

  /// Lower \p SLI into a STATEPOINT instruction.
  SDValue LowerAsSTATEPOINT(StatepointLoweringInfo &SI);

  // This function is responsible for the whole statepoint lowering process.
  // It uniformly handles invoke and call statepoints.
  void LowerStatepoint(ImmutableStatepoint ISP,
                       const BasicBlock *EHPadBB = nullptr);

  void LowerCallSiteWithDeoptBundle(ImmutableCallSite CS, SDValue Callee,
                                    const BasicBlock *EHPadBB);

  void LowerDeoptimizeCall(const CallInst *CI);
  void LowerDeoptimizingReturn();

  void LowerCallSiteWithDeoptBundleImpl(ImmutableCallSite CS, SDValue Callee,
                                        const BasicBlock *EHPadBB,
                                        bool VarArgDisallowed,
                                        bool ForceVoidReturnTy);

  /// Returns the type of FrameIndex and TargetFrameIndex nodes.
  MVT getFrameIndexTy() {
    return DAG.getTargetLoweringInfo().getFrameIndexTy(DAG.getDataLayout());
  }

private:
  // Terminator instructions.
  void visitRet(const ReturnInst &I);
  void visitBr(const BranchInst &I);
  void visitSwitch(const SwitchInst &I);
  void visitIndirectBr(const IndirectBrInst &I);
  void visitUnreachable(const UnreachableInst &I);
  void visitCleanupRet(const CleanupReturnInst &I);
  void visitCatchSwitch(const CatchSwitchInst &I);
  void visitCatchRet(const CatchReturnInst &I);
  void visitCatchPad(const CatchPadInst &I);
  void visitCleanupPad(const CleanupPadInst &CPI);

  BranchProbability getEdgeProbability(const MachineBasicBlock *Src,
                                       const MachineBasicBlock *Dst) const;
  void addSuccessorWithProb(
      MachineBasicBlock *Src, MachineBasicBlock *Dst,
      BranchProbability Prob = BranchProbability::getUnknown());

public:
  void visitSwitchCase(CaseBlock &CB,
                       MachineBasicBlock *SwitchBB);
  void visitSPDescriptorParent(StackProtectorDescriptor &SPD,
                               MachineBasicBlock *ParentBB);
  void visitSPDescriptorFailure(StackProtectorDescriptor &SPD);
  void visitBitTestHeader(BitTestBlock &B, MachineBasicBlock *SwitchBB);
  void visitBitTestCase(BitTestBlock &BB,
                        MachineBasicBlock* NextMBB,
                        BranchProbability BranchProbToNext,
                        unsigned Reg,
                        BitTestCase &B,
                        MachineBasicBlock *SwitchBB);
  void visitJumpTable(JumpTable &JT);
  void visitJumpTableHeader(JumpTable &JT, JumpTableHeader &JTH,
                            MachineBasicBlock *SwitchBB);

private:
  // These all get lowered before this pass.
  void visitInvoke(const InvokeInst &I);
  void visitResume(const ResumeInst &I);

  void visitUnary(const User &I, unsigned Opcode);
  void visitFNeg(const User &I) { visitUnary(I, ISD::FNEG); }

  void visitBinary(const User &I, unsigned Opcode);
  void visitShift(const User &I, unsigned Opcode);
  void visitAdd(const User &I)  { visitBinary(I, ISD::ADD); }
  void visitFAdd(const User &I) { visitBinary(I, ISD::FADD); }
  void visitSub(const User &I)  { visitBinary(I, ISD::SUB); }
  void visitFSub(const User &I);
  void visitMul(const User &I)  { visitBinary(I, ISD::MUL); }
  void visitFMul(const User &I) { visitBinary(I, ISD::FMUL); }
  void visitURem(const User &I) { visitBinary(I, ISD::UREM); }
  void visitSRem(const User &I) { visitBinary(I, ISD::SREM); }
  void visitFRem(const User &I) { visitBinary(I, ISD::FREM); }
  void visitUDiv(const User &I) { visitBinary(I, ISD::UDIV); }
  void visitSDiv(const User &I);
  void visitFDiv(const User &I) { visitBinary(I, ISD::FDIV); }
  void visitAnd (const User &I) { visitBinary(I, ISD::AND); }
  void visitOr  (const User &I) { visitBinary(I, ISD::OR); }
  void visitXor (const User &I) { visitBinary(I, ISD::XOR); }
  void visitShl (const User &I) { visitShift(I, ISD::SHL); }
  void visitLShr(const User &I) { visitShift(I, ISD::SRL); }
  void visitAShr(const User &I) { visitShift(I, ISD::SRA); }
  void visitICmp(const User &I);
  void visitFCmp(const User &I);
  // Visit the conversion instructions
  void visitTrunc(const User &I);
  void visitZExt(const User &I);
  void visitSExt(const User &I);
  void visitFPTrunc(const User &I);
  void visitFPExt(const User &I);
  void visitFPToUI(const User &I);
  void visitFPToSI(const User &I);
  void visitUIToFP(const User &I);
  void visitSIToFP(const User &I);
  void visitPtrToInt(const User &I);
  void visitIntToPtr(const User &I);
  void visitBitCast(const User &I);
  void visitAddrSpaceCast(const User &I);

  void visitExtractElement(const User &I);
  void visitInsertElement(const User &I);
  void visitShuffleVector(const User &I);

  void visitExtractValue(const User &I);
  void visitInsertValue(const User &I);
  void visitLandingPad(const LandingPadInst &LP);

  void visitGetElementPtr(const User &I);
  void visitSelect(const User &I);

  void visitAlloca(const AllocaInst &I);
  void visitLoad(const LoadInst &I);
  void visitStore(const StoreInst &I);
  void visitMaskedLoad(const CallInst &I, bool IsExpanding = false);
  void visitMaskedStore(const CallInst &I, bool IsCompressing = false);
  void visitMaskedGather(const CallInst &I);
  void visitMaskedScatter(const CallInst &I);
  void visitAtomicCmpXchg(const AtomicCmpXchgInst &I);
  void visitAtomicRMW(const AtomicRMWInst &I);
  void visitFence(const FenceInst &I);
  void visitPHI(const PHINode &I);
  void visitCall(const CallInst &I);
  bool visitMemCmpCall(const CallInst &I);
  bool visitMemPCpyCall(const CallInst &I);
  bool visitMemChrCall(const CallInst &I);
  bool visitStrCpyCall(const CallInst &I, bool isStpcpy);
  bool visitStrCmpCall(const CallInst &I);
  bool visitStrLenCall(const CallInst &I);
  bool visitStrNLenCall(const CallInst &I);
  bool visitUnaryFloatCall(const CallInst &I, unsigned Opcode);
  bool visitBinaryFloatCall(const CallInst &I, unsigned Opcode);
  void visitAtomicLoad(const LoadInst &I);
  void visitAtomicStore(const StoreInst &I);
  void visitLoadFromSwiftError(const LoadInst &I);
  void visitStoreToSwiftError(const StoreInst &I);

  void visitInlineAsm(ImmutableCallSite CS);
  const char *visitIntrinsicCall(const CallInst &I, unsigned Intrinsic);
  void visitTargetIntrinsic(const CallInst &I, unsigned Intrinsic);
  void visitConstrainedFPIntrinsic(const ConstrainedFPIntrinsic &FPI);

  void visitVAStart(const CallInst &I);
  void visitVAArg(const VAArgInst &I);
  void visitVAEnd(const CallInst &I);
  void visitVACopy(const CallInst &I);
  void visitStackmap(const CallInst &I);
  void visitPatchpoint(ImmutableCallSite CS,
                       const BasicBlock *EHPadBB = nullptr);

  // These two are implemented in StatepointLowering.cpp
  void visitGCRelocate(const GCRelocateInst &Relocate);
  void visitGCResult(const GCResultInst &I);

  void visitVectorReduce(const CallInst &I, unsigned Intrinsic);

  void visitUserOp1(const Instruction &I) {
    llvm_unreachable("UserOp1 should not exist at instruction selection time!");
  }
  void visitUserOp2(const Instruction &I) {
    llvm_unreachable("UserOp2 should not exist at instruction selection time!");
  }

  void processIntegerCallValue(const Instruction &I,
                               SDValue Value, bool IsSigned);

  void HandlePHINodesInSuccessorBlocks(const BasicBlock *LLVMBB);

  void emitInlineAsmError(ImmutableCallSite CS, const Twine &Message);

  /// If V is an function argument then create corresponding DBG_VALUE machine
  /// instruction for it now. At the end of instruction selection, they will be
  /// inserted to the entry BB.
  bool EmitFuncArgumentDbgValue(const Value *V, DILocalVariable *Variable,
                                DIExpression *Expr, DILocation *DL,
                                bool IsDbgDeclare, const SDValue &N);

  /// Return the next block after MBB, or nullptr if there is none.
  MachineBasicBlock *NextBlock(MachineBasicBlock *MBB);

  /// Update the DAG and DAG builder with the relevant information after
  /// a new root node has been created which could be a tail call.
  void updateDAGForMaybeTailCall(SDValue MaybeTC);

  /// Return the appropriate SDDbgValue based on N.
  SDDbgValue *getDbgValue(SDValue N, DILocalVariable *Variable,
                          DIExpression *Expr, const DebugLoc &dl,
                          unsigned DbgSDNodeOrder);
};

/// RegsForValue - This struct represents the registers (physical or virtual)
/// that a particular set of values is assigned, and the type information about
/// the value. The most common situation is to represent one value at a time,
/// but struct or array values are handled element-wise as multiple values.  The
/// splitting of aggregates is performed recursively, so that we never have
/// aggregate-typed registers. The values at this point do not necessarily have
/// legal types, so each value may require one or more registers of some legal
/// type.
///
struct RegsForValue {
  /// The value types of the values, which may not be legal, and
  /// may need be promoted or synthesized from one or more registers.
  SmallVector<EVT, 4> ValueVTs;

  /// The value types of the registers. This is the same size as ValueVTs and it
  /// records, for each value, what the type of the assigned register or
  /// registers are. (Individual values are never synthesized from more than one
  /// type of register.)
  ///
  /// With virtual registers, the contents of RegVTs is redundant with TLI's
  /// getRegisterType member function, however when with physical registers
  /// it is necessary to have a separate record of the types.
  SmallVector<MVT, 4> RegVTs;

  /// This list holds the registers assigned to the values.
  /// Each legal or promoted value requires one register, and each
  /// expanded value requires multiple registers.
  SmallVector<unsigned, 4> Regs;

  /// This list holds the number of registers for each value.
  SmallVector<unsigned, 4> RegCount;

  /// Records if this value needs to be treated in an ABI dependant manner,
  /// different to normal type legalization.
  Optional<CallingConv::ID> CallConv;

  RegsForValue() = default;
  RegsForValue(const SmallVector<unsigned, 4> &regs, MVT regvt, EVT valuevt,
               Optional<CallingConv::ID> CC = None);
  RegsForValue(LLVMContext &Context, const TargetLowering &TLI,
               const DataLayout &DL, unsigned Reg, Type *Ty,
               Optional<CallingConv::ID> CC);

  bool isABIMangled() const {
    return CallConv.hasValue();
  }

  /// Add the specified values to this one.
  void append(const RegsForValue &RHS) {
    ValueVTs.append(RHS.ValueVTs.begin(), RHS.ValueVTs.end());
    RegVTs.append(RHS.RegVTs.begin(), RHS.RegVTs.end());
    Regs.append(RHS.Regs.begin(), RHS.Regs.end());
    RegCount.push_back(RHS.Regs.size());
  }

  /// Emit a series of CopyFromReg nodes that copies from this value and returns
  /// the result as a ValueVTs value. This uses Chain/Flag as the input and
  /// updates them for the output Chain/Flag. If the Flag pointer is NULL, no
  /// flag is used.
  SDValue getCopyFromRegs(SelectionDAG &DAG, FunctionLoweringInfo &FuncInfo,
                          const SDLoc &dl, SDValue &Chain, SDValue *Flag,
                          const Value *V = nullptr) const;

  /// Emit a series of CopyToReg nodes that copies the specified value into the
  /// registers specified by this object. This uses Chain/Flag as the input and
  /// updates them for the output Chain/Flag. If the Flag pointer is nullptr, no
  /// flag is used. If V is not nullptr, then it is used in printing better
  /// diagnostic messages on error.
  void getCopyToRegs(SDValue Val, SelectionDAG &DAG, const SDLoc &dl,
                     SDValue &Chain, SDValue *Flag, const Value *V = nullptr,
                     ISD::NodeType PreferredExtendType = ISD::ANY_EXTEND) const;

  /// Add this value to the specified inlineasm node operand list. This adds the
  /// code marker, matching input operand index (if applicable), and includes
  /// the number of values added into it.
  void AddInlineAsmOperands(unsigned Code, bool HasMatching,
                            unsigned MatchingIdx, const SDLoc &dl,
                            SelectionDAG &DAG, std::vector<SDValue> &Ops) const;

  /// Check if the total RegCount is greater than one.
  bool occupiesMultipleRegs() const {
    return std::accumulate(RegCount.begin(), RegCount.end(), 0) > 1;
  }

  /// Return a list of registers and their sizes.
  SmallVector<std::pair<unsigned, unsigned>, 4> getRegsAndSizes() const;
};

} // end namespace llvm

#endif // LLVM_LIB_CODEGEN_SELECTIONDAG_SELECTIONDAGBUILDER_H
