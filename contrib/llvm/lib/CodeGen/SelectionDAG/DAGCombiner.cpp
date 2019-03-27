//===- DAGCombiner.cpp - Implement a DAG node combiner --------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This pass combines dag nodes to form fewer, simpler DAG nodes.  It can be run
// both before and after the DAG is legalized.
//
// This pass is not a substitute for the LLVM IR instcombine pass. This pass is
// primarily intended to handle simplification opportunities that are implicit
// in the LLVM IR and exposed by the various codegen lowering phases.
//
//===----------------------------------------------------------------------===//

#include "llvm/ADT/APFloat.h"
#include "llvm/ADT/APInt.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/IntervalMap.h"
#include "llvm/ADT/None.h"
#include "llvm/ADT/Optional.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SetVector.h"
#include "llvm/ADT/SmallBitVector.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/ADT/SmallSet.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/Analysis/AliasAnalysis.h"
#include "llvm/Analysis/MemoryLocation.h"
#include "llvm/CodeGen/DAGCombine.h"
#include "llvm/CodeGen/ISDOpcodes.h"
#include "llvm/CodeGen/MachineFrameInfo.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineMemOperand.h"
#include "llvm/CodeGen/RuntimeLibcalls.h"
#include "llvm/CodeGen/SelectionDAG.h"
#include "llvm/CodeGen/SelectionDAGAddressAnalysis.h"
#include "llvm/CodeGen/SelectionDAGNodes.h"
#include "llvm/CodeGen/SelectionDAGTargetInfo.h"
#include "llvm/CodeGen/TargetLowering.h"
#include "llvm/CodeGen/TargetRegisterInfo.h"
#include "llvm/CodeGen/TargetSubtargetInfo.h"
#include "llvm/CodeGen/ValueTypes.h"
#include "llvm/IR/Attributes.h"
#include "llvm/IR/Constant.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Metadata.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/CodeGen.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/KnownBits.h"
#include "llvm/Support/MachineValueType.h"
#include "llvm/Support/MathExtras.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Target/TargetMachine.h"
#include "llvm/Target/TargetOptions.h"
#include <algorithm>
#include <cassert>
#include <cstdint>
#include <functional>
#include <iterator>
#include <string>
#include <tuple>
#include <utility>

using namespace llvm;

#define DEBUG_TYPE "dagcombine"

STATISTIC(NodesCombined   , "Number of dag nodes combined");
STATISTIC(PreIndexedNodes , "Number of pre-indexed nodes created");
STATISTIC(PostIndexedNodes, "Number of post-indexed nodes created");
STATISTIC(OpsNarrowed     , "Number of load/op/store narrowed");
STATISTIC(LdStFP2Int      , "Number of fp load/store pairs transformed to int");
STATISTIC(SlicedLoads, "Number of load sliced");
STATISTIC(NumFPLogicOpsConv, "Number of logic ops converted to fp ops");

static cl::opt<bool>
CombinerGlobalAA("combiner-global-alias-analysis", cl::Hidden,
                 cl::desc("Enable DAG combiner's use of IR alias analysis"));

static cl::opt<bool>
UseTBAA("combiner-use-tbaa", cl::Hidden, cl::init(true),
        cl::desc("Enable DAG combiner's use of TBAA"));

#ifndef NDEBUG
static cl::opt<std::string>
CombinerAAOnlyFunc("combiner-aa-only-func", cl::Hidden,
                   cl::desc("Only use DAG-combiner alias analysis in this"
                            " function"));
#endif

/// Hidden option to stress test load slicing, i.e., when this option
/// is enabled, load slicing bypasses most of its profitability guards.
static cl::opt<bool>
StressLoadSlicing("combiner-stress-load-slicing", cl::Hidden,
                  cl::desc("Bypass the profitability model of load slicing"),
                  cl::init(false));

static cl::opt<bool>
  MaySplitLoadIndex("combiner-split-load-index", cl::Hidden, cl::init(true),
                    cl::desc("DAG combiner may split indexing from loads"));

namespace {

  class DAGCombiner {
    SelectionDAG &DAG;
    const TargetLowering &TLI;
    CombineLevel Level;
    CodeGenOpt::Level OptLevel;
    bool LegalOperations = false;
    bool LegalTypes = false;
    bool ForCodeSize;

    /// Worklist of all of the nodes that need to be simplified.
    ///
    /// This must behave as a stack -- new nodes to process are pushed onto the
    /// back and when processing we pop off of the back.
    ///
    /// The worklist will not contain duplicates but may contain null entries
    /// due to nodes being deleted from the underlying DAG.
    SmallVector<SDNode *, 64> Worklist;

    /// Mapping from an SDNode to its position on the worklist.
    ///
    /// This is used to find and remove nodes from the worklist (by nulling
    /// them) when they are deleted from the underlying DAG. It relies on
    /// stable indices of nodes within the worklist.
    DenseMap<SDNode *, unsigned> WorklistMap;

    /// Set of nodes which have been combined (at least once).
    ///
    /// This is used to allow us to reliably add any operands of a DAG node
    /// which have not yet been combined to the worklist.
    SmallPtrSet<SDNode *, 32> CombinedNodes;

    // AA - Used for DAG load/store alias analysis.
    AliasAnalysis *AA;

    /// When an instruction is simplified, add all users of the instruction to
    /// the work lists because they might get more simplified now.
    void AddUsersToWorklist(SDNode *N) {
      for (SDNode *Node : N->uses())
        AddToWorklist(Node);
    }

    /// Call the node-specific routine that folds each particular type of node.
    SDValue visit(SDNode *N);

  public:
    DAGCombiner(SelectionDAG &D, AliasAnalysis *AA, CodeGenOpt::Level OL)
        : DAG(D), TLI(D.getTargetLoweringInfo()), Level(BeforeLegalizeTypes),
          OptLevel(OL), AA(AA) {
      ForCodeSize = DAG.getMachineFunction().getFunction().optForSize();

      MaximumLegalStoreInBits = 0;
      for (MVT VT : MVT::all_valuetypes())
        if (EVT(VT).isSimple() && VT != MVT::Other &&
            TLI.isTypeLegal(EVT(VT)) &&
            VT.getSizeInBits() >= MaximumLegalStoreInBits)
          MaximumLegalStoreInBits = VT.getSizeInBits();
    }

    /// Add to the worklist making sure its instance is at the back (next to be
    /// processed.)
    void AddToWorklist(SDNode *N) {
      assert(N->getOpcode() != ISD::DELETED_NODE &&
             "Deleted Node added to Worklist");

      // Skip handle nodes as they can't usefully be combined and confuse the
      // zero-use deletion strategy.
      if (N->getOpcode() == ISD::HANDLENODE)
        return;

      if (WorklistMap.insert(std::make_pair(N, Worklist.size())).second)
        Worklist.push_back(N);
    }

    /// Remove all instances of N from the worklist.
    void removeFromWorklist(SDNode *N) {
      CombinedNodes.erase(N);

      auto It = WorklistMap.find(N);
      if (It == WorklistMap.end())
        return; // Not in the worklist.

      // Null out the entry rather than erasing it to avoid a linear operation.
      Worklist[It->second] = nullptr;
      WorklistMap.erase(It);
    }

    void deleteAndRecombine(SDNode *N);
    bool recursivelyDeleteUnusedNodes(SDNode *N);

    /// Replaces all uses of the results of one DAG node with new values.
    SDValue CombineTo(SDNode *N, const SDValue *To, unsigned NumTo,
                      bool AddTo = true);

    /// Replaces all uses of the results of one DAG node with new values.
    SDValue CombineTo(SDNode *N, SDValue Res, bool AddTo = true) {
      return CombineTo(N, &Res, 1, AddTo);
    }

    /// Replaces all uses of the results of one DAG node with new values.
    SDValue CombineTo(SDNode *N, SDValue Res0, SDValue Res1,
                      bool AddTo = true) {
      SDValue To[] = { Res0, Res1 };
      return CombineTo(N, To, 2, AddTo);
    }

    void CommitTargetLoweringOpt(const TargetLowering::TargetLoweringOpt &TLO);

  private:
    unsigned MaximumLegalStoreInBits;

    /// Check the specified integer node value to see if it can be simplified or
    /// if things it uses can be simplified by bit propagation.
    /// If so, return true.
    bool SimplifyDemandedBits(SDValue Op) {
      unsigned BitWidth = Op.getScalarValueSizeInBits();
      APInt Demanded = APInt::getAllOnesValue(BitWidth);
      return SimplifyDemandedBits(Op, Demanded);
    }

    /// Check the specified vector node value to see if it can be simplified or
    /// if things it uses can be simplified as it only uses some of the
    /// elements. If so, return true.
    bool SimplifyDemandedVectorElts(SDValue Op) {
      unsigned NumElts = Op.getValueType().getVectorNumElements();
      APInt Demanded = APInt::getAllOnesValue(NumElts);
      return SimplifyDemandedVectorElts(Op, Demanded);
    }

    bool SimplifyDemandedBits(SDValue Op, const APInt &Demanded);
    bool SimplifyDemandedVectorElts(SDValue Op, const APInt &Demanded,
                                    bool AssumeSingleUse = false);

    bool CombineToPreIndexedLoadStore(SDNode *N);
    bool CombineToPostIndexedLoadStore(SDNode *N);
    SDValue SplitIndexingFromLoad(LoadSDNode *LD);
    bool SliceUpLoad(SDNode *N);

    // Scalars have size 0 to distinguish from singleton vectors.
    SDValue ForwardStoreValueToDirectLoad(LoadSDNode *LD);
    bool getTruncatedStoreValue(StoreSDNode *ST, SDValue &Val);
    bool extendLoadedValueToExtension(LoadSDNode *LD, SDValue &Val);

    /// Replace an ISD::EXTRACT_VECTOR_ELT of a load with a narrowed
    ///   load.
    ///
    /// \param EVE ISD::EXTRACT_VECTOR_ELT to be replaced.
    /// \param InVecVT type of the input vector to EVE with bitcasts resolved.
    /// \param EltNo index of the vector element to load.
    /// \param OriginalLoad load that EVE came from to be replaced.
    /// \returns EVE on success SDValue() on failure.
    SDValue scalarizeExtractedVectorLoad(SDNode *EVE, EVT InVecVT,
                                         SDValue EltNo,
                                         LoadSDNode *OriginalLoad);
    void ReplaceLoadWithPromotedLoad(SDNode *Load, SDNode *ExtLoad);
    SDValue PromoteOperand(SDValue Op, EVT PVT, bool &Replace);
    SDValue SExtPromoteOperand(SDValue Op, EVT PVT);
    SDValue ZExtPromoteOperand(SDValue Op, EVT PVT);
    SDValue PromoteIntBinOp(SDValue Op);
    SDValue PromoteIntShiftOp(SDValue Op);
    SDValue PromoteExtend(SDValue Op);
    bool PromoteLoad(SDValue Op);

    /// Call the node-specific routine that knows how to fold each
    /// particular type of node. If that doesn't do anything, try the
    /// target-specific DAG combines.
    SDValue combine(SDNode *N);

    // Visitation implementation - Implement dag node combining for different
    // node types.  The semantics are as follows:
    // Return Value:
    //   SDValue.getNode() == 0 - No change was made
    //   SDValue.getNode() == N - N was replaced, is dead and has been handled.
    //   otherwise              - N should be replaced by the returned Operand.
    //
    SDValue visitTokenFactor(SDNode *N);
    SDValue visitMERGE_VALUES(SDNode *N);
    SDValue visitADD(SDNode *N);
    SDValue visitADDLike(SDValue N0, SDValue N1, SDNode *LocReference);
    SDValue visitSUB(SDNode *N);
    SDValue visitADDSAT(SDNode *N);
    SDValue visitSUBSAT(SDNode *N);
    SDValue visitADDC(SDNode *N);
    SDValue visitUADDO(SDNode *N);
    SDValue visitUADDOLike(SDValue N0, SDValue N1, SDNode *N);
    SDValue visitSUBC(SDNode *N);
    SDValue visitUSUBO(SDNode *N);
    SDValue visitADDE(SDNode *N);
    SDValue visitADDCARRY(SDNode *N);
    SDValue visitADDCARRYLike(SDValue N0, SDValue N1, SDValue CarryIn, SDNode *N);
    SDValue visitSUBE(SDNode *N);
    SDValue visitSUBCARRY(SDNode *N);
    SDValue visitMUL(SDNode *N);
    SDValue useDivRem(SDNode *N);
    SDValue visitSDIV(SDNode *N);
    SDValue visitSDIVLike(SDValue N0, SDValue N1, SDNode *N);
    SDValue visitUDIV(SDNode *N);
    SDValue visitUDIVLike(SDValue N0, SDValue N1, SDNode *N);
    SDValue visitREM(SDNode *N);
    SDValue visitMULHU(SDNode *N);
    SDValue visitMULHS(SDNode *N);
    SDValue visitSMUL_LOHI(SDNode *N);
    SDValue visitUMUL_LOHI(SDNode *N);
    SDValue visitSMULO(SDNode *N);
    SDValue visitUMULO(SDNode *N);
    SDValue visitIMINMAX(SDNode *N);
    SDValue visitAND(SDNode *N);
    SDValue visitANDLike(SDValue N0, SDValue N1, SDNode *N);
    SDValue visitOR(SDNode *N);
    SDValue visitORLike(SDValue N0, SDValue N1, SDNode *N);
    SDValue visitXOR(SDNode *N);
    SDValue SimplifyVBinOp(SDNode *N);
    SDValue visitSHL(SDNode *N);
    SDValue visitSRA(SDNode *N);
    SDValue visitSRL(SDNode *N);
    SDValue visitFunnelShift(SDNode *N);
    SDValue visitRotate(SDNode *N);
    SDValue visitABS(SDNode *N);
    SDValue visitBSWAP(SDNode *N);
    SDValue visitBITREVERSE(SDNode *N);
    SDValue visitCTLZ(SDNode *N);
    SDValue visitCTLZ_ZERO_UNDEF(SDNode *N);
    SDValue visitCTTZ(SDNode *N);
    SDValue visitCTTZ_ZERO_UNDEF(SDNode *N);
    SDValue visitCTPOP(SDNode *N);
    SDValue visitSELECT(SDNode *N);
    SDValue visitVSELECT(SDNode *N);
    SDValue visitSELECT_CC(SDNode *N);
    SDValue visitSETCC(SDNode *N);
    SDValue visitSETCCCARRY(SDNode *N);
    SDValue visitSIGN_EXTEND(SDNode *N);
    SDValue visitZERO_EXTEND(SDNode *N);
    SDValue visitANY_EXTEND(SDNode *N);
    SDValue visitAssertExt(SDNode *N);
    SDValue visitSIGN_EXTEND_INREG(SDNode *N);
    SDValue visitSIGN_EXTEND_VECTOR_INREG(SDNode *N);
    SDValue visitZERO_EXTEND_VECTOR_INREG(SDNode *N);
    SDValue visitTRUNCATE(SDNode *N);
    SDValue visitBITCAST(SDNode *N);
    SDValue visitBUILD_PAIR(SDNode *N);
    SDValue visitFADD(SDNode *N);
    SDValue visitFSUB(SDNode *N);
    SDValue visitFMUL(SDNode *N);
    SDValue visitFMA(SDNode *N);
    SDValue visitFDIV(SDNode *N);
    SDValue visitFREM(SDNode *N);
    SDValue visitFSQRT(SDNode *N);
    SDValue visitFCOPYSIGN(SDNode *N);
    SDValue visitFPOW(SDNode *N);
    SDValue visitSINT_TO_FP(SDNode *N);
    SDValue visitUINT_TO_FP(SDNode *N);
    SDValue visitFP_TO_SINT(SDNode *N);
    SDValue visitFP_TO_UINT(SDNode *N);
    SDValue visitFP_ROUND(SDNode *N);
    SDValue visitFP_ROUND_INREG(SDNode *N);
    SDValue visitFP_EXTEND(SDNode *N);
    SDValue visitFNEG(SDNode *N);
    SDValue visitFABS(SDNode *N);
    SDValue visitFCEIL(SDNode *N);
    SDValue visitFTRUNC(SDNode *N);
    SDValue visitFFLOOR(SDNode *N);
    SDValue visitFMINNUM(SDNode *N);
    SDValue visitFMAXNUM(SDNode *N);
    SDValue visitFMINIMUM(SDNode *N);
    SDValue visitFMAXIMUM(SDNode *N);
    SDValue visitBRCOND(SDNode *N);
    SDValue visitBR_CC(SDNode *N);
    SDValue visitLOAD(SDNode *N);

    SDValue replaceStoreChain(StoreSDNode *ST, SDValue BetterChain);
    SDValue replaceStoreOfFPConstant(StoreSDNode *ST);

    SDValue visitSTORE(SDNode *N);
    SDValue visitINSERT_VECTOR_ELT(SDNode *N);
    SDValue visitEXTRACT_VECTOR_ELT(SDNode *N);
    SDValue visitBUILD_VECTOR(SDNode *N);
    SDValue visitCONCAT_VECTORS(SDNode *N);
    SDValue visitEXTRACT_SUBVECTOR(SDNode *N);
    SDValue visitVECTOR_SHUFFLE(SDNode *N);
    SDValue visitSCALAR_TO_VECTOR(SDNode *N);
    SDValue visitINSERT_SUBVECTOR(SDNode *N);
    SDValue visitMLOAD(SDNode *N);
    SDValue visitMSTORE(SDNode *N);
    SDValue visitMGATHER(SDNode *N);
    SDValue visitMSCATTER(SDNode *N);
    SDValue visitFP_TO_FP16(SDNode *N);
    SDValue visitFP16_TO_FP(SDNode *N);

    SDValue visitFADDForFMACombine(SDNode *N);
    SDValue visitFSUBForFMACombine(SDNode *N);
    SDValue visitFMULForFMADistributiveCombine(SDNode *N);

    SDValue XformToShuffleWithZero(SDNode *N);
    SDValue ReassociateOps(unsigned Opc, const SDLoc &DL, SDValue N0,
                           SDValue N1, SDNodeFlags Flags);

    SDValue visitShiftByConstant(SDNode *N, ConstantSDNode *Amt);

    SDValue foldSelectOfConstants(SDNode *N);
    SDValue foldVSelectOfConstants(SDNode *N);
    SDValue foldBinOpIntoSelect(SDNode *BO);
    bool SimplifySelectOps(SDNode *SELECT, SDValue LHS, SDValue RHS);
    SDValue hoistLogicOpWithSameOpcodeHands(SDNode *N);
    SDValue SimplifySelect(const SDLoc &DL, SDValue N0, SDValue N1, SDValue N2);
    SDValue SimplifySelectCC(const SDLoc &DL, SDValue N0, SDValue N1,
                             SDValue N2, SDValue N3, ISD::CondCode CC,
                             bool NotExtCompare = false);
    SDValue convertSelectOfFPConstantsToLoadOffset(
        const SDLoc &DL, SDValue N0, SDValue N1, SDValue N2, SDValue N3,
        ISD::CondCode CC);
    SDValue foldSelectCCToShiftAnd(const SDLoc &DL, SDValue N0, SDValue N1,
                                   SDValue N2, SDValue N3, ISD::CondCode CC);
    SDValue foldLogicOfSetCCs(bool IsAnd, SDValue N0, SDValue N1,
                              const SDLoc &DL);
    SDValue unfoldMaskedMerge(SDNode *N);
    SDValue unfoldExtremeBitClearingToShifts(SDNode *N);
    SDValue SimplifySetCC(EVT VT, SDValue N0, SDValue N1, ISD::CondCode Cond,
                          const SDLoc &DL, bool foldBooleans);
    SDValue rebuildSetCC(SDValue N);

    bool isSetCCEquivalent(SDValue N, SDValue &LHS, SDValue &RHS,
                           SDValue &CC) const;
    bool isOneUseSetCC(SDValue N) const;

    SDValue SimplifyNodeWithTwoResults(SDNode *N, unsigned LoOp,
                                         unsigned HiOp);
    SDValue CombineConsecutiveLoads(SDNode *N, EVT VT);
    SDValue CombineExtLoad(SDNode *N);
    SDValue CombineZExtLogicopShiftLoad(SDNode *N);
    SDValue combineRepeatedFPDivisors(SDNode *N);
    SDValue combineInsertEltToShuffle(SDNode *N, unsigned InsIndex);
    SDValue ConstantFoldBITCASTofBUILD_VECTOR(SDNode *, EVT);
    SDValue BuildSDIV(SDNode *N);
    SDValue BuildSDIVPow2(SDNode *N);
    SDValue BuildUDIV(SDNode *N);
    SDValue BuildLogBase2(SDValue V, const SDLoc &DL);
    SDValue BuildReciprocalEstimate(SDValue Op, SDNodeFlags Flags);
    SDValue buildRsqrtEstimate(SDValue Op, SDNodeFlags Flags);
    SDValue buildSqrtEstimate(SDValue Op, SDNodeFlags Flags);
    SDValue buildSqrtEstimateImpl(SDValue Op, SDNodeFlags Flags, bool Recip);
    SDValue buildSqrtNROneConst(SDValue Arg, SDValue Est, unsigned Iterations,
                                SDNodeFlags Flags, bool Reciprocal);
    SDValue buildSqrtNRTwoConst(SDValue Arg, SDValue Est, unsigned Iterations,
                                SDNodeFlags Flags, bool Reciprocal);
    SDValue MatchBSwapHWordLow(SDNode *N, SDValue N0, SDValue N1,
                               bool DemandHighBits = true);
    SDValue MatchBSwapHWord(SDNode *N, SDValue N0, SDValue N1);
    SDNode *MatchRotatePosNeg(SDValue Shifted, SDValue Pos, SDValue Neg,
                              SDValue InnerPos, SDValue InnerNeg,
                              unsigned PosOpcode, unsigned NegOpcode,
                              const SDLoc &DL);
    SDNode *MatchRotate(SDValue LHS, SDValue RHS, const SDLoc &DL);
    SDValue MatchLoadCombine(SDNode *N);
    SDValue ReduceLoadWidth(SDNode *N);
    SDValue ReduceLoadOpStoreWidth(SDNode *N);
    SDValue splitMergedValStore(StoreSDNode *ST);
    SDValue TransformFPLoadStorePair(SDNode *N);
    SDValue convertBuildVecZextToZext(SDNode *N);
    SDValue reduceBuildVecExtToExtBuildVec(SDNode *N);
    SDValue reduceBuildVecToShuffle(SDNode *N);
    SDValue createBuildVecShuffle(const SDLoc &DL, SDNode *N,
                                  ArrayRef<int> VectorMask, SDValue VecIn1,
                                  SDValue VecIn2, unsigned LeftIdx);
    SDValue matchVSelectOpSizesWithSetCC(SDNode *Cast);

    /// Walk up chain skipping non-aliasing memory nodes,
    /// looking for aliasing nodes and adding them to the Aliases vector.
    void GatherAllAliases(SDNode *N, SDValue OriginalChain,
                          SmallVectorImpl<SDValue> &Aliases);

    /// Return true if there is any possibility that the two addresses overlap.
    bool isAlias(LSBaseSDNode *Op0, LSBaseSDNode *Op1) const;

    /// Walk up chain skipping non-aliasing memory nodes, looking for a better
    /// chain (aliasing node.)
    SDValue FindBetterChain(SDNode *N, SDValue Chain);

    /// Try to replace a store and any possibly adjacent stores on
    /// consecutive chains with better chains. Return true only if St is
    /// replaced.
    ///
    /// Notice that other chains may still be replaced even if the function
    /// returns false.
    bool findBetterNeighborChains(StoreSDNode *St);

    // Helper for findBetterNeighborChains. Walk up store chain add additional
    // chained stores that do not overlap and can be parallelized.
    bool parallelizeChainedStores(StoreSDNode *St);

    /// Holds a pointer to an LSBaseSDNode as well as information on where it
    /// is located in a sequence of memory operations connected by a chain.
    struct MemOpLink {
      // Ptr to the mem node.
      LSBaseSDNode *MemNode;

      // Offset from the base ptr.
      int64_t OffsetFromBase;

      MemOpLink(LSBaseSDNode *N, int64_t Offset)
          : MemNode(N), OffsetFromBase(Offset) {}
    };

    /// This is a helper function for visitMUL to check the profitability
    /// of folding (mul (add x, c1), c2) -> (add (mul x, c2), c1*c2).
    /// MulNode is the original multiply, AddNode is (add x, c1),
    /// and ConstNode is c2.
    bool isMulAddWithConstProfitable(SDNode *MulNode,
                                     SDValue &AddNode,
                                     SDValue &ConstNode);

    /// This is a helper function for visitAND and visitZERO_EXTEND.  Returns
    /// true if the (and (load x) c) pattern matches an extload.  ExtVT returns
    /// the type of the loaded value to be extended.
    bool isAndLoadExtLoad(ConstantSDNode *AndC, LoadSDNode *LoadN,
                          EVT LoadResultTy, EVT &ExtVT);

    /// Helper function to calculate whether the given Load/Store can have its
    /// width reduced to ExtVT.
    bool isLegalNarrowLdSt(LSBaseSDNode *LDSTN, ISD::LoadExtType ExtType,
                           EVT &MemVT, unsigned ShAmt = 0);

    /// Used by BackwardsPropagateMask to find suitable loads.
    bool SearchForAndLoads(SDNode *N, SmallVectorImpl<LoadSDNode*> &Loads,
                           SmallPtrSetImpl<SDNode*> &NodesWithConsts,
                           ConstantSDNode *Mask, SDNode *&NodeToMask);
    /// Attempt to propagate a given AND node back to load leaves so that they
    /// can be combined into narrow loads.
    bool BackwardsPropagateMask(SDNode *N, SelectionDAG &DAG);

    /// Helper function for MergeConsecutiveStores which merges the
    /// component store chains.
    SDValue getMergeStoreChains(SmallVectorImpl<MemOpLink> &StoreNodes,
                                unsigned NumStores);

    /// This is a helper function for MergeConsecutiveStores. When the
    /// source elements of the consecutive stores are all constants or
    /// all extracted vector elements, try to merge them into one
    /// larger store introducing bitcasts if necessary.  \return True
    /// if a merged store was created.
    bool MergeStoresOfConstantsOrVecElts(SmallVectorImpl<MemOpLink> &StoreNodes,
                                         EVT MemVT, unsigned NumStores,
                                         bool IsConstantSrc, bool UseVector,
                                         bool UseTrunc);

    /// This is a helper function for MergeConsecutiveStores. Stores
    /// that potentially may be merged with St are placed in
    /// StoreNodes. RootNode is a chain predecessor to all store
    /// candidates.
    void getStoreMergeCandidates(StoreSDNode *St,
                                 SmallVectorImpl<MemOpLink> &StoreNodes,
                                 SDNode *&Root);

    /// Helper function for MergeConsecutiveStores. Checks if
    /// candidate stores have indirect dependency through their
    /// operands. RootNode is the predecessor to all stores calculated
    /// by getStoreMergeCandidates and is used to prune the dependency check.
    /// \return True if safe to merge.
    bool checkMergeStoreCandidatesForDependencies(
        SmallVectorImpl<MemOpLink> &StoreNodes, unsigned NumStores,
        SDNode *RootNode);

    /// Merge consecutive store operations into a wide store.
    /// This optimization uses wide integers or vectors when possible.
    /// \return number of stores that were merged into a merged store (the
    /// affected nodes are stored as a prefix in \p StoreNodes).
    bool MergeConsecutiveStores(StoreSDNode *St);

    /// Try to transform a truncation where C is a constant:
    ///     (trunc (and X, C)) -> (and (trunc X), (trunc C))
    ///
    /// \p N needs to be a truncation and its first operand an AND. Other
    /// requirements are checked by the function (e.g. that trunc is
    /// single-use) and if missed an empty SDValue is returned.
    SDValue distributeTruncateThroughAnd(SDNode *N);

    /// Helper function to determine whether the target supports operation
    /// given by \p Opcode for type \p VT, that is, whether the operation
    /// is legal or custom before legalizing operations, and whether is
    /// legal (but not custom) after legalization.
    bool hasOperation(unsigned Opcode, EVT VT) {
      if (LegalOperations)
        return TLI.isOperationLegal(Opcode, VT);
      return TLI.isOperationLegalOrCustom(Opcode, VT);
    }

  public:
    /// Runs the dag combiner on all nodes in the work list
    void Run(CombineLevel AtLevel);

    SelectionDAG &getDAG() const { return DAG; }

    /// Returns a type large enough to hold any valid shift amount - before type
    /// legalization these can be huge.
    EVT getShiftAmountTy(EVT LHSTy) {
      assert(LHSTy.isInteger() && "Shift amount is not an integer type!");
      return TLI.getShiftAmountTy(LHSTy, DAG.getDataLayout(), LegalTypes);
    }

    /// This method returns true if we are running before type legalization or
    /// if the specified VT is legal.
    bool isTypeLegal(const EVT &VT) {
      if (!LegalTypes) return true;
      return TLI.isTypeLegal(VT);
    }

    /// Convenience wrapper around TargetLowering::getSetCCResultType
    EVT getSetCCResultType(EVT VT) const {
      return TLI.getSetCCResultType(DAG.getDataLayout(), *DAG.getContext(), VT);
    }

    void ExtendSetCCUses(const SmallVectorImpl<SDNode *> &SetCCs,
                         SDValue OrigLoad, SDValue ExtLoad,
                         ISD::NodeType ExtType);
  };

/// This class is a DAGUpdateListener that removes any deleted
/// nodes from the worklist.
class WorklistRemover : public SelectionDAG::DAGUpdateListener {
  DAGCombiner &DC;

public:
  explicit WorklistRemover(DAGCombiner &dc)
    : SelectionDAG::DAGUpdateListener(dc.getDAG()), DC(dc) {}

  void NodeDeleted(SDNode *N, SDNode *E) override {
    DC.removeFromWorklist(N);
  }
};

} // end anonymous namespace

//===----------------------------------------------------------------------===//
//  TargetLowering::DAGCombinerInfo implementation
//===----------------------------------------------------------------------===//

void TargetLowering::DAGCombinerInfo::AddToWorklist(SDNode *N) {
  ((DAGCombiner*)DC)->AddToWorklist(N);
}

SDValue TargetLowering::DAGCombinerInfo::
CombineTo(SDNode *N, ArrayRef<SDValue> To, bool AddTo) {
  return ((DAGCombiner*)DC)->CombineTo(N, &To[0], To.size(), AddTo);
}

SDValue TargetLowering::DAGCombinerInfo::
CombineTo(SDNode *N, SDValue Res, bool AddTo) {
  return ((DAGCombiner*)DC)->CombineTo(N, Res, AddTo);
}

SDValue TargetLowering::DAGCombinerInfo::
CombineTo(SDNode *N, SDValue Res0, SDValue Res1, bool AddTo) {
  return ((DAGCombiner*)DC)->CombineTo(N, Res0, Res1, AddTo);
}

void TargetLowering::DAGCombinerInfo::
CommitTargetLoweringOpt(const TargetLowering::TargetLoweringOpt &TLO) {
  return ((DAGCombiner*)DC)->CommitTargetLoweringOpt(TLO);
}

//===----------------------------------------------------------------------===//
// Helper Functions
//===----------------------------------------------------------------------===//

void DAGCombiner::deleteAndRecombine(SDNode *N) {
  removeFromWorklist(N);

  // If the operands of this node are only used by the node, they will now be
  // dead. Make sure to re-visit them and recursively delete dead nodes.
  for (const SDValue &Op : N->ops())
    // For an operand generating multiple values, one of the values may
    // become dead allowing further simplification (e.g. split index
    // arithmetic from an indexed load).
    if (Op->hasOneUse() || Op->getNumValues() > 1)
      AddToWorklist(Op.getNode());

  DAG.DeleteNode(N);
}

/// Return 1 if we can compute the negated form of the specified expression for
/// the same cost as the expression itself, or 2 if we can compute the negated
/// form more cheaply than the expression itself.
static char isNegatibleForFree(SDValue Op, bool LegalOperations,
                               const TargetLowering &TLI,
                               const TargetOptions *Options,
                               unsigned Depth = 0) {
  // fneg is removable even if it has multiple uses.
  if (Op.getOpcode() == ISD::FNEG) return 2;

  // Don't allow anything with multiple uses unless we know it is free.
  EVT VT = Op.getValueType();
  const SDNodeFlags Flags = Op->getFlags();
  if (!Op.hasOneUse())
    if (!(Op.getOpcode() == ISD::FP_EXTEND &&
          TLI.isFPExtFree(VT, Op.getOperand(0).getValueType())))
      return 0;

  // Don't recurse exponentially.
  if (Depth > 6) return 0;

  switch (Op.getOpcode()) {
  default: return false;
  case ISD::ConstantFP: {
    if (!LegalOperations)
      return 1;

    // Don't invert constant FP values after legalization unless the target says
    // the negated constant is legal.
    return TLI.isOperationLegal(ISD::ConstantFP, VT) ||
      TLI.isFPImmLegal(neg(cast<ConstantFPSDNode>(Op)->getValueAPF()), VT);
  }
  case ISD::FADD:
    if (!Options->UnsafeFPMath && !Flags.hasNoSignedZeros())
      return 0;

    // After operation legalization, it might not be legal to create new FSUBs.
    if (LegalOperations && !TLI.isOperationLegalOrCustom(ISD::FSUB, VT))
      return 0;

    // fold (fneg (fadd A, B)) -> (fsub (fneg A), B)
    if (char V = isNegatibleForFree(Op.getOperand(0), LegalOperations, TLI,
                                    Options, Depth + 1))
      return V;
    // fold (fneg (fadd A, B)) -> (fsub (fneg B), A)
    return isNegatibleForFree(Op.getOperand(1), LegalOperations, TLI, Options,
                              Depth + 1);
  case ISD::FSUB:
    // We can't turn -(A-B) into B-A when we honor signed zeros.
    if (!Options->NoSignedZerosFPMath &&
        !Flags.hasNoSignedZeros())
      return 0;

    // fold (fneg (fsub A, B)) -> (fsub B, A)
    return 1;

  case ISD::FMUL:
  case ISD::FDIV:
    // fold (fneg (fmul X, Y)) -> (fmul (fneg X), Y) or (fmul X, (fneg Y))
    if (char V = isNegatibleForFree(Op.getOperand(0), LegalOperations, TLI,
                                    Options, Depth + 1))
      return V;

    return isNegatibleForFree(Op.getOperand(1), LegalOperations, TLI, Options,
                              Depth + 1);

  case ISD::FP_EXTEND:
  case ISD::FP_ROUND:
  case ISD::FSIN:
    return isNegatibleForFree(Op.getOperand(0), LegalOperations, TLI, Options,
                              Depth + 1);
  }
}

/// If isNegatibleForFree returns true, return the newly negated expression.
static SDValue GetNegatedExpression(SDValue Op, SelectionDAG &DAG,
                                    bool LegalOperations, unsigned Depth = 0) {
  const TargetOptions &Options = DAG.getTarget().Options;
  // fneg is removable even if it has multiple uses.
  if (Op.getOpcode() == ISD::FNEG) return Op.getOperand(0);

  assert(Depth <= 6 && "GetNegatedExpression doesn't match isNegatibleForFree");

  const SDNodeFlags Flags = Op.getNode()->getFlags();

  switch (Op.getOpcode()) {
  default: llvm_unreachable("Unknown code");
  case ISD::ConstantFP: {
    APFloat V = cast<ConstantFPSDNode>(Op)->getValueAPF();
    V.changeSign();
    return DAG.getConstantFP(V, SDLoc(Op), Op.getValueType());
  }
  case ISD::FADD:
    assert(Options.UnsafeFPMath || Flags.hasNoSignedZeros());

    // fold (fneg (fadd A, B)) -> (fsub (fneg A), B)
    if (isNegatibleForFree(Op.getOperand(0), LegalOperations,
                           DAG.getTargetLoweringInfo(), &Options, Depth+1))
      return DAG.getNode(ISD::FSUB, SDLoc(Op), Op.getValueType(),
                         GetNegatedExpression(Op.getOperand(0), DAG,
                                              LegalOperations, Depth+1),
                         Op.getOperand(1), Flags);
    // fold (fneg (fadd A, B)) -> (fsub (fneg B), A)
    return DAG.getNode(ISD::FSUB, SDLoc(Op), Op.getValueType(),
                       GetNegatedExpression(Op.getOperand(1), DAG,
                                            LegalOperations, Depth+1),
                       Op.getOperand(0), Flags);
  case ISD::FSUB:
    // fold (fneg (fsub 0, B)) -> B
    if (ConstantFPSDNode *N0CFP = dyn_cast<ConstantFPSDNode>(Op.getOperand(0)))
      if (N0CFP->isZero())
        return Op.getOperand(1);

    // fold (fneg (fsub A, B)) -> (fsub B, A)
    return DAG.getNode(ISD::FSUB, SDLoc(Op), Op.getValueType(),
                       Op.getOperand(1), Op.getOperand(0), Flags);

  case ISD::FMUL:
  case ISD::FDIV:
    // fold (fneg (fmul X, Y)) -> (fmul (fneg X), Y)
    if (isNegatibleForFree(Op.getOperand(0), LegalOperations,
                           DAG.getTargetLoweringInfo(), &Options, Depth+1))
      return DAG.getNode(Op.getOpcode(), SDLoc(Op), Op.getValueType(),
                         GetNegatedExpression(Op.getOperand(0), DAG,
                                              LegalOperations, Depth+1),
                         Op.getOperand(1), Flags);

    // fold (fneg (fmul X, Y)) -> (fmul X, (fneg Y))
    return DAG.getNode(Op.getOpcode(), SDLoc(Op), Op.getValueType(),
                       Op.getOperand(0),
                       GetNegatedExpression(Op.getOperand(1), DAG,
                                            LegalOperations, Depth+1), Flags);

  case ISD::FP_EXTEND:
  case ISD::FSIN:
    return DAG.getNode(Op.getOpcode(), SDLoc(Op), Op.getValueType(),
                       GetNegatedExpression(Op.getOperand(0), DAG,
                                            LegalOperations, Depth+1));
  case ISD::FP_ROUND:
      return DAG.getNode(ISD::FP_ROUND, SDLoc(Op), Op.getValueType(),
                         GetNegatedExpression(Op.getOperand(0), DAG,
                                              LegalOperations, Depth+1),
                         Op.getOperand(1));
  }
}

// APInts must be the same size for most operations, this helper
// function zero extends the shorter of the pair so that they match.
// We provide an Offset so that we can create bitwidths that won't overflow.
static void zeroExtendToMatch(APInt &LHS, APInt &RHS, unsigned Offset = 0) {
  unsigned Bits = Offset + std::max(LHS.getBitWidth(), RHS.getBitWidth());
  LHS = LHS.zextOrSelf(Bits);
  RHS = RHS.zextOrSelf(Bits);
}

// Return true if this node is a setcc, or is a select_cc
// that selects between the target values used for true and false, making it
// equivalent to a setcc. Also, set the incoming LHS, RHS, and CC references to
// the appropriate nodes based on the type of node we are checking. This
// simplifies life a bit for the callers.
bool DAGCombiner::isSetCCEquivalent(SDValue N, SDValue &LHS, SDValue &RHS,
                                    SDValue &CC) const {
  if (N.getOpcode() == ISD::SETCC) {
    LHS = N.getOperand(0);
    RHS = N.getOperand(1);
    CC  = N.getOperand(2);
    return true;
  }

  if (N.getOpcode() != ISD::SELECT_CC ||
      !TLI.isConstTrueVal(N.getOperand(2).getNode()) ||
      !TLI.isConstFalseVal(N.getOperand(3).getNode()))
    return false;

  if (TLI.getBooleanContents(N.getValueType()) ==
      TargetLowering::UndefinedBooleanContent)
    return false;

  LHS = N.getOperand(0);
  RHS = N.getOperand(1);
  CC  = N.getOperand(4);
  return true;
}

/// Return true if this is a SetCC-equivalent operation with only one use.
/// If this is true, it allows the users to invert the operation for free when
/// it is profitable to do so.
bool DAGCombiner::isOneUseSetCC(SDValue N) const {
  SDValue N0, N1, N2;
  if (isSetCCEquivalent(N, N0, N1, N2) && N.getNode()->hasOneUse())
    return true;
  return false;
}

// Returns the SDNode if it is a constant float BuildVector
// or constant float.
static SDNode *isConstantFPBuildVectorOrConstantFP(SDValue N) {
  if (isa<ConstantFPSDNode>(N))
    return N.getNode();
  if (ISD::isBuildVectorOfConstantFPSDNodes(N.getNode()))
    return N.getNode();
  return nullptr;
}

// Determines if it is a constant integer or a build vector of constant
// integers (and undefs).
// Do not permit build vector implicit truncation.
static bool isConstantOrConstantVector(SDValue N, bool NoOpaques = false) {
  if (ConstantSDNode *Const = dyn_cast<ConstantSDNode>(N))
    return !(Const->isOpaque() && NoOpaques);
  if (N.getOpcode() != ISD::BUILD_VECTOR)
    return false;
  unsigned BitWidth = N.getScalarValueSizeInBits();
  for (const SDValue &Op : N->op_values()) {
    if (Op.isUndef())
      continue;
    ConstantSDNode *Const = dyn_cast<ConstantSDNode>(Op);
    if (!Const || Const->getAPIntValue().getBitWidth() != BitWidth ||
        (Const->isOpaque() && NoOpaques))
      return false;
  }
  return true;
}

// Determines if a BUILD_VECTOR is composed of all-constants possibly mixed with
// undef's.
static bool isAnyConstantBuildVector(SDValue V, bool NoOpaques = false) {
  if (V.getOpcode() != ISD::BUILD_VECTOR)
    return false;
  return isConstantOrConstantVector(V, NoOpaques) ||
         ISD::isBuildVectorOfConstantFPSDNodes(V.getNode());
}

SDValue DAGCombiner::ReassociateOps(unsigned Opc, const SDLoc &DL, SDValue N0,
                                    SDValue N1, SDNodeFlags Flags) {
  // Don't reassociate reductions.
  if (Flags.hasVectorReduction())
    return SDValue();

  EVT VT = N0.getValueType();
  if (N0.getOpcode() == Opc && !N0->getFlags().hasVectorReduction()) {
    if (SDNode *L = DAG.isConstantIntBuildVectorOrConstantInt(N0.getOperand(1))) {
      if (SDNode *R = DAG.isConstantIntBuildVectorOrConstantInt(N1)) {
        // reassoc. (op (op x, c1), c2) -> (op x, (op c1, c2))
        if (SDValue OpNode = DAG.FoldConstantArithmetic(Opc, DL, VT, L, R))
          return DAG.getNode(Opc, DL, VT, N0.getOperand(0), OpNode);
        return SDValue();
      }
      if (N0.hasOneUse()) {
        // reassoc. (op (op x, c1), y) -> (op (op x, y), c1) iff x+c1 has one
        // use
        SDValue OpNode = DAG.getNode(Opc, SDLoc(N0), VT, N0.getOperand(0), N1);
        if (!OpNode.getNode())
          return SDValue();
        AddToWorklist(OpNode.getNode());
        return DAG.getNode(Opc, DL, VT, OpNode, N0.getOperand(1));
      }
    }
  }

  if (N1.getOpcode() == Opc && !N1->getFlags().hasVectorReduction()) {
    if (SDNode *R = DAG.isConstantIntBuildVectorOrConstantInt(N1.getOperand(1))) {
      if (SDNode *L = DAG.isConstantIntBuildVectorOrConstantInt(N0)) {
        // reassoc. (op c2, (op x, c1)) -> (op x, (op c1, c2))
        if (SDValue OpNode = DAG.FoldConstantArithmetic(Opc, DL, VT, R, L))
          return DAG.getNode(Opc, DL, VT, N1.getOperand(0), OpNode);
        return SDValue();
      }
      if (N1.hasOneUse()) {
        // reassoc. (op x, (op y, c1)) -> (op (op x, y), c1) iff x+c1 has one
        // use
        SDValue OpNode = DAG.getNode(Opc, SDLoc(N0), VT, N0, N1.getOperand(0));
        if (!OpNode.getNode())
          return SDValue();
        AddToWorklist(OpNode.getNode());
        return DAG.getNode(Opc, DL, VT, OpNode, N1.getOperand(1));
      }
    }
  }

  return SDValue();
}

SDValue DAGCombiner::CombineTo(SDNode *N, const SDValue *To, unsigned NumTo,
                               bool AddTo) {
  assert(N->getNumValues() == NumTo && "Broken CombineTo call!");
  ++NodesCombined;
  LLVM_DEBUG(dbgs() << "\nReplacing.1 "; N->dump(&DAG); dbgs() << "\nWith: ";
             To[0].getNode()->dump(&DAG);
             dbgs() << " and " << NumTo - 1 << " other values\n");
  for (unsigned i = 0, e = NumTo; i != e; ++i)
    assert((!To[i].getNode() ||
            N->getValueType(i) == To[i].getValueType()) &&
           "Cannot combine value to value of different type!");

  WorklistRemover DeadNodes(*this);
  DAG.ReplaceAllUsesWith(N, To);
  if (AddTo) {
    // Push the new nodes and any users onto the worklist
    for (unsigned i = 0, e = NumTo; i != e; ++i) {
      if (To[i].getNode()) {
        AddToWorklist(To[i].getNode());
        AddUsersToWorklist(To[i].getNode());
      }
    }
  }

  // Finally, if the node is now dead, remove it from the graph.  The node
  // may not be dead if the replacement process recursively simplified to
  // something else needing this node.
  if (N->use_empty())
    deleteAndRecombine(N);
  return SDValue(N, 0);
}

void DAGCombiner::
CommitTargetLoweringOpt(const TargetLowering::TargetLoweringOpt &TLO) {
  // Replace all uses.  If any nodes become isomorphic to other nodes and
  // are deleted, make sure to remove them from our worklist.
  WorklistRemover DeadNodes(*this);
  DAG.ReplaceAllUsesOfValueWith(TLO.Old, TLO.New);

  // Push the new node and any (possibly new) users onto the worklist.
  AddToWorklist(TLO.New.getNode());
  AddUsersToWorklist(TLO.New.getNode());

  // Finally, if the node is now dead, remove it from the graph.  The node
  // may not be dead if the replacement process recursively simplified to
  // something else needing this node.
  if (TLO.Old.getNode()->use_empty())
    deleteAndRecombine(TLO.Old.getNode());
}

/// Check the specified integer node value to see if it can be simplified or if
/// things it uses can be simplified by bit propagation. If so, return true.
bool DAGCombiner::SimplifyDemandedBits(SDValue Op, const APInt &Demanded) {
  TargetLowering::TargetLoweringOpt TLO(DAG, LegalTypes, LegalOperations);
  KnownBits Known;
  if (!TLI.SimplifyDemandedBits(Op, Demanded, Known, TLO))
    return false;

  // Revisit the node.
  AddToWorklist(Op.getNode());

  // Replace the old value with the new one.
  ++NodesCombined;
  LLVM_DEBUG(dbgs() << "\nReplacing.2 "; TLO.Old.getNode()->dump(&DAG);
             dbgs() << "\nWith: "; TLO.New.getNode()->dump(&DAG);
             dbgs() << '\n');

  CommitTargetLoweringOpt(TLO);
  return true;
}

/// Check the specified vector node value to see if it can be simplified or
/// if things it uses can be simplified as it only uses some of the elements.
/// If so, return true.
bool DAGCombiner::SimplifyDemandedVectorElts(SDValue Op, const APInt &Demanded,
                                             bool AssumeSingleUse) {
  TargetLowering::TargetLoweringOpt TLO(DAG, LegalTypes, LegalOperations);
  APInt KnownUndef, KnownZero;
  if (!TLI.SimplifyDemandedVectorElts(Op, Demanded, KnownUndef, KnownZero, TLO,
                                      0, AssumeSingleUse))
    return false;

  // Revisit the node.
  AddToWorklist(Op.getNode());

  // Replace the old value with the new one.
  ++NodesCombined;
  LLVM_DEBUG(dbgs() << "\nReplacing.2 "; TLO.Old.getNode()->dump(&DAG);
             dbgs() << "\nWith: "; TLO.New.getNode()->dump(&DAG);
             dbgs() << '\n');

  CommitTargetLoweringOpt(TLO);
  return true;
}

void DAGCombiner::ReplaceLoadWithPromotedLoad(SDNode *Load, SDNode *ExtLoad) {
  SDLoc DL(Load);
  EVT VT = Load->getValueType(0);
  SDValue Trunc = DAG.getNode(ISD::TRUNCATE, DL, VT, SDValue(ExtLoad, 0));

  LLVM_DEBUG(dbgs() << "\nReplacing.9 "; Load->dump(&DAG); dbgs() << "\nWith: ";
             Trunc.getNode()->dump(&DAG); dbgs() << '\n');
  WorklistRemover DeadNodes(*this);
  DAG.ReplaceAllUsesOfValueWith(SDValue(Load, 0), Trunc);
  DAG.ReplaceAllUsesOfValueWith(SDValue(Load, 1), SDValue(ExtLoad, 1));
  deleteAndRecombine(Load);
  AddToWorklist(Trunc.getNode());
}

SDValue DAGCombiner::PromoteOperand(SDValue Op, EVT PVT, bool &Replace) {
  Replace = false;
  SDLoc DL(Op);
  if (ISD::isUNINDEXEDLoad(Op.getNode())) {
    LoadSDNode *LD = cast<LoadSDNode>(Op);
    EVT MemVT = LD->getMemoryVT();
    ISD::LoadExtType ExtType = ISD::isNON_EXTLoad(LD) ? ISD::EXTLOAD
                                                      : LD->getExtensionType();
    Replace = true;
    return DAG.getExtLoad(ExtType, DL, PVT,
                          LD->getChain(), LD->getBasePtr(),
                          MemVT, LD->getMemOperand());
  }

  unsigned Opc = Op.getOpcode();
  switch (Opc) {
  default: break;
  case ISD::AssertSext:
    if (SDValue Op0 = SExtPromoteOperand(Op.getOperand(0), PVT))
      return DAG.getNode(ISD::AssertSext, DL, PVT, Op0, Op.getOperand(1));
    break;
  case ISD::AssertZext:
    if (SDValue Op0 = ZExtPromoteOperand(Op.getOperand(0), PVT))
      return DAG.getNode(ISD::AssertZext, DL, PVT, Op0, Op.getOperand(1));
    break;
  case ISD::Constant: {
    unsigned ExtOpc =
      Op.getValueType().isByteSized() ? ISD::SIGN_EXTEND : ISD::ZERO_EXTEND;
    return DAG.getNode(ExtOpc, DL, PVT, Op);
  }
  }

  if (!TLI.isOperationLegal(ISD::ANY_EXTEND, PVT))
    return SDValue();
  return DAG.getNode(ISD::ANY_EXTEND, DL, PVT, Op);
}

SDValue DAGCombiner::SExtPromoteOperand(SDValue Op, EVT PVT) {
  if (!TLI.isOperationLegal(ISD::SIGN_EXTEND_INREG, PVT))
    return SDValue();
  EVT OldVT = Op.getValueType();
  SDLoc DL(Op);
  bool Replace = false;
  SDValue NewOp = PromoteOperand(Op, PVT, Replace);
  if (!NewOp.getNode())
    return SDValue();
  AddToWorklist(NewOp.getNode());

  if (Replace)
    ReplaceLoadWithPromotedLoad(Op.getNode(), NewOp.getNode());
  return DAG.getNode(ISD::SIGN_EXTEND_INREG, DL, NewOp.getValueType(), NewOp,
                     DAG.getValueType(OldVT));
}

SDValue DAGCombiner::ZExtPromoteOperand(SDValue Op, EVT PVT) {
  EVT OldVT = Op.getValueType();
  SDLoc DL(Op);
  bool Replace = false;
  SDValue NewOp = PromoteOperand(Op, PVT, Replace);
  if (!NewOp.getNode())
    return SDValue();
  AddToWorklist(NewOp.getNode());

  if (Replace)
    ReplaceLoadWithPromotedLoad(Op.getNode(), NewOp.getNode());
  return DAG.getZeroExtendInReg(NewOp, DL, OldVT);
}

/// Promote the specified integer binary operation if the target indicates it is
/// beneficial. e.g. On x86, it's usually better to promote i16 operations to
/// i32 since i16 instructions are longer.
SDValue DAGCombiner::PromoteIntBinOp(SDValue Op) {
  if (!LegalOperations)
    return SDValue();

  EVT VT = Op.getValueType();
  if (VT.isVector() || !VT.isInteger())
    return SDValue();

  // If operation type is 'undesirable', e.g. i16 on x86, consider
  // promoting it.
  unsigned Opc = Op.getOpcode();
  if (TLI.isTypeDesirableForOp(Opc, VT))
    return SDValue();

  EVT PVT = VT;
  // Consult target whether it is a good idea to promote this operation and
  // what's the right type to promote it to.
  if (TLI.IsDesirableToPromoteOp(Op, PVT)) {
    assert(PVT != VT && "Don't know what type to promote to!");

    LLVM_DEBUG(dbgs() << "\nPromoting "; Op.getNode()->dump(&DAG));

    bool Replace0 = false;
    SDValue N0 = Op.getOperand(0);
    SDValue NN0 = PromoteOperand(N0, PVT, Replace0);

    bool Replace1 = false;
    SDValue N1 = Op.getOperand(1);
    SDValue NN1 = PromoteOperand(N1, PVT, Replace1);
    SDLoc DL(Op);

    SDValue RV =
        DAG.getNode(ISD::TRUNCATE, DL, VT, DAG.getNode(Opc, DL, PVT, NN0, NN1));

    // We are always replacing N0/N1's use in N and only need
    // additional replacements if there are additional uses.
    Replace0 &= !N0->hasOneUse();
    Replace1 &= (N0 != N1) && !N1->hasOneUse();

    // Combine Op here so it is preserved past replacements.
    CombineTo(Op.getNode(), RV);

    // If operands have a use ordering, make sure we deal with
    // predecessor first.
    if (Replace0 && Replace1 && N0.getNode()->isPredecessorOf(N1.getNode())) {
      std::swap(N0, N1);
      std::swap(NN0, NN1);
    }

    if (Replace0) {
      AddToWorklist(NN0.getNode());
      ReplaceLoadWithPromotedLoad(N0.getNode(), NN0.getNode());
    }
    if (Replace1) {
      AddToWorklist(NN1.getNode());
      ReplaceLoadWithPromotedLoad(N1.getNode(), NN1.getNode());
    }
    return Op;
  }
  return SDValue();
}

/// Promote the specified integer shift operation if the target indicates it is
/// beneficial. e.g. On x86, it's usually better to promote i16 operations to
/// i32 since i16 instructions are longer.
SDValue DAGCombiner::PromoteIntShiftOp(SDValue Op) {
  if (!LegalOperations)
    return SDValue();

  EVT VT = Op.getValueType();
  if (VT.isVector() || !VT.isInteger())
    return SDValue();

  // If operation type is 'undesirable', e.g. i16 on x86, consider
  // promoting it.
  unsigned Opc = Op.getOpcode();
  if (TLI.isTypeDesirableForOp(Opc, VT))
    return SDValue();

  EVT PVT = VT;
  // Consult target whether it is a good idea to promote this operation and
  // what's the right type to promote it to.
  if (TLI.IsDesirableToPromoteOp(Op, PVT)) {
    assert(PVT != VT && "Don't know what type to promote to!");

    LLVM_DEBUG(dbgs() << "\nPromoting "; Op.getNode()->dump(&DAG));

    bool Replace = false;
    SDValue N0 = Op.getOperand(0);
    SDValue N1 = Op.getOperand(1);
    if (Opc == ISD::SRA)
      N0 = SExtPromoteOperand(N0, PVT);
    else if (Opc == ISD::SRL)
      N0 = ZExtPromoteOperand(N0, PVT);
    else
      N0 = PromoteOperand(N0, PVT, Replace);

    if (!N0.getNode())
      return SDValue();

    SDLoc DL(Op);
    SDValue RV =
        DAG.getNode(ISD::TRUNCATE, DL, VT, DAG.getNode(Opc, DL, PVT, N0, N1));

    AddToWorklist(N0.getNode());
    if (Replace)
      ReplaceLoadWithPromotedLoad(Op.getOperand(0).getNode(), N0.getNode());

    // Deal with Op being deleted.
    if (Op && Op.getOpcode() != ISD::DELETED_NODE)
      return RV;
  }
  return SDValue();
}

SDValue DAGCombiner::PromoteExtend(SDValue Op) {
  if (!LegalOperations)
    return SDValue();

  EVT VT = Op.getValueType();
  if (VT.isVector() || !VT.isInteger())
    return SDValue();

  // If operation type is 'undesirable', e.g. i16 on x86, consider
  // promoting it.
  unsigned Opc = Op.getOpcode();
  if (TLI.isTypeDesirableForOp(Opc, VT))
    return SDValue();

  EVT PVT = VT;
  // Consult target whether it is a good idea to promote this operation and
  // what's the right type to promote it to.
  if (TLI.IsDesirableToPromoteOp(Op, PVT)) {
    assert(PVT != VT && "Don't know what type to promote to!");
    // fold (aext (aext x)) -> (aext x)
    // fold (aext (zext x)) -> (zext x)
    // fold (aext (sext x)) -> (sext x)
    LLVM_DEBUG(dbgs() << "\nPromoting "; Op.getNode()->dump(&DAG));
    return DAG.getNode(Op.getOpcode(), SDLoc(Op), VT, Op.getOperand(0));
  }
  return SDValue();
}

bool DAGCombiner::PromoteLoad(SDValue Op) {
  if (!LegalOperations)
    return false;

  if (!ISD::isUNINDEXEDLoad(Op.getNode()))
    return false;

  EVT VT = Op.getValueType();
  if (VT.isVector() || !VT.isInteger())
    return false;

  // If operation type is 'undesirable', e.g. i16 on x86, consider
  // promoting it.
  unsigned Opc = Op.getOpcode();
  if (TLI.isTypeDesirableForOp(Opc, VT))
    return false;

  EVT PVT = VT;
  // Consult target whether it is a good idea to promote this operation and
  // what's the right type to promote it to.
  if (TLI.IsDesirableToPromoteOp(Op, PVT)) {
    assert(PVT != VT && "Don't know what type to promote to!");

    SDLoc DL(Op);
    SDNode *N = Op.getNode();
    LoadSDNode *LD = cast<LoadSDNode>(N);
    EVT MemVT = LD->getMemoryVT();
    ISD::LoadExtType ExtType = ISD::isNON_EXTLoad(LD) ? ISD::EXTLOAD
                                                      : LD->getExtensionType();
    SDValue NewLD = DAG.getExtLoad(ExtType, DL, PVT,
                                   LD->getChain(), LD->getBasePtr(),
                                   MemVT, LD->getMemOperand());
    SDValue Result = DAG.getNode(ISD::TRUNCATE, DL, VT, NewLD);

    LLVM_DEBUG(dbgs() << "\nPromoting "; N->dump(&DAG); dbgs() << "\nTo: ";
               Result.getNode()->dump(&DAG); dbgs() << '\n');
    WorklistRemover DeadNodes(*this);
    DAG.ReplaceAllUsesOfValueWith(SDValue(N, 0), Result);
    DAG.ReplaceAllUsesOfValueWith(SDValue(N, 1), NewLD.getValue(1));
    deleteAndRecombine(N);
    AddToWorklist(Result.getNode());
    return true;
  }
  return false;
}

/// Recursively delete a node which has no uses and any operands for
/// which it is the only use.
///
/// Note that this both deletes the nodes and removes them from the worklist.
/// It also adds any nodes who have had a user deleted to the worklist as they
/// may now have only one use and subject to other combines.
bool DAGCombiner::recursivelyDeleteUnusedNodes(SDNode *N) {
  if (!N->use_empty())
    return false;

  SmallSetVector<SDNode *, 16> Nodes;
  Nodes.insert(N);
  do {
    N = Nodes.pop_back_val();
    if (!N)
      continue;

    if (N->use_empty()) {
      for (const SDValue &ChildN : N->op_values())
        Nodes.insert(ChildN.getNode());

      removeFromWorklist(N);
      DAG.DeleteNode(N);
    } else {
      AddToWorklist(N);
    }
  } while (!Nodes.empty());
  return true;
}

//===----------------------------------------------------------------------===//
//  Main DAG Combiner implementation
//===----------------------------------------------------------------------===//

void DAGCombiner::Run(CombineLevel AtLevel) {
  // set the instance variables, so that the various visit routines may use it.
  Level = AtLevel;
  LegalOperations = Level >= AfterLegalizeVectorOps;
  LegalTypes = Level >= AfterLegalizeTypes;

  // Add all the dag nodes to the worklist.
  for (SDNode &Node : DAG.allnodes())
    AddToWorklist(&Node);

  // Create a dummy node (which is not added to allnodes), that adds a reference
  // to the root node, preventing it from being deleted, and tracking any
  // changes of the root.
  HandleSDNode Dummy(DAG.getRoot());

  // While the worklist isn't empty, find a node and try to combine it.
  while (!WorklistMap.empty()) {
    SDNode *N;
    // The Worklist holds the SDNodes in order, but it may contain null entries.
    do {
      N = Worklist.pop_back_val();
    } while (!N);

    bool GoodWorklistEntry = WorklistMap.erase(N);
    (void)GoodWorklistEntry;
    assert(GoodWorklistEntry &&
           "Found a worklist entry without a corresponding map entry!");

    // If N has no uses, it is dead.  Make sure to revisit all N's operands once
    // N is deleted from the DAG, since they too may now be dead or may have a
    // reduced number of uses, allowing other xforms.
    if (recursivelyDeleteUnusedNodes(N))
      continue;

    WorklistRemover DeadNodes(*this);

    // If this combine is running after legalizing the DAG, re-legalize any
    // nodes pulled off the worklist.
    if (Level == AfterLegalizeDAG) {
      SmallSetVector<SDNode *, 16> UpdatedNodes;
      bool NIsValid = DAG.LegalizeOp(N, UpdatedNodes);

      for (SDNode *LN : UpdatedNodes) {
        AddToWorklist(LN);
        AddUsersToWorklist(LN);
      }
      if (!NIsValid)
        continue;
    }

    LLVM_DEBUG(dbgs() << "\nCombining: "; N->dump(&DAG));

    // Add any operands of the new node which have not yet been combined to the
    // worklist as well. Because the worklist uniques things already, this
    // won't repeatedly process the same operand.
    CombinedNodes.insert(N);
    for (const SDValue &ChildN : N->op_values())
      if (!CombinedNodes.count(ChildN.getNode()))
        AddToWorklist(ChildN.getNode());

    SDValue RV = combine(N);

    if (!RV.getNode())
      continue;

    ++NodesCombined;

    // If we get back the same node we passed in, rather than a new node or
    // zero, we know that the node must have defined multiple values and
    // CombineTo was used.  Since CombineTo takes care of the worklist
    // mechanics for us, we have no work to do in this case.
    if (RV.getNode() == N)
      continue;

    assert(N->getOpcode() != ISD::DELETED_NODE &&
           RV.getOpcode() != ISD::DELETED_NODE &&
           "Node was deleted but visit returned new node!");

    LLVM_DEBUG(dbgs() << " ... into: "; RV.getNode()->dump(&DAG));

    if (N->getNumValues() == RV.getNode()->getNumValues())
      DAG.ReplaceAllUsesWith(N, RV.getNode());
    else {
      assert(N->getValueType(0) == RV.getValueType() &&
             N->getNumValues() == 1 && "Type mismatch");
      DAG.ReplaceAllUsesWith(N, &RV);
    }

    // Push the new node and any users onto the worklist
    AddToWorklist(RV.getNode());
    AddUsersToWorklist(RV.getNode());

    // Finally, if the node is now dead, remove it from the graph.  The node
    // may not be dead if the replacement process recursively simplified to
    // something else needing this node. This will also take care of adding any
    // operands which have lost a user to the worklist.
    recursivelyDeleteUnusedNodes(N);
  }

  // If the root changed (e.g. it was a dead load, update the root).
  DAG.setRoot(Dummy.getValue());
  DAG.RemoveDeadNodes();
}

SDValue DAGCombiner::visit(SDNode *N) {
  switch (N->getOpcode()) {
  default: break;
  case ISD::TokenFactor:        return visitTokenFactor(N);
  case ISD::MERGE_VALUES:       return visitMERGE_VALUES(N);
  case ISD::ADD:                return visitADD(N);
  case ISD::SUB:                return visitSUB(N);
  case ISD::SADDSAT:
  case ISD::UADDSAT:            return visitADDSAT(N);
  case ISD::SSUBSAT:
  case ISD::USUBSAT:            return visitSUBSAT(N);
  case ISD::ADDC:               return visitADDC(N);
  case ISD::UADDO:              return visitUADDO(N);
  case ISD::SUBC:               return visitSUBC(N);
  case ISD::USUBO:              return visitUSUBO(N);
  case ISD::ADDE:               return visitADDE(N);
  case ISD::ADDCARRY:           return visitADDCARRY(N);
  case ISD::SUBE:               return visitSUBE(N);
  case ISD::SUBCARRY:           return visitSUBCARRY(N);
  case ISD::MUL:                return visitMUL(N);
  case ISD::SDIV:               return visitSDIV(N);
  case ISD::UDIV:               return visitUDIV(N);
  case ISD::SREM:
  case ISD::UREM:               return visitREM(N);
  case ISD::MULHU:              return visitMULHU(N);
  case ISD::MULHS:              return visitMULHS(N);
  case ISD::SMUL_LOHI:          return visitSMUL_LOHI(N);
  case ISD::UMUL_LOHI:          return visitUMUL_LOHI(N);
  case ISD::SMULO:              return visitSMULO(N);
  case ISD::UMULO:              return visitUMULO(N);
  case ISD::SMIN:
  case ISD::SMAX:
  case ISD::UMIN:
  case ISD::UMAX:               return visitIMINMAX(N);
  case ISD::AND:                return visitAND(N);
  case ISD::OR:                 return visitOR(N);
  case ISD::XOR:                return visitXOR(N);
  case ISD::SHL:                return visitSHL(N);
  case ISD::SRA:                return visitSRA(N);
  case ISD::SRL:                return visitSRL(N);
  case ISD::ROTR:
  case ISD::ROTL:               return visitRotate(N);
  case ISD::FSHL:
  case ISD::FSHR:               return visitFunnelShift(N);
  case ISD::ABS:                return visitABS(N);
  case ISD::BSWAP:              return visitBSWAP(N);
  case ISD::BITREVERSE:         return visitBITREVERSE(N);
  case ISD::CTLZ:               return visitCTLZ(N);
  case ISD::CTLZ_ZERO_UNDEF:    return visitCTLZ_ZERO_UNDEF(N);
  case ISD::CTTZ:               return visitCTTZ(N);
  case ISD::CTTZ_ZERO_UNDEF:    return visitCTTZ_ZERO_UNDEF(N);
  case ISD::CTPOP:              return visitCTPOP(N);
  case ISD::SELECT:             return visitSELECT(N);
  case ISD::VSELECT:            return visitVSELECT(N);
  case ISD::SELECT_CC:          return visitSELECT_CC(N);
  case ISD::SETCC:              return visitSETCC(N);
  case ISD::SETCCCARRY:         return visitSETCCCARRY(N);
  case ISD::SIGN_EXTEND:        return visitSIGN_EXTEND(N);
  case ISD::ZERO_EXTEND:        return visitZERO_EXTEND(N);
  case ISD::ANY_EXTEND:         return visitANY_EXTEND(N);
  case ISD::AssertSext:
  case ISD::AssertZext:         return visitAssertExt(N);
  case ISD::SIGN_EXTEND_INREG:  return visitSIGN_EXTEND_INREG(N);
  case ISD::SIGN_EXTEND_VECTOR_INREG: return visitSIGN_EXTEND_VECTOR_INREG(N);
  case ISD::ZERO_EXTEND_VECTOR_INREG: return visitZERO_EXTEND_VECTOR_INREG(N);
  case ISD::TRUNCATE:           return visitTRUNCATE(N);
  case ISD::BITCAST:            return visitBITCAST(N);
  case ISD::BUILD_PAIR:         return visitBUILD_PAIR(N);
  case ISD::FADD:               return visitFADD(N);
  case ISD::FSUB:               return visitFSUB(N);
  case ISD::FMUL:               return visitFMUL(N);
  case ISD::FMA:                return visitFMA(N);
  case ISD::FDIV:               return visitFDIV(N);
  case ISD::FREM:               return visitFREM(N);
  case ISD::FSQRT:              return visitFSQRT(N);
  case ISD::FCOPYSIGN:          return visitFCOPYSIGN(N);
  case ISD::FPOW:               return visitFPOW(N);
  case ISD::SINT_TO_FP:         return visitSINT_TO_FP(N);
  case ISD::UINT_TO_FP:         return visitUINT_TO_FP(N);
  case ISD::FP_TO_SINT:         return visitFP_TO_SINT(N);
  case ISD::FP_TO_UINT:         return visitFP_TO_UINT(N);
  case ISD::FP_ROUND:           return visitFP_ROUND(N);
  case ISD::FP_ROUND_INREG:     return visitFP_ROUND_INREG(N);
  case ISD::FP_EXTEND:          return visitFP_EXTEND(N);
  case ISD::FNEG:               return visitFNEG(N);
  case ISD::FABS:               return visitFABS(N);
  case ISD::FFLOOR:             return visitFFLOOR(N);
  case ISD::FMINNUM:            return visitFMINNUM(N);
  case ISD::FMAXNUM:            return visitFMAXNUM(N);
  case ISD::FMINIMUM:           return visitFMINIMUM(N);
  case ISD::FMAXIMUM:           return visitFMAXIMUM(N);
  case ISD::FCEIL:              return visitFCEIL(N);
  case ISD::FTRUNC:             return visitFTRUNC(N);
  case ISD::BRCOND:             return visitBRCOND(N);
  case ISD::BR_CC:              return visitBR_CC(N);
  case ISD::LOAD:               return visitLOAD(N);
  case ISD::STORE:              return visitSTORE(N);
  case ISD::INSERT_VECTOR_ELT:  return visitINSERT_VECTOR_ELT(N);
  case ISD::EXTRACT_VECTOR_ELT: return visitEXTRACT_VECTOR_ELT(N);
  case ISD::BUILD_VECTOR:       return visitBUILD_VECTOR(N);
  case ISD::CONCAT_VECTORS:     return visitCONCAT_VECTORS(N);
  case ISD::EXTRACT_SUBVECTOR:  return visitEXTRACT_SUBVECTOR(N);
  case ISD::VECTOR_SHUFFLE:     return visitVECTOR_SHUFFLE(N);
  case ISD::SCALAR_TO_VECTOR:   return visitSCALAR_TO_VECTOR(N);
  case ISD::INSERT_SUBVECTOR:   return visitINSERT_SUBVECTOR(N);
  case ISD::MGATHER:            return visitMGATHER(N);
  case ISD::MLOAD:              return visitMLOAD(N);
  case ISD::MSCATTER:           return visitMSCATTER(N);
  case ISD::MSTORE:             return visitMSTORE(N);
  case ISD::FP_TO_FP16:         return visitFP_TO_FP16(N);
  case ISD::FP16_TO_FP:         return visitFP16_TO_FP(N);
  }
  return SDValue();
}

SDValue DAGCombiner::combine(SDNode *N) {
  SDValue RV = visit(N);

  // If nothing happened, try a target-specific DAG combine.
  if (!RV.getNode()) {
    assert(N->getOpcode() != ISD::DELETED_NODE &&
           "Node was deleted but visit returned NULL!");

    if (N->getOpcode() >= ISD::BUILTIN_OP_END ||
        TLI.hasTargetDAGCombine((ISD::NodeType)N->getOpcode())) {

      // Expose the DAG combiner to the target combiner impls.
      TargetLowering::DAGCombinerInfo
        DagCombineInfo(DAG, Level, false, this);

      RV = TLI.PerformDAGCombine(N, DagCombineInfo);
    }
  }

  // If nothing happened still, try promoting the operation.
  if (!RV.getNode()) {
    switch (N->getOpcode()) {
    default: break;
    case ISD::ADD:
    case ISD::SUB:
    case ISD::MUL:
    case ISD::AND:
    case ISD::OR:
    case ISD::XOR:
      RV = PromoteIntBinOp(SDValue(N, 0));
      break;
    case ISD::SHL:
    case ISD::SRA:
    case ISD::SRL:
      RV = PromoteIntShiftOp(SDValue(N, 0));
      break;
    case ISD::SIGN_EXTEND:
    case ISD::ZERO_EXTEND:
    case ISD::ANY_EXTEND:
      RV = PromoteExtend(SDValue(N, 0));
      break;
    case ISD::LOAD:
      if (PromoteLoad(SDValue(N, 0)))
        RV = SDValue(N, 0);
      break;
    }
  }

  // If N is a commutative binary node, try eliminate it if the commuted
  // version is already present in the DAG.
  if (!RV.getNode() && TLI.isCommutativeBinOp(N->getOpcode()) &&
      N->getNumValues() == 1) {
    SDValue N0 = N->getOperand(0);
    SDValue N1 = N->getOperand(1);

    // Constant operands are canonicalized to RHS.
    if (N0 != N1 && (isa<ConstantSDNode>(N0) || !isa<ConstantSDNode>(N1))) {
      SDValue Ops[] = {N1, N0};
      SDNode *CSENode = DAG.getNodeIfExists(N->getOpcode(), N->getVTList(), Ops,
                                            N->getFlags());
      if (CSENode)
        return SDValue(CSENode, 0);
    }
  }

  return RV;
}

/// Given a node, return its input chain if it has one, otherwise return a null
/// sd operand.
static SDValue getInputChainForNode(SDNode *N) {
  if (unsigned NumOps = N->getNumOperands()) {
    if (N->getOperand(0).getValueType() == MVT::Other)
      return N->getOperand(0);
    if (N->getOperand(NumOps-1).getValueType() == MVT::Other)
      return N->getOperand(NumOps-1);
    for (unsigned i = 1; i < NumOps-1; ++i)
      if (N->getOperand(i).getValueType() == MVT::Other)
        return N->getOperand(i);
  }
  return SDValue();
}

SDValue DAGCombiner::visitTokenFactor(SDNode *N) {
  // If N has two operands, where one has an input chain equal to the other,
  // the 'other' chain is redundant.
  if (N->getNumOperands() == 2) {
    if (getInputChainForNode(N->getOperand(0).getNode()) == N->getOperand(1))
      return N->getOperand(0);
    if (getInputChainForNode(N->getOperand(1).getNode()) == N->getOperand(0))
      return N->getOperand(1);
  }

  // Don't simplify token factors if optnone.
  if (OptLevel == CodeGenOpt::None)
    return SDValue();

  SmallVector<SDNode *, 8> TFs;     // List of token factors to visit.
  SmallVector<SDValue, 8> Ops;      // Ops for replacing token factor.
  SmallPtrSet<SDNode*, 16> SeenOps;
  bool Changed = false;             // If we should replace this token factor.

  // Start out with this token factor.
  TFs.push_back(N);

  // Iterate through token factors.  The TFs grows when new token factors are
  // encountered.
  for (unsigned i = 0; i < TFs.size(); ++i) {
    SDNode *TF = TFs[i];

    // Check each of the operands.
    for (const SDValue &Op : TF->op_values()) {
      switch (Op.getOpcode()) {
      case ISD::EntryToken:
        // Entry tokens don't need to be added to the list. They are
        // redundant.
        Changed = true;
        break;

      case ISD::TokenFactor:
        if (Op.hasOneUse() && !is_contained(TFs, Op.getNode())) {
          // Queue up for processing.
          TFs.push_back(Op.getNode());
          // Clean up in case the token factor is removed.
          AddToWorklist(Op.getNode());
          Changed = true;
          break;
        }
        LLVM_FALLTHROUGH;

      default:
        // Only add if it isn't already in the list.
        if (SeenOps.insert(Op.getNode()).second)
          Ops.push_back(Op);
        else
          Changed = true;
        break;
      }
    }
  }

  // Remove Nodes that are chained to another node in the list. Do so
  // by walking up chains breath-first stopping when we've seen
  // another operand. In general we must climb to the EntryNode, but we can exit
  // early if we find all remaining work is associated with just one operand as
  // no further pruning is possible.

  // List of nodes to search through and original Ops from which they originate.
  SmallVector<std::pair<SDNode *, unsigned>, 8> Worklist;
  SmallVector<unsigned, 8> OpWorkCount; // Count of work for each Op.
  SmallPtrSet<SDNode *, 16> SeenChains;
  bool DidPruneOps = false;

  unsigned NumLeftToConsider = 0;
  for (const SDValue &Op : Ops) {
    Worklist.push_back(std::make_pair(Op.getNode(), NumLeftToConsider++));
    OpWorkCount.push_back(1);
  }

  auto AddToWorklist = [&](unsigned CurIdx, SDNode *Op, unsigned OpNumber) {
    // If this is an Op, we can remove the op from the list. Remark any
    // search associated with it as from the current OpNumber.
    if (SeenOps.count(Op) != 0) {
      Changed = true;
      DidPruneOps = true;
      unsigned OrigOpNumber = 0;
      while (OrigOpNumber < Ops.size() && Ops[OrigOpNumber].getNode() != Op)
        OrigOpNumber++;
      assert((OrigOpNumber != Ops.size()) &&
             "expected to find TokenFactor Operand");
      // Re-mark worklist from OrigOpNumber to OpNumber
      for (unsigned i = CurIdx + 1; i < Worklist.size(); ++i) {
        if (Worklist[i].second == OrigOpNumber) {
          Worklist[i].second = OpNumber;
        }
      }
      OpWorkCount[OpNumber] += OpWorkCount[OrigOpNumber];
      OpWorkCount[OrigOpNumber] = 0;
      NumLeftToConsider--;
    }
    // Add if it's a new chain
    if (SeenChains.insert(Op).second) {
      OpWorkCount[OpNumber]++;
      Worklist.push_back(std::make_pair(Op, OpNumber));
    }
  };

  for (unsigned i = 0; i < Worklist.size() && i < 1024; ++i) {
    // We need at least be consider at least 2 Ops to prune.
    if (NumLeftToConsider <= 1)
      break;
    auto CurNode = Worklist[i].first;
    auto CurOpNumber = Worklist[i].second;
    assert((OpWorkCount[CurOpNumber] > 0) &&
           "Node should not appear in worklist");
    switch (CurNode->getOpcode()) {
    case ISD::EntryToken:
      // Hitting EntryToken is the only way for the search to terminate without
      // hitting
      // another operand's search. Prevent us from marking this operand
      // considered.
      NumLeftToConsider++;
      break;
    case ISD::TokenFactor:
      for (const SDValue &Op : CurNode->op_values())
        AddToWorklist(i, Op.getNode(), CurOpNumber);
      break;
    case ISD::CopyFromReg:
    case ISD::CopyToReg:
      AddToWorklist(i, CurNode->getOperand(0).getNode(), CurOpNumber);
      break;
    default:
      if (auto *MemNode = dyn_cast<MemSDNode>(CurNode))
        AddToWorklist(i, MemNode->getChain().getNode(), CurOpNumber);
      break;
    }
    OpWorkCount[CurOpNumber]--;
    if (OpWorkCount[CurOpNumber] == 0)
      NumLeftToConsider--;
  }

  // If we've changed things around then replace token factor.
  if (Changed) {
    SDValue Result;
    if (Ops.empty()) {
      // The entry token is the only possible outcome.
      Result = DAG.getEntryNode();
    } else {
      if (DidPruneOps) {
        SmallVector<SDValue, 8> PrunedOps;
        //
        for (const SDValue &Op : Ops) {
          if (SeenChains.count(Op.getNode()) == 0)
            PrunedOps.push_back(Op);
        }
        Result = DAG.getNode(ISD::TokenFactor, SDLoc(N), MVT::Other, PrunedOps);
      } else {
        Result = DAG.getNode(ISD::TokenFactor, SDLoc(N), MVT::Other, Ops);
      }
    }
    return Result;
  }
  return SDValue();
}

/// MERGE_VALUES can always be eliminated.
SDValue DAGCombiner::visitMERGE_VALUES(SDNode *N) {
  WorklistRemover DeadNodes(*this);
  // Replacing results may cause a different MERGE_VALUES to suddenly
  // be CSE'd with N, and carry its uses with it. Iterate until no
  // uses remain, to ensure that the node can be safely deleted.
  // First add the users of this node to the work list so that they
  // can be tried again once they have new operands.
  AddUsersToWorklist(N);
  do {
    // Do as a single replacement to avoid rewalking use lists.
    SmallVector<SDValue, 8> Ops;
    for (unsigned i = 0, e = N->getNumOperands(); i != e; ++i)
      Ops.push_back(N->getOperand(i));
    DAG.ReplaceAllUsesWith(N, Ops.data());
  } while (!N->use_empty());
  deleteAndRecombine(N);
  return SDValue(N, 0);   // Return N so it doesn't get rechecked!
}

/// If \p N is a ConstantSDNode with isOpaque() == false return it casted to a
/// ConstantSDNode pointer else nullptr.
static ConstantSDNode *getAsNonOpaqueConstant(SDValue N) {
  ConstantSDNode *Const = dyn_cast<ConstantSDNode>(N);
  return Const != nullptr && !Const->isOpaque() ? Const : nullptr;
}

SDValue DAGCombiner::foldBinOpIntoSelect(SDNode *BO) {
  assert(ISD::isBinaryOp(BO) && "Unexpected binary operator");

  // Don't do this unless the old select is going away. We want to eliminate the
  // binary operator, not replace a binop with a select.
  // TODO: Handle ISD::SELECT_CC.
  unsigned SelOpNo = 0;
  SDValue Sel = BO->getOperand(0);
  if (Sel.getOpcode() != ISD::SELECT || !Sel.hasOneUse()) {
    SelOpNo = 1;
    Sel = BO->getOperand(1);
  }

  if (Sel.getOpcode() != ISD::SELECT || !Sel.hasOneUse())
    return SDValue();

  SDValue CT = Sel.getOperand(1);
  if (!isConstantOrConstantVector(CT, true) &&
      !isConstantFPBuildVectorOrConstantFP(CT))
    return SDValue();

  SDValue CF = Sel.getOperand(2);
  if (!isConstantOrConstantVector(CF, true) &&
      !isConstantFPBuildVectorOrConstantFP(CF))
    return SDValue();

  // Bail out if any constants are opaque because we can't constant fold those.
  // The exception is "and" and "or" with either 0 or -1 in which case we can
  // propagate non constant operands into select. I.e.:
  // and (select Cond, 0, -1), X --> select Cond, 0, X
  // or X, (select Cond, -1, 0) --> select Cond, -1, X
  auto BinOpcode = BO->getOpcode();
  bool CanFoldNonConst =
      (BinOpcode == ISD::AND || BinOpcode == ISD::OR) &&
      (isNullOrNullSplat(CT) || isAllOnesOrAllOnesSplat(CT)) &&
      (isNullOrNullSplat(CF) || isAllOnesOrAllOnesSplat(CF));

  SDValue CBO = BO->getOperand(SelOpNo ^ 1);
  if (!CanFoldNonConst &&
      !isConstantOrConstantVector(CBO, true) &&
      !isConstantFPBuildVectorOrConstantFP(CBO))
    return SDValue();

  EVT VT = Sel.getValueType();

  // In case of shift value and shift amount may have different VT. For instance
  // on x86 shift amount is i8 regardles of LHS type. Bail out if we have
  // swapped operands and value types do not match. NB: x86 is fine if operands
  // are not swapped with shift amount VT being not bigger than shifted value.
  // TODO: that is possible to check for a shift operation, correct VTs and
  // still perform optimization on x86 if needed.
  if (SelOpNo && VT != CBO.getValueType())
    return SDValue();

  // We have a select-of-constants followed by a binary operator with a
  // constant. Eliminate the binop by pulling the constant math into the select.
  // Example: add (select Cond, CT, CF), CBO --> select Cond, CT + CBO, CF + CBO
  SDLoc DL(Sel);
  SDValue NewCT = SelOpNo ? DAG.getNode(BinOpcode, DL, VT, CBO, CT)
                          : DAG.getNode(BinOpcode, DL, VT, CT, CBO);
  if (!CanFoldNonConst && !NewCT.isUndef() &&
      !isConstantOrConstantVector(NewCT, true) &&
      !isConstantFPBuildVectorOrConstantFP(NewCT))
    return SDValue();

  SDValue NewCF = SelOpNo ? DAG.getNode(BinOpcode, DL, VT, CBO, CF)
                          : DAG.getNode(BinOpcode, DL, VT, CF, CBO);
  if (!CanFoldNonConst && !NewCF.isUndef() &&
      !isConstantOrConstantVector(NewCF, true) &&
      !isConstantFPBuildVectorOrConstantFP(NewCF))
    return SDValue();

  return DAG.getSelect(DL, VT, Sel.getOperand(0), NewCT, NewCF);
}

static SDValue foldAddSubBoolOfMaskedVal(SDNode *N, SelectionDAG &DAG) {
  assert((N->getOpcode() == ISD::ADD || N->getOpcode() == ISD::SUB) &&
         "Expecting add or sub");

  // Match a constant operand and a zext operand for the math instruction:
  // add Z, C
  // sub C, Z
  bool IsAdd = N->getOpcode() == ISD::ADD;
  SDValue C = IsAdd ? N->getOperand(1) : N->getOperand(0);
  SDValue Z = IsAdd ? N->getOperand(0) : N->getOperand(1);
  auto *CN = dyn_cast<ConstantSDNode>(C);
  if (!CN || Z.getOpcode() != ISD::ZERO_EXTEND)
    return SDValue();

  // Match the zext operand as a setcc of a boolean.
  if (Z.getOperand(0).getOpcode() != ISD::SETCC ||
      Z.getOperand(0).getValueType() != MVT::i1)
    return SDValue();

  // Match the compare as: setcc (X & 1), 0, eq.
  SDValue SetCC = Z.getOperand(0);
  ISD::CondCode CC = cast<CondCodeSDNode>(SetCC->getOperand(2))->get();
  if (CC != ISD::SETEQ || !isNullConstant(SetCC.getOperand(1)) ||
      SetCC.getOperand(0).getOpcode() != ISD::AND ||
      !isOneConstant(SetCC.getOperand(0).getOperand(1)))
    return SDValue();

  // We are adding/subtracting a constant and an inverted low bit. Turn that
  // into a subtract/add of the low bit with incremented/decremented constant:
  // add (zext i1 (seteq (X & 1), 0)), C --> sub C+1, (zext (X & 1))
  // sub C, (zext i1 (seteq (X & 1), 0)) --> add C-1, (zext (X & 1))
  EVT VT = C.getValueType();
  SDLoc DL(N);
  SDValue LowBit = DAG.getZExtOrTrunc(SetCC.getOperand(0), DL, VT);
  SDValue C1 = IsAdd ? DAG.getConstant(CN->getAPIntValue() + 1, DL, VT) :
                       DAG.getConstant(CN->getAPIntValue() - 1, DL, VT);
  return DAG.getNode(IsAdd ? ISD::SUB : ISD::ADD, DL, VT, C1, LowBit);
}

/// Try to fold a 'not' shifted sign-bit with add/sub with constant operand into
/// a shift and add with a different constant.
static SDValue foldAddSubOfSignBit(SDNode *N, SelectionDAG &DAG) {
  assert((N->getOpcode() == ISD::ADD || N->getOpcode() == ISD::SUB) &&
         "Expecting add or sub");

  // We need a constant operand for the add/sub, and the other operand is a
  // logical shift right: add (srl), C or sub C, (srl).
  bool IsAdd = N->getOpcode() == ISD::ADD;
  SDValue ConstantOp = IsAdd ? N->getOperand(1) : N->getOperand(0);
  SDValue ShiftOp = IsAdd ? N->getOperand(0) : N->getOperand(1);
  ConstantSDNode *C = isConstOrConstSplat(ConstantOp);
  if (!C || ShiftOp.getOpcode() != ISD::SRL)
    return SDValue();

  // The shift must be of a 'not' value.
  SDValue Not = ShiftOp.getOperand(0);
  if (!Not.hasOneUse() || !isBitwiseNot(Not))
    return SDValue();

  // The shift must be moving the sign bit to the least-significant-bit.
  EVT VT = ShiftOp.getValueType();
  SDValue ShAmt = ShiftOp.getOperand(1);
  ConstantSDNode *ShAmtC = isConstOrConstSplat(ShAmt);
  if (!ShAmtC || ShAmtC->getZExtValue() != VT.getScalarSizeInBits() - 1)
    return SDValue();

  // Eliminate the 'not' by adjusting the shift and add/sub constant:
  // add (srl (not X), 31), C --> add (sra X, 31), (C + 1)
  // sub C, (srl (not X), 31) --> add (srl X, 31), (C - 1)
  SDLoc DL(N);
  auto ShOpcode = IsAdd ? ISD::SRA : ISD::SRL;
  SDValue NewShift = DAG.getNode(ShOpcode, DL, VT, Not.getOperand(0), ShAmt);
  APInt NewC = IsAdd ? C->getAPIntValue() + 1 : C->getAPIntValue() - 1;
  return DAG.getNode(ISD::ADD, DL, VT, NewShift, DAG.getConstant(NewC, DL, VT));
}

SDValue DAGCombiner::visitADD(SDNode *N) {
  SDValue N0 = N->getOperand(0);
  SDValue N1 = N->getOperand(1);
  EVT VT = N0.getValueType();
  SDLoc DL(N);

  // fold vector ops
  if (VT.isVector()) {
    if (SDValue FoldedVOp = SimplifyVBinOp(N))
      return FoldedVOp;

    // fold (add x, 0) -> x, vector edition
    if (ISD::isBuildVectorAllZeros(N1.getNode()))
      return N0;
    if (ISD::isBuildVectorAllZeros(N0.getNode()))
      return N1;
  }

  // fold (add x, undef) -> undef
  if (N0.isUndef())
    return N0;

  if (N1.isUndef())
    return N1;

  if (DAG.isConstantIntBuildVectorOrConstantInt(N0)) {
    // canonicalize constant to RHS
    if (!DAG.isConstantIntBuildVectorOrConstantInt(N1))
      return DAG.getNode(ISD::ADD, DL, VT, N1, N0);
    // fold (add c1, c2) -> c1+c2
    return DAG.FoldConstantArithmetic(ISD::ADD, DL, VT, N0.getNode(),
                                      N1.getNode());
  }

  // fold (add x, 0) -> x
  if (isNullConstant(N1))
    return N0;

  if (isConstantOrConstantVector(N1, /* NoOpaque */ true)) {
    // fold ((c1-A)+c2) -> (c1+c2)-A
    if (N0.getOpcode() == ISD::SUB &&
        isConstantOrConstantVector(N0.getOperand(0), /* NoOpaque */ true)) {
      // FIXME: Adding 2 constants should be handled by FoldConstantArithmetic.
      return DAG.getNode(ISD::SUB, DL, VT,
                         DAG.getNode(ISD::ADD, DL, VT, N1, N0.getOperand(0)),
                         N0.getOperand(1));
    }

    // add (sext i1 X), 1 -> zext (not i1 X)
    // We don't transform this pattern:
    //   add (zext i1 X), -1 -> sext (not i1 X)
    // because most (?) targets generate better code for the zext form.
    if (N0.getOpcode() == ISD::SIGN_EXTEND && N0.hasOneUse() &&
        isOneOrOneSplat(N1)) {
      SDValue X = N0.getOperand(0);
      if ((!LegalOperations ||
           (TLI.isOperationLegal(ISD::XOR, X.getValueType()) &&
            TLI.isOperationLegal(ISD::ZERO_EXTEND, VT))) &&
          X.getScalarValueSizeInBits() == 1) {
        SDValue Not = DAG.getNOT(DL, X, X.getValueType());
        return DAG.getNode(ISD::ZERO_EXTEND, DL, VT, Not);
      }
    }

    // Undo the add -> or combine to merge constant offsets from a frame index.
    if (N0.getOpcode() == ISD::OR &&
        isa<FrameIndexSDNode>(N0.getOperand(0)) &&
        isa<ConstantSDNode>(N0.getOperand(1)) &&
        DAG.haveNoCommonBitsSet(N0.getOperand(0), N0.getOperand(1))) {
      SDValue Add0 = DAG.getNode(ISD::ADD, DL, VT, N1, N0.getOperand(1));
      return DAG.getNode(ISD::ADD, DL, VT, N0.getOperand(0), Add0);
    }
  }

  if (SDValue NewSel = foldBinOpIntoSelect(N))
    return NewSel;

  // reassociate add
  if (SDValue RADD = ReassociateOps(ISD::ADD, DL, N0, N1, N->getFlags()))
    return RADD;

  // fold ((0-A) + B) -> B-A
  if (N0.getOpcode() == ISD::SUB && isNullOrNullSplat(N0.getOperand(0)))
    return DAG.getNode(ISD::SUB, DL, VT, N1, N0.getOperand(1));

  // fold (A + (0-B)) -> A-B
  if (N1.getOpcode() == ISD::SUB && isNullOrNullSplat(N1.getOperand(0)))
    return DAG.getNode(ISD::SUB, DL, VT, N0, N1.getOperand(1));

  // fold (A+(B-A)) -> B
  if (N1.getOpcode() == ISD::SUB && N0 == N1.getOperand(1))
    return N1.getOperand(0);

  // fold ((B-A)+A) -> B
  if (N0.getOpcode() == ISD::SUB && N1 == N0.getOperand(1))
    return N0.getOperand(0);

  // fold (A+(B-(A+C))) to (B-C)
  if (N1.getOpcode() == ISD::SUB && N1.getOperand(1).getOpcode() == ISD::ADD &&
      N0 == N1.getOperand(1).getOperand(0))
    return DAG.getNode(ISD::SUB, DL, VT, N1.getOperand(0),
                       N1.getOperand(1).getOperand(1));

  // fold (A+(B-(C+A))) to (B-C)
  if (N1.getOpcode() == ISD::SUB && N1.getOperand(1).getOpcode() == ISD::ADD &&
      N0 == N1.getOperand(1).getOperand(1))
    return DAG.getNode(ISD::SUB, DL, VT, N1.getOperand(0),
                       N1.getOperand(1).getOperand(0));

  // fold (A+((B-A)+or-C)) to (B+or-C)
  if ((N1.getOpcode() == ISD::SUB || N1.getOpcode() == ISD::ADD) &&
      N1.getOperand(0).getOpcode() == ISD::SUB &&
      N0 == N1.getOperand(0).getOperand(1))
    return DAG.getNode(N1.getOpcode(), DL, VT, N1.getOperand(0).getOperand(0),
                       N1.getOperand(1));

  // fold (A-B)+(C-D) to (A+C)-(B+D) when A or C is constant
  if (N0.getOpcode() == ISD::SUB && N1.getOpcode() == ISD::SUB) {
    SDValue N00 = N0.getOperand(0);
    SDValue N01 = N0.getOperand(1);
    SDValue N10 = N1.getOperand(0);
    SDValue N11 = N1.getOperand(1);

    if (isConstantOrConstantVector(N00) || isConstantOrConstantVector(N10))
      return DAG.getNode(ISD::SUB, DL, VT,
                         DAG.getNode(ISD::ADD, SDLoc(N0), VT, N00, N10),
                         DAG.getNode(ISD::ADD, SDLoc(N1), VT, N01, N11));
  }

  if (SDValue V = foldAddSubBoolOfMaskedVal(N, DAG))
    return V;

  if (SDValue V = foldAddSubOfSignBit(N, DAG))
    return V;

  if (SimplifyDemandedBits(SDValue(N, 0)))
    return SDValue(N, 0);

  // fold (a+b) -> (a|b) iff a and b share no bits.
  if ((!LegalOperations || TLI.isOperationLegal(ISD::OR, VT)) &&
      DAG.haveNoCommonBitsSet(N0, N1))
    return DAG.getNode(ISD::OR, DL, VT, N0, N1);

  // fold (add (xor a, -1), 1) -> (sub 0, a)
  if (isBitwiseNot(N0) && isOneOrOneSplat(N1))
    return DAG.getNode(ISD::SUB, DL, VT, DAG.getConstant(0, DL, VT),
                       N0.getOperand(0));

  if (SDValue Combined = visitADDLike(N0, N1, N))
    return Combined;

  if (SDValue Combined = visitADDLike(N1, N0, N))
    return Combined;

  return SDValue();
}

SDValue DAGCombiner::visitADDSAT(SDNode *N) {
  unsigned Opcode = N->getOpcode();
  SDValue N0 = N->getOperand(0);
  SDValue N1 = N->getOperand(1);
  EVT VT = N0.getValueType();
  SDLoc DL(N);

  // fold vector ops
  if (VT.isVector()) {
    // TODO SimplifyVBinOp

    // fold (add_sat x, 0) -> x, vector edition
    if (ISD::isBuildVectorAllZeros(N1.getNode()))
      return N0;
    if (ISD::isBuildVectorAllZeros(N0.getNode()))
      return N1;
  }

  // fold (add_sat x, undef) -> -1
  if (N0.isUndef() || N1.isUndef())
    return DAG.getAllOnesConstant(DL, VT);

  if (DAG.isConstantIntBuildVectorOrConstantInt(N0)) {
    // canonicalize constant to RHS
    if (!DAG.isConstantIntBuildVectorOrConstantInt(N1))
      return DAG.getNode(Opcode, DL, VT, N1, N0);
    // fold (add_sat c1, c2) -> c3
    return DAG.FoldConstantArithmetic(Opcode, DL, VT, N0.getNode(),
                                      N1.getNode());
  }

  // fold (add_sat x, 0) -> x
  if (isNullConstant(N1))
    return N0;

  // If it cannot overflow, transform into an add.
  if (Opcode == ISD::UADDSAT)
    if (DAG.computeOverflowKind(N0, N1) == SelectionDAG::OFK_Never)
      return DAG.getNode(ISD::ADD, DL, VT, N0, N1);

  return SDValue();
}

static SDValue getAsCarry(const TargetLowering &TLI, SDValue V) {
  bool Masked = false;

  // First, peel away TRUNCATE/ZERO_EXTEND/AND nodes due to legalization.
  while (true) {
    if (V.getOpcode() == ISD::TRUNCATE || V.getOpcode() == ISD::ZERO_EXTEND) {
      V = V.getOperand(0);
      continue;
    }

    if (V.getOpcode() == ISD::AND && isOneConstant(V.getOperand(1))) {
      Masked = true;
      V = V.getOperand(0);
      continue;
    }

    break;
  }

  // If this is not a carry, return.
  if (V.getResNo() != 1)
    return SDValue();

  if (V.getOpcode() != ISD::ADDCARRY && V.getOpcode() != ISD::SUBCARRY &&
      V.getOpcode() != ISD::UADDO && V.getOpcode() != ISD::USUBO)
    return SDValue();

  // If the result is masked, then no matter what kind of bool it is we can
  // return. If it isn't, then we need to make sure the bool type is either 0 or
  // 1 and not other values.
  if (Masked ||
      TLI.getBooleanContents(V.getValueType()) ==
          TargetLoweringBase::ZeroOrOneBooleanContent)
    return V;

  return SDValue();
}

SDValue DAGCombiner::visitADDLike(SDValue N0, SDValue N1, SDNode *LocReference) {
  EVT VT = N0.getValueType();
  SDLoc DL(LocReference);

  // fold (add x, shl(0 - y, n)) -> sub(x, shl(y, n))
  if (N1.getOpcode() == ISD::SHL && N1.getOperand(0).getOpcode() == ISD::SUB &&
      isNullOrNullSplat(N1.getOperand(0).getOperand(0)))
    return DAG.getNode(ISD::SUB, DL, VT, N0,
                       DAG.getNode(ISD::SHL, DL, VT,
                                   N1.getOperand(0).getOperand(1),
                                   N1.getOperand(1)));

  if (N1.getOpcode() == ISD::AND) {
    SDValue AndOp0 = N1.getOperand(0);
    unsigned NumSignBits = DAG.ComputeNumSignBits(AndOp0);
    unsigned DestBits = VT.getScalarSizeInBits();

    // (add z, (and (sbbl x, x), 1)) -> (sub z, (sbbl x, x))
    // and similar xforms where the inner op is either ~0 or 0.
    if (NumSignBits == DestBits && isOneOrOneSplat(N1->getOperand(1)))
      return DAG.getNode(ISD::SUB, DL, VT, N0, AndOp0);
  }

  // add (sext i1), X -> sub X, (zext i1)
  if (N0.getOpcode() == ISD::SIGN_EXTEND &&
      N0.getOperand(0).getValueType() == MVT::i1 &&
      !TLI.isOperationLegal(ISD::SIGN_EXTEND, MVT::i1)) {
    SDValue ZExt = DAG.getNode(ISD::ZERO_EXTEND, DL, VT, N0.getOperand(0));
    return DAG.getNode(ISD::SUB, DL, VT, N1, ZExt);
  }

  // add X, (sextinreg Y i1) -> sub X, (and Y 1)
  if (N1.getOpcode() == ISD::SIGN_EXTEND_INREG) {
    VTSDNode *TN = cast<VTSDNode>(N1.getOperand(1));
    if (TN->getVT() == MVT::i1) {
      SDValue ZExt = DAG.getNode(ISD::AND, DL, VT, N1.getOperand(0),
                                 DAG.getConstant(1, DL, VT));
      return DAG.getNode(ISD::SUB, DL, VT, N0, ZExt);
    }
  }

  // (add X, (addcarry Y, 0, Carry)) -> (addcarry X, Y, Carry)
  if (N1.getOpcode() == ISD::ADDCARRY && isNullConstant(N1.getOperand(1)) &&
      N1.getResNo() == 0)
    return DAG.getNode(ISD::ADDCARRY, DL, N1->getVTList(),
                       N0, N1.getOperand(0), N1.getOperand(2));

  // (add X, Carry) -> (addcarry X, 0, Carry)
  if (TLI.isOperationLegalOrCustom(ISD::ADDCARRY, VT))
    if (SDValue Carry = getAsCarry(TLI, N1))
      return DAG.getNode(ISD::ADDCARRY, DL,
                         DAG.getVTList(VT, Carry.getValueType()), N0,
                         DAG.getConstant(0, DL, VT), Carry);

  return SDValue();
}

SDValue DAGCombiner::visitADDC(SDNode *N) {
  SDValue N0 = N->getOperand(0);
  SDValue N1 = N->getOperand(1);
  EVT VT = N0.getValueType();
  SDLoc DL(N);

  // If the flag result is dead, turn this into an ADD.
  if (!N->hasAnyUseOfValue(1))
    return CombineTo(N, DAG.getNode(ISD::ADD, DL, VT, N0, N1),
                     DAG.getNode(ISD::CARRY_FALSE, DL, MVT::Glue));

  // canonicalize constant to RHS.
  ConstantSDNode *N0C = dyn_cast<ConstantSDNode>(N0);
  ConstantSDNode *N1C = dyn_cast<ConstantSDNode>(N1);
  if (N0C && !N1C)
    return DAG.getNode(ISD::ADDC, DL, N->getVTList(), N1, N0);

  // fold (addc x, 0) -> x + no carry out
  if (isNullConstant(N1))
    return CombineTo(N, N0, DAG.getNode(ISD::CARRY_FALSE,
                                        DL, MVT::Glue));

  // If it cannot overflow, transform into an add.
  if (DAG.computeOverflowKind(N0, N1) == SelectionDAG::OFK_Never)
    return CombineTo(N, DAG.getNode(ISD::ADD, DL, VT, N0, N1),
                     DAG.getNode(ISD::CARRY_FALSE, DL, MVT::Glue));

  return SDValue();
}

static SDValue flipBoolean(SDValue V, const SDLoc &DL, EVT VT,
                           SelectionDAG &DAG, const TargetLowering &TLI) {
  SDValue Cst;
  switch (TLI.getBooleanContents(VT)) {
  case TargetLowering::ZeroOrOneBooleanContent:
  case TargetLowering::UndefinedBooleanContent:
    Cst = DAG.getConstant(1, DL, VT);
    break;
  case TargetLowering::ZeroOrNegativeOneBooleanContent:
    Cst = DAG.getConstant(-1, DL, VT);
    break;
  }

  return DAG.getNode(ISD::XOR, DL, VT, V, Cst);
}

static bool isBooleanFlip(SDValue V, EVT VT, const TargetLowering &TLI) {
  if (V.getOpcode() != ISD::XOR) return false;
  ConstantSDNode *Const = dyn_cast<ConstantSDNode>(V.getOperand(1));
  if (!Const) return false;

  switch(TLI.getBooleanContents(VT)) {
    case TargetLowering::ZeroOrOneBooleanContent:
      return Const->isOne();
    case TargetLowering::ZeroOrNegativeOneBooleanContent:
      return Const->isAllOnesValue();
    case TargetLowering::UndefinedBooleanContent:
      return (Const->getAPIntValue() & 0x01) == 1;
  }
  llvm_unreachable("Unsupported boolean content");
}

SDValue DAGCombiner::visitUADDO(SDNode *N) {
  SDValue N0 = N->getOperand(0);
  SDValue N1 = N->getOperand(1);
  EVT VT = N0.getValueType();
  if (VT.isVector())
    return SDValue();

  EVT CarryVT = N->getValueType(1);
  SDLoc DL(N);

  // If the flag result is dead, turn this into an ADD.
  if (!N->hasAnyUseOfValue(1))
    return CombineTo(N, DAG.getNode(ISD::ADD, DL, VT, N0, N1),
                     DAG.getUNDEF(CarryVT));

  // canonicalize constant to RHS.
  ConstantSDNode *N0C = dyn_cast<ConstantSDNode>(N0);
  ConstantSDNode *N1C = dyn_cast<ConstantSDNode>(N1);
  if (N0C && !N1C)
    return DAG.getNode(ISD::UADDO, DL, N->getVTList(), N1, N0);

  // fold (uaddo x, 0) -> x + no carry out
  if (isNullConstant(N1))
    return CombineTo(N, N0, DAG.getConstant(0, DL, CarryVT));

  // If it cannot overflow, transform into an add.
  if (DAG.computeOverflowKind(N0, N1) == SelectionDAG::OFK_Never)
    return CombineTo(N, DAG.getNode(ISD::ADD, DL, VT, N0, N1),
                     DAG.getConstant(0, DL, CarryVT));

  // fold (uaddo (xor a, -1), 1) -> (usub 0, a) and flip carry.
  if (isBitwiseNot(N0) && isOneOrOneSplat(N1)) {
    SDValue Sub = DAG.getNode(ISD::USUBO, DL, N->getVTList(),
                              DAG.getConstant(0, DL, VT),
                              N0.getOperand(0));
    return CombineTo(N, Sub,
                     flipBoolean(Sub.getValue(1), DL, CarryVT, DAG, TLI));
  }

  if (SDValue Combined = visitUADDOLike(N0, N1, N))
    return Combined;

  if (SDValue Combined = visitUADDOLike(N1, N0, N))
    return Combined;

  return SDValue();
}

SDValue DAGCombiner::visitUADDOLike(SDValue N0, SDValue N1, SDNode *N) {
  auto VT = N0.getValueType();

  // (uaddo X, (addcarry Y, 0, Carry)) -> (addcarry X, Y, Carry)
  // If Y + 1 cannot overflow.
  if (N1.getOpcode() == ISD::ADDCARRY && isNullConstant(N1.getOperand(1))) {
    SDValue Y = N1.getOperand(0);
    SDValue One = DAG.getConstant(1, SDLoc(N), Y.getValueType());
    if (DAG.computeOverflowKind(Y, One) == SelectionDAG::OFK_Never)
      return DAG.getNode(ISD::ADDCARRY, SDLoc(N), N->getVTList(), N0, Y,
                         N1.getOperand(2));
  }

  // (uaddo X, Carry) -> (addcarry X, 0, Carry)
  if (TLI.isOperationLegalOrCustom(ISD::ADDCARRY, VT))
    if (SDValue Carry = getAsCarry(TLI, N1))
      return DAG.getNode(ISD::ADDCARRY, SDLoc(N), N->getVTList(), N0,
                         DAG.getConstant(0, SDLoc(N), VT), Carry);

  return SDValue();
}

SDValue DAGCombiner::visitADDE(SDNode *N) {
  SDValue N0 = N->getOperand(0);
  SDValue N1 = N->getOperand(1);
  SDValue CarryIn = N->getOperand(2);

  // canonicalize constant to RHS
  ConstantSDNode *N0C = dyn_cast<ConstantSDNode>(N0);
  ConstantSDNode *N1C = dyn_cast<ConstantSDNode>(N1);
  if (N0C && !N1C)
    return DAG.getNode(ISD::ADDE, SDLoc(N), N->getVTList(),
                       N1, N0, CarryIn);

  // fold (adde x, y, false) -> (addc x, y)
  if (CarryIn.getOpcode() == ISD::CARRY_FALSE)
    return DAG.getNode(ISD::ADDC, SDLoc(N), N->getVTList(), N0, N1);

  return SDValue();
}

SDValue DAGCombiner::visitADDCARRY(SDNode *N) {
  SDValue N0 = N->getOperand(0);
  SDValue N1 = N->getOperand(1);
  SDValue CarryIn = N->getOperand(2);
  SDLoc DL(N);

  // canonicalize constant to RHS
  ConstantSDNode *N0C = dyn_cast<ConstantSDNode>(N0);
  ConstantSDNode *N1C = dyn_cast<ConstantSDNode>(N1);
  if (N0C && !N1C)
    return DAG.getNode(ISD::ADDCARRY, DL, N->getVTList(), N1, N0, CarryIn);

  // fold (addcarry x, y, false) -> (uaddo x, y)
  if (isNullConstant(CarryIn)) {
    if (!LegalOperations ||
        TLI.isOperationLegalOrCustom(ISD::UADDO, N->getValueType(0)))
      return DAG.getNode(ISD::UADDO, DL, N->getVTList(), N0, N1);
  }

  EVT CarryVT = CarryIn.getValueType();

  // fold (addcarry 0, 0, X) -> (and (ext/trunc X), 1) and no carry.
  if (isNullConstant(N0) && isNullConstant(N1)) {
    EVT VT = N0.getValueType();
    SDValue CarryExt = DAG.getBoolExtOrTrunc(CarryIn, DL, VT, CarryVT);
    AddToWorklist(CarryExt.getNode());
    return CombineTo(N, DAG.getNode(ISD::AND, DL, VT, CarryExt,
                                    DAG.getConstant(1, DL, VT)),
                     DAG.getConstant(0, DL, CarryVT));
  }

  // fold (addcarry (xor a, -1), 0, !b) -> (subcarry 0, a, b) and flip carry.
  if (isBitwiseNot(N0) && isNullConstant(N1) &&
      isBooleanFlip(CarryIn, CarryVT, TLI)) {
    SDValue Sub = DAG.getNode(ISD::SUBCARRY, DL, N->getVTList(),
                              DAG.getConstant(0, DL, N0.getValueType()),
                              N0.getOperand(0), CarryIn.getOperand(0));
    return CombineTo(N, Sub,
                     flipBoolean(Sub.getValue(1), DL, CarryVT, DAG, TLI));
  }

  if (SDValue Combined = visitADDCARRYLike(N0, N1, CarryIn, N))
    return Combined;

  if (SDValue Combined = visitADDCARRYLike(N1, N0, CarryIn, N))
    return Combined;

  return SDValue();
}

SDValue DAGCombiner::visitADDCARRYLike(SDValue N0, SDValue N1, SDValue CarryIn,
                                       SDNode *N) {
  // Iff the flag result is dead:
  // (addcarry (add|uaddo X, Y), 0, Carry) -> (addcarry X, Y, Carry)
  if ((N0.getOpcode() == ISD::ADD ||
       (N0.getOpcode() == ISD::UADDO && N0.getResNo() == 0)) &&
      isNullConstant(N1) && !N->hasAnyUseOfValue(1))
    return DAG.getNode(ISD::ADDCARRY, SDLoc(N), N->getVTList(),
                       N0.getOperand(0), N0.getOperand(1), CarryIn);

  /**
   * When one of the addcarry argument is itself a carry, we may be facing
   * a diamond carry propagation. In which case we try to transform the DAG
   * to ensure linear carry propagation if that is possible.
   *
   * We are trying to get:
   *   (addcarry X, 0, (addcarry A, B, Z):Carry)
   */
  if (auto Y = getAsCarry(TLI, N1)) {
    /**
     *            (uaddo A, B)
     *             /       \
     *          Carry      Sum
     *            |          \
     *            | (addcarry *, 0, Z)
     *            |       /
     *             \   Carry
     *              |   /
     * (addcarry X, *, *)
     */
    if (Y.getOpcode() == ISD::UADDO &&
        CarryIn.getResNo() == 1 &&
        CarryIn.getOpcode() == ISD::ADDCARRY &&
        isNullConstant(CarryIn.getOperand(1)) &&
        CarryIn.getOperand(0) == Y.getValue(0)) {
      auto NewY = DAG.getNode(ISD::ADDCARRY, SDLoc(N), Y->getVTList(),
                              Y.getOperand(0), Y.getOperand(1),
                              CarryIn.getOperand(2));
      AddToWorklist(NewY.getNode());
      return DAG.getNode(ISD::ADDCARRY, SDLoc(N), N->getVTList(), N0,
                         DAG.getConstant(0, SDLoc(N), N0.getValueType()),
                         NewY.getValue(1));
    }
  }

  return SDValue();
}

// Since it may not be valid to emit a fold to zero for vector initializers
// check if we can before folding.
static SDValue tryFoldToZero(const SDLoc &DL, const TargetLowering &TLI, EVT VT,
                             SelectionDAG &DAG, bool LegalOperations) {
  if (!VT.isVector())
    return DAG.getConstant(0, DL, VT);
  if (!LegalOperations || TLI.isOperationLegal(ISD::BUILD_VECTOR, VT))
    return DAG.getConstant(0, DL, VT);
  return SDValue();
}

SDValue DAGCombiner::visitSUB(SDNode *N) {
  SDValue N0 = N->getOperand(0);
  SDValue N1 = N->getOperand(1);
  EVT VT = N0.getValueType();
  SDLoc DL(N);

  // fold vector ops
  if (VT.isVector()) {
    if (SDValue FoldedVOp = SimplifyVBinOp(N))
      return FoldedVOp;

    // fold (sub x, 0) -> x, vector edition
    if (ISD::isBuildVectorAllZeros(N1.getNode()))
      return N0;
  }

  // fold (sub x, x) -> 0
  // FIXME: Refactor this and xor and other similar operations together.
  if (N0 == N1)
    return tryFoldToZero(DL, TLI, VT, DAG, LegalOperations);
  if (DAG.isConstantIntBuildVectorOrConstantInt(N0) &&
      DAG.isConstantIntBuildVectorOrConstantInt(N1)) {
    // fold (sub c1, c2) -> c1-c2
    return DAG.FoldConstantArithmetic(ISD::SUB, DL, VT, N0.getNode(),
                                      N1.getNode());
  }

  if (SDValue NewSel = foldBinOpIntoSelect(N))
    return NewSel;

  ConstantSDNode *N1C = getAsNonOpaqueConstant(N1);

  // fold (sub x, c) -> (add x, -c)
  if (N1C) {
    return DAG.getNode(ISD::ADD, DL, VT, N0,
                       DAG.getConstant(-N1C->getAPIntValue(), DL, VT));
  }

  if (isNullOrNullSplat(N0)) {
    unsigned BitWidth = VT.getScalarSizeInBits();
    // Right-shifting everything out but the sign bit followed by negation is
    // the same as flipping arithmetic/logical shift type without the negation:
    // -(X >>u 31) -> (X >>s 31)
    // -(X >>s 31) -> (X >>u 31)
    if (N1->getOpcode() == ISD::SRA || N1->getOpcode() == ISD::SRL) {
      ConstantSDNode *ShiftAmt = isConstOrConstSplat(N1.getOperand(1));
      if (ShiftAmt && ShiftAmt->getZExtValue() == BitWidth - 1) {
        auto NewSh = N1->getOpcode() == ISD::SRA ? ISD::SRL : ISD::SRA;
        if (!LegalOperations || TLI.isOperationLegal(NewSh, VT))
          return DAG.getNode(NewSh, DL, VT, N1.getOperand(0), N1.getOperand(1));
      }
    }

    // 0 - X --> 0 if the sub is NUW.
    if (N->getFlags().hasNoUnsignedWrap())
      return N0;

    if (DAG.MaskedValueIsZero(N1, ~APInt::getSignMask(BitWidth))) {
      // N1 is either 0 or the minimum signed value. If the sub is NSW, then
      // N1 must be 0 because negating the minimum signed value is undefined.
      if (N->getFlags().hasNoSignedWrap())
        return N0;

      // 0 - X --> X if X is 0 or the minimum signed value.
      return N1;
    }
  }

  // Canonicalize (sub -1, x) -> ~x, i.e. (xor x, -1)
  if (isAllOnesOrAllOnesSplat(N0))
    return DAG.getNode(ISD::XOR, DL, VT, N1, N0);

  // fold (A - (0-B)) -> A+B
  if (N1.getOpcode() == ISD::SUB && isNullOrNullSplat(N1.getOperand(0)))
    return DAG.getNode(ISD::ADD, DL, VT, N0, N1.getOperand(1));

  // fold A-(A-B) -> B
  if (N1.getOpcode() == ISD::SUB && N0 == N1.getOperand(0))
    return N1.getOperand(1);

  // fold (A+B)-A -> B
  if (N0.getOpcode() == ISD::ADD && N0.getOperand(0) == N1)
    return N0.getOperand(1);

  // fold (A+B)-B -> A
  if (N0.getOpcode() == ISD::ADD && N0.getOperand(1) == N1)
    return N0.getOperand(0);

  // fold C2-(A+C1) -> (C2-C1)-A
  if (N1.getOpcode() == ISD::ADD) {
    SDValue N11 = N1.getOperand(1);
    if (isConstantOrConstantVector(N0, /* NoOpaques */ true) &&
        isConstantOrConstantVector(N11, /* NoOpaques */ true)) {
      SDValue NewC = DAG.getNode(ISD::SUB, DL, VT, N0, N11);
      return DAG.getNode(ISD::SUB, DL, VT, NewC, N1.getOperand(0));
    }
  }

  // fold ((A+(B+or-C))-B) -> A+or-C
  if (N0.getOpcode() == ISD::ADD &&
      (N0.getOperand(1).getOpcode() == ISD::SUB ||
       N0.getOperand(1).getOpcode() == ISD::ADD) &&
      N0.getOperand(1).getOperand(0) == N1)
    return DAG.getNode(N0.getOperand(1).getOpcode(), DL, VT, N0.getOperand(0),
                       N0.getOperand(1).getOperand(1));

  // fold ((A+(C+B))-B) -> A+C
  if (N0.getOpcode() == ISD::ADD && N0.getOperand(1).getOpcode() == ISD::ADD &&
      N0.getOperand(1).getOperand(1) == N1)
    return DAG.getNode(ISD::ADD, DL, VT, N0.getOperand(0),
                       N0.getOperand(1).getOperand(0));

  // fold ((A-(B-C))-C) -> A-B
  if (N0.getOpcode() == ISD::SUB && N0.getOperand(1).getOpcode() == ISD::SUB &&
      N0.getOperand(1).getOperand(1) == N1)
    return DAG.getNode(ISD::SUB, DL, VT, N0.getOperand(0),
                       N0.getOperand(1).getOperand(0));

  // fold (A-(B-C)) -> A+(C-B)
  if (N1.getOpcode() == ISD::SUB && N1.hasOneUse())
    return DAG.getNode(ISD::ADD, DL, VT, N0,
                       DAG.getNode(ISD::SUB, DL, VT, N1.getOperand(1),
                                   N1.getOperand(0)));

  // fold (X - (-Y * Z)) -> (X + (Y * Z))
  if (N1.getOpcode() == ISD::MUL && N1.hasOneUse()) {
    if (N1.getOperand(0).getOpcode() == ISD::SUB &&
        isNullOrNullSplat(N1.getOperand(0).getOperand(0))) {
      SDValue Mul = DAG.getNode(ISD::MUL, DL, VT,
                                N1.getOperand(0).getOperand(1),
                                N1.getOperand(1));
      return DAG.getNode(ISD::ADD, DL, VT, N0, Mul);
    }
    if (N1.getOperand(1).getOpcode() == ISD::SUB &&
        isNullOrNullSplat(N1.getOperand(1).getOperand(0))) {
      SDValue Mul = DAG.getNode(ISD::MUL, DL, VT,
                                N1.getOperand(0),
                                N1.getOperand(1).getOperand(1));
      return DAG.getNode(ISD::ADD, DL, VT, N0, Mul);
    }
  }

  // If either operand of a sub is undef, the result is undef
  if (N0.isUndef())
    return N0;
  if (N1.isUndef())
    return N1;

  if (SDValue V = foldAddSubBoolOfMaskedVal(N, DAG))
    return V;

  if (SDValue V = foldAddSubOfSignBit(N, DAG))
    return V;

  // fold Y = sra (X, size(X)-1); sub (xor (X, Y), Y) -> (abs X)
  if (TLI.isOperationLegalOrCustom(ISD::ABS, VT)) {
    if (N0.getOpcode() == ISD::XOR && N1.getOpcode() == ISD::SRA) {
      SDValue X0 = N0.getOperand(0), X1 = N0.getOperand(1);
      SDValue S0 = N1.getOperand(0);
      if ((X0 == S0 && X1 == N1) || (X0 == N1 && X1 == S0)) {
        unsigned OpSizeInBits = VT.getScalarSizeInBits();
        if (ConstantSDNode *C = isConstOrConstSplat(N1.getOperand(1)))
          if (C->getAPIntValue() == (OpSizeInBits - 1))
            return DAG.getNode(ISD::ABS, SDLoc(N), VT, S0);
      }
    }
  }

  // If the relocation model supports it, consider symbol offsets.
  if (GlobalAddressSDNode *GA = dyn_cast<GlobalAddressSDNode>(N0))
    if (!LegalOperations && TLI.isOffsetFoldingLegal(GA)) {
      // fold (sub Sym, c) -> Sym-c
      if (N1C && GA->getOpcode() == ISD::GlobalAddress)
        return DAG.getGlobalAddress(GA->getGlobal(), SDLoc(N1C), VT,
                                    GA->getOffset() -
                                        (uint64_t)N1C->getSExtValue());
      // fold (sub Sym+c1, Sym+c2) -> c1-c2
      if (GlobalAddressSDNode *GB = dyn_cast<GlobalAddressSDNode>(N1))
        if (GA->getGlobal() == GB->getGlobal())
          return DAG.getConstant((uint64_t)GA->getOffset() - GB->getOffset(),
                                 DL, VT);
    }

  // sub X, (sextinreg Y i1) -> add X, (and Y 1)
  if (N1.getOpcode() == ISD::SIGN_EXTEND_INREG) {
    VTSDNode *TN = cast<VTSDNode>(N1.getOperand(1));
    if (TN->getVT() == MVT::i1) {
      SDValue ZExt = DAG.getNode(ISD::AND, DL, VT, N1.getOperand(0),
                                 DAG.getConstant(1, DL, VT));
      return DAG.getNode(ISD::ADD, DL, VT, N0, ZExt);
    }
  }

  // Prefer an add for more folding potential and possibly better codegen:
  // sub N0, (lshr N10, width-1) --> add N0, (ashr N10, width-1)
  if (!LegalOperations && N1.getOpcode() == ISD::SRL && N1.hasOneUse()) {
    SDValue ShAmt = N1.getOperand(1);
    ConstantSDNode *ShAmtC = isConstOrConstSplat(ShAmt);
    if (ShAmtC && ShAmtC->getZExtValue() == N1.getScalarValueSizeInBits() - 1) {
      SDValue SRA = DAG.getNode(ISD::SRA, DL, VT, N1.getOperand(0), ShAmt);
      return DAG.getNode(ISD::ADD, DL, VT, N0, SRA);
    }
  }

  return SDValue();
}

SDValue DAGCombiner::visitSUBSAT(SDNode *N) {
  SDValue N0 = N->getOperand(0);
  SDValue N1 = N->getOperand(1);
  EVT VT = N0.getValueType();
  SDLoc DL(N);

  // fold vector ops
  if (VT.isVector()) {
    // TODO SimplifyVBinOp

    // fold (sub_sat x, 0) -> x, vector edition
    if (ISD::isBuildVectorAllZeros(N1.getNode()))
      return N0;
  }

  // fold (sub_sat x, undef) -> 0
  if (N0.isUndef() || N1.isUndef())
    return DAG.getConstant(0, DL, VT);

  // fold (sub_sat x, x) -> 0
  if (N0 == N1)
    return DAG.getConstant(0, DL, VT);

  if (DAG.isConstantIntBuildVectorOrConstantInt(N0) &&
      DAG.isConstantIntBuildVectorOrConstantInt(N1)) {
    // fold (sub_sat c1, c2) -> c3
    return DAG.FoldConstantArithmetic(N->getOpcode(), DL, VT, N0.getNode(),
                                      N1.getNode());
  }

  // fold (sub_sat x, 0) -> x
  if (isNullConstant(N1))
    return N0;

  return SDValue();
}

SDValue DAGCombiner::visitSUBC(SDNode *N) {
  SDValue N0 = N->getOperand(0);
  SDValue N1 = N->getOperand(1);
  EVT VT = N0.getValueType();
  SDLoc DL(N);

  // If the flag result is dead, turn this into an SUB.
  if (!N->hasAnyUseOfValue(1))
    return CombineTo(N, DAG.getNode(ISD::SUB, DL, VT, N0, N1),
                     DAG.getNode(ISD::CARRY_FALSE, DL, MVT::Glue));

  // fold (subc x, x) -> 0 + no borrow
  if (N0 == N1)
    return CombineTo(N, DAG.getConstant(0, DL, VT),
                     DAG.getNode(ISD::CARRY_FALSE, DL, MVT::Glue));

  // fold (subc x, 0) -> x + no borrow
  if (isNullConstant(N1))
    return CombineTo(N, N0, DAG.getNode(ISD::CARRY_FALSE, DL, MVT::Glue));

  // Canonicalize (sub -1, x) -> ~x, i.e. (xor x, -1) + no borrow
  if (isAllOnesConstant(N0))
    return CombineTo(N, DAG.getNode(ISD::XOR, DL, VT, N1, N0),
                     DAG.getNode(ISD::CARRY_FALSE, DL, MVT::Glue));

  return SDValue();
}

SDValue DAGCombiner::visitUSUBO(SDNode *N) {
  SDValue N0 = N->getOperand(0);
  SDValue N1 = N->getOperand(1);
  EVT VT = N0.getValueType();
  if (VT.isVector())
    return SDValue();

  EVT CarryVT = N->getValueType(1);
  SDLoc DL(N);

  // If the flag result is dead, turn this into an SUB.
  if (!N->hasAnyUseOfValue(1))
    return CombineTo(N, DAG.getNode(ISD::SUB, DL, VT, N0, N1),
                     DAG.getUNDEF(CarryVT));

  // fold (usubo x, x) -> 0 + no borrow
  if (N0 == N1)
    return CombineTo(N, DAG.getConstant(0, DL, VT),
                     DAG.getConstant(0, DL, CarryVT));

  // fold (usubo x, 0) -> x + no borrow
  if (isNullConstant(N1))
    return CombineTo(N, N0, DAG.getConstant(0, DL, CarryVT));

  // Canonicalize (usubo -1, x) -> ~x, i.e. (xor x, -1) + no borrow
  if (isAllOnesConstant(N0))
    return CombineTo(N, DAG.getNode(ISD::XOR, DL, VT, N1, N0),
                     DAG.getConstant(0, DL, CarryVT));

  return SDValue();
}

SDValue DAGCombiner::visitSUBE(SDNode *N) {
  SDValue N0 = N->getOperand(0);
  SDValue N1 = N->getOperand(1);
  SDValue CarryIn = N->getOperand(2);

  // fold (sube x, y, false) -> (subc x, y)
  if (CarryIn.getOpcode() == ISD::CARRY_FALSE)
    return DAG.getNode(ISD::SUBC, SDLoc(N), N->getVTList(), N0, N1);

  return SDValue();
}

SDValue DAGCombiner::visitSUBCARRY(SDNode *N) {
  SDValue N0 = N->getOperand(0);
  SDValue N1 = N->getOperand(1);
  SDValue CarryIn = N->getOperand(2);

  // fold (subcarry x, y, false) -> (usubo x, y)
  if (isNullConstant(CarryIn)) {
    if (!LegalOperations ||
        TLI.isOperationLegalOrCustom(ISD::USUBO, N->getValueType(0)))
      return DAG.getNode(ISD::USUBO, SDLoc(N), N->getVTList(), N0, N1);
  }

  return SDValue();
}

SDValue DAGCombiner::visitMUL(SDNode *N) {
  SDValue N0 = N->getOperand(0);
  SDValue N1 = N->getOperand(1);
  EVT VT = N0.getValueType();

  // fold (mul x, undef) -> 0
  if (N0.isUndef() || N1.isUndef())
    return DAG.getConstant(0, SDLoc(N), VT);

  bool N0IsConst = false;
  bool N1IsConst = false;
  bool N1IsOpaqueConst = false;
  bool N0IsOpaqueConst = false;
  APInt ConstValue0, ConstValue1;
  // fold vector ops
  if (VT.isVector()) {
    if (SDValue FoldedVOp = SimplifyVBinOp(N))
      return FoldedVOp;

    N0IsConst = ISD::isConstantSplatVector(N0.getNode(), ConstValue0);
    N1IsConst = ISD::isConstantSplatVector(N1.getNode(), ConstValue1);
    assert((!N0IsConst ||
            ConstValue0.getBitWidth() == VT.getScalarSizeInBits()) &&
           "Splat APInt should be element width");
    assert((!N1IsConst ||
            ConstValue1.getBitWidth() == VT.getScalarSizeInBits()) &&
           "Splat APInt should be element width");
  } else {
    N0IsConst = isa<ConstantSDNode>(N0);
    if (N0IsConst) {
      ConstValue0 = cast<ConstantSDNode>(N0)->getAPIntValue();
      N0IsOpaqueConst = cast<ConstantSDNode>(N0)->isOpaque();
    }
    N1IsConst = isa<ConstantSDNode>(N1);
    if (N1IsConst) {
      ConstValue1 = cast<ConstantSDNode>(N1)->getAPIntValue();
      N1IsOpaqueConst = cast<ConstantSDNode>(N1)->isOpaque();
    }
  }

  // fold (mul c1, c2) -> c1*c2
  if (N0IsConst && N1IsConst && !N0IsOpaqueConst && !N1IsOpaqueConst)
    return DAG.FoldConstantArithmetic(ISD::MUL, SDLoc(N), VT,
                                      N0.getNode(), N1.getNode());

  // canonicalize constant to RHS (vector doesn't have to splat)
  if (DAG.isConstantIntBuildVectorOrConstantInt(N0) &&
     !DAG.isConstantIntBuildVectorOrConstantInt(N1))
    return DAG.getNode(ISD::MUL, SDLoc(N), VT, N1, N0);
  // fold (mul x, 0) -> 0
  if (N1IsConst && ConstValue1.isNullValue())
    return N1;
  // fold (mul x, 1) -> x
  if (N1IsConst && ConstValue1.isOneValue())
    return N0;

  if (SDValue NewSel = foldBinOpIntoSelect(N))
    return NewSel;

  // fold (mul x, -1) -> 0-x
  if (N1IsConst && ConstValue1.isAllOnesValue()) {
    SDLoc DL(N);
    return DAG.getNode(ISD::SUB, DL, VT,
                       DAG.getConstant(0, DL, VT), N0);
  }
  // fold (mul x, (1 << c)) -> x << c
  if (isConstantOrConstantVector(N1, /*NoOpaques*/ true) &&
      DAG.isKnownToBeAPowerOfTwo(N1) &&
      (!VT.isVector() || Level <= AfterLegalizeVectorOps)) {
    SDLoc DL(N);
    SDValue LogBase2 = BuildLogBase2(N1, DL);
    EVT ShiftVT = getShiftAmountTy(N0.getValueType());
    SDValue Trunc = DAG.getZExtOrTrunc(LogBase2, DL, ShiftVT);
    return DAG.getNode(ISD::SHL, DL, VT, N0, Trunc);
  }
  // fold (mul x, -(1 << c)) -> -(x << c) or (-x) << c
  if (N1IsConst && !N1IsOpaqueConst && (-ConstValue1).isPowerOf2()) {
    unsigned Log2Val = (-ConstValue1).logBase2();
    SDLoc DL(N);
    // FIXME: If the input is something that is easily negated (e.g. a
    // single-use add), we should put the negate there.
    return DAG.getNode(ISD::SUB, DL, VT,
                       DAG.getConstant(0, DL, VT),
                       DAG.getNode(ISD::SHL, DL, VT, N0,
                            DAG.getConstant(Log2Val, DL,
                                      getShiftAmountTy(N0.getValueType()))));
  }

  // Try to transform multiply-by-(power-of-2 +/- 1) into shift and add/sub.
  // mul x, (2^N + 1) --> add (shl x, N), x
  // mul x, (2^N - 1) --> sub (shl x, N), x
  // Examples: x * 33 --> (x << 5) + x
  //           x * 15 --> (x << 4) - x
  //           x * -33 --> -((x << 5) + x)
  //           x * -15 --> -((x << 4) - x) ; this reduces --> x - (x << 4)
  if (N1IsConst && TLI.decomposeMulByConstant(VT, N1)) {
    // TODO: We could handle more general decomposition of any constant by
    //       having the target set a limit on number of ops and making a
    //       callback to determine that sequence (similar to sqrt expansion).
    unsigned MathOp = ISD::DELETED_NODE;
    APInt MulC = ConstValue1.abs();
    if ((MulC - 1).isPowerOf2())
      MathOp = ISD::ADD;
    else if ((MulC + 1).isPowerOf2())
      MathOp = ISD::SUB;

    if (MathOp != ISD::DELETED_NODE) {
      unsigned ShAmt = MathOp == ISD::ADD ? (MulC - 1).logBase2()
                                          : (MulC + 1).logBase2();
      assert(ShAmt > 0 && ShAmt < VT.getScalarSizeInBits() &&
             "Not expecting multiply-by-constant that could have simplified");
      SDLoc DL(N);
      SDValue Shl = DAG.getNode(ISD::SHL, DL, VT, N0,
                                DAG.getConstant(ShAmt, DL, VT));
      SDValue R = DAG.getNode(MathOp, DL, VT, Shl, N0);
      if (ConstValue1.isNegative())
        R = DAG.getNode(ISD::SUB, DL, VT, DAG.getConstant(0, DL, VT), R);
      return R;
    }
  }

  // (mul (shl X, c1), c2) -> (mul X, c2 << c1)
  if (N0.getOpcode() == ISD::SHL &&
      isConstantOrConstantVector(N1, /* NoOpaques */ true) &&
      isConstantOrConstantVector(N0.getOperand(1), /* NoOpaques */ true)) {
    SDValue C3 = DAG.getNode(ISD::SHL, SDLoc(N), VT, N1, N0.getOperand(1));
    if (isConstantOrConstantVector(C3))
      return DAG.getNode(ISD::MUL, SDLoc(N), VT, N0.getOperand(0), C3);
  }

  // Change (mul (shl X, C), Y) -> (shl (mul X, Y), C) when the shift has one
  // use.
  {
    SDValue Sh(nullptr, 0), Y(nullptr, 0);

    // Check for both (mul (shl X, C), Y)  and  (mul Y, (shl X, C)).
    if (N0.getOpcode() == ISD::SHL &&
        isConstantOrConstantVector(N0.getOperand(1)) &&
        N0.getNode()->hasOneUse()) {
      Sh = N0; Y = N1;
    } else if (N1.getOpcode() == ISD::SHL &&
               isConstantOrConstantVector(N1.getOperand(1)) &&
               N1.getNode()->hasOneUse()) {
      Sh = N1; Y = N0;
    }

    if (Sh.getNode()) {
      SDValue Mul = DAG.getNode(ISD::MUL, SDLoc(N), VT, Sh.getOperand(0), Y);
      return DAG.getNode(ISD::SHL, SDLoc(N), VT, Mul, Sh.getOperand(1));
    }
  }

  // fold (mul (add x, c1), c2) -> (add (mul x, c2), c1*c2)
  if (DAG.isConstantIntBuildVectorOrConstantInt(N1) &&
      N0.getOpcode() == ISD::ADD &&
      DAG.isConstantIntBuildVectorOrConstantInt(N0.getOperand(1)) &&
      isMulAddWithConstProfitable(N, N0, N1))
      return DAG.getNode(ISD::ADD, SDLoc(N), VT,
                         DAG.getNode(ISD::MUL, SDLoc(N0), VT,
                                     N0.getOperand(0), N1),
                         DAG.getNode(ISD::MUL, SDLoc(N1), VT,
                                     N0.getOperand(1), N1));

  // reassociate mul
  if (SDValue RMUL = ReassociateOps(ISD::MUL, SDLoc(N), N0, N1, N->getFlags()))
    return RMUL;

  return SDValue();
}

/// Return true if divmod libcall is available.
static bool isDivRemLibcallAvailable(SDNode *Node, bool isSigned,
                                     const TargetLowering &TLI) {
  RTLIB::Libcall LC;
  EVT NodeType = Node->getValueType(0);
  if (!NodeType.isSimple())
    return false;
  switch (NodeType.getSimpleVT().SimpleTy) {
  default: return false; // No libcall for vector types.
  case MVT::i8:   LC= isSigned ? RTLIB::SDIVREM_I8  : RTLIB::UDIVREM_I8;  break;
  case MVT::i16:  LC= isSigned ? RTLIB::SDIVREM_I16 : RTLIB::UDIVREM_I16; break;
  case MVT::i32:  LC= isSigned ? RTLIB::SDIVREM_I32 : RTLIB::UDIVREM_I32; break;
  case MVT::i64:  LC= isSigned ? RTLIB::SDIVREM_I64 : RTLIB::UDIVREM_I64; break;
  case MVT::i128: LC= isSigned ? RTLIB::SDIVREM_I128:RTLIB::UDIVREM_I128; break;
  }

  return TLI.getLibcallName(LC) != nullptr;
}

/// Issue divrem if both quotient and remainder are needed.
SDValue DAGCombiner::useDivRem(SDNode *Node) {
  if (Node->use_empty())
    return SDValue(); // This is a dead node, leave it alone.

  unsigned Opcode = Node->getOpcode();
  bool isSigned = (Opcode == ISD::SDIV) || (Opcode == ISD::SREM);
  unsigned DivRemOpc = isSigned ? ISD::SDIVREM : ISD::UDIVREM;

  // DivMod lib calls can still work on non-legal types if using lib-calls.
  EVT VT = Node->getValueType(0);
  if (VT.isVector() || !VT.isInteger())
    return SDValue();

  if (!TLI.isTypeLegal(VT) && !TLI.isOperationCustom(DivRemOpc, VT))
    return SDValue();

  // If DIVREM is going to get expanded into a libcall,
  // but there is no libcall available, then don't combine.
  if (!TLI.isOperationLegalOrCustom(DivRemOpc, VT) &&
      !isDivRemLibcallAvailable(Node, isSigned, TLI))
    return SDValue();

  // If div is legal, it's better to do the normal expansion
  unsigned OtherOpcode = 0;
  if ((Opcode == ISD::SDIV) || (Opcode == ISD::UDIV)) {
    OtherOpcode = isSigned ? ISD::SREM : ISD::UREM;
    if (TLI.isOperationLegalOrCustom(Opcode, VT))
      return SDValue();
  } else {
    OtherOpcode = isSigned ? ISD::SDIV : ISD::UDIV;
    if (TLI.isOperationLegalOrCustom(OtherOpcode, VT))
      return SDValue();
  }

  SDValue Op0 = Node->getOperand(0);
  SDValue Op1 = Node->getOperand(1);
  SDValue combined;
  for (SDNode::use_iterator UI = Op0.getNode()->use_begin(),
         UE = Op0.getNode()->use_end(); UI != UE; ++UI) {
    SDNode *User = *UI;
    if (User == Node || User->getOpcode() == ISD::DELETED_NODE ||
        User->use_empty())
      continue;
    // Convert the other matching node(s), too;
    // otherwise, the DIVREM may get target-legalized into something
    // target-specific that we won't be able to recognize.
    unsigned UserOpc = User->getOpcode();
    if ((UserOpc == Opcode || UserOpc == OtherOpcode || UserOpc == DivRemOpc) &&
        User->getOperand(0) == Op0 &&
        User->getOperand(1) == Op1) {
      if (!combined) {
        if (UserOpc == OtherOpcode) {
          SDVTList VTs = DAG.getVTList(VT, VT);
          combined = DAG.getNode(DivRemOpc, SDLoc(Node), VTs, Op0, Op1);
        } else if (UserOpc == DivRemOpc) {
          combined = SDValue(User, 0);
        } else {
          assert(UserOpc == Opcode);
          continue;
        }
      }
      if (UserOpc == ISD::SDIV || UserOpc == ISD::UDIV)
        CombineTo(User, combined);
      else if (UserOpc == ISD::SREM || UserOpc == ISD::UREM)
        CombineTo(User, combined.getValue(1));
    }
  }
  return combined;
}

static SDValue simplifyDivRem(SDNode *N, SelectionDAG &DAG) {
  SDValue N0 = N->getOperand(0);
  SDValue N1 = N->getOperand(1);
  EVT VT = N->getValueType(0);
  SDLoc DL(N);

  unsigned Opc = N->getOpcode();
  bool IsDiv = (ISD::SDIV == Opc) || (ISD::UDIV == Opc);
  ConstantSDNode *N1C = isConstOrConstSplat(N1);

  // X / undef -> undef
  // X % undef -> undef
  // X / 0 -> undef
  // X % 0 -> undef
  // NOTE: This includes vectors where any divisor element is zero/undef.
  if (DAG.isUndef(Opc, {N0, N1}))
    return DAG.getUNDEF(VT);

  // undef / X -> 0
  // undef % X -> 0
  if (N0.isUndef())
    return DAG.getConstant(0, DL, VT);

  // 0 / X -> 0
  // 0 % X -> 0
  ConstantSDNode *N0C = isConstOrConstSplat(N0);
  if (N0C && N0C->isNullValue())
    return N0;

  // X / X -> 1
  // X % X -> 0
  if (N0 == N1)
    return DAG.getConstant(IsDiv ? 1 : 0, DL, VT);

  // X / 1 -> X
  // X % 1 -> 0
  // If this is a boolean op (single-bit element type), we can't have
  // division-by-zero or remainder-by-zero, so assume the divisor is 1.
  // TODO: Similarly, if we're zero-extending a boolean divisor, then assume
  // it's a 1.
  if ((N1C && N1C->isOne()) || (VT.getScalarType() == MVT::i1))
    return IsDiv ? N0 : DAG.getConstant(0, DL, VT);

  return SDValue();
}

SDValue DAGCombiner::visitSDIV(SDNode *N) {
  SDValue N0 = N->getOperand(0);
  SDValue N1 = N->getOperand(1);
  EVT VT = N->getValueType(0);
  EVT CCVT = getSetCCResultType(VT);

  // fold vector ops
  if (VT.isVector())
    if (SDValue FoldedVOp = SimplifyVBinOp(N))
      return FoldedVOp;

  SDLoc DL(N);

  // fold (sdiv c1, c2) -> c1/c2
  ConstantSDNode *N0C = isConstOrConstSplat(N0);
  ConstantSDNode *N1C = isConstOrConstSplat(N1);
  if (N0C && N1C && !N0C->isOpaque() && !N1C->isOpaque())
    return DAG.FoldConstantArithmetic(ISD::SDIV, DL, VT, N0C, N1C);
  // fold (sdiv X, -1) -> 0-X
  if (N1C && N1C->isAllOnesValue())
    return DAG.getNode(ISD::SUB, DL, VT, DAG.getConstant(0, DL, VT), N0);
  // fold (sdiv X, MIN_SIGNED) -> select(X == MIN_SIGNED, 1, 0)
  if (N1C && N1C->getAPIntValue().isMinSignedValue())
    return DAG.getSelect(DL, VT, DAG.getSetCC(DL, CCVT, N0, N1, ISD::SETEQ),
                         DAG.getConstant(1, DL, VT),
                         DAG.getConstant(0, DL, VT));

  if (SDValue V = simplifyDivRem(N, DAG))
    return V;

  if (SDValue NewSel = foldBinOpIntoSelect(N))
    return NewSel;

  // If we know the sign bits of both operands are zero, strength reduce to a
  // udiv instead.  Handles (X&15) /s 4 -> X&15 >> 2
  if (DAG.SignBitIsZero(N1) && DAG.SignBitIsZero(N0))
    return DAG.getNode(ISD::UDIV, DL, N1.getValueType(), N0, N1);

  if (SDValue V = visitSDIVLike(N0, N1, N)) {
    // If the corresponding remainder node exists, update its users with
    // (Dividend - (Quotient * Divisor).
    if (SDNode *RemNode = DAG.getNodeIfExists(ISD::SREM, N->getVTList(),
                                              { N0, N1 })) {
      SDValue Mul = DAG.getNode(ISD::MUL, DL, VT, V, N1);
      SDValue Sub = DAG.getNode(ISD::SUB, DL, VT, N0, Mul);
      AddToWorklist(Mul.getNode());
      AddToWorklist(Sub.getNode());
      CombineTo(RemNode, Sub);
    }
    return V;
  }

  // sdiv, srem -> sdivrem
  // If the divisor is constant, then return DIVREM only if isIntDivCheap() is
  // true.  Otherwise, we break the simplification logic in visitREM().
  AttributeList Attr = DAG.getMachineFunction().getFunction().getAttributes();
  if (!N1C || TLI.isIntDivCheap(N->getValueType(0), Attr))
    if (SDValue DivRem = useDivRem(N))
        return DivRem;

  return SDValue();
}

SDValue DAGCombiner::visitSDIVLike(SDValue N0, SDValue N1, SDNode *N) {
  SDLoc DL(N);
  EVT VT = N->getValueType(0);
  EVT CCVT = getSetCCResultType(VT);
  unsigned BitWidth = VT.getScalarSizeInBits();

  // Helper for determining whether a value is a power-2 constant scalar or a
  // vector of such elements.
  auto IsPowerOfTwo = [](ConstantSDNode *C) {
    if (C->isNullValue() || C->isOpaque())
      return false;
    if (C->getAPIntValue().isPowerOf2())
      return true;
    if ((-C->getAPIntValue()).isPowerOf2())
      return true;
    return false;
  };

  // fold (sdiv X, pow2) -> simple ops after legalize
  // FIXME: We check for the exact bit here because the generic lowering gives
  // better results in that case. The target-specific lowering should learn how
  // to handle exact sdivs efficiently.
  if (!N->getFlags().hasExact() && ISD::matchUnaryPredicate(N1, IsPowerOfTwo)) {
    // Target-specific implementation of sdiv x, pow2.
    if (SDValue Res = BuildSDIVPow2(N))
      return Res;

    // Create constants that are functions of the shift amount value.
    EVT ShiftAmtTy = getShiftAmountTy(N0.getValueType());
    SDValue Bits = DAG.getConstant(BitWidth, DL, ShiftAmtTy);
    SDValue C1 = DAG.getNode(ISD::CTTZ, DL, VT, N1);
    C1 = DAG.getZExtOrTrunc(C1, DL, ShiftAmtTy);
    SDValue Inexact = DAG.getNode(ISD::SUB, DL, ShiftAmtTy, Bits, C1);
    if (!isConstantOrConstantVector(Inexact))
      return SDValue();

    // Splat the sign bit into the register
    SDValue Sign = DAG.getNode(ISD::SRA, DL, VT, N0,
                               DAG.getConstant(BitWidth - 1, DL, ShiftAmtTy));
    AddToWorklist(Sign.getNode());

    // Add (N0 < 0) ? abs2 - 1 : 0;
    SDValue Srl = DAG.getNode(ISD::SRL, DL, VT, Sign, Inexact);
    AddToWorklist(Srl.getNode());
    SDValue Add = DAG.getNode(ISD::ADD, DL, VT, N0, Srl);
    AddToWorklist(Add.getNode());
    SDValue Sra = DAG.getNode(ISD::SRA, DL, VT, Add, C1);
    AddToWorklist(Sra.getNode());

    // Special case: (sdiv X, 1) -> X
    // Special Case: (sdiv X, -1) -> 0-X
    SDValue One = DAG.getConstant(1, DL, VT);
    SDValue AllOnes = DAG.getAllOnesConstant(DL, VT);
    SDValue IsOne = DAG.getSetCC(DL, CCVT, N1, One, ISD::SETEQ);
    SDValue IsAllOnes = DAG.getSetCC(DL, CCVT, N1, AllOnes, ISD::SETEQ);
    SDValue IsOneOrAllOnes = DAG.getNode(ISD::OR, DL, CCVT, IsOne, IsAllOnes);
    Sra = DAG.getSelect(DL, VT, IsOneOrAllOnes, N0, Sra);

    // If dividing by a positive value, we're done. Otherwise, the result must
    // be negated.
    SDValue Zero = DAG.getConstant(0, DL, VT);
    SDValue Sub = DAG.getNode(ISD::SUB, DL, VT, Zero, Sra);

    // FIXME: Use SELECT_CC once we improve SELECT_CC constant-folding.
    SDValue IsNeg = DAG.getSetCC(DL, CCVT, N1, Zero, ISD::SETLT);
    SDValue Res = DAG.getSelect(DL, VT, IsNeg, Sub, Sra);
    return Res;
  }

  // If integer divide is expensive and we satisfy the requirements, emit an
  // alternate sequence.  Targets may check function attributes for size/speed
  // trade-offs.
  AttributeList Attr = DAG.getMachineFunction().getFunction().getAttributes();
  if (isConstantOrConstantVector(N1) &&
      !TLI.isIntDivCheap(N->getValueType(0), Attr))
    if (SDValue Op = BuildSDIV(N))
      return Op;

  return SDValue();
}

SDValue DAGCombiner::visitUDIV(SDNode *N) {
  SDValue N0 = N->getOperand(0);
  SDValue N1 = N->getOperand(1);
  EVT VT = N->getValueType(0);
  EVT CCVT = getSetCCResultType(VT);

  // fold vector ops
  if (VT.isVector())
    if (SDValue FoldedVOp = SimplifyVBinOp(N))
      return FoldedVOp;

  SDLoc DL(N);

  // fold (udiv c1, c2) -> c1/c2
  ConstantSDNode *N0C = isConstOrConstSplat(N0);
  ConstantSDNode *N1C = isConstOrConstSplat(N1);
  if (N0C && N1C)
    if (SDValue Folded = DAG.FoldConstantArithmetic(ISD::UDIV, DL, VT,
                                                    N0C, N1C))
      return Folded;
  // fold (udiv X, -1) -> select(X == -1, 1, 0)
  if (N1C && N1C->getAPIntValue().isAllOnesValue())
    return DAG.getSelect(DL, VT, DAG.getSetCC(DL, CCVT, N0, N1, ISD::SETEQ),
                         DAG.getConstant(1, DL, VT),
                         DAG.getConstant(0, DL, VT));

  if (SDValue V = simplifyDivRem(N, DAG))
    return V;

  if (SDValue NewSel = foldBinOpIntoSelect(N))
    return NewSel;

  if (SDValue V = visitUDIVLike(N0, N1, N)) {
    // If the corresponding remainder node exists, update its users with
    // (Dividend - (Quotient * Divisor).
    if (SDNode *RemNode = DAG.getNodeIfExists(ISD::UREM, N->getVTList(),
                                              { N0, N1 })) {
      SDValue Mul = DAG.getNode(ISD::MUL, DL, VT, V, N1);
      SDValue Sub = DAG.getNode(ISD::SUB, DL, VT, N0, Mul);
      AddToWorklist(Mul.getNode());
      AddToWorklist(Sub.getNode());
      CombineTo(RemNode, Sub);
    }
    return V;
  }

  // sdiv, srem -> sdivrem
  // If the divisor is constant, then return DIVREM only if isIntDivCheap() is
  // true.  Otherwise, we break the simplification logic in visitREM().
  AttributeList Attr = DAG.getMachineFunction().getFunction().getAttributes();
  if (!N1C || TLI.isIntDivCheap(N->getValueType(0), Attr))
    if (SDValue DivRem = useDivRem(N))
        return DivRem;

  return SDValue();
}

SDValue DAGCombiner::visitUDIVLike(SDValue N0, SDValue N1, SDNode *N) {
  SDLoc DL(N);
  EVT VT = N->getValueType(0);

  // fold (udiv x, (1 << c)) -> x >>u c
  if (isConstantOrConstantVector(N1, /*NoOpaques*/ true) &&
      DAG.isKnownToBeAPowerOfTwo(N1)) {
    SDValue LogBase2 = BuildLogBase2(N1, DL);
    AddToWorklist(LogBase2.getNode());

    EVT ShiftVT = getShiftAmountTy(N0.getValueType());
    SDValue Trunc = DAG.getZExtOrTrunc(LogBase2, DL, ShiftVT);
    AddToWorklist(Trunc.getNode());
    return DAG.getNode(ISD::SRL, DL, VT, N0, Trunc);
  }

  // fold (udiv x, (shl c, y)) -> x >>u (log2(c)+y) iff c is power of 2
  if (N1.getOpcode() == ISD::SHL) {
    SDValue N10 = N1.getOperand(0);
    if (isConstantOrConstantVector(N10, /*NoOpaques*/ true) &&
        DAG.isKnownToBeAPowerOfTwo(N10)) {
      SDValue LogBase2 = BuildLogBase2(N10, DL);
      AddToWorklist(LogBase2.getNode());

      EVT ADDVT = N1.getOperand(1).getValueType();
      SDValue Trunc = DAG.getZExtOrTrunc(LogBase2, DL, ADDVT);
      AddToWorklist(Trunc.getNode());
      SDValue Add = DAG.getNode(ISD::ADD, DL, ADDVT, N1.getOperand(1), Trunc);
      AddToWorklist(Add.getNode());
      return DAG.getNode(ISD::SRL, DL, VT, N0, Add);
    }
  }

  // fold (udiv x, c) -> alternate
  AttributeList Attr = DAG.getMachineFunction().getFunction().getAttributes();
  if (isConstantOrConstantVector(N1) &&
      !TLI.isIntDivCheap(N->getValueType(0), Attr))
    if (SDValue Op = BuildUDIV(N))
      return Op;

  return SDValue();
}

// handles ISD::SREM and ISD::UREM
SDValue DAGCombiner::visitREM(SDNode *N) {
  unsigned Opcode = N->getOpcode();
  SDValue N0 = N->getOperand(0);
  SDValue N1 = N->getOperand(1);
  EVT VT = N->getValueType(0);
  EVT CCVT = getSetCCResultType(VT);

  bool isSigned = (Opcode == ISD::SREM);
  SDLoc DL(N);

  // fold (rem c1, c2) -> c1%c2
  ConstantSDNode *N0C = isConstOrConstSplat(N0);
  ConstantSDNode *N1C = isConstOrConstSplat(N1);
  if (N0C && N1C)
    if (SDValue Folded = DAG.FoldConstantArithmetic(Opcode, DL, VT, N0C, N1C))
      return Folded;
  // fold (urem X, -1) -> select(X == -1, 0, x)
  if (!isSigned && N1C && N1C->getAPIntValue().isAllOnesValue())
    return DAG.getSelect(DL, VT, DAG.getSetCC(DL, CCVT, N0, N1, ISD::SETEQ),
                         DAG.getConstant(0, DL, VT), N0);

  if (SDValue V = simplifyDivRem(N, DAG))
    return V;

  if (SDValue NewSel = foldBinOpIntoSelect(N))
    return NewSel;

  if (isSigned) {
    // If we know the sign bits of both operands are zero, strength reduce to a
    // urem instead.  Handles (X & 0x0FFFFFFF) %s 16 -> X&15
    if (DAG.SignBitIsZero(N1) && DAG.SignBitIsZero(N0))
      return DAG.getNode(ISD::UREM, DL, VT, N0, N1);
  } else {
    SDValue NegOne = DAG.getAllOnesConstant(DL, VT);
    if (DAG.isKnownToBeAPowerOfTwo(N1)) {
      // fold (urem x, pow2) -> (and x, pow2-1)
      SDValue Add = DAG.getNode(ISD::ADD, DL, VT, N1, NegOne);
      AddToWorklist(Add.getNode());
      return DAG.getNode(ISD::AND, DL, VT, N0, Add);
    }
    if (N1.getOpcode() == ISD::SHL &&
        DAG.isKnownToBeAPowerOfTwo(N1.getOperand(0))) {
      // fold (urem x, (shl pow2, y)) -> (and x, (add (shl pow2, y), -1))
      SDValue Add = DAG.getNode(ISD::ADD, DL, VT, N1, NegOne);
      AddToWorklist(Add.getNode());
      return DAG.getNode(ISD::AND, DL, VT, N0, Add);
    }
  }

  AttributeList Attr = DAG.getMachineFunction().getFunction().getAttributes();

  // If X/C can be simplified by the division-by-constant logic, lower
  // X%C to the equivalent of X-X/C*C.
  // Reuse the SDIVLike/UDIVLike combines - to avoid mangling nodes, the
  // speculative DIV must not cause a DIVREM conversion.  We guard against this
  // by skipping the simplification if isIntDivCheap().  When div is not cheap,
  // combine will not return a DIVREM.  Regardless, checking cheapness here
  // makes sense since the simplification results in fatter code.
  if (DAG.isKnownNeverZero(N1) && !TLI.isIntDivCheap(VT, Attr)) {
    SDValue OptimizedDiv =
        isSigned ? visitSDIVLike(N0, N1, N) : visitUDIVLike(N0, N1, N);
    if (OptimizedDiv.getNode()) {
      // If the equivalent Div node also exists, update its users.
      unsigned DivOpcode = isSigned ? ISD::SDIV : ISD::UDIV;
      if (SDNode *DivNode = DAG.getNodeIfExists(DivOpcode, N->getVTList(),
                                                { N0, N1 }))
        CombineTo(DivNode, OptimizedDiv);
      SDValue Mul = DAG.getNode(ISD::MUL, DL, VT, OptimizedDiv, N1);
      SDValue Sub = DAG.getNode(ISD::SUB, DL, VT, N0, Mul);
      AddToWorklist(OptimizedDiv.getNode());
      AddToWorklist(Mul.getNode());
      return Sub;
    }
  }

  // sdiv, srem -> sdivrem
  if (SDValue DivRem = useDivRem(N))
    return DivRem.getValue(1);

  return SDValue();
}

SDValue DAGCombiner::visitMULHS(SDNode *N) {
  SDValue N0 = N->getOperand(0);
  SDValue N1 = N->getOperand(1);
  EVT VT = N->getValueType(0);
  SDLoc DL(N);

  if (VT.isVector()) {
    // fold (mulhs x, 0) -> 0
    if (ISD::isBuildVectorAllZeros(N1.getNode()))
      return N1;
    if (ISD::isBuildVectorAllZeros(N0.getNode()))
      return N0;
  }

  // fold (mulhs x, 0) -> 0
  if (isNullConstant(N1))
    return N1;
  // fold (mulhs x, 1) -> (sra x, size(x)-1)
  if (isOneConstant(N1))
    return DAG.getNode(ISD::SRA, DL, N0.getValueType(), N0,
                       DAG.getConstant(N0.getValueSizeInBits() - 1, DL,
                                       getShiftAmountTy(N0.getValueType())));

  // fold (mulhs x, undef) -> 0
  if (N0.isUndef() || N1.isUndef())
    return DAG.getConstant(0, DL, VT);

  // If the type twice as wide is legal, transform the mulhs to a wider multiply
  // plus a shift.
  if (VT.isSimple() && !VT.isVector()) {
    MVT Simple = VT.getSimpleVT();
    unsigned SimpleSize = Simple.getSizeInBits();
    EVT NewVT = EVT::getIntegerVT(*DAG.getContext(), SimpleSize*2);
    if (TLI.isOperationLegal(ISD::MUL, NewVT)) {
      N0 = DAG.getNode(ISD::SIGN_EXTEND, DL, NewVT, N0);
      N1 = DAG.getNode(ISD::SIGN_EXTEND, DL, NewVT, N1);
      N1 = DAG.getNode(ISD::MUL, DL, NewVT, N0, N1);
      N1 = DAG.getNode(ISD::SRL, DL, NewVT, N1,
            DAG.getConstant(SimpleSize, DL,
                            getShiftAmountTy(N1.getValueType())));
      return DAG.getNode(ISD::TRUNCATE, DL, VT, N1);
    }
  }

  return SDValue();
}

SDValue DAGCombiner::visitMULHU(SDNode *N) {
  SDValue N0 = N->getOperand(0);
  SDValue N1 = N->getOperand(1);
  EVT VT = N->getValueType(0);
  SDLoc DL(N);

  if (VT.isVector()) {
    // fold (mulhu x, 0) -> 0
    if (ISD::isBuildVectorAllZeros(N1.getNode()))
      return N1;
    if (ISD::isBuildVectorAllZeros(N0.getNode()))
      return N0;
  }

  // fold (mulhu x, 0) -> 0
  if (isNullConstant(N1))
    return N1;
  // fold (mulhu x, 1) -> 0
  if (isOneConstant(N1))
    return DAG.getConstant(0, DL, N0.getValueType());
  // fold (mulhu x, undef) -> 0
  if (N0.isUndef() || N1.isUndef())
    return DAG.getConstant(0, DL, VT);

  // fold (mulhu x, (1 << c)) -> x >> (bitwidth - c)
  if (isConstantOrConstantVector(N1, /*NoOpaques*/ true) &&
      DAG.isKnownToBeAPowerOfTwo(N1) && hasOperation(ISD::SRL, VT)) {
    SDLoc DL(N);
    unsigned NumEltBits = VT.getScalarSizeInBits();
    SDValue LogBase2 = BuildLogBase2(N1, DL);
    SDValue SRLAmt = DAG.getNode(
        ISD::SUB, DL, VT, DAG.getConstant(NumEltBits, DL, VT), LogBase2);
    EVT ShiftVT = getShiftAmountTy(N0.getValueType());
    SDValue Trunc = DAG.getZExtOrTrunc(SRLAmt, DL, ShiftVT);
    return DAG.getNode(ISD::SRL, DL, VT, N0, Trunc);
  }

  // If the type twice as wide is legal, transform the mulhu to a wider multiply
  // plus a shift.
  if (VT.isSimple() && !VT.isVector()) {
    MVT Simple = VT.getSimpleVT();
    unsigned SimpleSize = Simple.getSizeInBits();
    EVT NewVT = EVT::getIntegerVT(*DAG.getContext(), SimpleSize*2);
    if (TLI.isOperationLegal(ISD::MUL, NewVT)) {
      N0 = DAG.getNode(ISD::ZERO_EXTEND, DL, NewVT, N0);
      N1 = DAG.getNode(ISD::ZERO_EXTEND, DL, NewVT, N1);
      N1 = DAG.getNode(ISD::MUL, DL, NewVT, N0, N1);
      N1 = DAG.getNode(ISD::SRL, DL, NewVT, N1,
            DAG.getConstant(SimpleSize, DL,
                            getShiftAmountTy(N1.getValueType())));
      return DAG.getNode(ISD::TRUNCATE, DL, VT, N1);
    }
  }

  return SDValue();
}

/// Perform optimizations common to nodes that compute two values. LoOp and HiOp
/// give the opcodes for the two computations that are being performed. Return
/// true if a simplification was made.
SDValue DAGCombiner::SimplifyNodeWithTwoResults(SDNode *N, unsigned LoOp,
                                                unsigned HiOp) {
  // If the high half is not needed, just compute the low half.
  bool HiExists = N->hasAnyUseOfValue(1);
  if (!HiExists && (!LegalOperations ||
                    TLI.isOperationLegalOrCustom(LoOp, N->getValueType(0)))) {
    SDValue Res = DAG.getNode(LoOp, SDLoc(N), N->getValueType(0), N->ops());
    return CombineTo(N, Res, Res);
  }

  // If the low half is not needed, just compute the high half.
  bool LoExists = N->hasAnyUseOfValue(0);
  if (!LoExists && (!LegalOperations ||
                    TLI.isOperationLegalOrCustom(HiOp, N->getValueType(1)))) {
    SDValue Res = DAG.getNode(HiOp, SDLoc(N), N->getValueType(1), N->ops());
    return CombineTo(N, Res, Res);
  }

  // If both halves are used, return as it is.
  if (LoExists && HiExists)
    return SDValue();

  // If the two computed results can be simplified separately, separate them.
  if (LoExists) {
    SDValue Lo = DAG.getNode(LoOp, SDLoc(N), N->getValueType(0), N->ops());
    AddToWorklist(Lo.getNode());
    SDValue LoOpt = combine(Lo.getNode());
    if (LoOpt.getNode() && LoOpt.getNode() != Lo.getNode() &&
        (!LegalOperations ||
         TLI.isOperationLegalOrCustom(LoOpt.getOpcode(), LoOpt.getValueType())))
      return CombineTo(N, LoOpt, LoOpt);
  }

  if (HiExists) {
    SDValue Hi = DAG.getNode(HiOp, SDLoc(N), N->getValueType(1), N->ops());
    AddToWorklist(Hi.getNode());
    SDValue HiOpt = combine(Hi.getNode());
    if (HiOpt.getNode() && HiOpt != Hi &&
        (!LegalOperations ||
         TLI.isOperationLegalOrCustom(HiOpt.getOpcode(), HiOpt.getValueType())))
      return CombineTo(N, HiOpt, HiOpt);
  }

  return SDValue();
}

SDValue DAGCombiner::visitSMUL_LOHI(SDNode *N) {
  if (SDValue Res = SimplifyNodeWithTwoResults(N, ISD::MUL, ISD::MULHS))
    return Res;

  EVT VT = N->getValueType(0);
  SDLoc DL(N);

  // If the type is twice as wide is legal, transform the mulhu to a wider
  // multiply plus a shift.
  if (VT.isSimple() && !VT.isVector()) {
    MVT Simple = VT.getSimpleVT();
    unsigned SimpleSize = Simple.getSizeInBits();
    EVT NewVT = EVT::getIntegerVT(*DAG.getContext(), SimpleSize*2);
    if (TLI.isOperationLegal(ISD::MUL, NewVT)) {
      SDValue Lo = DAG.getNode(ISD::SIGN_EXTEND, DL, NewVT, N->getOperand(0));
      SDValue Hi = DAG.getNode(ISD::SIGN_EXTEND, DL, NewVT, N->getOperand(1));
      Lo = DAG.getNode(ISD::MUL, DL, NewVT, Lo, Hi);
      // Compute the high part as N1.
      Hi = DAG.getNode(ISD::SRL, DL, NewVT, Lo,
            DAG.getConstant(SimpleSize, DL,
                            getShiftAmountTy(Lo.getValueType())));
      Hi = DAG.getNode(ISD::TRUNCATE, DL, VT, Hi);
      // Compute the low part as N0.
      Lo = DAG.getNode(ISD::TRUNCATE, DL, VT, Lo);
      return CombineTo(N, Lo, Hi);
    }
  }

  return SDValue();
}

SDValue DAGCombiner::visitUMUL_LOHI(SDNode *N) {
  if (SDValue Res = SimplifyNodeWithTwoResults(N, ISD::MUL, ISD::MULHU))
    return Res;

  EVT VT = N->getValueType(0);
  SDLoc DL(N);

  // If the type is twice as wide is legal, transform the mulhu to a wider
  // multiply plus a shift.
  if (VT.isSimple() && !VT.isVector()) {
    MVT Simple = VT.getSimpleVT();
    unsigned SimpleSize = Simple.getSizeInBits();
    EVT NewVT = EVT::getIntegerVT(*DAG.getContext(), SimpleSize*2);
    if (TLI.isOperationLegal(ISD::MUL, NewVT)) {
      SDValue Lo = DAG.getNode(ISD::ZERO_EXTEND, DL, NewVT, N->getOperand(0));
      SDValue Hi = DAG.getNode(ISD::ZERO_EXTEND, DL, NewVT, N->getOperand(1));
      Lo = DAG.getNode(ISD::MUL, DL, NewVT, Lo, Hi);
      // Compute the high part as N1.
      Hi = DAG.getNode(ISD::SRL, DL, NewVT, Lo,
            DAG.getConstant(SimpleSize, DL,
                            getShiftAmountTy(Lo.getValueType())));
      Hi = DAG.getNode(ISD::TRUNCATE, DL, VT, Hi);
      // Compute the low part as N0.
      Lo = DAG.getNode(ISD::TRUNCATE, DL, VT, Lo);
      return CombineTo(N, Lo, Hi);
    }
  }

  return SDValue();
}

SDValue DAGCombiner::visitSMULO(SDNode *N) {
  // (smulo x, 2) -> (saddo x, x)
  if (ConstantSDNode *C2 = dyn_cast<ConstantSDNode>(N->getOperand(1)))
    if (C2->getAPIntValue() == 2)
      return DAG.getNode(ISD::SADDO, SDLoc(N), N->getVTList(),
                         N->getOperand(0), N->getOperand(0));

  return SDValue();
}

SDValue DAGCombiner::visitUMULO(SDNode *N) {
  // (umulo x, 2) -> (uaddo x, x)
  if (ConstantSDNode *C2 = dyn_cast<ConstantSDNode>(N->getOperand(1)))
    if (C2->getAPIntValue() == 2)
      return DAG.getNode(ISD::UADDO, SDLoc(N), N->getVTList(),
                         N->getOperand(0), N->getOperand(0));

  return SDValue();
}

SDValue DAGCombiner::visitIMINMAX(SDNode *N) {
  SDValue N0 = N->getOperand(0);
  SDValue N1 = N->getOperand(1);
  EVT VT = N0.getValueType();

  // fold vector ops
  if (VT.isVector())
    if (SDValue FoldedVOp = SimplifyVBinOp(N))
      return FoldedVOp;

  // fold operation with constant operands.
  ConstantSDNode *N0C = getAsNonOpaqueConstant(N0);
  ConstantSDNode *N1C = getAsNonOpaqueConstant(N1);
  if (N0C && N1C)
    return DAG.FoldConstantArithmetic(N->getOpcode(), SDLoc(N), VT, N0C, N1C);

  // canonicalize constant to RHS
  if (DAG.isConstantIntBuildVectorOrConstantInt(N0) &&
     !DAG.isConstantIntBuildVectorOrConstantInt(N1))
    return DAG.getNode(N->getOpcode(), SDLoc(N), VT, N1, N0);

  // Is sign bits are zero, flip between UMIN/UMAX and SMIN/SMAX.
  // Only do this if the current op isn't legal and the flipped is.
  unsigned Opcode = N->getOpcode();
  const TargetLowering &TLI = DAG.getTargetLoweringInfo();
  if (!TLI.isOperationLegal(Opcode, VT) &&
      (N0.isUndef() || DAG.SignBitIsZero(N0)) &&
      (N1.isUndef() || DAG.SignBitIsZero(N1))) {
    unsigned AltOpcode;
    switch (Opcode) {
    case ISD::SMIN: AltOpcode = ISD::UMIN; break;
    case ISD::SMAX: AltOpcode = ISD::UMAX; break;
    case ISD::UMIN: AltOpcode = ISD::SMIN; break;
    case ISD::UMAX: AltOpcode = ISD::SMAX; break;
    default: llvm_unreachable("Unknown MINMAX opcode");
    }
    if (TLI.isOperationLegal(AltOpcode, VT))
      return DAG.getNode(AltOpcode, SDLoc(N), VT, N0, N1);
  }

  return SDValue();
}

/// If this is a bitwise logic instruction and both operands have the same
/// opcode, try to sink the other opcode after the logic instruction.
SDValue DAGCombiner::hoistLogicOpWithSameOpcodeHands(SDNode *N) {
  SDValue N0 = N->getOperand(0), N1 = N->getOperand(1);
  EVT VT = N0.getValueType();
  unsigned LogicOpcode = N->getOpcode();
  unsigned HandOpcode = N0.getOpcode();
  assert((LogicOpcode == ISD::AND || LogicOpcode == ISD::OR ||
          LogicOpcode == ISD::XOR) && "Expected logic opcode");
  assert(HandOpcode == N1.getOpcode() && "Bad input!");

  // Bail early if none of these transforms apply.
  if (N0.getNumOperands() == 0)
    return SDValue();

  // FIXME: We should check number of uses of the operands to not increase
  //        the instruction count for all transforms.

  // Handle size-changing casts.
  SDValue X = N0.getOperand(0);
  SDValue Y = N1.getOperand(0);
  EVT XVT = X.getValueType();
  SDLoc DL(N);
  if (HandOpcode == ISD::ANY_EXTEND || HandOpcode == ISD::ZERO_EXTEND ||
      HandOpcode == ISD::SIGN_EXTEND) {
    // If both operands have other uses, this transform would create extra
    // instructions without eliminating anything.
    if (!N0.hasOneUse() && !N1.hasOneUse())
      return SDValue();
    // We need matching integer source types.
    if (XVT != Y.getValueType())
      return SDValue();
    // Don't create an illegal op during or after legalization. Don't ever
    // create an unsupported vector op.
    if ((VT.isVector() || LegalOperations) &&
        !TLI.isOperationLegalOrCustom(LogicOpcode, XVT))
      return SDValue();
    // Avoid infinite looping with PromoteIntBinOp.
    // TODO: Should we apply desirable/legal constraints to all opcodes?
    if (HandOpcode == ISD::ANY_EXTEND && LegalTypes &&
        !TLI.isTypeDesirableForOp(LogicOpcode, XVT))
      return SDValue();
    // logic_op (hand_op X), (hand_op Y) --> hand_op (logic_op X, Y)
    SDValue Logic = DAG.getNode(LogicOpcode, DL, XVT, X, Y);
    return DAG.getNode(HandOpcode, DL, VT, Logic);
  }

  // logic_op (truncate x), (truncate y) --> truncate (logic_op x, y)
  if (HandOpcode == ISD::TRUNCATE) {
    // If both operands have other uses, this transform would create extra
    // instructions without eliminating anything.
    if (!N0.hasOneUse() && !N1.hasOneUse())
      return SDValue();
    // We need matching source types.
    if (XVT != Y.getValueType())
      return SDValue();
    // Don't create an illegal op during or after legalization.
    if (LegalOperations && !TLI.isOperationLegal(LogicOpcode, XVT))
      return SDValue();
    // Be extra careful sinking truncate. If it's free, there's no benefit in
    // widening a binop. Also, don't create a logic op on an illegal type.
    if (TLI.isZExtFree(VT, XVT) && TLI.isTruncateFree(XVT, VT))
      return SDValue();
    if (!TLI.isTypeLegal(XVT))
      return SDValue();
    SDValue Logic = DAG.getNode(LogicOpcode, DL, XVT, X, Y);
    return DAG.getNode(HandOpcode, DL, VT, Logic);
  }

  // For binops SHL/SRL/SRA/AND:
  //   logic_op (OP x, z), (OP y, z) --> OP (logic_op x, y), z
  if ((HandOpcode == ISD::SHL || HandOpcode == ISD::SRL ||
       HandOpcode == ISD::SRA || HandOpcode == ISD::AND) &&
      N0.getOperand(1) == N1.getOperand(1)) {
    // If either operand has other uses, this transform is not an improvement.
    if (!N0.hasOneUse() || !N1.hasOneUse())
      return SDValue();
    SDValue Logic = DAG.getNode(LogicOpcode, DL, XVT, X, Y);
    return DAG.getNode(HandOpcode, DL, VT, Logic, N0.getOperand(1));
  }

  // Unary ops: logic_op (bswap x), (bswap y) --> bswap (logic_op x, y)
  if (HandOpcode == ISD::BSWAP) {
    // If either operand has other uses, this transform is not an improvement.
    if (!N0.hasOneUse() || !N1.hasOneUse())
      return SDValue();
    SDValue Logic = DAG.getNode(LogicOpcode, DL, XVT, X, Y);
    return DAG.getNode(HandOpcode, DL, VT, Logic);
  }

  // Simplify xor/and/or (bitcast(A), bitcast(B)) -> bitcast(op (A,B))
  // Only perform this optimization up until type legalization, before
  // LegalizeVectorOprs. LegalizeVectorOprs promotes vector operations by
  // adding bitcasts. For example (xor v4i32) is promoted to (v2i64), and
  // we don't want to undo this promotion.
  // We also handle SCALAR_TO_VECTOR because xor/or/and operations are cheaper
  // on scalars.
  if ((HandOpcode == ISD::BITCAST || HandOpcode == ISD::SCALAR_TO_VECTOR) &&
       Level <= AfterLegalizeTypes) {
    // Input types must be integer and the same.
    if (XVT.isInteger() && XVT == Y.getValueType()) {
      SDValue Logic = DAG.getNode(LogicOpcode, DL, XVT, X, Y);
      return DAG.getNode(HandOpcode, DL, VT, Logic);
    }
  }

  // Xor/and/or are indifferent to the swizzle operation (shuffle of one value).
  // Simplify xor/and/or (shuff(A), shuff(B)) -> shuff(op (A,B))
  // If both shuffles use the same mask, and both shuffle within a single
  // vector, then it is worthwhile to move the swizzle after the operation.
  // The type-legalizer generates this pattern when loading illegal
  // vector types from memory. In many cases this allows additional shuffle
  // optimizations.
  // There are other cases where moving the shuffle after the xor/and/or
  // is profitable even if shuffles don't perform a swizzle.
  // If both shuffles use the same mask, and both shuffles have the same first
  // or second operand, then it might still be profitable to move the shuffle
  // after the xor/and/or operation.
  if (HandOpcode == ISD::VECTOR_SHUFFLE && Level < AfterLegalizeDAG) {
    auto *SVN0 = cast<ShuffleVectorSDNode>(N0);
    auto *SVN1 = cast<ShuffleVectorSDNode>(N1);
    assert(X.getValueType() == Y.getValueType() &&
           "Inputs to shuffles are not the same type");

    // Check that both shuffles use the same mask. The masks are known to be of
    // the same length because the result vector type is the same.
    // Check also that shuffles have only one use to avoid introducing extra
    // instructions.
    if (!SVN0->hasOneUse() || !SVN1->hasOneUse() ||
        !SVN0->getMask().equals(SVN1->getMask()))
      return SDValue();

    // Don't try to fold this node if it requires introducing a
    // build vector of all zeros that might be illegal at this stage.
    SDValue ShOp = N0.getOperand(1);
    if (LogicOpcode == ISD::XOR && !ShOp.isUndef())
      ShOp = tryFoldToZero(DL, TLI, VT, DAG, LegalOperations);

    // (logic_op (shuf (A, C), shuf (B, C))) --> shuf (logic_op (A, B), C)
    if (N0.getOperand(1) == N1.getOperand(1) && ShOp.getNode()) {
      SDValue Logic = DAG.getNode(LogicOpcode, DL, VT,
                                  N0.getOperand(0), N1.getOperand(0));
      return DAG.getVectorShuffle(VT, DL, Logic, ShOp, SVN0->getMask());
    }

    // Don't try to fold this node if it requires introducing a
    // build vector of all zeros that might be illegal at this stage.
    ShOp = N0.getOperand(0);
    if (LogicOpcode == ISD::XOR && !ShOp.isUndef())
      ShOp = tryFoldToZero(DL, TLI, VT, DAG, LegalOperations);

    // (logic_op (shuf (C, A), shuf (C, B))) --> shuf (C, logic_op (A, B))
    if (N0.getOperand(0) == N1.getOperand(0) && ShOp.getNode()) {
      SDValue Logic = DAG.getNode(LogicOpcode, DL, VT, N0.getOperand(1),
                                  N1.getOperand(1));
      return DAG.getVectorShuffle(VT, DL, ShOp, Logic, SVN0->getMask());
    }
  }

  return SDValue();
}

/// Try to make (and/or setcc (LL, LR), setcc (RL, RR)) more efficient.
SDValue DAGCombiner::foldLogicOfSetCCs(bool IsAnd, SDValue N0, SDValue N1,
                                       const SDLoc &DL) {
  SDValue LL, LR, RL, RR, N0CC, N1CC;
  if (!isSetCCEquivalent(N0, LL, LR, N0CC) ||
      !isSetCCEquivalent(N1, RL, RR, N1CC))
    return SDValue();

  assert(N0.getValueType() == N1.getValueType() &&
         "Unexpected operand types for bitwise logic op");
  assert(LL.getValueType() == LR.getValueType() &&
         RL.getValueType() == RR.getValueType() &&
         "Unexpected operand types for setcc");

  // If we're here post-legalization or the logic op type is not i1, the logic
  // op type must match a setcc result type. Also, all folds require new
  // operations on the left and right operands, so those types must match.
  EVT VT = N0.getValueType();
  EVT OpVT = LL.getValueType();
  if (LegalOperations || VT.getScalarType() != MVT::i1)
    if (VT != getSetCCResultType(OpVT))
      return SDValue();
  if (OpVT != RL.getValueType())
    return SDValue();

  ISD::CondCode CC0 = cast<CondCodeSDNode>(N0CC)->get();
  ISD::CondCode CC1 = cast<CondCodeSDNode>(N1CC)->get();
  bool IsInteger = OpVT.isInteger();
  if (LR == RR && CC0 == CC1 && IsInteger) {
    bool IsZero = isNullOrNullSplat(LR);
    bool IsNeg1 = isAllOnesOrAllOnesSplat(LR);

    // All bits clear?
    bool AndEqZero = IsAnd && CC1 == ISD::SETEQ && IsZero;
    // All sign bits clear?
    bool AndGtNeg1 = IsAnd && CC1 == ISD::SETGT && IsNeg1;
    // Any bits set?
    bool OrNeZero = !IsAnd && CC1 == ISD::SETNE && IsZero;
    // Any sign bits set?
    bool OrLtZero = !IsAnd && CC1 == ISD::SETLT && IsZero;

    // (and (seteq X,  0), (seteq Y,  0)) --> (seteq (or X, Y),  0)
    // (and (setgt X, -1), (setgt Y, -1)) --> (setgt (or X, Y), -1)
    // (or  (setne X,  0), (setne Y,  0)) --> (setne (or X, Y),  0)
    // (or  (setlt X,  0), (setlt Y,  0)) --> (setlt (or X, Y),  0)
    if (AndEqZero || AndGtNeg1 || OrNeZero || OrLtZero) {
      SDValue Or = DAG.getNode(ISD::OR, SDLoc(N0), OpVT, LL, RL);
      AddToWorklist(Or.getNode());
      return DAG.getSetCC(DL, VT, Or, LR, CC1);
    }

    // All bits set?
    bool AndEqNeg1 = IsAnd && CC1 == ISD::SETEQ && IsNeg1;
    // All sign bits set?
    bool AndLtZero = IsAnd && CC1 == ISD::SETLT && IsZero;
    // Any bits clear?
    bool OrNeNeg1 = !IsAnd && CC1 == ISD::SETNE && IsNeg1;
    // Any sign bits clear?
    bool OrGtNeg1 = !IsAnd && CC1 == ISD::SETGT && IsNeg1;

    // (and (seteq X, -1), (seteq Y, -1)) --> (seteq (and X, Y), -1)
    // (and (setlt X,  0), (setlt Y,  0)) --> (setlt (and X, Y),  0)
    // (or  (setne X, -1), (setne Y, -1)) --> (setne (and X, Y), -1)
    // (or  (setgt X, -1), (setgt Y  -1)) --> (setgt (and X, Y), -1)
    if (AndEqNeg1 || AndLtZero || OrNeNeg1 || OrGtNeg1) {
      SDValue And = DAG.getNode(ISD::AND, SDLoc(N0), OpVT, LL, RL);
      AddToWorklist(And.getNode());
      return DAG.getSetCC(DL, VT, And, LR, CC1);
    }
  }

  // TODO: What is the 'or' equivalent of this fold?
  // (and (setne X, 0), (setne X, -1)) --> (setuge (add X, 1), 2)
  if (IsAnd && LL == RL && CC0 == CC1 && OpVT.getScalarSizeInBits() > 1 &&
      IsInteger && CC0 == ISD::SETNE &&
      ((isNullConstant(LR) && isAllOnesConstant(RR)) ||
       (isAllOnesConstant(LR) && isNullConstant(RR)))) {
    SDValue One = DAG.getConstant(1, DL, OpVT);
    SDValue Two = DAG.getConstant(2, DL, OpVT);
    SDValue Add = DAG.getNode(ISD::ADD, SDLoc(N0), OpVT, LL, One);
    AddToWorklist(Add.getNode());
    return DAG.getSetCC(DL, VT, Add, Two, ISD::SETUGE);
  }

  // Try more general transforms if the predicates match and the only user of
  // the compares is the 'and' or 'or'.
  if (IsInteger && TLI.convertSetCCLogicToBitwiseLogic(OpVT) && CC0 == CC1 &&
      N0.hasOneUse() && N1.hasOneUse()) {
    // and (seteq A, B), (seteq C, D) --> seteq (or (xor A, B), (xor C, D)), 0
    // or  (setne A, B), (setne C, D) --> setne (or (xor A, B), (xor C, D)), 0
    if ((IsAnd && CC1 == ISD::SETEQ) || (!IsAnd && CC1 == ISD::SETNE)) {
      SDValue XorL = DAG.getNode(ISD::XOR, SDLoc(N0), OpVT, LL, LR);
      SDValue XorR = DAG.getNode(ISD::XOR, SDLoc(N1), OpVT, RL, RR);
      SDValue Or = DAG.getNode(ISD::OR, DL, OpVT, XorL, XorR);
      SDValue Zero = DAG.getConstant(0, DL, OpVT);
      return DAG.getSetCC(DL, VT, Or, Zero, CC1);
    }
  }

  // Canonicalize equivalent operands to LL == RL.
  if (LL == RR && LR == RL) {
    CC1 = ISD::getSetCCSwappedOperands(CC1);
    std::swap(RL, RR);
  }

  // (and (setcc X, Y, CC0), (setcc X, Y, CC1)) --> (setcc X, Y, NewCC)
  // (or  (setcc X, Y, CC0), (setcc X, Y, CC1)) --> (setcc X, Y, NewCC)
  if (LL == RL && LR == RR) {
    ISD::CondCode NewCC = IsAnd ? ISD::getSetCCAndOperation(CC0, CC1, IsInteger)
                                : ISD::getSetCCOrOperation(CC0, CC1, IsInteger);
    if (NewCC != ISD::SETCC_INVALID &&
        (!LegalOperations ||
         (TLI.isCondCodeLegal(NewCC, LL.getSimpleValueType()) &&
          TLI.isOperationLegal(ISD::SETCC, OpVT))))
      return DAG.getSetCC(DL, VT, LL, LR, NewCC);
  }

  return SDValue();
}

/// This contains all DAGCombine rules which reduce two values combined by
/// an And operation to a single value. This makes them reusable in the context
/// of visitSELECT(). Rules involving constants are not included as
/// visitSELECT() already handles those cases.
SDValue DAGCombiner::visitANDLike(SDValue N0, SDValue N1, SDNode *N) {
  EVT VT = N1.getValueType();
  SDLoc DL(N);

  // fold (and x, undef) -> 0
  if (N0.isUndef() || N1.isUndef())
    return DAG.getConstant(0, DL, VT);

  if (SDValue V = foldLogicOfSetCCs(true, N0, N1, DL))
    return V;

  if (N0.getOpcode() == ISD::ADD && N1.getOpcode() == ISD::SRL &&
      VT.getSizeInBits() <= 64) {
    if (ConstantSDNode *ADDI = dyn_cast<ConstantSDNode>(N0.getOperand(1))) {
      if (ConstantSDNode *SRLI = dyn_cast<ConstantSDNode>(N1.getOperand(1))) {
        // Look for (and (add x, c1), (lshr y, c2)). If C1 wasn't a legal
        // immediate for an add, but it is legal if its top c2 bits are set,
        // transform the ADD so the immediate doesn't need to be materialized
        // in a register.
        APInt ADDC = ADDI->getAPIntValue();
        APInt SRLC = SRLI->getAPIntValue();
        if (ADDC.getMinSignedBits() <= 64 &&
            SRLC.ult(VT.getSizeInBits()) &&
            !TLI.isLegalAddImmediate(ADDC.getSExtValue())) {
          APInt Mask = APInt::getHighBitsSet(VT.getSizeInBits(),
                                             SRLC.getZExtValue());
          if (DAG.MaskedValueIsZero(N0.getOperand(1), Mask)) {
            ADDC |= Mask;
            if (TLI.isLegalAddImmediate(ADDC.getSExtValue())) {
              SDLoc DL0(N0);
              SDValue NewAdd =
                DAG.getNode(ISD::ADD, DL0, VT,
                            N0.getOperand(0), DAG.getConstant(ADDC, DL, VT));
              CombineTo(N0.getNode(), NewAdd);
              // Return N so it doesn't get rechecked!
              return SDValue(N, 0);
            }
          }
        }
      }
    }
  }

  // Reduce bit extract of low half of an integer to the narrower type.
  // (and (srl i64:x, K), KMask) ->
  //   (i64 zero_extend (and (srl (i32 (trunc i64:x)), K)), KMask)
  if (N0.getOpcode() == ISD::SRL && N0.hasOneUse()) {
    if (ConstantSDNode *CAnd = dyn_cast<ConstantSDNode>(N1)) {
      if (ConstantSDNode *CShift = dyn_cast<ConstantSDNode>(N0.getOperand(1))) {
        unsigned Size = VT.getSizeInBits();
        const APInt &AndMask = CAnd->getAPIntValue();
        unsigned ShiftBits = CShift->getZExtValue();

        // Bail out, this node will probably disappear anyway.
        if (ShiftBits == 0)
          return SDValue();

        unsigned MaskBits = AndMask.countTrailingOnes();
        EVT HalfVT = EVT::getIntegerVT(*DAG.getContext(), Size / 2);

        if (AndMask.isMask() &&
            // Required bits must not span the two halves of the integer and
            // must fit in the half size type.
            (ShiftBits + MaskBits <= Size / 2) &&
            TLI.isNarrowingProfitable(VT, HalfVT) &&
            TLI.isTypeDesirableForOp(ISD::AND, HalfVT) &&
            TLI.isTypeDesirableForOp(ISD::SRL, HalfVT) &&
            TLI.isTruncateFree(VT, HalfVT) &&
            TLI.isZExtFree(HalfVT, VT)) {
          // The isNarrowingProfitable is to avoid regressions on PPC and
          // AArch64 which match a few 64-bit bit insert / bit extract patterns
          // on downstream users of this. Those patterns could probably be
          // extended to handle extensions mixed in.

          SDValue SL(N0);
          assert(MaskBits <= Size);

          // Extracting the highest bit of the low half.
          EVT ShiftVT = TLI.getShiftAmountTy(HalfVT, DAG.getDataLayout());
          SDValue Trunc = DAG.getNode(ISD::TRUNCATE, SL, HalfVT,
                                      N0.getOperand(0));

          SDValue NewMask = DAG.getConstant(AndMask.trunc(Size / 2), SL, HalfVT);
          SDValue ShiftK = DAG.getConstant(ShiftBits, SL, ShiftVT);
          SDValue Shift = DAG.getNode(ISD::SRL, SL, HalfVT, Trunc, ShiftK);
          SDValue And = DAG.getNode(ISD::AND, SL, HalfVT, Shift, NewMask);
          return DAG.getNode(ISD::ZERO_EXTEND, SL, VT, And);
        }
      }
    }
  }

  return SDValue();
}

bool DAGCombiner::isAndLoadExtLoad(ConstantSDNode *AndC, LoadSDNode *LoadN,
                                   EVT LoadResultTy, EVT &ExtVT) {
  if (!AndC->getAPIntValue().isMask())
    return false;

  unsigned ActiveBits = AndC->getAPIntValue().countTrailingOnes();

  ExtVT = EVT::getIntegerVT(*DAG.getContext(), ActiveBits);
  EVT LoadedVT = LoadN->getMemoryVT();

  if (ExtVT == LoadedVT &&
      (!LegalOperations ||
       TLI.isLoadExtLegal(ISD::ZEXTLOAD, LoadResultTy, ExtVT))) {
    // ZEXTLOAD will match without needing to change the size of the value being
    // loaded.
    return true;
  }

  // Do not change the width of a volatile load.
  if (LoadN->isVolatile())
    return false;

  // Do not generate loads of non-round integer types since these can
  // be expensive (and would be wrong if the type is not byte sized).
  if (!LoadedVT.bitsGT(ExtVT) || !ExtVT.isRound())
    return false;

  if (LegalOperations &&
      !TLI.isLoadExtLegal(ISD::ZEXTLOAD, LoadResultTy, ExtVT))
    return false;

  if (!TLI.shouldReduceLoadWidth(LoadN, ISD::ZEXTLOAD, ExtVT))
    return false;

  return true;
}

bool DAGCombiner::isLegalNarrowLdSt(LSBaseSDNode *LDST,
                                    ISD::LoadExtType ExtType, EVT &MemVT,
                                    unsigned ShAmt) {
  if (!LDST)
    return false;
  // Only allow byte offsets.
  if (ShAmt % 8)
    return false;

  // Do not generate loads of non-round integer types since these can
  // be expensive (and would be wrong if the type is not byte sized).
  if (!MemVT.isRound())
    return false;

  // Don't change the width of a volatile load.
  if (LDST->isVolatile())
    return false;

  // Verify that we are actually reducing a load width here.
  if (LDST->getMemoryVT().getSizeInBits() < MemVT.getSizeInBits())
    return false;

  // Ensure that this isn't going to produce an unsupported unaligned access.
  if (ShAmt &&
      !TLI.allowsMemoryAccess(*DAG.getContext(), DAG.getDataLayout(), MemVT,
                              LDST->getAddressSpace(), ShAmt / 8))
    return false;

  // It's not possible to generate a constant of extended or untyped type.
  EVT PtrType = LDST->getBasePtr().getValueType();
  if (PtrType == MVT::Untyped || PtrType.isExtended())
    return false;

  if (isa<LoadSDNode>(LDST)) {
    LoadSDNode *Load = cast<LoadSDNode>(LDST);
    // Don't transform one with multiple uses, this would require adding a new
    // load.
    if (!SDValue(Load, 0).hasOneUse())
      return false;

    if (LegalOperations &&
        !TLI.isLoadExtLegal(ExtType, Load->getValueType(0), MemVT))
      return false;

    // For the transform to be legal, the load must produce only two values
    // (the value loaded and the chain).  Don't transform a pre-increment
    // load, for example, which produces an extra value.  Otherwise the
    // transformation is not equivalent, and the downstream logic to replace
    // uses gets things wrong.
    if (Load->getNumValues() > 2)
      return false;

    // If the load that we're shrinking is an extload and we're not just
    // discarding the extension we can't simply shrink the load. Bail.
    // TODO: It would be possible to merge the extensions in some cases.
    if (Load->getExtensionType() != ISD::NON_EXTLOAD &&
        Load->getMemoryVT().getSizeInBits() < MemVT.getSizeInBits() + ShAmt)
      return false;

    if (!TLI.shouldReduceLoadWidth(Load, ExtType, MemVT))
      return false;
  } else {
    assert(isa<StoreSDNode>(LDST) && "It is not a Load nor a Store SDNode");
    StoreSDNode *Store = cast<StoreSDNode>(LDST);
    // Can't write outside the original store
    if (Store->getMemoryVT().getSizeInBits() < MemVT.getSizeInBits() + ShAmt)
      return false;

    if (LegalOperations &&
        !TLI.isTruncStoreLegal(Store->getValue().getValueType(), MemVT))
      return false;
  }
  return true;
}

bool DAGCombiner::SearchForAndLoads(SDNode *N,
                                    SmallVectorImpl<LoadSDNode*> &Loads,
                                    SmallPtrSetImpl<SDNode*> &NodesWithConsts,
                                    ConstantSDNode *Mask,
                                    SDNode *&NodeToMask) {
  // Recursively search for the operands, looking for loads which can be
  // narrowed.
  for (unsigned i = 0, e = N->getNumOperands(); i < e; ++i) {
    SDValue Op = N->getOperand(i);

    if (Op.getValueType().isVector())
      return false;

    // Some constants may need fixing up later if they are too large.
    if (auto *C = dyn_cast<ConstantSDNode>(Op)) {
      if ((N->getOpcode() == ISD::OR || N->getOpcode() == ISD::XOR) &&
          (Mask->getAPIntValue() & C->getAPIntValue()) != C->getAPIntValue())
        NodesWithConsts.insert(N);
      continue;
    }

    if (!Op.hasOneUse())
      return false;

    switch(Op.getOpcode()) {
    case ISD::LOAD: {
      auto *Load = cast<LoadSDNode>(Op);
      EVT ExtVT;
      if (isAndLoadExtLoad(Mask, Load, Load->getValueType(0), ExtVT) &&
          isLegalNarrowLdSt(Load, ISD::ZEXTLOAD, ExtVT)) {

        // ZEXTLOAD is already small enough.
        if (Load->getExtensionType() == ISD::ZEXTLOAD &&
            ExtVT.bitsGE(Load->getMemoryVT()))
          continue;

        // Use LE to convert equal sized loads to zext.
        if (ExtVT.bitsLE(Load->getMemoryVT()))
          Loads.push_back(Load);

        continue;
      }
      return false;
    }
    case ISD::ZERO_EXTEND:
    case ISD::AssertZext: {
      unsigned ActiveBits = Mask->getAPIntValue().countTrailingOnes();
      EVT ExtVT = EVT::getIntegerVT(*DAG.getContext(), ActiveBits);
      EVT VT = Op.getOpcode() == ISD::AssertZext ?
        cast<VTSDNode>(Op.getOperand(1))->getVT() :
        Op.getOperand(0).getValueType();

      // We can accept extending nodes if the mask is wider or an equal
      // width to the original type.
      if (ExtVT.bitsGE(VT))
        continue;
      break;
    }
    case ISD::OR:
    case ISD::XOR:
    case ISD::AND:
      if (!SearchForAndLoads(Op.getNode(), Loads, NodesWithConsts, Mask,
                             NodeToMask))
        return false;
      continue;
    }

    // Allow one node which will masked along with any loads found.
    if (NodeToMask)
      return false;

    // Also ensure that the node to be masked only produces one data result.
    NodeToMask = Op.getNode();
    if (NodeToMask->getNumValues() > 1) {
      bool HasValue = false;
      for (unsigned i = 0, e = NodeToMask->getNumValues(); i < e; ++i) {
        MVT VT = SDValue(NodeToMask, i).getSimpleValueType();
        if (VT != MVT::Glue && VT != MVT::Other) {
          if (HasValue) {
            NodeToMask = nullptr;
            return false;
          }
          HasValue = true;
        }
      }
      assert(HasValue && "Node to be masked has no data result?");
    }
  }
  return true;
}

bool DAGCombiner::BackwardsPropagateMask(SDNode *N, SelectionDAG &DAG) {
  auto *Mask = dyn_cast<ConstantSDNode>(N->getOperand(1));
  if (!Mask)
    return false;

  if (!Mask->getAPIntValue().isMask())
    return false;

  // No need to do anything if the and directly uses a load.
  if (isa<LoadSDNode>(N->getOperand(0)))
    return false;

  SmallVector<LoadSDNode*, 8> Loads;
  SmallPtrSet<SDNode*, 2> NodesWithConsts;
  SDNode *FixupNode = nullptr;
  if (SearchForAndLoads(N, Loads, NodesWithConsts, Mask, FixupNode)) {
    if (Loads.size() == 0)
      return false;

    LLVM_DEBUG(dbgs() << "Backwards propagate AND: "; N->dump());
    SDValue MaskOp = N->getOperand(1);

    // If it exists, fixup the single node we allow in the tree that needs
    // masking.
    if (FixupNode) {
      LLVM_DEBUG(dbgs() << "First, need to fix up: "; FixupNode->dump());
      SDValue And = DAG.getNode(ISD::AND, SDLoc(FixupNode),
                                FixupNode->getValueType(0),
                                SDValue(FixupNode, 0), MaskOp);
      DAG.ReplaceAllUsesOfValueWith(SDValue(FixupNode, 0), And);
      if (And.getOpcode() == ISD ::AND)
        DAG.UpdateNodeOperands(And.getNode(), SDValue(FixupNode, 0), MaskOp);
    }

    // Narrow any constants that need it.
    for (auto *LogicN : NodesWithConsts) {
      SDValue Op0 = LogicN->getOperand(0);
      SDValue Op1 = LogicN->getOperand(1);

      if (isa<ConstantSDNode>(Op0))
          std::swap(Op0, Op1);

      SDValue And = DAG.getNode(ISD::AND, SDLoc(Op1), Op1.getValueType(),
                                Op1, MaskOp);

      DAG.UpdateNodeOperands(LogicN, Op0, And);
    }

    // Create narrow loads.
    for (auto *Load : Loads) {
      LLVM_DEBUG(dbgs() << "Propagate AND back to: "; Load->dump());
      SDValue And = DAG.getNode(ISD::AND, SDLoc(Load), Load->getValueType(0),
                                SDValue(Load, 0), MaskOp);
      DAG.ReplaceAllUsesOfValueWith(SDValue(Load, 0), And);
      if (And.getOpcode() == ISD ::AND)
        And = SDValue(
            DAG.UpdateNodeOperands(And.getNode(), SDValue(Load, 0), MaskOp), 0);
      SDValue NewLoad = ReduceLoadWidth(And.getNode());
      assert(NewLoad &&
             "Shouldn't be masking the load if it can't be narrowed");
      CombineTo(Load, NewLoad, NewLoad.getValue(1));
    }
    DAG.ReplaceAllUsesWith(N, N->getOperand(0).getNode());
    return true;
  }
  return false;
}

// Unfold
//    x &  (-1 'logical shift' y)
// To
//    (x 'opposite logical shift' y) 'logical shift' y
// if it is better for performance.
SDValue DAGCombiner::unfoldExtremeBitClearingToShifts(SDNode *N) {
  assert(N->getOpcode() == ISD::AND);

  SDValue N0 = N->getOperand(0);
  SDValue N1 = N->getOperand(1);

  // Do we actually prefer shifts over mask?
  if (!TLI.preferShiftsToClearExtremeBits(N0))
    return SDValue();

  // Try to match  (-1 '[outer] logical shift' y)
  unsigned OuterShift;
  unsigned InnerShift; // The opposite direction to the OuterShift.
  SDValue Y;           // Shift amount.
  auto matchMask = [&OuterShift, &InnerShift, &Y](SDValue M) -> bool {
    if (!M.hasOneUse())
      return false;
    OuterShift = M->getOpcode();
    if (OuterShift == ISD::SHL)
      InnerShift = ISD::SRL;
    else if (OuterShift == ISD::SRL)
      InnerShift = ISD::SHL;
    else
      return false;
    if (!isAllOnesConstant(M->getOperand(0)))
      return false;
    Y = M->getOperand(1);
    return true;
  };

  SDValue X;
  if (matchMask(N1))
    X = N0;
  else if (matchMask(N0))
    X = N1;
  else
    return SDValue();

  SDLoc DL(N);
  EVT VT = N->getValueType(0);

  //     tmp = x   'opposite logical shift' y
  SDValue T0 = DAG.getNode(InnerShift, DL, VT, X, Y);
  //     ret = tmp 'logical shift' y
  SDValue T1 = DAG.getNode(OuterShift, DL, VT, T0, Y);

  return T1;
}

SDValue DAGCombiner::visitAND(SDNode *N) {
  SDValue N0 = N->getOperand(0);
  SDValue N1 = N->getOperand(1);
  EVT VT = N1.getValueType();

  // x & x --> x
  if (N0 == N1)
    return N0;

  // fold vector ops
  if (VT.isVector()) {
    if (SDValue FoldedVOp = SimplifyVBinOp(N))
      return FoldedVOp;

    // fold (and x, 0) -> 0, vector edition
    if (ISD::isBuildVectorAllZeros(N0.getNode()))
      // do not return N0, because undef node may exist in N0
      return DAG.getConstant(APInt::getNullValue(N0.getScalarValueSizeInBits()),
                             SDLoc(N), N0.getValueType());
    if (ISD::isBuildVectorAllZeros(N1.getNode()))
      // do not return N1, because undef node may exist in N1
      return DAG.getConstant(APInt::getNullValue(N1.getScalarValueSizeInBits()),
                             SDLoc(N), N1.getValueType());

    // fold (and x, -1) -> x, vector edition
    if (ISD::isBuildVectorAllOnes(N0.getNode()))
      return N1;
    if (ISD::isBuildVectorAllOnes(N1.getNode()))
      return N0;
  }

  // fold (and c1, c2) -> c1&c2
  ConstantSDNode *N0C = getAsNonOpaqueConstant(N0);
  ConstantSDNode *N1C = isConstOrConstSplat(N1);
  if (N0C && N1C && !N1C->isOpaque())
    return DAG.FoldConstantArithmetic(ISD::AND, SDLoc(N), VT, N0C, N1C);
  // canonicalize constant to RHS
  if (DAG.isConstantIntBuildVectorOrConstantInt(N0) &&
      !DAG.isConstantIntBuildVectorOrConstantInt(N1))
    return DAG.getNode(ISD::AND, SDLoc(N), VT, N1, N0);
  // fold (and x, -1) -> x
  if (isAllOnesConstant(N1))
    return N0;
  // if (and x, c) is known to be zero, return 0
  unsigned BitWidth = VT.getScalarSizeInBits();
  if (N1C && DAG.MaskedValueIsZero(SDValue(N, 0),
                                   APInt::getAllOnesValue(BitWidth)))
    return DAG.getConstant(0, SDLoc(N), VT);

  if (SDValue NewSel = foldBinOpIntoSelect(N))
    return NewSel;

  // reassociate and
  if (SDValue RAND = ReassociateOps(ISD::AND, SDLoc(N), N0, N1, N->getFlags()))
    return RAND;

  // Try to convert a constant mask AND into a shuffle clear mask.
  if (VT.isVector())
    if (SDValue Shuffle = XformToShuffleWithZero(N))
      return Shuffle;

  // fold (and (or x, C), D) -> D if (C & D) == D
  auto MatchSubset = [](ConstantSDNode *LHS, ConstantSDNode *RHS) {
    return RHS->getAPIntValue().isSubsetOf(LHS->getAPIntValue());
  };
  if (N0.getOpcode() == ISD::OR &&
      ISD::matchBinaryPredicate(N0.getOperand(1), N1, MatchSubset))
    return N1;
  // fold (and (any_ext V), c) -> (zero_ext V) if 'and' only clears top bits.
  if (N1C && N0.getOpcode() == ISD::ANY_EXTEND) {
    SDValue N0Op0 = N0.getOperand(0);
    APInt Mask = ~N1C->getAPIntValue();
    Mask = Mask.trunc(N0Op0.getScalarValueSizeInBits());
    if (DAG.MaskedValueIsZero(N0Op0, Mask)) {
      SDValue Zext = DAG.getNode(ISD::ZERO_EXTEND, SDLoc(N),
                                 N0.getValueType(), N0Op0);

      // Replace uses of the AND with uses of the Zero extend node.
      CombineTo(N, Zext);

      // We actually want to replace all uses of the any_extend with the
      // zero_extend, to avoid duplicating things.  This will later cause this
      // AND to be folded.
      CombineTo(N0.getNode(), Zext);
      return SDValue(N, 0);   // Return N so it doesn't get rechecked!
    }
  }
  // similarly fold (and (X (load ([non_ext|any_ext|zero_ext] V))), c) ->
  // (X (load ([non_ext|zero_ext] V))) if 'and' only clears top bits which must
  // already be zero by virtue of the width of the base type of the load.
  //
  // the 'X' node here can either be nothing or an extract_vector_elt to catch
  // more cases.
  if ((N0.getOpcode() == ISD::EXTRACT_VECTOR_ELT &&
       N0.getValueSizeInBits() == N0.getOperand(0).getScalarValueSizeInBits() &&
       N0.getOperand(0).getOpcode() == ISD::LOAD &&
       N0.getOperand(0).getResNo() == 0) ||
      (N0.getOpcode() == ISD::LOAD && N0.getResNo() == 0)) {
    LoadSDNode *Load = cast<LoadSDNode>( (N0.getOpcode() == ISD::LOAD) ?
                                         N0 : N0.getOperand(0) );

    // Get the constant (if applicable) the zero'th operand is being ANDed with.
    // This can be a pure constant or a vector splat, in which case we treat the
    // vector as a scalar and use the splat value.
    APInt Constant = APInt::getNullValue(1);
    if (const ConstantSDNode *C = dyn_cast<ConstantSDNode>(N1)) {
      Constant = C->getAPIntValue();
    } else if (BuildVectorSDNode *Vector = dyn_cast<BuildVectorSDNode>(N1)) {
      APInt SplatValue, SplatUndef;
      unsigned SplatBitSize;
      bool HasAnyUndefs;
      bool IsSplat = Vector->isConstantSplat(SplatValue, SplatUndef,
                                             SplatBitSize, HasAnyUndefs);
      if (IsSplat) {
        // Undef bits can contribute to a possible optimisation if set, so
        // set them.
        SplatValue |= SplatUndef;

        // The splat value may be something like "0x00FFFFFF", which means 0 for
        // the first vector value and FF for the rest, repeating. We need a mask
        // that will apply equally to all members of the vector, so AND all the
        // lanes of the constant together.
        EVT VT = Vector->getValueType(0);
        unsigned BitWidth = VT.getScalarSizeInBits();

        // If the splat value has been compressed to a bitlength lower
        // than the size of the vector lane, we need to re-expand it to
        // the lane size.
        if (BitWidth > SplatBitSize)
          for (SplatValue = SplatValue.zextOrTrunc(BitWidth);
               SplatBitSize < BitWidth;
               SplatBitSize = SplatBitSize * 2)
            SplatValue |= SplatValue.shl(SplatBitSize);

        // Make sure that variable 'Constant' is only set if 'SplatBitSize' is a
        // multiple of 'BitWidth'. Otherwise, we could propagate a wrong value.
        if (SplatBitSize % BitWidth == 0) {
          Constant = APInt::getAllOnesValue(BitWidth);
          for (unsigned i = 0, n = SplatBitSize/BitWidth; i < n; ++i)
            Constant &= SplatValue.lshr(i*BitWidth).zextOrTrunc(BitWidth);
        }
      }
    }

    // If we want to change an EXTLOAD to a ZEXTLOAD, ensure a ZEXTLOAD is
    // actually legal and isn't going to get expanded, else this is a false
    // optimisation.
    bool CanZextLoadProfitably = TLI.isLoadExtLegal(ISD::ZEXTLOAD,
                                                    Load->getValueType(0),
                                                    Load->getMemoryVT());

    // Resize the constant to the same size as the original memory access before
    // extension. If it is still the AllOnesValue then this AND is completely
    // unneeded.
    Constant = Constant.zextOrTrunc(Load->getMemoryVT().getScalarSizeInBits());

    bool B;
    switch (Load->getExtensionType()) {
    default: B = false; break;
    case ISD::EXTLOAD: B = CanZextLoadProfitably; break;
    case ISD::ZEXTLOAD:
    case ISD::NON_EXTLOAD: B = true; break;
    }

    if (B && Constant.isAllOnesValue()) {
      // If the load type was an EXTLOAD, convert to ZEXTLOAD in order to
      // preserve semantics once we get rid of the AND.
      SDValue NewLoad(Load, 0);

      // Fold the AND away. NewLoad may get replaced immediately.
      CombineTo(N, (N0.getNode() == Load) ? NewLoad : N0);

      if (Load->getExtensionType() == ISD::EXTLOAD) {
        NewLoad = DAG.getLoad(Load->getAddressingMode(), ISD::ZEXTLOAD,
                              Load->getValueType(0), SDLoc(Load),
                              Load->getChain(), Load->getBasePtr(),
                              Load->getOffset(), Load->getMemoryVT(),
                              Load->getMemOperand());
        // Replace uses of the EXTLOAD with the new ZEXTLOAD.
        if (Load->getNumValues() == 3) {
          // PRE/POST_INC loads have 3 values.
          SDValue To[] = { NewLoad.getValue(0), NewLoad.getValue(1),
                           NewLoad.getValue(2) };
          CombineTo(Load, To, 3, true);
        } else {
          CombineTo(Load, NewLoad.getValue(0), NewLoad.getValue(1));
        }
      }

      return SDValue(N, 0); // Return N so it doesn't get rechecked!
    }
  }

  // fold (and (load x), 255) -> (zextload x, i8)
  // fold (and (extload x, i16), 255) -> (zextload x, i8)
  // fold (and (any_ext (extload x, i16)), 255) -> (zextload x, i8)
  if (!VT.isVector() && N1C && (N0.getOpcode() == ISD::LOAD ||
                                (N0.getOpcode() == ISD::ANY_EXTEND &&
                                 N0.getOperand(0).getOpcode() == ISD::LOAD))) {
    if (SDValue Res = ReduceLoadWidth(N)) {
      LoadSDNode *LN0 = N0->getOpcode() == ISD::ANY_EXTEND
        ? cast<LoadSDNode>(N0.getOperand(0)) : cast<LoadSDNode>(N0);
      AddToWorklist(N);
      DAG.ReplaceAllUsesOfValueWith(SDValue(LN0, 0), Res);
      return SDValue(N, 0);
    }
  }

  if (Level >= AfterLegalizeTypes) {
    // Attempt to propagate the AND back up to the leaves which, if they're
    // loads, can be combined to narrow loads and the AND node can be removed.
    // Perform after legalization so that extend nodes will already be
    // combined into the loads.
    if (BackwardsPropagateMask(N, DAG)) {
      return SDValue(N, 0);
    }
  }

  if (SDValue Combined = visitANDLike(N0, N1, N))
    return Combined;

  // Simplify: (and (op x...), (op y...))  -> (op (and x, y))
  if (N0.getOpcode() == N1.getOpcode())
    if (SDValue V = hoistLogicOpWithSameOpcodeHands(N))
      return V;

  // Masking the negated extension of a boolean is just the zero-extended
  // boolean:
  // and (sub 0, zext(bool X)), 1 --> zext(bool X)
  // and (sub 0, sext(bool X)), 1 --> zext(bool X)
  //
  // Note: the SimplifyDemandedBits fold below can make an information-losing
  // transform, and then we have no way to find this better fold.
  if (N1C && N1C->isOne() && N0.getOpcode() == ISD::SUB) {
    if (isNullOrNullSplat(N0.getOperand(0))) {
      SDValue SubRHS = N0.getOperand(1);
      if (SubRHS.getOpcode() == ISD::ZERO_EXTEND &&
          SubRHS.getOperand(0).getScalarValueSizeInBits() == 1)
        return SubRHS;
      if (SubRHS.getOpcode() == ISD::SIGN_EXTEND &&
          SubRHS.getOperand(0).getScalarValueSizeInBits() == 1)
        return DAG.getNode(ISD::ZERO_EXTEND, SDLoc(N), VT, SubRHS.getOperand(0));
    }
  }

  // fold (and (sign_extend_inreg x, i16 to i32), 1) -> (and x, 1)
  // fold (and (sra)) -> (and (srl)) when possible.
  if (SimplifyDemandedBits(SDValue(N, 0)))
    return SDValue(N, 0);

  // fold (zext_inreg (extload x)) -> (zextload x)
  if (ISD::isEXTLoad(N0.getNode()) && ISD::isUNINDEXEDLoad(N0.getNode())) {
    LoadSDNode *LN0 = cast<LoadSDNode>(N0);
    EVT MemVT = LN0->getMemoryVT();
    // If we zero all the possible extended bits, then we can turn this into
    // a zextload if we are running before legalize or the operation is legal.
    unsigned BitWidth = N1.getScalarValueSizeInBits();
    if (DAG.MaskedValueIsZero(N1, APInt::getHighBitsSet(BitWidth,
                           BitWidth - MemVT.getScalarSizeInBits())) &&
        ((!LegalOperations && !LN0->isVolatile()) ||
         TLI.isLoadExtLegal(ISD::ZEXTLOAD, VT, MemVT))) {
      SDValue ExtLoad = DAG.getExtLoad(ISD::ZEXTLOAD, SDLoc(N0), VT,
                                       LN0->getChain(), LN0->getBasePtr(),
                                       MemVT, LN0->getMemOperand());
      AddToWorklist(N);
      CombineTo(N0.getNode(), ExtLoad, ExtLoad.getValue(1));
      return SDValue(N, 0);   // Return N so it doesn't get rechecked!
    }
  }
  // fold (zext_inreg (sextload x)) -> (zextload x) iff load has one use
  if (ISD::isSEXTLoad(N0.getNode()) && ISD::isUNINDEXEDLoad(N0.getNode()) &&
      N0.hasOneUse()) {
    LoadSDNode *LN0 = cast<LoadSDNode>(N0);
    EVT MemVT = LN0->getMemoryVT();
    // If we zero all the possible extended bits, then we can turn this into
    // a zextload if we are running before legalize or the operation is legal.
    unsigned BitWidth = N1.getScalarValueSizeInBits();
    if (DAG.MaskedValueIsZero(N1, APInt::getHighBitsSet(BitWidth,
                           BitWidth - MemVT.getScalarSizeInBits())) &&
        ((!LegalOperations && !LN0->isVolatile()) ||
         TLI.isLoadExtLegal(ISD::ZEXTLOAD, VT, MemVT))) {
      SDValue ExtLoad = DAG.getExtLoad(ISD::ZEXTLOAD, SDLoc(N0), VT,
                                       LN0->getChain(), LN0->getBasePtr(),
                                       MemVT, LN0->getMemOperand());
      AddToWorklist(N);
      CombineTo(N0.getNode(), ExtLoad, ExtLoad.getValue(1));
      return SDValue(N, 0);   // Return N so it doesn't get rechecked!
    }
  }
  // fold (and (or (srl N, 8), (shl N, 8)), 0xffff) -> (srl (bswap N), const)
  if (N1C && N1C->getAPIntValue() == 0xffff && N0.getOpcode() == ISD::OR) {
    if (SDValue BSwap = MatchBSwapHWordLow(N0.getNode(), N0.getOperand(0),
                                           N0.getOperand(1), false))
      return BSwap;
  }

  if (SDValue Shifts = unfoldExtremeBitClearingToShifts(N))
    return Shifts;

  return SDValue();
}

/// Match (a >> 8) | (a << 8) as (bswap a) >> 16.
SDValue DAGCombiner::MatchBSwapHWordLow(SDNode *N, SDValue N0, SDValue N1,
                                        bool DemandHighBits) {
  if (!LegalOperations)
    return SDValue();

  EVT VT = N->getValueType(0);
  if (VT != MVT::i64 && VT != MVT::i32 && VT != MVT::i16)
    return SDValue();
  if (!TLI.isOperationLegalOrCustom(ISD::BSWAP, VT))
    return SDValue();

  // Recognize (and (shl a, 8), 0xff00), (and (srl a, 8), 0xff)
  bool LookPassAnd0 = false;
  bool LookPassAnd1 = false;
  if (N0.getOpcode() == ISD::AND && N0.getOperand(0).getOpcode() == ISD::SRL)
      std::swap(N0, N1);
  if (N1.getOpcode() == ISD::AND && N1.getOperand(0).getOpcode() == ISD::SHL)
      std::swap(N0, N1);
  if (N0.getOpcode() == ISD::AND) {
    if (!N0.getNode()->hasOneUse())
      return SDValue();
    ConstantSDNode *N01C = dyn_cast<ConstantSDNode>(N0.getOperand(1));
    // Also handle 0xffff since the LHS is guaranteed to have zeros there.
    // This is needed for X86.
    if (!N01C || (N01C->getZExtValue() != 0xFF00 &&
                  N01C->getZExtValue() != 0xFFFF))
      return SDValue();
    N0 = N0.getOperand(0);
    LookPassAnd0 = true;
  }

  if (N1.getOpcode() == ISD::AND) {
    if (!N1.getNode()->hasOneUse())
      return SDValue();
    ConstantSDNode *N11C = dyn_cast<ConstantSDNode>(N1.getOperand(1));
    if (!N11C || N11C->getZExtValue() != 0xFF)
      return SDValue();
    N1 = N1.getOperand(0);
    LookPassAnd1 = true;
  }

  if (N0.getOpcode() == ISD::SRL && N1.getOpcode() == ISD::SHL)
    std::swap(N0, N1);
  if (N0.getOpcode() != ISD::SHL || N1.getOpcode() != ISD::SRL)
    return SDValue();
  if (!N0.getNode()->hasOneUse() || !N1.getNode()->hasOneUse())
    return SDValue();

  ConstantSDNode *N01C = dyn_cast<ConstantSDNode>(N0.getOperand(1));
  ConstantSDNode *N11C = dyn_cast<ConstantSDNode>(N1.getOperand(1));
  if (!N01C || !N11C)
    return SDValue();
  if (N01C->getZExtValue() != 8 || N11C->getZExtValue() != 8)
    return SDValue();

  // Look for (shl (and a, 0xff), 8), (srl (and a, 0xff00), 8)
  SDValue N00 = N0->getOperand(0);
  if (!LookPassAnd0 && N00.getOpcode() == ISD::AND) {
    if (!N00.getNode()->hasOneUse())
      return SDValue();
    ConstantSDNode *N001C = dyn_cast<ConstantSDNode>(N00.getOperand(1));
    if (!N001C || N001C->getZExtValue() != 0xFF)
      return SDValue();
    N00 = N00.getOperand(0);
    LookPassAnd0 = true;
  }

  SDValue N10 = N1->getOperand(0);
  if (!LookPassAnd1 && N10.getOpcode() == ISD::AND) {
    if (!N10.getNode()->hasOneUse())
      return SDValue();
    ConstantSDNode *N101C = dyn_cast<ConstantSDNode>(N10.getOperand(1));
    // Also allow 0xFFFF since the bits will be shifted out. This is needed
    // for X86.
    if (!N101C || (N101C->getZExtValue() != 0xFF00 &&
                   N101C->getZExtValue() != 0xFFFF))
      return SDValue();
    N10 = N10.getOperand(0);
    LookPassAnd1 = true;
  }

  if (N00 != N10)
    return SDValue();

  // Make sure everything beyond the low halfword gets set to zero since the SRL
  // 16 will clear the top bits.
  unsigned OpSizeInBits = VT.getSizeInBits();
  if (DemandHighBits && OpSizeInBits > 16) {
    // If the left-shift isn't masked out then the only way this is a bswap is
    // if all bits beyond the low 8 are 0. In that case the entire pattern
    // reduces to a left shift anyway: leave it for other parts of the combiner.
    if (!LookPassAnd0)
      return SDValue();

    // However, if the right shift isn't masked out then it might be because
    // it's not needed. See if we can spot that too.
    if (!LookPassAnd1 &&
        !DAG.MaskedValueIsZero(
            N10, APInt::getHighBitsSet(OpSizeInBits, OpSizeInBits - 16)))
      return SDValue();
  }

  SDValue Res = DAG.getNode(ISD::BSWAP, SDLoc(N), VT, N00);
  if (OpSizeInBits > 16) {
    SDLoc DL(N);
    Res = DAG.getNode(ISD::SRL, DL, VT, Res,
                      DAG.getConstant(OpSizeInBits - 16, DL,
                                      getShiftAmountTy(VT)));
  }
  return Res;
}

/// Return true if the specified node is an element that makes up a 32-bit
/// packed halfword byteswap.
/// ((x & 0x000000ff) << 8) |
/// ((x & 0x0000ff00) >> 8) |
/// ((x & 0x00ff0000) << 8) |
/// ((x & 0xff000000) >> 8)
static bool isBSwapHWordElement(SDValue N, MutableArrayRef<SDNode *> Parts) {
  if (!N.getNode()->hasOneUse())
    return false;

  unsigned Opc = N.getOpcode();
  if (Opc != ISD::AND && Opc != ISD::SHL && Opc != ISD::SRL)
    return false;

  SDValue N0 = N.getOperand(0);
  unsigned Opc0 = N0.getOpcode();
  if (Opc0 != ISD::AND && Opc0 != ISD::SHL && Opc0 != ISD::SRL)
    return false;

  ConstantSDNode *N1C = nullptr;
  // SHL or SRL: look upstream for AND mask operand
  if (Opc == ISD::AND)
    N1C = dyn_cast<ConstantSDNode>(N.getOperand(1));
  else if (Opc0 == ISD::AND)
    N1C = dyn_cast<ConstantSDNode>(N0.getOperand(1));
  if (!N1C)
    return false;

  unsigned MaskByteOffset;
  switch (N1C->getZExtValue()) {
  default:
    return false;
  case 0xFF:       MaskByteOffset = 0; break;
  case 0xFF00:     MaskByteOffset = 1; break;
  case 0xFFFF:
    // In case demanded bits didn't clear the bits that will be shifted out.
    // This is needed for X86.
    if (Opc == ISD::SRL || (Opc == ISD::AND && Opc0 == ISD::SHL)) {
      MaskByteOffset = 1;
      break;
    }
    return false;
  case 0xFF0000:   MaskByteOffset = 2; break;
  case 0xFF000000: MaskByteOffset = 3; break;
  }

  // Look for (x & 0xff) << 8 as well as ((x << 8) & 0xff00).
  if (Opc == ISD::AND) {
    if (MaskByteOffset == 0 || MaskByteOffset == 2) {
      // (x >> 8) & 0xff
      // (x >> 8) & 0xff0000
      if (Opc0 != ISD::SRL)
        return false;
      ConstantSDNode *C = dyn_cast<ConstantSDNode>(N0.getOperand(1));
      if (!C || C->getZExtValue() != 8)
        return false;
    } else {
      // (x << 8) & 0xff00
      // (x << 8) & 0xff000000
      if (Opc0 != ISD::SHL)
        return false;
      ConstantSDNode *C = dyn_cast<ConstantSDNode>(N0.getOperand(1));
      if (!C || C->getZExtValue() != 8)
        return false;
    }
  } else if (Opc == ISD::SHL) {
    // (x & 0xff) << 8
    // (x & 0xff0000) << 8
    if (MaskByteOffset != 0 && MaskByteOffset != 2)
      return false;
    ConstantSDNode *C = dyn_cast<ConstantSDNode>(N.getOperand(1));
    if (!C || C->getZExtValue() != 8)
      return false;
  } else { // Opc == ISD::SRL
    // (x & 0xff00) >> 8
    // (x & 0xff000000) >> 8
    if (MaskByteOffset != 1 && MaskByteOffset != 3)
      return false;
    ConstantSDNode *C = dyn_cast<ConstantSDNode>(N.getOperand(1));
    if (!C || C->getZExtValue() != 8)
      return false;
  }

  if (Parts[MaskByteOffset])
    return false;

  Parts[MaskByteOffset] = N0.getOperand(0).getNode();
  return true;
}

/// Match a 32-bit packed halfword bswap. That is
/// ((x & 0x000000ff) << 8) |
/// ((x & 0x0000ff00) >> 8) |
/// ((x & 0x00ff0000) << 8) |
/// ((x & 0xff000000) >> 8)
/// => (rotl (bswap x), 16)
SDValue DAGCombiner::MatchBSwapHWord(SDNode *N, SDValue N0, SDValue N1) {
  if (!LegalOperations)
    return SDValue();

  EVT VT = N->getValueType(0);
  if (VT != MVT::i32)
    return SDValue();
  if (!TLI.isOperationLegalOrCustom(ISD::BSWAP, VT))
    return SDValue();

  // Look for either
  // (or (or (and), (and)), (or (and), (and)))
  // (or (or (or (and), (and)), (and)), (and))
  if (N0.getOpcode() != ISD::OR)
    return SDValue();
  SDValue N00 = N0.getOperand(0);
  SDValue N01 = N0.getOperand(1);
  SDNode *Parts[4] = {};

  if (N1.getOpcode() == ISD::OR &&
      N00.getNumOperands() == 2 && N01.getNumOperands() == 2) {
    // (or (or (and), (and)), (or (and), (and)))
    if (!isBSwapHWordElement(N00, Parts))
      return SDValue();

    if (!isBSwapHWordElement(N01, Parts))
      return SDValue();
    SDValue N10 = N1.getOperand(0);
    if (!isBSwapHWordElement(N10, Parts))
      return SDValue();
    SDValue N11 = N1.getOperand(1);
    if (!isBSwapHWordElement(N11, Parts))
      return SDValue();
  } else {
    // (or (or (or (and), (and)), (and)), (and))
    if (!isBSwapHWordElement(N1, Parts))
      return SDValue();
    if (!isBSwapHWordElement(N01, Parts))
      return SDValue();
    if (N00.getOpcode() != ISD::OR)
      return SDValue();
    SDValue N000 = N00.getOperand(0);
    if (!isBSwapHWordElement(N000, Parts))
      return SDValue();
    SDValue N001 = N00.getOperand(1);
    if (!isBSwapHWordElement(N001, Parts))
      return SDValue();
  }

  // Make sure the parts are all coming from the same node.
  if (Parts[0] != Parts[1] || Parts[0] != Parts[2] || Parts[0] != Parts[3])
    return SDValue();

  SDLoc DL(N);
  SDValue BSwap = DAG.getNode(ISD::BSWAP, DL, VT,
                              SDValue(Parts[0], 0));

  // Result of the bswap should be rotated by 16. If it's not legal, then
  // do  (x << 16) | (x >> 16).
  SDValue ShAmt = DAG.getConstant(16, DL, getShiftAmountTy(VT));
  if (TLI.isOperationLegalOrCustom(ISD::ROTL, VT))
    return DAG.getNode(ISD::ROTL, DL, VT, BSwap, ShAmt);
  if (TLI.isOperationLegalOrCustom(ISD::ROTR, VT))
    return DAG.getNode(ISD::ROTR, DL, VT, BSwap, ShAmt);
  return DAG.getNode(ISD::OR, DL, VT,
                     DAG.getNode(ISD::SHL, DL, VT, BSwap, ShAmt),
                     DAG.getNode(ISD::SRL, DL, VT, BSwap, ShAmt));
}

/// This contains all DAGCombine rules which reduce two values combined by
/// an Or operation to a single value \see visitANDLike().
SDValue DAGCombiner::visitORLike(SDValue N0, SDValue N1, SDNode *N) {
  EVT VT = N1.getValueType();
  SDLoc DL(N);

  // fold (or x, undef) -> -1
  if (!LegalOperations && (N0.isUndef() || N1.isUndef()))
    return DAG.getAllOnesConstant(DL, VT);

  if (SDValue V = foldLogicOfSetCCs(false, N0, N1, DL))
    return V;

  // (or (and X, C1), (and Y, C2))  -> (and (or X, Y), C3) if possible.
  if (N0.getOpcode() == ISD::AND && N1.getOpcode() == ISD::AND &&
      // Don't increase # computations.
      (N0.getNode()->hasOneUse() || N1.getNode()->hasOneUse())) {
    // We can only do this xform if we know that bits from X that are set in C2
    // but not in C1 are already zero.  Likewise for Y.
    if (const ConstantSDNode *N0O1C =
        getAsNonOpaqueConstant(N0.getOperand(1))) {
      if (const ConstantSDNode *N1O1C =
          getAsNonOpaqueConstant(N1.getOperand(1))) {
        // We can only do this xform if we know that bits from X that are set in
        // C2 but not in C1 are already zero.  Likewise for Y.
        const APInt &LHSMask = N0O1C->getAPIntValue();
        const APInt &RHSMask = N1O1C->getAPIntValue();

        if (DAG.MaskedValueIsZero(N0.getOperand(0), RHSMask&~LHSMask) &&
            DAG.MaskedValueIsZero(N1.getOperand(0), LHSMask&~RHSMask)) {
          SDValue X = DAG.getNode(ISD::OR, SDLoc(N0), VT,
                                  N0.getOperand(0), N1.getOperand(0));
          return DAG.getNode(ISD::AND, DL, VT, X,
                             DAG.getConstant(LHSMask | RHSMask, DL, VT));
        }
      }
    }
  }

  // (or (and X, M), (and X, N)) -> (and X, (or M, N))
  if (N0.getOpcode() == ISD::AND &&
      N1.getOpcode() == ISD::AND &&
      N0.getOperand(0) == N1.getOperand(0) &&
      // Don't increase # computations.
      (N0.getNode()->hasOneUse() || N1.getNode()->hasOneUse())) {
    SDValue X = DAG.getNode(ISD::OR, SDLoc(N0), VT,
                            N0.getOperand(1), N1.getOperand(1));
    return DAG.getNode(ISD::AND, DL, VT, N0.getOperand(0), X);
  }

  return SDValue();
}

SDValue DAGCombiner::visitOR(SDNode *N) {
  SDValue N0 = N->getOperand(0);
  SDValue N1 = N->getOperand(1);
  EVT VT = N1.getValueType();

  // x | x --> x
  if (N0 == N1)
    return N0;

  // fold vector ops
  if (VT.isVector()) {
    if (SDValue FoldedVOp = SimplifyVBinOp(N))
      return FoldedVOp;

    // fold (or x, 0) -> x, vector edition
    if (ISD::isBuildVectorAllZeros(N0.getNode()))
      return N1;
    if (ISD::isBuildVectorAllZeros(N1.getNode()))
      return N0;

    // fold (or x, -1) -> -1, vector edition
    if (ISD::isBuildVectorAllOnes(N0.getNode()))
      // do not return N0, because undef node may exist in N0
      return DAG.getAllOnesConstant(SDLoc(N), N0.getValueType());
    if (ISD::isBuildVectorAllOnes(N1.getNode()))
      // do not return N1, because undef node may exist in N1
      return DAG.getAllOnesConstant(SDLoc(N), N1.getValueType());

    // fold (or (shuf A, V_0, MA), (shuf B, V_0, MB)) -> (shuf A, B, Mask)
    // Do this only if the resulting shuffle is legal.
    if (isa<ShuffleVectorSDNode>(N0) &&
        isa<ShuffleVectorSDNode>(N1) &&
        // Avoid folding a node with illegal type.
        TLI.isTypeLegal(VT)) {
      bool ZeroN00 = ISD::isBuildVectorAllZeros(N0.getOperand(0).getNode());
      bool ZeroN01 = ISD::isBuildVectorAllZeros(N0.getOperand(1).getNode());
      bool ZeroN10 = ISD::isBuildVectorAllZeros(N1.getOperand(0).getNode());
      bool ZeroN11 = ISD::isBuildVectorAllZeros(N1.getOperand(1).getNode());
      // Ensure both shuffles have a zero input.
      if ((ZeroN00 != ZeroN01) && (ZeroN10 != ZeroN11)) {
        assert((!ZeroN00 || !ZeroN01) && "Both inputs zero!");
        assert((!ZeroN10 || !ZeroN11) && "Both inputs zero!");
        const ShuffleVectorSDNode *SV0 = cast<ShuffleVectorSDNode>(N0);
        const ShuffleVectorSDNode *SV1 = cast<ShuffleVectorSDNode>(N1);
        bool CanFold = true;
        int NumElts = VT.getVectorNumElements();
        SmallVector<int, 4> Mask(NumElts);

        for (int i = 0; i != NumElts; ++i) {
          int M0 = SV0->getMaskElt(i);
          int M1 = SV1->getMaskElt(i);

          // Determine if either index is pointing to a zero vector.
          bool M0Zero = M0 < 0 || (ZeroN00 == (M0 < NumElts));
          bool M1Zero = M1 < 0 || (ZeroN10 == (M1 < NumElts));

          // If one element is zero and the otherside is undef, keep undef.
          // This also handles the case that both are undef.
          if ((M0Zero && M1 < 0) || (M1Zero && M0 < 0)) {
            Mask[i] = -1;
            continue;
          }

          // Make sure only one of the elements is zero.
          if (M0Zero == M1Zero) {
            CanFold = false;
            break;
          }

          assert((M0 >= 0 || M1 >= 0) && "Undef index!");

          // We have a zero and non-zero element. If the non-zero came from
          // SV0 make the index a LHS index. If it came from SV1, make it
          // a RHS index. We need to mod by NumElts because we don't care
          // which operand it came from in the original shuffles.
          Mask[i] = M1Zero ? M0 % NumElts : (M1 % NumElts) + NumElts;
        }

        if (CanFold) {
          SDValue NewLHS = ZeroN00 ? N0.getOperand(1) : N0.getOperand(0);
          SDValue NewRHS = ZeroN10 ? N1.getOperand(1) : N1.getOperand(0);

          bool LegalMask = TLI.isShuffleMaskLegal(Mask, VT);
          if (!LegalMask) {
            std::swap(NewLHS, NewRHS);
            ShuffleVectorSDNode::commuteMask(Mask);
            LegalMask = TLI.isShuffleMaskLegal(Mask, VT);
          }

          if (LegalMask)
            return DAG.getVectorShuffle(VT, SDLoc(N), NewLHS, NewRHS, Mask);
        }
      }
    }
  }

  // fold (or c1, c2) -> c1|c2
  ConstantSDNode *N0C = getAsNonOpaqueConstant(N0);
  ConstantSDNode *N1C = dyn_cast<ConstantSDNode>(N1);
  if (N0C && N1C && !N1C->isOpaque())
    return DAG.FoldConstantArithmetic(ISD::OR, SDLoc(N), VT, N0C, N1C);
  // canonicalize constant to RHS
  if (DAG.isConstantIntBuildVectorOrConstantInt(N0) &&
     !DAG.isConstantIntBuildVectorOrConstantInt(N1))
    return DAG.getNode(ISD::OR, SDLoc(N), VT, N1, N0);
  // fold (or x, 0) -> x
  if (isNullConstant(N1))
    return N0;
  // fold (or x, -1) -> -1
  if (isAllOnesConstant(N1))
    return N1;

  if (SDValue NewSel = foldBinOpIntoSelect(N))
    return NewSel;

  // fold (or x, c) -> c iff (x & ~c) == 0
  if (N1C && DAG.MaskedValueIsZero(N0, ~N1C->getAPIntValue()))
    return N1;

  if (SDValue Combined = visitORLike(N0, N1, N))
    return Combined;

  // Recognize halfword bswaps as (bswap + rotl 16) or (bswap + shl 16)
  if (SDValue BSwap = MatchBSwapHWord(N, N0, N1))
    return BSwap;
  if (SDValue BSwap = MatchBSwapHWordLow(N, N0, N1))
    return BSwap;

  // reassociate or
  if (SDValue ROR = ReassociateOps(ISD::OR, SDLoc(N), N0, N1, N->getFlags()))
    return ROR;

  // Canonicalize (or (and X, c1), c2) -> (and (or X, c2), c1|c2)
  // iff (c1 & c2) != 0 or c1/c2 are undef.
  auto MatchIntersect = [](ConstantSDNode *C1, ConstantSDNode *C2) {
    return !C1 || !C2 || C1->getAPIntValue().intersects(C2->getAPIntValue());
  };
  if (N0.getOpcode() == ISD::AND && N0.getNode()->hasOneUse() &&
      ISD::matchBinaryPredicate(N0.getOperand(1), N1, MatchIntersect, true)) {
    if (SDValue COR = DAG.FoldConstantArithmetic(
            ISD::OR, SDLoc(N1), VT, N1.getNode(), N0.getOperand(1).getNode())) {
      SDValue IOR = DAG.getNode(ISD::OR, SDLoc(N0), VT, N0.getOperand(0), N1);
      AddToWorklist(IOR.getNode());
      return DAG.getNode(ISD::AND, SDLoc(N), VT, COR, IOR);
    }
  }

  // Simplify: (or (op x...), (op y...))  -> (op (or x, y))
  if (N0.getOpcode() == N1.getOpcode())
    if (SDValue V = hoistLogicOpWithSameOpcodeHands(N))
      return V;

  // See if this is some rotate idiom.
  if (SDNode *Rot = MatchRotate(N0, N1, SDLoc(N)))
    return SDValue(Rot, 0);

  if (SDValue Load = MatchLoadCombine(N))
    return Load;

  // Simplify the operands using demanded-bits information.
  if (SimplifyDemandedBits(SDValue(N, 0)))
    return SDValue(N, 0);

  return SDValue();
}

static SDValue stripConstantMask(SelectionDAG &DAG, SDValue Op, SDValue &Mask) {
  if (Op.getOpcode() == ISD::AND &&
      DAG.isConstantIntBuildVectorOrConstantInt(Op.getOperand(1))) {
    Mask = Op.getOperand(1);
    return Op.getOperand(0);
  }
  return Op;
}

/// Match "(X shl/srl V1) & V2" where V2 may not be present.
static bool matchRotateHalf(SelectionDAG &DAG, SDValue Op, SDValue &Shift,
                            SDValue &Mask) {
  Op = stripConstantMask(DAG, Op, Mask);
  if (Op.getOpcode() == ISD::SRL || Op.getOpcode() == ISD::SHL) {
    Shift = Op;
    return true;
  }
  return false;
}

/// Helper function for visitOR to extract the needed side of a rotate idiom
/// from a shl/srl/mul/udiv.  This is meant to handle cases where
/// InstCombine merged some outside op with one of the shifts from
/// the rotate pattern.
/// \returns An empty \c SDValue if the needed shift couldn't be extracted.
/// Otherwise, returns an expansion of \p ExtractFrom based on the following
/// patterns:
///
///   (or (mul v c0) (shrl (mul v c1) c2)):
///     expands (mul v c0) -> (shl (mul v c1) c3)
///
///   (or (udiv v c0) (shl (udiv v c1) c2)):
///     expands (udiv v c0) -> (shrl (udiv v c1) c3)
///
///   (or (shl v c0) (shrl (shl v c1) c2)):
///     expands (shl v c0) -> (shl (shl v c1) c3)
///
///   (or (shrl v c0) (shl (shrl v c1) c2)):
///     expands (shrl v c0) -> (shrl (shrl v c1) c3)
///
/// Such that in all cases, c3+c2==bitwidth(op v c1).
static SDValue extractShiftForRotate(SelectionDAG &DAG, SDValue OppShift,
                                     SDValue ExtractFrom, SDValue &Mask,
                                     const SDLoc &DL) {
  assert(OppShift && ExtractFrom && "Empty SDValue");
  assert(
      (OppShift.getOpcode() == ISD::SHL || OppShift.getOpcode() == ISD::SRL) &&
      "Existing shift must be valid as a rotate half");

  ExtractFrom = stripConstantMask(DAG, ExtractFrom, Mask);
  // Preconditions:
  //    (or (op0 v c0) (shiftl/r (op0 v c1) c2))
  //
  // Find opcode of the needed shift to be extracted from (op0 v c0).
  unsigned Opcode = ISD::DELETED_NODE;
  bool IsMulOrDiv = false;
  // Set Opcode and IsMulOrDiv if the extract opcode matches the needed shift
  // opcode or its arithmetic (mul or udiv) variant.
  auto SelectOpcode = [&](unsigned NeededShift, unsigned MulOrDivVariant) {
    IsMulOrDiv = ExtractFrom.getOpcode() == MulOrDivVariant;
    if (!IsMulOrDiv && ExtractFrom.getOpcode() != NeededShift)
      return false;
    Opcode = NeededShift;
    return true;
  };
  // op0 must be either the needed shift opcode or the mul/udiv equivalent
  // that the needed shift can be extracted from.
  if ((OppShift.getOpcode() != ISD::SRL || !SelectOpcode(ISD::SHL, ISD::MUL)) &&
      (OppShift.getOpcode() != ISD::SHL || !SelectOpcode(ISD::SRL, ISD::UDIV)))
    return SDValue();

  // op0 must be the same opcode on both sides, have the same LHS argument,
  // and produce the same value type.
  SDValue OppShiftLHS = OppShift.getOperand(0);
  EVT ShiftedVT = OppShiftLHS.getValueType();
  if (OppShiftLHS.getOpcode() != ExtractFrom.getOpcode() ||
      OppShiftLHS.getOperand(0) != ExtractFrom.getOperand(0) ||
      ShiftedVT != ExtractFrom.getValueType())
    return SDValue();

  // Amount of the existing shift.
  ConstantSDNode *OppShiftCst = isConstOrConstSplat(OppShift.getOperand(1));
  // Constant mul/udiv/shift amount from the RHS of the shift's LHS op.
  ConstantSDNode *OppLHSCst = isConstOrConstSplat(OppShiftLHS.getOperand(1));
  // Constant mul/udiv/shift amount from the RHS of the ExtractFrom op.
  ConstantSDNode *ExtractFromCst =
      isConstOrConstSplat(ExtractFrom.getOperand(1));
  // TODO: We should be able to handle non-uniform constant vectors for these values
  // Check that we have constant values.
  if (!OppShiftCst || !OppShiftCst->getAPIntValue() ||
      !OppLHSCst || !OppLHSCst->getAPIntValue() ||
      !ExtractFromCst || !ExtractFromCst->getAPIntValue())
    return SDValue();

  // Compute the shift amount we need to extract to complete the rotate.
  const unsigned VTWidth = ShiftedVT.getScalarSizeInBits();
  if (OppShiftCst->getAPIntValue().ugt(VTWidth))
    return SDValue();
  APInt NeededShiftAmt = VTWidth - OppShiftCst->getAPIntValue();
  // Normalize the bitwidth of the two mul/udiv/shift constant operands.
  APInt ExtractFromAmt = ExtractFromCst->getAPIntValue();
  APInt OppLHSAmt = OppLHSCst->getAPIntValue();
  zeroExtendToMatch(ExtractFromAmt, OppLHSAmt);

  // Now try extract the needed shift from the ExtractFrom op and see if the
  // result matches up with the existing shift's LHS op.
  if (IsMulOrDiv) {
    // Op to extract from is a mul or udiv by a constant.
    // Check:
    //     c2 / (1 << (bitwidth(op0 v c0) - c1)) == c0
    //     c2 % (1 << (bitwidth(op0 v c0) - c1)) == 0
    const APInt ExtractDiv = APInt::getOneBitSet(ExtractFromAmt.getBitWidth(),
                                                 NeededShiftAmt.getZExtValue());
    APInt ResultAmt;
    APInt Rem;
    APInt::udivrem(ExtractFromAmt, ExtractDiv, ResultAmt, Rem);
    if (Rem != 0 || ResultAmt != OppLHSAmt)
      return SDValue();
  } else {
    // Op to extract from is a shift by a constant.
    // Check:
    //      c2 - (bitwidth(op0 v c0) - c1) == c0
    if (OppLHSAmt != ExtractFromAmt - NeededShiftAmt.zextOrTrunc(
                                          ExtractFromAmt.getBitWidth()))
      return SDValue();
  }

  // Return the expanded shift op that should allow a rotate to be formed.
  EVT ShiftVT = OppShift.getOperand(1).getValueType();
  EVT ResVT = ExtractFrom.getValueType();
  SDValue NewShiftNode = DAG.getConstant(NeededShiftAmt, DL, ShiftVT);
  return DAG.getNode(Opcode, DL, ResVT, OppShiftLHS, NewShiftNode);
}

// Return true if we can prove that, whenever Neg and Pos are both in the
// range [0, EltSize), Neg == (Pos == 0 ? 0 : EltSize - Pos).  This means that
// for two opposing shifts shift1 and shift2 and a value X with OpBits bits:
//
//     (or (shift1 X, Neg), (shift2 X, Pos))
//
// reduces to a rotate in direction shift2 by Pos or (equivalently) a rotate
// in direction shift1 by Neg.  The range [0, EltSize) means that we only need
// to consider shift amounts with defined behavior.
static bool matchRotateSub(SDValue Pos, SDValue Neg, unsigned EltSize,
                           SelectionDAG &DAG) {
  // If EltSize is a power of 2 then:
  //
  //  (a) (Pos == 0 ? 0 : EltSize - Pos) == (EltSize - Pos) & (EltSize - 1)
  //  (b) Neg == Neg & (EltSize - 1) whenever Neg is in [0, EltSize).
  //
  // So if EltSize is a power of 2 and Neg is (and Neg', EltSize-1), we check
  // for the stronger condition:
  //
  //     Neg & (EltSize - 1) == (EltSize - Pos) & (EltSize - 1)    [A]
  //
  // for all Neg and Pos.  Since Neg & (EltSize - 1) == Neg' & (EltSize - 1)
  // we can just replace Neg with Neg' for the rest of the function.
  //
  // In other cases we check for the even stronger condition:
  //
  //     Neg == EltSize - Pos                                    [B]
  //
  // for all Neg and Pos.  Note that the (or ...) then invokes undefined
  // behavior if Pos == 0 (and consequently Neg == EltSize).
  //
  // We could actually use [A] whenever EltSize is a power of 2, but the
  // only extra cases that it would match are those uninteresting ones
  // where Neg and Pos are never in range at the same time.  E.g. for
  // EltSize == 32, using [A] would allow a Neg of the form (sub 64, Pos)
  // as well as (sub 32, Pos), but:
  //
  //     (or (shift1 X, (sub 64, Pos)), (shift2 X, Pos))
  //
  // always invokes undefined behavior for 32-bit X.
  //
  // Below, Mask == EltSize - 1 when using [A] and is all-ones otherwise.
  unsigned MaskLoBits = 0;
  if (Neg.getOpcode() == ISD::AND && isPowerOf2_64(EltSize)) {
    if (ConstantSDNode *NegC = isConstOrConstSplat(Neg.getOperand(1))) {
      KnownBits Known = DAG.computeKnownBits(Neg.getOperand(0));
      unsigned Bits = Log2_64(EltSize);
      if (NegC->getAPIntValue().getActiveBits() <= Bits &&
          ((NegC->getAPIntValue() | Known.Zero).countTrailingOnes() >= Bits)) {
        Neg = Neg.getOperand(0);
        MaskLoBits = Bits;
      }
    }
  }

  // Check whether Neg has the form (sub NegC, NegOp1) for some NegC and NegOp1.
  if (Neg.getOpcode() != ISD::SUB)
    return false;
  ConstantSDNode *NegC = isConstOrConstSplat(Neg.getOperand(0));
  if (!NegC)
    return false;
  SDValue NegOp1 = Neg.getOperand(1);

  // On the RHS of [A], if Pos is Pos' & (EltSize - 1), just replace Pos with
  // Pos'.  The truncation is redundant for the purpose of the equality.
  if (MaskLoBits && Pos.getOpcode() == ISD::AND) {
    if (ConstantSDNode *PosC = isConstOrConstSplat(Pos.getOperand(1))) {
      KnownBits Known = DAG.computeKnownBits(Pos.getOperand(0));
      if (PosC->getAPIntValue().getActiveBits() <= MaskLoBits &&
          ((PosC->getAPIntValue() | Known.Zero).countTrailingOnes() >=
           MaskLoBits))
        Pos = Pos.getOperand(0);
    }
  }

  // The condition we need is now:
  //
  //     (NegC - NegOp1) & Mask == (EltSize - Pos) & Mask
  //
  // If NegOp1 == Pos then we need:
  //
  //              EltSize & Mask == NegC & Mask
  //
  // (because "x & Mask" is a truncation and distributes through subtraction).
  APInt Width;
  if (Pos == NegOp1)
    Width = NegC->getAPIntValue();

  // Check for cases where Pos has the form (add NegOp1, PosC) for some PosC.
  // Then the condition we want to prove becomes:
  //
  //     (NegC - NegOp1) & Mask == (EltSize - (NegOp1 + PosC)) & Mask
  //
  // which, again because "x & Mask" is a truncation, becomes:
  //
  //                NegC & Mask == (EltSize - PosC) & Mask
  //             EltSize & Mask == (NegC + PosC) & Mask
  else if (Pos.getOpcode() == ISD::ADD && Pos.getOperand(0) == NegOp1) {
    if (ConstantSDNode *PosC = isConstOrConstSplat(Pos.getOperand(1)))
      Width = PosC->getAPIntValue() + NegC->getAPIntValue();
    else
      return false;
  } else
    return false;

  // Now we just need to check that EltSize & Mask == Width & Mask.
  if (MaskLoBits)
    // EltSize & Mask is 0 since Mask is EltSize - 1.
    return Width.getLoBits(MaskLoBits) == 0;
  return Width == EltSize;
}

// A subroutine of MatchRotate used once we have found an OR of two opposite
// shifts of Shifted.  If Neg == <operand size> - Pos then the OR reduces
// to both (PosOpcode Shifted, Pos) and (NegOpcode Shifted, Neg), with the
// former being preferred if supported.  InnerPos and InnerNeg are Pos and
// Neg with outer conversions stripped away.
SDNode *DAGCombiner::MatchRotatePosNeg(SDValue Shifted, SDValue Pos,
                                       SDValue Neg, SDValue InnerPos,
                                       SDValue InnerNeg, unsigned PosOpcode,
                                       unsigned NegOpcode, const SDLoc &DL) {
  // fold (or (shl x, (*ext y)),
  //          (srl x, (*ext (sub 32, y)))) ->
  //   (rotl x, y) or (rotr x, (sub 32, y))
  //
  // fold (or (shl x, (*ext (sub 32, y))),
  //          (srl x, (*ext y))) ->
  //   (rotr x, y) or (rotl x, (sub 32, y))
  EVT VT = Shifted.getValueType();
  if (matchRotateSub(InnerPos, InnerNeg, VT.getScalarSizeInBits(), DAG)) {
    bool HasPos = TLI.isOperationLegalOrCustom(PosOpcode, VT);
    return DAG.getNode(HasPos ? PosOpcode : NegOpcode, DL, VT, Shifted,
                       HasPos ? Pos : Neg).getNode();
  }

  return nullptr;
}

// MatchRotate - Handle an 'or' of two operands.  If this is one of the many
// idioms for rotate, and if the target supports rotation instructions, generate
// a rot[lr].
SDNode *DAGCombiner::MatchRotate(SDValue LHS, SDValue RHS, const SDLoc &DL) {
  // Must be a legal type.  Expanded 'n promoted things won't work with rotates.
  EVT VT = LHS.getValueType();
  if (!TLI.isTypeLegal(VT)) return nullptr;

  // The target must have at least one rotate flavor.
  bool HasROTL = hasOperation(ISD::ROTL, VT);
  bool HasROTR = hasOperation(ISD::ROTR, VT);
  if (!HasROTL && !HasROTR) return nullptr;

  // Check for truncated rotate.
  if (LHS.getOpcode() == ISD::TRUNCATE && RHS.getOpcode() == ISD::TRUNCATE &&
      LHS.getOperand(0).getValueType() == RHS.getOperand(0).getValueType()) {
    assert(LHS.getValueType() == RHS.getValueType());
    if (SDNode *Rot = MatchRotate(LHS.getOperand(0), RHS.getOperand(0), DL)) {
      return DAG.getNode(ISD::TRUNCATE, SDLoc(LHS), LHS.getValueType(),
                         SDValue(Rot, 0)).getNode();
    }
  }

  // Match "(X shl/srl V1) & V2" where V2 may not be present.
  SDValue LHSShift;   // The shift.
  SDValue LHSMask;    // AND value if any.
  matchRotateHalf(DAG, LHS, LHSShift, LHSMask);

  SDValue RHSShift;   // The shift.
  SDValue RHSMask;    // AND value if any.
  matchRotateHalf(DAG, RHS, RHSShift, RHSMask);

  // If neither side matched a rotate half, bail
  if (!LHSShift && !RHSShift)
    return nullptr;

  // InstCombine may have combined a constant shl, srl, mul, or udiv with one
  // side of the rotate, so try to handle that here. In all cases we need to
  // pass the matched shift from the opposite side to compute the opcode and
  // needed shift amount to extract.  We still want to do this if both sides
  // matched a rotate half because one half may be a potential overshift that
  // can be broken down (ie if InstCombine merged two shl or srl ops into a
  // single one).

  // Have LHS side of the rotate, try to extract the needed shift from the RHS.
  if (LHSShift)
    if (SDValue NewRHSShift =
            extractShiftForRotate(DAG, LHSShift, RHS, RHSMask, DL))
      RHSShift = NewRHSShift;
  // Have RHS side of the rotate, try to extract the needed shift from the LHS.
  if (RHSShift)
    if (SDValue NewLHSShift =
            extractShiftForRotate(DAG, RHSShift, LHS, LHSMask, DL))
      LHSShift = NewLHSShift;

  // If a side is still missing, nothing else we can do.
  if (!RHSShift || !LHSShift)
    return nullptr;

  // At this point we've matched or extracted a shift op on each side.

  if (LHSShift.getOperand(0) != RHSShift.getOperand(0))
    return nullptr;   // Not shifting the same value.

  if (LHSShift.getOpcode() == RHSShift.getOpcode())
    return nullptr;   // Shifts must disagree.

  // Canonicalize shl to left side in a shl/srl pair.
  if (RHSShift.getOpcode() == ISD::SHL) {
    std::swap(LHS, RHS);
    std::swap(LHSShift, RHSShift);
    std::swap(LHSMask, RHSMask);
  }

  unsigned EltSizeInBits = VT.getScalarSizeInBits();
  SDValue LHSShiftArg = LHSShift.getOperand(0);
  SDValue LHSShiftAmt = LHSShift.getOperand(1);
  SDValue RHSShiftArg = RHSShift.getOperand(0);
  SDValue RHSShiftAmt = RHSShift.getOperand(1);

  // fold (or (shl x, C1), (srl x, C2)) -> (rotl x, C1)
  // fold (or (shl x, C1), (srl x, C2)) -> (rotr x, C2)
  auto MatchRotateSum = [EltSizeInBits](ConstantSDNode *LHS,
                                        ConstantSDNode *RHS) {
    return (LHS->getAPIntValue() + RHS->getAPIntValue()) == EltSizeInBits;
  };
  if (ISD::matchBinaryPredicate(LHSShiftAmt, RHSShiftAmt, MatchRotateSum)) {
    SDValue Rot = DAG.getNode(HasROTL ? ISD::ROTL : ISD::ROTR, DL, VT,
                              LHSShiftArg, HasROTL ? LHSShiftAmt : RHSShiftAmt);

    // If there is an AND of either shifted operand, apply it to the result.
    if (LHSMask.getNode() || RHSMask.getNode()) {
      SDValue AllOnes = DAG.getAllOnesConstant(DL, VT);
      SDValue Mask = AllOnes;

      if (LHSMask.getNode()) {
        SDValue RHSBits = DAG.getNode(ISD::SRL, DL, VT, AllOnes, RHSShiftAmt);
        Mask = DAG.getNode(ISD::AND, DL, VT, Mask,
                           DAG.getNode(ISD::OR, DL, VT, LHSMask, RHSBits));
      }
      if (RHSMask.getNode()) {
        SDValue LHSBits = DAG.getNode(ISD::SHL, DL, VT, AllOnes, LHSShiftAmt);
        Mask = DAG.getNode(ISD::AND, DL, VT, Mask,
                           DAG.getNode(ISD::OR, DL, VT, RHSMask, LHSBits));
      }

      Rot = DAG.getNode(ISD::AND, DL, VT, Rot, Mask);
    }

    return Rot.getNode();
  }

  // If there is a mask here, and we have a variable shift, we can't be sure
  // that we're masking out the right stuff.
  if (LHSMask.getNode() || RHSMask.getNode())
    return nullptr;

  // If the shift amount is sign/zext/any-extended just peel it off.
  SDValue LExtOp0 = LHSShiftAmt;
  SDValue RExtOp0 = RHSShiftAmt;
  if ((LHSShiftAmt.getOpcode() == ISD::SIGN_EXTEND ||
       LHSShiftAmt.getOpcode() == ISD::ZERO_EXTEND ||
       LHSShiftAmt.getOpcode() == ISD::ANY_EXTEND ||
       LHSShiftAmt.getOpcode() == ISD::TRUNCATE) &&
      (RHSShiftAmt.getOpcode() == ISD::SIGN_EXTEND ||
       RHSShiftAmt.getOpcode() == ISD::ZERO_EXTEND ||
       RHSShiftAmt.getOpcode() == ISD::ANY_EXTEND ||
       RHSShiftAmt.getOpcode() == ISD::TRUNCATE)) {
    LExtOp0 = LHSShiftAmt.getOperand(0);
    RExtOp0 = RHSShiftAmt.getOperand(0);
  }

  SDNode *TryL = MatchRotatePosNeg(LHSShiftArg, LHSShiftAmt, RHSShiftAmt,
                                   LExtOp0, RExtOp0, ISD::ROTL, ISD::ROTR, DL);
  if (TryL)
    return TryL;

  SDNode *TryR = MatchRotatePosNeg(RHSShiftArg, RHSShiftAmt, LHSShiftAmt,
                                   RExtOp0, LExtOp0, ISD::ROTR, ISD::ROTL, DL);
  if (TryR)
    return TryR;

  return nullptr;
}

namespace {

/// Represents known origin of an individual byte in load combine pattern. The
/// value of the byte is either constant zero or comes from memory.
struct ByteProvider {
  // For constant zero providers Load is set to nullptr. For memory providers
  // Load represents the node which loads the byte from memory.
  // ByteOffset is the offset of the byte in the value produced by the load.
  LoadSDNode *Load = nullptr;
  unsigned ByteOffset = 0;

  ByteProvider() = default;

  static ByteProvider getMemory(LoadSDNode *Load, unsigned ByteOffset) {
    return ByteProvider(Load, ByteOffset);
  }

  static ByteProvider getConstantZero() { return ByteProvider(nullptr, 0); }

  bool isConstantZero() const { return !Load; }
  bool isMemory() const { return Load; }

  bool operator==(const ByteProvider &Other) const {
    return Other.Load == Load && Other.ByteOffset == ByteOffset;
  }

private:
  ByteProvider(LoadSDNode *Load, unsigned ByteOffset)
      : Load(Load), ByteOffset(ByteOffset) {}
};

} // end anonymous namespace

/// Recursively traverses the expression calculating the origin of the requested
/// byte of the given value. Returns None if the provider can't be calculated.
///
/// For all the values except the root of the expression verifies that the value
/// has exactly one use and if it's not true return None. This way if the origin
/// of the byte is returned it's guaranteed that the values which contribute to
/// the byte are not used outside of this expression.
///
/// Because the parts of the expression are not allowed to have more than one
/// use this function iterates over trees, not DAGs. So it never visits the same
/// node more than once.
static const Optional<ByteProvider>
calculateByteProvider(SDValue Op, unsigned Index, unsigned Depth,
                      bool Root = false) {
  // Typical i64 by i8 pattern requires recursion up to 8 calls depth
  if (Depth == 10)
    return None;

  if (!Root && !Op.hasOneUse())
    return None;

  assert(Op.getValueType().isScalarInteger() && "can't handle other types");
  unsigned BitWidth = Op.getValueSizeInBits();
  if (BitWidth % 8 != 0)
    return None;
  unsigned ByteWidth = BitWidth / 8;
  assert(Index < ByteWidth && "invalid index requested");
  (void) ByteWidth;

  switch (Op.getOpcode()) {
  case ISD::OR: {
    auto LHS = calculateByteProvider(Op->getOperand(0), Index, Depth + 1);
    if (!LHS)
      return None;
    auto RHS = calculateByteProvider(Op->getOperand(1), Index, Depth + 1);
    if (!RHS)
      return None;

    if (LHS->isConstantZero())
      return RHS;
    if (RHS->isConstantZero())
      return LHS;
    return None;
  }
  case ISD::SHL: {
    auto ShiftOp = dyn_cast<ConstantSDNode>(Op->getOperand(1));
    if (!ShiftOp)
      return None;

    uint64_t BitShift = ShiftOp->getZExtValue();
    if (BitShift % 8 != 0)
      return None;
    uint64_t ByteShift = BitShift / 8;

    return Index < ByteShift
               ? ByteProvider::getConstantZero()
               : calculateByteProvider(Op->getOperand(0), Index - ByteShift,
                                       Depth + 1);
  }
  case ISD::ANY_EXTEND:
  case ISD::SIGN_EXTEND:
  case ISD::ZERO_EXTEND: {
    SDValue NarrowOp = Op->getOperand(0);
    unsigned NarrowBitWidth = NarrowOp.getScalarValueSizeInBits();
    if (NarrowBitWidth % 8 != 0)
      return None;
    uint64_t NarrowByteWidth = NarrowBitWidth / 8;

    if (Index >= NarrowByteWidth)
      return Op.getOpcode() == ISD::ZERO_EXTEND
                 ? Optional<ByteProvider>(ByteProvider::getConstantZero())
                 : None;
    return calculateByteProvider(NarrowOp, Index, Depth + 1);
  }
  case ISD::BSWAP:
    return calculateByteProvider(Op->getOperand(0), ByteWidth - Index - 1,
                                 Depth + 1);
  case ISD::LOAD: {
    auto L = cast<LoadSDNode>(Op.getNode());
    if (L->isVolatile() || L->isIndexed())
      return None;

    unsigned NarrowBitWidth = L->getMemoryVT().getSizeInBits();
    if (NarrowBitWidth % 8 != 0)
      return None;
    uint64_t NarrowByteWidth = NarrowBitWidth / 8;

    if (Index >= NarrowByteWidth)
      return L->getExtensionType() == ISD::ZEXTLOAD
                 ? Optional<ByteProvider>(ByteProvider::getConstantZero())
                 : None;
    return ByteProvider::getMemory(L, Index);
  }
  }

  return None;
}

/// Match a pattern where a wide type scalar value is loaded by several narrow
/// loads and combined by shifts and ors. Fold it into a single load or a load
/// and a BSWAP if the targets supports it.
///
/// Assuming little endian target:
///  i8 *a = ...
///  i32 val = a[0] | (a[1] << 8) | (a[2] << 16) | (a[3] << 24)
/// =>
///  i32 val = *((i32)a)
///
///  i8 *a = ...
///  i32 val = (a[0] << 24) | (a[1] << 16) | (a[2] << 8) | a[3]
/// =>
///  i32 val = BSWAP(*((i32)a))
///
/// TODO: This rule matches complex patterns with OR node roots and doesn't
/// interact well with the worklist mechanism. When a part of the pattern is
/// updated (e.g. one of the loads) its direct users are put into the worklist,
/// but the root node of the pattern which triggers the load combine is not
/// necessarily a direct user of the changed node. For example, once the address
/// of t28 load is reassociated load combine won't be triggered:
///             t25: i32 = add t4, Constant:i32<2>
///           t26: i64 = sign_extend t25
///        t27: i64 = add t2, t26
///       t28: i8,ch = load<LD1[%tmp9]> t0, t27, undef:i64
///     t29: i32 = zero_extend t28
///   t32: i32 = shl t29, Constant:i8<8>
/// t33: i32 = or t23, t32
/// As a possible fix visitLoad can check if the load can be a part of a load
/// combine pattern and add corresponding OR roots to the worklist.
SDValue DAGCombiner::MatchLoadCombine(SDNode *N) {
  assert(N->getOpcode() == ISD::OR &&
         "Can only match load combining against OR nodes");

  // Handles simple types only
  EVT VT = N->getValueType(0);
  if (VT != MVT::i16 && VT != MVT::i32 && VT != MVT::i64)
    return SDValue();
  unsigned ByteWidth = VT.getSizeInBits() / 8;

  const TargetLowering &TLI = DAG.getTargetLoweringInfo();
  // Before legalize we can introduce too wide illegal loads which will be later
  // split into legal sized loads. This enables us to combine i64 load by i8
  // patterns to a couple of i32 loads on 32 bit targets.
  if (LegalOperations && !TLI.isOperationLegal(ISD::LOAD, VT))
    return SDValue();

  std::function<unsigned(unsigned, unsigned)> LittleEndianByteAt = [](
    unsigned BW, unsigned i) { return i; };
  std::function<unsigned(unsigned, unsigned)> BigEndianByteAt = [](
    unsigned BW, unsigned i) { return BW - i - 1; };

  bool IsBigEndianTarget = DAG.getDataLayout().isBigEndian();
  auto MemoryByteOffset = [&] (ByteProvider P) {
    assert(P.isMemory() && "Must be a memory byte provider");
    unsigned LoadBitWidth = P.Load->getMemoryVT().getSizeInBits();
    assert(LoadBitWidth % 8 == 0 &&
           "can only analyze providers for individual bytes not bit");
    unsigned LoadByteWidth = LoadBitWidth / 8;
    return IsBigEndianTarget
            ? BigEndianByteAt(LoadByteWidth, P.ByteOffset)
            : LittleEndianByteAt(LoadByteWidth, P.ByteOffset);
  };

  Optional<BaseIndexOffset> Base;
  SDValue Chain;

  SmallPtrSet<LoadSDNode *, 8> Loads;
  Optional<ByteProvider> FirstByteProvider;
  int64_t FirstOffset = INT64_MAX;

  // Check if all the bytes of the OR we are looking at are loaded from the same
  // base address. Collect bytes offsets from Base address in ByteOffsets.
  SmallVector<int64_t, 4> ByteOffsets(ByteWidth);
  for (unsigned i = 0; i < ByteWidth; i++) {
    auto P = calculateByteProvider(SDValue(N, 0), i, 0, /*Root=*/true);
    if (!P || !P->isMemory()) // All the bytes must be loaded from memory
      return SDValue();

    LoadSDNode *L = P->Load;
    assert(L->hasNUsesOfValue(1, 0) && !L->isVolatile() && !L->isIndexed() &&
           "Must be enforced by calculateByteProvider");
    assert(L->getOffset().isUndef() && "Unindexed load must have undef offset");

    // All loads must share the same chain
    SDValue LChain = L->getChain();
    if (!Chain)
      Chain = LChain;
    else if (Chain != LChain)
      return SDValue();

    // Loads must share the same base address
    BaseIndexOffset Ptr = BaseIndexOffset::match(L, DAG);
    int64_t ByteOffsetFromBase = 0;
    if (!Base)
      Base = Ptr;
    else if (!Base->equalBaseIndex(Ptr, DAG, ByteOffsetFromBase))
      return SDValue();

    // Calculate the offset of the current byte from the base address
    ByteOffsetFromBase += MemoryByteOffset(*P);
    ByteOffsets[i] = ByteOffsetFromBase;

    // Remember the first byte load
    if (ByteOffsetFromBase < FirstOffset) {
      FirstByteProvider = P;
      FirstOffset = ByteOffsetFromBase;
    }

    Loads.insert(L);
  }
  assert(!Loads.empty() && "All the bytes of the value must be loaded from "
         "memory, so there must be at least one load which produces the value");
  assert(Base && "Base address of the accessed memory location must be set");
  assert(FirstOffset != INT64_MAX && "First byte offset must be set");

  // Check if the bytes of the OR we are looking at match with either big or
  // little endian value load
  bool BigEndian = true, LittleEndian = true;
  for (unsigned i = 0; i < ByteWidth; i++) {
    int64_t CurrentByteOffset = ByteOffsets[i] - FirstOffset;
    LittleEndian &= CurrentByteOffset == LittleEndianByteAt(ByteWidth, i);
    BigEndian &= CurrentByteOffset == BigEndianByteAt(ByteWidth, i);
    if (!BigEndian && !LittleEndian)
      return SDValue();
  }
  assert((BigEndian != LittleEndian) && "should be either or");
  assert(FirstByteProvider && "must be set");

  // Ensure that the first byte is loaded from zero offset of the first load.
  // So the combined value can be loaded from the first load address.
  if (MemoryByteOffset(*FirstByteProvider) != 0)
    return SDValue();
  LoadSDNode *FirstLoad = FirstByteProvider->Load;

  // The node we are looking at matches with the pattern, check if we can
  // replace it with a single load and bswap if needed.

  // If the load needs byte swap check if the target supports it
  bool NeedsBswap = IsBigEndianTarget != BigEndian;

  // Before legalize we can introduce illegal bswaps which will be later
  // converted to an explicit bswap sequence. This way we end up with a single
  // load and byte shuffling instead of several loads and byte shuffling.
  if (NeedsBswap && LegalOperations && !TLI.isOperationLegal(ISD::BSWAP, VT))
    return SDValue();

  // Check that a load of the wide type is both allowed and fast on the target
  bool Fast = false;
  bool Allowed = TLI.allowsMemoryAccess(*DAG.getContext(), DAG.getDataLayout(),
                                        VT, FirstLoad->getAddressSpace(),
                                        FirstLoad->getAlignment(), &Fast);
  if (!Allowed || !Fast)
    return SDValue();

  SDValue NewLoad =
      DAG.getLoad(VT, SDLoc(N), Chain, FirstLoad->getBasePtr(),
                  FirstLoad->getPointerInfo(), FirstLoad->getAlignment());

  // Transfer chain users from old loads to the new load.
  for (LoadSDNode *L : Loads)
    DAG.ReplaceAllUsesOfValueWith(SDValue(L, 1), SDValue(NewLoad.getNode(), 1));

  return NeedsBswap ? DAG.getNode(ISD::BSWAP, SDLoc(N), VT, NewLoad) : NewLoad;
}

// If the target has andn, bsl, or a similar bit-select instruction,
// we want to unfold masked merge, with canonical pattern of:
//   |        A  |  |B|
//   ((x ^ y) & m) ^ y
//    |  D  |
// Into:
//   (x & m) | (y & ~m)
// If y is a constant, and the 'andn' does not work with immediates,
// we unfold into a different pattern:
//   ~(~x & m) & (m | y)
// NOTE: we don't unfold the pattern if 'xor' is actually a 'not', because at
//       the very least that breaks andnpd / andnps patterns, and because those
//       patterns are simplified in IR and shouldn't be created in the DAG
SDValue DAGCombiner::unfoldMaskedMerge(SDNode *N) {
  assert(N->getOpcode() == ISD::XOR);

  // Don't touch 'not' (i.e. where y = -1).
  if (isAllOnesOrAllOnesSplat(N->getOperand(1)))
    return SDValue();

  EVT VT = N->getValueType(0);

  // There are 3 commutable operators in the pattern,
  // so we have to deal with 8 possible variants of the basic pattern.
  SDValue X, Y, M;
  auto matchAndXor = [&X, &Y, &M](SDValue And, unsigned XorIdx, SDValue Other) {
    if (And.getOpcode() != ISD::AND || !And.hasOneUse())
      return false;
    SDValue Xor = And.getOperand(XorIdx);
    if (Xor.getOpcode() != ISD::XOR || !Xor.hasOneUse())
      return false;
    SDValue Xor0 = Xor.getOperand(0);
    SDValue Xor1 = Xor.getOperand(1);
    // Don't touch 'not' (i.e. where y = -1).
    if (isAllOnesOrAllOnesSplat(Xor1))
      return false;
    if (Other == Xor0)
      std::swap(Xor0, Xor1);
    if (Other != Xor1)
      return false;
    X = Xor0;
    Y = Xor1;
    M = And.getOperand(XorIdx ? 0 : 1);
    return true;
  };

  SDValue N0 = N->getOperand(0);
  SDValue N1 = N->getOperand(1);
  if (!matchAndXor(N0, 0, N1) && !matchAndXor(N0, 1, N1) &&
      !matchAndXor(N1, 0, N0) && !matchAndXor(N1, 1, N0))
    return SDValue();

  // Don't do anything if the mask is constant. This should not be reachable.
  // InstCombine should have already unfolded this pattern, and DAGCombiner
  // probably shouldn't produce it, too.
  if (isa<ConstantSDNode>(M.getNode()))
    return SDValue();

  // We can transform if the target has AndNot
  if (!TLI.hasAndNot(M))
    return SDValue();

  SDLoc DL(N);

  // If Y is a constant, check that 'andn' works with immediates.
  if (!TLI.hasAndNot(Y)) {
    assert(TLI.hasAndNot(X) && "Only mask is a variable? Unreachable.");
    // If not, we need to do a bit more work to make sure andn is still used.
    SDValue NotX = DAG.getNOT(DL, X, VT);
    SDValue LHS = DAG.getNode(ISD::AND, DL, VT, NotX, M);
    SDValue NotLHS = DAG.getNOT(DL, LHS, VT);
    SDValue RHS = DAG.getNode(ISD::OR, DL, VT, M, Y);
    return DAG.getNode(ISD::AND, DL, VT, NotLHS, RHS);
  }

  SDValue LHS = DAG.getNode(ISD::AND, DL, VT, X, M);
  SDValue NotM = DAG.getNOT(DL, M, VT);
  SDValue RHS = DAG.getNode(ISD::AND, DL, VT, Y, NotM);

  return DAG.getNode(ISD::OR, DL, VT, LHS, RHS);
}

SDValue DAGCombiner::visitXOR(SDNode *N) {
  SDValue N0 = N->getOperand(0);
  SDValue N1 = N->getOperand(1);
  EVT VT = N0.getValueType();

  // fold vector ops
  if (VT.isVector()) {
    if (SDValue FoldedVOp = SimplifyVBinOp(N))
      return FoldedVOp;

    // fold (xor x, 0) -> x, vector edition
    if (ISD::isBuildVectorAllZeros(N0.getNode()))
      return N1;
    if (ISD::isBuildVectorAllZeros(N1.getNode()))
      return N0;
  }

  // fold (xor undef, undef) -> 0. This is a common idiom (misuse).
  SDLoc DL(N);
  if (N0.isUndef() && N1.isUndef())
    return DAG.getConstant(0, DL, VT);
  // fold (xor x, undef) -> undef
  if (N0.isUndef())
    return N0;
  if (N1.isUndef())
    return N1;
  // fold (xor c1, c2) -> c1^c2
  ConstantSDNode *N0C = getAsNonOpaqueConstant(N0);
  ConstantSDNode *N1C = getAsNonOpaqueConstant(N1);
  if (N0C && N1C)
    return DAG.FoldConstantArithmetic(ISD::XOR, DL, VT, N0C, N1C);
  // canonicalize constant to RHS
  if (DAG.isConstantIntBuildVectorOrConstantInt(N0) &&
     !DAG.isConstantIntBuildVectorOrConstantInt(N1))
    return DAG.getNode(ISD::XOR, DL, VT, N1, N0);
  // fold (xor x, 0) -> x
  if (isNullConstant(N1))
    return N0;

  if (SDValue NewSel = foldBinOpIntoSelect(N))
    return NewSel;

  // reassociate xor
  if (SDValue RXOR = ReassociateOps(ISD::XOR, DL, N0, N1, N->getFlags()))
    return RXOR;

  // fold !(x cc y) -> (x !cc y)
  unsigned N0Opcode = N0.getOpcode();
  SDValue LHS, RHS, CC;
  if (TLI.isConstTrueVal(N1.getNode()) && isSetCCEquivalent(N0, LHS, RHS, CC)) {
    ISD::CondCode NotCC = ISD::getSetCCInverse(cast<CondCodeSDNode>(CC)->get(),
                                               LHS.getValueType().isInteger());
    if (!LegalOperations ||
        TLI.isCondCodeLegal(NotCC, LHS.getSimpleValueType())) {
      switch (N0Opcode) {
      default:
        llvm_unreachable("Unhandled SetCC Equivalent!");
      case ISD::SETCC:
        return DAG.getSetCC(SDLoc(N0), VT, LHS, RHS, NotCC);
      case ISD::SELECT_CC:
        return DAG.getSelectCC(SDLoc(N0), LHS, RHS, N0.getOperand(2),
                               N0.getOperand(3), NotCC);
      }
    }
  }

  // fold (not (zext (setcc x, y))) -> (zext (not (setcc x, y)))
  if (isOneConstant(N1) && N0Opcode == ISD::ZERO_EXTEND && N0.hasOneUse() &&
      isSetCCEquivalent(N0.getOperand(0), LHS, RHS, CC)){
    SDValue V = N0.getOperand(0);
    SDLoc DL0(N0);
    V = DAG.getNode(ISD::XOR, DL0, V.getValueType(), V,
                    DAG.getConstant(1, DL0, V.getValueType()));
    AddToWorklist(V.getNode());
    return DAG.getNode(ISD::ZERO_EXTEND, DL, VT, V);
  }

  // fold (not (or x, y)) -> (and (not x), (not y)) iff x or y are setcc
  if (isOneConstant(N1) && VT == MVT::i1 && N0.hasOneUse() &&
      (N0Opcode == ISD::OR || N0Opcode == ISD::AND)) {
    SDValue LHS = N0.getOperand(0), RHS = N0.getOperand(1);
    if (isOneUseSetCC(RHS) || isOneUseSetCC(LHS)) {
      unsigned NewOpcode = N0Opcode == ISD::AND ? ISD::OR : ISD::AND;
      LHS = DAG.getNode(ISD::XOR, SDLoc(LHS), VT, LHS, N1); // LHS = ~LHS
      RHS = DAG.getNode(ISD::XOR, SDLoc(RHS), VT, RHS, N1); // RHS = ~RHS
      AddToWorklist(LHS.getNode()); AddToWorklist(RHS.getNode());
      return DAG.getNode(NewOpcode, DL, VT, LHS, RHS);
    }
  }
  // fold (not (or x, y)) -> (and (not x), (not y)) iff x or y are constants
  if (isAllOnesConstant(N1) && N0.hasOneUse() &&
      (N0Opcode == ISD::OR || N0Opcode == ISD::AND)) {
    SDValue LHS = N0.getOperand(0), RHS = N0.getOperand(1);
    if (isa<ConstantSDNode>(RHS) || isa<ConstantSDNode>(LHS)) {
      unsigned NewOpcode = N0Opcode == ISD::AND ? ISD::OR : ISD::AND;
      LHS = DAG.getNode(ISD::XOR, SDLoc(LHS), VT, LHS, N1); // LHS = ~LHS
      RHS = DAG.getNode(ISD::XOR, SDLoc(RHS), VT, RHS, N1); // RHS = ~RHS
      AddToWorklist(LHS.getNode()); AddToWorklist(RHS.getNode());
      return DAG.getNode(NewOpcode, DL, VT, LHS, RHS);
    }
  }
  // fold (xor (and x, y), y) -> (and (not x), y)
  if (N0Opcode == ISD::AND && N0.hasOneUse() && N0->getOperand(1) == N1) {
    SDValue X = N0.getOperand(0);
    SDValue NotX = DAG.getNOT(SDLoc(X), X, VT);
    AddToWorklist(NotX.getNode());
    return DAG.getNode(ISD::AND, DL, VT, NotX, N1);
  }

  if ((N0Opcode == ISD::SRL || N0Opcode == ISD::SHL) && N0.hasOneUse()) {
    ConstantSDNode *XorC = isConstOrConstSplat(N1);
    ConstantSDNode *ShiftC = isConstOrConstSplat(N0.getOperand(1));
    unsigned BitWidth = VT.getScalarSizeInBits();
    if (XorC && ShiftC) {
      // Don't crash on an oversized shift. We can not guarantee that a bogus
      // shift has been simplified to undef.
      uint64_t ShiftAmt = ShiftC->getLimitedValue();
      if (ShiftAmt < BitWidth) {
        APInt Ones = APInt::getAllOnesValue(BitWidth);
        Ones = N0Opcode == ISD::SHL ? Ones.shl(ShiftAmt) : Ones.lshr(ShiftAmt);
        if (XorC->getAPIntValue() == Ones) {
          // If the xor constant is a shifted -1, do a 'not' before the shift:
          // xor (X << ShiftC), XorC --> (not X) << ShiftC
          // xor (X >> ShiftC), XorC --> (not X) >> ShiftC
          SDValue Not = DAG.getNOT(DL, N0.getOperand(0), VT);
          return DAG.getNode(N0Opcode, DL, VT, Not, N0.getOperand(1));
        }
      }
    }
  }

  // fold Y = sra (X, size(X)-1); xor (add (X, Y), Y) -> (abs X)
  if (TLI.isOperationLegalOrCustom(ISD::ABS, VT)) {
    SDValue A = N0Opcode == ISD::ADD ? N0 : N1;
    SDValue S = N0Opcode == ISD::SRA ? N0 : N1;
    if (A.getOpcode() == ISD::ADD && S.getOpcode() == ISD::SRA) {
      SDValue A0 = A.getOperand(0), A1 = A.getOperand(1);
      SDValue S0 = S.getOperand(0);
      if ((A0 == S && A1 == S0) || (A1 == S && A0 == S0)) {
        unsigned OpSizeInBits = VT.getScalarSizeInBits();
        if (ConstantSDNode *C = isConstOrConstSplat(S.getOperand(1)))
          if (C->getAPIntValue() == (OpSizeInBits - 1))
            return DAG.getNode(ISD::ABS, DL, VT, S0);
      }
    }
  }

  // fold (xor x, x) -> 0
  if (N0 == N1)
    return tryFoldToZero(DL, TLI, VT, DAG, LegalOperations);

  // fold (xor (shl 1, x), -1) -> (rotl ~1, x)
  // Here is a concrete example of this equivalence:
  // i16   x ==  14
  // i16 shl ==   1 << 14  == 16384 == 0b0100000000000000
  // i16 xor == ~(1 << 14) == 49151 == 0b1011111111111111
  //
  // =>
  //
  // i16     ~1      == 0b1111111111111110
  // i16 rol(~1, 14) == 0b1011111111111111
  //
  // Some additional tips to help conceptualize this transform:
  // - Try to see the operation as placing a single zero in a value of all ones.
  // - There exists no value for x which would allow the result to contain zero.
  // - Values of x larger than the bitwidth are undefined and do not require a
  //   consistent result.
  // - Pushing the zero left requires shifting one bits in from the right.
  // A rotate left of ~1 is a nice way of achieving the desired result.
  if (TLI.isOperationLegalOrCustom(ISD::ROTL, VT) && N0Opcode == ISD::SHL &&
      isAllOnesConstant(N1) && isOneConstant(N0.getOperand(0))) {
    return DAG.getNode(ISD::ROTL, DL, VT, DAG.getConstant(~1, DL, VT),
                       N0.getOperand(1));
  }

  // Simplify: xor (op x...), (op y...)  -> (op (xor x, y))
  if (N0Opcode == N1.getOpcode())
    if (SDValue V = hoistLogicOpWithSameOpcodeHands(N))
      return V;

  // Unfold  ((x ^ y) & m) ^ y  into  (x & m) | (y & ~m)  if profitable
  if (SDValue MM = unfoldMaskedMerge(N))
    return MM;

  // Simplify the expression using non-local knowledge.
  if (SimplifyDemandedBits(SDValue(N, 0)))
    return SDValue(N, 0);

  return SDValue();
}

/// Handle transforms common to the three shifts, when the shift amount is a
/// constant.
SDValue DAGCombiner::visitShiftByConstant(SDNode *N, ConstantSDNode *Amt) {
  // Do not turn a 'not' into a regular xor.
  if (isBitwiseNot(N->getOperand(0)))
    return SDValue();

  SDNode *LHS = N->getOperand(0).getNode();
  if (!LHS->hasOneUse()) return SDValue();

  // We want to pull some binops through shifts, so that we have (and (shift))
  // instead of (shift (and)), likewise for add, or, xor, etc.  This sort of
  // thing happens with address calculations, so it's important to canonicalize
  // it.
  bool HighBitSet = false;  // Can we transform this if the high bit is set?

  switch (LHS->getOpcode()) {
  default: return SDValue();
  case ISD::OR:
  case ISD::XOR:
    HighBitSet = false; // We can only transform sra if the high bit is clear.
    break;
  case ISD::AND:
    HighBitSet = true;  // We can only transform sra if the high bit is set.
    break;
  case ISD::ADD:
    if (N->getOpcode() != ISD::SHL)
      return SDValue(); // only shl(add) not sr[al](add).
    HighBitSet = false; // We can only transform sra if the high bit is clear.
    break;
  }

  // We require the RHS of the binop to be a constant and not opaque as well.
  ConstantSDNode *BinOpCst = getAsNonOpaqueConstant(LHS->getOperand(1));
  if (!BinOpCst) return SDValue();

  // FIXME: disable this unless the input to the binop is a shift by a constant
  // or is copy/select.Enable this in other cases when figure out it's exactly profitable.
  SDNode *BinOpLHSVal = LHS->getOperand(0).getNode();
  bool isShift = BinOpLHSVal->getOpcode() == ISD::SHL ||
                 BinOpLHSVal->getOpcode() == ISD::SRA ||
                 BinOpLHSVal->getOpcode() == ISD::SRL;
  bool isCopyOrSelect = BinOpLHSVal->getOpcode() == ISD::CopyFromReg ||
                        BinOpLHSVal->getOpcode() == ISD::SELECT;

  if ((!isShift || !isa<ConstantSDNode>(BinOpLHSVal->getOperand(1))) &&
      !isCopyOrSelect)
    return SDValue();

  if (isCopyOrSelect && N->hasOneUse())
    return SDValue();

  EVT VT = N->getValueType(0);

  // If this is a signed shift right, and the high bit is modified by the
  // logical operation, do not perform the transformation. The highBitSet
  // boolean indicates the value of the high bit of the constant which would
  // cause it to be modified for this operation.
  if (N->getOpcode() == ISD::SRA) {
    bool BinOpRHSSignSet = BinOpCst->getAPIntValue().isNegative();
    if (BinOpRHSSignSet != HighBitSet)
      return SDValue();
  }

  if (!TLI.isDesirableToCommuteWithShift(N, Level))
    return SDValue();

  // Fold the constants, shifting the binop RHS by the shift amount.
  SDValue NewRHS = DAG.getNode(N->getOpcode(), SDLoc(LHS->getOperand(1)),
                               N->getValueType(0),
                               LHS->getOperand(1), N->getOperand(1));
  assert(isa<ConstantSDNode>(NewRHS) && "Folding was not successful!");

  // Create the new shift.
  SDValue NewShift = DAG.getNode(N->getOpcode(),
                                 SDLoc(LHS->getOperand(0)),
                                 VT, LHS->getOperand(0), N->getOperand(1));

  // Create the new binop.
  return DAG.getNode(LHS->getOpcode(), SDLoc(N), VT, NewShift, NewRHS);
}

SDValue DAGCombiner::distributeTruncateThroughAnd(SDNode *N) {
  assert(N->getOpcode() == ISD::TRUNCATE);
  assert(N->getOperand(0).getOpcode() == ISD::AND);

  // (truncate:TruncVT (and N00, N01C)) -> (and (truncate:TruncVT N00), TruncC)
  if (N->hasOneUse() && N->getOperand(0).hasOneUse()) {
    SDValue N01 = N->getOperand(0).getOperand(1);
    if (isConstantOrConstantVector(N01, /* NoOpaques */ true)) {
      SDLoc DL(N);
      EVT TruncVT = N->getValueType(0);
      SDValue N00 = N->getOperand(0).getOperand(0);
      SDValue Trunc00 = DAG.getNode(ISD::TRUNCATE, DL, TruncVT, N00);
      SDValue Trunc01 = DAG.getNode(ISD::TRUNCATE, DL, TruncVT, N01);
      AddToWorklist(Trunc00.getNode());
      AddToWorklist(Trunc01.getNode());
      return DAG.getNode(ISD::AND, DL, TruncVT, Trunc00, Trunc01);
    }
  }

  return SDValue();
}

SDValue DAGCombiner::visitRotate(SDNode *N) {
  SDLoc dl(N);
  SDValue N0 = N->getOperand(0);
  SDValue N1 = N->getOperand(1);
  EVT VT = N->getValueType(0);
  unsigned Bitsize = VT.getScalarSizeInBits();

  // fold (rot x, 0) -> x
  if (isNullOrNullSplat(N1))
    return N0;

  // fold (rot x, c) -> x iff (c % BitSize) == 0
  if (isPowerOf2_32(Bitsize) && Bitsize > 1) {
    APInt ModuloMask(N1.getScalarValueSizeInBits(), Bitsize - 1);
    if (DAG.MaskedValueIsZero(N1, ModuloMask))
      return N0;
  }

  // fold (rot x, c) -> (rot x, c % BitSize)
  if (ConstantSDNode *Cst = isConstOrConstSplat(N1)) {
    if (Cst->getAPIntValue().uge(Bitsize)) {
      uint64_t RotAmt = Cst->getAPIntValue().urem(Bitsize);
      return DAG.getNode(N->getOpcode(), dl, VT, N0,
                         DAG.getConstant(RotAmt, dl, N1.getValueType()));
    }
  }

  // fold (rot* x, (trunc (and y, c))) -> (rot* x, (and (trunc y), (trunc c))).
  if (N1.getOpcode() == ISD::TRUNCATE &&
      N1.getOperand(0).getOpcode() == ISD::AND) {
    if (SDValue NewOp1 = distributeTruncateThroughAnd(N1.getNode()))
      return DAG.getNode(N->getOpcode(), dl, VT, N0, NewOp1);
  }

  unsigned NextOp = N0.getOpcode();
  // fold (rot* (rot* x, c2), c1) -> (rot* x, c1 +- c2 % bitsize)
  if (NextOp == ISD::ROTL || NextOp == ISD::ROTR) {
    SDNode *C1 = DAG.isConstantIntBuildVectorOrConstantInt(N1);
    SDNode *C2 = DAG.isConstantIntBuildVectorOrConstantInt(N0.getOperand(1));
    if (C1 && C2 && C1->getValueType(0) == C2->getValueType(0)) {
      EVT ShiftVT = C1->getValueType(0);
      bool SameSide = (N->getOpcode() == NextOp);
      unsigned CombineOp = SameSide ? ISD::ADD : ISD::SUB;
      if (SDValue CombinedShift =
              DAG.FoldConstantArithmetic(CombineOp, dl, ShiftVT, C1, C2)) {
        SDValue BitsizeC = DAG.getConstant(Bitsize, dl, ShiftVT);
        SDValue CombinedShiftNorm = DAG.FoldConstantArithmetic(
            ISD::SREM, dl, ShiftVT, CombinedShift.getNode(),
            BitsizeC.getNode());
        return DAG.getNode(N->getOpcode(), dl, VT, N0->getOperand(0),
                           CombinedShiftNorm);
      }
    }
  }
  return SDValue();
}

SDValue DAGCombiner::visitSHL(SDNode *N) {
  SDValue N0 = N->getOperand(0);
  SDValue N1 = N->getOperand(1);
  if (SDValue V = DAG.simplifyShift(N0, N1))
    return V;

  EVT VT = N0.getValueType();
  unsigned OpSizeInBits = VT.getScalarSizeInBits();

  // fold vector ops
  if (VT.isVector()) {
    if (SDValue FoldedVOp = SimplifyVBinOp(N))
      return FoldedVOp;

    BuildVectorSDNode *N1CV = dyn_cast<BuildVectorSDNode>(N1);
    // If setcc produces all-one true value then:
    // (shl (and (setcc) N01CV) N1CV) -> (and (setcc) N01CV<<N1CV)
    if (N1CV && N1CV->isConstant()) {
      if (N0.getOpcode() == ISD::AND) {
        SDValue N00 = N0->getOperand(0);
        SDValue N01 = N0->getOperand(1);
        BuildVectorSDNode *N01CV = dyn_cast<BuildVectorSDNode>(N01);

        if (N01CV && N01CV->isConstant() && N00.getOpcode() == ISD::SETCC &&
            TLI.getBooleanContents(N00.getOperand(0).getValueType()) ==
                TargetLowering::ZeroOrNegativeOneBooleanContent) {
          if (SDValue C = DAG.FoldConstantArithmetic(ISD::SHL, SDLoc(N), VT,
                                                     N01CV, N1CV))
            return DAG.getNode(ISD::AND, SDLoc(N), VT, N00, C);
        }
      }
    }
  }

  ConstantSDNode *N1C = isConstOrConstSplat(N1);

  // fold (shl c1, c2) -> c1<<c2
  ConstantSDNode *N0C = getAsNonOpaqueConstant(N0);
  if (N0C && N1C && !N1C->isOpaque())
    return DAG.FoldConstantArithmetic(ISD::SHL, SDLoc(N), VT, N0C, N1C);

  if (SDValue NewSel = foldBinOpIntoSelect(N))
    return NewSel;

  // if (shl x, c) is known to be zero, return 0
  if (DAG.MaskedValueIsZero(SDValue(N, 0),
                            APInt::getAllOnesValue(OpSizeInBits)))
    return DAG.getConstant(0, SDLoc(N), VT);
  // fold (shl x, (trunc (and y, c))) -> (shl x, (and (trunc y), (trunc c))).
  if (N1.getOpcode() == ISD::TRUNCATE &&
      N1.getOperand(0).getOpcode() == ISD::AND) {
    if (SDValue NewOp1 = distributeTruncateThroughAnd(N1.getNode()))
      return DAG.getNode(ISD::SHL, SDLoc(N), VT, N0, NewOp1);
  }

  if (N1C && SimplifyDemandedBits(SDValue(N, 0)))
    return SDValue(N, 0);

  // fold (shl (shl x, c1), c2) -> 0 or (shl x, (add c1, c2))
  if (N0.getOpcode() == ISD::SHL) {
    auto MatchOutOfRange = [OpSizeInBits](ConstantSDNode *LHS,
                                          ConstantSDNode *RHS) {
      APInt c1 = LHS->getAPIntValue();
      APInt c2 = RHS->getAPIntValue();
      zeroExtendToMatch(c1, c2, 1 /* Overflow Bit */);
      return (c1 + c2).uge(OpSizeInBits);
    };
    if (ISD::matchBinaryPredicate(N1, N0.getOperand(1), MatchOutOfRange))
      return DAG.getConstant(0, SDLoc(N), VT);

    auto MatchInRange = [OpSizeInBits](ConstantSDNode *LHS,
                                       ConstantSDNode *RHS) {
      APInt c1 = LHS->getAPIntValue();
      APInt c2 = RHS->getAPIntValue();
      zeroExtendToMatch(c1, c2, 1 /* Overflow Bit */);
      return (c1 + c2).ult(OpSizeInBits);
    };
    if (ISD::matchBinaryPredicate(N1, N0.getOperand(1), MatchInRange)) {
      SDLoc DL(N);
      EVT ShiftVT = N1.getValueType();
      SDValue Sum = DAG.getNode(ISD::ADD, DL, ShiftVT, N1, N0.getOperand(1));
      return DAG.getNode(ISD::SHL, DL, VT, N0.getOperand(0), Sum);
    }
  }

  // fold (shl (ext (shl x, c1)), c2) -> (ext (shl x, (add c1, c2)))
  // For this to be valid, the second form must not preserve any of the bits
  // that are shifted out by the inner shift in the first form.  This means
  // the outer shift size must be >= the number of bits added by the ext.
  // As a corollary, we don't care what kind of ext it is.
  if (N1C && (N0.getOpcode() == ISD::ZERO_EXTEND ||
              N0.getOpcode() == ISD::ANY_EXTEND ||
              N0.getOpcode() == ISD::SIGN_EXTEND) &&
      N0.getOperand(0).getOpcode() == ISD::SHL) {
    SDValue N0Op0 = N0.getOperand(0);
    if (ConstantSDNode *N0Op0C1 = isConstOrConstSplat(N0Op0.getOperand(1))) {
      APInt c1 = N0Op0C1->getAPIntValue();
      APInt c2 = N1C->getAPIntValue();
      zeroExtendToMatch(c1, c2, 1 /* Overflow Bit */);

      EVT InnerShiftVT = N0Op0.getValueType();
      uint64_t InnerShiftSize = InnerShiftVT.getScalarSizeInBits();
      if (c2.uge(OpSizeInBits - InnerShiftSize)) {
        SDLoc DL(N0);
        APInt Sum = c1 + c2;
        if (Sum.uge(OpSizeInBits))
          return DAG.getConstant(0, DL, VT);

        return DAG.getNode(
            ISD::SHL, DL, VT,
            DAG.getNode(N0.getOpcode(), DL, VT, N0Op0->getOperand(0)),
            DAG.getConstant(Sum.getZExtValue(), DL, N1.getValueType()));
      }
    }
  }

  // fold (shl (zext (srl x, C)), C) -> (zext (shl (srl x, C), C))
  // Only fold this if the inner zext has no other uses to avoid increasing
  // the total number of instructions.
  if (N1C && N0.getOpcode() == ISD::ZERO_EXTEND && N0.hasOneUse() &&
      N0.getOperand(0).getOpcode() == ISD::SRL) {
    SDValue N0Op0 = N0.getOperand(0);
    if (ConstantSDNode *N0Op0C1 = isConstOrConstSplat(N0Op0.getOperand(1))) {
      if (N0Op0C1->getAPIntValue().ult(VT.getScalarSizeInBits())) {
        uint64_t c1 = N0Op0C1->getZExtValue();
        uint64_t c2 = N1C->getZExtValue();
        if (c1 == c2) {
          SDValue NewOp0 = N0.getOperand(0);
          EVT CountVT = NewOp0.getOperand(1).getValueType();
          SDLoc DL(N);
          SDValue NewSHL = DAG.getNode(ISD::SHL, DL, NewOp0.getValueType(),
                                       NewOp0,
                                       DAG.getConstant(c2, DL, CountVT));
          AddToWorklist(NewSHL.getNode());
          return DAG.getNode(ISD::ZERO_EXTEND, SDLoc(N0), VT, NewSHL);
        }
      }
    }
  }

  // fold (shl (sr[la] exact X,  C1), C2) -> (shl    X, (C2-C1)) if C1 <= C2
  // fold (shl (sr[la] exact X,  C1), C2) -> (sr[la] X, (C2-C1)) if C1  > C2
  if (N1C && (N0.getOpcode() == ISD::SRL || N0.getOpcode() == ISD::SRA) &&
      N0->getFlags().hasExact()) {
    if (ConstantSDNode *N0C1 = isConstOrConstSplat(N0.getOperand(1))) {
      uint64_t C1 = N0C1->getZExtValue();
      uint64_t C2 = N1C->getZExtValue();
      SDLoc DL(N);
      if (C1 <= C2)
        return DAG.getNode(ISD::SHL, DL, VT, N0.getOperand(0),
                           DAG.getConstant(C2 - C1, DL, N1.getValueType()));
      return DAG.getNode(N0.getOpcode(), DL, VT, N0.getOperand(0),
                         DAG.getConstant(C1 - C2, DL, N1.getValueType()));
    }
  }

  // fold (shl (srl x, c1), c2) -> (and (shl x, (sub c2, c1), MASK) or
  //                               (and (srl x, (sub c1, c2), MASK)
  // Only fold this if the inner shift has no other uses -- if it does, folding
  // this will increase the total number of instructions.
  if (N1C && N0.getOpcode() == ISD::SRL && N0.hasOneUse() &&
      TLI.shouldFoldShiftPairToMask(N, Level)) {
    if (ConstantSDNode *N0C1 = isConstOrConstSplat(N0.getOperand(1))) {
      uint64_t c1 = N0C1->getZExtValue();
      if (c1 < OpSizeInBits) {
        uint64_t c2 = N1C->getZExtValue();
        APInt Mask = APInt::getHighBitsSet(OpSizeInBits, OpSizeInBits - c1);
        SDValue Shift;
        if (c2 > c1) {
          Mask <<= c2 - c1;
          SDLoc DL(N);
          Shift = DAG.getNode(ISD::SHL, DL, VT, N0.getOperand(0),
                              DAG.getConstant(c2 - c1, DL, N1.getValueType()));
        } else {
          Mask.lshrInPlace(c1 - c2);
          SDLoc DL(N);
          Shift = DAG.getNode(ISD::SRL, DL, VT, N0.getOperand(0),
                              DAG.getConstant(c1 - c2, DL, N1.getValueType()));
        }
        SDLoc DL(N0);
        return DAG.getNode(ISD::AND, DL, VT, Shift,
                           DAG.getConstant(Mask, DL, VT));
      }
    }
  }

  // fold (shl (sra x, c1), c1) -> (and x, (shl -1, c1))
  if (N0.getOpcode() == ISD::SRA && N1 == N0.getOperand(1) &&
      isConstantOrConstantVector(N1, /* No Opaques */ true)) {
    SDLoc DL(N);
    SDValue AllBits = DAG.getAllOnesConstant(DL, VT);
    SDValue HiBitsMask = DAG.getNode(ISD::SHL, DL, VT, AllBits, N1);
    return DAG.getNode(ISD::AND, DL, VT, N0.getOperand(0), HiBitsMask);
  }

  // fold (shl (add x, c1), c2) -> (add (shl x, c2), c1 << c2)
  // fold (shl (or x, c1), c2) -> (or (shl x, c2), c1 << c2)
  // Variant of version done on multiply, except mul by a power of 2 is turned
  // into a shift.
  if ((N0.getOpcode() == ISD::ADD || N0.getOpcode() == ISD::OR) &&
      N0.getNode()->hasOneUse() &&
      isConstantOrConstantVector(N1, /* No Opaques */ true) &&
      isConstantOrConstantVector(N0.getOperand(1), /* No Opaques */ true) &&
      TLI.isDesirableToCommuteWithShift(N, Level)) {
    SDValue Shl0 = DAG.getNode(ISD::SHL, SDLoc(N0), VT, N0.getOperand(0), N1);
    SDValue Shl1 = DAG.getNode(ISD::SHL, SDLoc(N1), VT, N0.getOperand(1), N1);
    AddToWorklist(Shl0.getNode());
    AddToWorklist(Shl1.getNode());
    return DAG.getNode(N0.getOpcode(), SDLoc(N), VT, Shl0, Shl1);
  }

  // fold (shl (mul x, c1), c2) -> (mul x, c1 << c2)
  if (N0.getOpcode() == ISD::MUL && N0.getNode()->hasOneUse() &&
      isConstantOrConstantVector(N1, /* No Opaques */ true) &&
      isConstantOrConstantVector(N0.getOperand(1), /* No Opaques */ true)) {
    SDValue Shl = DAG.getNode(ISD::SHL, SDLoc(N1), VT, N0.getOperand(1), N1);
    if (isConstantOrConstantVector(Shl))
      return DAG.getNode(ISD::MUL, SDLoc(N), VT, N0.getOperand(0), Shl);
  }

  if (N1C && !N1C->isOpaque())
    if (SDValue NewSHL = visitShiftByConstant(N, N1C))
      return NewSHL;

  return SDValue();
}

SDValue DAGCombiner::visitSRA(SDNode *N) {
  SDValue N0 = N->getOperand(0);
  SDValue N1 = N->getOperand(1);
  if (SDValue V = DAG.simplifyShift(N0, N1))
    return V;

  EVT VT = N0.getValueType();
  unsigned OpSizeInBits = VT.getScalarSizeInBits();

  // Arithmetic shifting an all-sign-bit value is a no-op.
  // fold (sra 0, x) -> 0
  // fold (sra -1, x) -> -1
  if (DAG.ComputeNumSignBits(N0) == OpSizeInBits)
    return N0;

  // fold vector ops
  if (VT.isVector())
    if (SDValue FoldedVOp = SimplifyVBinOp(N))
      return FoldedVOp;

  ConstantSDNode *N1C = isConstOrConstSplat(N1);

  // fold (sra c1, c2) -> (sra c1, c2)
  ConstantSDNode *N0C = getAsNonOpaqueConstant(N0);
  if (N0C && N1C && !N1C->isOpaque())
    return DAG.FoldConstantArithmetic(ISD::SRA, SDLoc(N), VT, N0C, N1C);

  if (SDValue NewSel = foldBinOpIntoSelect(N))
    return NewSel;

  // fold (sra (shl x, c1), c1) -> sext_inreg for some c1 and target supports
  // sext_inreg.
  if (N1C && N0.getOpcode() == ISD::SHL && N1 == N0.getOperand(1)) {
    unsigned LowBits = OpSizeInBits - (unsigned)N1C->getZExtValue();
    EVT ExtVT = EVT::getIntegerVT(*DAG.getContext(), LowBits);
    if (VT.isVector())
      ExtVT = EVT::getVectorVT(*DAG.getContext(),
                               ExtVT, VT.getVectorNumElements());
    if ((!LegalOperations ||
         TLI.isOperationLegal(ISD::SIGN_EXTEND_INREG, ExtVT)))
      return DAG.getNode(ISD::SIGN_EXTEND_INREG, SDLoc(N), VT,
                         N0.getOperand(0), DAG.getValueType(ExtVT));
  }

  // fold (sra (sra x, c1), c2) -> (sra x, (add c1, c2))
  // clamp (add c1, c2) to max shift.
  if (N0.getOpcode() == ISD::SRA) {
    SDLoc DL(N);
    EVT ShiftVT = N1.getValueType();
    EVT ShiftSVT = ShiftVT.getScalarType();
    SmallVector<SDValue, 16> ShiftValues;

    auto SumOfShifts = [&](ConstantSDNode *LHS, ConstantSDNode *RHS) {
      APInt c1 = LHS->getAPIntValue();
      APInt c2 = RHS->getAPIntValue();
      zeroExtendToMatch(c1, c2, 1 /* Overflow Bit */);
      APInt Sum = c1 + c2;
      unsigned ShiftSum =
          Sum.uge(OpSizeInBits) ? (OpSizeInBits - 1) : Sum.getZExtValue();
      ShiftValues.push_back(DAG.getConstant(ShiftSum, DL, ShiftSVT));
      return true;
    };
    if (ISD::matchBinaryPredicate(N1, N0.getOperand(1), SumOfShifts)) {
      SDValue ShiftValue;
      if (VT.isVector())
        ShiftValue = DAG.getBuildVector(ShiftVT, DL, ShiftValues);
      else
        ShiftValue = ShiftValues[0];
      return DAG.getNode(ISD::SRA, DL, VT, N0.getOperand(0), ShiftValue);
    }
  }

  // fold (sra (shl X, m), (sub result_size, n))
  // -> (sign_extend (trunc (shl X, (sub (sub result_size, n), m)))) for
  // result_size - n != m.
  // If truncate is free for the target sext(shl) is likely to result in better
  // code.
  if (N0.getOpcode() == ISD::SHL && N1C) {
    // Get the two constanst of the shifts, CN0 = m, CN = n.
    const ConstantSDNode *N01C = isConstOrConstSplat(N0.getOperand(1));
    if (N01C) {
      LLVMContext &Ctx = *DAG.getContext();
      // Determine what the truncate's result bitsize and type would be.
      EVT TruncVT = EVT::getIntegerVT(Ctx, OpSizeInBits - N1C->getZExtValue());

      if (VT.isVector())
        TruncVT = EVT::getVectorVT(Ctx, TruncVT, VT.getVectorNumElements());

      // Determine the residual right-shift amount.
      int ShiftAmt = N1C->getZExtValue() - N01C->getZExtValue();

      // If the shift is not a no-op (in which case this should be just a sign
      // extend already), the truncated to type is legal, sign_extend is legal
      // on that type, and the truncate to that type is both legal and free,
      // perform the transform.
      if ((ShiftAmt > 0) &&
          TLI.isOperationLegalOrCustom(ISD::SIGN_EXTEND, TruncVT) &&
          TLI.isOperationLegalOrCustom(ISD::TRUNCATE, VT) &&
          TLI.isTruncateFree(VT, TruncVT)) {
        SDLoc DL(N);
        SDValue Amt = DAG.getConstant(ShiftAmt, DL,
            getShiftAmountTy(N0.getOperand(0).getValueType()));
        SDValue Shift = DAG.getNode(ISD::SRL, DL, VT,
                                    N0.getOperand(0), Amt);
        SDValue Trunc = DAG.getNode(ISD::TRUNCATE, DL, TruncVT,
                                    Shift);
        return DAG.getNode(ISD::SIGN_EXTEND, DL,
                           N->getValueType(0), Trunc);
      }
    }
  }

  // fold (sra x, (trunc (and y, c))) -> (sra x, (and (trunc y), (trunc c))).
  if (N1.getOpcode() == ISD::TRUNCATE &&
      N1.getOperand(0).getOpcode() == ISD::AND) {
    if (SDValue NewOp1 = distributeTruncateThroughAnd(N1.getNode()))
      return DAG.getNode(ISD::SRA, SDLoc(N), VT, N0, NewOp1);
  }

  // fold (sra (trunc (srl x, c1)), c2) -> (trunc (sra x, c1 + c2))
  //      if c1 is equal to the number of bits the trunc removes
  if (N0.getOpcode() == ISD::TRUNCATE &&
      (N0.getOperand(0).getOpcode() == ISD::SRL ||
       N0.getOperand(0).getOpcode() == ISD::SRA) &&
      N0.getOperand(0).hasOneUse() &&
      N0.getOperand(0).getOperand(1).hasOneUse() &&
      N1C) {
    SDValue N0Op0 = N0.getOperand(0);
    if (ConstantSDNode *LargeShift = isConstOrConstSplat(N0Op0.getOperand(1))) {
      unsigned LargeShiftVal = LargeShift->getZExtValue();
      EVT LargeVT = N0Op0.getValueType();

      if (LargeVT.getScalarSizeInBits() - OpSizeInBits == LargeShiftVal) {
        SDLoc DL(N);
        SDValue Amt =
          DAG.getConstant(LargeShiftVal + N1C->getZExtValue(), DL,
                          getShiftAmountTy(N0Op0.getOperand(0).getValueType()));
        SDValue SRA = DAG.getNode(ISD::SRA, DL, LargeVT,
                                  N0Op0.getOperand(0), Amt);
        return DAG.getNode(ISD::TRUNCATE, DL, VT, SRA);
      }
    }
  }

  // Simplify, based on bits shifted out of the LHS.
  if (N1C && SimplifyDemandedBits(SDValue(N, 0)))
    return SDValue(N, 0);

  // If the sign bit is known to be zero, switch this to a SRL.
  if (DAG.SignBitIsZero(N0))
    return DAG.getNode(ISD::SRL, SDLoc(N), VT, N0, N1);

  if (N1C && !N1C->isOpaque())
    if (SDValue NewSRA = visitShiftByConstant(N, N1C))
      return NewSRA;

  return SDValue();
}

SDValue DAGCombiner::visitSRL(SDNode *N) {
  SDValue N0 = N->getOperand(0);
  SDValue N1 = N->getOperand(1);
  if (SDValue V = DAG.simplifyShift(N0, N1))
    return V;

  EVT VT = N0.getValueType();
  unsigned OpSizeInBits = VT.getScalarSizeInBits();

  // fold vector ops
  if (VT.isVector())
    if (SDValue FoldedVOp = SimplifyVBinOp(N))
      return FoldedVOp;

  ConstantSDNode *N1C = isConstOrConstSplat(N1);

  // fold (srl c1, c2) -> c1 >>u c2
  ConstantSDNode *N0C = getAsNonOpaqueConstant(N0);
  if (N0C && N1C && !N1C->isOpaque())
    return DAG.FoldConstantArithmetic(ISD::SRL, SDLoc(N), VT, N0C, N1C);

  if (SDValue NewSel = foldBinOpIntoSelect(N))
    return NewSel;

  // if (srl x, c) is known to be zero, return 0
  if (N1C && DAG.MaskedValueIsZero(SDValue(N, 0),
                                   APInt::getAllOnesValue(OpSizeInBits)))
    return DAG.getConstant(0, SDLoc(N), VT);

  // fold (srl (srl x, c1), c2) -> 0 or (srl x, (add c1, c2))
  if (N0.getOpcode() == ISD::SRL) {
    auto MatchOutOfRange = [OpSizeInBits](ConstantSDNode *LHS,
                                          ConstantSDNode *RHS) {
      APInt c1 = LHS->getAPIntValue();
      APInt c2 = RHS->getAPIntValue();
      zeroExtendToMatch(c1, c2, 1 /* Overflow Bit */);
      return (c1 + c2).uge(OpSizeInBits);
    };
    if (ISD::matchBinaryPredicate(N1, N0.getOperand(1), MatchOutOfRange))
      return DAG.getConstant(0, SDLoc(N), VT);

    auto MatchInRange = [OpSizeInBits](ConstantSDNode *LHS,
                                       ConstantSDNode *RHS) {
      APInt c1 = LHS->getAPIntValue();
      APInt c2 = RHS->getAPIntValue();
      zeroExtendToMatch(c1, c2, 1 /* Overflow Bit */);
      return (c1 + c2).ult(OpSizeInBits);
    };
    if (ISD::matchBinaryPredicate(N1, N0.getOperand(1), MatchInRange)) {
      SDLoc DL(N);
      EVT ShiftVT = N1.getValueType();
      SDValue Sum = DAG.getNode(ISD::ADD, DL, ShiftVT, N1, N0.getOperand(1));
      return DAG.getNode(ISD::SRL, DL, VT, N0.getOperand(0), Sum);
    }
  }

  // fold (srl (trunc (srl x, c1)), c2) -> 0 or (trunc (srl x, (add c1, c2)))
  if (N1C && N0.getOpcode() == ISD::TRUNCATE &&
      N0.getOperand(0).getOpcode() == ISD::SRL) {
    if (auto N001C = isConstOrConstSplat(N0.getOperand(0).getOperand(1))) {
      uint64_t c1 = N001C->getZExtValue();
      uint64_t c2 = N1C->getZExtValue();
      EVT InnerShiftVT = N0.getOperand(0).getValueType();
      EVT ShiftCountVT = N0.getOperand(0).getOperand(1).getValueType();
      uint64_t InnerShiftSize = InnerShiftVT.getScalarSizeInBits();
      // This is only valid if the OpSizeInBits + c1 = size of inner shift.
      if (c1 + OpSizeInBits == InnerShiftSize) {
        SDLoc DL(N0);
        if (c1 + c2 >= InnerShiftSize)
          return DAG.getConstant(0, DL, VT);
        return DAG.getNode(ISD::TRUNCATE, DL, VT,
                           DAG.getNode(ISD::SRL, DL, InnerShiftVT,
                                       N0.getOperand(0).getOperand(0),
                                       DAG.getConstant(c1 + c2, DL,
                                                       ShiftCountVT)));
      }
    }
  }

  // fold (srl (shl x, c), c) -> (and x, cst2)
  if (N0.getOpcode() == ISD::SHL && N0.getOperand(1) == N1 &&
      isConstantOrConstantVector(N1, /* NoOpaques */ true)) {
    SDLoc DL(N);
    SDValue Mask =
        DAG.getNode(ISD::SRL, DL, VT, DAG.getAllOnesConstant(DL, VT), N1);
    AddToWorklist(Mask.getNode());
    return DAG.getNode(ISD::AND, DL, VT, N0.getOperand(0), Mask);
  }

  // fold (srl (anyextend x), c) -> (and (anyextend (srl x, c)), mask)
  if (N1C && N0.getOpcode() == ISD::ANY_EXTEND) {
    // Shifting in all undef bits?
    EVT SmallVT = N0.getOperand(0).getValueType();
    unsigned BitSize = SmallVT.getScalarSizeInBits();
    if (N1C->getZExtValue() >= BitSize)
      return DAG.getUNDEF(VT);

    if (!LegalTypes || TLI.isTypeDesirableForOp(ISD::SRL, SmallVT)) {
      uint64_t ShiftAmt = N1C->getZExtValue();
      SDLoc DL0(N0);
      SDValue SmallShift = DAG.getNode(ISD::SRL, DL0, SmallVT,
                                       N0.getOperand(0),
                          DAG.getConstant(ShiftAmt, DL0,
                                          getShiftAmountTy(SmallVT)));
      AddToWorklist(SmallShift.getNode());
      APInt Mask = APInt::getLowBitsSet(OpSizeInBits, OpSizeInBits - ShiftAmt);
      SDLoc DL(N);
      return DAG.getNode(ISD::AND, DL, VT,
                         DAG.getNode(ISD::ANY_EXTEND, DL, VT, SmallShift),
                         DAG.getConstant(Mask, DL, VT));
    }
  }

  // fold (srl (sra X, Y), 31) -> (srl X, 31).  This srl only looks at the sign
  // bit, which is unmodified by sra.
  if (N1C && N1C->getZExtValue() + 1 == OpSizeInBits) {
    if (N0.getOpcode() == ISD::SRA)
      return DAG.getNode(ISD::SRL, SDLoc(N), VT, N0.getOperand(0), N1);
  }

  // fold (srl (ctlz x), "5") -> x  iff x has one bit set (the low bit).
  if (N1C && N0.getOpcode() == ISD::CTLZ &&
      N1C->getAPIntValue() == Log2_32(OpSizeInBits)) {
    KnownBits Known = DAG.computeKnownBits(N0.getOperand(0));

    // If any of the input bits are KnownOne, then the input couldn't be all
    // zeros, thus the result of the srl will always be zero.
    if (Known.One.getBoolValue()) return DAG.getConstant(0, SDLoc(N0), VT);

    // If all of the bits input the to ctlz node are known to be zero, then
    // the result of the ctlz is "32" and the result of the shift is one.
    APInt UnknownBits = ~Known.Zero;
    if (UnknownBits == 0) return DAG.getConstant(1, SDLoc(N0), VT);

    // Otherwise, check to see if there is exactly one bit input to the ctlz.
    if (UnknownBits.isPowerOf2()) {
      // Okay, we know that only that the single bit specified by UnknownBits
      // could be set on input to the CTLZ node. If this bit is set, the SRL
      // will return 0, if it is clear, it returns 1. Change the CTLZ/SRL pair
      // to an SRL/XOR pair, which is likely to simplify more.
      unsigned ShAmt = UnknownBits.countTrailingZeros();
      SDValue Op = N0.getOperand(0);

      if (ShAmt) {
        SDLoc DL(N0);
        Op = DAG.getNode(ISD::SRL, DL, VT, Op,
                  DAG.getConstant(ShAmt, DL,
                                  getShiftAmountTy(Op.getValueType())));
        AddToWorklist(Op.getNode());
      }

      SDLoc DL(N);
      return DAG.getNode(ISD::XOR, DL, VT,
                         Op, DAG.getConstant(1, DL, VT));
    }
  }

  // fold (srl x, (trunc (and y, c))) -> (srl x, (and (trunc y), (trunc c))).
  if (N1.getOpcode() == ISD::TRUNCATE &&
      N1.getOperand(0).getOpcode() == ISD::AND) {
    if (SDValue NewOp1 = distributeTruncateThroughAnd(N1.getNode()))
      return DAG.getNode(ISD::SRL, SDLoc(N), VT, N0, NewOp1);
  }

  // fold operands of srl based on knowledge that the low bits are not
  // demanded.
  if (N1C && SimplifyDemandedBits(SDValue(N, 0)))
    return SDValue(N, 0);

  if (N1C && !N1C->isOpaque())
    if (SDValue NewSRL = visitShiftByConstant(N, N1C))
      return NewSRL;

  // Attempt to convert a srl of a load into a narrower zero-extending load.
  if (SDValue NarrowLoad = ReduceLoadWidth(N))
    return NarrowLoad;

  // Here is a common situation. We want to optimize:
  //
  //   %a = ...
  //   %b = and i32 %a, 2
  //   %c = srl i32 %b, 1
  //   brcond i32 %c ...
  //
  // into
  //
  //   %a = ...
  //   %b = and %a, 2
  //   %c = setcc eq %b, 0
  //   brcond %c ...
  //
  // However when after the source operand of SRL is optimized into AND, the SRL
  // itself may not be optimized further. Look for it and add the BRCOND into
  // the worklist.
  if (N->hasOneUse()) {
    SDNode *Use = *N->use_begin();
    if (Use->getOpcode() == ISD::BRCOND)
      AddToWorklist(Use);
    else if (Use->getOpcode() == ISD::TRUNCATE && Use->hasOneUse()) {
      // Also look pass the truncate.
      Use = *Use->use_begin();
      if (Use->getOpcode() == ISD::BRCOND)
        AddToWorklist(Use);
    }
  }

  return SDValue();
}

SDValue DAGCombiner::visitFunnelShift(SDNode *N) {
  EVT VT = N->getValueType(0);
  SDValue N0 = N->getOperand(0);
  SDValue N1 = N->getOperand(1);
  SDValue N2 = N->getOperand(2);
  bool IsFSHL = N->getOpcode() == ISD::FSHL;
  unsigned BitWidth = VT.getScalarSizeInBits();

  // fold (fshl N0, N1, 0) -> N0
  // fold (fshr N0, N1, 0) -> N1
  if (isPowerOf2_32(BitWidth))
    if (DAG.MaskedValueIsZero(
            N2, APInt(N2.getScalarValueSizeInBits(), BitWidth - 1)))
      return IsFSHL ? N0 : N1;

  // fold (fsh* N0, N1, c) -> (fsh* N0, N1, c % BitWidth)
  if (ConstantSDNode *Cst = isConstOrConstSplat(N2)) {
    if (Cst->getAPIntValue().uge(BitWidth)) {
      uint64_t RotAmt = Cst->getAPIntValue().urem(BitWidth);
      return DAG.getNode(N->getOpcode(), SDLoc(N), VT, N0, N1,
                         DAG.getConstant(RotAmt, SDLoc(N), N2.getValueType()));
    }
  }

  // fold (fshl N0, N0, N2) -> (rotl N0, N2)
  // fold (fshr N0, N0, N2) -> (rotr N0, N2)
  // TODO: Investigate flipping this rotate if only one is legal, if funnel shift
  // is legal as well we might be better off avoiding non-constant (BW - N2).
  unsigned RotOpc = IsFSHL ? ISD::ROTL : ISD::ROTR;
  if (N0 == N1 && hasOperation(RotOpc, VT))
    return DAG.getNode(RotOpc, SDLoc(N), VT, N0, N2);

  return SDValue();
}

SDValue DAGCombiner::visitABS(SDNode *N) {
  SDValue N0 = N->getOperand(0);
  EVT VT = N->getValueType(0);

  // fold (abs c1) -> c2
  if (DAG.isConstantIntBuildVectorOrConstantInt(N0))
    return DAG.getNode(ISD::ABS, SDLoc(N), VT, N0);
  // fold (abs (abs x)) -> (abs x)
  if (N0.getOpcode() == ISD::ABS)
    return N0;
  // fold (abs x) -> x iff not-negative
  if (DAG.SignBitIsZero(N0))
    return N0;
  return SDValue();
}

SDValue DAGCombiner::visitBSWAP(SDNode *N) {
  SDValue N0 = N->getOperand(0);
  EVT VT = N->getValueType(0);

  // fold (bswap c1) -> c2
  if (DAG.isConstantIntBuildVectorOrConstantInt(N0))
    return DAG.getNode(ISD::BSWAP, SDLoc(N), VT, N0);
  // fold (bswap (bswap x)) -> x
  if (N0.getOpcode() == ISD::BSWAP)
    return N0->getOperand(0);
  return SDValue();
}

SDValue DAGCombiner::visitBITREVERSE(SDNode *N) {
  SDValue N0 = N->getOperand(0);
  EVT VT = N->getValueType(0);

  // fold (bitreverse c1) -> c2
  if (DAG.isConstantIntBuildVectorOrConstantInt(N0))
    return DAG.getNode(ISD::BITREVERSE, SDLoc(N), VT, N0);
  // fold (bitreverse (bitreverse x)) -> x
  if (N0.getOpcode() == ISD::BITREVERSE)
    return N0.getOperand(0);
  return SDValue();
}

SDValue DAGCombiner::visitCTLZ(SDNode *N) {
  SDValue N0 = N->getOperand(0);
  EVT VT = N->getValueType(0);

  // fold (ctlz c1) -> c2
  if (DAG.isConstantIntBuildVectorOrConstantInt(N0))
    return DAG.getNode(ISD::CTLZ, SDLoc(N), VT, N0);

  // If the value is known never to be zero, switch to the undef version.
  if (!LegalOperations || TLI.isOperationLegal(ISD::CTLZ_ZERO_UNDEF, VT)) {
    if (DAG.isKnownNeverZero(N0))
      return DAG.getNode(ISD::CTLZ_ZERO_UNDEF, SDLoc(N), VT, N0);
  }

  return SDValue();
}

SDValue DAGCombiner::visitCTLZ_ZERO_UNDEF(SDNode *N) {
  SDValue N0 = N->getOperand(0);
  EVT VT = N->getValueType(0);

  // fold (ctlz_zero_undef c1) -> c2
  if (DAG.isConstantIntBuildVectorOrConstantInt(N0))
    return DAG.getNode(ISD::CTLZ_ZERO_UNDEF, SDLoc(N), VT, N0);
  return SDValue();
}

SDValue DAGCombiner::visitCTTZ(SDNode *N) {
  SDValue N0 = N->getOperand(0);
  EVT VT = N->getValueType(0);

  // fold (cttz c1) -> c2
  if (DAG.isConstantIntBuildVectorOrConstantInt(N0))
    return DAG.getNode(ISD::CTTZ, SDLoc(N), VT, N0);

  // If the value is known never to be zero, switch to the undef version.
  if (!LegalOperations || TLI.isOperationLegal(ISD::CTTZ_ZERO_UNDEF, VT)) {
    if (DAG.isKnownNeverZero(N0))
      return DAG.getNode(ISD::CTTZ_ZERO_UNDEF, SDLoc(N), VT, N0);
  }

  return SDValue();
}

SDValue DAGCombiner::visitCTTZ_ZERO_UNDEF(SDNode *N) {
  SDValue N0 = N->getOperand(0);
  EVT VT = N->getValueType(0);

  // fold (cttz_zero_undef c1) -> c2
  if (DAG.isConstantIntBuildVectorOrConstantInt(N0))
    return DAG.getNode(ISD::CTTZ_ZERO_UNDEF, SDLoc(N), VT, N0);
  return SDValue();
}

SDValue DAGCombiner::visitCTPOP(SDNode *N) {
  SDValue N0 = N->getOperand(0);
  EVT VT = N->getValueType(0);

  // fold (ctpop c1) -> c2
  if (DAG.isConstantIntBuildVectorOrConstantInt(N0))
    return DAG.getNode(ISD::CTPOP, SDLoc(N), VT, N0);
  return SDValue();
}

// FIXME: This should be checking for no signed zeros on individual operands, as
// well as no nans.
static bool isLegalToCombineMinNumMaxNum(SelectionDAG &DAG, SDValue LHS, SDValue RHS) {
  const TargetOptions &Options = DAG.getTarget().Options;
  EVT VT = LHS.getValueType();

  return Options.NoSignedZerosFPMath && VT.isFloatingPoint() &&
         DAG.isKnownNeverNaN(LHS) && DAG.isKnownNeverNaN(RHS);
}

/// Generate Min/Max node
static SDValue combineMinNumMaxNum(const SDLoc &DL, EVT VT, SDValue LHS,
                                   SDValue RHS, SDValue True, SDValue False,
                                   ISD::CondCode CC, const TargetLowering &TLI,
                                   SelectionDAG &DAG) {
  if (!(LHS == True && RHS == False) && !(LHS == False && RHS == True))
    return SDValue();

  EVT TransformVT = TLI.getTypeToTransformTo(*DAG.getContext(), VT);
  switch (CC) {
  case ISD::SETOLT:
  case ISD::SETOLE:
  case ISD::SETLT:
  case ISD::SETLE:
  case ISD::SETULT:
  case ISD::SETULE: {
    // Since it's known never nan to get here already, either fminnum or
    // fminnum_ieee are OK. Try the ieee version first, since it's fminnum is
    // expanded in terms of it.
    unsigned IEEEOpcode = (LHS == True) ? ISD::FMINNUM_IEEE : ISD::FMAXNUM_IEEE;
    if (TLI.isOperationLegalOrCustom(IEEEOpcode, VT))
      return DAG.getNode(IEEEOpcode, DL, VT, LHS, RHS);

    unsigned Opcode = (LHS == True) ? ISD::FMINNUM : ISD::FMAXNUM;
    if (TLI.isOperationLegalOrCustom(Opcode, TransformVT))
      return DAG.getNode(Opcode, DL, VT, LHS, RHS);
    return SDValue();
  }
  case ISD::SETOGT:
  case ISD::SETOGE:
  case ISD::SETGT:
  case ISD::SETGE:
  case ISD::SETUGT:
  case ISD::SETUGE: {
    unsigned IEEEOpcode = (LHS == True) ? ISD::FMAXNUM_IEEE : ISD::FMINNUM_IEEE;
    if (TLI.isOperationLegalOrCustom(IEEEOpcode, VT))
      return DAG.getNode(IEEEOpcode, DL, VT, LHS, RHS);

    unsigned Opcode = (LHS == True) ? ISD::FMAXNUM : ISD::FMINNUM;
    if (TLI.isOperationLegalOrCustom(Opcode, TransformVT))
      return DAG.getNode(Opcode, DL, VT, LHS, RHS);
    return SDValue();
  }
  default:
    return SDValue();
  }
}

SDValue DAGCombiner::foldSelectOfConstants(SDNode *N) {
  SDValue Cond = N->getOperand(0);
  SDValue N1 = N->getOperand(1);
  SDValue N2 = N->getOperand(2);
  EVT VT = N->getValueType(0);
  EVT CondVT = Cond.getValueType();
  SDLoc DL(N);

  if (!VT.isInteger())
    return SDValue();

  auto *C1 = dyn_cast<ConstantSDNode>(N1);
  auto *C2 = dyn_cast<ConstantSDNode>(N2);
  if (!C1 || !C2)
    return SDValue();

  // Only do this before legalization to avoid conflicting with target-specific
  // transforms in the other direction (create a select from a zext/sext). There
  // is also a target-independent combine here in DAGCombiner in the other
  // direction for (select Cond, -1, 0) when the condition is not i1.
  if (CondVT == MVT::i1 && !LegalOperations) {
    if (C1->isNullValue() && C2->isOne()) {
      // select Cond, 0, 1 --> zext (!Cond)
      SDValue NotCond = DAG.getNOT(DL, Cond, MVT::i1);
      if (VT != MVT::i1)
        NotCond = DAG.getNode(ISD::ZERO_EXTEND, DL, VT, NotCond);
      return NotCond;
    }
    if (C1->isNullValue() && C2->isAllOnesValue()) {
      // select Cond, 0, -1 --> sext (!Cond)
      SDValue NotCond = DAG.getNOT(DL, Cond, MVT::i1);
      if (VT != MVT::i1)
        NotCond = DAG.getNode(ISD::SIGN_EXTEND, DL, VT, NotCond);
      return NotCond;
    }
    if (C1->isOne() && C2->isNullValue()) {
      // select Cond, 1, 0 --> zext (Cond)
      if (VT != MVT::i1)
        Cond = DAG.getNode(ISD::ZERO_EXTEND, DL, VT, Cond);
      return Cond;
    }
    if (C1->isAllOnesValue() && C2->isNullValue()) {
      // select Cond, -1, 0 --> sext (Cond)
      if (VT != MVT::i1)
        Cond = DAG.getNode(ISD::SIGN_EXTEND, DL, VT, Cond);
      return Cond;
    }

    // For any constants that differ by 1, we can transform the select into an
    // extend and add. Use a target hook because some targets may prefer to
    // transform in the other direction.
    if (TLI.convertSelectOfConstantsToMath(VT)) {
      if (C1->getAPIntValue() - 1 == C2->getAPIntValue()) {
        // select Cond, C1, C1-1 --> add (zext Cond), C1-1
        if (VT != MVT::i1)
          Cond = DAG.getNode(ISD::ZERO_EXTEND, DL, VT, Cond);
        return DAG.getNode(ISD::ADD, DL, VT, Cond, N2);
      }
      if (C1->getAPIntValue() + 1 == C2->getAPIntValue()) {
        // select Cond, C1, C1+1 --> add (sext Cond), C1+1
        if (VT != MVT::i1)
          Cond = DAG.getNode(ISD::SIGN_EXTEND, DL, VT, Cond);
        return DAG.getNode(ISD::ADD, DL, VT, Cond, N2);
      }
    }

    return SDValue();
  }

  // fold (select Cond, 0, 1) -> (xor Cond, 1)
  // We can't do this reliably if integer based booleans have different contents
  // to floating point based booleans. This is because we can't tell whether we
  // have an integer-based boolean or a floating-point-based boolean unless we
  // can find the SETCC that produced it and inspect its operands. This is
  // fairly easy if C is the SETCC node, but it can potentially be
  // undiscoverable (or not reasonably discoverable). For example, it could be
  // in another basic block or it could require searching a complicated
  // expression.
  if (CondVT.isInteger() &&
      TLI.getBooleanContents(/*isVec*/false, /*isFloat*/true) ==
          TargetLowering::ZeroOrOneBooleanContent &&
      TLI.getBooleanContents(/*isVec*/false, /*isFloat*/false) ==
          TargetLowering::ZeroOrOneBooleanContent &&
      C1->isNullValue() && C2->isOne()) {
    SDValue NotCond =
        DAG.getNode(ISD::XOR, DL, CondVT, Cond, DAG.getConstant(1, DL, CondVT));
    if (VT.bitsEq(CondVT))
      return NotCond;
    return DAG.getZExtOrTrunc(NotCond, DL, VT);
  }

  return SDValue();
}

SDValue DAGCombiner::visitSELECT(SDNode *N) {
  SDValue N0 = N->getOperand(0);
  SDValue N1 = N->getOperand(1);
  SDValue N2 = N->getOperand(2);
  EVT VT = N->getValueType(0);
  EVT VT0 = N0.getValueType();
  SDLoc DL(N);

  if (SDValue V = DAG.simplifySelect(N0, N1, N2))
    return V;

  // fold (select X, X, Y) -> (or X, Y)
  // fold (select X, 1, Y) -> (or C, Y)
  if (VT == VT0 && VT == MVT::i1 && (N0 == N1 || isOneConstant(N1)))
    return DAG.getNode(ISD::OR, DL, VT, N0, N2);

  if (SDValue V = foldSelectOfConstants(N))
    return V;

  // fold (select C, 0, X) -> (and (not C), X)
  if (VT == VT0 && VT == MVT::i1 && isNullConstant(N1)) {
    SDValue NOTNode = DAG.getNOT(SDLoc(N0), N0, VT);
    AddToWorklist(NOTNode.getNode());
    return DAG.getNode(ISD::AND, DL, VT, NOTNode, N2);
  }
  // fold (select C, X, 1) -> (or (not C), X)
  if (VT == VT0 && VT == MVT::i1 && isOneConstant(N2)) {
    SDValue NOTNode = DAG.getNOT(SDLoc(N0), N0, VT);
    AddToWorklist(NOTNode.getNode());
    return DAG.getNode(ISD::OR, DL, VT, NOTNode, N1);
  }
  // fold (select X, Y, X) -> (and X, Y)
  // fold (select X, Y, 0) -> (and X, Y)
  if (VT == VT0 && VT == MVT::i1 && (N0 == N2 || isNullConstant(N2)))
    return DAG.getNode(ISD::AND, DL, VT, N0, N1);

  // If we can fold this based on the true/false value, do so.
  if (SimplifySelectOps(N, N1, N2))
    return SDValue(N, 0); // Don't revisit N.

  if (VT0 == MVT::i1) {
    // The code in this block deals with the following 2 equivalences:
    //    select(C0|C1, x, y) <=> select(C0, x, select(C1, x, y))
    //    select(C0&C1, x, y) <=> select(C0, select(C1, x, y), y)
    // The target can specify its preferred form with the
    // shouldNormalizeToSelectSequence() callback. However we always transform
    // to the right anyway if we find the inner select exists in the DAG anyway
    // and we always transform to the left side if we know that we can further
    // optimize the combination of the conditions.
    bool normalizeToSequence =
        TLI.shouldNormalizeToSelectSequence(*DAG.getContext(), VT);
    // select (and Cond0, Cond1), X, Y
    //   -> select Cond0, (select Cond1, X, Y), Y
    if (N0->getOpcode() == ISD::AND && N0->hasOneUse()) {
      SDValue Cond0 = N0->getOperand(0);
      SDValue Cond1 = N0->getOperand(1);
      SDValue InnerSelect =
          DAG.getNode(ISD::SELECT, DL, N1.getValueType(), Cond1, N1, N2);
      if (normalizeToSequence || !InnerSelect.use_empty())
        return DAG.getNode(ISD::SELECT, DL, N1.getValueType(), Cond0,
                           InnerSelect, N2);
    }
    // select (or Cond0, Cond1), X, Y -> select Cond0, X, (select Cond1, X, Y)
    if (N0->getOpcode() == ISD::OR && N0->hasOneUse()) {
      SDValue Cond0 = N0->getOperand(0);
      SDValue Cond1 = N0->getOperand(1);
      SDValue InnerSelect =
          DAG.getNode(ISD::SELECT, DL, N1.getValueType(), Cond1, N1, N2);
      if (normalizeToSequence || !InnerSelect.use_empty())
        return DAG.getNode(ISD::SELECT, DL, N1.getValueType(), Cond0, N1,
                           InnerSelect);
    }

    // select Cond0, (select Cond1, X, Y), Y -> select (and Cond0, Cond1), X, Y
    if (N1->getOpcode() == ISD::SELECT && N1->hasOneUse()) {
      SDValue N1_0 = N1->getOperand(0);
      SDValue N1_1 = N1->getOperand(1);
      SDValue N1_2 = N1->getOperand(2);
      if (N1_2 == N2 && N0.getValueType() == N1_0.getValueType()) {
        // Create the actual and node if we can generate good code for it.
        if (!normalizeToSequence) {
          SDValue And = DAG.getNode(ISD::AND, DL, N0.getValueType(), N0, N1_0);
          return DAG.getNode(ISD::SELECT, DL, N1.getValueType(), And, N1_1, N2);
        }
        // Otherwise see if we can optimize the "and" to a better pattern.
        if (SDValue Combined = visitANDLike(N0, N1_0, N))
          return DAG.getNode(ISD::SELECT, DL, N1.getValueType(), Combined, N1_1,
                             N2);
      }
    }
    // select Cond0, X, (select Cond1, X, Y) -> select (or Cond0, Cond1), X, Y
    if (N2->getOpcode() == ISD::SELECT && N2->hasOneUse()) {
      SDValue N2_0 = N2->getOperand(0);
      SDValue N2_1 = N2->getOperand(1);
      SDValue N2_2 = N2->getOperand(2);
      if (N2_1 == N1 && N0.getValueType() == N2_0.getValueType()) {
        // Create the actual or node if we can generate good code for it.
        if (!normalizeToSequence) {
          SDValue Or = DAG.getNode(ISD::OR, DL, N0.getValueType(), N0, N2_0);
          return DAG.getNode(ISD::SELECT, DL, N1.getValueType(), Or, N1, N2_2);
        }
        // Otherwise see if we can optimize to a better pattern.
        if (SDValue Combined = visitORLike(N0, N2_0, N))
          return DAG.getNode(ISD::SELECT, DL, N1.getValueType(), Combined, N1,
                             N2_2);
      }
    }
  }

  if (VT0 == MVT::i1) {
    // select (not Cond), N1, N2 -> select Cond, N2, N1
    if (isBitwiseNot(N0))
      return DAG.getNode(ISD::SELECT, DL, VT, N0->getOperand(0), N2, N1);
  }

  // Fold selects based on a setcc into other things, such as min/max/abs.
  if (N0.getOpcode() == ISD::SETCC) {
    SDValue Cond0 = N0.getOperand(0), Cond1 = N0.getOperand(1);
    ISD::CondCode CC = cast<CondCodeSDNode>(N0.getOperand(2))->get();

    // select (fcmp lt x, y), x, y -> fminnum x, y
    // select (fcmp gt x, y), x, y -> fmaxnum x, y
    //
    // This is OK if we don't care what happens if either operand is a NaN.
    if (N0.hasOneUse() && isLegalToCombineMinNumMaxNum(DAG, N1, N2))
      if (SDValue FMinMax = combineMinNumMaxNum(DL, VT, Cond0, Cond1, N1, N2,
                                                CC, TLI, DAG))
        return FMinMax;

    // Use 'unsigned add with overflow' to optimize an unsigned saturating add.
    // This is conservatively limited to pre-legal-operations to give targets
    // a chance to reverse the transform if they want to do that. Also, it is
    // unlikely that the pattern would be formed late, so it's probably not
    // worth going through the other checks.
    if (!LegalOperations && TLI.isOperationLegalOrCustom(ISD::UADDO, VT) &&
        CC == ISD::SETUGT && N0.hasOneUse() && isAllOnesConstant(N1) &&
        N2.getOpcode() == ISD::ADD && Cond0 == N2.getOperand(0)) {
      auto *C = dyn_cast<ConstantSDNode>(N2.getOperand(1));
      auto *NotC = dyn_cast<ConstantSDNode>(Cond1);
      if (C && NotC && C->getAPIntValue() == ~NotC->getAPIntValue()) {
        // select (setcc Cond0, ~C, ugt), -1, (add Cond0, C) -->
        // uaddo Cond0, C; select uaddo.1, -1, uaddo.0
        //
        // The IR equivalent of this transform would have this form:
        //   %a = add %x, C
        //   %c = icmp ugt %x, ~C
        //   %r = select %c, -1, %a
        //   =>
        //   %u = call {iN,i1} llvm.uadd.with.overflow(%x, C)
        //   %u0 = extractvalue %u, 0
        //   %u1 = extractvalue %u, 1
        //   %r = select %u1, -1, %u0
        SDVTList VTs = DAG.getVTList(VT, VT0);
        SDValue UAO = DAG.getNode(ISD::UADDO, DL, VTs, Cond0, N2.getOperand(1));
        return DAG.getSelect(DL, VT, UAO.getValue(1), N1, UAO.getValue(0));
      }
    }

    if (TLI.isOperationLegal(ISD::SELECT_CC, VT) ||
        (!LegalOperations && TLI.isOperationLegalOrCustom(ISD::SELECT_CC, VT)))
      return DAG.getNode(ISD::SELECT_CC, DL, VT, Cond0, Cond1, N1, N2,
                         N0.getOperand(2));

    return SimplifySelect(DL, N0, N1, N2);
  }

  return SDValue();
}

static
std::pair<SDValue, SDValue> SplitVSETCC(const SDNode *N, SelectionDAG &DAG) {
  SDLoc DL(N);
  EVT LoVT, HiVT;
  std::tie(LoVT, HiVT) = DAG.GetSplitDestVTs(N->getValueType(0));

  // Split the inputs.
  SDValue Lo, Hi, LL, LH, RL, RH;
  std::tie(LL, LH) = DAG.SplitVectorOperand(N, 0);
  std::tie(RL, RH) = DAG.SplitVectorOperand(N, 1);

  Lo = DAG.getNode(N->getOpcode(), DL, LoVT, LL, RL, N->getOperand(2));
  Hi = DAG.getNode(N->getOpcode(), DL, HiVT, LH, RH, N->getOperand(2));

  return std::make_pair(Lo, Hi);
}

// This function assumes all the vselect's arguments are CONCAT_VECTOR
// nodes and that the condition is a BV of ConstantSDNodes (or undefs).
static SDValue ConvertSelectToConcatVector(SDNode *N, SelectionDAG &DAG) {
  SDLoc DL(N);
  SDValue Cond = N->getOperand(0);
  SDValue LHS = N->getOperand(1);
  SDValue RHS = N->getOperand(2);
  EVT VT = N->getValueType(0);
  int NumElems = VT.getVectorNumElements();
  assert(LHS.getOpcode() == ISD::CONCAT_VECTORS &&
         RHS.getOpcode() == ISD::CONCAT_VECTORS &&
         Cond.getOpcode() == ISD::BUILD_VECTOR);

  // CONCAT_VECTOR can take an arbitrary number of arguments. We only care about
  // binary ones here.
  if (LHS->getNumOperands() != 2 || RHS->getNumOperands() != 2)
    return SDValue();

  // We're sure we have an even number of elements due to the
  // concat_vectors we have as arguments to vselect.
  // Skip BV elements until we find one that's not an UNDEF
  // After we find an UNDEF element, keep looping until we get to half the
  // length of the BV and see if all the non-undef nodes are the same.
  ConstantSDNode *BottomHalf = nullptr;
  for (int i = 0; i < NumElems / 2; ++i) {
    if (Cond->getOperand(i)->isUndef())
      continue;

    if (BottomHalf == nullptr)
      BottomHalf = cast<ConstantSDNode>(Cond.getOperand(i));
    else if (Cond->getOperand(i).getNode() != BottomHalf)
      return SDValue();
  }

  // Do the same for the second half of the BuildVector
  ConstantSDNode *TopHalf = nullptr;
  for (int i = NumElems / 2; i < NumElems; ++i) {
    if (Cond->getOperand(i)->isUndef())
      continue;

    if (TopHalf == nullptr)
      TopHalf = cast<ConstantSDNode>(Cond.getOperand(i));
    else if (Cond->getOperand(i).getNode() != TopHalf)
      return SDValue();
  }

  assert(TopHalf && BottomHalf &&
         "One half of the selector was all UNDEFs and the other was all the "
         "same value. This should have been addressed before this function.");
  return DAG.getNode(
      ISD::CONCAT_VECTORS, DL, VT,
      BottomHalf->isNullValue() ? RHS->getOperand(0) : LHS->getOperand(0),
      TopHalf->isNullValue() ? RHS->getOperand(1) : LHS->getOperand(1));
}

SDValue DAGCombiner::visitMSCATTER(SDNode *N) {
  if (Level >= AfterLegalizeTypes)
    return SDValue();

  MaskedScatterSDNode *MSC = cast<MaskedScatterSDNode>(N);
  SDValue Mask = MSC->getMask();
  SDValue Data  = MSC->getValue();
  SDLoc DL(N);

  // If the MSCATTER data type requires splitting and the mask is provided by a
  // SETCC, then split both nodes and its operands before legalization. This
  // prevents the type legalizer from unrolling SETCC into scalar comparisons
  // and enables future optimizations (e.g. min/max pattern matching on X86).
  if (Mask.getOpcode() != ISD::SETCC)
    return SDValue();

  // Check if any splitting is required.
  if (TLI.getTypeAction(*DAG.getContext(), Data.getValueType()) !=
      TargetLowering::TypeSplitVector)
    return SDValue();
  SDValue MaskLo, MaskHi;
  std::tie(MaskLo, MaskHi) = SplitVSETCC(Mask.getNode(), DAG);

  EVT LoVT, HiVT;
  std::tie(LoVT, HiVT) = DAG.GetSplitDestVTs(MSC->getValueType(0));

  SDValue Chain = MSC->getChain();

  EVT MemoryVT = MSC->getMemoryVT();
  unsigned Alignment = MSC->getOriginalAlignment();

  EVT LoMemVT, HiMemVT;
  std::tie(LoMemVT, HiMemVT) = DAG.GetSplitDestVTs(MemoryVT);

  SDValue DataLo, DataHi;
  std::tie(DataLo, DataHi) = DAG.SplitVector(Data, DL);

  SDValue Scale = MSC->getScale();
  SDValue BasePtr = MSC->getBasePtr();
  SDValue IndexLo, IndexHi;
  std::tie(IndexLo, IndexHi) = DAG.SplitVector(MSC->getIndex(), DL);

  MachineMemOperand *MMO = DAG.getMachineFunction().
    getMachineMemOperand(MSC->getPointerInfo(),
                          MachineMemOperand::MOStore,  LoMemVT.getStoreSize(),
                          Alignment, MSC->getAAInfo(), MSC->getRanges());

  SDValue OpsLo[] = { Chain, DataLo, MaskLo, BasePtr, IndexLo, Scale };
  SDValue Lo = DAG.getMaskedScatter(DAG.getVTList(MVT::Other),
                                    DataLo.getValueType(), DL, OpsLo, MMO);

  // The order of the Scatter operation after split is well defined. The "Hi"
  // part comes after the "Lo". So these two operations should be chained one
  // after another.
  SDValue OpsHi[] = { Lo, DataHi, MaskHi, BasePtr, IndexHi, Scale };
  return DAG.getMaskedScatter(DAG.getVTList(MVT::Other), DataHi.getValueType(),
                              DL, OpsHi, MMO);
}

SDValue DAGCombiner::visitMSTORE(SDNode *N) {
  if (Level >= AfterLegalizeTypes)
    return SDValue();

  MaskedStoreSDNode *MST = dyn_cast<MaskedStoreSDNode>(N);
  SDValue Mask = MST->getMask();
  SDValue Data  = MST->getValue();
  EVT VT = Data.getValueType();
  SDLoc DL(N);

  // If the MSTORE data type requires splitting and the mask is provided by a
  // SETCC, then split both nodes and its operands before legalization. This
  // prevents the type legalizer from unrolling SETCC into scalar comparisons
  // and enables future optimizations (e.g. min/max pattern matching on X86).
  if (Mask.getOpcode() == ISD::SETCC) {
    // Check if any splitting is required.
    if (TLI.getTypeAction(*DAG.getContext(), VT) !=
        TargetLowering::TypeSplitVector)
      return SDValue();

    SDValue MaskLo, MaskHi, Lo, Hi;
    std::tie(MaskLo, MaskHi) = SplitVSETCC(Mask.getNode(), DAG);

    SDValue Chain = MST->getChain();
    SDValue Ptr   = MST->getBasePtr();

    EVT MemoryVT = MST->getMemoryVT();
    unsigned Alignment = MST->getOriginalAlignment();

    // if Alignment is equal to the vector size,
    // take the half of it for the second part
    unsigned SecondHalfAlignment =
      (Alignment == VT.getSizeInBits() / 8) ? Alignment / 2 : Alignment;

    EVT LoMemVT, HiMemVT;
    std::tie(LoMemVT, HiMemVT) = DAG.GetSplitDestVTs(MemoryVT);

    SDValue DataLo, DataHi;
    std::tie(DataLo, DataHi) = DAG.SplitVector(Data, DL);

    MachineMemOperand *MMO = DAG.getMachineFunction().
      getMachineMemOperand(MST->getPointerInfo(),
                           MachineMemOperand::MOStore,  LoMemVT.getStoreSize(),
                           Alignment, MST->getAAInfo(), MST->getRanges());

    Lo = DAG.getMaskedStore(Chain, DL, DataLo, Ptr, MaskLo, LoMemVT, MMO,
                            MST->isTruncatingStore(),
                            MST->isCompressingStore());

    Ptr = TLI.IncrementMemoryAddress(Ptr, MaskLo, DL, LoMemVT, DAG,
                                     MST->isCompressingStore());
    unsigned HiOffset = LoMemVT.getStoreSize();

    MMO = DAG.getMachineFunction().getMachineMemOperand(
        MST->getPointerInfo().getWithOffset(HiOffset),
        MachineMemOperand::MOStore, HiMemVT.getStoreSize(), SecondHalfAlignment,
        MST->getAAInfo(), MST->getRanges());

    Hi = DAG.getMaskedStore(Chain, DL, DataHi, Ptr, MaskHi, HiMemVT, MMO,
                            MST->isTruncatingStore(),
                            MST->isCompressingStore());

    AddToWorklist(Lo.getNode());
    AddToWorklist(Hi.getNode());

    return DAG.getNode(ISD::TokenFactor, DL, MVT::Other, Lo, Hi);
  }
  return SDValue();
}

SDValue DAGCombiner::visitMGATHER(SDNode *N) {
  if (Level >= AfterLegalizeTypes)
    return SDValue();

  MaskedGatherSDNode *MGT = cast<MaskedGatherSDNode>(N);
  SDValue Mask = MGT->getMask();
  SDLoc DL(N);

  // If the MGATHER result requires splitting and the mask is provided by a
  // SETCC, then split both nodes and its operands before legalization. This
  // prevents the type legalizer from unrolling SETCC into scalar comparisons
  // and enables future optimizations (e.g. min/max pattern matching on X86).

  if (Mask.getOpcode() != ISD::SETCC)
    return SDValue();

  EVT VT = N->getValueType(0);

  // Check if any splitting is required.
  if (TLI.getTypeAction(*DAG.getContext(), VT) !=
      TargetLowering::TypeSplitVector)
    return SDValue();

  SDValue MaskLo, MaskHi, Lo, Hi;
  std::tie(MaskLo, MaskHi) = SplitVSETCC(Mask.getNode(), DAG);

  SDValue PassThru = MGT->getPassThru();
  SDValue PassThruLo, PassThruHi;
  std::tie(PassThruLo, PassThruHi) = DAG.SplitVector(PassThru, DL);

  EVT LoVT, HiVT;
  std::tie(LoVT, HiVT) = DAG.GetSplitDestVTs(VT);

  SDValue Chain = MGT->getChain();
  EVT MemoryVT = MGT->getMemoryVT();
  unsigned Alignment = MGT->getOriginalAlignment();

  EVT LoMemVT, HiMemVT;
  std::tie(LoMemVT, HiMemVT) = DAG.GetSplitDestVTs(MemoryVT);

  SDValue Scale = MGT->getScale();
  SDValue BasePtr = MGT->getBasePtr();
  SDValue Index = MGT->getIndex();
  SDValue IndexLo, IndexHi;
  std::tie(IndexLo, IndexHi) = DAG.SplitVector(Index, DL);

  MachineMemOperand *MMO = DAG.getMachineFunction().
    getMachineMemOperand(MGT->getPointerInfo(),
                          MachineMemOperand::MOLoad,  LoMemVT.getStoreSize(),
                          Alignment, MGT->getAAInfo(), MGT->getRanges());

  SDValue OpsLo[] = { Chain, PassThruLo, MaskLo, BasePtr, IndexLo, Scale };
  Lo = DAG.getMaskedGather(DAG.getVTList(LoVT, MVT::Other), LoVT, DL, OpsLo,
                           MMO);

  SDValue OpsHi[] = { Chain, PassThruHi, MaskHi, BasePtr, IndexHi, Scale };
  Hi = DAG.getMaskedGather(DAG.getVTList(HiVT, MVT::Other), HiVT, DL, OpsHi,
                           MMO);

  AddToWorklist(Lo.getNode());
  AddToWorklist(Hi.getNode());

  // Build a factor node to remember that this load is independent of the
  // other one.
  Chain = DAG.getNode(ISD::TokenFactor, DL, MVT::Other, Lo.getValue(1),
                      Hi.getValue(1));

  // Legalized the chain result - switch anything that used the old chain to
  // use the new one.
  DAG.ReplaceAllUsesOfValueWith(SDValue(MGT, 1), Chain);

  SDValue GatherRes = DAG.getNode(ISD::CONCAT_VECTORS, DL, VT, Lo, Hi);

  SDValue RetOps[] = { GatherRes, Chain };
  return DAG.getMergeValues(RetOps, DL);
}

SDValue DAGCombiner::visitMLOAD(SDNode *N) {
  if (Level >= AfterLegalizeTypes)
    return SDValue();

  MaskedLoadSDNode *MLD = dyn_cast<MaskedLoadSDNode>(N);
  SDValue Mask = MLD->getMask();
  SDLoc DL(N);

  // If the MLOAD result requires splitting and the mask is provided by a
  // SETCC, then split both nodes and its operands before legalization. This
  // prevents the type legalizer from unrolling SETCC into scalar comparisons
  // and enables future optimizations (e.g. min/max pattern matching on X86).
  if (Mask.getOpcode() == ISD::SETCC) {
    EVT VT = N->getValueType(0);

    // Check if any splitting is required.
    if (TLI.getTypeAction(*DAG.getContext(), VT) !=
        TargetLowering::TypeSplitVector)
      return SDValue();

    SDValue MaskLo, MaskHi, Lo, Hi;
    std::tie(MaskLo, MaskHi) = SplitVSETCC(Mask.getNode(), DAG);

    SDValue PassThru = MLD->getPassThru();
    SDValue PassThruLo, PassThruHi;
    std::tie(PassThruLo, PassThruHi) = DAG.SplitVector(PassThru, DL);

    EVT LoVT, HiVT;
    std::tie(LoVT, HiVT) = DAG.GetSplitDestVTs(MLD->getValueType(0));

    SDValue Chain = MLD->getChain();
    SDValue Ptr   = MLD->getBasePtr();
    EVT MemoryVT = MLD->getMemoryVT();
    unsigned Alignment = MLD->getOriginalAlignment();

    // if Alignment is equal to the vector size,
    // take the half of it for the second part
    unsigned SecondHalfAlignment =
      (Alignment == MLD->getValueType(0).getSizeInBits()/8) ?
         Alignment/2 : Alignment;

    EVT LoMemVT, HiMemVT;
    std::tie(LoMemVT, HiMemVT) = DAG.GetSplitDestVTs(MemoryVT);

    MachineMemOperand *MMO = DAG.getMachineFunction().
    getMachineMemOperand(MLD->getPointerInfo(),
                         MachineMemOperand::MOLoad,  LoMemVT.getStoreSize(),
                         Alignment, MLD->getAAInfo(), MLD->getRanges());

    Lo = DAG.getMaskedLoad(LoVT, DL, Chain, Ptr, MaskLo, PassThruLo, LoMemVT,
                           MMO, ISD::NON_EXTLOAD, MLD->isExpandingLoad());

    Ptr = TLI.IncrementMemoryAddress(Ptr, MaskLo, DL, LoMemVT, DAG,
                                     MLD->isExpandingLoad());
    unsigned HiOffset = LoMemVT.getStoreSize();

    MMO = DAG.getMachineFunction().getMachineMemOperand(
        MLD->getPointerInfo().getWithOffset(HiOffset),
        MachineMemOperand::MOLoad, HiMemVT.getStoreSize(), SecondHalfAlignment,
        MLD->getAAInfo(), MLD->getRanges());

    Hi = DAG.getMaskedLoad(HiVT, DL, Chain, Ptr, MaskHi, PassThruHi, HiMemVT,
                           MMO, ISD::NON_EXTLOAD, MLD->isExpandingLoad());

    AddToWorklist(Lo.getNode());
    AddToWorklist(Hi.getNode());

    // Build a factor node to remember that this load is independent of the
    // other one.
    Chain = DAG.getNode(ISD::TokenFactor, DL, MVT::Other, Lo.getValue(1),
                        Hi.getValue(1));

    // Legalized the chain result - switch anything that used the old chain to
    // use the new one.
    DAG.ReplaceAllUsesOfValueWith(SDValue(MLD, 1), Chain);

    SDValue LoadRes = DAG.getNode(ISD::CONCAT_VECTORS, DL, VT, Lo, Hi);

    SDValue RetOps[] = { LoadRes, Chain };
    return DAG.getMergeValues(RetOps, DL);
  }
  return SDValue();
}

/// A vector select of 2 constant vectors can be simplified to math/logic to
/// avoid a variable select instruction and possibly avoid constant loads.
SDValue DAGCombiner::foldVSelectOfConstants(SDNode *N) {
  SDValue Cond = N->getOperand(0);
  SDValue N1 = N->getOperand(1);
  SDValue N2 = N->getOperand(2);
  EVT VT = N->getValueType(0);
  if (!Cond.hasOneUse() || Cond.getScalarValueSizeInBits() != 1 ||
      !TLI.convertSelectOfConstantsToMath(VT) ||
      !ISD::isBuildVectorOfConstantSDNodes(N1.getNode()) ||
      !ISD::isBuildVectorOfConstantSDNodes(N2.getNode()))
    return SDValue();

  // Check if we can use the condition value to increment/decrement a single
  // constant value. This simplifies a select to an add and removes a constant
  // load/materialization from the general case.
  bool AllAddOne = true;
  bool AllSubOne = true;
  unsigned Elts = VT.getVectorNumElements();
  for (unsigned i = 0; i != Elts; ++i) {
    SDValue N1Elt = N1.getOperand(i);
    SDValue N2Elt = N2.getOperand(i);
    if (N1Elt.isUndef() || N2Elt.isUndef())
      continue;

    const APInt &C1 = cast<ConstantSDNode>(N1Elt)->getAPIntValue();
    const APInt &C2 = cast<ConstantSDNode>(N2Elt)->getAPIntValue();
    if (C1 != C2 + 1)
      AllAddOne = false;
    if (C1 != C2 - 1)
      AllSubOne = false;
  }

  // Further simplifications for the extra-special cases where the constants are
  // all 0 or all -1 should be implemented as folds of these patterns.
  SDLoc DL(N);
  if (AllAddOne || AllSubOne) {
    // vselect <N x i1> Cond, C+1, C --> add (zext Cond), C
    // vselect <N x i1> Cond, C-1, C --> add (sext Cond), C
    auto ExtendOpcode = AllAddOne ? ISD::ZERO_EXTEND : ISD::SIGN_EXTEND;
    SDValue ExtendedCond = DAG.getNode(ExtendOpcode, DL, VT, Cond);
    return DAG.getNode(ISD::ADD, DL, VT, ExtendedCond, N2);
  }

  // The general case for select-of-constants:
  // vselect <N x i1> Cond, C1, C2 --> xor (and (sext Cond), (C1^C2)), C2
  // ...but that only makes sense if a vselect is slower than 2 logic ops, so
  // leave that to a machine-specific pass.
  return SDValue();
}

SDValue DAGCombiner::visitVSELECT(SDNode *N) {
  SDValue N0 = N->getOperand(0);
  SDValue N1 = N->getOperand(1);
  SDValue N2 = N->getOperand(2);
  SDLoc DL(N);

  if (SDValue V = DAG.simplifySelect(N0, N1, N2))
    return V;

  // Canonicalize integer abs.
  // vselect (setg[te] X,  0),  X, -X ->
  // vselect (setgt    X, -1),  X, -X ->
  // vselect (setl[te] X,  0), -X,  X ->
  // Y = sra (X, size(X)-1); xor (add (X, Y), Y)
  if (N0.getOpcode() == ISD::SETCC) {
    SDValue LHS = N0.getOperand(0), RHS = N0.getOperand(1);
    ISD::CondCode CC = cast<CondCodeSDNode>(N0.getOperand(2))->get();
    bool isAbs = false;
    bool RHSIsAllZeros = ISD::isBuildVectorAllZeros(RHS.getNode());

    if (((RHSIsAllZeros && (CC == ISD::SETGT || CC == ISD::SETGE)) ||
         (ISD::isBuildVectorAllOnes(RHS.getNode()) && CC == ISD::SETGT)) &&
        N1 == LHS && N2.getOpcode() == ISD::SUB && N1 == N2.getOperand(1))
      isAbs = ISD::isBuildVectorAllZeros(N2.getOperand(0).getNode());
    else if ((RHSIsAllZeros && (CC == ISD::SETLT || CC == ISD::SETLE)) &&
             N2 == LHS && N1.getOpcode() == ISD::SUB && N2 == N1.getOperand(1))
      isAbs = ISD::isBuildVectorAllZeros(N1.getOperand(0).getNode());

    if (isAbs) {
      EVT VT = LHS.getValueType();
      if (TLI.isOperationLegalOrCustom(ISD::ABS, VT))
        return DAG.getNode(ISD::ABS, DL, VT, LHS);

      SDValue Shift = DAG.getNode(
          ISD::SRA, DL, VT, LHS,
          DAG.getConstant(VT.getScalarSizeInBits() - 1, DL, VT));
      SDValue Add = DAG.getNode(ISD::ADD, DL, VT, LHS, Shift);
      AddToWorklist(Shift.getNode());
      AddToWorklist(Add.getNode());
      return DAG.getNode(ISD::XOR, DL, VT, Add, Shift);
    }

    // vselect x, y (fcmp lt x, y) -> fminnum x, y
    // vselect x, y (fcmp gt x, y) -> fmaxnum x, y
    //
    // This is OK if we don't care about what happens if either operand is a
    // NaN.
    //
    EVT VT = N->getValueType(0);
    if (N0.hasOneUse() && isLegalToCombineMinNumMaxNum(DAG, N0.getOperand(0), N0.getOperand(1))) {
      ISD::CondCode CC = cast<CondCodeSDNode>(N0.getOperand(2))->get();
      if (SDValue FMinMax = combineMinNumMaxNum(
            DL, VT, N0.getOperand(0), N0.getOperand(1), N1, N2, CC, TLI, DAG))
        return FMinMax;
    }

    // If this select has a condition (setcc) with narrower operands than the
    // select, try to widen the compare to match the select width.
    // TODO: This should be extended to handle any constant.
    // TODO: This could be extended to handle non-loading patterns, but that
    //       requires thorough testing to avoid regressions.
    if (isNullOrNullSplat(RHS)) {
      EVT NarrowVT = LHS.getValueType();
      EVT WideVT = N1.getValueType().changeVectorElementTypeToInteger();
      EVT SetCCVT = getSetCCResultType(LHS.getValueType());
      unsigned SetCCWidth = SetCCVT.getScalarSizeInBits();
      unsigned WideWidth = WideVT.getScalarSizeInBits();
      bool IsSigned = isSignedIntSetCC(CC);
      auto LoadExtOpcode = IsSigned ? ISD::SEXTLOAD : ISD::ZEXTLOAD;
      if (LHS.getOpcode() == ISD::LOAD && LHS.hasOneUse() &&
          SetCCWidth != 1 && SetCCWidth < WideWidth &&
          TLI.isLoadExtLegalOrCustom(LoadExtOpcode, WideVT, NarrowVT) &&
          TLI.isOperationLegalOrCustom(ISD::SETCC, WideVT)) {
        // Both compare operands can be widened for free. The LHS can use an
        // extended load, and the RHS is a constant:
        //   vselect (ext (setcc load(X), C)), N1, N2 -->
        //   vselect (setcc extload(X), C'), N1, N2
        auto ExtOpcode = IsSigned ? ISD::SIGN_EXTEND : ISD::ZERO_EXTEND;
        SDValue WideLHS = DAG.getNode(ExtOpcode, DL, WideVT, LHS);
        SDValue WideRHS = DAG.getNode(ExtOpcode, DL, WideVT, RHS);
        EVT WideSetCCVT = getSetCCResultType(WideVT);
        SDValue WideSetCC = DAG.getSetCC(DL, WideSetCCVT, WideLHS, WideRHS, CC);
        return DAG.getSelect(DL, N1.getValueType(), WideSetCC, N1, N2);
      }
    }
  }

  if (SimplifySelectOps(N, N1, N2))
    return SDValue(N, 0);  // Don't revisit N.

  // Fold (vselect (build_vector all_ones), N1, N2) -> N1
  if (ISD::isBuildVectorAllOnes(N0.getNode()))
    return N1;
  // Fold (vselect (build_vector all_zeros), N1, N2) -> N2
  if (ISD::isBuildVectorAllZeros(N0.getNode()))
    return N2;

  // The ConvertSelectToConcatVector function is assuming both the above
  // checks for (vselect (build_vector all{ones,zeros) ...) have been made
  // and addressed.
  if (N1.getOpcode() == ISD::CONCAT_VECTORS &&
      N2.getOpcode() == ISD::CONCAT_VECTORS &&
      ISD::isBuildVectorOfConstantSDNodes(N0.getNode())) {
    if (SDValue CV = ConvertSelectToConcatVector(N, DAG))
      return CV;
  }

  if (SDValue V = foldVSelectOfConstants(N))
    return V;

  return SDValue();
}

SDValue DAGCombiner::visitSELECT_CC(SDNode *N) {
  SDValue N0 = N->getOperand(0);
  SDValue N1 = N->getOperand(1);
  SDValue N2 = N->getOperand(2);
  SDValue N3 = N->getOperand(3);
  SDValue N4 = N->getOperand(4);
  ISD::CondCode CC = cast<CondCodeSDNode>(N4)->get();

  // fold select_cc lhs, rhs, x, x, cc -> x
  if (N2 == N3)
    return N2;

  // Determine if the condition we're dealing with is constant
  if (SDValue SCC = SimplifySetCC(getSetCCResultType(N0.getValueType()), N0, N1,
                                  CC, SDLoc(N), false)) {
    AddToWorklist(SCC.getNode());

    if (ConstantSDNode *SCCC = dyn_cast<ConstantSDNode>(SCC.getNode())) {
      if (!SCCC->isNullValue())
        return N2;    // cond always true -> true val
      else
        return N3;    // cond always false -> false val
    } else if (SCC->isUndef()) {
      // When the condition is UNDEF, just return the first operand. This is
      // coherent the DAG creation, no setcc node is created in this case
      return N2;
    } else if (SCC.getOpcode() == ISD::SETCC) {
      // Fold to a simpler select_cc
      return DAG.getNode(ISD::SELECT_CC, SDLoc(N), N2.getValueType(),
                         SCC.getOperand(0), SCC.getOperand(1), N2, N3,
                         SCC.getOperand(2));
    }
  }

  // If we can fold this based on the true/false value, do so.
  if (SimplifySelectOps(N, N2, N3))
    return SDValue(N, 0);  // Don't revisit N.

  // fold select_cc into other things, such as min/max/abs
  return SimplifySelectCC(SDLoc(N), N0, N1, N2, N3, CC);
}

SDValue DAGCombiner::visitSETCC(SDNode *N) {
  // setcc is very commonly used as an argument to brcond. This pattern
  // also lend itself to numerous combines and, as a result, it is desired
  // we keep the argument to a brcond as a setcc as much as possible.
  bool PreferSetCC =
      N->hasOneUse() && N->use_begin()->getOpcode() == ISD::BRCOND;

  SDValue Combined = SimplifySetCC(
      N->getValueType(0), N->getOperand(0), N->getOperand(1),
      cast<CondCodeSDNode>(N->getOperand(2))->get(), SDLoc(N), !PreferSetCC);

  if (!Combined)
    return SDValue();

  // If we prefer to have a setcc, and we don't, we'll try our best to
  // recreate one using rebuildSetCC.
  if (PreferSetCC && Combined.getOpcode() != ISD::SETCC) {
    SDValue NewSetCC = rebuildSetCC(Combined);

    // We don't have anything interesting to combine to.
    if (NewSetCC.getNode() == N)
      return SDValue();

    if (NewSetCC)
      return NewSetCC;
  }

  return Combined;
}

SDValue DAGCombiner::visitSETCCCARRY(SDNode *N) {
  SDValue LHS = N->getOperand(0);
  SDValue RHS = N->getOperand(1);
  SDValue Carry = N->getOperand(2);
  SDValue Cond = N->getOperand(3);

  // If Carry is false, fold to a regular SETCC.
  if (isNullConstant(Carry))
    return DAG.getNode(ISD::SETCC, SDLoc(N), N->getVTList(), LHS, RHS, Cond);

  return SDValue();
}

/// Try to fold a sext/zext/aext dag node into a ConstantSDNode or
/// a build_vector of constants.
/// This function is called by the DAGCombiner when visiting sext/zext/aext
/// dag nodes (see for example method DAGCombiner::visitSIGN_EXTEND).
/// Vector extends are not folded if operations are legal; this is to
/// avoid introducing illegal build_vector dag nodes.
static SDValue tryToFoldExtendOfConstant(SDNode *N, const TargetLowering &TLI,
                                         SelectionDAG &DAG, bool LegalTypes) {
  unsigned Opcode = N->getOpcode();
  SDValue N0 = N->getOperand(0);
  EVT VT = N->getValueType(0);

  assert((Opcode == ISD::SIGN_EXTEND || Opcode == ISD::ZERO_EXTEND ||
         Opcode == ISD::ANY_EXTEND || Opcode == ISD::SIGN_EXTEND_VECTOR_INREG ||
         Opcode == ISD::ZERO_EXTEND_VECTOR_INREG)
         && "Expected EXTEND dag node in input!");

  // fold (sext c1) -> c1
  // fold (zext c1) -> c1
  // fold (aext c1) -> c1
  if (isa<ConstantSDNode>(N0))
    return DAG.getNode(Opcode, SDLoc(N), VT, N0);

  // fold (sext (build_vector AllConstants) -> (build_vector AllConstants)
  // fold (zext (build_vector AllConstants) -> (build_vector AllConstants)
  // fold (aext (build_vector AllConstants) -> (build_vector AllConstants)
  EVT SVT = VT.getScalarType();
  if (!(VT.isVector() && (!LegalTypes || TLI.isTypeLegal(SVT)) &&
      ISD::isBuildVectorOfConstantSDNodes(N0.getNode())))
    return SDValue();

  // We can fold this node into a build_vector.
  unsigned VTBits = SVT.getSizeInBits();
  unsigned EVTBits = N0->getValueType(0).getScalarSizeInBits();
  SmallVector<SDValue, 8> Elts;
  unsigned NumElts = VT.getVectorNumElements();
  SDLoc DL(N);

  // For zero-extensions, UNDEF elements still guarantee to have the upper
  // bits set to zero.
  bool IsZext =
      Opcode == ISD::ZERO_EXTEND || Opcode == ISD::ZERO_EXTEND_VECTOR_INREG;

  for (unsigned i = 0; i != NumElts; ++i) {
    SDValue Op = N0.getOperand(i);
    if (Op.isUndef()) {
      Elts.push_back(IsZext ? DAG.getConstant(0, DL, SVT) : DAG.getUNDEF(SVT));
      continue;
    }

    SDLoc DL(Op);
    // Get the constant value and if needed trunc it to the size of the type.
    // Nodes like build_vector might have constants wider than the scalar type.
    APInt C = cast<ConstantSDNode>(Op)->getAPIntValue().zextOrTrunc(EVTBits);
    if (Opcode == ISD::SIGN_EXTEND || Opcode == ISD::SIGN_EXTEND_VECTOR_INREG)
      Elts.push_back(DAG.getConstant(C.sext(VTBits), DL, SVT));
    else
      Elts.push_back(DAG.getConstant(C.zext(VTBits), DL, SVT));
  }

  return DAG.getBuildVector(VT, DL, Elts);
}

// ExtendUsesToFormExtLoad - Trying to extend uses of a load to enable this:
// "fold ({s|z|a}ext (load x)) -> ({s|z|a}ext (truncate ({s|z|a}extload x)))"
// transformation. Returns true if extension are possible and the above
// mentioned transformation is profitable.
static bool ExtendUsesToFormExtLoad(EVT VT, SDNode *N, SDValue N0,
                                    unsigned ExtOpc,
                                    SmallVectorImpl<SDNode *> &ExtendNodes,
                                    const TargetLowering &TLI) {
  bool HasCopyToRegUses = false;
  bool isTruncFree = TLI.isTruncateFree(VT, N0.getValueType());
  for (SDNode::use_iterator UI = N0.getNode()->use_begin(),
                            UE = N0.getNode()->use_end();
       UI != UE; ++UI) {
    SDNode *User = *UI;
    if (User == N)
      continue;
    if (UI.getUse().getResNo() != N0.getResNo())
      continue;
    // FIXME: Only extend SETCC N, N and SETCC N, c for now.
    if (ExtOpc != ISD::ANY_EXTEND && User->getOpcode() == ISD::SETCC) {
      ISD::CondCode CC = cast<CondCodeSDNode>(User->getOperand(2))->get();
      if (ExtOpc == ISD::ZERO_EXTEND && ISD::isSignedIntSetCC(CC))
        // Sign bits will be lost after a zext.
        return false;
      bool Add = false;
      for (unsigned i = 0; i != 2; ++i) {
        SDValue UseOp = User->getOperand(i);
        if (UseOp == N0)
          continue;
        if (!isa<ConstantSDNode>(UseOp))
          return false;
        Add = true;
      }
      if (Add)
        ExtendNodes.push_back(User);
      continue;
    }
    // If truncates aren't free and there are users we can't
    // extend, it isn't worthwhile.
    if (!isTruncFree)
      return false;
    // Remember if this value is live-out.
    if (User->getOpcode() == ISD::CopyToReg)
      HasCopyToRegUses = true;
  }

  if (HasCopyToRegUses) {
    bool BothLiveOut = false;
    for (SDNode::use_iterator UI = N->use_begin(), UE = N->use_end();
         UI != UE; ++UI) {
      SDUse &Use = UI.getUse();
      if (Use.getResNo() == 0 && Use.getUser()->getOpcode() == ISD::CopyToReg) {
        BothLiveOut = true;
        break;
      }
    }
    if (BothLiveOut)
      // Both unextended and extended values are live out. There had better be
      // a good reason for the transformation.
      return ExtendNodes.size();
  }
  return true;
}

void DAGCombiner::ExtendSetCCUses(const SmallVectorImpl<SDNode *> &SetCCs,
                                  SDValue OrigLoad, SDValue ExtLoad,
                                  ISD::NodeType ExtType) {
  // Extend SetCC uses if necessary.
  SDLoc DL(ExtLoad);
  for (SDNode *SetCC : SetCCs) {
    SmallVector<SDValue, 4> Ops;

    for (unsigned j = 0; j != 2; ++j) {
      SDValue SOp = SetCC->getOperand(j);
      if (SOp == OrigLoad)
        Ops.push_back(ExtLoad);
      else
        Ops.push_back(DAG.getNode(ExtType, DL, ExtLoad->getValueType(0), SOp));
    }

    Ops.push_back(SetCC->getOperand(2));
    CombineTo(SetCC, DAG.getNode(ISD::SETCC, DL, SetCC->getValueType(0), Ops));
  }
}

// FIXME: Bring more similar combines here, common to sext/zext (maybe aext?).
SDValue DAGCombiner::CombineExtLoad(SDNode *N) {
  SDValue N0 = N->getOperand(0);
  EVT DstVT = N->getValueType(0);
  EVT SrcVT = N0.getValueType();

  assert((N->getOpcode() == ISD::SIGN_EXTEND ||
          N->getOpcode() == ISD::ZERO_EXTEND) &&
         "Unexpected node type (not an extend)!");

  // fold (sext (load x)) to multiple smaller sextloads; same for zext.
  // For example, on a target with legal v4i32, but illegal v8i32, turn:
  //   (v8i32 (sext (v8i16 (load x))))
  // into:
  //   (v8i32 (concat_vectors (v4i32 (sextload x)),
  //                          (v4i32 (sextload (x + 16)))))
  // Where uses of the original load, i.e.:
  //   (v8i16 (load x))
  // are replaced with:
  //   (v8i16 (truncate
  //     (v8i32 (concat_vectors (v4i32 (sextload x)),
  //                            (v4i32 (sextload (x + 16)))))))
  //
  // This combine is only applicable to illegal, but splittable, vectors.
  // All legal types, and illegal non-vector types, are handled elsewhere.
  // This combine is controlled by TargetLowering::isVectorLoadExtDesirable.
  //
  if (N0->getOpcode() != ISD::LOAD)
    return SDValue();

  LoadSDNode *LN0 = cast<LoadSDNode>(N0);

  if (!ISD::isNON_EXTLoad(LN0) || !ISD::isUNINDEXEDLoad(LN0) ||
      !N0.hasOneUse() || LN0->isVolatile() || !DstVT.isVector() ||
      !DstVT.isPow2VectorType() || !TLI.isVectorLoadExtDesirable(SDValue(N, 0)))
    return SDValue();

  SmallVector<SDNode *, 4> SetCCs;
  if (!ExtendUsesToFormExtLoad(DstVT, N, N0, N->getOpcode(), SetCCs, TLI))
    return SDValue();

  ISD::LoadExtType ExtType =
      N->getOpcode() == ISD::SIGN_EXTEND ? ISD::SEXTLOAD : ISD::ZEXTLOAD;

  // Try to split the vector types to get down to legal types.
  EVT SplitSrcVT = SrcVT;
  EVT SplitDstVT = DstVT;
  while (!TLI.isLoadExtLegalOrCustom(ExtType, SplitDstVT, SplitSrcVT) &&
         SplitSrcVT.getVectorNumElements() > 1) {
    SplitDstVT = DAG.GetSplitDestVTs(SplitDstVT).first;
    SplitSrcVT = DAG.GetSplitDestVTs(SplitSrcVT).first;
  }

  if (!TLI.isLoadExtLegalOrCustom(ExtType, SplitDstVT, SplitSrcVT))
    return SDValue();

  SDLoc DL(N);
  const unsigned NumSplits =
      DstVT.getVectorNumElements() / SplitDstVT.getVectorNumElements();
  const unsigned Stride = SplitSrcVT.getStoreSize();
  SmallVector<SDValue, 4> Loads;
  SmallVector<SDValue, 4> Chains;

  SDValue BasePtr = LN0->getBasePtr();
  for (unsigned Idx = 0; Idx < NumSplits; Idx++) {
    const unsigned Offset = Idx * Stride;
    const unsigned Align = MinAlign(LN0->getAlignment(), Offset);

    SDValue SplitLoad = DAG.getExtLoad(
        ExtType, SDLoc(LN0), SplitDstVT, LN0->getChain(), BasePtr,
        LN0->getPointerInfo().getWithOffset(Offset), SplitSrcVT, Align,
        LN0->getMemOperand()->getFlags(), LN0->getAAInfo());

    BasePtr = DAG.getNode(ISD::ADD, DL, BasePtr.getValueType(), BasePtr,
                          DAG.getConstant(Stride, DL, BasePtr.getValueType()));

    Loads.push_back(SplitLoad.getValue(0));
    Chains.push_back(SplitLoad.getValue(1));
  }

  SDValue NewChain = DAG.getNode(ISD::TokenFactor, DL, MVT::Other, Chains);
  SDValue NewValue = DAG.getNode(ISD::CONCAT_VECTORS, DL, DstVT, Loads);

  // Simplify TF.
  AddToWorklist(NewChain.getNode());

  CombineTo(N, NewValue);

  // Replace uses of the original load (before extension)
  // with a truncate of the concatenated sextloaded vectors.
  SDValue Trunc =
      DAG.getNode(ISD::TRUNCATE, SDLoc(N0), N0.getValueType(), NewValue);
  ExtendSetCCUses(SetCCs, N0, NewValue, (ISD::NodeType)N->getOpcode());
  CombineTo(N0.getNode(), Trunc, NewChain);
  return SDValue(N, 0); // Return N so it doesn't get rechecked!
}

// fold (zext (and/or/xor (shl/shr (load x), cst), cst)) ->
//      (and/or/xor (shl/shr (zextload x), (zext cst)), (zext cst))
SDValue DAGCombiner::CombineZExtLogicopShiftLoad(SDNode *N) {
  assert(N->getOpcode() == ISD::ZERO_EXTEND);
  EVT VT = N->getValueType(0);

  // and/or/xor
  SDValue N0 = N->getOperand(0);
  if (!(N0.getOpcode() == ISD::AND || N0.getOpcode() == ISD::OR ||
        N0.getOpcode() == ISD::XOR) ||
      N0.getOperand(1).getOpcode() != ISD::Constant ||
      (LegalOperations && !TLI.isOperationLegal(N0.getOpcode(), VT)))
    return SDValue();

  // shl/shr
  SDValue N1 = N0->getOperand(0);
  if (!(N1.getOpcode() == ISD::SHL || N1.getOpcode() == ISD::SRL) ||
      N1.getOperand(1).getOpcode() != ISD::Constant ||
      (LegalOperations && !TLI.isOperationLegal(N1.getOpcode(), VT)))
    return SDValue();

  // load
  if (!isa<LoadSDNode>(N1.getOperand(0)))
    return SDValue();
  LoadSDNode *Load = cast<LoadSDNode>(N1.getOperand(0));
  EVT MemVT = Load->getMemoryVT();
  if (!TLI.isLoadExtLegal(ISD::ZEXTLOAD, VT, MemVT) ||
      Load->getExtensionType() == ISD::SEXTLOAD || Load->isIndexed())
    return SDValue();


  // If the shift op is SHL, the logic op must be AND, otherwise the result
  // will be wrong.
  if (N1.getOpcode() == ISD::SHL && N0.getOpcode() != ISD::AND)
    return SDValue();

  if (!N0.hasOneUse() || !N1.hasOneUse())
    return SDValue();

  SmallVector<SDNode*, 4> SetCCs;
  if (!ExtendUsesToFormExtLoad(VT, N1.getNode(), N1.getOperand(0),
                               ISD::ZERO_EXTEND, SetCCs, TLI))
    return SDValue();

  // Actually do the transformation.
  SDValue ExtLoad = DAG.getExtLoad(ISD::ZEXTLOAD, SDLoc(Load), VT,
                                   Load->getChain(), Load->getBasePtr(),
                                   Load->getMemoryVT(), Load->getMemOperand());

  SDLoc DL1(N1);
  SDValue Shift = DAG.getNode(N1.getOpcode(), DL1, VT, ExtLoad,
                              N1.getOperand(1));

  APInt Mask = cast<ConstantSDNode>(N0.getOperand(1))->getAPIntValue();
  Mask = Mask.zext(VT.getSizeInBits());
  SDLoc DL0(N0);
  SDValue And = DAG.getNode(N0.getOpcode(), DL0, VT, Shift,
                            DAG.getConstant(Mask, DL0, VT));

  ExtendSetCCUses(SetCCs, N1.getOperand(0), ExtLoad, ISD::ZERO_EXTEND);
  CombineTo(N, And);
  if (SDValue(Load, 0).hasOneUse()) {
    DAG.ReplaceAllUsesOfValueWith(SDValue(Load, 1), ExtLoad.getValue(1));
  } else {
    SDValue Trunc = DAG.getNode(ISD::TRUNCATE, SDLoc(Load),
                                Load->getValueType(0), ExtLoad);
    CombineTo(Load, Trunc, ExtLoad.getValue(1));
  }
  return SDValue(N,0); // Return N so it doesn't get rechecked!
}

/// If we're narrowing or widening the result of a vector select and the final
/// size is the same size as a setcc (compare) feeding the select, then try to
/// apply the cast operation to the select's operands because matching vector
/// sizes for a select condition and other operands should be more efficient.
SDValue DAGCombiner::matchVSelectOpSizesWithSetCC(SDNode *Cast) {
  unsigned CastOpcode = Cast->getOpcode();
  assert((CastOpcode == ISD::SIGN_EXTEND || CastOpcode == ISD::ZERO_EXTEND ||
          CastOpcode == ISD::TRUNCATE || CastOpcode == ISD::FP_EXTEND ||
          CastOpcode == ISD::FP_ROUND) &&
         "Unexpected opcode for vector select narrowing/widening");

  // We only do this transform before legal ops because the pattern may be
  // obfuscated by target-specific operations after legalization. Do not create
  // an illegal select op, however, because that may be difficult to lower.
  EVT VT = Cast->getValueType(0);
  if (LegalOperations || !TLI.isOperationLegalOrCustom(ISD::VSELECT, VT))
    return SDValue();

  SDValue VSel = Cast->getOperand(0);
  if (VSel.getOpcode() != ISD::VSELECT || !VSel.hasOneUse() ||
      VSel.getOperand(0).getOpcode() != ISD::SETCC)
    return SDValue();

  // Does the setcc have the same vector size as the casted select?
  SDValue SetCC = VSel.getOperand(0);
  EVT SetCCVT = getSetCCResultType(SetCC.getOperand(0).getValueType());
  if (SetCCVT.getSizeInBits() != VT.getSizeInBits())
    return SDValue();

  // cast (vsel (setcc X), A, B) --> vsel (setcc X), (cast A), (cast B)
  SDValue A = VSel.getOperand(1);
  SDValue B = VSel.getOperand(2);
  SDValue CastA, CastB;
  SDLoc DL(Cast);
  if (CastOpcode == ISD::FP_ROUND) {
    // FP_ROUND (fptrunc) has an extra flag operand to pass along.
    CastA = DAG.getNode(CastOpcode, DL, VT, A, Cast->getOperand(1));
    CastB = DAG.getNode(CastOpcode, DL, VT, B, Cast->getOperand(1));
  } else {
    CastA = DAG.getNode(CastOpcode, DL, VT, A);
    CastB = DAG.getNode(CastOpcode, DL, VT, B);
  }
  return DAG.getNode(ISD::VSELECT, DL, VT, SetCC, CastA, CastB);
}

// fold ([s|z]ext ([s|z]extload x)) -> ([s|z]ext (truncate ([s|z]extload x)))
// fold ([s|z]ext (     extload x)) -> ([s|z]ext (truncate ([s|z]extload x)))
static SDValue tryToFoldExtOfExtload(SelectionDAG &DAG, DAGCombiner &Combiner,
                                     const TargetLowering &TLI, EVT VT,
                                     bool LegalOperations, SDNode *N,
                                     SDValue N0, ISD::LoadExtType ExtLoadType) {
  SDNode *N0Node = N0.getNode();
  bool isAExtLoad = (ExtLoadType == ISD::SEXTLOAD) ? ISD::isSEXTLoad(N0Node)
                                                   : ISD::isZEXTLoad(N0Node);
  if ((!isAExtLoad && !ISD::isEXTLoad(N0Node)) ||
      !ISD::isUNINDEXEDLoad(N0Node) || !N0.hasOneUse())
    return {};

  LoadSDNode *LN0 = cast<LoadSDNode>(N0);
  EVT MemVT = LN0->getMemoryVT();
  if ((LegalOperations || LN0->isVolatile() || VT.isVector()) &&
      !TLI.isLoadExtLegal(ExtLoadType, VT, MemVT))
    return {};

  SDValue ExtLoad =
      DAG.getExtLoad(ExtLoadType, SDLoc(LN0), VT, LN0->getChain(),
                     LN0->getBasePtr(), MemVT, LN0->getMemOperand());
  Combiner.CombineTo(N, ExtLoad);
  DAG.ReplaceAllUsesOfValueWith(SDValue(LN0, 1), ExtLoad.getValue(1));
  return SDValue(N, 0); // Return N so it doesn't get rechecked!
}

// fold ([s|z]ext (load x)) -> ([s|z]ext (truncate ([s|z]extload x)))
// Only generate vector extloads when 1) they're legal, and 2) they are
// deemed desirable by the target.
static SDValue tryToFoldExtOfLoad(SelectionDAG &DAG, DAGCombiner &Combiner,
                                  const TargetLowering &TLI, EVT VT,
                                  bool LegalOperations, SDNode *N, SDValue N0,
                                  ISD::LoadExtType ExtLoadType,
                                  ISD::NodeType ExtOpc) {
  if (!ISD::isNON_EXTLoad(N0.getNode()) ||
      !ISD::isUNINDEXEDLoad(N0.getNode()) ||
      ((LegalOperations || VT.isVector() ||
        cast<LoadSDNode>(N0)->isVolatile()) &&
       !TLI.isLoadExtLegal(ExtLoadType, VT, N0.getValueType())))
    return {};

  bool DoXform = true;
  SmallVector<SDNode *, 4> SetCCs;
  if (!N0.hasOneUse())
    DoXform = ExtendUsesToFormExtLoad(VT, N, N0, ExtOpc, SetCCs, TLI);
  if (VT.isVector())
    DoXform &= TLI.isVectorLoadExtDesirable(SDValue(N, 0));
  if (!DoXform)
    return {};

  LoadSDNode *LN0 = cast<LoadSDNode>(N0);
  SDValue ExtLoad = DAG.getExtLoad(ExtLoadType, SDLoc(LN0), VT, LN0->getChain(),
                                   LN0->getBasePtr(), N0.getValueType(),
                                   LN0->getMemOperand());
  Combiner.ExtendSetCCUses(SetCCs, N0, ExtLoad, ExtOpc);
  // If the load value is used only by N, replace it via CombineTo N.
  bool NoReplaceTrunc = SDValue(LN0, 0).hasOneUse();
  Combiner.CombineTo(N, ExtLoad);
  if (NoReplaceTrunc) {
    DAG.ReplaceAllUsesOfValueWith(SDValue(LN0, 1), ExtLoad.getValue(1));
  } else {
    SDValue Trunc =
        DAG.getNode(ISD::TRUNCATE, SDLoc(N0), N0.getValueType(), ExtLoad);
    Combiner.CombineTo(LN0, Trunc, ExtLoad.getValue(1));
  }
  return SDValue(N, 0); // Return N so it doesn't get rechecked!
}

static SDValue foldExtendedSignBitTest(SDNode *N, SelectionDAG &DAG,
                                       bool LegalOperations) {
  assert((N->getOpcode() == ISD::SIGN_EXTEND ||
          N->getOpcode() == ISD::ZERO_EXTEND) && "Expected sext or zext");

  SDValue SetCC = N->getOperand(0);
  if (LegalOperations || SetCC.getOpcode() != ISD::SETCC ||
      !SetCC.hasOneUse() || SetCC.getValueType() != MVT::i1)
    return SDValue();

  SDValue X = SetCC.getOperand(0);
  SDValue Ones = SetCC.getOperand(1);
  ISD::CondCode CC = cast<CondCodeSDNode>(SetCC.getOperand(2))->get();
  EVT VT = N->getValueType(0);
  EVT XVT = X.getValueType();
  // setge X, C is canonicalized to setgt, so we do not need to match that
  // pattern. The setlt sibling is folded in SimplifySelectCC() because it does
  // not require the 'not' op.
  if (CC == ISD::SETGT && isAllOnesConstant(Ones) && VT == XVT) {
    // Invert and smear/shift the sign bit:
    // sext i1 (setgt iN X, -1) --> sra (not X), (N - 1)
    // zext i1 (setgt iN X, -1) --> srl (not X), (N - 1)
    SDLoc DL(N);
    SDValue NotX = DAG.getNOT(DL, X, VT);
    SDValue ShiftAmount = DAG.getConstant(VT.getSizeInBits() - 1, DL, VT);
    auto ShiftOpcode = N->getOpcode() == ISD::SIGN_EXTEND ? ISD::SRA : ISD::SRL;
    return DAG.getNode(ShiftOpcode, DL, VT, NotX, ShiftAmount);
  }
  return SDValue();
}

SDValue DAGCombiner::visitSIGN_EXTEND(SDNode *N) {
  SDValue N0 = N->getOperand(0);
  EVT VT = N->getValueType(0);
  SDLoc DL(N);

  if (SDValue Res = tryToFoldExtendOfConstant(N, TLI, DAG, LegalTypes))
    return Res;

  // fold (sext (sext x)) -> (sext x)
  // fold (sext (aext x)) -> (sext x)
  if (N0.getOpcode() == ISD::SIGN_EXTEND || N0.getOpcode() == ISD::ANY_EXTEND)
    return DAG.getNode(ISD::SIGN_EXTEND, DL, VT, N0.getOperand(0));

  if (N0.getOpcode() == ISD::TRUNCATE) {
    // fold (sext (truncate (load x))) -> (sext (smaller load x))
    // fold (sext (truncate (srl (load x), c))) -> (sext (smaller load (x+c/n)))
    if (SDValue NarrowLoad = ReduceLoadWidth(N0.getNode())) {
      SDNode *oye = N0.getOperand(0).getNode();
      if (NarrowLoad.getNode() != N0.getNode()) {
        CombineTo(N0.getNode(), NarrowLoad);
        // CombineTo deleted the truncate, if needed, but not what's under it.
        AddToWorklist(oye);
      }
      return SDValue(N, 0);   // Return N so it doesn't get rechecked!
    }

    // See if the value being truncated is already sign extended.  If so, just
    // eliminate the trunc/sext pair.
    SDValue Op = N0.getOperand(0);
    unsigned OpBits   = Op.getScalarValueSizeInBits();
    unsigned MidBits  = N0.getScalarValueSizeInBits();
    unsigned DestBits = VT.getScalarSizeInBits();
    unsigned NumSignBits = DAG.ComputeNumSignBits(Op);

    if (OpBits == DestBits) {
      // Op is i32, Mid is i8, and Dest is i32.  If Op has more than 24 sign
      // bits, it is already ready.
      if (NumSignBits > DestBits-MidBits)
        return Op;
    } else if (OpBits < DestBits) {
      // Op is i32, Mid is i8, and Dest is i64.  If Op has more than 24 sign
      // bits, just sext from i32.
      if (NumSignBits > OpBits-MidBits)
        return DAG.getNode(ISD::SIGN_EXTEND, DL, VT, Op);
    } else {
      // Op is i64, Mid is i8, and Dest is i32.  If Op has more than 56 sign
      // bits, just truncate to i32.
      if (NumSignBits > OpBits-MidBits)
        return DAG.getNode(ISD::TRUNCATE, DL, VT, Op);
    }

    // fold (sext (truncate x)) -> (sextinreg x).
    if (!LegalOperations || TLI.isOperationLegal(ISD::SIGN_EXTEND_INREG,
                                                 N0.getValueType())) {
      if (OpBits < DestBits)
        Op = DAG.getNode(ISD::ANY_EXTEND, SDLoc(N0), VT, Op);
      else if (OpBits > DestBits)
        Op = DAG.getNode(ISD::TRUNCATE, SDLoc(N0), VT, Op);
      return DAG.getNode(ISD::SIGN_EXTEND_INREG, DL, VT, Op,
                         DAG.getValueType(N0.getValueType()));
    }
  }

  // Try to simplify (sext (load x)).
  if (SDValue foldedExt =
          tryToFoldExtOfLoad(DAG, *this, TLI, VT, LegalOperations, N, N0,
                             ISD::SEXTLOAD, ISD::SIGN_EXTEND))
    return foldedExt;

  // fold (sext (load x)) to multiple smaller sextloads.
  // Only on illegal but splittable vectors.
  if (SDValue ExtLoad = CombineExtLoad(N))
    return ExtLoad;

  // Try to simplify (sext (sextload x)).
  if (SDValue foldedExt = tryToFoldExtOfExtload(
          DAG, *this, TLI, VT, LegalOperations, N, N0, ISD::SEXTLOAD))
    return foldedExt;

  // fold (sext (and/or/xor (load x), cst)) ->
  //      (and/or/xor (sextload x), (sext cst))
  if ((N0.getOpcode() == ISD::AND || N0.getOpcode() == ISD::OR ||
       N0.getOpcode() == ISD::XOR) &&
      isa<LoadSDNode>(N0.getOperand(0)) &&
      N0.getOperand(1).getOpcode() == ISD::Constant &&
      (!LegalOperations && TLI.isOperationLegal(N0.getOpcode(), VT))) {
    LoadSDNode *LN00 = cast<LoadSDNode>(N0.getOperand(0));
    EVT MemVT = LN00->getMemoryVT();
    if (TLI.isLoadExtLegal(ISD::SEXTLOAD, VT, MemVT) &&
      LN00->getExtensionType() != ISD::ZEXTLOAD && LN00->isUnindexed()) {
      SmallVector<SDNode*, 4> SetCCs;
      bool DoXform = ExtendUsesToFormExtLoad(VT, N0.getNode(), N0.getOperand(0),
                                             ISD::SIGN_EXTEND, SetCCs, TLI);
      if (DoXform) {
        SDValue ExtLoad = DAG.getExtLoad(ISD::SEXTLOAD, SDLoc(LN00), VT,
                                         LN00->getChain(), LN00->getBasePtr(),
                                         LN00->getMemoryVT(),
                                         LN00->getMemOperand());
        APInt Mask = cast<ConstantSDNode>(N0.getOperand(1))->getAPIntValue();
        Mask = Mask.sext(VT.getSizeInBits());
        SDValue And = DAG.getNode(N0.getOpcode(), DL, VT,
                                  ExtLoad, DAG.getConstant(Mask, DL, VT));
        ExtendSetCCUses(SetCCs, N0.getOperand(0), ExtLoad, ISD::SIGN_EXTEND);
        bool NoReplaceTruncAnd = !N0.hasOneUse();
        bool NoReplaceTrunc = SDValue(LN00, 0).hasOneUse();
        CombineTo(N, And);
        // If N0 has multiple uses, change other uses as well.
        if (NoReplaceTruncAnd) {
          SDValue TruncAnd =
              DAG.getNode(ISD::TRUNCATE, DL, N0.getValueType(), And);
          CombineTo(N0.getNode(), TruncAnd);
        }
        if (NoReplaceTrunc) {
          DAG.ReplaceAllUsesOfValueWith(SDValue(LN00, 1), ExtLoad.getValue(1));
        } else {
          SDValue Trunc = DAG.getNode(ISD::TRUNCATE, SDLoc(LN00),
                                      LN00->getValueType(0), ExtLoad);
          CombineTo(LN00, Trunc, ExtLoad.getValue(1));
        }
        return SDValue(N,0); // Return N so it doesn't get rechecked!
      }
    }
  }

  if (SDValue V = foldExtendedSignBitTest(N, DAG, LegalOperations))
    return V;

  if (N0.getOpcode() == ISD::SETCC) {
    SDValue N00 = N0.getOperand(0);
    SDValue N01 = N0.getOperand(1);
    ISD::CondCode CC = cast<CondCodeSDNode>(N0.getOperand(2))->get();
    EVT N00VT = N0.getOperand(0).getValueType();

    // sext(setcc) -> sext_in_reg(vsetcc) for vectors.
    // Only do this before legalize for now.
    if (VT.isVector() && !LegalOperations &&
        TLI.getBooleanContents(N00VT) ==
            TargetLowering::ZeroOrNegativeOneBooleanContent) {
      // On some architectures (such as SSE/NEON/etc) the SETCC result type is
      // of the same size as the compared operands. Only optimize sext(setcc())
      // if this is the case.
      EVT SVT = getSetCCResultType(N00VT);

      // If we already have the desired type, don't change it.
      if (SVT != N0.getValueType()) {
        // We know that the # elements of the results is the same as the
        // # elements of the compare (and the # elements of the compare result
        // for that matter).  Check to see that they are the same size.  If so,
        // we know that the element size of the sext'd result matches the
        // element size of the compare operands.
        if (VT.getSizeInBits() == SVT.getSizeInBits())
          return DAG.getSetCC(DL, VT, N00, N01, CC);

        // If the desired elements are smaller or larger than the source
        // elements, we can use a matching integer vector type and then
        // truncate/sign extend.
        EVT MatchingVecType = N00VT.changeVectorElementTypeToInteger();
        if (SVT == MatchingVecType) {
          SDValue VsetCC = DAG.getSetCC(DL, MatchingVecType, N00, N01, CC);
          return DAG.getSExtOrTrunc(VsetCC, DL, VT);
        }
      }
    }

    // sext(setcc x, y, cc) -> (select (setcc x, y, cc), T, 0)
    // Here, T can be 1 or -1, depending on the type of the setcc and
    // getBooleanContents().
    unsigned SetCCWidth = N0.getScalarValueSizeInBits();

    // To determine the "true" side of the select, we need to know the high bit
    // of the value returned by the setcc if it evaluates to true.
    // If the type of the setcc is i1, then the true case of the select is just
    // sext(i1 1), that is, -1.
    // If the type of the setcc is larger (say, i8) then the value of the high
    // bit depends on getBooleanContents(), so ask TLI for a real "true" value
    // of the appropriate width.
    SDValue ExtTrueVal = (SetCCWidth == 1)
                             ? DAG.getAllOnesConstant(DL, VT)
                             : DAG.getBoolConstant(true, DL, VT, N00VT);
    SDValue Zero = DAG.getConstant(0, DL, VT);
    if (SDValue SCC =
            SimplifySelectCC(DL, N00, N01, ExtTrueVal, Zero, CC, true))
      return SCC;

    if (!VT.isVector() && !TLI.convertSelectOfConstantsToMath(VT)) {
      EVT SetCCVT = getSetCCResultType(N00VT);
      // Don't do this transform for i1 because there's a select transform
      // that would reverse it.
      // TODO: We should not do this transform at all without a target hook
      // because a sext is likely cheaper than a select?
      if (SetCCVT.getScalarSizeInBits() != 1 &&
          (!LegalOperations || TLI.isOperationLegal(ISD::SETCC, N00VT))) {
        SDValue SetCC = DAG.getSetCC(DL, SetCCVT, N00, N01, CC);
        return DAG.getSelect(DL, VT, SetCC, ExtTrueVal, Zero);
      }
    }
  }

  // fold (sext x) -> (zext x) if the sign bit is known zero.
  if ((!LegalOperations || TLI.isOperationLegal(ISD::ZERO_EXTEND, VT)) &&
      DAG.SignBitIsZero(N0))
    return DAG.getNode(ISD::ZERO_EXTEND, DL, VT, N0);

  if (SDValue NewVSel = matchVSelectOpSizesWithSetCC(N))
    return NewVSel;

  return SDValue();
}

// isTruncateOf - If N is a truncate of some other value, return true, record
// the value being truncated in Op and which of Op's bits are zero/one in Known.
// This function computes KnownBits to avoid a duplicated call to
// computeKnownBits in the caller.
static bool isTruncateOf(SelectionDAG &DAG, SDValue N, SDValue &Op,
                         KnownBits &Known) {
  if (N->getOpcode() == ISD::TRUNCATE) {
    Op = N->getOperand(0);
    Known = DAG.computeKnownBits(Op);
    return true;
  }

  if (N.getOpcode() != ISD::SETCC ||
      N.getValueType().getScalarType() != MVT::i1 ||
      cast<CondCodeSDNode>(N.getOperand(2))->get() != ISD::SETNE)
    return false;

  SDValue Op0 = N->getOperand(0);
  SDValue Op1 = N->getOperand(1);
  assert(Op0.getValueType() == Op1.getValueType());

  if (isNullOrNullSplat(Op0))
    Op = Op1;
  else if (isNullOrNullSplat(Op1))
    Op = Op0;
  else
    return false;

  Known = DAG.computeKnownBits(Op);

  return (Known.Zero | 1).isAllOnesValue();
}

SDValue DAGCombiner::visitZERO_EXTEND(SDNode *N) {
  SDValue N0 = N->getOperand(0);
  EVT VT = N->getValueType(0);

  if (SDValue Res = tryToFoldExtendOfConstant(N, TLI, DAG, LegalTypes))
    return Res;

  // fold (zext (zext x)) -> (zext x)
  // fold (zext (aext x)) -> (zext x)
  if (N0.getOpcode() == ISD::ZERO_EXTEND || N0.getOpcode() == ISD::ANY_EXTEND)
    return DAG.getNode(ISD::ZERO_EXTEND, SDLoc(N), VT,
                       N0.getOperand(0));

  // fold (zext (truncate x)) -> (zext x) or
  //      (zext (truncate x)) -> (truncate x)
  // This is valid when the truncated bits of x are already zero.
  SDValue Op;
  KnownBits Known;
  if (isTruncateOf(DAG, N0, Op, Known)) {
    APInt TruncatedBits =
      (Op.getScalarValueSizeInBits() == N0.getScalarValueSizeInBits()) ?
      APInt(Op.getScalarValueSizeInBits(), 0) :
      APInt::getBitsSet(Op.getScalarValueSizeInBits(),
                        N0.getScalarValueSizeInBits(),
                        std::min(Op.getScalarValueSizeInBits(),
                                 VT.getScalarSizeInBits()));
    if (TruncatedBits.isSubsetOf(Known.Zero))
      return DAG.getZExtOrTrunc(Op, SDLoc(N), VT);
  }

  // fold (zext (truncate x)) -> (and x, mask)
  if (N0.getOpcode() == ISD::TRUNCATE) {
    // fold (zext (truncate (load x))) -> (zext (smaller load x))
    // fold (zext (truncate (srl (load x), c))) -> (zext (smaller load (x+c/n)))
    if (SDValue NarrowLoad = ReduceLoadWidth(N0.getNode())) {
      SDNode *oye = N0.getOperand(0).getNode();
      if (NarrowLoad.getNode() != N0.getNode()) {
        CombineTo(N0.getNode(), NarrowLoad);
        // CombineTo deleted the truncate, if needed, but not what's under it.
        AddToWorklist(oye);
      }
      return SDValue(N, 0); // Return N so it doesn't get rechecked!
    }

    EVT SrcVT = N0.getOperand(0).getValueType();
    EVT MinVT = N0.getValueType();

    // Try to mask before the extension to avoid having to generate a larger mask,
    // possibly over several sub-vectors.
    if (SrcVT.bitsLT(VT) && VT.isVector()) {
      if (!LegalOperations || (TLI.isOperationLegal(ISD::AND, SrcVT) &&
                               TLI.isOperationLegal(ISD::ZERO_EXTEND, VT))) {
        SDValue Op = N0.getOperand(0);
        Op = DAG.getZeroExtendInReg(Op, SDLoc(N), MinVT.getScalarType());
        AddToWorklist(Op.getNode());
        SDValue ZExtOrTrunc = DAG.getZExtOrTrunc(Op, SDLoc(N), VT);
        // Transfer the debug info; the new node is equivalent to N0.
        DAG.transferDbgValues(N0, ZExtOrTrunc);
        return ZExtOrTrunc;
      }
    }

    if (!LegalOperations || TLI.isOperationLegal(ISD::AND, VT)) {
      SDValue Op = DAG.getAnyExtOrTrunc(N0.getOperand(0), SDLoc(N), VT);
      AddToWorklist(Op.getNode());
      SDValue And = DAG.getZeroExtendInReg(Op, SDLoc(N), MinVT.getScalarType());
      // We may safely transfer the debug info describing the truncate node over
      // to the equivalent and operation.
      DAG.transferDbgValues(N0, And);
      return And;
    }
  }

  // Fold (zext (and (trunc x), cst)) -> (and x, cst),
  // if either of the casts is not free.
  if (N0.getOpcode() == ISD::AND &&
      N0.getOperand(0).getOpcode() == ISD::TRUNCATE &&
      N0.getOperand(1).getOpcode() == ISD::Constant &&
      (!TLI.isTruncateFree(N0.getOperand(0).getOperand(0).getValueType(),
                           N0.getValueType()) ||
       !TLI.isZExtFree(N0.getValueType(), VT))) {
    SDValue X = N0.getOperand(0).getOperand(0);
    X = DAG.getAnyExtOrTrunc(X, SDLoc(X), VT);
    APInt Mask = cast<ConstantSDNode>(N0.getOperand(1))->getAPIntValue();
    Mask = Mask.zext(VT.getSizeInBits());
    SDLoc DL(N);
    return DAG.getNode(ISD::AND, DL, VT,
                       X, DAG.getConstant(Mask, DL, VT));
  }

  // Try to simplify (zext (load x)).
  if (SDValue foldedExt =
          tryToFoldExtOfLoad(DAG, *this, TLI, VT, LegalOperations, N, N0,
                             ISD::ZEXTLOAD, ISD::ZERO_EXTEND))
    return foldedExt;

  // fold (zext (load x)) to multiple smaller zextloads.
  // Only on illegal but splittable vectors.
  if (SDValue ExtLoad = CombineExtLoad(N))
    return ExtLoad;

  // fold (zext (and/or/xor (load x), cst)) ->
  //      (and/or/xor (zextload x), (zext cst))
  // Unless (and (load x) cst) will match as a zextload already and has
  // additional users.
  if ((N0.getOpcode() == ISD::AND || N0.getOpcode() == ISD::OR ||
       N0.getOpcode() == ISD::XOR) &&
      isa<LoadSDNode>(N0.getOperand(0)) &&
      N0.getOperand(1).getOpcode() == ISD::Constant &&
      (!LegalOperations && TLI.isOperationLegal(N0.getOpcode(), VT))) {
    LoadSDNode *LN00 = cast<LoadSDNode>(N0.getOperand(0));
    EVT MemVT = LN00->getMemoryVT();
    if (TLI.isLoadExtLegal(ISD::ZEXTLOAD, VT, MemVT) &&
        LN00->getExtensionType() != ISD::SEXTLOAD && LN00->isUnindexed()) {
      bool DoXform = true;
      SmallVector<SDNode*, 4> SetCCs;
      if (!N0.hasOneUse()) {
        if (N0.getOpcode() == ISD::AND) {
          auto *AndC = cast<ConstantSDNode>(N0.getOperand(1));
          EVT LoadResultTy = AndC->getValueType(0);
          EVT ExtVT;
          if (isAndLoadExtLoad(AndC, LN00, LoadResultTy, ExtVT))
            DoXform = false;
        }
      }
      if (DoXform)
        DoXform = ExtendUsesToFormExtLoad(VT, N0.getNode(), N0.getOperand(0),
                                          ISD::ZERO_EXTEND, SetCCs, TLI);
      if (DoXform) {
        SDValue ExtLoad = DAG.getExtLoad(ISD::ZEXTLOAD, SDLoc(LN00), VT,
                                         LN00->getChain(), LN00->getBasePtr(),
                                         LN00->getMemoryVT(),
                                         LN00->getMemOperand());
        APInt Mask = cast<ConstantSDNode>(N0.getOperand(1))->getAPIntValue();
        Mask = Mask.zext(VT.getSizeInBits());
        SDLoc DL(N);
        SDValue And = DAG.getNode(N0.getOpcode(), DL, VT,
                                  ExtLoad, DAG.getConstant(Mask, DL, VT));
        ExtendSetCCUses(SetCCs, N0.getOperand(0), ExtLoad, ISD::ZERO_EXTEND);
        bool NoReplaceTruncAnd = !N0.hasOneUse();
        bool NoReplaceTrunc = SDValue(LN00, 0).hasOneUse();
        CombineTo(N, And);
        // If N0 has multiple uses, change other uses as well.
        if (NoReplaceTruncAnd) {
          SDValue TruncAnd =
              DAG.getNode(ISD::TRUNCATE, DL, N0.getValueType(), And);
          CombineTo(N0.getNode(), TruncAnd);
        }
        if (NoReplaceTrunc) {
          DAG.ReplaceAllUsesOfValueWith(SDValue(LN00, 1), ExtLoad.getValue(1));
        } else {
          SDValue Trunc = DAG.getNode(ISD::TRUNCATE, SDLoc(LN00),
                                      LN00->getValueType(0), ExtLoad);
          CombineTo(LN00, Trunc, ExtLoad.getValue(1));
        }
        return SDValue(N,0); // Return N so it doesn't get rechecked!
      }
    }
  }

  // fold (zext (and/or/xor (shl/shr (load x), cst), cst)) ->
  //      (and/or/xor (shl/shr (zextload x), (zext cst)), (zext cst))
  if (SDValue ZExtLoad = CombineZExtLogicopShiftLoad(N))
    return ZExtLoad;

  // Try to simplify (zext (zextload x)).
  if (SDValue foldedExt = tryToFoldExtOfExtload(
          DAG, *this, TLI, VT, LegalOperations, N, N0, ISD::ZEXTLOAD))
    return foldedExt;

  if (SDValue V = foldExtendedSignBitTest(N, DAG, LegalOperations))
    return V;

  if (N0.getOpcode() == ISD::SETCC) {
    // Only do this before legalize for now.
    if (!LegalOperations && VT.isVector() &&
        N0.getValueType().getVectorElementType() == MVT::i1) {
      EVT N00VT = N0.getOperand(0).getValueType();
      if (getSetCCResultType(N00VT) == N0.getValueType())
        return SDValue();

      // We know that the # elements of the results is the same as the #
      // elements of the compare (and the # elements of the compare result for
      // that matter). Check to see that they are the same size. If so, we know
      // that the element size of the sext'd result matches the element size of
      // the compare operands.
      SDLoc DL(N);
      SDValue VecOnes = DAG.getConstant(1, DL, VT);
      if (VT.getSizeInBits() == N00VT.getSizeInBits()) {
        // zext(setcc) -> (and (vsetcc), (1, 1, ...) for vectors.
        SDValue VSetCC = DAG.getNode(ISD::SETCC, DL, VT, N0.getOperand(0),
                                     N0.getOperand(1), N0.getOperand(2));
        return DAG.getNode(ISD::AND, DL, VT, VSetCC, VecOnes);
      }

      // If the desired elements are smaller or larger than the source
      // elements we can use a matching integer vector type and then
      // truncate/sign extend.
      EVT MatchingVectorType = N00VT.changeVectorElementTypeToInteger();
      SDValue VsetCC =
          DAG.getNode(ISD::SETCC, DL, MatchingVectorType, N0.getOperand(0),
                      N0.getOperand(1), N0.getOperand(2));
      return DAG.getNode(ISD::AND, DL, VT, DAG.getSExtOrTrunc(VsetCC, DL, VT),
                         VecOnes);
    }

    // zext(setcc x,y,cc) -> select_cc x, y, 1, 0, cc
    SDLoc DL(N);
    if (SDValue SCC = SimplifySelectCC(
            DL, N0.getOperand(0), N0.getOperand(1), DAG.getConstant(1, DL, VT),
            DAG.getConstant(0, DL, VT),
            cast<CondCodeSDNode>(N0.getOperand(2))->get(), true))
      return SCC;
  }

  // (zext (shl (zext x), cst)) -> (shl (zext x), cst)
  if ((N0.getOpcode() == ISD::SHL || N0.getOpcode() == ISD::SRL) &&
      isa<ConstantSDNode>(N0.getOperand(1)) &&
      N0.getOperand(0).getOpcode() == ISD::ZERO_EXTEND &&
      N0.hasOneUse()) {
    SDValue ShAmt = N0.getOperand(1);
    unsigned ShAmtVal = cast<ConstantSDNode>(ShAmt)->getZExtValue();
    if (N0.getOpcode() == ISD::SHL) {
      SDValue InnerZExt = N0.getOperand(0);
      // If the original shl may be shifting out bits, do not perform this
      // transformation.
      unsigned KnownZeroBits = InnerZExt.getValueSizeInBits() -
        InnerZExt.getOperand(0).getValueSizeInBits();
      if (ShAmtVal > KnownZeroBits)
        return SDValue();
    }

    SDLoc DL(N);

    // Ensure that the shift amount is wide enough for the shifted value.
    if (VT.getSizeInBits() >= 256)
      ShAmt = DAG.getNode(ISD::ZERO_EXTEND, DL, MVT::i32, ShAmt);

    return DAG.getNode(N0.getOpcode(), DL, VT,
                       DAG.getNode(ISD::ZERO_EXTEND, DL, VT, N0.getOperand(0)),
                       ShAmt);
  }

  if (SDValue NewVSel = matchVSelectOpSizesWithSetCC(N))
    return NewVSel;

  return SDValue();
}

SDValue DAGCombiner::visitANY_EXTEND(SDNode *N) {
  SDValue N0 = N->getOperand(0);
  EVT VT = N->getValueType(0);

  if (SDValue Res = tryToFoldExtendOfConstant(N, TLI, DAG, LegalTypes))
    return Res;

  // fold (aext (aext x)) -> (aext x)
  // fold (aext (zext x)) -> (zext x)
  // fold (aext (sext x)) -> (sext x)
  if (N0.getOpcode() == ISD::ANY_EXTEND  ||
      N0.getOpcode() == ISD::ZERO_EXTEND ||
      N0.getOpcode() == ISD::SIGN_EXTEND)
    return DAG.getNode(N0.getOpcode(), SDLoc(N), VT, N0.getOperand(0));

  // fold (aext (truncate (load x))) -> (aext (smaller load x))
  // fold (aext (truncate (srl (load x), c))) -> (aext (small load (x+c/n)))
  if (N0.getOpcode() == ISD::TRUNCATE) {
    if (SDValue NarrowLoad = ReduceLoadWidth(N0.getNode())) {
      SDNode *oye = N0.getOperand(0).getNode();
      if (NarrowLoad.getNode() != N0.getNode()) {
        CombineTo(N0.getNode(), NarrowLoad);
        // CombineTo deleted the truncate, if needed, but not what's under it.
        AddToWorklist(oye);
      }
      return SDValue(N, 0);   // Return N so it doesn't get rechecked!
    }
  }

  // fold (aext (truncate x))
  if (N0.getOpcode() == ISD::TRUNCATE)
    return DAG.getAnyExtOrTrunc(N0.getOperand(0), SDLoc(N), VT);

  // Fold (aext (and (trunc x), cst)) -> (and x, cst)
  // if the trunc is not free.
  if (N0.getOpcode() == ISD::AND &&
      N0.getOperand(0).getOpcode() == ISD::TRUNCATE &&
      N0.getOperand(1).getOpcode() == ISD::Constant &&
      !TLI.isTruncateFree(N0.getOperand(0).getOperand(0).getValueType(),
                          N0.getValueType())) {
    SDLoc DL(N);
    SDValue X = N0.getOperand(0).getOperand(0);
    X = DAG.getAnyExtOrTrunc(X, DL, VT);
    APInt Mask = cast<ConstantSDNode>(N0.getOperand(1))->getAPIntValue();
    Mask = Mask.zext(VT.getSizeInBits());
    return DAG.getNode(ISD::AND, DL, VT,
                       X, DAG.getConstant(Mask, DL, VT));
  }

  // fold (aext (load x)) -> (aext (truncate (extload x)))
  // None of the supported targets knows how to perform load and any_ext
  // on vectors in one instruction.  We only perform this transformation on
  // scalars.
  if (ISD::isNON_EXTLoad(N0.getNode()) && !VT.isVector() &&
      ISD::isUNINDEXEDLoad(N0.getNode()) &&
      TLI.isLoadExtLegal(ISD::EXTLOAD, VT, N0.getValueType())) {
    bool DoXform = true;
    SmallVector<SDNode*, 4> SetCCs;
    if (!N0.hasOneUse())
      DoXform = ExtendUsesToFormExtLoad(VT, N, N0, ISD::ANY_EXTEND, SetCCs,
                                        TLI);
    if (DoXform) {
      LoadSDNode *LN0 = cast<LoadSDNode>(N0);
      SDValue ExtLoad = DAG.getExtLoad(ISD::EXTLOAD, SDLoc(N), VT,
                                       LN0->getChain(),
                                       LN0->getBasePtr(), N0.getValueType(),
                                       LN0->getMemOperand());
      ExtendSetCCUses(SetCCs, N0, ExtLoad, ISD::ANY_EXTEND);
      // If the load value is used only by N, replace it via CombineTo N.
      bool NoReplaceTrunc = N0.hasOneUse();
      CombineTo(N, ExtLoad);
      if (NoReplaceTrunc) {
        DAG.ReplaceAllUsesOfValueWith(SDValue(LN0, 1), ExtLoad.getValue(1));
      } else {
        SDValue Trunc = DAG.getNode(ISD::TRUNCATE, SDLoc(N0),
                                    N0.getValueType(), ExtLoad);
        CombineTo(LN0, Trunc, ExtLoad.getValue(1));
      }
      return SDValue(N, 0); // Return N so it doesn't get rechecked!
    }
  }

  // fold (aext (zextload x)) -> (aext (truncate (zextload x)))
  // fold (aext (sextload x)) -> (aext (truncate (sextload x)))
  // fold (aext ( extload x)) -> (aext (truncate (extload  x)))
  if (N0.getOpcode() == ISD::LOAD && !ISD::isNON_EXTLoad(N0.getNode()) &&
      ISD::isUNINDEXEDLoad(N0.getNode()) && N0.hasOneUse()) {
    LoadSDNode *LN0 = cast<LoadSDNode>(N0);
    ISD::LoadExtType ExtType = LN0->getExtensionType();
    EVT MemVT = LN0->getMemoryVT();
    if (!LegalOperations || TLI.isLoadExtLegal(ExtType, VT, MemVT)) {
      SDValue ExtLoad = DAG.getExtLoad(ExtType, SDLoc(N),
                                       VT, LN0->getChain(), LN0->getBasePtr(),
                                       MemVT, LN0->getMemOperand());
      CombineTo(N, ExtLoad);
      DAG.ReplaceAllUsesOfValueWith(SDValue(LN0, 1), ExtLoad.getValue(1));
      return SDValue(N, 0);   // Return N so it doesn't get rechecked!
    }
  }

  if (N0.getOpcode() == ISD::SETCC) {
    // For vectors:
    // aext(setcc) -> vsetcc
    // aext(setcc) -> truncate(vsetcc)
    // aext(setcc) -> aext(vsetcc)
    // Only do this before legalize for now.
    if (VT.isVector() && !LegalOperations) {
      EVT N00VT = N0.getOperand(0).getValueType();
      if (getSetCCResultType(N00VT) == N0.getValueType())
        return SDValue();

      // We know that the # elements of the results is the same as the
      // # elements of the compare (and the # elements of the compare result
      // for that matter).  Check to see that they are the same size.  If so,
      // we know that the element size of the sext'd result matches the
      // element size of the compare operands.
      if (VT.getSizeInBits() == N00VT.getSizeInBits())
        return DAG.getSetCC(SDLoc(N), VT, N0.getOperand(0),
                             N0.getOperand(1),
                             cast<CondCodeSDNode>(N0.getOperand(2))->get());

      // If the desired elements are smaller or larger than the source
      // elements we can use a matching integer vector type and then
      // truncate/any extend
      EVT MatchingVectorType = N00VT.changeVectorElementTypeToInteger();
      SDValue VsetCC =
        DAG.getSetCC(SDLoc(N), MatchingVectorType, N0.getOperand(0),
                      N0.getOperand(1),
                      cast<CondCodeSDNode>(N0.getOperand(2))->get());
      return DAG.getAnyExtOrTrunc(VsetCC, SDLoc(N), VT);
    }

    // aext(setcc x,y,cc) -> select_cc x, y, 1, 0, cc
    SDLoc DL(N);
    if (SDValue SCC = SimplifySelectCC(
            DL, N0.getOperand(0), N0.getOperand(1), DAG.getConstant(1, DL, VT),
            DAG.getConstant(0, DL, VT),
            cast<CondCodeSDNode>(N0.getOperand(2))->get(), true))
      return SCC;
  }

  return SDValue();
}

SDValue DAGCombiner::visitAssertExt(SDNode *N) {
  unsigned Opcode = N->getOpcode();
  SDValue N0 = N->getOperand(0);
  SDValue N1 = N->getOperand(1);
  EVT AssertVT = cast<VTSDNode>(N1)->getVT();

  // fold (assert?ext (assert?ext x, vt), vt) -> (assert?ext x, vt)
  if (N0.getOpcode() == Opcode &&
      AssertVT == cast<VTSDNode>(N0.getOperand(1))->getVT())
    return N0;

  if (N0.getOpcode() == ISD::TRUNCATE && N0.hasOneUse() &&
      N0.getOperand(0).getOpcode() == Opcode) {
    // We have an assert, truncate, assert sandwich. Make one stronger assert
    // by asserting on the smallest asserted type to the larger source type.
    // This eliminates the later assert:
    // assert (trunc (assert X, i8) to iN), i1 --> trunc (assert X, i1) to iN
    // assert (trunc (assert X, i1) to iN), i8 --> trunc (assert X, i1) to iN
    SDValue BigA = N0.getOperand(0);
    EVT BigA_AssertVT = cast<VTSDNode>(BigA.getOperand(1))->getVT();
    assert(BigA_AssertVT.bitsLE(N0.getValueType()) &&
           "Asserting zero/sign-extended bits to a type larger than the "
           "truncated destination does not provide information");

    SDLoc DL(N);
    EVT MinAssertVT = AssertVT.bitsLT(BigA_AssertVT) ? AssertVT : BigA_AssertVT;
    SDValue MinAssertVTVal = DAG.getValueType(MinAssertVT);
    SDValue NewAssert = DAG.getNode(Opcode, DL, BigA.getValueType(),
                                    BigA.getOperand(0), MinAssertVTVal);
    return DAG.getNode(ISD::TRUNCATE, DL, N->getValueType(0), NewAssert);
  }

  // If we have (AssertZext (truncate (AssertSext X, iX)), iY) and Y is smaller
  // than X. Just move the AssertZext in front of the truncate and drop the
  // AssertSExt.
  if (N0.getOpcode() == ISD::TRUNCATE && N0.hasOneUse() &&
      N0.getOperand(0).getOpcode() == ISD::AssertSext &&
      Opcode == ISD::AssertZext) {
    SDValue BigA = N0.getOperand(0);
    EVT BigA_AssertVT = cast<VTSDNode>(BigA.getOperand(1))->getVT();
    assert(BigA_AssertVT.bitsLE(N0.getValueType()) &&
           "Asserting zero/sign-extended bits to a type larger than the "
           "truncated destination does not provide information");

    if (AssertVT.bitsLT(BigA_AssertVT)) {
      SDLoc DL(N);
      SDValue NewAssert = DAG.getNode(Opcode, DL, BigA.getValueType(),
                                      BigA.getOperand(0), N1);
      return DAG.getNode(ISD::TRUNCATE, DL, N->getValueType(0), NewAssert);
    }
  }

  return SDValue();
}

/// If the result of a wider load is shifted to right of N  bits and then
/// truncated to a narrower type and where N is a multiple of number of bits of
/// the narrower type, transform it to a narrower load from address + N / num of
/// bits of new type. Also narrow the load if the result is masked with an AND
/// to effectively produce a smaller type. If the result is to be extended, also
/// fold the extension to form a extending load.
SDValue DAGCombiner::ReduceLoadWidth(SDNode *N) {
  unsigned Opc = N->getOpcode();

  ISD::LoadExtType ExtType = ISD::NON_EXTLOAD;
  SDValue N0 = N->getOperand(0);
  EVT VT = N->getValueType(0);
  EVT ExtVT = VT;

  // This transformation isn't valid for vector loads.
  if (VT.isVector())
    return SDValue();

  unsigned ShAmt = 0;
  bool HasShiftedOffset = false;
  // Special case: SIGN_EXTEND_INREG is basically truncating to ExtVT then
  // extended to VT.
  if (Opc == ISD::SIGN_EXTEND_INREG) {
    ExtType = ISD::SEXTLOAD;
    ExtVT = cast<VTSDNode>(N->getOperand(1))->getVT();
  } else if (Opc == ISD::SRL) {
    // Another special-case: SRL is basically zero-extending a narrower value,
    // or it maybe shifting a higher subword, half or byte into the lowest
    // bits.
    ExtType = ISD::ZEXTLOAD;
    N0 = SDValue(N, 0);

    auto *LN0 = dyn_cast<LoadSDNode>(N0.getOperand(0));
    auto *N01 = dyn_cast<ConstantSDNode>(N0.getOperand(1));
    if (!N01 || !LN0)
      return SDValue();

    uint64_t ShiftAmt = N01->getZExtValue();
    uint64_t MemoryWidth = LN0->getMemoryVT().getSizeInBits();
    if (LN0->getExtensionType() != ISD::SEXTLOAD && MemoryWidth > ShiftAmt)
      ExtVT = EVT::getIntegerVT(*DAG.getContext(), MemoryWidth - ShiftAmt);
    else
      ExtVT = EVT::getIntegerVT(*DAG.getContext(),
                                VT.getSizeInBits() - ShiftAmt);
  } else if (Opc == ISD::AND) {
    // An AND with a constant mask is the same as a truncate + zero-extend.
    auto AndC = dyn_cast<ConstantSDNode>(N->getOperand(1));
    if (!AndC)
      return SDValue();

    const APInt &Mask = AndC->getAPIntValue();
    unsigned ActiveBits = 0;
    if (Mask.isMask()) {
      ActiveBits = Mask.countTrailingOnes();
    } else if (Mask.isShiftedMask()) {
      ShAmt = Mask.countTrailingZeros();
      APInt ShiftedMask = Mask.lshr(ShAmt);
      ActiveBits = ShiftedMask.countTrailingOnes();
      HasShiftedOffset = true;
    } else
      return SDValue();

    ExtType = ISD::ZEXTLOAD;
    ExtVT = EVT::getIntegerVT(*DAG.getContext(), ActiveBits);
  }

  if (N0.getOpcode() == ISD::SRL && N0.hasOneUse()) {
    SDValue SRL = N0;
    if (auto *ConstShift = dyn_cast<ConstantSDNode>(SRL.getOperand(1))) {
      ShAmt = ConstShift->getZExtValue();
      unsigned EVTBits = ExtVT.getSizeInBits();
      // Is the shift amount a multiple of size of VT?
      if ((ShAmt & (EVTBits-1)) == 0) {
        N0 = N0.getOperand(0);
        // Is the load width a multiple of size of VT?
        if ((N0.getValueSizeInBits() & (EVTBits-1)) != 0)
          return SDValue();
      }

      // At this point, we must have a load or else we can't do the transform.
      if (!isa<LoadSDNode>(N0)) return SDValue();

      auto *LN0 = cast<LoadSDNode>(N0);

      // Because a SRL must be assumed to *need* to zero-extend the high bits
      // (as opposed to anyext the high bits), we can't combine the zextload
      // lowering of SRL and an sextload.
      if (LN0->getExtensionType() == ISD::SEXTLOAD)
        return SDValue();

      // If the shift amount is larger than the input type then we're not
      // accessing any of the loaded bytes.  If the load was a zextload/extload
      // then the result of the shift+trunc is zero/undef (handled elsewhere).
      if (ShAmt >= LN0->getMemoryVT().getSizeInBits())
        return SDValue();

      // If the SRL is only used by a masking AND, we may be able to adjust
      // the ExtVT to make the AND redundant.
      SDNode *Mask = *(SRL->use_begin());
      if (Mask->getOpcode() == ISD::AND &&
          isa<ConstantSDNode>(Mask->getOperand(1))) {
        const APInt &ShiftMask =
          cast<ConstantSDNode>(Mask->getOperand(1))->getAPIntValue();
        if (ShiftMask.isMask()) {
          EVT MaskedVT = EVT::getIntegerVT(*DAG.getContext(),
                                           ShiftMask.countTrailingOnes());
          // If the mask is smaller, recompute the type.
          if ((ExtVT.getSizeInBits() > MaskedVT.getSizeInBits()) &&
              TLI.isLoadExtLegal(ExtType, N0.getValueType(), MaskedVT))
            ExtVT = MaskedVT;
        }
      }
    }
  }

  // If the load is shifted left (and the result isn't shifted back right),
  // we can fold the truncate through the shift.
  unsigned ShLeftAmt = 0;
  if (ShAmt == 0 && N0.getOpcode() == ISD::SHL && N0.hasOneUse() &&
      ExtVT == VT && TLI.isNarrowingProfitable(N0.getValueType(), VT)) {
    if (ConstantSDNode *N01 = dyn_cast<ConstantSDNode>(N0.getOperand(1))) {
      ShLeftAmt = N01->getZExtValue();
      N0 = N0.getOperand(0);
    }
  }

  // If we haven't found a load, we can't narrow it.
  if (!isa<LoadSDNode>(N0))
    return SDValue();

  LoadSDNode *LN0 = cast<LoadSDNode>(N0);
  if (!isLegalNarrowLdSt(LN0, ExtType, ExtVT, ShAmt))
    return SDValue();

  auto AdjustBigEndianShift = [&](unsigned ShAmt) {
    unsigned LVTStoreBits = LN0->getMemoryVT().getStoreSizeInBits();
    unsigned EVTStoreBits = ExtVT.getStoreSizeInBits();
    return LVTStoreBits - EVTStoreBits - ShAmt;
  };

  // For big endian targets, we need to adjust the offset to the pointer to
  // load the correct bytes.
  if (DAG.getDataLayout().isBigEndian())
    ShAmt = AdjustBigEndianShift(ShAmt);

  EVT PtrType = N0.getOperand(1).getValueType();
  uint64_t PtrOff = ShAmt / 8;
  unsigned NewAlign = MinAlign(LN0->getAlignment(), PtrOff);
  SDLoc DL(LN0);
  // The original load itself didn't wrap, so an offset within it doesn't.
  SDNodeFlags Flags;
  Flags.setNoUnsignedWrap(true);
  SDValue NewPtr = DAG.getNode(ISD::ADD, DL,
                               PtrType, LN0->getBasePtr(),
                               DAG.getConstant(PtrOff, DL, PtrType),
                               Flags);
  AddToWorklist(NewPtr.getNode());

  SDValue Load;
  if (ExtType == ISD::NON_EXTLOAD)
    Load = DAG.getLoad(VT, SDLoc(N0), LN0->getChain(), NewPtr,
                       LN0->getPointerInfo().getWithOffset(PtrOff), NewAlign,
                       LN0->getMemOperand()->getFlags(), LN0->getAAInfo());
  else
    Load = DAG.getExtLoad(ExtType, SDLoc(N0), VT, LN0->getChain(), NewPtr,
                          LN0->getPointerInfo().getWithOffset(PtrOff), ExtVT,
                          NewAlign, LN0->getMemOperand()->getFlags(),
                          LN0->getAAInfo());

  // Replace the old load's chain with the new load's chain.
  WorklistRemover DeadNodes(*this);
  DAG.ReplaceAllUsesOfValueWith(N0.getValue(1), Load.getValue(1));

  // Shift the result left, if we've swallowed a left shift.
  SDValue Result = Load;
  if (ShLeftAmt != 0) {
    EVT ShImmTy = getShiftAmountTy(Result.getValueType());
    if (!isUIntN(ShImmTy.getSizeInBits(), ShLeftAmt))
      ShImmTy = VT;
    // If the shift amount is as large as the result size (but, presumably,
    // no larger than the source) then the useful bits of the result are
    // zero; we can't simply return the shortened shift, because the result
    // of that operation is undefined.
    SDLoc DL(N0);
    if (ShLeftAmt >= VT.getSizeInBits())
      Result = DAG.getConstant(0, DL, VT);
    else
      Result = DAG.getNode(ISD::SHL, DL, VT,
                          Result, DAG.getConstant(ShLeftAmt, DL, ShImmTy));
  }

  if (HasShiftedOffset) {
    // Recalculate the shift amount after it has been altered to calculate
    // the offset.
    if (DAG.getDataLayout().isBigEndian())
      ShAmt = AdjustBigEndianShift(ShAmt);

    // We're using a shifted mask, so the load now has an offset. This means
    // that data has been loaded into the lower bytes than it would have been
    // before, so we need to shl the loaded data into the correct position in the
    // register.
    SDValue ShiftC = DAG.getConstant(ShAmt, DL, VT);
    Result = DAG.getNode(ISD::SHL, DL, VT, Result, ShiftC);
    DAG.ReplaceAllUsesOfValueWith(SDValue(N, 0), Result);
  }

  // Return the new loaded value.
  return Result;
}

SDValue DAGCombiner::visitSIGN_EXTEND_INREG(SDNode *N) {
  SDValue N0 = N->getOperand(0);
  SDValue N1 = N->getOperand(1);
  EVT VT = N->getValueType(0);
  EVT EVT = cast<VTSDNode>(N1)->getVT();
  unsigned VTBits = VT.getScalarSizeInBits();
  unsigned EVTBits = EVT.getScalarSizeInBits();

  if (N0.isUndef())
    return DAG.getUNDEF(VT);

  // fold (sext_in_reg c1) -> c1
  if (DAG.isConstantIntBuildVectorOrConstantInt(N0))
    return DAG.getNode(ISD::SIGN_EXTEND_INREG, SDLoc(N), VT, N0, N1);

  // If the input is already sign extended, just drop the extension.
  if (DAG.ComputeNumSignBits(N0) >= VTBits-EVTBits+1)
    return N0;

  // fold (sext_in_reg (sext_in_reg x, VT2), VT1) -> (sext_in_reg x, minVT) pt2
  if (N0.getOpcode() == ISD::SIGN_EXTEND_INREG &&
      EVT.bitsLT(cast<VTSDNode>(N0.getOperand(1))->getVT()))
    return DAG.getNode(ISD::SIGN_EXTEND_INREG, SDLoc(N), VT,
                       N0.getOperand(0), N1);

  // fold (sext_in_reg (sext x)) -> (sext x)
  // fold (sext_in_reg (aext x)) -> (sext x)
  // if x is small enough or if we know that x has more than 1 sign bit and the
  // sign_extend_inreg is extending from one of them.
  if (N0.getOpcode() == ISD::SIGN_EXTEND || N0.getOpcode() == ISD::ANY_EXTEND) {
    SDValue N00 = N0.getOperand(0);
    unsigned N00Bits = N00.getScalarValueSizeInBits();
    if ((N00Bits <= EVTBits ||
         (N00Bits - DAG.ComputeNumSignBits(N00)) < EVTBits) &&
        (!LegalOperations || TLI.isOperationLegal(ISD::SIGN_EXTEND, VT)))
      return DAG.getNode(ISD::SIGN_EXTEND, SDLoc(N), VT, N00);
  }

  // fold (sext_in_reg (*_extend_vector_inreg x)) -> (sext_vector_inreg x)
  if ((N0.getOpcode() == ISD::ANY_EXTEND_VECTOR_INREG ||
       N0.getOpcode() == ISD::SIGN_EXTEND_VECTOR_INREG ||
       N0.getOpcode() == ISD::ZERO_EXTEND_VECTOR_INREG) &&
      N0.getOperand(0).getScalarValueSizeInBits() == EVTBits) {
    if (!LegalOperations ||
        TLI.isOperationLegal(ISD::SIGN_EXTEND_VECTOR_INREG, VT))
      return DAG.getNode(ISD::SIGN_EXTEND_VECTOR_INREG, SDLoc(N), VT,
                         N0.getOperand(0));
  }

  // fold (sext_in_reg (zext x)) -> (sext x)
  // iff we are extending the source sign bit.
  if (N0.getOpcode() == ISD::ZERO_EXTEND) {
    SDValue N00 = N0.getOperand(0);
    if (N00.getScalarValueSizeInBits() == EVTBits &&
        (!LegalOperations || TLI.isOperationLegal(ISD::SIGN_EXTEND, VT)))
      return DAG.getNode(ISD::SIGN_EXTEND, SDLoc(N), VT, N00, N1);
  }

  // fold (sext_in_reg x) -> (zext_in_reg x) if the sign bit is known zero.
  if (DAG.MaskedValueIsZero(N0, APInt::getOneBitSet(VTBits, EVTBits - 1)))
    return DAG.getZeroExtendInReg(N0, SDLoc(N), EVT.getScalarType());

  // fold operands of sext_in_reg based on knowledge that the top bits are not
  // demanded.
  if (SimplifyDemandedBits(SDValue(N, 0)))
    return SDValue(N, 0);

  // fold (sext_in_reg (load x)) -> (smaller sextload x)
  // fold (sext_in_reg (srl (load x), c)) -> (smaller sextload (x+c/evtbits))
  if (SDValue NarrowLoad = ReduceLoadWidth(N))
    return NarrowLoad;

  // fold (sext_in_reg (srl X, 24), i8) -> (sra X, 24)
  // fold (sext_in_reg (srl X, 23), i8) -> (sra X, 23) iff possible.
  // We already fold "(sext_in_reg (srl X, 25), i8) -> srl X, 25" above.
  if (N0.getOpcode() == ISD::SRL) {
    if (ConstantSDNode *ShAmt = dyn_cast<ConstantSDNode>(N0.getOperand(1)))
      if (ShAmt->getZExtValue()+EVTBits <= VTBits) {
        // We can turn this into an SRA iff the input to the SRL is already sign
        // extended enough.
        unsigned InSignBits = DAG.ComputeNumSignBits(N0.getOperand(0));
        if (VTBits-(ShAmt->getZExtValue()+EVTBits) < InSignBits)
          return DAG.getNode(ISD::SRA, SDLoc(N), VT,
                             N0.getOperand(0), N0.getOperand(1));
      }
  }

  // fold (sext_inreg (extload x)) -> (sextload x)
  // If sextload is not supported by target, we can only do the combine when
  // load has one use. Doing otherwise can block folding the extload with other
  // extends that the target does support.
  if (ISD::isEXTLoad(N0.getNode()) &&
      ISD::isUNINDEXEDLoad(N0.getNode()) &&
      EVT == cast<LoadSDNode>(N0)->getMemoryVT() &&
      ((!LegalOperations && !cast<LoadSDNode>(N0)->isVolatile() &&
        N0.hasOneUse()) ||
       TLI.isLoadExtLegal(ISD::SEXTLOAD, VT, EVT))) {
    LoadSDNode *LN0 = cast<LoadSDNode>(N0);
    SDValue ExtLoad = DAG.getExtLoad(ISD::SEXTLOAD, SDLoc(N), VT,
                                     LN0->getChain(),
                                     LN0->getBasePtr(), EVT,
                                     LN0->getMemOperand());
    CombineTo(N, ExtLoad);
    CombineTo(N0.getNode(), ExtLoad, ExtLoad.getValue(1));
    AddToWorklist(ExtLoad.getNode());
    return SDValue(N, 0);   // Return N so it doesn't get rechecked!
  }
  // fold (sext_inreg (zextload x)) -> (sextload x) iff load has one use
  if (ISD::isZEXTLoad(N0.getNode()) && ISD::isUNINDEXEDLoad(N0.getNode()) &&
      N0.hasOneUse() &&
      EVT == cast<LoadSDNode>(N0)->getMemoryVT() &&
      ((!LegalOperations && !cast<LoadSDNode>(N0)->isVolatile()) ||
       TLI.isLoadExtLegal(ISD::SEXTLOAD, VT, EVT))) {
    LoadSDNode *LN0 = cast<LoadSDNode>(N0);
    SDValue ExtLoad = DAG.getExtLoad(ISD::SEXTLOAD, SDLoc(N), VT,
                                     LN0->getChain(),
                                     LN0->getBasePtr(), EVT,
                                     LN0->getMemOperand());
    CombineTo(N, ExtLoad);
    CombineTo(N0.getNode(), ExtLoad, ExtLoad.getValue(1));
    return SDValue(N, 0);   // Return N so it doesn't get rechecked!
  }

  // Form (sext_inreg (bswap >> 16)) or (sext_inreg (rotl (bswap) 16))
  if (EVTBits <= 16 && N0.getOpcode() == ISD::OR) {
    if (SDValue BSwap = MatchBSwapHWordLow(N0.getNode(), N0.getOperand(0),
                                           N0.getOperand(1), false))
      return DAG.getNode(ISD::SIGN_EXTEND_INREG, SDLoc(N), VT,
                         BSwap, N1);
  }

  return SDValue();
}

SDValue DAGCombiner::visitSIGN_EXTEND_VECTOR_INREG(SDNode *N) {
  SDValue N0 = N->getOperand(0);
  EVT VT = N->getValueType(0);

  if (N0.isUndef())
    return DAG.getUNDEF(VT);

  if (SDValue Res = tryToFoldExtendOfConstant(N, TLI, DAG, LegalTypes))
    return Res;

  if (SimplifyDemandedVectorElts(SDValue(N, 0)))
    return SDValue(N, 0);

  return SDValue();
}

SDValue DAGCombiner::visitZERO_EXTEND_VECTOR_INREG(SDNode *N) {
  SDValue N0 = N->getOperand(0);
  EVT VT = N->getValueType(0);

  if (N0.isUndef())
    return DAG.getUNDEF(VT);

  if (SDValue Res = tryToFoldExtendOfConstant(N, TLI, DAG, LegalTypes))
    return Res;

  if (SimplifyDemandedVectorElts(SDValue(N, 0)))
    return SDValue(N, 0);

  return SDValue();
}

SDValue DAGCombiner::visitTRUNCATE(SDNode *N) {
  SDValue N0 = N->getOperand(0);
  EVT VT = N->getValueType(0);
  bool isLE = DAG.getDataLayout().isLittleEndian();

  // noop truncate
  if (N0.getValueType() == N->getValueType(0))
    return N0;

  // fold (truncate (truncate x)) -> (truncate x)
  if (N0.getOpcode() == ISD::TRUNCATE)
    return DAG.getNode(ISD::TRUNCATE, SDLoc(N), VT, N0.getOperand(0));

  // fold (truncate c1) -> c1
  if (DAG.isConstantIntBuildVectorOrConstantInt(N0)) {
    SDValue C = DAG.getNode(ISD::TRUNCATE, SDLoc(N), VT, N0);
    if (C.getNode() != N)
      return C;
  }

  // fold (truncate (ext x)) -> (ext x) or (truncate x) or x
  if (N0.getOpcode() == ISD::ZERO_EXTEND ||
      N0.getOpcode() == ISD::SIGN_EXTEND ||
      N0.getOpcode() == ISD::ANY_EXTEND) {
    // if the source is smaller than the dest, we still need an extend.
    if (N0.getOperand(0).getValueType().bitsLT(VT))
      return DAG.getNode(N0.getOpcode(), SDLoc(N), VT, N0.getOperand(0));
    // if the source is larger than the dest, than we just need the truncate.
    if (N0.getOperand(0).getValueType().bitsGT(VT))
      return DAG.getNode(ISD::TRUNCATE, SDLoc(N), VT, N0.getOperand(0));
    // if the source and dest are the same type, we can drop both the extend
    // and the truncate.
    return N0.getOperand(0);
  }

  // If this is anyext(trunc), don't fold it, allow ourselves to be folded.
  if (N->hasOneUse() && (N->use_begin()->getOpcode() == ISD::ANY_EXTEND))
    return SDValue();

  // Fold extract-and-trunc into a narrow extract. For example:
  //   i64 x = EXTRACT_VECTOR_ELT(v2i64 val, i32 1)
  //   i32 y = TRUNCATE(i64 x)
  //        -- becomes --
  //   v16i8 b = BITCAST (v2i64 val)
  //   i8 x = EXTRACT_VECTOR_ELT(v16i8 b, i32 8)
  //
  // Note: We only run this optimization after type legalization (which often
  // creates this pattern) and before operation legalization after which
  // we need to be more careful about the vector instructions that we generate.
  if (N0.getOpcode() == ISD::EXTRACT_VECTOR_ELT &&
      LegalTypes && !LegalOperations && N0->hasOneUse() && VT != MVT::i1) {
    EVT VecTy = N0.getOperand(0).getValueType();
    EVT ExTy = N0.getValueType();
    EVT TrTy = N->getValueType(0);

    unsigned NumElem = VecTy.getVectorNumElements();
    unsigned SizeRatio = ExTy.getSizeInBits()/TrTy.getSizeInBits();

    EVT NVT = EVT::getVectorVT(*DAG.getContext(), TrTy, SizeRatio * NumElem);
    assert(NVT.getSizeInBits() == VecTy.getSizeInBits() && "Invalid Size");

    SDValue EltNo = N0->getOperand(1);
    if (isa<ConstantSDNode>(EltNo) && isTypeLegal(NVT)) {
      int Elt = cast<ConstantSDNode>(EltNo)->getZExtValue();
      EVT IndexTy = TLI.getVectorIdxTy(DAG.getDataLayout());
      int Index = isLE ? (Elt*SizeRatio) : (Elt*SizeRatio + (SizeRatio-1));

      SDLoc DL(N);
      return DAG.getNode(ISD::EXTRACT_VECTOR_ELT, DL, TrTy,
                         DAG.getBitcast(NVT, N0.getOperand(0)),
                         DAG.getConstant(Index, DL, IndexTy));
    }
  }

  // trunc (select c, a, b) -> select c, (trunc a), (trunc b)
  if (N0.getOpcode() == ISD::SELECT && N0.hasOneUse()) {
    EVT SrcVT = N0.getValueType();
    if ((!LegalOperations || TLI.isOperationLegal(ISD::SELECT, SrcVT)) &&
        TLI.isTruncateFree(SrcVT, VT)) {
      SDLoc SL(N0);
      SDValue Cond = N0.getOperand(0);
      SDValue TruncOp0 = DAG.getNode(ISD::TRUNCATE, SL, VT, N0.getOperand(1));
      SDValue TruncOp1 = DAG.getNode(ISD::TRUNCATE, SL, VT, N0.getOperand(2));
      return DAG.getNode(ISD::SELECT, SDLoc(N), VT, Cond, TruncOp0, TruncOp1);
    }
  }

  // trunc (shl x, K) -> shl (trunc x), K => K < VT.getScalarSizeInBits()
  if (N0.getOpcode() == ISD::SHL && N0.hasOneUse() &&
      (!LegalOperations || TLI.isOperationLegalOrCustom(ISD::SHL, VT)) &&
      TLI.isTypeDesirableForOp(ISD::SHL, VT)) {
    SDValue Amt = N0.getOperand(1);
    KnownBits Known = DAG.computeKnownBits(Amt);
    unsigned Size = VT.getScalarSizeInBits();
    if (Known.getBitWidth() - Known.countMinLeadingZeros() <= Log2_32(Size)) {
      SDLoc SL(N);
      EVT AmtVT = TLI.getShiftAmountTy(VT, DAG.getDataLayout());

      SDValue Trunc = DAG.getNode(ISD::TRUNCATE, SL, VT, N0.getOperand(0));
      if (AmtVT != Amt.getValueType()) {
        Amt = DAG.getZExtOrTrunc(Amt, SL, AmtVT);
        AddToWorklist(Amt.getNode());
      }
      return DAG.getNode(ISD::SHL, SL, VT, Trunc, Amt);
    }
  }

  // Fold a series of buildvector, bitcast, and truncate if possible.
  // For example fold
  //   (2xi32 trunc (bitcast ((4xi32)buildvector x, x, y, y) 2xi64)) to
  //   (2xi32 (buildvector x, y)).
  if (Level == AfterLegalizeVectorOps && VT.isVector() &&
      N0.getOpcode() == ISD::BITCAST && N0.hasOneUse() &&
      N0.getOperand(0).getOpcode() == ISD::BUILD_VECTOR &&
      N0.getOperand(0).hasOneUse()) {
    SDValue BuildVect = N0.getOperand(0);
    EVT BuildVectEltTy = BuildVect.getValueType().getVectorElementType();
    EVT TruncVecEltTy = VT.getVectorElementType();

    // Check that the element types match.
    if (BuildVectEltTy == TruncVecEltTy) {
      // Now we only need to compute the offset of the truncated elements.
      unsigned BuildVecNumElts =  BuildVect.getNumOperands();
      unsigned TruncVecNumElts = VT.getVectorNumElements();
      unsigned TruncEltOffset = BuildVecNumElts / TruncVecNumElts;

      assert((BuildVecNumElts % TruncVecNumElts) == 0 &&
             "Invalid number of elements");

      SmallVector<SDValue, 8> Opnds;
      for (unsigned i = 0, e = BuildVecNumElts; i != e; i += TruncEltOffset)
        Opnds.push_back(BuildVect.getOperand(i));

      return DAG.getBuildVector(VT, SDLoc(N), Opnds);
    }
  }

  // See if we can simplify the input to this truncate through knowledge that
  // only the low bits are being used.
  // For example "trunc (or (shl x, 8), y)" // -> trunc y
  // Currently we only perform this optimization on scalars because vectors
  // may have different active low bits.
  if (!VT.isVector()) {
    APInt Mask =
        APInt::getLowBitsSet(N0.getValueSizeInBits(), VT.getSizeInBits());
    if (SDValue Shorter = DAG.GetDemandedBits(N0, Mask))
      return DAG.getNode(ISD::TRUNCATE, SDLoc(N), VT, Shorter);
  }

  // fold (truncate (load x)) -> (smaller load x)
  // fold (truncate (srl (load x), c)) -> (smaller load (x+c/evtbits))
  if (!LegalTypes || TLI.isTypeDesirableForOp(N0.getOpcode(), VT)) {
    if (SDValue Reduced = ReduceLoadWidth(N))
      return Reduced;

    // Handle the case where the load remains an extending load even
    // after truncation.
    if (N0.hasOneUse() && ISD::isUNINDEXEDLoad(N0.getNode())) {
      LoadSDNode *LN0 = cast<LoadSDNode>(N0);
      if (!LN0->isVolatile() &&
          LN0->getMemoryVT().getStoreSizeInBits() < VT.getSizeInBits()) {
        SDValue NewLoad = DAG.getExtLoad(LN0->getExtensionType(), SDLoc(LN0),
                                         VT, LN0->getChain(), LN0->getBasePtr(),
                                         LN0->getMemoryVT(),
                                         LN0->getMemOperand());
        DAG.ReplaceAllUsesOfValueWith(N0.getValue(1), NewLoad.getValue(1));
        return NewLoad;
      }
    }
  }

  // fold (trunc (concat ... x ...)) -> (concat ..., (trunc x), ...)),
  // where ... are all 'undef'.
  if (N0.getOpcode() == ISD::CONCAT_VECTORS && !LegalTypes) {
    SmallVector<EVT, 8> VTs;
    SDValue V;
    unsigned Idx = 0;
    unsigned NumDefs = 0;

    for (unsigned i = 0, e = N0.getNumOperands(); i != e; ++i) {
      SDValue X = N0.getOperand(i);
      if (!X.isUndef()) {
        V = X;
        Idx = i;
        NumDefs++;
      }
      // Stop if more than one members are non-undef.
      if (NumDefs > 1)
        break;
      VTs.push_back(EVT::getVectorVT(*DAG.getContext(),
                                     VT.getVectorElementType(),
                                     X.getValueType().getVectorNumElements()));
    }

    if (NumDefs == 0)
      return DAG.getUNDEF(VT);

    if (NumDefs == 1) {
      assert(V.getNode() && "The single defined operand is empty!");
      SmallVector<SDValue, 8> Opnds;
      for (unsigned i = 0, e = VTs.size(); i != e; ++i) {
        if (i != Idx) {
          Opnds.push_back(DAG.getUNDEF(VTs[i]));
          continue;
        }
        SDValue NV = DAG.getNode(ISD::TRUNCATE, SDLoc(V), VTs[i], V);
        AddToWorklist(NV.getNode());
        Opnds.push_back(NV);
      }
      return DAG.getNode(ISD::CONCAT_VECTORS, SDLoc(N), VT, Opnds);
    }
  }

  // Fold truncate of a bitcast of a vector to an extract of the low vector
  // element.
  //
  // e.g. trunc (i64 (bitcast v2i32:x)) -> extract_vector_elt v2i32:x, idx
  if (N0.getOpcode() == ISD::BITCAST && !VT.isVector()) {
    SDValue VecSrc = N0.getOperand(0);
    EVT SrcVT = VecSrc.getValueType();
    if (SrcVT.isVector() && SrcVT.getScalarType() == VT &&
        (!LegalOperations ||
         TLI.isOperationLegal(ISD::EXTRACT_VECTOR_ELT, SrcVT))) {
      SDLoc SL(N);

      EVT IdxVT = TLI.getVectorIdxTy(DAG.getDataLayout());
      unsigned Idx = isLE ? 0 : SrcVT.getVectorNumElements() - 1;
      return DAG.getNode(ISD::EXTRACT_VECTOR_ELT, SL, VT,
                         VecSrc, DAG.getConstant(Idx, SL, IdxVT));
    }
  }

  // Simplify the operands using demanded-bits information.
  if (!VT.isVector() &&
      SimplifyDemandedBits(SDValue(N, 0)))
    return SDValue(N, 0);

  // (trunc adde(X, Y, Carry)) -> (adde trunc(X), trunc(Y), Carry)
  // (trunc addcarry(X, Y, Carry)) -> (addcarry trunc(X), trunc(Y), Carry)
  // When the adde's carry is not used.
  if ((N0.getOpcode() == ISD::ADDE || N0.getOpcode() == ISD::ADDCARRY) &&
      N0.hasOneUse() && !N0.getNode()->hasAnyUseOfValue(1) &&
      (!LegalOperations || TLI.isOperationLegal(N0.getOpcode(), VT))) {
    SDLoc SL(N);
    auto X = DAG.getNode(ISD::TRUNCATE, SL, VT, N0.getOperand(0));
    auto Y = DAG.getNode(ISD::TRUNCATE, SL, VT, N0.getOperand(1));
    auto VTs = DAG.getVTList(VT, N0->getValueType(1));
    return DAG.getNode(N0.getOpcode(), SL, VTs, X, Y, N0.getOperand(2));
  }

  // fold (truncate (extract_subvector(ext x))) ->
  //      (extract_subvector x)
  // TODO: This can be generalized to cover cases where the truncate and extract
  // do not fully cancel each other out.
  if (!LegalTypes && N0.getOpcode() == ISD::EXTRACT_SUBVECTOR) {
    SDValue N00 = N0.getOperand(0);
    if (N00.getOpcode() == ISD::SIGN_EXTEND ||
        N00.getOpcode() == ISD::ZERO_EXTEND ||
        N00.getOpcode() == ISD::ANY_EXTEND) {
      if (N00.getOperand(0)->getValueType(0).getVectorElementType() ==
          VT.getVectorElementType())
        return DAG.getNode(ISD::EXTRACT_SUBVECTOR, SDLoc(N0->getOperand(0)), VT,
                           N00.getOperand(0), N0.getOperand(1));
    }
  }

  if (SDValue NewVSel = matchVSelectOpSizesWithSetCC(N))
    return NewVSel;

  // Narrow a suitable binary operation with a non-opaque constant operand by
  // moving it ahead of the truncate. This is limited to pre-legalization
  // because targets may prefer a wider type during later combines and invert
  // this transform.
  switch (N0.getOpcode()) {
  case ISD::ADD:
  case ISD::SUB:
  case ISD::MUL:
  case ISD::AND:
  case ISD::OR:
  case ISD::XOR:
    if (!LegalOperations && N0.hasOneUse() &&
        (isConstantOrConstantVector(N0.getOperand(0), true) ||
         isConstantOrConstantVector(N0.getOperand(1), true))) {
      // TODO: We already restricted this to pre-legalization, but for vectors
      // we are extra cautious to not create an unsupported operation.
      // Target-specific changes are likely needed to avoid regressions here.
      if (VT.isScalarInteger() || TLI.isOperationLegal(N0.getOpcode(), VT)) {
        SDLoc DL(N);
        SDValue NarrowL = DAG.getNode(ISD::TRUNCATE, DL, VT, N0.getOperand(0));
        SDValue NarrowR = DAG.getNode(ISD::TRUNCATE, DL, VT, N0.getOperand(1));
        return DAG.getNode(N0.getOpcode(), DL, VT, NarrowL, NarrowR);
      }
    }
  }

  return SDValue();
}

static SDNode *getBuildPairElt(SDNode *N, unsigned i) {
  SDValue Elt = N->getOperand(i);
  if (Elt.getOpcode() != ISD::MERGE_VALUES)
    return Elt.getNode();
  return Elt.getOperand(Elt.getResNo()).getNode();
}

/// build_pair (load, load) -> load
/// if load locations are consecutive.
SDValue DAGCombiner::CombineConsecutiveLoads(SDNode *N, EVT VT) {
  assert(N->getOpcode() == ISD::BUILD_PAIR);

  LoadSDNode *LD1 = dyn_cast<LoadSDNode>(getBuildPairElt(N, 0));
  LoadSDNode *LD2 = dyn_cast<LoadSDNode>(getBuildPairElt(N, 1));

  // A BUILD_PAIR is always having the least significant part in elt 0 and the
  // most significant part in elt 1. So when combining into one large load, we
  // need to consider the endianness.
  if (DAG.getDataLayout().isBigEndian())
    std::swap(LD1, LD2);

  if (!LD1 || !LD2 || !ISD::isNON_EXTLoad(LD1) || !LD1->hasOneUse() ||
      LD1->getAddressSpace() != LD2->getAddressSpace())
    return SDValue();
  EVT LD1VT = LD1->getValueType(0);
  unsigned LD1Bytes = LD1VT.getStoreSize();
  if (ISD::isNON_EXTLoad(LD2) && LD2->hasOneUse() &&
      DAG.areNonVolatileConsecutiveLoads(LD2, LD1, LD1Bytes, 1)) {
    unsigned Align = LD1->getAlignment();
    unsigned NewAlign = DAG.getDataLayout().getABITypeAlignment(
        VT.getTypeForEVT(*DAG.getContext()));

    if (NewAlign <= Align &&
        (!LegalOperations || TLI.isOperationLegal(ISD::LOAD, VT)))
      return DAG.getLoad(VT, SDLoc(N), LD1->getChain(), LD1->getBasePtr(),
                         LD1->getPointerInfo(), Align);
  }

  return SDValue();
}

static unsigned getPPCf128HiElementSelector(const SelectionDAG &DAG) {
  // On little-endian machines, bitcasting from ppcf128 to i128 does swap the Hi
  // and Lo parts; on big-endian machines it doesn't.
  return DAG.getDataLayout().isBigEndian() ? 1 : 0;
}

static SDValue foldBitcastedFPLogic(SDNode *N, SelectionDAG &DAG,
                                    const TargetLowering &TLI) {
  // If this is not a bitcast to an FP type or if the target doesn't have
  // IEEE754-compliant FP logic, we're done.
  EVT VT = N->getValueType(0);
  if (!VT.isFloatingPoint() || !TLI.hasBitPreservingFPLogic(VT))
    return SDValue();

  // TODO: Handle cases where the integer constant is a different scalar
  // bitwidth to the FP.
  SDValue N0 = N->getOperand(0);
  EVT SourceVT = N0.getValueType();
  if (VT.getScalarSizeInBits() != SourceVT.getScalarSizeInBits())
    return SDValue();

  unsigned FPOpcode;
  APInt SignMask;
  switch (N0.getOpcode()) {
  case ISD::AND:
    FPOpcode = ISD::FABS;
    SignMask = ~APInt::getSignMask(SourceVT.getScalarSizeInBits());
    break;
  case ISD::XOR:
    FPOpcode = ISD::FNEG;
    SignMask = APInt::getSignMask(SourceVT.getScalarSizeInBits());
    break;
  case ISD::OR:
    FPOpcode = ISD::FABS;
    SignMask = APInt::getSignMask(SourceVT.getScalarSizeInBits());
    break;
  default:
    return SDValue();
  }

  // Fold (bitcast int (and (bitcast fp X to int), 0x7fff...) to fp) -> fabs X
  // Fold (bitcast int (xor (bitcast fp X to int), 0x8000...) to fp) -> fneg X
  // Fold (bitcast int (or (bitcast fp X to int), 0x8000...) to fp) ->
  //   fneg (fabs X)
  SDValue LogicOp0 = N0.getOperand(0);
  ConstantSDNode *LogicOp1 = isConstOrConstSplat(N0.getOperand(1), true);
  if (LogicOp1 && LogicOp1->getAPIntValue() == SignMask &&
      LogicOp0.getOpcode() == ISD::BITCAST &&
      LogicOp0.getOperand(0).getValueType() == VT) {
    SDValue FPOp = DAG.getNode(FPOpcode, SDLoc(N), VT, LogicOp0.getOperand(0));
    NumFPLogicOpsConv++;
    if (N0.getOpcode() == ISD::OR)
      return DAG.getNode(ISD::FNEG, SDLoc(N), VT, FPOp);
    return FPOp;
  }

  return SDValue();
}

SDValue DAGCombiner::visitBITCAST(SDNode *N) {
  SDValue N0 = N->getOperand(0);
  EVT VT = N->getValueType(0);

  if (N0.isUndef())
    return DAG.getUNDEF(VT);

  // If the input is a BUILD_VECTOR with all constant elements, fold this now.
  // Only do this before legalize types, since we might create an illegal
  // scalar type. Even if we knew we wouldn't create an illegal scalar type
  // we can only do this before legalize ops, since the target maybe
  // depending on the bitcast.
  // First check to see if this is all constant.
  if (!LegalTypes &&
      N0.getOpcode() == ISD::BUILD_VECTOR && N0.getNode()->hasOneUse() &&
      VT.isVector() && cast<BuildVectorSDNode>(N0)->isConstant())
    return ConstantFoldBITCASTofBUILD_VECTOR(N0.getNode(),
                                             VT.getVectorElementType());

  // If the input is a constant, let getNode fold it.
  if (isa<ConstantSDNode>(N0) || isa<ConstantFPSDNode>(N0)) {
    // If we can't allow illegal operations, we need to check that this is just
    // a fp -> int or int -> conversion and that the resulting operation will
    // be legal.
    if (!LegalOperations ||
        (isa<ConstantSDNode>(N0) && VT.isFloatingPoint() && !VT.isVector() &&
         TLI.isOperationLegal(ISD::ConstantFP, VT)) ||
        (isa<ConstantFPSDNode>(N0) && VT.isInteger() && !VT.isVector() &&
         TLI.isOperationLegal(ISD::Constant, VT))) {
      SDValue C = DAG.getBitcast(VT, N0);
      if (C.getNode() != N)
        return C;
    }
  }

  // (conv (conv x, t1), t2) -> (conv x, t2)
  if (N0.getOpcode() == ISD::BITCAST)
    return DAG.getBitcast(VT, N0.getOperand(0));

  // fold (conv (load x)) -> (load (conv*)x)
  // If the resultant load doesn't need a higher alignment than the original!
  if (ISD::isNormalLoad(N0.getNode()) && N0.hasOneUse() &&
      // Do not remove the cast if the types differ in endian layout.
      TLI.hasBigEndianPartOrdering(N0.getValueType(), DAG.getDataLayout()) ==
          TLI.hasBigEndianPartOrdering(VT, DAG.getDataLayout()) &&
      // If the load is volatile, we only want to change the load type if the
      // resulting load is legal. Otherwise we might increase the number of
      // memory accesses. We don't care if the original type was legal or not
      // as we assume software couldn't rely on the number of accesses of an
      // illegal type.
      ((!LegalOperations && !cast<LoadSDNode>(N0)->isVolatile()) ||
       TLI.isOperationLegal(ISD::LOAD, VT)) &&
      TLI.isLoadBitCastBeneficial(N0.getValueType(), VT)) {
    LoadSDNode *LN0 = cast<LoadSDNode>(N0);
    unsigned OrigAlign = LN0->getAlignment();

    bool Fast = false;
    if (TLI.allowsMemoryAccess(*DAG.getContext(), DAG.getDataLayout(), VT,
                               LN0->getAddressSpace(), OrigAlign, &Fast) &&
        Fast) {
      SDValue Load =
          DAG.getLoad(VT, SDLoc(N), LN0->getChain(), LN0->getBasePtr(),
                      LN0->getPointerInfo(), OrigAlign,
                      LN0->getMemOperand()->getFlags(), LN0->getAAInfo());
      DAG.ReplaceAllUsesOfValueWith(N0.getValue(1), Load.getValue(1));
      return Load;
    }
  }

  if (SDValue V = foldBitcastedFPLogic(N, DAG, TLI))
    return V;

  // fold (bitconvert (fneg x)) -> (xor (bitconvert x), signbit)
  // fold (bitconvert (fabs x)) -> (and (bitconvert x), (not signbit))
  //
  // For ppc_fp128:
  // fold (bitcast (fneg x)) ->
  //     flipbit = signbit
  //     (xor (bitcast x) (build_pair flipbit, flipbit))
  //
  // fold (bitcast (fabs x)) ->
  //     flipbit = (and (extract_element (bitcast x), 0), signbit)
  //     (xor (bitcast x) (build_pair flipbit, flipbit))
  // This often reduces constant pool loads.
  if (((N0.getOpcode() == ISD::FNEG && !TLI.isFNegFree(N0.getValueType())) ||
       (N0.getOpcode() == ISD::FABS && !TLI.isFAbsFree(N0.getValueType()))) &&
      N0.getNode()->hasOneUse() && VT.isInteger() &&
      !VT.isVector() && !N0.getValueType().isVector()) {
    SDValue NewConv = DAG.getBitcast(VT, N0.getOperand(0));
    AddToWorklist(NewConv.getNode());

    SDLoc DL(N);
    if (N0.getValueType() == MVT::ppcf128 && !LegalTypes) {
      assert(VT.getSizeInBits() == 128);
      SDValue SignBit = DAG.getConstant(
          APInt::getSignMask(VT.getSizeInBits() / 2), SDLoc(N0), MVT::i64);
      SDValue FlipBit;
      if (N0.getOpcode() == ISD::FNEG) {
        FlipBit = SignBit;
        AddToWorklist(FlipBit.getNode());
      } else {
        assert(N0.getOpcode() == ISD::FABS);
        SDValue Hi =
            DAG.getNode(ISD::EXTRACT_ELEMENT, SDLoc(NewConv), MVT::i64, NewConv,
                        DAG.getIntPtrConstant(getPPCf128HiElementSelector(DAG),
                                              SDLoc(NewConv)));
        AddToWorklist(Hi.getNode());
        FlipBit = DAG.getNode(ISD::AND, SDLoc(N0), MVT::i64, Hi, SignBit);
        AddToWorklist(FlipBit.getNode());
      }
      SDValue FlipBits =
          DAG.getNode(ISD::BUILD_PAIR, SDLoc(N0), VT, FlipBit, FlipBit);
      AddToWorklist(FlipBits.getNode());
      return DAG.getNode(ISD::XOR, DL, VT, NewConv, FlipBits);
    }
    APInt SignBit = APInt::getSignMask(VT.getSizeInBits());
    if (N0.getOpcode() == ISD::FNEG)
      return DAG.getNode(ISD::XOR, DL, VT,
                         NewConv, DAG.getConstant(SignBit, DL, VT));
    assert(N0.getOpcode() == ISD::FABS);
    return DAG.getNode(ISD::AND, DL, VT,
                       NewConv, DAG.getConstant(~SignBit, DL, VT));
  }

  // fold (bitconvert (fcopysign cst, x)) ->
  //         (or (and (bitconvert x), sign), (and cst, (not sign)))
  // Note that we don't handle (copysign x, cst) because this can always be
  // folded to an fneg or fabs.
  //
  // For ppc_fp128:
  // fold (bitcast (fcopysign cst, x)) ->
  //     flipbit = (and (extract_element
  //                     (xor (bitcast cst), (bitcast x)), 0),
  //                    signbit)
  //     (xor (bitcast cst) (build_pair flipbit, flipbit))
  if (N0.getOpcode() == ISD::FCOPYSIGN && N0.getNode()->hasOneUse() &&
      isa<ConstantFPSDNode>(N0.getOperand(0)) &&
      VT.isInteger() && !VT.isVector()) {
    unsigned OrigXWidth = N0.getOperand(1).getValueSizeInBits();
    EVT IntXVT = EVT::getIntegerVT(*DAG.getContext(), OrigXWidth);
    if (isTypeLegal(IntXVT)) {
      SDValue X = DAG.getBitcast(IntXVT, N0.getOperand(1));
      AddToWorklist(X.getNode());

      // If X has a different width than the result/lhs, sext it or truncate it.
      unsigned VTWidth = VT.getSizeInBits();
      if (OrigXWidth < VTWidth) {
        X = DAG.getNode(ISD::SIGN_EXTEND, SDLoc(N), VT, X);
        AddToWorklist(X.getNode());
      } else if (OrigXWidth > VTWidth) {
        // To get the sign bit in the right place, we have to shift it right
        // before truncating.
        SDLoc DL(X);
        X = DAG.getNode(ISD::SRL, DL,
                        X.getValueType(), X,
                        DAG.getConstant(OrigXWidth-VTWidth, DL,
                                        X.getValueType()));
        AddToWorklist(X.getNode());
        X = DAG.getNode(ISD::TRUNCATE, SDLoc(X), VT, X);
        AddToWorklist(X.getNode());
      }

      if (N0.getValueType() == MVT::ppcf128 && !LegalTypes) {
        APInt SignBit = APInt::getSignMask(VT.getSizeInBits() / 2);
        SDValue Cst = DAG.getBitcast(VT, N0.getOperand(0));
        AddToWorklist(Cst.getNode());
        SDValue X = DAG.getBitcast(VT, N0.getOperand(1));
        AddToWorklist(X.getNode());
        SDValue XorResult = DAG.getNode(ISD::XOR, SDLoc(N0), VT, Cst, X);
        AddToWorklist(XorResult.getNode());
        SDValue XorResult64 = DAG.getNode(
            ISD::EXTRACT_ELEMENT, SDLoc(XorResult), MVT::i64, XorResult,
            DAG.getIntPtrConstant(getPPCf128HiElementSelector(DAG),
                                  SDLoc(XorResult)));
        AddToWorklist(XorResult64.getNode());
        SDValue FlipBit =
            DAG.getNode(ISD::AND, SDLoc(XorResult64), MVT::i64, XorResult64,
                        DAG.getConstant(SignBit, SDLoc(XorResult64), MVT::i64));
        AddToWorklist(FlipBit.getNode());
        SDValue FlipBits =
            DAG.getNode(ISD::BUILD_PAIR, SDLoc(N0), VT, FlipBit, FlipBit);
        AddToWorklist(FlipBits.getNode());
        return DAG.getNode(ISD::XOR, SDLoc(N), VT, Cst, FlipBits);
      }
      APInt SignBit = APInt::getSignMask(VT.getSizeInBits());
      X = DAG.getNode(ISD::AND, SDLoc(X), VT,
                      X, DAG.getConstant(SignBit, SDLoc(X), VT));
      AddToWorklist(X.getNode());

      SDValue Cst = DAG.getBitcast(VT, N0.getOperand(0));
      Cst = DAG.getNode(ISD::AND, SDLoc(Cst), VT,
                        Cst, DAG.getConstant(~SignBit, SDLoc(Cst), VT));
      AddToWorklist(Cst.getNode());

      return DAG.getNode(ISD::OR, SDLoc(N), VT, X, Cst);
    }
  }

  // bitconvert(build_pair(ld, ld)) -> ld iff load locations are consecutive.
  if (N0.getOpcode() == ISD::BUILD_PAIR)
    if (SDValue CombineLD = CombineConsecutiveLoads(N0.getNode(), VT))
      return CombineLD;

  // Remove double bitcasts from shuffles - this is often a legacy of
  // XformToShuffleWithZero being used to combine bitmaskings (of
  // float vectors bitcast to integer vectors) into shuffles.
  // bitcast(shuffle(bitcast(s0),bitcast(s1))) -> shuffle(s0,s1)
  if (Level < AfterLegalizeDAG && TLI.isTypeLegal(VT) && VT.isVector() &&
      N0->getOpcode() == ISD::VECTOR_SHUFFLE && N0.hasOneUse() &&
      VT.getVectorNumElements() >= N0.getValueType().getVectorNumElements() &&
      !(VT.getVectorNumElements() % N0.getValueType().getVectorNumElements())) {
    ShuffleVectorSDNode *SVN = cast<ShuffleVectorSDNode>(N0);

    // If operands are a bitcast, peek through if it casts the original VT.
    // If operands are a constant, just bitcast back to original VT.
    auto PeekThroughBitcast = [&](SDValue Op) {
      if (Op.getOpcode() == ISD::BITCAST &&
          Op.getOperand(0).getValueType() == VT)
        return SDValue(Op.getOperand(0));
      if (Op.isUndef() || ISD::isBuildVectorOfConstantSDNodes(Op.getNode()) ||
          ISD::isBuildVectorOfConstantFPSDNodes(Op.getNode()))
        return DAG.getBitcast(VT, Op);
      return SDValue();
    };

    // FIXME: If either input vector is bitcast, try to convert the shuffle to
    // the result type of this bitcast. This would eliminate at least one
    // bitcast. See the transform in InstCombine.
    SDValue SV0 = PeekThroughBitcast(N0->getOperand(0));
    SDValue SV1 = PeekThroughBitcast(N0->getOperand(1));
    if (!(SV0 && SV1))
      return SDValue();

    int MaskScale =
        VT.getVectorNumElements() / N0.getValueType().getVectorNumElements();
    SmallVector<int, 8> NewMask;
    for (int M : SVN->getMask())
      for (int i = 0; i != MaskScale; ++i)
        NewMask.push_back(M < 0 ? -1 : M * MaskScale + i);

    bool LegalMask = TLI.isShuffleMaskLegal(NewMask, VT);
    if (!LegalMask) {
      std::swap(SV0, SV1);
      ShuffleVectorSDNode::commuteMask(NewMask);
      LegalMask = TLI.isShuffleMaskLegal(NewMask, VT);
    }

    if (LegalMask)
      return DAG.getVectorShuffle(VT, SDLoc(N), SV0, SV1, NewMask);
  }

  return SDValue();
}

SDValue DAGCombiner::visitBUILD_PAIR(SDNode *N) {
  EVT VT = N->getValueType(0);
  return CombineConsecutiveLoads(N, VT);
}

/// We know that BV is a build_vector node with Constant, ConstantFP or Undef
/// operands. DstEltVT indicates the destination element value type.
SDValue DAGCombiner::
ConstantFoldBITCASTofBUILD_VECTOR(SDNode *BV, EVT DstEltVT) {
  EVT SrcEltVT = BV->getValueType(0).getVectorElementType();

  // If this is already the right type, we're done.
  if (SrcEltVT == DstEltVT) return SDValue(BV, 0);

  unsigned SrcBitSize = SrcEltVT.getSizeInBits();
  unsigned DstBitSize = DstEltVT.getSizeInBits();

  // If this is a conversion of N elements of one type to N elements of another
  // type, convert each element.  This handles FP<->INT cases.
  if (SrcBitSize == DstBitSize) {
    SmallVector<SDValue, 8> Ops;
    for (SDValue Op : BV->op_values()) {
      // If the vector element type is not legal, the BUILD_VECTOR operands
      // are promoted and implicitly truncated.  Make that explicit here.
      if (Op.getValueType() != SrcEltVT)
        Op = DAG.getNode(ISD::TRUNCATE, SDLoc(BV), SrcEltVT, Op);
      Ops.push_back(DAG.getBitcast(DstEltVT, Op));
      AddToWorklist(Ops.back().getNode());
    }
    EVT VT = EVT::getVectorVT(*DAG.getContext(), DstEltVT,
                              BV->getValueType(0).getVectorNumElements());
    return DAG.getBuildVector(VT, SDLoc(BV), Ops);
  }

  // Otherwise, we're growing or shrinking the elements.  To avoid having to
  // handle annoying details of growing/shrinking FP values, we convert them to
  // int first.
  if (SrcEltVT.isFloatingPoint()) {
    // Convert the input float vector to a int vector where the elements are the
    // same sizes.
    EVT IntVT = EVT::getIntegerVT(*DAG.getContext(), SrcEltVT.getSizeInBits());
    BV = ConstantFoldBITCASTofBUILD_VECTOR(BV, IntVT).getNode();
    SrcEltVT = IntVT;
  }

  // Now we know the input is an integer vector.  If the output is a FP type,
  // convert to integer first, then to FP of the right size.
  if (DstEltVT.isFloatingPoint()) {
    EVT TmpVT = EVT::getIntegerVT(*DAG.getContext(), DstEltVT.getSizeInBits());
    SDNode *Tmp = ConstantFoldBITCASTofBUILD_VECTOR(BV, TmpVT).getNode();

    // Next, convert to FP elements of the same size.
    return ConstantFoldBITCASTofBUILD_VECTOR(Tmp, DstEltVT);
  }

  SDLoc DL(BV);

  // Okay, we know the src/dst types are both integers of differing types.
  // Handling growing first.
  assert(SrcEltVT.isInteger() && DstEltVT.isInteger());
  if (SrcBitSize < DstBitSize) {
    unsigned NumInputsPerOutput = DstBitSize/SrcBitSize;

    SmallVector<SDValue, 8> Ops;
    for (unsigned i = 0, e = BV->getNumOperands(); i != e;
         i += NumInputsPerOutput) {
      bool isLE = DAG.getDataLayout().isLittleEndian();
      APInt NewBits = APInt(DstBitSize, 0);
      bool EltIsUndef = true;
      for (unsigned j = 0; j != NumInputsPerOutput; ++j) {
        // Shift the previously computed bits over.
        NewBits <<= SrcBitSize;
        SDValue Op = BV->getOperand(i+ (isLE ? (NumInputsPerOutput-j-1) : j));
        if (Op.isUndef()) continue;
        EltIsUndef = false;

        NewBits |= cast<ConstantSDNode>(Op)->getAPIntValue().
                   zextOrTrunc(SrcBitSize).zext(DstBitSize);
      }

      if (EltIsUndef)
        Ops.push_back(DAG.getUNDEF(DstEltVT));
      else
        Ops.push_back(DAG.getConstant(NewBits, DL, DstEltVT));
    }

    EVT VT = EVT::getVectorVT(*DAG.getContext(), DstEltVT, Ops.size());
    return DAG.getBuildVector(VT, DL, Ops);
  }

  // Finally, this must be the case where we are shrinking elements: each input
  // turns into multiple outputs.
  unsigned NumOutputsPerInput = SrcBitSize/DstBitSize;
  EVT VT = EVT::getVectorVT(*DAG.getContext(), DstEltVT,
                            NumOutputsPerInput*BV->getNumOperands());
  SmallVector<SDValue, 8> Ops;

  for (const SDValue &Op : BV->op_values()) {
    if (Op.isUndef()) {
      Ops.append(NumOutputsPerInput, DAG.getUNDEF(DstEltVT));
      continue;
    }

    APInt OpVal = cast<ConstantSDNode>(Op)->
                  getAPIntValue().zextOrTrunc(SrcBitSize);

    for (unsigned j = 0; j != NumOutputsPerInput; ++j) {
      APInt ThisVal = OpVal.trunc(DstBitSize);
      Ops.push_back(DAG.getConstant(ThisVal, DL, DstEltVT));
      OpVal.lshrInPlace(DstBitSize);
    }

    // For big endian targets, swap the order of the pieces of each element.
    if (DAG.getDataLayout().isBigEndian())
      std::reverse(Ops.end()-NumOutputsPerInput, Ops.end());
  }

  return DAG.getBuildVector(VT, DL, Ops);
}

static bool isContractable(SDNode *N) {
  SDNodeFlags F = N->getFlags();
  return F.hasAllowContract() || F.hasAllowReassociation();
}

/// Try to perform FMA combining on a given FADD node.
SDValue DAGCombiner::visitFADDForFMACombine(SDNode *N) {
  SDValue N0 = N->getOperand(0);
  SDValue N1 = N->getOperand(1);
  EVT VT = N->getValueType(0);
  SDLoc SL(N);

  const TargetOptions &Options = DAG.getTarget().Options;

  // Floating-point multiply-add with intermediate rounding.
  bool HasFMAD = (LegalOperations && TLI.isOperationLegal(ISD::FMAD, VT));

  // Floating-point multiply-add without intermediate rounding.
  bool HasFMA =
      TLI.isFMAFasterThanFMulAndFAdd(VT) &&
      (!LegalOperations || TLI.isOperationLegalOrCustom(ISD::FMA, VT));

  // No valid opcode, do not combine.
  if (!HasFMAD && !HasFMA)
    return SDValue();

  SDNodeFlags Flags = N->getFlags();
  bool CanFuse = Options.UnsafeFPMath || isContractable(N);
  bool AllowFusionGlobally = (Options.AllowFPOpFusion == FPOpFusion::Fast ||
                              CanFuse || HasFMAD);
  // If the addition is not contractable, do not combine.
  if (!AllowFusionGlobally && !isContractable(N))
    return SDValue();

  const SelectionDAGTargetInfo *STI = DAG.getSubtarget().getSelectionDAGInfo();
  if (STI && STI->generateFMAsInMachineCombiner(OptLevel))
    return SDValue();

  // Always prefer FMAD to FMA for precision.
  unsigned PreferredFusedOpcode = HasFMAD ? ISD::FMAD : ISD::FMA;
  bool Aggressive = TLI.enableAggressiveFMAFusion(VT);

  // Is the node an FMUL and contractable either due to global flags or
  // SDNodeFlags.
  auto isContractableFMUL = [AllowFusionGlobally](SDValue N) {
    if (N.getOpcode() != ISD::FMUL)
      return false;
    return AllowFusionGlobally || isContractable(N.getNode());
  };
  // If we have two choices trying to fold (fadd (fmul u, v), (fmul x, y)),
  // prefer to fold the multiply with fewer uses.
  if (Aggressive && isContractableFMUL(N0) && isContractableFMUL(N1)) {
    if (N0.getNode()->use_size() > N1.getNode()->use_size())
      std::swap(N0, N1);
  }

  // fold (fadd (fmul x, y), z) -> (fma x, y, z)
  if (isContractableFMUL(N0) && (Aggressive || N0->hasOneUse())) {
    return DAG.getNode(PreferredFusedOpcode, SL, VT,
                       N0.getOperand(0), N0.getOperand(1), N1, Flags);
  }

  // fold (fadd x, (fmul y, z)) -> (fma y, z, x)
  // Note: Commutes FADD operands.
  if (isContractableFMUL(N1) && (Aggressive || N1->hasOneUse())) {
    return DAG.getNode(PreferredFusedOpcode, SL, VT,
                       N1.getOperand(0), N1.getOperand(1), N0, Flags);
  }

  // Look through FP_EXTEND nodes to do more combining.

  // fold (fadd (fpext (fmul x, y)), z) -> (fma (fpext x), (fpext y), z)
  if (N0.getOpcode() == ISD::FP_EXTEND) {
    SDValue N00 = N0.getOperand(0);
    if (isContractableFMUL(N00) &&
        TLI.isFPExtFoldable(PreferredFusedOpcode, VT, N00.getValueType())) {
      return DAG.getNode(PreferredFusedOpcode, SL, VT,
                         DAG.getNode(ISD::FP_EXTEND, SL, VT,
                                     N00.getOperand(0)),
                         DAG.getNode(ISD::FP_EXTEND, SL, VT,
                                     N00.getOperand(1)), N1, Flags);
    }
  }

  // fold (fadd x, (fpext (fmul y, z))) -> (fma (fpext y), (fpext z), x)
  // Note: Commutes FADD operands.
  if (N1.getOpcode() == ISD::FP_EXTEND) {
    SDValue N10 = N1.getOperand(0);
    if (isContractableFMUL(N10) &&
        TLI.isFPExtFoldable(PreferredFusedOpcode, VT, N10.getValueType())) {
      return DAG.getNode(PreferredFusedOpcode, SL, VT,
                         DAG.getNode(ISD::FP_EXTEND, SL, VT,
                                     N10.getOperand(0)),
                         DAG.getNode(ISD::FP_EXTEND, SL, VT,
                                     N10.getOperand(1)), N0, Flags);
    }
  }

  // More folding opportunities when target permits.
  if (Aggressive) {
    // fold (fadd (fma x, y, (fmul u, v)), z) -> (fma x, y (fma u, v, z))
    if (CanFuse &&
        N0.getOpcode() == PreferredFusedOpcode &&
        N0.getOperand(2).getOpcode() == ISD::FMUL &&
        N0->hasOneUse() && N0.getOperand(2)->hasOneUse()) {
      return DAG.getNode(PreferredFusedOpcode, SL, VT,
                         N0.getOperand(0), N0.getOperand(1),
                         DAG.getNode(PreferredFusedOpcode, SL, VT,
                                     N0.getOperand(2).getOperand(0),
                                     N0.getOperand(2).getOperand(1),
                                     N1, Flags), Flags);
    }

    // fold (fadd x, (fma y, z, (fmul u, v)) -> (fma y, z (fma u, v, x))
    if (CanFuse &&
        N1->getOpcode() == PreferredFusedOpcode &&
        N1.getOperand(2).getOpcode() == ISD::FMUL &&
        N1->hasOneUse() && N1.getOperand(2)->hasOneUse()) {
      return DAG.getNode(PreferredFusedOpcode, SL, VT,
                         N1.getOperand(0), N1.getOperand(1),
                         DAG.getNode(PreferredFusedOpcode, SL, VT,
                                     N1.getOperand(2).getOperand(0),
                                     N1.getOperand(2).getOperand(1),
                                     N0, Flags), Flags);
    }


    // fold (fadd (fma x, y, (fpext (fmul u, v))), z)
    //   -> (fma x, y, (fma (fpext u), (fpext v), z))
    auto FoldFAddFMAFPExtFMul = [&] (
      SDValue X, SDValue Y, SDValue U, SDValue V, SDValue Z,
      SDNodeFlags Flags) {
      return DAG.getNode(PreferredFusedOpcode, SL, VT, X, Y,
                         DAG.getNode(PreferredFusedOpcode, SL, VT,
                                     DAG.getNode(ISD::FP_EXTEND, SL, VT, U),
                                     DAG.getNode(ISD::FP_EXTEND, SL, VT, V),
                                     Z, Flags), Flags);
    };
    if (N0.getOpcode() == PreferredFusedOpcode) {
      SDValue N02 = N0.getOperand(2);
      if (N02.getOpcode() == ISD::FP_EXTEND) {
        SDValue N020 = N02.getOperand(0);
        if (isContractableFMUL(N020) &&
            TLI.isFPExtFoldable(PreferredFusedOpcode, VT, N020.getValueType())) {
          return FoldFAddFMAFPExtFMul(N0.getOperand(0), N0.getOperand(1),
                                      N020.getOperand(0), N020.getOperand(1),
                                      N1, Flags);
        }
      }
    }

    // fold (fadd (fpext (fma x, y, (fmul u, v))), z)
    //   -> (fma (fpext x), (fpext y), (fma (fpext u), (fpext v), z))
    // FIXME: This turns two single-precision and one double-precision
    // operation into two double-precision operations, which might not be
    // interesting for all targets, especially GPUs.
    auto FoldFAddFPExtFMAFMul = [&] (
      SDValue X, SDValue Y, SDValue U, SDValue V, SDValue Z,
      SDNodeFlags Flags) {
      return DAG.getNode(PreferredFusedOpcode, SL, VT,
                         DAG.getNode(ISD::FP_EXTEND, SL, VT, X),
                         DAG.getNode(ISD::FP_EXTEND, SL, VT, Y),
                         DAG.getNode(PreferredFusedOpcode, SL, VT,
                                     DAG.getNode(ISD::FP_EXTEND, SL, VT, U),
                                     DAG.getNode(ISD::FP_EXTEND, SL, VT, V),
                                     Z, Flags), Flags);
    };
    if (N0.getOpcode() == ISD::FP_EXTEND) {
      SDValue N00 = N0.getOperand(0);
      if (N00.getOpcode() == PreferredFusedOpcode) {
        SDValue N002 = N00.getOperand(2);
        if (isContractableFMUL(N002) &&
            TLI.isFPExtFoldable(PreferredFusedOpcode, VT, N00.getValueType())) {
          return FoldFAddFPExtFMAFMul(N00.getOperand(0), N00.getOperand(1),
                                      N002.getOperand(0), N002.getOperand(1),
                                      N1, Flags);
        }
      }
    }

    // fold (fadd x, (fma y, z, (fpext (fmul u, v)))
    //   -> (fma y, z, (fma (fpext u), (fpext v), x))
    if (N1.getOpcode() == PreferredFusedOpcode) {
      SDValue N12 = N1.getOperand(2);
      if (N12.getOpcode() == ISD::FP_EXTEND) {
        SDValue N120 = N12.getOperand(0);
        if (isContractableFMUL(N120) &&
            TLI.isFPExtFoldable(PreferredFusedOpcode, VT, N120.getValueType())) {
          return FoldFAddFMAFPExtFMul(N1.getOperand(0), N1.getOperand(1),
                                      N120.getOperand(0), N120.getOperand(1),
                                      N0, Flags);
        }
      }
    }

    // fold (fadd x, (fpext (fma y, z, (fmul u, v)))
    //   -> (fma (fpext y), (fpext z), (fma (fpext u), (fpext v), x))
    // FIXME: This turns two single-precision and one double-precision
    // operation into two double-precision operations, which might not be
    // interesting for all targets, especially GPUs.
    if (N1.getOpcode() == ISD::FP_EXTEND) {
      SDValue N10 = N1.getOperand(0);
      if (N10.getOpcode() == PreferredFusedOpcode) {
        SDValue N102 = N10.getOperand(2);
        if (isContractableFMUL(N102) &&
            TLI.isFPExtFoldable(PreferredFusedOpcode, VT, N10.getValueType())) {
          return FoldFAddFPExtFMAFMul(N10.getOperand(0), N10.getOperand(1),
                                      N102.getOperand(0), N102.getOperand(1),
                                      N0, Flags);
        }
      }
    }
  }

  return SDValue();
}

/// Try to perform FMA combining on a given FSUB node.
SDValue DAGCombiner::visitFSUBForFMACombine(SDNode *N) {
  SDValue N0 = N->getOperand(0);
  SDValue N1 = N->getOperand(1);
  EVT VT = N->getValueType(0);
  SDLoc SL(N);

  const TargetOptions &Options = DAG.getTarget().Options;
  // Floating-point multiply-add with intermediate rounding.
  bool HasFMAD = (LegalOperations && TLI.isOperationLegal(ISD::FMAD, VT));

  // Floating-point multiply-add without intermediate rounding.
  bool HasFMA =
      TLI.isFMAFasterThanFMulAndFAdd(VT) &&
      (!LegalOperations || TLI.isOperationLegalOrCustom(ISD::FMA, VT));

  // No valid opcode, do not combine.
  if (!HasFMAD && !HasFMA)
    return SDValue();

  const SDNodeFlags Flags = N->getFlags();
  bool CanFuse = Options.UnsafeFPMath || isContractable(N);
  bool AllowFusionGlobally = (Options.AllowFPOpFusion == FPOpFusion::Fast ||
                              CanFuse || HasFMAD);

  // If the subtraction is not contractable, do not combine.
  if (!AllowFusionGlobally && !isContractable(N))
    return SDValue();

  const SelectionDAGTargetInfo *STI = DAG.getSubtarget().getSelectionDAGInfo();
  if (STI && STI->generateFMAsInMachineCombiner(OptLevel))
    return SDValue();

  // Always prefer FMAD to FMA for precision.
  unsigned PreferredFusedOpcode = HasFMAD ? ISD::FMAD : ISD::FMA;
  bool Aggressive = TLI.enableAggressiveFMAFusion(VT);

  // Is the node an FMUL and contractable either due to global flags or
  // SDNodeFlags.
  auto isContractableFMUL = [AllowFusionGlobally](SDValue N) {
    if (N.getOpcode() != ISD::FMUL)
      return false;
    return AllowFusionGlobally || isContractable(N.getNode());
  };

  // fold (fsub (fmul x, y), z) -> (fma x, y, (fneg z))
  if (isContractableFMUL(N0) && (Aggressive || N0->hasOneUse())) {
    return DAG.getNode(PreferredFusedOpcode, SL, VT,
                       N0.getOperand(0), N0.getOperand(1),
                       DAG.getNode(ISD::FNEG, SL, VT, N1), Flags);
  }

  // fold (fsub x, (fmul y, z)) -> (fma (fneg y), z, x)
  // Note: Commutes FSUB operands.
  if (isContractableFMUL(N1) && (Aggressive || N1->hasOneUse())) {
    return DAG.getNode(PreferredFusedOpcode, SL, VT,
                       DAG.getNode(ISD::FNEG, SL, VT,
                                   N1.getOperand(0)),
                       N1.getOperand(1), N0, Flags);
  }

  // fold (fsub (fneg (fmul, x, y)), z) -> (fma (fneg x), y, (fneg z))
  if (N0.getOpcode() == ISD::FNEG && isContractableFMUL(N0.getOperand(0)) &&
      (Aggressive || (N0->hasOneUse() && N0.getOperand(0).hasOneUse()))) {
    SDValue N00 = N0.getOperand(0).getOperand(0);
    SDValue N01 = N0.getOperand(0).getOperand(1);
    return DAG.getNode(PreferredFusedOpcode, SL, VT,
                       DAG.getNode(ISD::FNEG, SL, VT, N00), N01,
                       DAG.getNode(ISD::FNEG, SL, VT, N1), Flags);
  }

  // Look through FP_EXTEND nodes to do more combining.

  // fold (fsub (fpext (fmul x, y)), z)
  //   -> (fma (fpext x), (fpext y), (fneg z))
  if (N0.getOpcode() == ISD::FP_EXTEND) {
    SDValue N00 = N0.getOperand(0);
    if (isContractableFMUL(N00) &&
        TLI.isFPExtFoldable(PreferredFusedOpcode, VT, N00.getValueType())) {
      return DAG.getNode(PreferredFusedOpcode, SL, VT,
                         DAG.getNode(ISD::FP_EXTEND, SL, VT,
                                     N00.getOperand(0)),
                         DAG.getNode(ISD::FP_EXTEND, SL, VT,
                                     N00.getOperand(1)),
                         DAG.getNode(ISD::FNEG, SL, VT, N1), Flags);
    }
  }

  // fold (fsub x, (fpext (fmul y, z)))
  //   -> (fma (fneg (fpext y)), (fpext z), x)
  // Note: Commutes FSUB operands.
  if (N1.getOpcode() == ISD::FP_EXTEND) {
    SDValue N10 = N1.getOperand(0);
    if (isContractableFMUL(N10) &&
        TLI.isFPExtFoldable(PreferredFusedOpcode, VT, N10.getValueType())) {
      return DAG.getNode(PreferredFusedOpcode, SL, VT,
                         DAG.getNode(ISD::FNEG, SL, VT,
                                     DAG.getNode(ISD::FP_EXTEND, SL, VT,
                                                 N10.getOperand(0))),
                         DAG.getNode(ISD::FP_EXTEND, SL, VT,
                                     N10.getOperand(1)),
                         N0, Flags);
    }
  }

  // fold (fsub (fpext (fneg (fmul, x, y))), z)
  //   -> (fneg (fma (fpext x), (fpext y), z))
  // Note: This could be removed with appropriate canonicalization of the
  // input expression into (fneg (fadd (fpext (fmul, x, y)), z). However, the
  // orthogonal flags -fp-contract=fast and -enable-unsafe-fp-math prevent
  // from implementing the canonicalization in visitFSUB.
  if (N0.getOpcode() == ISD::FP_EXTEND) {
    SDValue N00 = N0.getOperand(0);
    if (N00.getOpcode() == ISD::FNEG) {
      SDValue N000 = N00.getOperand(0);
      if (isContractableFMUL(N000) &&
          TLI.isFPExtFoldable(PreferredFusedOpcode, VT, N00.getValueType())) {
        return DAG.getNode(ISD::FNEG, SL, VT,
                           DAG.getNode(PreferredFusedOpcode, SL, VT,
                                       DAG.getNode(ISD::FP_EXTEND, SL, VT,
                                                   N000.getOperand(0)),
                                       DAG.getNode(ISD::FP_EXTEND, SL, VT,
                                                   N000.getOperand(1)),
                                       N1, Flags));
      }
    }
  }

  // fold (fsub (fneg (fpext (fmul, x, y))), z)
  //   -> (fneg (fma (fpext x)), (fpext y), z)
  // Note: This could be removed with appropriate canonicalization of the
  // input expression into (fneg (fadd (fpext (fmul, x, y)), z). However, the
  // orthogonal flags -fp-contract=fast and -enable-unsafe-fp-math prevent
  // from implementing the canonicalization in visitFSUB.
  if (N0.getOpcode() == ISD::FNEG) {
    SDValue N00 = N0.getOperand(0);
    if (N00.getOpcode() == ISD::FP_EXTEND) {
      SDValue N000 = N00.getOperand(0);
      if (isContractableFMUL(N000) &&
          TLI.isFPExtFoldable(PreferredFusedOpcode, VT, N000.getValueType())) {
        return DAG.getNode(ISD::FNEG, SL, VT,
                           DAG.getNode(PreferredFusedOpcode, SL, VT,
                                       DAG.getNode(ISD::FP_EXTEND, SL, VT,
                                                   N000.getOperand(0)),
                                       DAG.getNode(ISD::FP_EXTEND, SL, VT,
                                                   N000.getOperand(1)),
                                       N1, Flags));
      }
    }
  }

  // More folding opportunities when target permits.
  if (Aggressive) {
    // fold (fsub (fma x, y, (fmul u, v)), z)
    //   -> (fma x, y (fma u, v, (fneg z)))
    if (CanFuse && N0.getOpcode() == PreferredFusedOpcode &&
        isContractableFMUL(N0.getOperand(2)) && N0->hasOneUse() &&
        N0.getOperand(2)->hasOneUse()) {
      return DAG.getNode(PreferredFusedOpcode, SL, VT,
                         N0.getOperand(0), N0.getOperand(1),
                         DAG.getNode(PreferredFusedOpcode, SL, VT,
                                     N0.getOperand(2).getOperand(0),
                                     N0.getOperand(2).getOperand(1),
                                     DAG.getNode(ISD::FNEG, SL, VT,
                                                 N1), Flags), Flags);
    }

    // fold (fsub x, (fma y, z, (fmul u, v)))
    //   -> (fma (fneg y), z, (fma (fneg u), v, x))
    if (CanFuse && N1.getOpcode() == PreferredFusedOpcode &&
        isContractableFMUL(N1.getOperand(2))) {
      SDValue N20 = N1.getOperand(2).getOperand(0);
      SDValue N21 = N1.getOperand(2).getOperand(1);
      return DAG.getNode(PreferredFusedOpcode, SL, VT,
                         DAG.getNode(ISD::FNEG, SL, VT,
                                     N1.getOperand(0)),
                         N1.getOperand(1),
                         DAG.getNode(PreferredFusedOpcode, SL, VT,
                                     DAG.getNode(ISD::FNEG, SL, VT, N20),
                                     N21, N0, Flags), Flags);
    }


    // fold (fsub (fma x, y, (fpext (fmul u, v))), z)
    //   -> (fma x, y (fma (fpext u), (fpext v), (fneg z)))
    if (N0.getOpcode() == PreferredFusedOpcode) {
      SDValue N02 = N0.getOperand(2);
      if (N02.getOpcode() == ISD::FP_EXTEND) {
        SDValue N020 = N02.getOperand(0);
        if (isContractableFMUL(N020) &&
            TLI.isFPExtFoldable(PreferredFusedOpcode, VT, N020.getValueType())) {
          return DAG.getNode(PreferredFusedOpcode, SL, VT,
                             N0.getOperand(0), N0.getOperand(1),
                             DAG.getNode(PreferredFusedOpcode, SL, VT,
                                         DAG.getNode(ISD::FP_EXTEND, SL, VT,
                                                     N020.getOperand(0)),
                                         DAG.getNode(ISD::FP_EXTEND, SL, VT,
                                                     N020.getOperand(1)),
                                         DAG.getNode(ISD::FNEG, SL, VT,
                                                     N1), Flags), Flags);
        }
      }
    }

    // fold (fsub (fpext (fma x, y, (fmul u, v))), z)
    //   -> (fma (fpext x), (fpext y),
    //           (fma (fpext u), (fpext v), (fneg z)))
    // FIXME: This turns two single-precision and one double-precision
    // operation into two double-precision operations, which might not be
    // interesting for all targets, especially GPUs.
    if (N0.getOpcode() == ISD::FP_EXTEND) {
      SDValue N00 = N0.getOperand(0);
      if (N00.getOpcode() == PreferredFusedOpcode) {
        SDValue N002 = N00.getOperand(2);
        if (isContractableFMUL(N002) &&
            TLI.isFPExtFoldable(PreferredFusedOpcode, VT, N00.getValueType())) {
          return DAG.getNode(PreferredFusedOpcode, SL, VT,
                             DAG.getNode(ISD::FP_EXTEND, SL, VT,
                                         N00.getOperand(0)),
                             DAG.getNode(ISD::FP_EXTEND, SL, VT,
                                         N00.getOperand(1)),
                             DAG.getNode(PreferredFusedOpcode, SL, VT,
                                         DAG.getNode(ISD::FP_EXTEND, SL, VT,
                                                     N002.getOperand(0)),
                                         DAG.getNode(ISD::FP_EXTEND, SL, VT,
                                                     N002.getOperand(1)),
                                         DAG.getNode(ISD::FNEG, SL, VT,
                                                     N1), Flags), Flags);
        }
      }
    }

    // fold (fsub x, (fma y, z, (fpext (fmul u, v))))
    //   -> (fma (fneg y), z, (fma (fneg (fpext u)), (fpext v), x))
    if (N1.getOpcode() == PreferredFusedOpcode &&
        N1.getOperand(2).getOpcode() == ISD::FP_EXTEND) {
      SDValue N120 = N1.getOperand(2).getOperand(0);
      if (isContractableFMUL(N120) &&
          TLI.isFPExtFoldable(PreferredFusedOpcode, VT, N120.getValueType())) {
        SDValue N1200 = N120.getOperand(0);
        SDValue N1201 = N120.getOperand(1);
        return DAG.getNode(PreferredFusedOpcode, SL, VT,
                           DAG.getNode(ISD::FNEG, SL, VT, N1.getOperand(0)),
                           N1.getOperand(1),
                           DAG.getNode(PreferredFusedOpcode, SL, VT,
                                       DAG.getNode(ISD::FNEG, SL, VT,
                                                   DAG.getNode(ISD::FP_EXTEND, SL,
                                                               VT, N1200)),
                                       DAG.getNode(ISD::FP_EXTEND, SL, VT,
                                                   N1201),
                                       N0, Flags), Flags);
      }
    }

    // fold (fsub x, (fpext (fma y, z, (fmul u, v))))
    //   -> (fma (fneg (fpext y)), (fpext z),
    //           (fma (fneg (fpext u)), (fpext v), x))
    // FIXME: This turns two single-precision and one double-precision
    // operation into two double-precision operations, which might not be
    // interesting for all targets, especially GPUs.
    if (N1.getOpcode() == ISD::FP_EXTEND &&
        N1.getOperand(0).getOpcode() == PreferredFusedOpcode) {
      SDValue CvtSrc = N1.getOperand(0);
      SDValue N100 = CvtSrc.getOperand(0);
      SDValue N101 = CvtSrc.getOperand(1);
      SDValue N102 = CvtSrc.getOperand(2);
      if (isContractableFMUL(N102) &&
          TLI.isFPExtFoldable(PreferredFusedOpcode, VT, CvtSrc.getValueType())) {
        SDValue N1020 = N102.getOperand(0);
        SDValue N1021 = N102.getOperand(1);
        return DAG.getNode(PreferredFusedOpcode, SL, VT,
                           DAG.getNode(ISD::FNEG, SL, VT,
                                       DAG.getNode(ISD::FP_EXTEND, SL, VT,
                                                   N100)),
                           DAG.getNode(ISD::FP_EXTEND, SL, VT, N101),
                           DAG.getNode(PreferredFusedOpcode, SL, VT,
                                       DAG.getNode(ISD::FNEG, SL, VT,
                                                   DAG.getNode(ISD::FP_EXTEND, SL,
                                                               VT, N1020)),
                                       DAG.getNode(ISD::FP_EXTEND, SL, VT,
                                                   N1021),
                                       N0, Flags), Flags);
      }
    }
  }

  return SDValue();
}

/// Try to perform FMA combining on a given FMUL node based on the distributive
/// law x * (y + 1) = x * y + x and variants thereof (commuted versions,
/// subtraction instead of addition).
SDValue DAGCombiner::visitFMULForFMADistributiveCombine(SDNode *N) {
  SDValue N0 = N->getOperand(0);
  SDValue N1 = N->getOperand(1);
  EVT VT = N->getValueType(0);
  SDLoc SL(N);
  const SDNodeFlags Flags = N->getFlags();

  assert(N->getOpcode() == ISD::FMUL && "Expected FMUL Operation");

  const TargetOptions &Options = DAG.getTarget().Options;

  // The transforms below are incorrect when x == 0 and y == inf, because the
  // intermediate multiplication produces a nan.
  if (!Options.NoInfsFPMath)
    return SDValue();

  // Floating-point multiply-add without intermediate rounding.
  bool HasFMA =
      (Options.AllowFPOpFusion == FPOpFusion::Fast || Options.UnsafeFPMath) &&
      TLI.isFMAFasterThanFMulAndFAdd(VT) &&
      (!LegalOperations || TLI.isOperationLegalOrCustom(ISD::FMA, VT));

  // Floating-point multiply-add with intermediate rounding. This can result
  // in a less precise result due to the changed rounding order.
  bool HasFMAD = Options.UnsafeFPMath &&
                 (LegalOperations && TLI.isOperationLegal(ISD::FMAD, VT));

  // No valid opcode, do not combine.
  if (!HasFMAD && !HasFMA)
    return SDValue();

  // Always prefer FMAD to FMA for precision.
  unsigned PreferredFusedOpcode = HasFMAD ? ISD::FMAD : ISD::FMA;
  bool Aggressive = TLI.enableAggressiveFMAFusion(VT);

  // fold (fmul (fadd x0, +1.0), y) -> (fma x0, y, y)
  // fold (fmul (fadd x0, -1.0), y) -> (fma x0, y, (fneg y))
  auto FuseFADD = [&](SDValue X, SDValue Y, const SDNodeFlags Flags) {
    if (X.getOpcode() == ISD::FADD && (Aggressive || X->hasOneUse())) {
      if (auto *C = isConstOrConstSplatFP(X.getOperand(1), true)) {
        if (C->isExactlyValue(+1.0))
          return DAG.getNode(PreferredFusedOpcode, SL, VT, X.getOperand(0), Y,
                             Y, Flags);
        if (C->isExactlyValue(-1.0))
          return DAG.getNode(PreferredFusedOpcode, SL, VT, X.getOperand(0), Y,
                             DAG.getNode(ISD::FNEG, SL, VT, Y), Flags);
      }
    }
    return SDValue();
  };

  if (SDValue FMA = FuseFADD(N0, N1, Flags))
    return FMA;
  if (SDValue FMA = FuseFADD(N1, N0, Flags))
    return FMA;

  // fold (fmul (fsub +1.0, x1), y) -> (fma (fneg x1), y, y)
  // fold (fmul (fsub -1.0, x1), y) -> (fma (fneg x1), y, (fneg y))
  // fold (fmul (fsub x0, +1.0), y) -> (fma x0, y, (fneg y))
  // fold (fmul (fsub x0, -1.0), y) -> (fma x0, y, y)
  auto FuseFSUB = [&](SDValue X, SDValue Y, const SDNodeFlags Flags) {
    if (X.getOpcode() == ISD::FSUB && (Aggressive || X->hasOneUse())) {
      if (auto *C0 = isConstOrConstSplatFP(X.getOperand(0), true)) {
        if (C0->isExactlyValue(+1.0))
          return DAG.getNode(PreferredFusedOpcode, SL, VT,
                             DAG.getNode(ISD::FNEG, SL, VT, X.getOperand(1)), Y,
                             Y, Flags);
        if (C0->isExactlyValue(-1.0))
          return DAG.getNode(PreferredFusedOpcode, SL, VT,
                             DAG.getNode(ISD::FNEG, SL, VT, X.getOperand(1)), Y,
                             DAG.getNode(ISD::FNEG, SL, VT, Y), Flags);
      }
      if (auto *C1 = isConstOrConstSplatFP(X.getOperand(1), true)) {
        if (C1->isExactlyValue(+1.0))
          return DAG.getNode(PreferredFusedOpcode, SL, VT, X.getOperand(0), Y,
                             DAG.getNode(ISD::FNEG, SL, VT, Y), Flags);
        if (C1->isExactlyValue(-1.0))
          return DAG.getNode(PreferredFusedOpcode, SL, VT, X.getOperand(0), Y,
                             Y, Flags);
      }
    }
    return SDValue();
  };

  if (SDValue FMA = FuseFSUB(N0, N1, Flags))
    return FMA;
  if (SDValue FMA = FuseFSUB(N1, N0, Flags))
    return FMA;

  return SDValue();
}

SDValue DAGCombiner::visitFADD(SDNode *N) {
  SDValue N0 = N->getOperand(0);
  SDValue N1 = N->getOperand(1);
  bool N0CFP = isConstantFPBuildVectorOrConstantFP(N0);
  bool N1CFP = isConstantFPBuildVectorOrConstantFP(N1);
  EVT VT = N->getValueType(0);
  SDLoc DL(N);
  const TargetOptions &Options = DAG.getTarget().Options;
  const SDNodeFlags Flags = N->getFlags();

  // fold vector ops
  if (VT.isVector())
    if (SDValue FoldedVOp = SimplifyVBinOp(N))
      return FoldedVOp;

  // fold (fadd c1, c2) -> c1 + c2
  if (N0CFP && N1CFP)
    return DAG.getNode(ISD::FADD, DL, VT, N0, N1, Flags);

  // canonicalize constant to RHS
  if (N0CFP && !N1CFP)
    return DAG.getNode(ISD::FADD, DL, VT, N1, N0, Flags);

  // N0 + -0.0 --> N0 (also allowed with +0.0 and fast-math)
  ConstantFPSDNode *N1C = isConstOrConstSplatFP(N1, true);
  if (N1C && N1C->isZero())
    if (N1C->isNegative() || Options.UnsafeFPMath || Flags.hasNoSignedZeros())
      return N0;

  if (SDValue NewSel = foldBinOpIntoSelect(N))
    return NewSel;

  // fold (fadd A, (fneg B)) -> (fsub A, B)
  if ((!LegalOperations || TLI.isOperationLegalOrCustom(ISD::FSUB, VT)) &&
      isNegatibleForFree(N1, LegalOperations, TLI, &Options) == 2)
    return DAG.getNode(ISD::FSUB, DL, VT, N0,
                       GetNegatedExpression(N1, DAG, LegalOperations), Flags);

  // fold (fadd (fneg A), B) -> (fsub B, A)
  if ((!LegalOperations || TLI.isOperationLegalOrCustom(ISD::FSUB, VT)) &&
      isNegatibleForFree(N0, LegalOperations, TLI, &Options) == 2)
    return DAG.getNode(ISD::FSUB, DL, VT, N1,
                       GetNegatedExpression(N0, DAG, LegalOperations), Flags);

  auto isFMulNegTwo = [](SDValue FMul) {
    if (!FMul.hasOneUse() || FMul.getOpcode() != ISD::FMUL)
      return false;
    auto *C = isConstOrConstSplatFP(FMul.getOperand(1), true);
    return C && C->isExactlyValue(-2.0);
  };

  // fadd (fmul B, -2.0), A --> fsub A, (fadd B, B)
  if (isFMulNegTwo(N0)) {
    SDValue B = N0.getOperand(0);
    SDValue Add = DAG.getNode(ISD::FADD, DL, VT, B, B, Flags);
    return DAG.getNode(ISD::FSUB, DL, VT, N1, Add, Flags);
  }
  // fadd A, (fmul B, -2.0) --> fsub A, (fadd B, B)
  if (isFMulNegTwo(N1)) {
    SDValue B = N1.getOperand(0);
    SDValue Add = DAG.getNode(ISD::FADD, DL, VT, B, B, Flags);
    return DAG.getNode(ISD::FSUB, DL, VT, N0, Add, Flags);
  }

  // No FP constant should be created after legalization as Instruction
  // Selection pass has a hard time dealing with FP constants.
  bool AllowNewConst = (Level < AfterLegalizeDAG);

  // If 'unsafe math' or nnan is enabled, fold lots of things.
  if ((Options.UnsafeFPMath || Flags.hasNoNaNs()) && AllowNewConst) {
    // If allowed, fold (fadd (fneg x), x) -> 0.0
    if (N0.getOpcode() == ISD::FNEG && N0.getOperand(0) == N1)
      return DAG.getConstantFP(0.0, DL, VT);

    // If allowed, fold (fadd x, (fneg x)) -> 0.0
    if (N1.getOpcode() == ISD::FNEG && N1.getOperand(0) == N0)
      return DAG.getConstantFP(0.0, DL, VT);
  }

  // If 'unsafe math' or reassoc and nsz, fold lots of things.
  // TODO: break out portions of the transformations below for which Unsafe is
  //       considered and which do not require both nsz and reassoc
  if ((Options.UnsafeFPMath ||
       (Flags.hasAllowReassociation() && Flags.hasNoSignedZeros())) &&
      AllowNewConst) {
    // fadd (fadd x, c1), c2 -> fadd x, c1 + c2
    if (N1CFP && N0.getOpcode() == ISD::FADD &&
        isConstantFPBuildVectorOrConstantFP(N0.getOperand(1))) {
      SDValue NewC = DAG.getNode(ISD::FADD, DL, VT, N0.getOperand(1), N1, Flags);
      return DAG.getNode(ISD::FADD, DL, VT, N0.getOperand(0), NewC, Flags);
    }

    // We can fold chains of FADD's of the same value into multiplications.
    // This transform is not safe in general because we are reducing the number
    // of rounding steps.
    if (TLI.isOperationLegalOrCustom(ISD::FMUL, VT) && !N0CFP && !N1CFP) {
      if (N0.getOpcode() == ISD::FMUL) {
        bool CFP00 = isConstantFPBuildVectorOrConstantFP(N0.getOperand(0));
        bool CFP01 = isConstantFPBuildVectorOrConstantFP(N0.getOperand(1));

        // (fadd (fmul x, c), x) -> (fmul x, c+1)
        if (CFP01 && !CFP00 && N0.getOperand(0) == N1) {
          SDValue NewCFP = DAG.getNode(ISD::FADD, DL, VT, N0.getOperand(1),
                                       DAG.getConstantFP(1.0, DL, VT), Flags);
          return DAG.getNode(ISD::FMUL, DL, VT, N1, NewCFP, Flags);
        }

        // (fadd (fmul x, c), (fadd x, x)) -> (fmul x, c+2)
        if (CFP01 && !CFP00 && N1.getOpcode() == ISD::FADD &&
            N1.getOperand(0) == N1.getOperand(1) &&
            N0.getOperand(0) == N1.getOperand(0)) {
          SDValue NewCFP = DAG.getNode(ISD::FADD, DL, VT, N0.getOperand(1),
                                       DAG.getConstantFP(2.0, DL, VT), Flags);
          return DAG.getNode(ISD::FMUL, DL, VT, N0.getOperand(0), NewCFP, Flags);
        }
      }

      if (N1.getOpcode() == ISD::FMUL) {
        bool CFP10 = isConstantFPBuildVectorOrConstantFP(N1.getOperand(0));
        bool CFP11 = isConstantFPBuildVectorOrConstantFP(N1.getOperand(1));

        // (fadd x, (fmul x, c)) -> (fmul x, c+1)
        if (CFP11 && !CFP10 && N1.getOperand(0) == N0) {
          SDValue NewCFP = DAG.getNode(ISD::FADD, DL, VT, N1.getOperand(1),
                                       DAG.getConstantFP(1.0, DL, VT), Flags);
          return DAG.getNode(ISD::FMUL, DL, VT, N0, NewCFP, Flags);
        }

        // (fadd (fadd x, x), (fmul x, c)) -> (fmul x, c+2)
        if (CFP11 && !CFP10 && N0.getOpcode() == ISD::FADD &&
            N0.getOperand(0) == N0.getOperand(1) &&
            N1.getOperand(0) == N0.getOperand(0)) {
          SDValue NewCFP = DAG.getNode(ISD::FADD, DL, VT, N1.getOperand(1),
                                       DAG.getConstantFP(2.0, DL, VT), Flags);
          return DAG.getNode(ISD::FMUL, DL, VT, N1.getOperand(0), NewCFP, Flags);
        }
      }

      if (N0.getOpcode() == ISD::FADD) {
        bool CFP00 = isConstantFPBuildVectorOrConstantFP(N0.getOperand(0));
        // (fadd (fadd x, x), x) -> (fmul x, 3.0)
        if (!CFP00 && N0.getOperand(0) == N0.getOperand(1) &&
            (N0.getOperand(0) == N1)) {
          return DAG.getNode(ISD::FMUL, DL, VT,
                             N1, DAG.getConstantFP(3.0, DL, VT), Flags);
        }
      }

      if (N1.getOpcode() == ISD::FADD) {
        bool CFP10 = isConstantFPBuildVectorOrConstantFP(N1.getOperand(0));
        // (fadd x, (fadd x, x)) -> (fmul x, 3.0)
        if (!CFP10 && N1.getOperand(0) == N1.getOperand(1) &&
            N1.getOperand(0) == N0) {
          return DAG.getNode(ISD::FMUL, DL, VT,
                             N0, DAG.getConstantFP(3.0, DL, VT), Flags);
        }
      }

      // (fadd (fadd x, x), (fadd x, x)) -> (fmul x, 4.0)
      if (N0.getOpcode() == ISD::FADD && N1.getOpcode() == ISD::FADD &&
          N0.getOperand(0) == N0.getOperand(1) &&
          N1.getOperand(0) == N1.getOperand(1) &&
          N0.getOperand(0) == N1.getOperand(0)) {
        return DAG.getNode(ISD::FMUL, DL, VT, N0.getOperand(0),
                           DAG.getConstantFP(4.0, DL, VT), Flags);
      }
    }
  } // enable-unsafe-fp-math

  // FADD -> FMA combines:
  if (SDValue Fused = visitFADDForFMACombine(N)) {
    AddToWorklist(Fused.getNode());
    return Fused;
  }
  return SDValue();
}

SDValue DAGCombiner::visitFSUB(SDNode *N) {
  SDValue N0 = N->getOperand(0);
  SDValue N1 = N->getOperand(1);
  ConstantFPSDNode *N0CFP = isConstOrConstSplatFP(N0, true);
  ConstantFPSDNode *N1CFP = isConstOrConstSplatFP(N1, true);
  EVT VT = N->getValueType(0);
  SDLoc DL(N);
  const TargetOptions &Options = DAG.getTarget().Options;
  const SDNodeFlags Flags = N->getFlags();

  // fold vector ops
  if (VT.isVector())
    if (SDValue FoldedVOp = SimplifyVBinOp(N))
      return FoldedVOp;

  // fold (fsub c1, c2) -> c1-c2
  if (N0CFP && N1CFP)
    return DAG.getNode(ISD::FSUB, DL, VT, N0, N1, Flags);

  if (SDValue NewSel = foldBinOpIntoSelect(N))
    return NewSel;

  // (fsub A, 0) -> A
  if (N1CFP && N1CFP->isZero()) {
    if (!N1CFP->isNegative() || Options.UnsafeFPMath ||
        Flags.hasNoSignedZeros()) {
      return N0;
    }
  }

  if (N0 == N1) {
    // (fsub x, x) -> 0.0
    if (Options.UnsafeFPMath || Flags.hasNoNaNs())
      return DAG.getConstantFP(0.0f, DL, VT);
  }

  // (fsub -0.0, N1) -> -N1
  if (N0CFP && N0CFP->isZero()) {
    if (N0CFP->isNegative() ||
        (Options.NoSignedZerosFPMath || Flags.hasNoSignedZeros())) {
      if (isNegatibleForFree(N1, LegalOperations, TLI, &Options))
        return GetNegatedExpression(N1, DAG, LegalOperations);
      if (!LegalOperations || TLI.isOperationLegal(ISD::FNEG, VT))
        return DAG.getNode(ISD::FNEG, DL, VT, N1, Flags);
    }
  }

  if ((Options.UnsafeFPMath ||
      (Flags.hasAllowReassociation() && Flags.hasNoSignedZeros()))
      && N1.getOpcode() == ISD::FADD) {
    // X - (X + Y) -> -Y
    if (N0 == N1->getOperand(0))
      return DAG.getNode(ISD::FNEG, DL, VT, N1->getOperand(1), Flags);
    // X - (Y + X) -> -Y
    if (N0 == N1->getOperand(1))
      return DAG.getNode(ISD::FNEG, DL, VT, N1->getOperand(0), Flags);
  }

  // fold (fsub A, (fneg B)) -> (fadd A, B)
  if (isNegatibleForFree(N1, LegalOperations, TLI, &Options))
    return DAG.getNode(ISD::FADD, DL, VT, N0,
                       GetNegatedExpression(N1, DAG, LegalOperations), Flags);

  // FSUB -> FMA combines:
  if (SDValue Fused = visitFSUBForFMACombine(N)) {
    AddToWorklist(Fused.getNode());
    return Fused;
  }

  return SDValue();
}

SDValue DAGCombiner::visitFMUL(SDNode *N) {
  SDValue N0 = N->getOperand(0);
  SDValue N1 = N->getOperand(1);
  ConstantFPSDNode *N0CFP = isConstOrConstSplatFP(N0, true);
  ConstantFPSDNode *N1CFP = isConstOrConstSplatFP(N1, true);
  EVT VT = N->getValueType(0);
  SDLoc DL(N);
  const TargetOptions &Options = DAG.getTarget().Options;
  const SDNodeFlags Flags = N->getFlags();

  // fold vector ops
  if (VT.isVector()) {
    // This just handles C1 * C2 for vectors. Other vector folds are below.
    if (SDValue FoldedVOp = SimplifyVBinOp(N))
      return FoldedVOp;
  }

  // fold (fmul c1, c2) -> c1*c2
  if (N0CFP && N1CFP)
    return DAG.getNode(ISD::FMUL, DL, VT, N0, N1, Flags);

  // canonicalize constant to RHS
  if (isConstantFPBuildVectorOrConstantFP(N0) &&
     !isConstantFPBuildVectorOrConstantFP(N1))
    return DAG.getNode(ISD::FMUL, DL, VT, N1, N0, Flags);

  // fold (fmul A, 1.0) -> A
  if (N1CFP && N1CFP->isExactlyValue(1.0))
    return N0;

  if (SDValue NewSel = foldBinOpIntoSelect(N))
    return NewSel;

  if (Options.UnsafeFPMath ||
      (Flags.hasNoNaNs() && Flags.hasNoSignedZeros())) {
    // fold (fmul A, 0) -> 0
    if (N1CFP && N1CFP->isZero())
      return N1;
  }

  if (Options.UnsafeFPMath || Flags.hasAllowReassociation()) {
    // fmul (fmul X, C1), C2 -> fmul X, C1 * C2
    if (isConstantFPBuildVectorOrConstantFP(N1) &&
        N0.getOpcode() == ISD::FMUL) {
      SDValue N00 = N0.getOperand(0);
      SDValue N01 = N0.getOperand(1);
      // Avoid an infinite loop by making sure that N00 is not a constant
      // (the inner multiply has not been constant folded yet).
      if (isConstantFPBuildVectorOrConstantFP(N01) &&
          !isConstantFPBuildVectorOrConstantFP(N00)) {
        SDValue MulConsts = DAG.getNode(ISD::FMUL, DL, VT, N01, N1, Flags);
        return DAG.getNode(ISD::FMUL, DL, VT, N00, MulConsts, Flags);
      }
    }

    // Match a special-case: we convert X * 2.0 into fadd.
    // fmul (fadd X, X), C -> fmul X, 2.0 * C
    if (N0.getOpcode() == ISD::FADD && N0.hasOneUse() &&
        N0.getOperand(0) == N0.getOperand(1)) {
      const SDValue Two = DAG.getConstantFP(2.0, DL, VT);
      SDValue MulConsts = DAG.getNode(ISD::FMUL, DL, VT, Two, N1, Flags);
      return DAG.getNode(ISD::FMUL, DL, VT, N0.getOperand(0), MulConsts, Flags);
    }
  }

  // fold (fmul X, 2.0) -> (fadd X, X)
  if (N1CFP && N1CFP->isExactlyValue(+2.0))
    return DAG.getNode(ISD::FADD, DL, VT, N0, N0, Flags);

  // fold (fmul X, -1.0) -> (fneg X)
  if (N1CFP && N1CFP->isExactlyValue(-1.0))
    if (!LegalOperations || TLI.isOperationLegal(ISD::FNEG, VT))
      return DAG.getNode(ISD::FNEG, DL, VT, N0);

  // fold (fmul (fneg X), (fneg Y)) -> (fmul X, Y)
  if (char LHSNeg = isNegatibleForFree(N0, LegalOperations, TLI, &Options)) {
    if (char RHSNeg = isNegatibleForFree(N1, LegalOperations, TLI, &Options)) {
      // Both can be negated for free, check to see if at least one is cheaper
      // negated.
      if (LHSNeg == 2 || RHSNeg == 2)
        return DAG.getNode(ISD::FMUL, DL, VT,
                           GetNegatedExpression(N0, DAG, LegalOperations),
                           GetNegatedExpression(N1, DAG, LegalOperations),
                           Flags);
    }
  }

  // fold (fmul X, (select (fcmp X > 0.0), -1.0, 1.0)) -> (fneg (fabs X))
  // fold (fmul X, (select (fcmp X > 0.0), 1.0, -1.0)) -> (fabs X)
  if (Flags.hasNoNaNs() && Flags.hasNoSignedZeros() &&
      (N0.getOpcode() == ISD::SELECT || N1.getOpcode() == ISD::SELECT) &&
      TLI.isOperationLegal(ISD::FABS, VT)) {
    SDValue Select = N0, X = N1;
    if (Select.getOpcode() != ISD::SELECT)
      std::swap(Select, X);

    SDValue Cond = Select.getOperand(0);
    auto TrueOpnd  = dyn_cast<ConstantFPSDNode>(Select.getOperand(1));
    auto FalseOpnd = dyn_cast<ConstantFPSDNode>(Select.getOperand(2));

    if (TrueOpnd && FalseOpnd &&
        Cond.getOpcode() == ISD::SETCC && Cond.getOperand(0) == X &&
        isa<ConstantFPSDNode>(Cond.getOperand(1)) &&
        cast<ConstantFPSDNode>(Cond.getOperand(1))->isExactlyValue(0.0)) {
      ISD::CondCode CC = cast<CondCodeSDNode>(Cond.getOperand(2))->get();
      switch (CC) {
      default: break;
      case ISD::SETOLT:
      case ISD::SETULT:
      case ISD::SETOLE:
      case ISD::SETULE:
      case ISD::SETLT:
      case ISD::SETLE:
        std::swap(TrueOpnd, FalseOpnd);
        LLVM_FALLTHROUGH;
      case ISD::SETOGT:
      case ISD::SETUGT:
      case ISD::SETOGE:
      case ISD::SETUGE:
      case ISD::SETGT:
      case ISD::SETGE:
        if (TrueOpnd->isExactlyValue(-1.0) && FalseOpnd->isExactlyValue(1.0) &&
            TLI.isOperationLegal(ISD::FNEG, VT))
          return DAG.getNode(ISD::FNEG, DL, VT,
                   DAG.getNode(ISD::FABS, DL, VT, X));
        if (TrueOpnd->isExactlyValue(1.0) && FalseOpnd->isExactlyValue(-1.0))
          return DAG.getNode(ISD::FABS, DL, VT, X);

        break;
      }
    }
  }

  // FMUL -> FMA combines:
  if (SDValue Fused = visitFMULForFMADistributiveCombine(N)) {
    AddToWorklist(Fused.getNode());
    return Fused;
  }

  return SDValue();
}

SDValue DAGCombiner::visitFMA(SDNode *N) {
  SDValue N0 = N->getOperand(0);
  SDValue N1 = N->getOperand(1);
  SDValue N2 = N->getOperand(2);
  ConstantFPSDNode *N0CFP = dyn_cast<ConstantFPSDNode>(N0);
  ConstantFPSDNode *N1CFP = dyn_cast<ConstantFPSDNode>(N1);
  EVT VT = N->getValueType(0);
  SDLoc DL(N);
  const TargetOptions &Options = DAG.getTarget().Options;

  // FMA nodes have flags that propagate to the created nodes.
  const SDNodeFlags Flags = N->getFlags();
  bool UnsafeFPMath = Options.UnsafeFPMath || isContractable(N);

  // Constant fold FMA.
  if (isa<ConstantFPSDNode>(N0) &&
      isa<ConstantFPSDNode>(N1) &&
      isa<ConstantFPSDNode>(N2)) {
    return DAG.getNode(ISD::FMA, DL, VT, N0, N1, N2);
  }

  if (UnsafeFPMath) {
    if (N0CFP && N0CFP->isZero())
      return N2;
    if (N1CFP && N1CFP->isZero())
      return N2;
  }
  // TODO: The FMA node should have flags that propagate to these nodes.
  if (N0CFP && N0CFP->isExactlyValue(1.0))
    return DAG.getNode(ISD::FADD, SDLoc(N), VT, N1, N2);
  if (N1CFP && N1CFP->isExactlyValue(1.0))
    return DAG.getNode(ISD::FADD, SDLoc(N), VT, N0, N2);

  // Canonicalize (fma c, x, y) -> (fma x, c, y)
  if (isConstantFPBuildVectorOrConstantFP(N0) &&
     !isConstantFPBuildVectorOrConstantFP(N1))
    return DAG.getNode(ISD::FMA, SDLoc(N), VT, N1, N0, N2);

  if (UnsafeFPMath) {
    // (fma x, c1, (fmul x, c2)) -> (fmul x, c1+c2)
    if (N2.getOpcode() == ISD::FMUL && N0 == N2.getOperand(0) &&
        isConstantFPBuildVectorOrConstantFP(N1) &&
        isConstantFPBuildVectorOrConstantFP(N2.getOperand(1))) {
      return DAG.getNode(ISD::FMUL, DL, VT, N0,
                         DAG.getNode(ISD::FADD, DL, VT, N1, N2.getOperand(1),
                                     Flags), Flags);
    }

    // (fma (fmul x, c1), c2, y) -> (fma x, c1*c2, y)
    if (N0.getOpcode() == ISD::FMUL &&
        isConstantFPBuildVectorOrConstantFP(N1) &&
        isConstantFPBuildVectorOrConstantFP(N0.getOperand(1))) {
      return DAG.getNode(ISD::FMA, DL, VT,
                         N0.getOperand(0),
                         DAG.getNode(ISD::FMUL, DL, VT, N1, N0.getOperand(1),
                                     Flags),
                         N2);
    }
  }

  // (fma x, 1, y) -> (fadd x, y)
  // (fma x, -1, y) -> (fadd (fneg x), y)
  if (N1CFP) {
    if (N1CFP->isExactlyValue(1.0))
      // TODO: The FMA node should have flags that propagate to this node.
      return DAG.getNode(ISD::FADD, DL, VT, N0, N2);

    if (N1CFP->isExactlyValue(-1.0) &&
        (!LegalOperations || TLI.isOperationLegal(ISD::FNEG, VT))) {
      SDValue RHSNeg = DAG.getNode(ISD::FNEG, DL, VT, N0);
      AddToWorklist(RHSNeg.getNode());
      // TODO: The FMA node should have flags that propagate to this node.
      return DAG.getNode(ISD::FADD, DL, VT, N2, RHSNeg);
    }

    // fma (fneg x), K, y -> fma x -K, y
    if (N0.getOpcode() == ISD::FNEG &&
        (TLI.isOperationLegal(ISD::ConstantFP, VT) ||
         (N1.hasOneUse() && !TLI.isFPImmLegal(N1CFP->getValueAPF(), VT)))) {
      return DAG.getNode(ISD::FMA, DL, VT, N0.getOperand(0),
                         DAG.getNode(ISD::FNEG, DL, VT, N1, Flags), N2);
    }
  }

  if (UnsafeFPMath) {
    // (fma x, c, x) -> (fmul x, (c+1))
    if (N1CFP && N0 == N2) {
      return DAG.getNode(ISD::FMUL, DL, VT, N0,
                         DAG.getNode(ISD::FADD, DL, VT, N1,
                                     DAG.getConstantFP(1.0, DL, VT), Flags),
                         Flags);
    }

    // (fma x, c, (fneg x)) -> (fmul x, (c-1))
    if (N1CFP && N2.getOpcode() == ISD::FNEG && N2.getOperand(0) == N0) {
      return DAG.getNode(ISD::FMUL, DL, VT, N0,
                         DAG.getNode(ISD::FADD, DL, VT, N1,
                                     DAG.getConstantFP(-1.0, DL, VT), Flags),
                         Flags);
    }
  }

  return SDValue();
}

// Combine multiple FDIVs with the same divisor into multiple FMULs by the
// reciprocal.
// E.g., (a / D; b / D;) -> (recip = 1.0 / D; a * recip; b * recip)
// Notice that this is not always beneficial. One reason is different targets
// may have different costs for FDIV and FMUL, so sometimes the cost of two
// FDIVs may be lower than the cost of one FDIV and two FMULs. Another reason
// is the critical path is increased from "one FDIV" to "one FDIV + one FMUL".
SDValue DAGCombiner::combineRepeatedFPDivisors(SDNode *N) {
  bool UnsafeMath = DAG.getTarget().Options.UnsafeFPMath;
  const SDNodeFlags Flags = N->getFlags();
  if (!UnsafeMath && !Flags.hasAllowReciprocal())
    return SDValue();

  // Skip if current node is a reciprocal.
  SDValue N0 = N->getOperand(0);
  ConstantFPSDNode *N0CFP = dyn_cast<ConstantFPSDNode>(N0);
  if (N0CFP && N0CFP->isExactlyValue(1.0))
    return SDValue();

  // Exit early if the target does not want this transform or if there can't
  // possibly be enough uses of the divisor to make the transform worthwhile.
  SDValue N1 = N->getOperand(1);
  unsigned MinUses = TLI.combineRepeatedFPDivisors();
  if (!MinUses || N1->use_size() < MinUses)
    return SDValue();

  // Find all FDIV users of the same divisor.
  // Use a set because duplicates may be present in the user list.
  SetVector<SDNode *> Users;
  for (auto *U : N1->uses()) {
    if (U->getOpcode() == ISD::FDIV && U->getOperand(1) == N1) {
      // This division is eligible for optimization only if global unsafe math
      // is enabled or if this division allows reciprocal formation.
      if (UnsafeMath || U->getFlags().hasAllowReciprocal())
        Users.insert(U);
    }
  }

  // Now that we have the actual number of divisor uses, make sure it meets
  // the minimum threshold specified by the target.
  if (Users.size() < MinUses)
    return SDValue();

  EVT VT = N->getValueType(0);
  SDLoc DL(N);
  SDValue FPOne = DAG.getConstantFP(1.0, DL, VT);
  SDValue Reciprocal = DAG.getNode(ISD::FDIV, DL, VT, FPOne, N1, Flags);

  // Dividend / Divisor -> Dividend * Reciprocal
  for (auto *U : Users) {
    SDValue Dividend = U->getOperand(0);
    if (Dividend != FPOne) {
      SDValue NewNode = DAG.getNode(ISD::FMUL, SDLoc(U), VT, Dividend,
                                    Reciprocal, Flags);
      CombineTo(U, NewNode);
    } else if (U != Reciprocal.getNode()) {
      // In the absence of fast-math-flags, this user node is always the
      // same node as Reciprocal, but with FMF they may be different nodes.
      CombineTo(U, Reciprocal);
    }
  }
  return SDValue(N, 0);  // N was replaced.
}

SDValue DAGCombiner::visitFDIV(SDNode *N) {
  SDValue N0 = N->getOperand(0);
  SDValue N1 = N->getOperand(1);
  ConstantFPSDNode *N0CFP = dyn_cast<ConstantFPSDNode>(N0);
  ConstantFPSDNode *N1CFP = dyn_cast<ConstantFPSDNode>(N1);
  EVT VT = N->getValueType(0);
  SDLoc DL(N);
  const TargetOptions &Options = DAG.getTarget().Options;
  SDNodeFlags Flags = N->getFlags();

  // fold vector ops
  if (VT.isVector())
    if (SDValue FoldedVOp = SimplifyVBinOp(N))
      return FoldedVOp;

  // fold (fdiv c1, c2) -> c1/c2
  if (N0CFP && N1CFP)
    return DAG.getNode(ISD::FDIV, SDLoc(N), VT, N0, N1, Flags);

  if (SDValue NewSel = foldBinOpIntoSelect(N))
    return NewSel;

  if (Options.UnsafeFPMath || Flags.hasAllowReciprocal()) {
    // fold (fdiv X, c2) -> fmul X, 1/c2 if losing precision is acceptable.
    if (N1CFP) {
      // Compute the reciprocal 1.0 / c2.
      const APFloat &N1APF = N1CFP->getValueAPF();
      APFloat Recip(N1APF.getSemantics(), 1); // 1.0
      APFloat::opStatus st = Recip.divide(N1APF, APFloat::rmNearestTiesToEven);
      // Only do the transform if the reciprocal is a legal fp immediate that
      // isn't too nasty (eg NaN, denormal, ...).
      if ((st == APFloat::opOK || st == APFloat::opInexact) && // Not too nasty
          (!LegalOperations ||
           // FIXME: custom lowering of ConstantFP might fail (see e.g. ARM
           // backend)... we should handle this gracefully after Legalize.
           // TLI.isOperationLegalOrCustom(ISD::ConstantFP, VT) ||
           TLI.isOperationLegal(ISD::ConstantFP, VT) ||
           TLI.isFPImmLegal(Recip, VT)))
        return DAG.getNode(ISD::FMUL, DL, VT, N0,
                           DAG.getConstantFP(Recip, DL, VT), Flags);
    }

    // If this FDIV is part of a reciprocal square root, it may be folded
    // into a target-specific square root estimate instruction.
    if (N1.getOpcode() == ISD::FSQRT) {
      if (SDValue RV = buildRsqrtEstimate(N1.getOperand(0), Flags)) {
        return DAG.getNode(ISD::FMUL, DL, VT, N0, RV, Flags);
      }
    } else if (N1.getOpcode() == ISD::FP_EXTEND &&
               N1.getOperand(0).getOpcode() == ISD::FSQRT) {
      if (SDValue RV = buildRsqrtEstimate(N1.getOperand(0).getOperand(0),
                                          Flags)) {
        RV = DAG.getNode(ISD::FP_EXTEND, SDLoc(N1), VT, RV);
        AddToWorklist(RV.getNode());
        return DAG.getNode(ISD::FMUL, DL, VT, N0, RV, Flags);
      }
    } else if (N1.getOpcode() == ISD::FP_ROUND &&
               N1.getOperand(0).getOpcode() == ISD::FSQRT) {
      if (SDValue RV = buildRsqrtEstimate(N1.getOperand(0).getOperand(0),
                                          Flags)) {
        RV = DAG.getNode(ISD::FP_ROUND, SDLoc(N1), VT, RV, N1.getOperand(1));
        AddToWorklist(RV.getNode());
        return DAG.getNode(ISD::FMUL, DL, VT, N0, RV, Flags);
      }
    } else if (N1.getOpcode() == ISD::FMUL) {
      // Look through an FMUL. Even though this won't remove the FDIV directly,
      // it's still worthwhile to get rid of the FSQRT if possible.
      SDValue SqrtOp;
      SDValue OtherOp;
      if (N1.getOperand(0).getOpcode() == ISD::FSQRT) {
        SqrtOp = N1.getOperand(0);
        OtherOp = N1.getOperand(1);
      } else if (N1.getOperand(1).getOpcode() == ISD::FSQRT) {
        SqrtOp = N1.getOperand(1);
        OtherOp = N1.getOperand(0);
      }
      if (SqrtOp.getNode()) {
        // We found a FSQRT, so try to make this fold:
        // x / (y * sqrt(z)) -> x * (rsqrt(z) / y)
        if (SDValue RV = buildRsqrtEstimate(SqrtOp.getOperand(0), Flags)) {
          RV = DAG.getNode(ISD::FDIV, SDLoc(N1), VT, RV, OtherOp, Flags);
          AddToWorklist(RV.getNode());
          return DAG.getNode(ISD::FMUL, DL, VT, N0, RV, Flags);
        }
      }
    }

    // Fold into a reciprocal estimate and multiply instead of a real divide.
    if (SDValue RV = BuildReciprocalEstimate(N1, Flags)) {
      AddToWorklist(RV.getNode());
      return DAG.getNode(ISD::FMUL, DL, VT, N0, RV, Flags);
    }
  }

  // (fdiv (fneg X), (fneg Y)) -> (fdiv X, Y)
  if (char LHSNeg = isNegatibleForFree(N0, LegalOperations, TLI, &Options)) {
    if (char RHSNeg = isNegatibleForFree(N1, LegalOperations, TLI, &Options)) {
      // Both can be negated for free, check to see if at least one is cheaper
      // negated.
      if (LHSNeg == 2 || RHSNeg == 2)
        return DAG.getNode(ISD::FDIV, SDLoc(N), VT,
                           GetNegatedExpression(N0, DAG, LegalOperations),
                           GetNegatedExpression(N1, DAG, LegalOperations),
                           Flags);
    }
  }

  if (SDValue CombineRepeatedDivisors = combineRepeatedFPDivisors(N))
    return CombineRepeatedDivisors;

  return SDValue();
}

SDValue DAGCombiner::visitFREM(SDNode *N) {
  SDValue N0 = N->getOperand(0);
  SDValue N1 = N->getOperand(1);
  ConstantFPSDNode *N0CFP = dyn_cast<ConstantFPSDNode>(N0);
  ConstantFPSDNode *N1CFP = dyn_cast<ConstantFPSDNode>(N1);
  EVT VT = N->getValueType(0);

  // fold (frem c1, c2) -> fmod(c1,c2)
  if (N0CFP && N1CFP)
    return DAG.getNode(ISD::FREM, SDLoc(N), VT, N0, N1, N->getFlags());

  if (SDValue NewSel = foldBinOpIntoSelect(N))
    return NewSel;

  return SDValue();
}

SDValue DAGCombiner::visitFSQRT(SDNode *N) {
  SDNodeFlags Flags = N->getFlags();
  if (!DAG.getTarget().Options.UnsafeFPMath &&
      !Flags.hasApproximateFuncs())
    return SDValue();

  SDValue N0 = N->getOperand(0);
  if (TLI.isFsqrtCheap(N0, DAG))
    return SDValue();

  // FSQRT nodes have flags that propagate to the created nodes.
  return buildSqrtEstimate(N0, Flags);
}

/// copysign(x, fp_extend(y)) -> copysign(x, y)
/// copysign(x, fp_round(y)) -> copysign(x, y)
static inline bool CanCombineFCOPYSIGN_EXTEND_ROUND(SDNode *N) {
  SDValue N1 = N->getOperand(1);
  if ((N1.getOpcode() == ISD::FP_EXTEND ||
       N1.getOpcode() == ISD::FP_ROUND)) {
    // Do not optimize out type conversion of f128 type yet.
    // For some targets like x86_64, configuration is changed to keep one f128
    // value in one SSE register, but instruction selection cannot handle
    // FCOPYSIGN on SSE registers yet.
    EVT N1VT = N1->getValueType(0);
    EVT N1Op0VT = N1->getOperand(0).getValueType();
    return (N1VT == N1Op0VT || N1Op0VT != MVT::f128);
  }
  return false;
}

SDValue DAGCombiner::visitFCOPYSIGN(SDNode *N) {
  SDValue N0 = N->getOperand(0);
  SDValue N1 = N->getOperand(1);
  bool N0CFP = isConstantFPBuildVectorOrConstantFP(N0);
  bool N1CFP = isConstantFPBuildVectorOrConstantFP(N1);
  EVT VT = N->getValueType(0);

  if (N0CFP && N1CFP) // Constant fold
    return DAG.getNode(ISD::FCOPYSIGN, SDLoc(N), VT, N0, N1);

  if (ConstantFPSDNode *N1C = isConstOrConstSplatFP(N->getOperand(1))) {
    const APFloat &V = N1C->getValueAPF();
    // copysign(x, c1) -> fabs(x)       iff ispos(c1)
    // copysign(x, c1) -> fneg(fabs(x)) iff isneg(c1)
    if (!V.isNegative()) {
      if (!LegalOperations || TLI.isOperationLegal(ISD::FABS, VT))
        return DAG.getNode(ISD::FABS, SDLoc(N), VT, N0);
    } else {
      if (!LegalOperations || TLI.isOperationLegal(ISD::FNEG, VT))
        return DAG.getNode(ISD::FNEG, SDLoc(N), VT,
                           DAG.getNode(ISD::FABS, SDLoc(N0), VT, N0));
    }
  }

  // copysign(fabs(x), y) -> copysign(x, y)
  // copysign(fneg(x), y) -> copysign(x, y)
  // copysign(copysign(x,z), y) -> copysign(x, y)
  if (N0.getOpcode() == ISD::FABS || N0.getOpcode() == ISD::FNEG ||
      N0.getOpcode() == ISD::FCOPYSIGN)
    return DAG.getNode(ISD::FCOPYSIGN, SDLoc(N), VT, N0.getOperand(0), N1);

  // copysign(x, abs(y)) -> abs(x)
  if (N1.getOpcode() == ISD::FABS)
    return DAG.getNode(ISD::FABS, SDLoc(N), VT, N0);

  // copysign(x, copysign(y,z)) -> copysign(x, z)
  if (N1.getOpcode() == ISD::FCOPYSIGN)
    return DAG.getNode(ISD::FCOPYSIGN, SDLoc(N), VT, N0, N1.getOperand(1));

  // copysign(x, fp_extend(y)) -> copysign(x, y)
  // copysign(x, fp_round(y)) -> copysign(x, y)
  if (CanCombineFCOPYSIGN_EXTEND_ROUND(N))
    return DAG.getNode(ISD::FCOPYSIGN, SDLoc(N), VT, N0, N1.getOperand(0));

  return SDValue();
}

SDValue DAGCombiner::visitFPOW(SDNode *N) {
  ConstantFPSDNode *ExponentC = isConstOrConstSplatFP(N->getOperand(1));
  if (!ExponentC)
    return SDValue();

  // Try to convert x ** (1/3) into cube root.
  // TODO: Handle the various flavors of long double.
  // TODO: Since we're approximating, we don't need an exact 1/3 exponent.
  //       Some range near 1/3 should be fine.
  EVT VT = N->getValueType(0);
  if ((VT == MVT::f32 && ExponentC->getValueAPF().isExactlyValue(1.0f/3.0f)) ||
      (VT == MVT::f64 && ExponentC->getValueAPF().isExactlyValue(1.0/3.0))) {
    // pow(-0.0, 1/3) = +0.0; cbrt(-0.0) = -0.0.
    // pow(-inf, 1/3) = +inf; cbrt(-inf) = -inf.
    // pow(-val, 1/3) =  nan; cbrt(-val) = -num.
    // For regular numbers, rounding may cause the results to differ.
    // Therefore, we require { nsz ninf nnan afn } for this transform.
    // TODO: We could select out the special cases if we don't have nsz/ninf.
    SDNodeFlags Flags = N->getFlags();
    if (!Flags.hasNoSignedZeros() || !Flags.hasNoInfs() || !Flags.hasNoNaNs() ||
        !Flags.hasApproximateFuncs())
      return SDValue();

    // Do not create a cbrt() libcall if the target does not have it, and do not
    // turn a pow that has lowering support into a cbrt() libcall.
    if (!DAG.getLibInfo().has(LibFunc_cbrt) ||
        (!DAG.getTargetLoweringInfo().isOperationExpand(ISD::FPOW, VT) &&
         DAG.getTargetLoweringInfo().isOperationExpand(ISD::FCBRT, VT)))
      return SDValue();

    return DAG.getNode(ISD::FCBRT, SDLoc(N), VT, N->getOperand(0), Flags);
  }

  // Try to convert x ** (1/4) into square roots.
  // x ** (1/2) is canonicalized to sqrt, so we do not bother with that case.
  // TODO: This could be extended (using a target hook) to handle smaller
  // power-of-2 fractional exponents.
  if (ExponentC->getValueAPF().isExactlyValue(0.25)) {
    // pow(-0.0, 0.25) = +0.0; sqrt(sqrt(-0.0)) = -0.0.
    // pow(-inf, 0.25) = +inf; sqrt(sqrt(-inf)) =  NaN.
    // For regular numbers, rounding may cause the results to differ.
    // Therefore, we require { nsz ninf afn } for this transform.
    // TODO: We could select out the special cases if we don't have nsz/ninf.
    SDNodeFlags Flags = N->getFlags();
    if (!Flags.hasNoSignedZeros() || !Flags.hasNoInfs() ||
        !Flags.hasApproximateFuncs())
      return SDValue();

    // Don't double the number of libcalls. We are trying to inline fast code.
    if (!DAG.getTargetLoweringInfo().isOperationLegalOrCustom(ISD::FSQRT, VT))
      return SDValue();

    // Assume that libcalls are the smallest code.
    // TODO: This restriction should probably be lifted for vectors.
    if (DAG.getMachineFunction().getFunction().optForSize())
      return SDValue();

    // pow(X, 0.25) --> sqrt(sqrt(X))
    SDLoc DL(N);
    SDValue Sqrt = DAG.getNode(ISD::FSQRT, DL, VT, N->getOperand(0), Flags);
    return DAG.getNode(ISD::FSQRT, DL, VT, Sqrt, Flags);
  }

  return SDValue();
}

static SDValue foldFPToIntToFP(SDNode *N, SelectionDAG &DAG,
                               const TargetLowering &TLI) {
  // This optimization is guarded by a function attribute because it may produce
  // unexpected results. Ie, programs may be relying on the platform-specific
  // undefined behavior when the float-to-int conversion overflows.
  const Function &F = DAG.getMachineFunction().getFunction();
  Attribute StrictOverflow = F.getFnAttribute("strict-float-cast-overflow");
  if (StrictOverflow.getValueAsString().equals("false"))
    return SDValue();

  // We only do this if the target has legal ftrunc. Otherwise, we'd likely be
  // replacing casts with a libcall. We also must be allowed to ignore -0.0
  // because FTRUNC will return -0.0 for (-1.0, -0.0), but using integer
  // conversions would return +0.0.
  // FIXME: We should be able to use node-level FMF here.
  // TODO: If strict math, should we use FABS (+ range check for signed cast)?
  EVT VT = N->getValueType(0);
  if (!TLI.isOperationLegal(ISD::FTRUNC, VT) ||
      !DAG.getTarget().Options.NoSignedZerosFPMath)
    return SDValue();

  // fptosi/fptoui round towards zero, so converting from FP to integer and
  // back is the same as an 'ftrunc': [us]itofp (fpto[us]i X) --> ftrunc X
  SDValue N0 = N->getOperand(0);
  if (N->getOpcode() == ISD::SINT_TO_FP && N0.getOpcode() == ISD::FP_TO_SINT &&
      N0.getOperand(0).getValueType() == VT)
    return DAG.getNode(ISD::FTRUNC, SDLoc(N), VT, N0.getOperand(0));

  if (N->getOpcode() == ISD::UINT_TO_FP && N0.getOpcode() == ISD::FP_TO_UINT &&
      N0.getOperand(0).getValueType() == VT)
    return DAG.getNode(ISD::FTRUNC, SDLoc(N), VT, N0.getOperand(0));

  return SDValue();
}

SDValue DAGCombiner::visitSINT_TO_FP(SDNode *N) {
  SDValue N0 = N->getOperand(0);
  EVT VT = N->getValueType(0);
  EVT OpVT = N0.getValueType();

  // fold (sint_to_fp c1) -> c1fp
  if (DAG.isConstantIntBuildVectorOrConstantInt(N0) &&
      // ...but only if the target supports immediate floating-point values
      (!LegalOperations ||
       TLI.isOperationLegalOrCustom(ISD::ConstantFP, VT)))
    return DAG.getNode(ISD::SINT_TO_FP, SDLoc(N), VT, N0);

  // If the input is a legal type, and SINT_TO_FP is not legal on this target,
  // but UINT_TO_FP is legal on this target, try to convert.
  if (!hasOperation(ISD::SINT_TO_FP, OpVT) &&
      hasOperation(ISD::UINT_TO_FP, OpVT)) {
    // If the sign bit is known to be zero, we can change this to UINT_TO_FP.
    if (DAG.SignBitIsZero(N0))
      return DAG.getNode(ISD::UINT_TO_FP, SDLoc(N), VT, N0);
  }

  // The next optimizations are desirable only if SELECT_CC can be lowered.
  if (TLI.isOperationLegalOrCustom(ISD::SELECT_CC, VT) || !LegalOperations) {
    // fold (sint_to_fp (setcc x, y, cc)) -> (select_cc x, y, -1.0, 0.0,, cc)
    if (N0.getOpcode() == ISD::SETCC && N0.getValueType() == MVT::i1 &&
        !VT.isVector() &&
        (!LegalOperations ||
         TLI.isOperationLegalOrCustom(ISD::ConstantFP, VT))) {
      SDLoc DL(N);
      SDValue Ops[] =
        { N0.getOperand(0), N0.getOperand(1),
          DAG.getConstantFP(-1.0, DL, VT), DAG.getConstantFP(0.0, DL, VT),
          N0.getOperand(2) };
      return DAG.getNode(ISD::SELECT_CC, DL, VT, Ops);
    }

    // fold (sint_to_fp (zext (setcc x, y, cc))) ->
    //      (select_cc x, y, 1.0, 0.0,, cc)
    if (N0.getOpcode() == ISD::ZERO_EXTEND &&
        N0.getOperand(0).getOpcode() == ISD::SETCC &&!VT.isVector() &&
        (!LegalOperations ||
         TLI.isOperationLegalOrCustom(ISD::ConstantFP, VT))) {
      SDLoc DL(N);
      SDValue Ops[] =
        { N0.getOperand(0).getOperand(0), N0.getOperand(0).getOperand(1),
          DAG.getConstantFP(1.0, DL, VT), DAG.getConstantFP(0.0, DL, VT),
          N0.getOperand(0).getOperand(2) };
      return DAG.getNode(ISD::SELECT_CC, DL, VT, Ops);
    }
  }

  if (SDValue FTrunc = foldFPToIntToFP(N, DAG, TLI))
    return FTrunc;

  return SDValue();
}

SDValue DAGCombiner::visitUINT_TO_FP(SDNode *N) {
  SDValue N0 = N->getOperand(0);
  EVT VT = N->getValueType(0);
  EVT OpVT = N0.getValueType();

  // fold (uint_to_fp c1) -> c1fp
  if (DAG.isConstantIntBuildVectorOrConstantInt(N0) &&
      // ...but only if the target supports immediate floating-point values
      (!LegalOperations ||
       TLI.isOperationLegalOrCustom(ISD::ConstantFP, VT)))
    return DAG.getNode(ISD::UINT_TO_FP, SDLoc(N), VT, N0);

  // If the input is a legal type, and UINT_TO_FP is not legal on this target,
  // but SINT_TO_FP is legal on this target, try to convert.
  if (!hasOperation(ISD::UINT_TO_FP, OpVT) &&
      hasOperation(ISD::SINT_TO_FP, OpVT)) {
    // If the sign bit is known to be zero, we can change this to SINT_TO_FP.
    if (DAG.SignBitIsZero(N0))
      return DAG.getNode(ISD::SINT_TO_FP, SDLoc(N), VT, N0);
  }

  // The next optimizations are desirable only if SELECT_CC can be lowered.
  if (TLI.isOperationLegalOrCustom(ISD::SELECT_CC, VT) || !LegalOperations) {
    // fold (uint_to_fp (setcc x, y, cc)) -> (select_cc x, y, -1.0, 0.0,, cc)
    if (N0.getOpcode() == ISD::SETCC && !VT.isVector() &&
        (!LegalOperations ||
         TLI.isOperationLegalOrCustom(ISD::ConstantFP, VT))) {
      SDLoc DL(N);
      SDValue Ops[] =
        { N0.getOperand(0), N0.getOperand(1),
          DAG.getConstantFP(1.0, DL, VT), DAG.getConstantFP(0.0, DL, VT),
          N0.getOperand(2) };
      return DAG.getNode(ISD::SELECT_CC, DL, VT, Ops);
    }
  }

  if (SDValue FTrunc = foldFPToIntToFP(N, DAG, TLI))
    return FTrunc;

  return SDValue();
}

// Fold (fp_to_{s/u}int ({s/u}int_to_fpx)) -> zext x, sext x, trunc x, or x
static SDValue FoldIntToFPToInt(SDNode *N, SelectionDAG &DAG) {
  SDValue N0 = N->getOperand(0);
  EVT VT = N->getValueType(0);

  if (N0.getOpcode() != ISD::UINT_TO_FP && N0.getOpcode() != ISD::SINT_TO_FP)
    return SDValue();

  SDValue Src = N0.getOperand(0);
  EVT SrcVT = Src.getValueType();
  bool IsInputSigned = N0.getOpcode() == ISD::SINT_TO_FP;
  bool IsOutputSigned = N->getOpcode() == ISD::FP_TO_SINT;

  // We can safely assume the conversion won't overflow the output range,
  // because (for example) (uint8_t)18293.f is undefined behavior.

  // Since we can assume the conversion won't overflow, our decision as to
  // whether the input will fit in the float should depend on the minimum
  // of the input range and output range.

  // This means this is also safe for a signed input and unsigned output, since
  // a negative input would lead to undefined behavior.
  unsigned InputSize = (int)SrcVT.getScalarSizeInBits() - IsInputSigned;
  unsigned OutputSize = (int)VT.getScalarSizeInBits() - IsOutputSigned;
  unsigned ActualSize = std::min(InputSize, OutputSize);
  const fltSemantics &sem = DAG.EVTToAPFloatSemantics(N0.getValueType());

  // We can only fold away the float conversion if the input range can be
  // represented exactly in the float range.
  if (APFloat::semanticsPrecision(sem) >= ActualSize) {
    if (VT.getScalarSizeInBits() > SrcVT.getScalarSizeInBits()) {
      unsigned ExtOp = IsInputSigned && IsOutputSigned ? ISD::SIGN_EXTEND
                                                       : ISD::ZERO_EXTEND;
      return DAG.getNode(ExtOp, SDLoc(N), VT, Src);
    }
    if (VT.getScalarSizeInBits() < SrcVT.getScalarSizeInBits())
      return DAG.getNode(ISD::TRUNCATE, SDLoc(N), VT, Src);
    return DAG.getBitcast(VT, Src);
  }
  return SDValue();
}

SDValue DAGCombiner::visitFP_TO_SINT(SDNode *N) {
  SDValue N0 = N->getOperand(0);
  EVT VT = N->getValueType(0);

  // fold (fp_to_sint c1fp) -> c1
  if (isConstantFPBuildVectorOrConstantFP(N0))
    return DAG.getNode(ISD::FP_TO_SINT, SDLoc(N), VT, N0);

  return FoldIntToFPToInt(N, DAG);
}

SDValue DAGCombiner::visitFP_TO_UINT(SDNode *N) {
  SDValue N0 = N->getOperand(0);
  EVT VT = N->getValueType(0);

  // fold (fp_to_uint c1fp) -> c1
  if (isConstantFPBuildVectorOrConstantFP(N0))
    return DAG.getNode(ISD::FP_TO_UINT, SDLoc(N), VT, N0);

  return FoldIntToFPToInt(N, DAG);
}

SDValue DAGCombiner::visitFP_ROUND(SDNode *N) {
  SDValue N0 = N->getOperand(0);
  SDValue N1 = N->getOperand(1);
  ConstantFPSDNode *N0CFP = dyn_cast<ConstantFPSDNode>(N0);
  EVT VT = N->getValueType(0);

  // fold (fp_round c1fp) -> c1fp
  if (N0CFP)
    return DAG.getNode(ISD::FP_ROUND, SDLoc(N), VT, N0, N1);

  // fold (fp_round (fp_extend x)) -> x
  if (N0.getOpcode() == ISD::FP_EXTEND && VT == N0.getOperand(0).getValueType())
    return N0.getOperand(0);

  // fold (fp_round (fp_round x)) -> (fp_round x)
  if (N0.getOpcode() == ISD::FP_ROUND) {
    const bool NIsTrunc = N->getConstantOperandVal(1) == 1;
    const bool N0IsTrunc = N0.getConstantOperandVal(1) == 1;

    // Skip this folding if it results in an fp_round from f80 to f16.
    //
    // f80 to f16 always generates an expensive (and as yet, unimplemented)
    // libcall to __truncxfhf2 instead of selecting native f16 conversion
    // instructions from f32 or f64.  Moreover, the first (value-preserving)
    // fp_round from f80 to either f32 or f64 may become a NOP in platforms like
    // x86.
    if (N0.getOperand(0).getValueType() == MVT::f80 && VT == MVT::f16)
      return SDValue();

    // If the first fp_round isn't a value preserving truncation, it might
    // introduce a tie in the second fp_round, that wouldn't occur in the
    // single-step fp_round we want to fold to.
    // In other words, double rounding isn't the same as rounding.
    // Also, this is a value preserving truncation iff both fp_round's are.
    if (DAG.getTarget().Options.UnsafeFPMath || N0IsTrunc) {
      SDLoc DL(N);
      return DAG.getNode(ISD::FP_ROUND, DL, VT, N0.getOperand(0),
                         DAG.getIntPtrConstant(NIsTrunc && N0IsTrunc, DL));
    }
  }

  // fold (fp_round (copysign X, Y)) -> (copysign (fp_round X), Y)
  if (N0.getOpcode() == ISD::FCOPYSIGN && N0.getNode()->hasOneUse()) {
    SDValue Tmp = DAG.getNode(ISD::FP_ROUND, SDLoc(N0), VT,
                              N0.getOperand(0), N1);
    AddToWorklist(Tmp.getNode());
    return DAG.getNode(ISD::FCOPYSIGN, SDLoc(N), VT,
                       Tmp, N0.getOperand(1));
  }

  if (SDValue NewVSel = matchVSelectOpSizesWithSetCC(N))
    return NewVSel;

  return SDValue();
}

SDValue DAGCombiner::visitFP_ROUND_INREG(SDNode *N) {
  SDValue N0 = N->getOperand(0);
  EVT VT = N->getValueType(0);
  EVT EVT = cast<VTSDNode>(N->getOperand(1))->getVT();
  ConstantFPSDNode *N0CFP = dyn_cast<ConstantFPSDNode>(N0);

  // fold (fp_round_inreg c1fp) -> c1fp
  if (N0CFP && isTypeLegal(EVT)) {
    SDLoc DL(N);
    SDValue Round = DAG.getConstantFP(*N0CFP->getConstantFPValue(), DL, EVT);
    return DAG.getNode(ISD::FP_EXTEND, DL, VT, Round);
  }

  return SDValue();
}

SDValue DAGCombiner::visitFP_EXTEND(SDNode *N) {
  SDValue N0 = N->getOperand(0);
  EVT VT = N->getValueType(0);

  // If this is fp_round(fpextend), don't fold it, allow ourselves to be folded.
  if (N->hasOneUse() &&
      N->use_begin()->getOpcode() == ISD::FP_ROUND)
    return SDValue();

  // fold (fp_extend c1fp) -> c1fp
  if (isConstantFPBuildVectorOrConstantFP(N0))
    return DAG.getNode(ISD::FP_EXTEND, SDLoc(N), VT, N0);

  // fold (fp_extend (fp16_to_fp op)) -> (fp16_to_fp op)
  if (N0.getOpcode() == ISD::FP16_TO_FP &&
      TLI.getOperationAction(ISD::FP16_TO_FP, VT) == TargetLowering::Legal)
    return DAG.getNode(ISD::FP16_TO_FP, SDLoc(N), VT, N0.getOperand(0));

  // Turn fp_extend(fp_round(X, 1)) -> x since the fp_round doesn't affect the
  // value of X.
  if (N0.getOpcode() == ISD::FP_ROUND
      && N0.getConstantOperandVal(1) == 1) {
    SDValue In = N0.getOperand(0);
    if (In.getValueType() == VT) return In;
    if (VT.bitsLT(In.getValueType()))
      return DAG.getNode(ISD::FP_ROUND, SDLoc(N), VT,
                         In, N0.getOperand(1));
    return DAG.getNode(ISD::FP_EXTEND, SDLoc(N), VT, In);
  }

  // fold (fpext (load x)) -> (fpext (fptrunc (extload x)))
  if (ISD::isNormalLoad(N0.getNode()) && N0.hasOneUse() &&
       TLI.isLoadExtLegal(ISD::EXTLOAD, VT, N0.getValueType())) {
    LoadSDNode *LN0 = cast<LoadSDNode>(N0);
    SDValue ExtLoad = DAG.getExtLoad(ISD::EXTLOAD, SDLoc(N), VT,
                                     LN0->getChain(),
                                     LN0->getBasePtr(), N0.getValueType(),
                                     LN0->getMemOperand());
    CombineTo(N, ExtLoad);
    CombineTo(N0.getNode(),
              DAG.getNode(ISD::FP_ROUND, SDLoc(N0),
                          N0.getValueType(), ExtLoad,
                          DAG.getIntPtrConstant(1, SDLoc(N0))),
              ExtLoad.getValue(1));
    return SDValue(N, 0);   // Return N so it doesn't get rechecked!
  }

  if (SDValue NewVSel = matchVSelectOpSizesWithSetCC(N))
    return NewVSel;

  return SDValue();
}

SDValue DAGCombiner::visitFCEIL(SDNode *N) {
  SDValue N0 = N->getOperand(0);
  EVT VT = N->getValueType(0);

  // fold (fceil c1) -> fceil(c1)
  if (isConstantFPBuildVectorOrConstantFP(N0))
    return DAG.getNode(ISD::FCEIL, SDLoc(N), VT, N0);

  return SDValue();
}

SDValue DAGCombiner::visitFTRUNC(SDNode *N) {
  SDValue N0 = N->getOperand(0);
  EVT VT = N->getValueType(0);

  // fold (ftrunc c1) -> ftrunc(c1)
  if (isConstantFPBuildVectorOrConstantFP(N0))
    return DAG.getNode(ISD::FTRUNC, SDLoc(N), VT, N0);

  // fold ftrunc (known rounded int x) -> x
  // ftrunc is a part of fptosi/fptoui expansion on some targets, so this is
  // likely to be generated to extract integer from a rounded floating value.
  switch (N0.getOpcode()) {
  default: break;
  case ISD::FRINT:
  case ISD::FTRUNC:
  case ISD::FNEARBYINT:
  case ISD::FFLOOR:
  case ISD::FCEIL:
    return N0;
  }

  return SDValue();
}

SDValue DAGCombiner::visitFFLOOR(SDNode *N) {
  SDValue N0 = N->getOperand(0);
  EVT VT = N->getValueType(0);

  // fold (ffloor c1) -> ffloor(c1)
  if (isConstantFPBuildVectorOrConstantFP(N0))
    return DAG.getNode(ISD::FFLOOR, SDLoc(N), VT, N0);

  return SDValue();
}

// FIXME: FNEG and FABS have a lot in common; refactor.
SDValue DAGCombiner::visitFNEG(SDNode *N) {
  SDValue N0 = N->getOperand(0);
  EVT VT = N->getValueType(0);

  // Constant fold FNEG.
  if (isConstantFPBuildVectorOrConstantFP(N0))
    return DAG.getNode(ISD::FNEG, SDLoc(N), VT, N0);

  if (isNegatibleForFree(N0, LegalOperations, DAG.getTargetLoweringInfo(),
                         &DAG.getTarget().Options))
    return GetNegatedExpression(N0, DAG, LegalOperations);

  // Transform fneg(bitconvert(x)) -> bitconvert(x ^ sign) to avoid loading
  // constant pool values.
  if (!TLI.isFNegFree(VT) &&
      N0.getOpcode() == ISD::BITCAST &&
      N0.getNode()->hasOneUse()) {
    SDValue Int = N0.getOperand(0);
    EVT IntVT = Int.getValueType();
    if (IntVT.isInteger() && !IntVT.isVector()) {
      APInt SignMask;
      if (N0.getValueType().isVector()) {
        // For a vector, get a mask such as 0x80... per scalar element
        // and splat it.
        SignMask = APInt::getSignMask(N0.getScalarValueSizeInBits());
        SignMask = APInt::getSplat(IntVT.getSizeInBits(), SignMask);
      } else {
        // For a scalar, just generate 0x80...
        SignMask = APInt::getSignMask(IntVT.getSizeInBits());
      }
      SDLoc DL0(N0);
      Int = DAG.getNode(ISD::XOR, DL0, IntVT, Int,
                        DAG.getConstant(SignMask, DL0, IntVT));
      AddToWorklist(Int.getNode());
      return DAG.getBitcast(VT, Int);
    }
  }

  // (fneg (fmul c, x)) -> (fmul -c, x)
  if (N0.getOpcode() == ISD::FMUL &&
      (N0.getNode()->hasOneUse() || !TLI.isFNegFree(VT))) {
    ConstantFPSDNode *CFP1 = dyn_cast<ConstantFPSDNode>(N0.getOperand(1));
    if (CFP1) {
      APFloat CVal = CFP1->getValueAPF();
      CVal.changeSign();
      if (Level >= AfterLegalizeDAG &&
          (TLI.isFPImmLegal(CVal, VT) ||
           TLI.isOperationLegal(ISD::ConstantFP, VT)))
        return DAG.getNode(
            ISD::FMUL, SDLoc(N), VT, N0.getOperand(0),
            DAG.getNode(ISD::FNEG, SDLoc(N), VT, N0.getOperand(1)),
            N0->getFlags());
    }
  }

  return SDValue();
}

static SDValue visitFMinMax(SelectionDAG &DAG, SDNode *N,
                            APFloat (*Op)(const APFloat &, const APFloat &)) {
  SDValue N0 = N->getOperand(0);
  SDValue N1 = N->getOperand(1);
  EVT VT = N->getValueType(0);
  const ConstantFPSDNode *N0CFP = isConstOrConstSplatFP(N0);
  const ConstantFPSDNode *N1CFP = isConstOrConstSplatFP(N1);

  if (N0CFP && N1CFP) {
    const APFloat &C0 = N0CFP->getValueAPF();
    const APFloat &C1 = N1CFP->getValueAPF();
    return DAG.getConstantFP(Op(C0, C1), SDLoc(N), VT);
  }

  // Canonicalize to constant on RHS.
  if (isConstantFPBuildVectorOrConstantFP(N0) &&
      !isConstantFPBuildVectorOrConstantFP(N1))
    return DAG.getNode(N->getOpcode(), SDLoc(N), VT, N1, N0);

  return SDValue();
}

SDValue DAGCombiner::visitFMINNUM(SDNode *N) {
  return visitFMinMax(DAG, N, minnum);
}

SDValue DAGCombiner::visitFMAXNUM(SDNode *N) {
  return visitFMinMax(DAG, N, maxnum);
}

SDValue DAGCombiner::visitFMINIMUM(SDNode *N) {
  return visitFMinMax(DAG, N, minimum);
}

SDValue DAGCombiner::visitFMAXIMUM(SDNode *N) {
  return visitFMinMax(DAG, N, maximum);
}

SDValue DAGCombiner::visitFABS(SDNode *N) {
  SDValue N0 = N->getOperand(0);
  EVT VT = N->getValueType(0);

  // fold (fabs c1) -> fabs(c1)
  if (isConstantFPBuildVectorOrConstantFP(N0))
    return DAG.getNode(ISD::FABS, SDLoc(N), VT, N0);

  // fold (fabs (fabs x)) -> (fabs x)
  if (N0.getOpcode() == ISD::FABS)
    return N->getOperand(0);

  // fold (fabs (fneg x)) -> (fabs x)
  // fold (fabs (fcopysign x, y)) -> (fabs x)
  if (N0.getOpcode() == ISD::FNEG || N0.getOpcode() == ISD::FCOPYSIGN)
    return DAG.getNode(ISD::FABS, SDLoc(N), VT, N0.getOperand(0));

  // fabs(bitcast(x)) -> bitcast(x & ~sign) to avoid constant pool loads.
  if (!TLI.isFAbsFree(VT) && N0.getOpcode() == ISD::BITCAST && N0.hasOneUse()) {
    SDValue Int = N0.getOperand(0);
    EVT IntVT = Int.getValueType();
    if (IntVT.isInteger() && !IntVT.isVector()) {
      APInt SignMask;
      if (N0.getValueType().isVector()) {
        // For a vector, get a mask such as 0x7f... per scalar element
        // and splat it.
        SignMask = ~APInt::getSignMask(N0.getScalarValueSizeInBits());
        SignMask = APInt::getSplat(IntVT.getSizeInBits(), SignMask);
      } else {
        // For a scalar, just generate 0x7f...
        SignMask = ~APInt::getSignMask(IntVT.getSizeInBits());
      }
      SDLoc DL(N0);
      Int = DAG.getNode(ISD::AND, DL, IntVT, Int,
                        DAG.getConstant(SignMask, DL, IntVT));
      AddToWorklist(Int.getNode());
      return DAG.getBitcast(N->getValueType(0), Int);
    }
  }

  return SDValue();
}

SDValue DAGCombiner::visitBRCOND(SDNode *N) {
  SDValue Chain = N->getOperand(0);
  SDValue N1 = N->getOperand(1);
  SDValue N2 = N->getOperand(2);

  // If N is a constant we could fold this into a fallthrough or unconditional
  // branch. However that doesn't happen very often in normal code, because
  // Instcombine/SimplifyCFG should have handled the available opportunities.
  // If we did this folding here, it would be necessary to update the
  // MachineBasicBlock CFG, which is awkward.

  // fold a brcond with a setcc condition into a BR_CC node if BR_CC is legal
  // on the target.
  if (N1.getOpcode() == ISD::SETCC &&
      TLI.isOperationLegalOrCustom(ISD::BR_CC,
                                   N1.getOperand(0).getValueType())) {
    return DAG.getNode(ISD::BR_CC, SDLoc(N), MVT::Other,
                       Chain, N1.getOperand(2),
                       N1.getOperand(0), N1.getOperand(1), N2);
  }

  if (N1.hasOneUse()) {
    if (SDValue NewN1 = rebuildSetCC(N1))
      return DAG.getNode(ISD::BRCOND, SDLoc(N), MVT::Other, Chain, NewN1, N2);
  }

  return SDValue();
}

SDValue DAGCombiner::rebuildSetCC(SDValue N) {
  if (N.getOpcode() == ISD::SRL ||
      (N.getOpcode() == ISD::TRUNCATE &&
       (N.getOperand(0).hasOneUse() &&
        N.getOperand(0).getOpcode() == ISD::SRL))) {
    // Look pass the truncate.
    if (N.getOpcode() == ISD::TRUNCATE)
      N = N.getOperand(0);

    // Match this pattern so that we can generate simpler code:
    //
    //   %a = ...
    //   %b = and i32 %a, 2
    //   %c = srl i32 %b, 1
    //   brcond i32 %c ...
    //
    // into
    //
    //   %a = ...
    //   %b = and i32 %a, 2
    //   %c = setcc eq %b, 0
    //   brcond %c ...
    //
    // This applies only when the AND constant value has one bit set and the
    // SRL constant is equal to the log2 of the AND constant. The back-end is
    // smart enough to convert the result into a TEST/JMP sequence.
    SDValue Op0 = N.getOperand(0);
    SDValue Op1 = N.getOperand(1);

    if (Op0.getOpcode() == ISD::AND && Op1.getOpcode() == ISD::Constant) {
      SDValue AndOp1 = Op0.getOperand(1);

      if (AndOp1.getOpcode() == ISD::Constant) {
        const APInt &AndConst = cast<ConstantSDNode>(AndOp1)->getAPIntValue();

        if (AndConst.isPowerOf2() &&
            cast<ConstantSDNode>(Op1)->getAPIntValue() == AndConst.logBase2()) {
          SDLoc DL(N);
          return DAG.getSetCC(DL, getSetCCResultType(Op0.getValueType()),
                              Op0, DAG.getConstant(0, DL, Op0.getValueType()),
                              ISD::SETNE);
        }
      }
    }
  }

  // Transform br(xor(x, y)) -> br(x != y)
  // Transform br(xor(xor(x,y), 1)) -> br (x == y)
  if (N.getOpcode() == ISD::XOR) {
    // Because we may call this on a speculatively constructed
    // SimplifiedSetCC Node, we need to simplify this node first.
    // Ideally this should be folded into SimplifySetCC and not
    // here. For now, grab a handle to N so we don't lose it from
    // replacements interal to the visit.
    HandleSDNode XORHandle(N);
    while (N.getOpcode() == ISD::XOR) {
      SDValue Tmp = visitXOR(N.getNode());
      // No simplification done.
      if (!Tmp.getNode())
        break;
      // Returning N is form in-visit replacement that may invalidated
      // N. Grab value from Handle.
      if (Tmp.getNode() == N.getNode())
        N = XORHandle.getValue();
      else // Node simplified. Try simplifying again.
        N = Tmp;
    }

    if (N.getOpcode() != ISD::XOR)
      return N;

    SDNode *TheXor = N.getNode();

    SDValue Op0 = TheXor->getOperand(0);
    SDValue Op1 = TheXor->getOperand(1);

    if (Op0.getOpcode() != ISD::SETCC && Op1.getOpcode() != ISD::SETCC) {
      bool Equal = false;
      if (isOneConstant(Op0) && Op0.hasOneUse() &&
          Op0.getOpcode() == ISD::XOR) {
        TheXor = Op0.getNode();
        Equal = true;
      }

      EVT SetCCVT = N.getValueType();
      if (LegalTypes)
        SetCCVT = getSetCCResultType(SetCCVT);
      // Replace the uses of XOR with SETCC
      return DAG.getSetCC(SDLoc(TheXor), SetCCVT, Op0, Op1,
                          Equal ? ISD::SETEQ : ISD::SETNE);
    }
  }

  return SDValue();
}

// Operand List for BR_CC: Chain, CondCC, CondLHS, CondRHS, DestBB.
//
SDValue DAGCombiner::visitBR_CC(SDNode *N) {
  CondCodeSDNode *CC = cast<CondCodeSDNode>(N->getOperand(1));
  SDValue CondLHS = N->getOperand(2), CondRHS = N->getOperand(3);

  // If N is a constant we could fold this into a fallthrough or unconditional
  // branch. However that doesn't happen very often in normal code, because
  // Instcombine/SimplifyCFG should have handled the available opportunities.
  // If we did this folding here, it would be necessary to update the
  // MachineBasicBlock CFG, which is awkward.

  // Use SimplifySetCC to simplify SETCC's.
  SDValue Simp = SimplifySetCC(getSetCCResultType(CondLHS.getValueType()),
                               CondLHS, CondRHS, CC->get(), SDLoc(N),
                               false);
  if (Simp.getNode()) AddToWorklist(Simp.getNode());

  // fold to a simpler setcc
  if (Simp.getNode() && Simp.getOpcode() == ISD::SETCC)
    return DAG.getNode(ISD::BR_CC, SDLoc(N), MVT::Other,
                       N->getOperand(0), Simp.getOperand(2),
                       Simp.getOperand(0), Simp.getOperand(1),
                       N->getOperand(4));

  return SDValue();
}

/// Return true if 'Use' is a load or a store that uses N as its base pointer
/// and that N may be folded in the load / store addressing mode.
static bool canFoldInAddressingMode(SDNode *N, SDNode *Use,
                                    SelectionDAG &DAG,
                                    const TargetLowering &TLI) {
  EVT VT;
  unsigned AS;

  if (LoadSDNode *LD  = dyn_cast<LoadSDNode>(Use)) {
    if (LD->isIndexed() || LD->getBasePtr().getNode() != N)
      return false;
    VT = LD->getMemoryVT();
    AS = LD->getAddressSpace();
  } else if (StoreSDNode *ST  = dyn_cast<StoreSDNode>(Use)) {
    if (ST->isIndexed() || ST->getBasePtr().getNode() != N)
      return false;
    VT = ST->getMemoryVT();
    AS = ST->getAddressSpace();
  } else
    return false;

  TargetLowering::AddrMode AM;
  if (N->getOpcode() == ISD::ADD) {
    ConstantSDNode *Offset = dyn_cast<ConstantSDNode>(N->getOperand(1));
    if (Offset)
      // [reg +/- imm]
      AM.BaseOffs = Offset->getSExtValue();
    else
      // [reg +/- reg]
      AM.Scale = 1;
  } else if (N->getOpcode() == ISD::SUB) {
    ConstantSDNode *Offset = dyn_cast<ConstantSDNode>(N->getOperand(1));
    if (Offset)
      // [reg +/- imm]
      AM.BaseOffs = -Offset->getSExtValue();
    else
      // [reg +/- reg]
      AM.Scale = 1;
  } else
    return false;

  return TLI.isLegalAddressingMode(DAG.getDataLayout(), AM,
                                   VT.getTypeForEVT(*DAG.getContext()), AS);
}

/// Try turning a load/store into a pre-indexed load/store when the base
/// pointer is an add or subtract and it has other uses besides the load/store.
/// After the transformation, the new indexed load/store has effectively folded
/// the add/subtract in and all of its other uses are redirected to the
/// new load/store.
bool DAGCombiner::CombineToPreIndexedLoadStore(SDNode *N) {
  if (Level < AfterLegalizeDAG)
    return false;

  bool isLoad = true;
  SDValue Ptr;
  EVT VT;
  if (LoadSDNode *LD  = dyn_cast<LoadSDNode>(N)) {
    if (LD->isIndexed())
      return false;
    VT = LD->getMemoryVT();
    if (!TLI.isIndexedLoadLegal(ISD::PRE_INC, VT) &&
        !TLI.isIndexedLoadLegal(ISD::PRE_DEC, VT))
      return false;
    Ptr = LD->getBasePtr();
  } else if (StoreSDNode *ST  = dyn_cast<StoreSDNode>(N)) {
    if (ST->isIndexed())
      return false;
    VT = ST->getMemoryVT();
    if (!TLI.isIndexedStoreLegal(ISD::PRE_INC, VT) &&
        !TLI.isIndexedStoreLegal(ISD::PRE_DEC, VT))
      return false;
    Ptr = ST->getBasePtr();
    isLoad = false;
  } else {
    return false;
  }

  // If the pointer is not an add/sub, or if it doesn't have multiple uses, bail
  // out.  There is no reason to make this a preinc/predec.
  if ((Ptr.getOpcode() != ISD::ADD && Ptr.getOpcode() != ISD::SUB) ||
      Ptr.getNode()->hasOneUse())
    return false;

  // Ask the target to do addressing mode selection.
  SDValue BasePtr;
  SDValue Offset;
  ISD::MemIndexedMode AM = ISD::UNINDEXED;
  if (!TLI.getPreIndexedAddressParts(N, BasePtr, Offset, AM, DAG))
    return false;

  // Backends without true r+i pre-indexed forms may need to pass a
  // constant base with a variable offset so that constant coercion
  // will work with the patterns in canonical form.
  bool Swapped = false;
  if (isa<ConstantSDNode>(BasePtr)) {
    std::swap(BasePtr, Offset);
    Swapped = true;
  }

  // Don't create a indexed load / store with zero offset.
  if (isNullConstant(Offset))
    return false;

  // Try turning it into a pre-indexed load / store except when:
  // 1) The new base ptr is a frame index.
  // 2) If N is a store and the new base ptr is either the same as or is a
  //    predecessor of the value being stored.
  // 3) Another use of old base ptr is a predecessor of N. If ptr is folded
  //    that would create a cycle.
  // 4) All uses are load / store ops that use it as old base ptr.

  // Check #1.  Preinc'ing a frame index would require copying the stack pointer
  // (plus the implicit offset) to a register to preinc anyway.
  if (isa<FrameIndexSDNode>(BasePtr) || isa<RegisterSDNode>(BasePtr))
    return false;

  // Check #2.
  if (!isLoad) {
    SDValue Val = cast<StoreSDNode>(N)->getValue();
    if (Val == BasePtr || BasePtr.getNode()->isPredecessorOf(Val.getNode()))
      return false;
  }

  // Caches for hasPredecessorHelper.
  SmallPtrSet<const SDNode *, 32> Visited;
  SmallVector<const SDNode *, 16> Worklist;
  Worklist.push_back(N);

  // If the offset is a constant, there may be other adds of constants that
  // can be folded with this one. We should do this to avoid having to keep
  // a copy of the original base pointer.
  SmallVector<SDNode *, 16> OtherUses;
  if (isa<ConstantSDNode>(Offset))
    for (SDNode::use_iterator UI = BasePtr.getNode()->use_begin(),
                              UE = BasePtr.getNode()->use_end();
         UI != UE; ++UI) {
      SDUse &Use = UI.getUse();
      // Skip the use that is Ptr and uses of other results from BasePtr's
      // node (important for nodes that return multiple results).
      if (Use.getUser() == Ptr.getNode() || Use != BasePtr)
        continue;

      if (SDNode::hasPredecessorHelper(Use.getUser(), Visited, Worklist))
        continue;

      if (Use.getUser()->getOpcode() != ISD::ADD &&
          Use.getUser()->getOpcode() != ISD::SUB) {
        OtherUses.clear();
        break;
      }

      SDValue Op1 = Use.getUser()->getOperand((UI.getOperandNo() + 1) & 1);
      if (!isa<ConstantSDNode>(Op1)) {
        OtherUses.clear();
        break;
      }

      // FIXME: In some cases, we can be smarter about this.
      if (Op1.getValueType() != Offset.getValueType()) {
        OtherUses.clear();
        break;
      }

      OtherUses.push_back(Use.getUser());
    }

  if (Swapped)
    std::swap(BasePtr, Offset);

  // Now check for #3 and #4.
  bool RealUse = false;

  for (SDNode *Use : Ptr.getNode()->uses()) {
    if (Use == N)
      continue;
    if (SDNode::hasPredecessorHelper(Use, Visited, Worklist))
      return false;

    // If Ptr may be folded in addressing mode of other use, then it's
    // not profitable to do this transformation.
    if (!canFoldInAddressingMode(Ptr.getNode(), Use, DAG, TLI))
      RealUse = true;
  }

  if (!RealUse)
    return false;

  SDValue Result;
  if (isLoad)
    Result = DAG.getIndexedLoad(SDValue(N,0), SDLoc(N),
                                BasePtr, Offset, AM);
  else
    Result = DAG.getIndexedStore(SDValue(N,0), SDLoc(N),
                                 BasePtr, Offset, AM);
  ++PreIndexedNodes;
  ++NodesCombined;
  LLVM_DEBUG(dbgs() << "\nReplacing.4 "; N->dump(&DAG); dbgs() << "\nWith: ";
             Result.getNode()->dump(&DAG); dbgs() << '\n');
  WorklistRemover DeadNodes(*this);
  if (isLoad) {
    DAG.ReplaceAllUsesOfValueWith(SDValue(N, 0), Result.getValue(0));
    DAG.ReplaceAllUsesOfValueWith(SDValue(N, 1), Result.getValue(2));
  } else {
    DAG.ReplaceAllUsesOfValueWith(SDValue(N, 0), Result.getValue(1));
  }

  // Finally, since the node is now dead, remove it from the graph.
  deleteAndRecombine(N);

  if (Swapped)
    std::swap(BasePtr, Offset);

  // Replace other uses of BasePtr that can be updated to use Ptr
  for (unsigned i = 0, e = OtherUses.size(); i != e; ++i) {
    unsigned OffsetIdx = 1;
    if (OtherUses[i]->getOperand(OffsetIdx).getNode() == BasePtr.getNode())
      OffsetIdx = 0;
    assert(OtherUses[i]->getOperand(!OffsetIdx).getNode() ==
           BasePtr.getNode() && "Expected BasePtr operand");

    // We need to replace ptr0 in the following expression:
    //   x0 * offset0 + y0 * ptr0 = t0
    // knowing that
    //   x1 * offset1 + y1 * ptr0 = t1 (the indexed load/store)
    //
    // where x0, x1, y0 and y1 in {-1, 1} are given by the types of the
    // indexed load/store and the expression that needs to be re-written.
    //
    // Therefore, we have:
    //   t0 = (x0 * offset0 - x1 * y0 * y1 *offset1) + (y0 * y1) * t1

    ConstantSDNode *CN =
      cast<ConstantSDNode>(OtherUses[i]->getOperand(OffsetIdx));
    int X0, X1, Y0, Y1;
    const APInt &Offset0 = CN->getAPIntValue();
    APInt Offset1 = cast<ConstantSDNode>(Offset)->getAPIntValue();

    X0 = (OtherUses[i]->getOpcode() == ISD::SUB && OffsetIdx == 1) ? -1 : 1;
    Y0 = (OtherUses[i]->getOpcode() == ISD::SUB && OffsetIdx == 0) ? -1 : 1;
    X1 = (AM == ISD::PRE_DEC && !Swapped) ? -1 : 1;
    Y1 = (AM == ISD::PRE_DEC && Swapped) ? -1 : 1;

    unsigned Opcode = (Y0 * Y1 < 0) ? ISD::SUB : ISD::ADD;

    APInt CNV = Offset0;
    if (X0 < 0) CNV = -CNV;
    if (X1 * Y0 * Y1 < 0) CNV = CNV + Offset1;
    else CNV = CNV - Offset1;

    SDLoc DL(OtherUses[i]);

    // We can now generate the new expression.
    SDValue NewOp1 = DAG.getConstant(CNV, DL, CN->getValueType(0));
    SDValue NewOp2 = Result.getValue(isLoad ? 1 : 0);

    SDValue NewUse = DAG.getNode(Opcode,
                                 DL,
                                 OtherUses[i]->getValueType(0), NewOp1, NewOp2);
    DAG.ReplaceAllUsesOfValueWith(SDValue(OtherUses[i], 0), NewUse);
    deleteAndRecombine(OtherUses[i]);
  }

  // Replace the uses of Ptr with uses of the updated base value.
  DAG.ReplaceAllUsesOfValueWith(Ptr, Result.getValue(isLoad ? 1 : 0));
  deleteAndRecombine(Ptr.getNode());
  AddToWorklist(Result.getNode());

  return true;
}

/// Try to combine a load/store with a add/sub of the base pointer node into a
/// post-indexed load/store. The transformation folded the add/subtract into the
/// new indexed load/store effectively and all of its uses are redirected to the
/// new load/store.
bool DAGCombiner::CombineToPostIndexedLoadStore(SDNode *N) {
  if (Level < AfterLegalizeDAG)
    return false;

  bool isLoad = true;
  SDValue Ptr;
  EVT VT;
  if (LoadSDNode *LD  = dyn_cast<LoadSDNode>(N)) {
    if (LD->isIndexed())
      return false;
    VT = LD->getMemoryVT();
    if (!TLI.isIndexedLoadLegal(ISD::POST_INC, VT) &&
        !TLI.isIndexedLoadLegal(ISD::POST_DEC, VT))
      return false;
    Ptr = LD->getBasePtr();
  } else if (StoreSDNode *ST  = dyn_cast<StoreSDNode>(N)) {
    if (ST->isIndexed())
      return false;
    VT = ST->getMemoryVT();
    if (!TLI.isIndexedStoreLegal(ISD::POST_INC, VT) &&
        !TLI.isIndexedStoreLegal(ISD::POST_DEC, VT))
      return false;
    Ptr = ST->getBasePtr();
    isLoad = false;
  } else {
    return false;
  }

  if (Ptr.getNode()->hasOneUse())
    return false;

  for (SDNode *Op : Ptr.getNode()->uses()) {
    if (Op == N ||
        (Op->getOpcode() != ISD::ADD && Op->getOpcode() != ISD::SUB))
      continue;

    SDValue BasePtr;
    SDValue Offset;
    ISD::MemIndexedMode AM = ISD::UNINDEXED;
    if (TLI.getPostIndexedAddressParts(N, Op, BasePtr, Offset, AM, DAG)) {
      // Don't create a indexed load / store with zero offset.
      if (isNullConstant(Offset))
        continue;

      // Try turning it into a post-indexed load / store except when
      // 1) All uses are load / store ops that use it as base ptr (and
      //    it may be folded as addressing mmode).
      // 2) Op must be independent of N, i.e. Op is neither a predecessor
      //    nor a successor of N. Otherwise, if Op is folded that would
      //    create a cycle.

      if (isa<FrameIndexSDNode>(BasePtr) || isa<RegisterSDNode>(BasePtr))
        continue;

      // Check for #1.
      bool TryNext = false;
      for (SDNode *Use : BasePtr.getNode()->uses()) {
        if (Use == Ptr.getNode())
          continue;

        // If all the uses are load / store addresses, then don't do the
        // transformation.
        if (Use->getOpcode() == ISD::ADD || Use->getOpcode() == ISD::SUB){
          bool RealUse = false;
          for (SDNode *UseUse : Use->uses()) {
            if (!canFoldInAddressingMode(Use, UseUse, DAG, TLI))
              RealUse = true;
          }

          if (!RealUse) {
            TryNext = true;
            break;
          }
        }
      }

      if (TryNext)
        continue;

      // Check for #2.
      SmallPtrSet<const SDNode *, 32> Visited;
      SmallVector<const SDNode *, 8> Worklist;
      // Ptr is predecessor to both N and Op.
      Visited.insert(Ptr.getNode());
      Worklist.push_back(N);
      Worklist.push_back(Op);
      if (!SDNode::hasPredecessorHelper(N, Visited, Worklist) &&
          !SDNode::hasPredecessorHelper(Op, Visited, Worklist)) {
        SDValue Result = isLoad
          ? DAG.getIndexedLoad(SDValue(N,0), SDLoc(N),
                               BasePtr, Offset, AM)
          : DAG.getIndexedStore(SDValue(N,0), SDLoc(N),
                                BasePtr, Offset, AM);
        ++PostIndexedNodes;
        ++NodesCombined;
        LLVM_DEBUG(dbgs() << "\nReplacing.5 "; N->dump(&DAG);
                   dbgs() << "\nWith: "; Result.getNode()->dump(&DAG);
                   dbgs() << '\n');
        WorklistRemover DeadNodes(*this);
        if (isLoad) {
          DAG.ReplaceAllUsesOfValueWith(SDValue(N, 0), Result.getValue(0));
          DAG.ReplaceAllUsesOfValueWith(SDValue(N, 1), Result.getValue(2));
        } else {
          DAG.ReplaceAllUsesOfValueWith(SDValue(N, 0), Result.getValue(1));
        }

        // Finally, since the node is now dead, remove it from the graph.
        deleteAndRecombine(N);

        // Replace the uses of Use with uses of the updated base value.
        DAG.ReplaceAllUsesOfValueWith(SDValue(Op, 0),
                                      Result.getValue(isLoad ? 1 : 0));
        deleteAndRecombine(Op);
        return true;
      }
    }
  }

  return false;
}

/// Return the base-pointer arithmetic from an indexed \p LD.
SDValue DAGCombiner::SplitIndexingFromLoad(LoadSDNode *LD) {
  ISD::MemIndexedMode AM = LD->getAddressingMode();
  assert(AM != ISD::UNINDEXED);
  SDValue BP = LD->getOperand(1);
  SDValue Inc = LD->getOperand(2);

  // Some backends use TargetConstants for load offsets, but don't expect
  // TargetConstants in general ADD nodes. We can convert these constants into
  // regular Constants (if the constant is not opaque).
  assert((Inc.getOpcode() != ISD::TargetConstant ||
          !cast<ConstantSDNode>(Inc)->isOpaque()) &&
         "Cannot split out indexing using opaque target constants");
  if (Inc.getOpcode() == ISD::TargetConstant) {
    ConstantSDNode *ConstInc = cast<ConstantSDNode>(Inc);
    Inc = DAG.getConstant(*ConstInc->getConstantIntValue(), SDLoc(Inc),
                          ConstInc->getValueType(0));
  }

  unsigned Opc =
      (AM == ISD::PRE_INC || AM == ISD::POST_INC ? ISD::ADD : ISD::SUB);
  return DAG.getNode(Opc, SDLoc(LD), BP.getSimpleValueType(), BP, Inc);
}

static inline int numVectorEltsOrZero(EVT T) {
  return T.isVector() ? T.getVectorNumElements() : 0;
}

bool DAGCombiner::getTruncatedStoreValue(StoreSDNode *ST, SDValue &Val) {
  Val = ST->getValue();
  EVT STType = Val.getValueType();
  EVT STMemType = ST->getMemoryVT();
  if (STType == STMemType)
    return true;
  if (isTypeLegal(STMemType))
    return false; // fail.
  if (STType.isFloatingPoint() && STMemType.isFloatingPoint() &&
      TLI.isOperationLegal(ISD::FTRUNC, STMemType)) {
    Val = DAG.getNode(ISD::FTRUNC, SDLoc(ST), STMemType, Val);
    return true;
  }
  if (numVectorEltsOrZero(STType) == numVectorEltsOrZero(STMemType) &&
      STType.isInteger() && STMemType.isInteger()) {
    Val = DAG.getNode(ISD::TRUNCATE, SDLoc(ST), STMemType, Val);
    return true;
  }
  if (STType.getSizeInBits() == STMemType.getSizeInBits()) {
    Val = DAG.getBitcast(STMemType, Val);
    return true;
  }
  return false; // fail.
}

bool DAGCombiner::extendLoadedValueToExtension(LoadSDNode *LD, SDValue &Val) {
  EVT LDMemType = LD->getMemoryVT();
  EVT LDType = LD->getValueType(0);
  assert(Val.getValueType() == LDMemType &&
         "Attempting to extend value of non-matching type");
  if (LDType == LDMemType)
    return true;
  if (LDMemType.isInteger() && LDType.isInteger()) {
    switch (LD->getExtensionType()) {
    case ISD::NON_EXTLOAD:
      Val = DAG.getBitcast(LDType, Val);
      return true;
    case ISD::EXTLOAD:
      Val = DAG.getNode(ISD::ANY_EXTEND, SDLoc(LD), LDType, Val);
      return true;
    case ISD::SEXTLOAD:
      Val = DAG.getNode(ISD::SIGN_EXTEND, SDLoc(LD), LDType, Val);
      return true;
    case ISD::ZEXTLOAD:
      Val = DAG.getNode(ISD::ZERO_EXTEND, SDLoc(LD), LDType, Val);
      return true;
    }
  }
  return false;
}

SDValue DAGCombiner::ForwardStoreValueToDirectLoad(LoadSDNode *LD) {
  if (OptLevel == CodeGenOpt::None || LD->isVolatile())
    return SDValue();
  SDValue Chain = LD->getOperand(0);
  StoreSDNode *ST = dyn_cast<StoreSDNode>(Chain.getNode());
  if (!ST || ST->isVolatile())
    return SDValue();

  EVT LDType = LD->getValueType(0);
  EVT LDMemType = LD->getMemoryVT();
  EVT STMemType = ST->getMemoryVT();
  EVT STType = ST->getValue().getValueType();

  BaseIndexOffset BasePtrLD = BaseIndexOffset::match(LD, DAG);
  BaseIndexOffset BasePtrST = BaseIndexOffset::match(ST, DAG);
  int64_t Offset;
  if (!BasePtrST.equalBaseIndex(BasePtrLD, DAG, Offset))
    return SDValue();

  // Normalize for Endianness. After this Offset=0 will denote that the least
  // significant bit in the loaded value maps to the least significant bit in
  // the stored value). With Offset=n (for n > 0) the loaded value starts at the
  // n:th least significant byte of the stored value.
  if (DAG.getDataLayout().isBigEndian())
    Offset = (STMemType.getStoreSizeInBits() -
              LDMemType.getStoreSizeInBits()) / 8 - Offset;

  // Check that the stored value cover all bits that are loaded.
  bool STCoversLD =
      (Offset >= 0) &&
      (Offset * 8 + LDMemType.getSizeInBits() <= STMemType.getSizeInBits());

  auto ReplaceLd = [&](LoadSDNode *LD, SDValue Val, SDValue Chain) -> SDValue {
    if (LD->isIndexed()) {
      bool IsSub = (LD->getAddressingMode() == ISD::PRE_DEC ||
                    LD->getAddressingMode() == ISD::POST_DEC);
      unsigned Opc = IsSub ? ISD::SUB : ISD::ADD;
      SDValue Idx = DAG.getNode(Opc, SDLoc(LD), LD->getOperand(1).getValueType(),
                             LD->getOperand(1), LD->getOperand(2));
      SDValue Ops[] = {Val, Idx, Chain};
      return CombineTo(LD, Ops, 3);
    }
    return CombineTo(LD, Val, Chain);
  };

  if (!STCoversLD)
    return SDValue();

  // Memory as copy space (potentially masked).
  if (Offset == 0 && LDType == STType && STMemType == LDMemType) {
    // Simple case: Direct non-truncating forwarding
    if (LDType.getSizeInBits() == LDMemType.getSizeInBits())
      return ReplaceLd(LD, ST->getValue(), Chain);
    // Can we model the truncate and extension with an and mask?
    if (STType.isInteger() && LDMemType.isInteger() && !STType.isVector() &&
        !LDMemType.isVector() && LD->getExtensionType() != ISD::SEXTLOAD) {
      // Mask to size of LDMemType
      auto Mask =
          DAG.getConstant(APInt::getLowBitsSet(STType.getSizeInBits(),
                                               STMemType.getSizeInBits()),
                          SDLoc(ST), STType);
      auto Val = DAG.getNode(ISD::AND, SDLoc(LD), LDType, ST->getValue(), Mask);
      return ReplaceLd(LD, Val, Chain);
    }
  }

  // TODO: Deal with nonzero offset.
  if (LD->getBasePtr().isUndef() || Offset != 0)
    return SDValue();
  // Model necessary truncations / extenstions.
  SDValue Val;
  // Truncate Value To Stored Memory Size.
  do {
    if (!getTruncatedStoreValue(ST, Val))
      continue;
    if (!isTypeLegal(LDMemType))
      continue;
    if (STMemType != LDMemType) {
      // TODO: Support vectors? This requires extract_subvector/bitcast.
      if (!STMemType.isVector() && !LDMemType.isVector() &&
          STMemType.isInteger() && LDMemType.isInteger())
        Val = DAG.getNode(ISD::TRUNCATE, SDLoc(LD), LDMemType, Val);
      else
        continue;
    }
    if (!extendLoadedValueToExtension(LD, Val))
      continue;
    return ReplaceLd(LD, Val, Chain);
  } while (false);

  // On failure, cleanup dead nodes we may have created.
  if (Val->use_empty())
    deleteAndRecombine(Val.getNode());
  return SDValue();
}

SDValue DAGCombiner::visitLOAD(SDNode *N) {
  LoadSDNode *LD  = cast<LoadSDNode>(N);
  SDValue Chain = LD->getChain();
  SDValue Ptr   = LD->getBasePtr();

  // If load is not volatile and there are no uses of the loaded value (and
  // the updated indexed value in case of indexed loads), change uses of the
  // chain value into uses of the chain input (i.e. delete the dead load).
  if (!LD->isVolatile()) {
    if (N->getValueType(1) == MVT::Other) {
      // Unindexed loads.
      if (!N->hasAnyUseOfValue(0)) {
        // It's not safe to use the two value CombineTo variant here. e.g.
        // v1, chain2 = load chain1, loc
        // v2, chain3 = load chain2, loc
        // v3         = add v2, c
        // Now we replace use of chain2 with chain1.  This makes the second load
        // isomorphic to the one we are deleting, and thus makes this load live.
        LLVM_DEBUG(dbgs() << "\nReplacing.6 "; N->dump(&DAG);
                   dbgs() << "\nWith chain: "; Chain.getNode()->dump(&DAG);
                   dbgs() << "\n");
        WorklistRemover DeadNodes(*this);
        DAG.ReplaceAllUsesOfValueWith(SDValue(N, 1), Chain);
        AddUsersToWorklist(Chain.getNode());
        if (N->use_empty())
          deleteAndRecombine(N);

        return SDValue(N, 0);   // Return N so it doesn't get rechecked!
      }
    } else {
      // Indexed loads.
      assert(N->getValueType(2) == MVT::Other && "Malformed indexed loads?");

      // If this load has an opaque TargetConstant offset, then we cannot split
      // the indexing into an add/sub directly (that TargetConstant may not be
      // valid for a different type of node, and we cannot convert an opaque
      // target constant into a regular constant).
      bool HasOTCInc = LD->getOperand(2).getOpcode() == ISD::TargetConstant &&
                       cast<ConstantSDNode>(LD->getOperand(2))->isOpaque();

      if (!N->hasAnyUseOfValue(0) &&
          ((MaySplitLoadIndex && !HasOTCInc) || !N->hasAnyUseOfValue(1))) {
        SDValue Undef = DAG.getUNDEF(N->getValueType(0));
        SDValue Index;
        if (N->hasAnyUseOfValue(1) && MaySplitLoadIndex && !HasOTCInc) {
          Index = SplitIndexingFromLoad(LD);
          // Try to fold the base pointer arithmetic into subsequent loads and
          // stores.
          AddUsersToWorklist(N);
        } else
          Index = DAG.getUNDEF(N->getValueType(1));
        LLVM_DEBUG(dbgs() << "\nReplacing.7 "; N->dump(&DAG);
                   dbgs() << "\nWith: "; Undef.getNode()->dump(&DAG);
                   dbgs() << " and 2 other values\n");
        WorklistRemover DeadNodes(*this);
        DAG.ReplaceAllUsesOfValueWith(SDValue(N, 0), Undef);
        DAG.ReplaceAllUsesOfValueWith(SDValue(N, 1), Index);
        DAG.ReplaceAllUsesOfValueWith(SDValue(N, 2), Chain);
        deleteAndRecombine(N);
        return SDValue(N, 0);   // Return N so it doesn't get rechecked!
      }
    }
  }

  // If this load is directly stored, replace the load value with the stored
  // value.
  if (auto V = ForwardStoreValueToDirectLoad(LD))
    return V;

  // Try to infer better alignment information than the load already has.
  if (OptLevel != CodeGenOpt::None && LD->isUnindexed()) {
    if (unsigned Align = DAG.InferPtrAlignment(Ptr)) {
      if (Align > LD->getAlignment() && LD->getSrcValueOffset() % Align == 0) {
        SDValue NewLoad = DAG.getExtLoad(
            LD->getExtensionType(), SDLoc(N), LD->getValueType(0), Chain, Ptr,
            LD->getPointerInfo(), LD->getMemoryVT(), Align,
            LD->getMemOperand()->getFlags(), LD->getAAInfo());
        // NewLoad will always be N as we are only refining the alignment
        assert(NewLoad.getNode() == N);
        (void)NewLoad;
      }
    }
  }

  if (LD->isUnindexed()) {
    // Walk up chain skipping non-aliasing memory nodes.
    SDValue BetterChain = FindBetterChain(N, Chain);

    // If there is a better chain.
    if (Chain != BetterChain) {
      SDValue ReplLoad;

      // Replace the chain to void dependency.
      if (LD->getExtensionType() == ISD::NON_EXTLOAD) {
        ReplLoad = DAG.getLoad(N->getValueType(0), SDLoc(LD),
                               BetterChain, Ptr, LD->getMemOperand());
      } else {
        ReplLoad = DAG.getExtLoad(LD->getExtensionType(), SDLoc(LD),
                                  LD->getValueType(0),
                                  BetterChain, Ptr, LD->getMemoryVT(),
                                  LD->getMemOperand());
      }

      // Create token factor to keep old chain connected.
      SDValue Token = DAG.getNode(ISD::TokenFactor, SDLoc(N),
                                  MVT::Other, Chain, ReplLoad.getValue(1));

      // Replace uses with load result and token factor
      return CombineTo(N, ReplLoad.getValue(0), Token);
    }
  }

  // Try transforming N to an indexed load.
  if (CombineToPreIndexedLoadStore(N) || CombineToPostIndexedLoadStore(N))
    return SDValue(N, 0);

  // Try to slice up N to more direct loads if the slices are mapped to
  // different register banks or pairing can take place.
  if (SliceUpLoad(N))
    return SDValue(N, 0);

  return SDValue();
}

namespace {

/// Helper structure used to slice a load in smaller loads.
/// Basically a slice is obtained from the following sequence:
/// Origin = load Ty1, Base
/// Shift = srl Ty1 Origin, CstTy Amount
/// Inst = trunc Shift to Ty2
///
/// Then, it will be rewritten into:
/// Slice = load SliceTy, Base + SliceOffset
/// [Inst = zext Slice to Ty2], only if SliceTy <> Ty2
///
/// SliceTy is deduced from the number of bits that are actually used to
/// build Inst.
struct LoadedSlice {
  /// Helper structure used to compute the cost of a slice.
  struct Cost {
    /// Are we optimizing for code size.
    bool ForCodeSize;

    /// Various cost.
    unsigned Loads = 0;
    unsigned Truncates = 0;
    unsigned CrossRegisterBanksCopies = 0;
    unsigned ZExts = 0;
    unsigned Shift = 0;

    Cost(bool ForCodeSize = false) : ForCodeSize(ForCodeSize) {}

    /// Get the cost of one isolated slice.
    Cost(const LoadedSlice &LS, bool ForCodeSize = false)
        : ForCodeSize(ForCodeSize), Loads(1) {
      EVT TruncType = LS.Inst->getValueType(0);
      EVT LoadedType = LS.getLoadedType();
      if (TruncType != LoadedType &&
          !LS.DAG->getTargetLoweringInfo().isZExtFree(LoadedType, TruncType))
        ZExts = 1;
    }

    /// Account for slicing gain in the current cost.
    /// Slicing provide a few gains like removing a shift or a
    /// truncate. This method allows to grow the cost of the original
    /// load with the gain from this slice.
    void addSliceGain(const LoadedSlice &LS) {
      // Each slice saves a truncate.
      const TargetLowering &TLI = LS.DAG->getTargetLoweringInfo();
      if (!TLI.isTruncateFree(LS.Inst->getOperand(0).getValueType(),
                              LS.Inst->getValueType(0)))
        ++Truncates;
      // If there is a shift amount, this slice gets rid of it.
      if (LS.Shift)
        ++Shift;
      // If this slice can merge a cross register bank copy, account for it.
      if (LS.canMergeExpensiveCrossRegisterBankCopy())
        ++CrossRegisterBanksCopies;
    }

    Cost &operator+=(const Cost &RHS) {
      Loads += RHS.Loads;
      Truncates += RHS.Truncates;
      CrossRegisterBanksCopies += RHS.CrossRegisterBanksCopies;
      ZExts += RHS.ZExts;
      Shift += RHS.Shift;
      return *this;
    }

    bool operator==(const Cost &RHS) const {
      return Loads == RHS.Loads && Truncates == RHS.Truncates &&
             CrossRegisterBanksCopies == RHS.CrossRegisterBanksCopies &&
             ZExts == RHS.ZExts && Shift == RHS.Shift;
    }

    bool operator!=(const Cost &RHS) const { return !(*this == RHS); }

    bool operator<(const Cost &RHS) const {
      // Assume cross register banks copies are as expensive as loads.
      // FIXME: Do we want some more target hooks?
      unsigned ExpensiveOpsLHS = Loads + CrossRegisterBanksCopies;
      unsigned ExpensiveOpsRHS = RHS.Loads + RHS.CrossRegisterBanksCopies;
      // Unless we are optimizing for code size, consider the
      // expensive operation first.
      if (!ForCodeSize && ExpensiveOpsLHS != ExpensiveOpsRHS)
        return ExpensiveOpsLHS < ExpensiveOpsRHS;
      return (Truncates + ZExts + Shift + ExpensiveOpsLHS) <
             (RHS.Truncates + RHS.ZExts + RHS.Shift + ExpensiveOpsRHS);
    }

    bool operator>(const Cost &RHS) const { return RHS < *this; }

    bool operator<=(const Cost &RHS) const { return !(RHS < *this); }

    bool operator>=(const Cost &RHS) const { return !(*this < RHS); }
  };

  // The last instruction that represent the slice. This should be a
  // truncate instruction.
  SDNode *Inst;

  // The original load instruction.
  LoadSDNode *Origin;

  // The right shift amount in bits from the original load.
  unsigned Shift;

  // The DAG from which Origin came from.
  // This is used to get some contextual information about legal types, etc.
  SelectionDAG *DAG;

  LoadedSlice(SDNode *Inst = nullptr, LoadSDNode *Origin = nullptr,
              unsigned Shift = 0, SelectionDAG *DAG = nullptr)
      : Inst(Inst), Origin(Origin), Shift(Shift), DAG(DAG) {}

  /// Get the bits used in a chunk of bits \p BitWidth large.
  /// \return Result is \p BitWidth and has used bits set to 1 and
  ///         not used bits set to 0.
  APInt getUsedBits() const {
    // Reproduce the trunc(lshr) sequence:
    // - Start from the truncated value.
    // - Zero extend to the desired bit width.
    // - Shift left.
    assert(Origin && "No original load to compare against.");
    unsigned BitWidth = Origin->getValueSizeInBits(0);
    assert(Inst && "This slice is not bound to an instruction");
    assert(Inst->getValueSizeInBits(0) <= BitWidth &&
           "Extracted slice is bigger than the whole type!");
    APInt UsedBits(Inst->getValueSizeInBits(0), 0);
    UsedBits.setAllBits();
    UsedBits = UsedBits.zext(BitWidth);
    UsedBits <<= Shift;
    return UsedBits;
  }

  /// Get the size of the slice to be loaded in bytes.
  unsigned getLoadedSize() const {
    unsigned SliceSize = getUsedBits().countPopulation();
    assert(!(SliceSize & 0x7) && "Size is not a multiple of a byte.");
    return SliceSize / 8;
  }

  /// Get the type that will be loaded for this slice.
  /// Note: This may not be the final type for the slice.
  EVT getLoadedType() const {
    assert(DAG && "Missing context");
    LLVMContext &Ctxt = *DAG->getContext();
    return EVT::getIntegerVT(Ctxt, getLoadedSize() * 8);
  }

  /// Get the alignment of the load used for this slice.
  unsigned getAlignment() const {
    unsigned Alignment = Origin->getAlignment();
    unsigned Offset = getOffsetFromBase();
    if (Offset != 0)
      Alignment = MinAlign(Alignment, Alignment + Offset);
    return Alignment;
  }

  /// Check if this slice can be rewritten with legal operations.
  bool isLegal() const {
    // An invalid slice is not legal.
    if (!Origin || !Inst || !DAG)
      return false;

    // Offsets are for indexed load only, we do not handle that.
    if (!Origin->getOffset().isUndef())
      return false;

    const TargetLowering &TLI = DAG->getTargetLoweringInfo();

    // Check that the type is legal.
    EVT SliceType = getLoadedType();
    if (!TLI.isTypeLegal(SliceType))
      return false;

    // Check that the load is legal for this type.
    if (!TLI.isOperationLegal(ISD::LOAD, SliceType))
      return false;

    // Check that the offset can be computed.
    // 1. Check its type.
    EVT PtrType = Origin->getBasePtr().getValueType();
    if (PtrType == MVT::Untyped || PtrType.isExtended())
      return false;

    // 2. Check that it fits in the immediate.
    if (!TLI.isLegalAddImmediate(getOffsetFromBase()))
      return false;

    // 3. Check that the computation is legal.
    if (!TLI.isOperationLegal(ISD::ADD, PtrType))
      return false;

    // Check that the zext is legal if it needs one.
    EVT TruncateType = Inst->getValueType(0);
    if (TruncateType != SliceType &&
        !TLI.isOperationLegal(ISD::ZERO_EXTEND, TruncateType))
      return false;

    return true;
  }

  /// Get the offset in bytes of this slice in the original chunk of
  /// bits.
  /// \pre DAG != nullptr.
  uint64_t getOffsetFromBase() const {
    assert(DAG && "Missing context.");
    bool IsBigEndian = DAG->getDataLayout().isBigEndian();
    assert(!(Shift & 0x7) && "Shifts not aligned on Bytes are not supported.");
    uint64_t Offset = Shift / 8;
    unsigned TySizeInBytes = Origin->getValueSizeInBits(0) / 8;
    assert(!(Origin->getValueSizeInBits(0) & 0x7) &&
           "The size of the original loaded type is not a multiple of a"
           " byte.");
    // If Offset is bigger than TySizeInBytes, it means we are loading all
    // zeros. This should have been optimized before in the process.
    assert(TySizeInBytes > Offset &&
           "Invalid shift amount for given loaded size");
    if (IsBigEndian)
      Offset = TySizeInBytes - Offset - getLoadedSize();
    return Offset;
  }

  /// Generate the sequence of instructions to load the slice
  /// represented by this object and redirect the uses of this slice to
  /// this new sequence of instructions.
  /// \pre this->Inst && this->Origin are valid Instructions and this
  /// object passed the legal check: LoadedSlice::isLegal returned true.
  /// \return The last instruction of the sequence used to load the slice.
  SDValue loadSlice() const {
    assert(Inst && Origin && "Unable to replace a non-existing slice.");
    const SDValue &OldBaseAddr = Origin->getBasePtr();
    SDValue BaseAddr = OldBaseAddr;
    // Get the offset in that chunk of bytes w.r.t. the endianness.
    int64_t Offset = static_cast<int64_t>(getOffsetFromBase());
    assert(Offset >= 0 && "Offset too big to fit in int64_t!");
    if (Offset) {
      // BaseAddr = BaseAddr + Offset.
      EVT ArithType = BaseAddr.getValueType();
      SDLoc DL(Origin);
      BaseAddr = DAG->getNode(ISD::ADD, DL, ArithType, BaseAddr,
                              DAG->getConstant(Offset, DL, ArithType));
    }

    // Create the type of the loaded slice according to its size.
    EVT SliceType = getLoadedType();

    // Create the load for the slice.
    SDValue LastInst =
        DAG->getLoad(SliceType, SDLoc(Origin), Origin->getChain(), BaseAddr,
                     Origin->getPointerInfo().getWithOffset(Offset),
                     getAlignment(), Origin->getMemOperand()->getFlags());
    // If the final type is not the same as the loaded type, this means that
    // we have to pad with zero. Create a zero extend for that.
    EVT FinalType = Inst->getValueType(0);
    if (SliceType != FinalType)
      LastInst =
          DAG->getNode(ISD::ZERO_EXTEND, SDLoc(LastInst), FinalType, LastInst);
    return LastInst;
  }

  /// Check if this slice can be merged with an expensive cross register
  /// bank copy. E.g.,
  /// i = load i32
  /// f = bitcast i32 i to float
  bool canMergeExpensiveCrossRegisterBankCopy() const {
    if (!Inst || !Inst->hasOneUse())
      return false;
    SDNode *Use = *Inst->use_begin();
    if (Use->getOpcode() != ISD::BITCAST)
      return false;
    assert(DAG && "Missing context");
    const TargetLowering &TLI = DAG->getTargetLoweringInfo();
    EVT ResVT = Use->getValueType(0);
    const TargetRegisterClass *ResRC = TLI.getRegClassFor(ResVT.getSimpleVT());
    const TargetRegisterClass *ArgRC =
        TLI.getRegClassFor(Use->getOperand(0).getValueType().getSimpleVT());
    if (ArgRC == ResRC || !TLI.isOperationLegal(ISD::LOAD, ResVT))
      return false;

    // At this point, we know that we perform a cross-register-bank copy.
    // Check if it is expensive.
    const TargetRegisterInfo *TRI = DAG->getSubtarget().getRegisterInfo();
    // Assume bitcasts are cheap, unless both register classes do not
    // explicitly share a common sub class.
    if (!TRI || TRI->getCommonSubClass(ArgRC, ResRC))
      return false;

    // Check if it will be merged with the load.
    // 1. Check the alignment constraint.
    unsigned RequiredAlignment = DAG->getDataLayout().getABITypeAlignment(
        ResVT.getTypeForEVT(*DAG->getContext()));

    if (RequiredAlignment > getAlignment())
      return false;

    // 2. Check that the load is a legal operation for that type.
    if (!TLI.isOperationLegal(ISD::LOAD, ResVT))
      return false;

    // 3. Check that we do not have a zext in the way.
    if (Inst->getValueType(0) != getLoadedType())
      return false;

    return true;
  }
};

} // end anonymous namespace

/// Check that all bits set in \p UsedBits form a dense region, i.e.,
/// \p UsedBits looks like 0..0 1..1 0..0.
static bool areUsedBitsDense(const APInt &UsedBits) {
  // If all the bits are one, this is dense!
  if (UsedBits.isAllOnesValue())
    return true;

  // Get rid of the unused bits on the right.
  APInt NarrowedUsedBits = UsedBits.lshr(UsedBits.countTrailingZeros());
  // Get rid of the unused bits on the left.
  if (NarrowedUsedBits.countLeadingZeros())
    NarrowedUsedBits = NarrowedUsedBits.trunc(NarrowedUsedBits.getActiveBits());
  // Check that the chunk of bits is completely used.
  return NarrowedUsedBits.isAllOnesValue();
}

/// Check whether or not \p First and \p Second are next to each other
/// in memory. This means that there is no hole between the bits loaded
/// by \p First and the bits loaded by \p Second.
static bool areSlicesNextToEachOther(const LoadedSlice &First,
                                     const LoadedSlice &Second) {
  assert(First.Origin == Second.Origin && First.Origin &&
         "Unable to match different memory origins.");
  APInt UsedBits = First.getUsedBits();
  assert((UsedBits & Second.getUsedBits()) == 0 &&
         "Slices are not supposed to overlap.");
  UsedBits |= Second.getUsedBits();
  return areUsedBitsDense(UsedBits);
}

/// Adjust the \p GlobalLSCost according to the target
/// paring capabilities and the layout of the slices.
/// \pre \p GlobalLSCost should account for at least as many loads as
/// there is in the slices in \p LoadedSlices.
static void adjustCostForPairing(SmallVectorImpl<LoadedSlice> &LoadedSlices,
                                 LoadedSlice::Cost &GlobalLSCost) {
  unsigned NumberOfSlices = LoadedSlices.size();
  // If there is less than 2 elements, no pairing is possible.
  if (NumberOfSlices < 2)
    return;

  // Sort the slices so that elements that are likely to be next to each
  // other in memory are next to each other in the list.
  llvm::sort(LoadedSlices, [](const LoadedSlice &LHS, const LoadedSlice &RHS) {
    assert(LHS.Origin == RHS.Origin && "Different bases not implemented.");
    return LHS.getOffsetFromBase() < RHS.getOffsetFromBase();
  });
  const TargetLowering &TLI = LoadedSlices[0].DAG->getTargetLoweringInfo();
  // First (resp. Second) is the first (resp. Second) potentially candidate
  // to be placed in a paired load.
  const LoadedSlice *First = nullptr;
  const LoadedSlice *Second = nullptr;
  for (unsigned CurrSlice = 0; CurrSlice < NumberOfSlices; ++CurrSlice,
                // Set the beginning of the pair.
                                                           First = Second) {
    Second = &LoadedSlices[CurrSlice];

    // If First is NULL, it means we start a new pair.
    // Get to the next slice.
    if (!First)
      continue;

    EVT LoadedType = First->getLoadedType();

    // If the types of the slices are different, we cannot pair them.
    if (LoadedType != Second->getLoadedType())
      continue;

    // Check if the target supplies paired loads for this type.
    unsigned RequiredAlignment = 0;
    if (!TLI.hasPairedLoad(LoadedType, RequiredAlignment)) {
      // move to the next pair, this type is hopeless.
      Second = nullptr;
      continue;
    }
    // Check if we meet the alignment requirement.
    if (RequiredAlignment > First->getAlignment())
      continue;

    // Check that both loads are next to each other in memory.
    if (!areSlicesNextToEachOther(*First, *Second))
      continue;

    assert(GlobalLSCost.Loads > 0 && "We save more loads than we created!");
    --GlobalLSCost.Loads;
    // Move to the next pair.
    Second = nullptr;
  }
}

/// Check the profitability of all involved LoadedSlice.
/// Currently, it is considered profitable if there is exactly two
/// involved slices (1) which are (2) next to each other in memory, and
/// whose cost (\see LoadedSlice::Cost) is smaller than the original load (3).
///
/// Note: The order of the elements in \p LoadedSlices may be modified, but not
/// the elements themselves.
///
/// FIXME: When the cost model will be mature enough, we can relax
/// constraints (1) and (2).
static bool isSlicingProfitable(SmallVectorImpl<LoadedSlice> &LoadedSlices,
                                const APInt &UsedBits, bool ForCodeSize) {
  unsigned NumberOfSlices = LoadedSlices.size();
  if (StressLoadSlicing)
    return NumberOfSlices > 1;

  // Check (1).
  if (NumberOfSlices != 2)
    return false;

  // Check (2).
  if (!areUsedBitsDense(UsedBits))
    return false;

  // Check (3).
  LoadedSlice::Cost OrigCost(ForCodeSize), GlobalSlicingCost(ForCodeSize);
  // The original code has one big load.
  OrigCost.Loads = 1;
  for (unsigned CurrSlice = 0; CurrSlice < NumberOfSlices; ++CurrSlice) {
    const LoadedSlice &LS = LoadedSlices[CurrSlice];
    // Accumulate the cost of all the slices.
    LoadedSlice::Cost SliceCost(LS, ForCodeSize);
    GlobalSlicingCost += SliceCost;

    // Account as cost in the original configuration the gain obtained
    // with the current slices.
    OrigCost.addSliceGain(LS);
  }

  // If the target supports paired load, adjust the cost accordingly.
  adjustCostForPairing(LoadedSlices, GlobalSlicingCost);
  return OrigCost > GlobalSlicingCost;
}

/// If the given load, \p LI, is used only by trunc or trunc(lshr)
/// operations, split it in the various pieces being extracted.
///
/// This sort of thing is introduced by SROA.
/// This slicing takes care not to insert overlapping loads.
/// \pre LI is a simple load (i.e., not an atomic or volatile load).
bool DAGCombiner::SliceUpLoad(SDNode *N) {
  if (Level < AfterLegalizeDAG)
    return false;

  LoadSDNode *LD = cast<LoadSDNode>(N);
  if (LD->isVolatile() || !ISD::isNormalLoad(LD) ||
      !LD->getValueType(0).isInteger())
    return false;

  // Keep track of already used bits to detect overlapping values.
  // In that case, we will just abort the transformation.
  APInt UsedBits(LD->getValueSizeInBits(0), 0);

  SmallVector<LoadedSlice, 4> LoadedSlices;

  // Check if this load is used as several smaller chunks of bits.
  // Basically, look for uses in trunc or trunc(lshr) and record a new chain
  // of computation for each trunc.
  for (SDNode::use_iterator UI = LD->use_begin(), UIEnd = LD->use_end();
       UI != UIEnd; ++UI) {
    // Skip the uses of the chain.
    if (UI.getUse().getResNo() != 0)
      continue;

    SDNode *User = *UI;
    unsigned Shift = 0;

    // Check if this is a trunc(lshr).
    if (User->getOpcode() == ISD::SRL && User->hasOneUse() &&
        isa<ConstantSDNode>(User->getOperand(1))) {
      Shift = User->getConstantOperandVal(1);
      User = *User->use_begin();
    }

    // At this point, User is a Truncate, iff we encountered, trunc or
    // trunc(lshr).
    if (User->getOpcode() != ISD::TRUNCATE)
      return false;

    // The width of the type must be a power of 2 and greater than 8-bits.
    // Otherwise the load cannot be represented in LLVM IR.
    // Moreover, if we shifted with a non-8-bits multiple, the slice
    // will be across several bytes. We do not support that.
    unsigned Width = User->getValueSizeInBits(0);
    if (Width < 8 || !isPowerOf2_32(Width) || (Shift & 0x7))
      return false;

    // Build the slice for this chain of computations.
    LoadedSlice LS(User, LD, Shift, &DAG);
    APInt CurrentUsedBits = LS.getUsedBits();

    // Check if this slice overlaps with another.
    if ((CurrentUsedBits & UsedBits) != 0)
      return false;
    // Update the bits used globally.
    UsedBits |= CurrentUsedBits;

    // Check if the new slice would be legal.
    if (!LS.isLegal())
      return false;

    // Record the slice.
    LoadedSlices.push_back(LS);
  }

  // Abort slicing if it does not seem to be profitable.
  if (!isSlicingProfitable(LoadedSlices, UsedBits, ForCodeSize))
    return false;

  ++SlicedLoads;

  // Rewrite each chain to use an independent load.
  // By construction, each chain can be represented by a unique load.

  // Prepare the argument for the new token factor for all the slices.
  SmallVector<SDValue, 8> ArgChains;
  for (SmallVectorImpl<LoadedSlice>::const_iterator
           LSIt = LoadedSlices.begin(),
           LSItEnd = LoadedSlices.end();
       LSIt != LSItEnd; ++LSIt) {
    SDValue SliceInst = LSIt->loadSlice();
    CombineTo(LSIt->Inst, SliceInst, true);
    if (SliceInst.getOpcode() != ISD::LOAD)
      SliceInst = SliceInst.getOperand(0);
    assert(SliceInst->getOpcode() == ISD::LOAD &&
           "It takes more than a zext to get to the loaded slice!!");
    ArgChains.push_back(SliceInst.getValue(1));
  }

  SDValue Chain = DAG.getNode(ISD::TokenFactor, SDLoc(LD), MVT::Other,
                              ArgChains);
  DAG.ReplaceAllUsesOfValueWith(SDValue(N, 1), Chain);
  AddToWorklist(Chain.getNode());
  return true;
}

/// Check to see if V is (and load (ptr), imm), where the load is having
/// specific bytes cleared out.  If so, return the byte size being masked out
/// and the shift amount.
static std::pair<unsigned, unsigned>
CheckForMaskedLoad(SDValue V, SDValue Ptr, SDValue Chain) {
  std::pair<unsigned, unsigned> Result(0, 0);

  // Check for the structure we're looking for.
  if (V->getOpcode() != ISD::AND ||
      !isa<ConstantSDNode>(V->getOperand(1)) ||
      !ISD::isNormalLoad(V->getOperand(0).getNode()))
    return Result;

  // Check the chain and pointer.
  LoadSDNode *LD = cast<LoadSDNode>(V->getOperand(0));
  if (LD->getBasePtr() != Ptr) return Result;  // Not from same pointer.

  // This only handles simple types.
  if (V.getValueType() != MVT::i16 &&
      V.getValueType() != MVT::i32 &&
      V.getValueType() != MVT::i64)
    return Result;

  // Check the constant mask.  Invert it so that the bits being masked out are
  // 0 and the bits being kept are 1.  Use getSExtValue so that leading bits
  // follow the sign bit for uniformity.
  uint64_t NotMask = ~cast<ConstantSDNode>(V->getOperand(1))->getSExtValue();
  unsigned NotMaskLZ = countLeadingZeros(NotMask);
  if (NotMaskLZ & 7) return Result;  // Must be multiple of a byte.
  unsigned NotMaskTZ = countTrailingZeros(NotMask);
  if (NotMaskTZ & 7) return Result;  // Must be multiple of a byte.
  if (NotMaskLZ == 64) return Result;  // All zero mask.

  // See if we have a continuous run of bits.  If so, we have 0*1+0*
  if (countTrailingOnes(NotMask >> NotMaskTZ) + NotMaskTZ + NotMaskLZ != 64)
    return Result;

  // Adjust NotMaskLZ down to be from the actual size of the int instead of i64.
  if (V.getValueType() != MVT::i64 && NotMaskLZ)
    NotMaskLZ -= 64-V.getValueSizeInBits();

  unsigned MaskedBytes = (V.getValueSizeInBits()-NotMaskLZ-NotMaskTZ)/8;
  switch (MaskedBytes) {
  case 1:
  case 2:
  case 4: break;
  default: return Result; // All one mask, or 5-byte mask.
  }

  // Verify that the first bit starts at a multiple of mask so that the access
  // is aligned the same as the access width.
  if (NotMaskTZ && NotMaskTZ/8 % MaskedBytes) return Result;

  // For narrowing to be valid, it must be the case that the load the
  // immediately preceeding memory operation before the store.
  if (LD == Chain.getNode())
    ; // ok.
  else if (Chain->getOpcode() == ISD::TokenFactor &&
           SDValue(LD, 1).hasOneUse()) {
    // LD has only 1 chain use so they are no indirect dependencies.
    bool isOk = false;
    for (const SDValue &ChainOp : Chain->op_values())
      if (ChainOp.getNode() == LD) {
        isOk = true;
        break;
      }
    if (!isOk)
      return Result;
  } else
    return Result; // Fail.

  Result.first = MaskedBytes;
  Result.second = NotMaskTZ/8;
  return Result;
}

/// Check to see if IVal is something that provides a value as specified by
/// MaskInfo. If so, replace the specified store with a narrower store of
/// truncated IVal.
static SDNode *
ShrinkLoadReplaceStoreWithStore(const std::pair<unsigned, unsigned> &MaskInfo,
                                SDValue IVal, StoreSDNode *St,
                                DAGCombiner *DC) {
  unsigned NumBytes = MaskInfo.first;
  unsigned ByteShift = MaskInfo.second;
  SelectionDAG &DAG = DC->getDAG();

  // Check to see if IVal is all zeros in the part being masked in by the 'or'
  // that uses this.  If not, this is not a replacement.
  APInt Mask = ~APInt::getBitsSet(IVal.getValueSizeInBits(),
                                  ByteShift*8, (ByteShift+NumBytes)*8);
  if (!DAG.MaskedValueIsZero(IVal, Mask)) return nullptr;

  // Check that it is legal on the target to do this.  It is legal if the new
  // VT we're shrinking to (i8/i16/i32) is legal or we're still before type
  // legalization.
  MVT VT = MVT::getIntegerVT(NumBytes*8);
  if (!DC->isTypeLegal(VT))
    return nullptr;

  // Okay, we can do this!  Replace the 'St' store with a store of IVal that is
  // shifted by ByteShift and truncated down to NumBytes.
  if (ByteShift) {
    SDLoc DL(IVal);
    IVal = DAG.getNode(ISD::SRL, DL, IVal.getValueType(), IVal,
                       DAG.getConstant(ByteShift*8, DL,
                                    DC->getShiftAmountTy(IVal.getValueType())));
  }

  // Figure out the offset for the store and the alignment of the access.
  unsigned StOffset;
  unsigned NewAlign = St->getAlignment();

  if (DAG.getDataLayout().isLittleEndian())
    StOffset = ByteShift;
  else
    StOffset = IVal.getValueType().getStoreSize() - ByteShift - NumBytes;

  SDValue Ptr = St->getBasePtr();
  if (StOffset) {
    SDLoc DL(IVal);
    Ptr = DAG.getNode(ISD::ADD, DL, Ptr.getValueType(),
                      Ptr, DAG.getConstant(StOffset, DL, Ptr.getValueType()));
    NewAlign = MinAlign(NewAlign, StOffset);
  }

  // Truncate down to the new size.
  IVal = DAG.getNode(ISD::TRUNCATE, SDLoc(IVal), VT, IVal);

  ++OpsNarrowed;
  return DAG
      .getStore(St->getChain(), SDLoc(St), IVal, Ptr,
                St->getPointerInfo().getWithOffset(StOffset), NewAlign)
      .getNode();
}

/// Look for sequence of load / op / store where op is one of 'or', 'xor', and
/// 'and' of immediates. If 'op' is only touching some of the loaded bits, try
/// narrowing the load and store if it would end up being a win for performance
/// or code size.
SDValue DAGCombiner::ReduceLoadOpStoreWidth(SDNode *N) {
  StoreSDNode *ST  = cast<StoreSDNode>(N);
  if (ST->isVolatile())
    return SDValue();

  SDValue Chain = ST->getChain();
  SDValue Value = ST->getValue();
  SDValue Ptr   = ST->getBasePtr();
  EVT VT = Value.getValueType();

  if (ST->isTruncatingStore() || VT.isVector() || !Value.hasOneUse())
    return SDValue();

  unsigned Opc = Value.getOpcode();

  // If this is "store (or X, Y), P" and X is "(and (load P), cst)", where cst
  // is a byte mask indicating a consecutive number of bytes, check to see if
  // Y is known to provide just those bytes.  If so, we try to replace the
  // load + replace + store sequence with a single (narrower) store, which makes
  // the load dead.
  if (Opc == ISD::OR) {
    std::pair<unsigned, unsigned> MaskedLoad;
    MaskedLoad = CheckForMaskedLoad(Value.getOperand(0), Ptr, Chain);
    if (MaskedLoad.first)
      if (SDNode *NewST = ShrinkLoadReplaceStoreWithStore(MaskedLoad,
                                                  Value.getOperand(1), ST,this))
        return SDValue(NewST, 0);

    // Or is commutative, so try swapping X and Y.
    MaskedLoad = CheckForMaskedLoad(Value.getOperand(1), Ptr, Chain);
    if (MaskedLoad.first)
      if (SDNode *NewST = ShrinkLoadReplaceStoreWithStore(MaskedLoad,
                                                  Value.getOperand(0), ST,this))
        return SDValue(NewST, 0);
  }

  if ((Opc != ISD::OR && Opc != ISD::XOR && Opc != ISD::AND) ||
      Value.getOperand(1).getOpcode() != ISD::Constant)
    return SDValue();

  SDValue N0 = Value.getOperand(0);
  if (ISD::isNormalLoad(N0.getNode()) && N0.hasOneUse() &&
      Chain == SDValue(N0.getNode(), 1)) {
    LoadSDNode *LD = cast<LoadSDNode>(N0);
    if (LD->getBasePtr() != Ptr ||
        LD->getPointerInfo().getAddrSpace() !=
        ST->getPointerInfo().getAddrSpace())
      return SDValue();

    // Find the type to narrow it the load / op / store to.
    SDValue N1 = Value.getOperand(1);
    unsigned BitWidth = N1.getValueSizeInBits();
    APInt Imm = cast<ConstantSDNode>(N1)->getAPIntValue();
    if (Opc == ISD::AND)
      Imm ^= APInt::getAllOnesValue(BitWidth);
    if (Imm == 0 || Imm.isAllOnesValue())
      return SDValue();
    unsigned ShAmt = Imm.countTrailingZeros();
    unsigned MSB = BitWidth - Imm.countLeadingZeros() - 1;
    unsigned NewBW = NextPowerOf2(MSB - ShAmt);
    EVT NewVT = EVT::getIntegerVT(*DAG.getContext(), NewBW);
    // The narrowing should be profitable, the load/store operation should be
    // legal (or custom) and the store size should be equal to the NewVT width.
    while (NewBW < BitWidth &&
           (NewVT.getStoreSizeInBits() != NewBW ||
            !TLI.isOperationLegalOrCustom(Opc, NewVT) ||
            !TLI.isNarrowingProfitable(VT, NewVT))) {
      NewBW = NextPowerOf2(NewBW);
      NewVT = EVT::getIntegerVT(*DAG.getContext(), NewBW);
    }
    if (NewBW >= BitWidth)
      return SDValue();

    // If the lsb changed does not start at the type bitwidth boundary,
    // start at the previous one.
    if (ShAmt % NewBW)
      ShAmt = (((ShAmt + NewBW - 1) / NewBW) * NewBW) - NewBW;
    APInt Mask = APInt::getBitsSet(BitWidth, ShAmt,
                                   std::min(BitWidth, ShAmt + NewBW));
    if ((Imm & Mask) == Imm) {
      APInt NewImm = (Imm & Mask).lshr(ShAmt).trunc(NewBW);
      if (Opc == ISD::AND)
        NewImm ^= APInt::getAllOnesValue(NewBW);
      uint64_t PtrOff = ShAmt / 8;
      // For big endian targets, we need to adjust the offset to the pointer to
      // load the correct bytes.
      if (DAG.getDataLayout().isBigEndian())
        PtrOff = (BitWidth + 7 - NewBW) / 8 - PtrOff;

      unsigned NewAlign = MinAlign(LD->getAlignment(), PtrOff);
      Type *NewVTTy = NewVT.getTypeForEVT(*DAG.getContext());
      if (NewAlign < DAG.getDataLayout().getABITypeAlignment(NewVTTy))
        return SDValue();

      SDValue NewPtr = DAG.getNode(ISD::ADD, SDLoc(LD),
                                   Ptr.getValueType(), Ptr,
                                   DAG.getConstant(PtrOff, SDLoc(LD),
                                                   Ptr.getValueType()));
      SDValue NewLD =
          DAG.getLoad(NewVT, SDLoc(N0), LD->getChain(), NewPtr,
                      LD->getPointerInfo().getWithOffset(PtrOff), NewAlign,
                      LD->getMemOperand()->getFlags(), LD->getAAInfo());
      SDValue NewVal = DAG.getNode(Opc, SDLoc(Value), NewVT, NewLD,
                                   DAG.getConstant(NewImm, SDLoc(Value),
                                                   NewVT));
      SDValue NewST =
          DAG.getStore(Chain, SDLoc(N), NewVal, NewPtr,
                       ST->getPointerInfo().getWithOffset(PtrOff), NewAlign);

      AddToWorklist(NewPtr.getNode());
      AddToWorklist(NewLD.getNode());
      AddToWorklist(NewVal.getNode());
      WorklistRemover DeadNodes(*this);
      DAG.ReplaceAllUsesOfValueWith(N0.getValue(1), NewLD.getValue(1));
      ++OpsNarrowed;
      return NewST;
    }
  }

  return SDValue();
}

/// For a given floating point load / store pair, if the load value isn't used
/// by any other operations, then consider transforming the pair to integer
/// load / store operations if the target deems the transformation profitable.
SDValue DAGCombiner::TransformFPLoadStorePair(SDNode *N) {
  StoreSDNode *ST  = cast<StoreSDNode>(N);
  SDValue Chain = ST->getChain();
  SDValue Value = ST->getValue();
  if (ISD::isNormalStore(ST) && ISD::isNormalLoad(Value.getNode()) &&
      Value.hasOneUse() &&
      Chain == SDValue(Value.getNode(), 1)) {
    LoadSDNode *LD = cast<LoadSDNode>(Value);
    EVT VT = LD->getMemoryVT();
    if (!VT.isFloatingPoint() ||
        VT != ST->getMemoryVT() ||
        LD->isNonTemporal() ||
        ST->isNonTemporal() ||
        LD->getPointerInfo().getAddrSpace() != 0 ||
        ST->getPointerInfo().getAddrSpace() != 0)
      return SDValue();

    EVT IntVT = EVT::getIntegerVT(*DAG.getContext(), VT.getSizeInBits());
    if (!TLI.isOperationLegal(ISD::LOAD, IntVT) ||
        !TLI.isOperationLegal(ISD::STORE, IntVT) ||
        !TLI.isDesirableToTransformToIntegerOp(ISD::LOAD, VT) ||
        !TLI.isDesirableToTransformToIntegerOp(ISD::STORE, VT))
      return SDValue();

    unsigned LDAlign = LD->getAlignment();
    unsigned STAlign = ST->getAlignment();
    Type *IntVTTy = IntVT.getTypeForEVT(*DAG.getContext());
    unsigned ABIAlign = DAG.getDataLayout().getABITypeAlignment(IntVTTy);
    if (LDAlign < ABIAlign || STAlign < ABIAlign)
      return SDValue();

    SDValue NewLD =
        DAG.getLoad(IntVT, SDLoc(Value), LD->getChain(), LD->getBasePtr(),
                    LD->getPointerInfo(), LDAlign);

    SDValue NewST =
        DAG.getStore(NewLD.getValue(1), SDLoc(N), NewLD, ST->getBasePtr(),
                     ST->getPointerInfo(), STAlign);

    AddToWorklist(NewLD.getNode());
    AddToWorklist(NewST.getNode());
    WorklistRemover DeadNodes(*this);
    DAG.ReplaceAllUsesOfValueWith(Value.getValue(1), NewLD.getValue(1));
    ++LdStFP2Int;
    return NewST;
  }

  return SDValue();
}

// This is a helper function for visitMUL to check the profitability
// of folding (mul (add x, c1), c2) -> (add (mul x, c2), c1*c2).
// MulNode is the original multiply, AddNode is (add x, c1),
// and ConstNode is c2.
//
// If the (add x, c1) has multiple uses, we could increase
// the number of adds if we make this transformation.
// It would only be worth doing this if we can remove a
// multiply in the process. Check for that here.
// To illustrate:
//     (A + c1) * c3
//     (A + c2) * c3
// We're checking for cases where we have common "c3 * A" expressions.
bool DAGCombiner::isMulAddWithConstProfitable(SDNode *MulNode,
                                              SDValue &AddNode,
                                              SDValue &ConstNode) {
  APInt Val;

  // If the add only has one use, this would be OK to do.
  if (AddNode.getNode()->hasOneUse())
    return true;

  // Walk all the users of the constant with which we're multiplying.
  for (SDNode *Use : ConstNode->uses()) {
    if (Use == MulNode) // This use is the one we're on right now. Skip it.
      continue;

    if (Use->getOpcode() == ISD::MUL) { // We have another multiply use.
      SDNode *OtherOp;
      SDNode *MulVar = AddNode.getOperand(0).getNode();

      // OtherOp is what we're multiplying against the constant.
      if (Use->getOperand(0) == ConstNode)
        OtherOp = Use->getOperand(1).getNode();
      else
        OtherOp = Use->getOperand(0).getNode();

      // Check to see if multiply is with the same operand of our "add".
      //
      //     ConstNode  = CONST
      //     Use = ConstNode * A  <-- visiting Use. OtherOp is A.
      //     ...
      //     AddNode  = (A + c1)  <-- MulVar is A.
      //         = AddNode * ConstNode   <-- current visiting instruction.
      //
      // If we make this transformation, we will have a common
      // multiply (ConstNode * A) that we can save.
      if (OtherOp == MulVar)
        return true;

      // Now check to see if a future expansion will give us a common
      // multiply.
      //
      //     ConstNode  = CONST
      //     AddNode    = (A + c1)
      //     ...   = AddNode * ConstNode <-- current visiting instruction.
      //     ...
      //     OtherOp = (A + c2)
      //     Use     = OtherOp * ConstNode <-- visiting Use.
      //
      // If we make this transformation, we will have a common
      // multiply (CONST * A) after we also do the same transformation
      // to the "t2" instruction.
      if (OtherOp->getOpcode() == ISD::ADD &&
          DAG.isConstantIntBuildVectorOrConstantInt(OtherOp->getOperand(1)) &&
          OtherOp->getOperand(0).getNode() == MulVar)
        return true;
    }
  }

  // Didn't find a case where this would be profitable.
  return false;
}

SDValue DAGCombiner::getMergeStoreChains(SmallVectorImpl<MemOpLink> &StoreNodes,
                                         unsigned NumStores) {
  SmallVector<SDValue, 8> Chains;
  SmallPtrSet<const SDNode *, 8> Visited;
  SDLoc StoreDL(StoreNodes[0].MemNode);

  for (unsigned i = 0; i < NumStores; ++i) {
    Visited.insert(StoreNodes[i].MemNode);
  }

  // don't include nodes that are children
  for (unsigned i = 0; i < NumStores; ++i) {
    if (Visited.count(StoreNodes[i].MemNode->getChain().getNode()) == 0)
      Chains.push_back(StoreNodes[i].MemNode->getChain());
  }

  assert(Chains.size() > 0 && "Chain should have generated a chain");
  return DAG.getNode(ISD::TokenFactor, StoreDL, MVT::Other, Chains);
}

bool DAGCombiner::MergeStoresOfConstantsOrVecElts(
    SmallVectorImpl<MemOpLink> &StoreNodes, EVT MemVT, unsigned NumStores,
    bool IsConstantSrc, bool UseVector, bool UseTrunc) {
  // Make sure we have something to merge.
  if (NumStores < 2)
    return false;

  // The latest Node in the DAG.
  SDLoc DL(StoreNodes[0].MemNode);

  int64_t ElementSizeBits = MemVT.getStoreSizeInBits();
  unsigned SizeInBits = NumStores * ElementSizeBits;
  unsigned NumMemElts = MemVT.isVector() ? MemVT.getVectorNumElements() : 1;

  EVT StoreTy;
  if (UseVector) {
    unsigned Elts = NumStores * NumMemElts;
    // Get the type for the merged vector store.
    StoreTy = EVT::getVectorVT(*DAG.getContext(), MemVT.getScalarType(), Elts);
  } else
    StoreTy = EVT::getIntegerVT(*DAG.getContext(), SizeInBits);

  SDValue StoredVal;
  if (UseVector) {
    if (IsConstantSrc) {
      SmallVector<SDValue, 8> BuildVector;
      for (unsigned I = 0; I != NumStores; ++I) {
        StoreSDNode *St = cast<StoreSDNode>(StoreNodes[I].MemNode);
        SDValue Val = St->getValue();
        // If constant is of the wrong type, convert it now.
        if (MemVT != Val.getValueType()) {
          Val = peekThroughBitcasts(Val);
          // Deal with constants of wrong size.
          if (ElementSizeBits != Val.getValueSizeInBits()) {
            EVT IntMemVT =
                EVT::getIntegerVT(*DAG.getContext(), MemVT.getSizeInBits());
            if (isa<ConstantFPSDNode>(Val)) {
              // Not clear how to truncate FP values.
              return false;
            } else if (auto *C = dyn_cast<ConstantSDNode>(Val))
              Val = DAG.getConstant(C->getAPIntValue()
                                        .zextOrTrunc(Val.getValueSizeInBits())
                                        .zextOrTrunc(ElementSizeBits),
                                    SDLoc(C), IntMemVT);
          }
          // Make sure correctly size type is the correct type.
          Val = DAG.getBitcast(MemVT, Val);
        }
        BuildVector.push_back(Val);
      }
      StoredVal = DAG.getNode(MemVT.isVector() ? ISD::CONCAT_VECTORS
                                               : ISD::BUILD_VECTOR,
                              DL, StoreTy, BuildVector);
    } else {
      SmallVector<SDValue, 8> Ops;
      for (unsigned i = 0; i < NumStores; ++i) {
        StoreSDNode *St = cast<StoreSDNode>(StoreNodes[i].MemNode);
        SDValue Val = peekThroughBitcasts(St->getValue());
        // All operands of BUILD_VECTOR / CONCAT_VECTOR must be of
        // type MemVT. If the underlying value is not the correct
        // type, but it is an extraction of an appropriate vector we
        // can recast Val to be of the correct type. This may require
        // converting between EXTRACT_VECTOR_ELT and
        // EXTRACT_SUBVECTOR.
        if ((MemVT != Val.getValueType()) &&
            (Val.getOpcode() == ISD::EXTRACT_VECTOR_ELT ||
             Val.getOpcode() == ISD::EXTRACT_SUBVECTOR)) {
          EVT MemVTScalarTy = MemVT.getScalarType();
          // We may need to add a bitcast here to get types to line up.
          if (MemVTScalarTy != Val.getValueType().getScalarType()) {
            Val = DAG.getBitcast(MemVT, Val);
          } else {
            unsigned OpC = MemVT.isVector() ? ISD::EXTRACT_SUBVECTOR
                                            : ISD::EXTRACT_VECTOR_ELT;
            SDValue Vec = Val.getOperand(0);
            SDValue Idx = Val.getOperand(1);
            Val = DAG.getNode(OpC, SDLoc(Val), MemVT, Vec, Idx);
          }
        }
        Ops.push_back(Val);
      }

      // Build the extracted vector elements back into a vector.
      StoredVal = DAG.getNode(MemVT.isVector() ? ISD::CONCAT_VECTORS
                                               : ISD::BUILD_VECTOR,
                              DL, StoreTy, Ops);
    }
  } else {
    // We should always use a vector store when merging extracted vector
    // elements, so this path implies a store of constants.
    assert(IsConstantSrc && "Merged vector elements should use vector store");

    APInt StoreInt(SizeInBits, 0);

    // Construct a single integer constant which is made of the smaller
    // constant inputs.
    bool IsLE = DAG.getDataLayout().isLittleEndian();
    for (unsigned i = 0; i < NumStores; ++i) {
      unsigned Idx = IsLE ? (NumStores - 1 - i) : i;
      StoreSDNode *St  = cast<StoreSDNode>(StoreNodes[Idx].MemNode);

      SDValue Val = St->getValue();
      Val = peekThroughBitcasts(Val);
      StoreInt <<= ElementSizeBits;
      if (ConstantSDNode *C = dyn_cast<ConstantSDNode>(Val)) {
        StoreInt |= C->getAPIntValue()
                        .zextOrTrunc(ElementSizeBits)
                        .zextOrTrunc(SizeInBits);
      } else if (ConstantFPSDNode *C = dyn_cast<ConstantFPSDNode>(Val)) {
        StoreInt |= C->getValueAPF()
                        .bitcastToAPInt()
                        .zextOrTrunc(ElementSizeBits)
                        .zextOrTrunc(SizeInBits);
        // If fp truncation is necessary give up for now.
        if (MemVT.getSizeInBits() != ElementSizeBits)
          return false;
      } else {
        llvm_unreachable("Invalid constant element type");
      }
    }

    // Create the new Load and Store operations.
    StoredVal = DAG.getConstant(StoreInt, DL, StoreTy);
  }

  LSBaseSDNode *FirstInChain = StoreNodes[0].MemNode;
  SDValue NewChain = getMergeStoreChains(StoreNodes, NumStores);

  // make sure we use trunc store if it's necessary to be legal.
  SDValue NewStore;
  if (!UseTrunc) {
    NewStore = DAG.getStore(NewChain, DL, StoredVal, FirstInChain->getBasePtr(),
                            FirstInChain->getPointerInfo(),
                            FirstInChain->getAlignment());
  } else { // Must be realized as a trunc store
    EVT LegalizedStoredValTy =
        TLI.getTypeToTransformTo(*DAG.getContext(), StoredVal.getValueType());
    unsigned LegalizedStoreSize = LegalizedStoredValTy.getSizeInBits();
    ConstantSDNode *C = cast<ConstantSDNode>(StoredVal);
    SDValue ExtendedStoreVal =
        DAG.getConstant(C->getAPIntValue().zextOrTrunc(LegalizedStoreSize), DL,
                        LegalizedStoredValTy);
    NewStore = DAG.getTruncStore(
        NewChain, DL, ExtendedStoreVal, FirstInChain->getBasePtr(),
        FirstInChain->getPointerInfo(), StoredVal.getValueType() /*TVT*/,
        FirstInChain->getAlignment(),
        FirstInChain->getMemOperand()->getFlags());
  }

  // Replace all merged stores with the new store.
  for (unsigned i = 0; i < NumStores; ++i)
    CombineTo(StoreNodes[i].MemNode, NewStore);

  AddToWorklist(NewChain.getNode());
  return true;
}

void DAGCombiner::getStoreMergeCandidates(
    StoreSDNode *St, SmallVectorImpl<MemOpLink> &StoreNodes,
    SDNode *&RootNode) {
  // This holds the base pointer, index, and the offset in bytes from the base
  // pointer.
  BaseIndexOffset BasePtr = BaseIndexOffset::match(St, DAG);
  EVT MemVT = St->getMemoryVT();

  SDValue Val = peekThroughBitcasts(St->getValue());
  // We must have a base and an offset.
  if (!BasePtr.getBase().getNode())
    return;

  // Do not handle stores to undef base pointers.
  if (BasePtr.getBase().isUndef())
    return;

  bool IsConstantSrc = isa<ConstantSDNode>(Val) || isa<ConstantFPSDNode>(Val);
  bool IsExtractVecSrc = (Val.getOpcode() == ISD::EXTRACT_VECTOR_ELT ||
                          Val.getOpcode() == ISD::EXTRACT_SUBVECTOR);
  bool IsLoadSrc = isa<LoadSDNode>(Val);
  BaseIndexOffset LBasePtr;
  // Match on loadbaseptr if relevant.
  EVT LoadVT;
  if (IsLoadSrc) {
    auto *Ld = cast<LoadSDNode>(Val);
    LBasePtr = BaseIndexOffset::match(Ld, DAG);
    LoadVT = Ld->getMemoryVT();
    // Load and store should be the same type.
    if (MemVT != LoadVT)
      return;
    // Loads must only have one use.
    if (!Ld->hasNUsesOfValue(1, 0))
      return;
    // The memory operands must not be volatile.
    if (Ld->isVolatile() || Ld->isIndexed())
      return;
  }
  auto CandidateMatch = [&](StoreSDNode *Other, BaseIndexOffset &Ptr,
                            int64_t &Offset) -> bool {
    if (Other->isVolatile() || Other->isIndexed())
      return false;
    SDValue Val = peekThroughBitcasts(Other->getValue());
    // Allow merging constants of different types as integers.
    bool NoTypeMatch = (MemVT.isInteger()) ? !MemVT.bitsEq(Other->getMemoryVT())
                                           : Other->getMemoryVT() != MemVT;
    if (IsLoadSrc) {
      if (NoTypeMatch)
        return false;
      // The Load's Base Ptr must also match
      if (LoadSDNode *OtherLd = dyn_cast<LoadSDNode>(Val)) {
        auto LPtr = BaseIndexOffset::match(OtherLd, DAG);
        if (LoadVT != OtherLd->getMemoryVT())
          return false;
        // Loads must only have one use.
        if (!OtherLd->hasNUsesOfValue(1, 0))
          return false;
        // The memory operands must not be volatile.
        if (OtherLd->isVolatile() || OtherLd->isIndexed())
          return false;
        if (!(LBasePtr.equalBaseIndex(LPtr, DAG)))
          return false;
      } else
        return false;
    }
    if (IsConstantSrc) {
      if (NoTypeMatch)
        return false;
      if (!(isa<ConstantSDNode>(Val) || isa<ConstantFPSDNode>(Val)))
        return false;
    }
    if (IsExtractVecSrc) {
      // Do not merge truncated stores here.
      if (Other->isTruncatingStore())
        return false;
      if (!MemVT.bitsEq(Val.getValueType()))
        return false;
      if (Val.getOpcode() != ISD::EXTRACT_VECTOR_ELT &&
          Val.getOpcode() != ISD::EXTRACT_SUBVECTOR)
        return false;
    }
    Ptr = BaseIndexOffset::match(Other, DAG);
    return (BasePtr.equalBaseIndex(Ptr, DAG, Offset));
  };

  // We looking for a root node which is an ancestor to all mergable
  // stores. We search up through a load, to our root and then down
  // through all children. For instance we will find Store{1,2,3} if
  // St is Store1, Store2. or Store3 where the root is not a load
  // which always true for nonvolatile ops. TODO: Expand
  // the search to find all valid candidates through multiple layers of loads.
  //
  // Root
  // |-------|-------|
  // Load    Load    Store3
  // |       |
  // Store1   Store2
  //
  // FIXME: We should be able to climb and
  // descend TokenFactors to find candidates as well.

  RootNode = St->getChain().getNode();

  if (LoadSDNode *Ldn = dyn_cast<LoadSDNode>(RootNode)) {
    RootNode = Ldn->getChain().getNode();
    for (auto I = RootNode->use_begin(), E = RootNode->use_end(); I != E; ++I)
      if (I.getOperandNo() == 0 && isa<LoadSDNode>(*I)) // walk down chain
        for (auto I2 = (*I)->use_begin(), E2 = (*I)->use_end(); I2 != E2; ++I2)
          if (I2.getOperandNo() == 0)
            if (StoreSDNode *OtherST = dyn_cast<StoreSDNode>(*I2)) {
              BaseIndexOffset Ptr;
              int64_t PtrDiff;
              if (CandidateMatch(OtherST, Ptr, PtrDiff))
                StoreNodes.push_back(MemOpLink(OtherST, PtrDiff));
            }
  } else
    for (auto I = RootNode->use_begin(), E = RootNode->use_end(); I != E; ++I)
      if (I.getOperandNo() == 0)
        if (StoreSDNode *OtherST = dyn_cast<StoreSDNode>(*I)) {
          BaseIndexOffset Ptr;
          int64_t PtrDiff;
          if (CandidateMatch(OtherST, Ptr, PtrDiff))
            StoreNodes.push_back(MemOpLink(OtherST, PtrDiff));
        }
}

// We need to check that merging these stores does not cause a loop in
// the DAG. Any store candidate may depend on another candidate
// indirectly through its operand (we already consider dependencies
// through the chain). Check in parallel by searching up from
// non-chain operands of candidates.
bool DAGCombiner::checkMergeStoreCandidatesForDependencies(
    SmallVectorImpl<MemOpLink> &StoreNodes, unsigned NumStores,
    SDNode *RootNode) {
  // FIXME: We should be able to truncate a full search of
  // predecessors by doing a BFS and keeping tabs the originating
  // stores from which worklist nodes come from in a similar way to
  // TokenFactor simplfication.

  SmallPtrSet<const SDNode *, 32> Visited;
  SmallVector<const SDNode *, 8> Worklist;

  // RootNode is a predecessor to all candidates so we need not search
  // past it. Add RootNode (peeking through TokenFactors). Do not count
  // these towards size check.

  Worklist.push_back(RootNode);
  while (!Worklist.empty()) {
    auto N = Worklist.pop_back_val();
    if (!Visited.insert(N).second)
      continue; // Already present in Visited.
    if (N->getOpcode() == ISD::TokenFactor) {
      for (SDValue Op : N->ops())
        Worklist.push_back(Op.getNode());
    }
  }

  // Don't count pruning nodes towards max.
  unsigned int Max = 1024 + Visited.size();
  // Search Ops of store candidates.
  for (unsigned i = 0; i < NumStores; ++i) {
    SDNode *N = StoreNodes[i].MemNode;
    // Of the 4 Store Operands:
    //   * Chain (Op 0) -> We have already considered these
    //                    in candidate selection and can be
    //                    safely ignored
    //   * Value (Op 1) -> Cycles may happen (e.g. through load chains)
    //   * Address (Op 2) -> Merged addresses may only vary by a fixed constant,
    //                       but aren't necessarily fromt the same base node, so
    //                       cycles possible (e.g. via indexed store).
    //   * (Op 3) -> Represents the pre or post-indexing offset (or undef for
    //               non-indexed stores). Not constant on all targets (e.g. ARM)
    //               and so can participate in a cycle.
    for (unsigned j = 1; j < N->getNumOperands(); ++j)
      Worklist.push_back(N->getOperand(j).getNode());
  }
  // Search through DAG. We can stop early if we find a store node.
  for (unsigned i = 0; i < NumStores; ++i)
    if (SDNode::hasPredecessorHelper(StoreNodes[i].MemNode, Visited, Worklist,
                                     Max))
      return false;
  return true;
}

bool DAGCombiner::MergeConsecutiveStores(StoreSDNode *St) {
  if (OptLevel == CodeGenOpt::None)
    return false;

  EVT MemVT = St->getMemoryVT();
  int64_t ElementSizeBytes = MemVT.getStoreSize();
  unsigned NumMemElts = MemVT.isVector() ? MemVT.getVectorNumElements() : 1;

  if (MemVT.getSizeInBits() * 2 > MaximumLegalStoreInBits)
    return false;

  bool NoVectors = DAG.getMachineFunction().getFunction().hasFnAttribute(
      Attribute::NoImplicitFloat);

  // This function cannot currently deal with non-byte-sized memory sizes.
  if (ElementSizeBytes * 8 != MemVT.getSizeInBits())
    return false;

  if (!MemVT.isSimple())
    return false;

  // Perform an early exit check. Do not bother looking at stored values that
  // are not constants, loads, or extracted vector elements.
  SDValue StoredVal = peekThroughBitcasts(St->getValue());
  bool IsLoadSrc = isa<LoadSDNode>(StoredVal);
  bool IsConstantSrc = isa<ConstantSDNode>(StoredVal) ||
                       isa<ConstantFPSDNode>(StoredVal);
  bool IsExtractVecSrc = (StoredVal.getOpcode() == ISD::EXTRACT_VECTOR_ELT ||
                          StoredVal.getOpcode() == ISD::EXTRACT_SUBVECTOR);

  if (!IsConstantSrc && !IsLoadSrc && !IsExtractVecSrc)
    return false;

  SmallVector<MemOpLink, 8> StoreNodes;
  SDNode *RootNode;
  // Find potential store merge candidates by searching through chain sub-DAG
  getStoreMergeCandidates(St, StoreNodes, RootNode);

  // Check if there is anything to merge.
  if (StoreNodes.size() < 2)
    return false;

  // Sort the memory operands according to their distance from the
  // base pointer.
  llvm::sort(StoreNodes, [](MemOpLink LHS, MemOpLink RHS) {
    return LHS.OffsetFromBase < RHS.OffsetFromBase;
  });

  // Store Merge attempts to merge the lowest stores. This generally
  // works out as if successful, as the remaining stores are checked
  // after the first collection of stores is merged. However, in the
  // case that a non-mergeable store is found first, e.g., {p[-2],
  // p[0], p[1], p[2], p[3]}, we would fail and miss the subsequent
  // mergeable cases. To prevent this, we prune such stores from the
  // front of StoreNodes here.

  bool RV = false;
  while (StoreNodes.size() > 1) {
    unsigned StartIdx = 0;
    while ((StartIdx + 1 < StoreNodes.size()) &&
           StoreNodes[StartIdx].OffsetFromBase + ElementSizeBytes !=
               StoreNodes[StartIdx + 1].OffsetFromBase)
      ++StartIdx;

    // Bail if we don't have enough candidates to merge.
    if (StartIdx + 1 >= StoreNodes.size())
      return RV;

    if (StartIdx)
      StoreNodes.erase(StoreNodes.begin(), StoreNodes.begin() + StartIdx);

    // Scan the memory operations on the chain and find the first
    // non-consecutive store memory address.
    unsigned NumConsecutiveStores = 1;
    int64_t StartAddress = StoreNodes[0].OffsetFromBase;
    // Check that the addresses are consecutive starting from the second
    // element in the list of stores.
    for (unsigned i = 1, e = StoreNodes.size(); i < e; ++i) {
      int64_t CurrAddress = StoreNodes[i].OffsetFromBase;
      if (CurrAddress - StartAddress != (ElementSizeBytes * i))
        break;
      NumConsecutiveStores = i + 1;
    }

    if (NumConsecutiveStores < 2) {
      StoreNodes.erase(StoreNodes.begin(),
                       StoreNodes.begin() + NumConsecutiveStores);
      continue;
    }

    // The node with the lowest store address.
    LLVMContext &Context = *DAG.getContext();
    const DataLayout &DL = DAG.getDataLayout();

    // Store the constants into memory as one consecutive store.
    if (IsConstantSrc) {
      while (NumConsecutiveStores >= 2) {
        LSBaseSDNode *FirstInChain = StoreNodes[0].MemNode;
        unsigned FirstStoreAS = FirstInChain->getAddressSpace();
        unsigned FirstStoreAlign = FirstInChain->getAlignment();
        unsigned LastLegalType = 1;
        unsigned LastLegalVectorType = 1;
        bool LastIntegerTrunc = false;
        bool NonZero = false;
        unsigned FirstZeroAfterNonZero = NumConsecutiveStores;
        for (unsigned i = 0; i < NumConsecutiveStores; ++i) {
          StoreSDNode *ST = cast<StoreSDNode>(StoreNodes[i].MemNode);
          SDValue StoredVal = ST->getValue();
          bool IsElementZero = false;
          if (ConstantSDNode *C = dyn_cast<ConstantSDNode>(StoredVal))
            IsElementZero = C->isNullValue();
          else if (ConstantFPSDNode *C = dyn_cast<ConstantFPSDNode>(StoredVal))
            IsElementZero = C->getConstantFPValue()->isNullValue();
          if (IsElementZero) {
            if (NonZero && FirstZeroAfterNonZero == NumConsecutiveStores)
              FirstZeroAfterNonZero = i;
          }
          NonZero |= !IsElementZero;

          // Find a legal type for the constant store.
          unsigned SizeInBits = (i + 1) * ElementSizeBytes * 8;
          EVT StoreTy = EVT::getIntegerVT(Context, SizeInBits);
          bool IsFast = false;

          // Break early when size is too large to be legal.
          if (StoreTy.getSizeInBits() > MaximumLegalStoreInBits)
            break;

          if (TLI.isTypeLegal(StoreTy) &&
              TLI.canMergeStoresTo(FirstStoreAS, StoreTy, DAG) &&
              TLI.allowsMemoryAccess(Context, DL, StoreTy, FirstStoreAS,
                                     FirstStoreAlign, &IsFast) &&
              IsFast) {
            LastIntegerTrunc = false;
            LastLegalType = i + 1;
            // Or check whether a truncstore is legal.
          } else if (TLI.getTypeAction(Context, StoreTy) ==
                     TargetLowering::TypePromoteInteger) {
            EVT LegalizedStoredValTy =
                TLI.getTypeToTransformTo(Context, StoredVal.getValueType());
            if (TLI.isTruncStoreLegal(LegalizedStoredValTy, StoreTy) &&
                TLI.canMergeStoresTo(FirstStoreAS, LegalizedStoredValTy, DAG) &&
                TLI.allowsMemoryAccess(Context, DL, StoreTy, FirstStoreAS,
                                       FirstStoreAlign, &IsFast) &&
                IsFast) {
              LastIntegerTrunc = true;
              LastLegalType = i + 1;
            }
          }

          // We only use vectors if the constant is known to be zero or the
          // target allows it and the function is not marked with the
          // noimplicitfloat attribute.
          if ((!NonZero ||
               TLI.storeOfVectorConstantIsCheap(MemVT, i + 1, FirstStoreAS)) &&
              !NoVectors) {
            // Find a legal type for the vector store.
            unsigned Elts = (i + 1) * NumMemElts;
            EVT Ty = EVT::getVectorVT(Context, MemVT.getScalarType(), Elts);
            if (TLI.isTypeLegal(Ty) && TLI.isTypeLegal(MemVT) &&
                TLI.canMergeStoresTo(FirstStoreAS, Ty, DAG) &&
                TLI.allowsMemoryAccess(Context, DL, Ty, FirstStoreAS,
                                       FirstStoreAlign, &IsFast) &&
                IsFast)
              LastLegalVectorType = i + 1;
          }
        }

        bool UseVector = (LastLegalVectorType > LastLegalType) && !NoVectors;
        unsigned NumElem = (UseVector) ? LastLegalVectorType : LastLegalType;

        // Check if we found a legal integer type that creates a meaningful
        // merge.
        if (NumElem < 2) {
          // We know that candidate stores are in order and of correct
          // shape. While there is no mergeable sequence from the
          // beginning one may start later in the sequence. The only
          // reason a merge of size N could have failed where another of
          // the same size would not have, is if the alignment has
          // improved or we've dropped a non-zero value. Drop as many
          // candidates as we can here.
          unsigned NumSkip = 1;
          while (
              (NumSkip < NumConsecutiveStores) &&
              (NumSkip < FirstZeroAfterNonZero) &&
              (StoreNodes[NumSkip].MemNode->getAlignment() <= FirstStoreAlign))
            NumSkip++;

          StoreNodes.erase(StoreNodes.begin(), StoreNodes.begin() + NumSkip);
          NumConsecutiveStores -= NumSkip;
          continue;
        }

        // Check that we can merge these candidates without causing a cycle.
        if (!checkMergeStoreCandidatesForDependencies(StoreNodes, NumElem,
                                                      RootNode)) {
          StoreNodes.erase(StoreNodes.begin(), StoreNodes.begin() + NumElem);
          NumConsecutiveStores -= NumElem;
          continue;
        }

        RV |= MergeStoresOfConstantsOrVecElts(StoreNodes, MemVT, NumElem, true,
                                              UseVector, LastIntegerTrunc);

        // Remove merged stores for next iteration.
        StoreNodes.erase(StoreNodes.begin(), StoreNodes.begin() + NumElem);
        NumConsecutiveStores -= NumElem;
      }
      continue;
    }

    // When extracting multiple vector elements, try to store them
    // in one vector store rather than a sequence of scalar stores.
    if (IsExtractVecSrc) {
      // Loop on Consecutive Stores on success.
      while (NumConsecutiveStores >= 2) {
        LSBaseSDNode *FirstInChain = StoreNodes[0].MemNode;
        unsigned FirstStoreAS = FirstInChain->getAddressSpace();
        unsigned FirstStoreAlign = FirstInChain->getAlignment();
        unsigned NumStoresToMerge = 1;
        for (unsigned i = 0; i < NumConsecutiveStores; ++i) {
          // Find a legal type for the vector store.
          unsigned Elts = (i + 1) * NumMemElts;
          EVT Ty =
              EVT::getVectorVT(*DAG.getContext(), MemVT.getScalarType(), Elts);
          bool IsFast;

          // Break early when size is too large to be legal.
          if (Ty.getSizeInBits() > MaximumLegalStoreInBits)
            break;

          if (TLI.isTypeLegal(Ty) &&
              TLI.canMergeStoresTo(FirstStoreAS, Ty, DAG) &&
              TLI.allowsMemoryAccess(Context, DL, Ty, FirstStoreAS,
                                     FirstStoreAlign, &IsFast) &&
              IsFast)
            NumStoresToMerge = i + 1;
        }

        // Check if we found a legal integer type creating a meaningful
        // merge.
        if (NumStoresToMerge < 2) {
          // We know that candidate stores are in order and of correct
          // shape. While there is no mergeable sequence from the
          // beginning one may start later in the sequence. The only
          // reason a merge of size N could have failed where another of
          // the same size would not have, is if the alignment has
          // improved. Drop as many candidates as we can here.
          unsigned NumSkip = 1;
          while (
              (NumSkip < NumConsecutiveStores) &&
              (StoreNodes[NumSkip].MemNode->getAlignment() <= FirstStoreAlign))
            NumSkip++;

          StoreNodes.erase(StoreNodes.begin(), StoreNodes.begin() + NumSkip);
          NumConsecutiveStores -= NumSkip;
          continue;
        }

        // Check that we can merge these candidates without causing a cycle.
        if (!checkMergeStoreCandidatesForDependencies(
                StoreNodes, NumStoresToMerge, RootNode)) {
          StoreNodes.erase(StoreNodes.begin(),
                           StoreNodes.begin() + NumStoresToMerge);
          NumConsecutiveStores -= NumStoresToMerge;
          continue;
        }

        RV |= MergeStoresOfConstantsOrVecElts(
            StoreNodes, MemVT, NumStoresToMerge, false, true, false);

        StoreNodes.erase(StoreNodes.begin(),
                         StoreNodes.begin() + NumStoresToMerge);
        NumConsecutiveStores -= NumStoresToMerge;
      }
      continue;
    }

    // Below we handle the case of multiple consecutive stores that
    // come from multiple consecutive loads. We merge them into a single
    // wide load and a single wide store.

    // Look for load nodes which are used by the stored values.
    SmallVector<MemOpLink, 8> LoadNodes;

    // Find acceptable loads. Loads need to have the same chain (token factor),
    // must not be zext, volatile, indexed, and they must be consecutive.
    BaseIndexOffset LdBasePtr;

    for (unsigned i = 0; i < NumConsecutiveStores; ++i) {
      StoreSDNode *St = cast<StoreSDNode>(StoreNodes[i].MemNode);
      SDValue Val = peekThroughBitcasts(St->getValue());
      LoadSDNode *Ld = cast<LoadSDNode>(Val);

      BaseIndexOffset LdPtr = BaseIndexOffset::match(Ld, DAG);
      // If this is not the first ptr that we check.
      int64_t LdOffset = 0;
      if (LdBasePtr.getBase().getNode()) {
        // The base ptr must be the same.
        if (!LdBasePtr.equalBaseIndex(LdPtr, DAG, LdOffset))
          break;
      } else {
        // Check that all other base pointers are the same as this one.
        LdBasePtr = LdPtr;
      }

      // We found a potential memory operand to merge.
      LoadNodes.push_back(MemOpLink(Ld, LdOffset));
    }

    while (NumConsecutiveStores >= 2 && LoadNodes.size() >= 2) {
      // If we have load/store pair instructions and we only have two values,
      // don't bother merging.
      unsigned RequiredAlignment;
      if (LoadNodes.size() == 2 &&
          TLI.hasPairedLoad(MemVT, RequiredAlignment) &&
          StoreNodes[0].MemNode->getAlignment() >= RequiredAlignment) {
        StoreNodes.erase(StoreNodes.begin(), StoreNodes.begin() + 2);
        LoadNodes.erase(LoadNodes.begin(), LoadNodes.begin() + 2);
        break;
      }
      LSBaseSDNode *FirstInChain = StoreNodes[0].MemNode;
      unsigned FirstStoreAS = FirstInChain->getAddressSpace();
      unsigned FirstStoreAlign = FirstInChain->getAlignment();
      LoadSDNode *FirstLoad = cast<LoadSDNode>(LoadNodes[0].MemNode);
      unsigned FirstLoadAS = FirstLoad->getAddressSpace();
      unsigned FirstLoadAlign = FirstLoad->getAlignment();

      // Scan the memory operations on the chain and find the first
      // non-consecutive load memory address. These variables hold the index in
      // the store node array.

      unsigned LastConsecutiveLoad = 1;

      // This variable refers to the size and not index in the array.
      unsigned LastLegalVectorType = 1;
      unsigned LastLegalIntegerType = 1;
      bool isDereferenceable = true;
      bool DoIntegerTruncate = false;
      StartAddress = LoadNodes[0].OffsetFromBase;
      SDValue FirstChain = FirstLoad->getChain();
      for (unsigned i = 1; i < LoadNodes.size(); ++i) {
        // All loads must share the same chain.
        if (LoadNodes[i].MemNode->getChain() != FirstChain)
          break;

        int64_t CurrAddress = LoadNodes[i].OffsetFromBase;
        if (CurrAddress - StartAddress != (ElementSizeBytes * i))
          break;
        LastConsecutiveLoad = i;

        if (isDereferenceable && !LoadNodes[i].MemNode->isDereferenceable())
          isDereferenceable = false;

        // Find a legal type for the vector store.
        unsigned Elts = (i + 1) * NumMemElts;
        EVT StoreTy = EVT::getVectorVT(Context, MemVT.getScalarType(), Elts);

        // Break early when size is too large to be legal.
        if (StoreTy.getSizeInBits() > MaximumLegalStoreInBits)
          break;

        bool IsFastSt, IsFastLd;
        if (TLI.isTypeLegal(StoreTy) &&
            TLI.canMergeStoresTo(FirstStoreAS, StoreTy, DAG) &&
            TLI.allowsMemoryAccess(Context, DL, StoreTy, FirstStoreAS,
                                   FirstStoreAlign, &IsFastSt) &&
            IsFastSt &&
            TLI.allowsMemoryAccess(Context, DL, StoreTy, FirstLoadAS,
                                   FirstLoadAlign, &IsFastLd) &&
            IsFastLd) {
          LastLegalVectorType = i + 1;
        }

        // Find a legal type for the integer store.
        unsigned SizeInBits = (i + 1) * ElementSizeBytes * 8;
        StoreTy = EVT::getIntegerVT(Context, SizeInBits);
        if (TLI.isTypeLegal(StoreTy) &&
            TLI.canMergeStoresTo(FirstStoreAS, StoreTy, DAG) &&
            TLI.allowsMemoryAccess(Context, DL, StoreTy, FirstStoreAS,
                                   FirstStoreAlign, &IsFastSt) &&
            IsFastSt &&
            TLI.allowsMemoryAccess(Context, DL, StoreTy, FirstLoadAS,
                                   FirstLoadAlign, &IsFastLd) &&
            IsFastLd) {
          LastLegalIntegerType = i + 1;
          DoIntegerTruncate = false;
          // Or check whether a truncstore and extload is legal.
        } else if (TLI.getTypeAction(Context, StoreTy) ==
                   TargetLowering::TypePromoteInteger) {
          EVT LegalizedStoredValTy = TLI.getTypeToTransformTo(Context, StoreTy);
          if (TLI.isTruncStoreLegal(LegalizedStoredValTy, StoreTy) &&
              TLI.canMergeStoresTo(FirstStoreAS, LegalizedStoredValTy, DAG) &&
              TLI.isLoadExtLegal(ISD::ZEXTLOAD, LegalizedStoredValTy,
                                 StoreTy) &&
              TLI.isLoadExtLegal(ISD::SEXTLOAD, LegalizedStoredValTy,
                                 StoreTy) &&
              TLI.isLoadExtLegal(ISD::EXTLOAD, LegalizedStoredValTy, StoreTy) &&
              TLI.allowsMemoryAccess(Context, DL, StoreTy, FirstStoreAS,
                                     FirstStoreAlign, &IsFastSt) &&
              IsFastSt &&
              TLI.allowsMemoryAccess(Context, DL, StoreTy, FirstLoadAS,
                                     FirstLoadAlign, &IsFastLd) &&
              IsFastLd) {
            LastLegalIntegerType = i + 1;
            DoIntegerTruncate = true;
          }
        }
      }

      // Only use vector types if the vector type is larger than the integer
      // type. If they are the same, use integers.
      bool UseVectorTy =
          LastLegalVectorType > LastLegalIntegerType && !NoVectors;
      unsigned LastLegalType =
          std::max(LastLegalVectorType, LastLegalIntegerType);

      // We add +1 here because the LastXXX variables refer to location while
      // the NumElem refers to array/index size.
      unsigned NumElem =
          std::min(NumConsecutiveStores, LastConsecutiveLoad + 1);
      NumElem = std::min(LastLegalType, NumElem);

      if (NumElem < 2) {
        // We know that candidate stores are in order and of correct
        // shape. While there is no mergeable sequence from the
        // beginning one may start later in the sequence. The only
        // reason a merge of size N could have failed where another of
        // the same size would not have is if the alignment or either
        // the load or store has improved. Drop as many candidates as we
        // can here.
        unsigned NumSkip = 1;
        while ((NumSkip < LoadNodes.size()) &&
               (LoadNodes[NumSkip].MemNode->getAlignment() <= FirstLoadAlign) &&
               (StoreNodes[NumSkip].MemNode->getAlignment() <= FirstStoreAlign))
          NumSkip++;
        StoreNodes.erase(StoreNodes.begin(), StoreNodes.begin() + NumSkip);
        LoadNodes.erase(LoadNodes.begin(), LoadNodes.begin() + NumSkip);
        NumConsecutiveStores -= NumSkip;
        continue;
      }

      // Check that we can merge these candidates without causing a cycle.
      if (!checkMergeStoreCandidatesForDependencies(StoreNodes, NumElem,
                                                    RootNode)) {
        StoreNodes.erase(StoreNodes.begin(), StoreNodes.begin() + NumElem);
        LoadNodes.erase(LoadNodes.begin(), LoadNodes.begin() + NumElem);
        NumConsecutiveStores -= NumElem;
        continue;
      }

      // Find if it is better to use vectors or integers to load and store
      // to memory.
      EVT JointMemOpVT;
      if (UseVectorTy) {
        // Find a legal type for the vector store.
        unsigned Elts = NumElem * NumMemElts;
        JointMemOpVT = EVT::getVectorVT(Context, MemVT.getScalarType(), Elts);
      } else {
        unsigned SizeInBits = NumElem * ElementSizeBytes * 8;
        JointMemOpVT = EVT::getIntegerVT(Context, SizeInBits);
      }

      SDLoc LoadDL(LoadNodes[0].MemNode);
      SDLoc StoreDL(StoreNodes[0].MemNode);

      // The merged loads are required to have the same incoming chain, so
      // using the first's chain is acceptable.

      SDValue NewStoreChain = getMergeStoreChains(StoreNodes, NumElem);
      AddToWorklist(NewStoreChain.getNode());

      MachineMemOperand::Flags MMOFlags =
          isDereferenceable ? MachineMemOperand::MODereferenceable
                            : MachineMemOperand::MONone;

      SDValue NewLoad, NewStore;
      if (UseVectorTy || !DoIntegerTruncate) {
        NewLoad =
            DAG.getLoad(JointMemOpVT, LoadDL, FirstLoad->getChain(),
                        FirstLoad->getBasePtr(), FirstLoad->getPointerInfo(),
                        FirstLoadAlign, MMOFlags);
        NewStore = DAG.getStore(
            NewStoreChain, StoreDL, NewLoad, FirstInChain->getBasePtr(),
            FirstInChain->getPointerInfo(), FirstStoreAlign);
      } else { // This must be the truncstore/extload case
        EVT ExtendedTy =
            TLI.getTypeToTransformTo(*DAG.getContext(), JointMemOpVT);
        NewLoad = DAG.getExtLoad(ISD::EXTLOAD, LoadDL, ExtendedTy,
                                 FirstLoad->getChain(), FirstLoad->getBasePtr(),
                                 FirstLoad->getPointerInfo(), JointMemOpVT,
                                 FirstLoadAlign, MMOFlags);
        NewStore = DAG.getTruncStore(NewStoreChain, StoreDL, NewLoad,
                                     FirstInChain->getBasePtr(),
                                     FirstInChain->getPointerInfo(),
                                     JointMemOpVT, FirstInChain->getAlignment(),
                                     FirstInChain->getMemOperand()->getFlags());
      }

      // Transfer chain users from old loads to the new load.
      for (unsigned i = 0; i < NumElem; ++i) {
        LoadSDNode *Ld = cast<LoadSDNode>(LoadNodes[i].MemNode);
        DAG.ReplaceAllUsesOfValueWith(SDValue(Ld, 1),
                                      SDValue(NewLoad.getNode(), 1));
      }

      // Replace the all stores with the new store. Recursively remove
      // corresponding value if its no longer used.
      for (unsigned i = 0; i < NumElem; ++i) {
        SDValue Val = StoreNodes[i].MemNode->getOperand(1);
        CombineTo(StoreNodes[i].MemNode, NewStore);
        if (Val.getNode()->use_empty())
          recursivelyDeleteUnusedNodes(Val.getNode());
      }

      RV = true;
      StoreNodes.erase(StoreNodes.begin(), StoreNodes.begin() + NumElem);
      LoadNodes.erase(LoadNodes.begin(), LoadNodes.begin() + NumElem);
      NumConsecutiveStores -= NumElem;
    }
  }
  return RV;
}

SDValue DAGCombiner::replaceStoreChain(StoreSDNode *ST, SDValue BetterChain) {
  SDLoc SL(ST);
  SDValue ReplStore;

  // Replace the chain to avoid dependency.
  if (ST->isTruncatingStore()) {
    ReplStore = DAG.getTruncStore(BetterChain, SL, ST->getValue(),
                                  ST->getBasePtr(), ST->getMemoryVT(),
                                  ST->getMemOperand());
  } else {
    ReplStore = DAG.getStore(BetterChain, SL, ST->getValue(), ST->getBasePtr(),
                             ST->getMemOperand());
  }

  // Create token to keep both nodes around.
  SDValue Token = DAG.getNode(ISD::TokenFactor, SL,
                              MVT::Other, ST->getChain(), ReplStore);

  // Make sure the new and old chains are cleaned up.
  AddToWorklist(Token.getNode());

  // Don't add users to work list.
  return CombineTo(ST, Token, false);
}

SDValue DAGCombiner::replaceStoreOfFPConstant(StoreSDNode *ST) {
  SDValue Value = ST->getValue();
  if (Value.getOpcode() == ISD::TargetConstantFP)
    return SDValue();

  SDLoc DL(ST);

  SDValue Chain = ST->getChain();
  SDValue Ptr = ST->getBasePtr();

  const ConstantFPSDNode *CFP = cast<ConstantFPSDNode>(Value);

  // NOTE: If the original store is volatile, this transform must not increase
  // the number of stores.  For example, on x86-32 an f64 can be stored in one
  // processor operation but an i64 (which is not legal) requires two.  So the
  // transform should not be done in this case.

  SDValue Tmp;
  switch (CFP->getSimpleValueType(0).SimpleTy) {
  default:
    llvm_unreachable("Unknown FP type");
  case MVT::f16:    // We don't do this for these yet.
  case MVT::f80:
  case MVT::f128:
  case MVT::ppcf128:
    return SDValue();
  case MVT::f32:
    if ((isTypeLegal(MVT::i32) && !LegalOperations && !ST->isVolatile()) ||
        TLI.isOperationLegalOrCustom(ISD::STORE, MVT::i32)) {
      ;
      Tmp = DAG.getConstant((uint32_t)CFP->getValueAPF().
                            bitcastToAPInt().getZExtValue(), SDLoc(CFP),
                            MVT::i32);
      return DAG.getStore(Chain, DL, Tmp, Ptr, ST->getMemOperand());
    }

    return SDValue();
  case MVT::f64:
    if ((TLI.isTypeLegal(MVT::i64) && !LegalOperations &&
         !ST->isVolatile()) ||
        TLI.isOperationLegalOrCustom(ISD::STORE, MVT::i64)) {
      ;
      Tmp = DAG.getConstant(CFP->getValueAPF().bitcastToAPInt().
                            getZExtValue(), SDLoc(CFP), MVT::i64);
      return DAG.getStore(Chain, DL, Tmp,
                          Ptr, ST->getMemOperand());
    }

    if (!ST->isVolatile() &&
        TLI.isOperationLegalOrCustom(ISD::STORE, MVT::i32)) {
      // Many FP stores are not made apparent until after legalize, e.g. for
      // argument passing.  Since this is so common, custom legalize the
      // 64-bit integer store into two 32-bit stores.
      uint64_t Val = CFP->getValueAPF().bitcastToAPInt().getZExtValue();
      SDValue Lo = DAG.getConstant(Val & 0xFFFFFFFF, SDLoc(CFP), MVT::i32);
      SDValue Hi = DAG.getConstant(Val >> 32, SDLoc(CFP), MVT::i32);
      if (DAG.getDataLayout().isBigEndian())
        std::swap(Lo, Hi);

      unsigned Alignment = ST->getAlignment();
      MachineMemOperand::Flags MMOFlags = ST->getMemOperand()->getFlags();
      AAMDNodes AAInfo = ST->getAAInfo();

      SDValue St0 = DAG.getStore(Chain, DL, Lo, Ptr, ST->getPointerInfo(),
                                 ST->getAlignment(), MMOFlags, AAInfo);
      Ptr = DAG.getNode(ISD::ADD, DL, Ptr.getValueType(), Ptr,
                        DAG.getConstant(4, DL, Ptr.getValueType()));
      Alignment = MinAlign(Alignment, 4U);
      SDValue St1 = DAG.getStore(Chain, DL, Hi, Ptr,
                                 ST->getPointerInfo().getWithOffset(4),
                                 Alignment, MMOFlags, AAInfo);
      return DAG.getNode(ISD::TokenFactor, DL, MVT::Other,
                         St0, St1);
    }

    return SDValue();
  }
}

SDValue DAGCombiner::visitSTORE(SDNode *N) {
  StoreSDNode *ST  = cast<StoreSDNode>(N);
  SDValue Chain = ST->getChain();
  SDValue Value = ST->getValue();
  SDValue Ptr   = ST->getBasePtr();

  // If this is a store of a bit convert, store the input value if the
  // resultant store does not need a higher alignment than the original.
  if (Value.getOpcode() == ISD::BITCAST && !ST->isTruncatingStore() &&
      ST->isUnindexed()) {
    EVT SVT = Value.getOperand(0).getValueType();
    // If the store is volatile, we only want to change the store type if the
    // resulting store is legal. Otherwise we might increase the number of
    // memory accesses. We don't care if the original type was legal or not
    // as we assume software couldn't rely on the number of accesses of an
    // illegal type.
    if (((!LegalOperations && !ST->isVolatile()) ||
         TLI.isOperationLegal(ISD::STORE, SVT)) &&
        TLI.isStoreBitCastBeneficial(Value.getValueType(), SVT)) {
      unsigned OrigAlign = ST->getAlignment();
      bool Fast = false;
      if (TLI.allowsMemoryAccess(*DAG.getContext(), DAG.getDataLayout(), SVT,
                                 ST->getAddressSpace(), OrigAlign, &Fast) &&
          Fast) {
        return DAG.getStore(Chain, SDLoc(N), Value.getOperand(0), Ptr,
                            ST->getPointerInfo(), OrigAlign,
                            ST->getMemOperand()->getFlags(), ST->getAAInfo());
      }
    }
  }

  // Turn 'store undef, Ptr' -> nothing.
  if (Value.isUndef() && ST->isUnindexed())
    return Chain;

  // Try to infer better alignment information than the store already has.
  if (OptLevel != CodeGenOpt::None && ST->isUnindexed()) {
    if (unsigned Align = DAG.InferPtrAlignment(Ptr)) {
      if (Align > ST->getAlignment() && ST->getSrcValueOffset() % Align == 0) {
        SDValue NewStore =
            DAG.getTruncStore(Chain, SDLoc(N), Value, Ptr, ST->getPointerInfo(),
                              ST->getMemoryVT(), Align,
                              ST->getMemOperand()->getFlags(), ST->getAAInfo());
        // NewStore will always be N as we are only refining the alignment
        assert(NewStore.getNode() == N);
        (void)NewStore;
      }
    }
  }

  // Try transforming a pair floating point load / store ops to integer
  // load / store ops.
  if (SDValue NewST = TransformFPLoadStorePair(N))
    return NewST;

  if (ST->isUnindexed()) {
    // Walk up chain skipping non-aliasing memory nodes, on this store and any
    // adjacent stores.
    if (findBetterNeighborChains(ST)) {
      // replaceStoreChain uses CombineTo, which handled all of the worklist
      // manipulation. Return the original node to not do anything else.
      return SDValue(ST, 0);
    }
    Chain = ST->getChain();
  }

  // FIXME: is there such a thing as a truncating indexed store?
  if (ST->isTruncatingStore() && ST->isUnindexed() &&
      Value.getValueType().isInteger() &&
      (!isa<ConstantSDNode>(Value) ||
       !cast<ConstantSDNode>(Value)->isOpaque())) {
    // See if we can simplify the input to this truncstore with knowledge that
    // only the low bits are being used.  For example:
    // "truncstore (or (shl x, 8), y), i8"  -> "truncstore y, i8"
    SDValue Shorter = DAG.GetDemandedBits(
        Value, APInt::getLowBitsSet(Value.getScalarValueSizeInBits(),
                                    ST->getMemoryVT().getScalarSizeInBits()));
    AddToWorklist(Value.getNode());
    if (Shorter.getNode())
      return DAG.getTruncStore(Chain, SDLoc(N), Shorter,
                               Ptr, ST->getMemoryVT(), ST->getMemOperand());

    // Otherwise, see if we can simplify the operation with
    // SimplifyDemandedBits, which only works if the value has a single use.
    if (SimplifyDemandedBits(
            Value,
            APInt::getLowBitsSet(Value.getScalarValueSizeInBits(),
                                 ST->getMemoryVT().getScalarSizeInBits()))) {
      // Re-visit the store if anything changed and the store hasn't been merged
      // with another node (N is deleted) SimplifyDemandedBits will add Value's
      // node back to the worklist if necessary, but we also need to re-visit
      // the Store node itself.
      if (N->getOpcode() != ISD::DELETED_NODE)
        AddToWorklist(N);
      return SDValue(N, 0);
    }
  }

  // If this is a load followed by a store to the same location, then the store
  // is dead/noop.
  if (LoadSDNode *Ld = dyn_cast<LoadSDNode>(Value)) {
    if (Ld->getBasePtr() == Ptr && ST->getMemoryVT() == Ld->getMemoryVT() &&
        ST->isUnindexed() && !ST->isVolatile() &&
        // There can't be any side effects between the load and store, such as
        // a call or store.
        Chain.reachesChainWithoutSideEffects(SDValue(Ld, 1))) {
      // The store is dead, remove it.
      return Chain;
    }
  }

  if (StoreSDNode *ST1 = dyn_cast<StoreSDNode>(Chain)) {
    if (ST->isUnindexed() && !ST->isVolatile() && ST1->isUnindexed() &&
        !ST1->isVolatile() && ST1->getBasePtr() == Ptr &&
        ST->getMemoryVT() == ST1->getMemoryVT()) {
      // If this is a store followed by a store with the same value to the same
      // location, then the store is dead/noop.
      if (ST1->getValue() == Value) {
        // The store is dead, remove it.
        return Chain;
      }

      // If this is a store who's preceeding store to the same location
      // and no one other node is chained to that store we can effectively
      // drop the store. Do not remove stores to undef as they may be used as
      // data sinks.
      if (OptLevel != CodeGenOpt::None && ST1->hasOneUse() &&
          !ST1->getBasePtr().isUndef()) {
        // ST1 is fully overwritten and can be elided. Combine with it's chain
        // value.
        CombineTo(ST1, ST1->getChain());
        return SDValue();
      }
    }
  }

  // If this is an FP_ROUND or TRUNC followed by a store, fold this into a
  // truncating store.  We can do this even if this is already a truncstore.
  if ((Value.getOpcode() == ISD::FP_ROUND || Value.getOpcode() == ISD::TRUNCATE)
      && Value.getNode()->hasOneUse() && ST->isUnindexed() &&
      TLI.isTruncStoreLegal(Value.getOperand(0).getValueType(),
                            ST->getMemoryVT())) {
    return DAG.getTruncStore(Chain, SDLoc(N), Value.getOperand(0),
                             Ptr, ST->getMemoryVT(), ST->getMemOperand());
  }

  // Always perform this optimization before types are legal. If the target
  // prefers, also try this after legalization to catch stores that were created
  // by intrinsics or other nodes.
  if (!LegalTypes || (TLI.mergeStoresAfterLegalization())) {
    while (true) {
      // There can be multiple store sequences on the same chain.
      // Keep trying to merge store sequences until we are unable to do so
      // or until we merge the last store on the chain.
      bool Changed = MergeConsecutiveStores(ST);
      if (!Changed) break;
      // Return N as merge only uses CombineTo and no worklist clean
      // up is necessary.
      if (N->getOpcode() == ISD::DELETED_NODE || !isa<StoreSDNode>(N))
        return SDValue(N, 0);
    }
  }

  // Try transforming N to an indexed store.
  if (CombineToPreIndexedLoadStore(N) || CombineToPostIndexedLoadStore(N))
    return SDValue(N, 0);

  // Turn 'store float 1.0, Ptr' -> 'store int 0x12345678, Ptr'
  //
  // Make sure to do this only after attempting to merge stores in order to
  //  avoid changing the types of some subset of stores due to visit order,
  //  preventing their merging.
  if (isa<ConstantFPSDNode>(ST->getValue())) {
    if (SDValue NewSt = replaceStoreOfFPConstant(ST))
      return NewSt;
  }

  if (SDValue NewSt = splitMergedValStore(ST))
    return NewSt;

  return ReduceLoadOpStoreWidth(N);
}

/// For the instruction sequence of store below, F and I values
/// are bundled together as an i64 value before being stored into memory.
/// Sometimes it is more efficent to generate separate stores for F and I,
/// which can remove the bitwise instructions or sink them to colder places.
///
///   (store (or (zext (bitcast F to i32) to i64),
///              (shl (zext I to i64), 32)), addr)  -->
///   (store F, addr) and (store I, addr+4)
///
/// Similarly, splitting for other merged store can also be beneficial, like:
/// For pair of {i32, i32}, i64 store --> two i32 stores.
/// For pair of {i32, i16}, i64 store --> two i32 stores.
/// For pair of {i16, i16}, i32 store --> two i16 stores.
/// For pair of {i16, i8},  i32 store --> two i16 stores.
/// For pair of {i8, i8},   i16 store --> two i8 stores.
///
/// We allow each target to determine specifically which kind of splitting is
/// supported.
///
/// The store patterns are commonly seen from the simple code snippet below
/// if only std::make_pair(...) is sroa transformed before inlined into hoo.
///   void goo(const std::pair<int, float> &);
///   hoo() {
///     ...
///     goo(std::make_pair(tmp, ftmp));
///     ...
///   }
///
SDValue DAGCombiner::splitMergedValStore(StoreSDNode *ST) {
  if (OptLevel == CodeGenOpt::None)
    return SDValue();

  SDValue Val = ST->getValue();
  SDLoc DL(ST);

  // Match OR operand.
  if (!Val.getValueType().isScalarInteger() || Val.getOpcode() != ISD::OR)
    return SDValue();

  // Match SHL operand and get Lower and Higher parts of Val.
  SDValue Op1 = Val.getOperand(0);
  SDValue Op2 = Val.getOperand(1);
  SDValue Lo, Hi;
  if (Op1.getOpcode() != ISD::SHL) {
    std::swap(Op1, Op2);
    if (Op1.getOpcode() != ISD::SHL)
      return SDValue();
  }
  Lo = Op2;
  Hi = Op1.getOperand(0);
  if (!Op1.hasOneUse())
    return SDValue();

  // Match shift amount to HalfValBitSize.
  unsigned HalfValBitSize = Val.getValueSizeInBits() / 2;
  ConstantSDNode *ShAmt = dyn_cast<ConstantSDNode>(Op1.getOperand(1));
  if (!ShAmt || ShAmt->getAPIntValue() != HalfValBitSize)
    return SDValue();

  // Lo and Hi are zero-extended from int with size less equal than 32
  // to i64.
  if (Lo.getOpcode() != ISD::ZERO_EXTEND || !Lo.hasOneUse() ||
      !Lo.getOperand(0).getValueType().isScalarInteger() ||
      Lo.getOperand(0).getValueSizeInBits() > HalfValBitSize ||
      Hi.getOpcode() != ISD::ZERO_EXTEND || !Hi.hasOneUse() ||
      !Hi.getOperand(0).getValueType().isScalarInteger() ||
      Hi.getOperand(0).getValueSizeInBits() > HalfValBitSize)
    return SDValue();

  // Use the EVT of low and high parts before bitcast as the input
  // of target query.
  EVT LowTy = (Lo.getOperand(0).getOpcode() == ISD::BITCAST)
                  ? Lo.getOperand(0).getValueType()
                  : Lo.getValueType();
  EVT HighTy = (Hi.getOperand(0).getOpcode() == ISD::BITCAST)
                   ? Hi.getOperand(0).getValueType()
                   : Hi.getValueType();
  if (!TLI.isMultiStoresCheaperThanBitsMerge(LowTy, HighTy))
    return SDValue();

  // Start to split store.
  unsigned Alignment = ST->getAlignment();
  MachineMemOperand::Flags MMOFlags = ST->getMemOperand()->getFlags();
  AAMDNodes AAInfo = ST->getAAInfo();

  // Change the sizes of Lo and Hi's value types to HalfValBitSize.
  EVT VT = EVT::getIntegerVT(*DAG.getContext(), HalfValBitSize);
  Lo = DAG.getNode(ISD::ZERO_EXTEND, DL, VT, Lo.getOperand(0));
  Hi = DAG.getNode(ISD::ZERO_EXTEND, DL, VT, Hi.getOperand(0));

  SDValue Chain = ST->getChain();
  SDValue Ptr = ST->getBasePtr();
  // Lower value store.
  SDValue St0 = DAG.getStore(Chain, DL, Lo, Ptr, ST->getPointerInfo(),
                             ST->getAlignment(), MMOFlags, AAInfo);
  Ptr =
      DAG.getNode(ISD::ADD, DL, Ptr.getValueType(), Ptr,
                  DAG.getConstant(HalfValBitSize / 8, DL, Ptr.getValueType()));
  // Higher value store.
  SDValue St1 =
      DAG.getStore(St0, DL, Hi, Ptr,
                   ST->getPointerInfo().getWithOffset(HalfValBitSize / 8),
                   Alignment / 2, MMOFlags, AAInfo);
  return St1;
}

/// Convert a disguised subvector insertion into a shuffle:
/// insert_vector_elt V, (bitcast X from vector type), IdxC -->
/// bitcast(shuffle (bitcast V), (extended X), Mask)
/// Note: We do not use an insert_subvector node because that requires a legal
/// subvector type.
SDValue DAGCombiner::combineInsertEltToShuffle(SDNode *N, unsigned InsIndex) {
  SDValue InsertVal = N->getOperand(1);
  if (InsertVal.getOpcode() != ISD::BITCAST || !InsertVal.hasOneUse() ||
      !InsertVal.getOperand(0).getValueType().isVector())
    return SDValue();

  SDValue SubVec = InsertVal.getOperand(0);
  SDValue DestVec = N->getOperand(0);
  EVT SubVecVT = SubVec.getValueType();
  EVT VT = DestVec.getValueType();
  unsigned NumSrcElts = SubVecVT.getVectorNumElements();
  unsigned ExtendRatio = VT.getSizeInBits() / SubVecVT.getSizeInBits();
  unsigned NumMaskVals = ExtendRatio * NumSrcElts;

  // Step 1: Create a shuffle mask that implements this insert operation. The
  // vector that we are inserting into will be operand 0 of the shuffle, so
  // those elements are just 'i'. The inserted subvector is in the first
  // positions of operand 1 of the shuffle. Example:
  // insert v4i32 V, (v2i16 X), 2 --> shuffle v8i16 V', X', {0,1,2,3,8,9,6,7}
  SmallVector<int, 16> Mask(NumMaskVals);
  for (unsigned i = 0; i != NumMaskVals; ++i) {
    if (i / NumSrcElts == InsIndex)
      Mask[i] = (i % NumSrcElts) + NumMaskVals;
    else
      Mask[i] = i;
  }

  // Bail out if the target can not handle the shuffle we want to create.
  EVT SubVecEltVT = SubVecVT.getVectorElementType();
  EVT ShufVT = EVT::getVectorVT(*DAG.getContext(), SubVecEltVT, NumMaskVals);
  if (!TLI.isShuffleMaskLegal(Mask, ShufVT))
    return SDValue();

  // Step 2: Create a wide vector from the inserted source vector by appending
  // undefined elements. This is the same size as our destination vector.
  SDLoc DL(N);
  SmallVector<SDValue, 8> ConcatOps(ExtendRatio, DAG.getUNDEF(SubVecVT));
  ConcatOps[0] = SubVec;
  SDValue PaddedSubV = DAG.getNode(ISD::CONCAT_VECTORS, DL, ShufVT, ConcatOps);

  // Step 3: Shuffle in the padded subvector.
  SDValue DestVecBC = DAG.getBitcast(ShufVT, DestVec);
  SDValue Shuf = DAG.getVectorShuffle(ShufVT, DL, DestVecBC, PaddedSubV, Mask);
  AddToWorklist(PaddedSubV.getNode());
  AddToWorklist(DestVecBC.getNode());
  AddToWorklist(Shuf.getNode());
  return DAG.getBitcast(VT, Shuf);
}

SDValue DAGCombiner::visitINSERT_VECTOR_ELT(SDNode *N) {
  SDValue InVec = N->getOperand(0);
  SDValue InVal = N->getOperand(1);
  SDValue EltNo = N->getOperand(2);
  SDLoc DL(N);

  // If the inserted element is an UNDEF, just use the input vector.
  if (InVal.isUndef())
    return InVec;

  EVT VT = InVec.getValueType();
  unsigned NumElts = VT.getVectorNumElements();

  // Remove redundant insertions:
  // (insert_vector_elt x (extract_vector_elt x idx) idx) -> x
  if (InVal.getOpcode() == ISD::EXTRACT_VECTOR_ELT &&
      InVec == InVal.getOperand(0) && EltNo == InVal.getOperand(1))
    return InVec;

  auto *IndexC = dyn_cast<ConstantSDNode>(EltNo);
  if (!IndexC) {
    // If this is variable insert to undef vector, it might be better to splat:
    // inselt undef, InVal, EltNo --> build_vector < InVal, InVal, ... >
    if (InVec.isUndef() && TLI.shouldSplatInsEltVarIndex(VT)) {
      SmallVector<SDValue, 8> Ops(NumElts, InVal);
      return DAG.getBuildVector(VT, DL, Ops);
    }
    return SDValue();
  }

  // We must know which element is being inserted for folds below here.
  unsigned Elt = IndexC->getZExtValue();
  if (SDValue Shuf = combineInsertEltToShuffle(N, Elt))
    return Shuf;

  // Canonicalize insert_vector_elt dag nodes.
  // Example:
  // (insert_vector_elt (insert_vector_elt A, Idx0), Idx1)
  // -> (insert_vector_elt (insert_vector_elt A, Idx1), Idx0)
  //
  // Do this only if the child insert_vector node has one use; also
  // do this only if indices are both constants and Idx1 < Idx0.
  if (InVec.getOpcode() == ISD::INSERT_VECTOR_ELT && InVec.hasOneUse()
      && isa<ConstantSDNode>(InVec.getOperand(2))) {
    unsigned OtherElt = InVec.getConstantOperandVal(2);
    if (Elt < OtherElt) {
      // Swap nodes.
      SDValue NewOp = DAG.getNode(ISD::INSERT_VECTOR_ELT, DL, VT,
                                  InVec.getOperand(0), InVal, EltNo);
      AddToWorklist(NewOp.getNode());
      return DAG.getNode(ISD::INSERT_VECTOR_ELT, SDLoc(InVec.getNode()),
                         VT, NewOp, InVec.getOperand(1), InVec.getOperand(2));
    }
  }

  // If we can't generate a legal BUILD_VECTOR, exit
  if (LegalOperations && !TLI.isOperationLegal(ISD::BUILD_VECTOR, VT))
    return SDValue();

  // Check that the operand is a BUILD_VECTOR (or UNDEF, which can essentially
  // be converted to a BUILD_VECTOR).  Fill in the Ops vector with the
  // vector elements.
  SmallVector<SDValue, 8> Ops;
  // Do not combine these two vectors if the output vector will not replace
  // the input vector.
  if (InVec.getOpcode() == ISD::BUILD_VECTOR && InVec.hasOneUse()) {
    Ops.append(InVec.getNode()->op_begin(),
               InVec.getNode()->op_end());
  } else if (InVec.isUndef()) {
    Ops.append(NumElts, DAG.getUNDEF(InVal.getValueType()));
  } else {
    return SDValue();
  }
  assert(Ops.size() == NumElts && "Unexpected vector size");

  // Insert the element
  if (Elt < Ops.size()) {
    // All the operands of BUILD_VECTOR must have the same type;
    // we enforce that here.
    EVT OpVT = Ops[0].getValueType();
    Ops[Elt] = OpVT.isInteger() ? DAG.getAnyExtOrTrunc(InVal, DL, OpVT) : InVal;
  }

  // Return the new vector
  return DAG.getBuildVector(VT, DL, Ops);
}

SDValue DAGCombiner::scalarizeExtractedVectorLoad(SDNode *EVE, EVT InVecVT,
                                                  SDValue EltNo,
                                                  LoadSDNode *OriginalLoad) {
  assert(!OriginalLoad->isVolatile());

  EVT ResultVT = EVE->getValueType(0);
  EVT VecEltVT = InVecVT.getVectorElementType();
  unsigned Align = OriginalLoad->getAlignment();
  unsigned NewAlign = DAG.getDataLayout().getABITypeAlignment(
      VecEltVT.getTypeForEVT(*DAG.getContext()));

  if (NewAlign > Align || !TLI.isOperationLegalOrCustom(ISD::LOAD, VecEltVT))
    return SDValue();

  ISD::LoadExtType ExtTy = ResultVT.bitsGT(VecEltVT) ?
    ISD::NON_EXTLOAD : ISD::EXTLOAD;
  if (!TLI.shouldReduceLoadWidth(OriginalLoad, ExtTy, VecEltVT))
    return SDValue();

  Align = NewAlign;

  SDValue NewPtr = OriginalLoad->getBasePtr();
  SDValue Offset;
  EVT PtrType = NewPtr.getValueType();
  MachinePointerInfo MPI;
  SDLoc DL(EVE);
  if (auto *ConstEltNo = dyn_cast<ConstantSDNode>(EltNo)) {
    int Elt = ConstEltNo->getZExtValue();
    unsigned PtrOff = VecEltVT.getSizeInBits() * Elt / 8;
    Offset = DAG.getConstant(PtrOff, DL, PtrType);
    MPI = OriginalLoad->getPointerInfo().getWithOffset(PtrOff);
  } else {
    Offset = DAG.getZExtOrTrunc(EltNo, DL, PtrType);
    Offset = DAG.getNode(
        ISD::MUL, DL, PtrType, Offset,
        DAG.getConstant(VecEltVT.getStoreSize(), DL, PtrType));
    MPI = OriginalLoad->getPointerInfo();
  }
  NewPtr = DAG.getNode(ISD::ADD, DL, PtrType, NewPtr, Offset);

  // The replacement we need to do here is a little tricky: we need to
  // replace an extractelement of a load with a load.
  // Use ReplaceAllUsesOfValuesWith to do the replacement.
  // Note that this replacement assumes that the extractvalue is the only
  // use of the load; that's okay because we don't want to perform this
  // transformation in other cases anyway.
  SDValue Load;
  SDValue Chain;
  if (ResultVT.bitsGT(VecEltVT)) {
    // If the result type of vextract is wider than the load, then issue an
    // extending load instead.
    ISD::LoadExtType ExtType = TLI.isLoadExtLegal(ISD::ZEXTLOAD, ResultVT,
                                                  VecEltVT)
                                   ? ISD::ZEXTLOAD
                                   : ISD::EXTLOAD;
    Load = DAG.getExtLoad(ExtType, SDLoc(EVE), ResultVT,
                          OriginalLoad->getChain(), NewPtr, MPI, VecEltVT,
                          Align, OriginalLoad->getMemOperand()->getFlags(),
                          OriginalLoad->getAAInfo());
    Chain = Load.getValue(1);
  } else {
    Load = DAG.getLoad(VecEltVT, SDLoc(EVE), OriginalLoad->getChain(), NewPtr,
                       MPI, Align, OriginalLoad->getMemOperand()->getFlags(),
                       OriginalLoad->getAAInfo());
    Chain = Load.getValue(1);
    if (ResultVT.bitsLT(VecEltVT))
      Load = DAG.getNode(ISD::TRUNCATE, SDLoc(EVE), ResultVT, Load);
    else
      Load = DAG.getBitcast(ResultVT, Load);
  }
  WorklistRemover DeadNodes(*this);
  SDValue From[] = { SDValue(EVE, 0), SDValue(OriginalLoad, 1) };
  SDValue To[] = { Load, Chain };
  DAG.ReplaceAllUsesOfValuesWith(From, To, 2);
  // Since we're explicitly calling ReplaceAllUses, add the new node to the
  // worklist explicitly as well.
  AddToWorklist(Load.getNode());
  AddUsersToWorklist(Load.getNode()); // Add users too
  // Make sure to revisit this node to clean it up; it will usually be dead.
  AddToWorklist(EVE);
  ++OpsNarrowed;
  return SDValue(EVE, 0);
}

/// Transform a vector binary operation into a scalar binary operation by moving
/// the math/logic after an extract element of a vector.
static SDValue scalarizeExtractedBinop(SDNode *ExtElt, SelectionDAG &DAG,
                                       bool LegalOperations) {
  SDValue Vec = ExtElt->getOperand(0);
  SDValue Index = ExtElt->getOperand(1);
  auto *IndexC = dyn_cast<ConstantSDNode>(Index);
  if (!IndexC || !ISD::isBinaryOp(Vec.getNode()) || !Vec.hasOneUse())
    return SDValue();

  // Targets may want to avoid this to prevent an expensive register transfer.
  const TargetLowering &TLI = DAG.getTargetLoweringInfo();
  if (!TLI.shouldScalarizeBinop(Vec))
    return SDValue();

  // Extracting an element of a vector constant is constant-folded, so this
  // transform is just replacing a vector op with a scalar op while moving the
  // extract.
  SDValue Op0 = Vec.getOperand(0);
  SDValue Op1 = Vec.getOperand(1);
  if (isAnyConstantBuildVector(Op0, true) ||
      isAnyConstantBuildVector(Op1, true)) {
    // extractelt (binop X, C), IndexC --> binop (extractelt X, IndexC), C'
    // extractelt (binop C, X), IndexC --> binop C', (extractelt X, IndexC)
    SDLoc DL(ExtElt);
    EVT VT = ExtElt->getValueType(0);
    SDValue Ext0 = DAG.getNode(ISD::EXTRACT_VECTOR_ELT, DL, VT, Op0, Index);
    SDValue Ext1 = DAG.getNode(ISD::EXTRACT_VECTOR_ELT, DL, VT, Op1, Index);
    return DAG.getNode(Vec.getOpcode(), DL, VT, Ext0, Ext1);
  }

  return SDValue();
}

SDValue DAGCombiner::visitEXTRACT_VECTOR_ELT(SDNode *N) {
  SDValue VecOp = N->getOperand(0);
  SDValue Index = N->getOperand(1);
  EVT ScalarVT = N->getValueType(0);
  EVT VecVT = VecOp.getValueType();
  if (VecOp.isUndef())
    return DAG.getUNDEF(ScalarVT);

  // extract_vector_elt (insert_vector_elt vec, val, idx), idx) -> val
  //
  // This only really matters if the index is non-constant since other combines
  // on the constant elements already work.
  SDLoc DL(N);
  if (VecOp.getOpcode() == ISD::INSERT_VECTOR_ELT &&
      Index == VecOp.getOperand(2)) {
    SDValue Elt = VecOp.getOperand(1);
    return VecVT.isInteger() ? DAG.getAnyExtOrTrunc(Elt, DL, ScalarVT) : Elt;
  }

  // (vextract (scalar_to_vector val, 0) -> val
  if (VecOp.getOpcode() == ISD::SCALAR_TO_VECTOR) {
    // Check if the result type doesn't match the inserted element type. A
    // SCALAR_TO_VECTOR may truncate the inserted element and the
    // EXTRACT_VECTOR_ELT may widen the extracted vector.
    SDValue InOp = VecOp.getOperand(0);
    if (InOp.getValueType() != ScalarVT) {
      assert(InOp.getValueType().isInteger() && ScalarVT.isInteger());
      return DAG.getSExtOrTrunc(InOp, DL, ScalarVT);
    }
    return InOp;
  }

  // extract_vector_elt of out-of-bounds element -> UNDEF
  auto *IndexC = dyn_cast<ConstantSDNode>(Index);
  unsigned NumElts = VecVT.getVectorNumElements();
  if (IndexC && IndexC->getAPIntValue().uge(NumElts))
    return DAG.getUNDEF(ScalarVT);

  // extract_vector_elt (build_vector x, y), 1 -> y
  if (IndexC && VecOp.getOpcode() == ISD::BUILD_VECTOR &&
      TLI.isTypeLegal(VecVT) &&
      (VecOp.hasOneUse() || TLI.aggressivelyPreferBuildVectorSources(VecVT))) {
    SDValue Elt = VecOp.getOperand(IndexC->getZExtValue());
    EVT InEltVT = Elt.getValueType();

    // Sometimes build_vector's scalar input types do not match result type.
    if (ScalarVT == InEltVT)
      return Elt;

    // TODO: It may be useful to truncate if free if the build_vector implicitly
    // converts.
  }

  // TODO: These transforms should not require the 'hasOneUse' restriction, but
  // there are regressions on multiple targets without it. We can end up with a
  // mess of scalar and vector code if we reduce only part of the DAG to scalar.
  if (IndexC && VecOp.getOpcode() == ISD::BITCAST && VecVT.isInteger() &&
      VecOp.hasOneUse()) {
    // The vector index of the LSBs of the source depend on the endian-ness.
    bool IsLE = DAG.getDataLayout().isLittleEndian();
    unsigned ExtractIndex = IndexC->getZExtValue();
    // extract_elt (v2i32 (bitcast i64:x)), BCTruncElt -> i32 (trunc i64:x)
    unsigned BCTruncElt = IsLE ? 0 : NumElts - 1;
    SDValue BCSrc = VecOp.getOperand(0);
    if (ExtractIndex == BCTruncElt && BCSrc.getValueType().isScalarInteger())
      return DAG.getNode(ISD::TRUNCATE, DL, ScalarVT, BCSrc);

    if (LegalTypes && BCSrc.getValueType().isInteger() &&
        BCSrc.getOpcode() == ISD::SCALAR_TO_VECTOR) {
      // ext_elt (bitcast (scalar_to_vec i64 X to v2i64) to v4i32), TruncElt -->
      // trunc i64 X to i32
      SDValue X = BCSrc.getOperand(0);
      assert(X.getValueType().isScalarInteger() && ScalarVT.isScalarInteger() &&
             "Extract element and scalar to vector can't change element type "
             "from FP to integer.");
      unsigned XBitWidth = X.getValueSizeInBits();
      unsigned VecEltBitWidth = VecVT.getScalarSizeInBits();
      BCTruncElt = IsLE ? 0 : XBitWidth / VecEltBitWidth - 1;

      // An extract element return value type can be wider than its vector
      // operand element type. In that case, the high bits are undefined, so
      // it's possible that we may need to extend rather than truncate.
      if (ExtractIndex == BCTruncElt && XBitWidth > VecEltBitWidth) {
        assert(XBitWidth % VecEltBitWidth == 0 &&
               "Scalar bitwidth must be a multiple of vector element bitwidth");
        return DAG.getAnyExtOrTrunc(X, DL, ScalarVT);
      }
    }
  }

  if (SDValue BO = scalarizeExtractedBinop(N, DAG, LegalOperations))
    return BO;

  // Transform: (EXTRACT_VECTOR_ELT( VECTOR_SHUFFLE )) -> EXTRACT_VECTOR_ELT.
  // We only perform this optimization before the op legalization phase because
  // we may introduce new vector instructions which are not backed by TD
  // patterns. For example on AVX, extracting elements from a wide vector
  // without using extract_subvector. However, if we can find an underlying
  // scalar value, then we can always use that.
  if (IndexC && VecOp.getOpcode() == ISD::VECTOR_SHUFFLE) {
    auto *Shuf = cast<ShuffleVectorSDNode>(VecOp);
    // Find the new index to extract from.
    int OrigElt = Shuf->getMaskElt(IndexC->getZExtValue());

    // Extracting an undef index is undef.
    if (OrigElt == -1)
      return DAG.getUNDEF(ScalarVT);

    // Select the right vector half to extract from.
    SDValue SVInVec;
    if (OrigElt < (int)NumElts) {
      SVInVec = VecOp.getOperand(0);
    } else {
      SVInVec = VecOp.getOperand(1);
      OrigElt -= NumElts;
    }

    if (SVInVec.getOpcode() == ISD::BUILD_VECTOR) {
      SDValue InOp = SVInVec.getOperand(OrigElt);
      if (InOp.getValueType() != ScalarVT) {
        assert(InOp.getValueType().isInteger() && ScalarVT.isInteger());
        InOp = DAG.getSExtOrTrunc(InOp, DL, ScalarVT);
      }

      return InOp;
    }

    // FIXME: We should handle recursing on other vector shuffles and
    // scalar_to_vector here as well.

    if (!LegalOperations ||
        // FIXME: Should really be just isOperationLegalOrCustom.
        TLI.isOperationLegal(ISD::EXTRACT_VECTOR_ELT, VecVT) ||
        TLI.isOperationExpand(ISD::VECTOR_SHUFFLE, VecVT)) {
      EVT IndexTy = TLI.getVectorIdxTy(DAG.getDataLayout());
      return DAG.getNode(ISD::EXTRACT_VECTOR_ELT, DL, ScalarVT, SVInVec,
                         DAG.getConstant(OrigElt, DL, IndexTy));
    }
  }

  // If only EXTRACT_VECTOR_ELT nodes use the source vector we can
  // simplify it based on the (valid) extraction indices.
  if (llvm::all_of(VecOp->uses(), [&](SDNode *Use) {
        return Use->getOpcode() == ISD::EXTRACT_VECTOR_ELT &&
               Use->getOperand(0) == VecOp &&
               isa<ConstantSDNode>(Use->getOperand(1));
      })) {
    APInt DemandedElts = APInt::getNullValue(NumElts);
    for (SDNode *Use : VecOp->uses()) {
      auto *CstElt = cast<ConstantSDNode>(Use->getOperand(1));
      if (CstElt->getAPIntValue().ult(NumElts))
        DemandedElts.setBit(CstElt->getZExtValue());
    }
    if (SimplifyDemandedVectorElts(VecOp, DemandedElts, true)) {
      // We simplified the vector operand of this extract element. If this
      // extract is not dead, visit it again so it is folded properly.
      if (N->getOpcode() != ISD::DELETED_NODE)
        AddToWorklist(N);
      return SDValue(N, 0);
    }
  }

  // Everything under here is trying to match an extract of a loaded value.
  // If the result of load has to be truncated, then it's not necessarily
  // profitable.
  bool BCNumEltsChanged = false;
  EVT ExtVT = VecVT.getVectorElementType();
  EVT LVT = ExtVT;
  if (ScalarVT.bitsLT(LVT) && !TLI.isTruncateFree(LVT, ScalarVT))
    return SDValue();

  if (VecOp.getOpcode() == ISD::BITCAST) {
    // Don't duplicate a load with other uses.
    if (!VecOp.hasOneUse())
      return SDValue();

    EVT BCVT = VecOp.getOperand(0).getValueType();
    if (!BCVT.isVector() || ExtVT.bitsGT(BCVT.getVectorElementType()))
      return SDValue();
    if (NumElts != BCVT.getVectorNumElements())
      BCNumEltsChanged = true;
    VecOp = VecOp.getOperand(0);
    ExtVT = BCVT.getVectorElementType();
  }

  // extract (vector load $addr), i --> load $addr + i * size
  if (!LegalOperations && !IndexC && VecOp.hasOneUse() &&
      ISD::isNormalLoad(VecOp.getNode()) &&
      !Index->hasPredecessor(VecOp.getNode())) {
    auto *VecLoad = dyn_cast<LoadSDNode>(VecOp);
    if (VecLoad && !VecLoad->isVolatile())
      return scalarizeExtractedVectorLoad(N, VecVT, Index, VecLoad);
  }

  // Perform only after legalization to ensure build_vector / vector_shuffle
  // optimizations have already been done.
  if (!LegalOperations || !IndexC)
    return SDValue();

  // (vextract (v4f32 load $addr), c) -> (f32 load $addr+c*size)
  // (vextract (v4f32 s2v (f32 load $addr)), c) -> (f32 load $addr+c*size)
  // (vextract (v4f32 shuffle (load $addr), <1,u,u,u>), 0) -> (f32 load $addr)
  int Elt = IndexC->getZExtValue();
  LoadSDNode *LN0 = nullptr;
  if (ISD::isNormalLoad(VecOp.getNode())) {
    LN0 = cast<LoadSDNode>(VecOp);
  } else if (VecOp.getOpcode() == ISD::SCALAR_TO_VECTOR &&
             VecOp.getOperand(0).getValueType() == ExtVT &&
             ISD::isNormalLoad(VecOp.getOperand(0).getNode())) {
    // Don't duplicate a load with other uses.
    if (!VecOp.hasOneUse())
      return SDValue();

    LN0 = cast<LoadSDNode>(VecOp.getOperand(0));
  }
  if (auto *Shuf = dyn_cast<ShuffleVectorSDNode>(VecOp)) {
    // (vextract (vector_shuffle (load $addr), v2, <1, u, u, u>), 1)
    // =>
    // (load $addr+1*size)

    // Don't duplicate a load with other uses.
    if (!VecOp.hasOneUse())
      return SDValue();

    // If the bit convert changed the number of elements, it is unsafe
    // to examine the mask.
    if (BCNumEltsChanged)
      return SDValue();

    // Select the input vector, guarding against out of range extract vector.
    int Idx = (Elt > (int)NumElts) ? -1 : Shuf->getMaskElt(Elt);
    VecOp = (Idx < (int)NumElts) ? VecOp.getOperand(0) : VecOp.getOperand(1);

    if (VecOp.getOpcode() == ISD::BITCAST) {
      // Don't duplicate a load with other uses.
      if (!VecOp.hasOneUse())
        return SDValue();

      VecOp = VecOp.getOperand(0);
    }
    if (ISD::isNormalLoad(VecOp.getNode())) {
      LN0 = cast<LoadSDNode>(VecOp);
      Elt = (Idx < (int)NumElts) ? Idx : Idx - (int)NumElts;
      Index = DAG.getConstant(Elt, DL, Index.getValueType());
    }
  }

  // Make sure we found a non-volatile load and the extractelement is
  // the only use.
  if (!LN0 || !LN0->hasNUsesOfValue(1,0) || LN0->isVolatile())
    return SDValue();

  // If Idx was -1 above, Elt is going to be -1, so just return undef.
  if (Elt == -1)
    return DAG.getUNDEF(LVT);

  return scalarizeExtractedVectorLoad(N, VecVT, Index, LN0);
}

// Simplify (build_vec (ext )) to (bitcast (build_vec ))
SDValue DAGCombiner::reduceBuildVecExtToExtBuildVec(SDNode *N) {
  // We perform this optimization post type-legalization because
  // the type-legalizer often scalarizes integer-promoted vectors.
  // Performing this optimization before may create bit-casts which
  // will be type-legalized to complex code sequences.
  // We perform this optimization only before the operation legalizer because we
  // may introduce illegal operations.
  if (Level != AfterLegalizeVectorOps && Level != AfterLegalizeTypes)
    return SDValue();

  unsigned NumInScalars = N->getNumOperands();
  SDLoc DL(N);
  EVT VT = N->getValueType(0);

  // Check to see if this is a BUILD_VECTOR of a bunch of values
  // which come from any_extend or zero_extend nodes. If so, we can create
  // a new BUILD_VECTOR using bit-casts which may enable other BUILD_VECTOR
  // optimizations. We do not handle sign-extend because we can't fill the sign
  // using shuffles.
  EVT SourceType = MVT::Other;
  bool AllAnyExt = true;

  for (unsigned i = 0; i != NumInScalars; ++i) {
    SDValue In = N->getOperand(i);
    // Ignore undef inputs.
    if (In.isUndef()) continue;

    bool AnyExt  = In.getOpcode() == ISD::ANY_EXTEND;
    bool ZeroExt = In.getOpcode() == ISD::ZERO_EXTEND;

    // Abort if the element is not an extension.
    if (!ZeroExt && !AnyExt) {
      SourceType = MVT::Other;
      break;
    }

    // The input is a ZeroExt or AnyExt. Check the original type.
    EVT InTy = In.getOperand(0).getValueType();

    // Check that all of the widened source types are the same.
    if (SourceType == MVT::Other)
      // First time.
      SourceType = InTy;
    else if (InTy != SourceType) {
      // Multiple income types. Abort.
      SourceType = MVT::Other;
      break;
    }

    // Check if all of the extends are ANY_EXTENDs.
    AllAnyExt &= AnyExt;
  }

  // In order to have valid types, all of the inputs must be extended from the
  // same source type and all of the inputs must be any or zero extend.
  // Scalar sizes must be a power of two.
  EVT OutScalarTy = VT.getScalarType();
  bool ValidTypes = SourceType != MVT::Other &&
                 isPowerOf2_32(OutScalarTy.getSizeInBits()) &&
                 isPowerOf2_32(SourceType.getSizeInBits());

  // Create a new simpler BUILD_VECTOR sequence which other optimizations can
  // turn into a single shuffle instruction.
  if (!ValidTypes)
    return SDValue();

  bool isLE = DAG.getDataLayout().isLittleEndian();
  unsigned ElemRatio = OutScalarTy.getSizeInBits()/SourceType.getSizeInBits();
  assert(ElemRatio > 1 && "Invalid element size ratio");
  SDValue Filler = AllAnyExt ? DAG.getUNDEF(SourceType):
                               DAG.getConstant(0, DL, SourceType);

  unsigned NewBVElems = ElemRatio * VT.getVectorNumElements();
  SmallVector<SDValue, 8> Ops(NewBVElems, Filler);

  // Populate the new build_vector
  for (unsigned i = 0, e = N->getNumOperands(); i != e; ++i) {
    SDValue Cast = N->getOperand(i);
    assert((Cast.getOpcode() == ISD::ANY_EXTEND ||
            Cast.getOpcode() == ISD::ZERO_EXTEND ||
            Cast.isUndef()) && "Invalid cast opcode");
    SDValue In;
    if (Cast.isUndef())
      In = DAG.getUNDEF(SourceType);
    else
      In = Cast->getOperand(0);
    unsigned Index = isLE ? (i * ElemRatio) :
                            (i * ElemRatio + (ElemRatio - 1));

    assert(Index < Ops.size() && "Invalid index");
    Ops[Index] = In;
  }

  // The type of the new BUILD_VECTOR node.
  EVT VecVT = EVT::getVectorVT(*DAG.getContext(), SourceType, NewBVElems);
  assert(VecVT.getSizeInBits() == VT.getSizeInBits() &&
         "Invalid vector size");
  // Check if the new vector type is legal.
  if (!isTypeLegal(VecVT) ||
      (!TLI.isOperationLegal(ISD::BUILD_VECTOR, VecVT) &&
       TLI.isOperationLegal(ISD::BUILD_VECTOR, VT)))
    return SDValue();

  // Make the new BUILD_VECTOR.
  SDValue BV = DAG.getBuildVector(VecVT, DL, Ops);

  // The new BUILD_VECTOR node has the potential to be further optimized.
  AddToWorklist(BV.getNode());
  // Bitcast to the desired type.
  return DAG.getBitcast(VT, BV);
}

SDValue DAGCombiner::createBuildVecShuffle(const SDLoc &DL, SDNode *N,
                                           ArrayRef<int> VectorMask,
                                           SDValue VecIn1, SDValue VecIn2,
                                           unsigned LeftIdx) {
  MVT IdxTy = TLI.getVectorIdxTy(DAG.getDataLayout());
  SDValue ZeroIdx = DAG.getConstant(0, DL, IdxTy);

  EVT VT = N->getValueType(0);
  EVT InVT1 = VecIn1.getValueType();
  EVT InVT2 = VecIn2.getNode() ? VecIn2.getValueType() : InVT1;

  unsigned Vec2Offset = 0;
  unsigned NumElems = VT.getVectorNumElements();
  unsigned ShuffleNumElems = NumElems;

  // In case both the input vectors are extracted from same base
  // vector we do not need extra addend (Vec2Offset) while
  // computing shuffle mask.
  if (!VecIn2 || !(VecIn1.getOpcode() == ISD::EXTRACT_SUBVECTOR) ||
      !(VecIn2.getOpcode() == ISD::EXTRACT_SUBVECTOR) ||
      !(VecIn1.getOperand(0) == VecIn2.getOperand(0)))
    Vec2Offset = InVT1.getVectorNumElements();

  // We can't generate a shuffle node with mismatched input and output types.
  // Try to make the types match the type of the output.
  if (InVT1 != VT || InVT2 != VT) {
    if ((VT.getSizeInBits() % InVT1.getSizeInBits() == 0) && InVT1 == InVT2) {
      // If the output vector length is a multiple of both input lengths,
      // we can concatenate them and pad the rest with undefs.
      unsigned NumConcats = VT.getSizeInBits() / InVT1.getSizeInBits();
      assert(NumConcats >= 2 && "Concat needs at least two inputs!");
      SmallVector<SDValue, 2> ConcatOps(NumConcats, DAG.getUNDEF(InVT1));
      ConcatOps[0] = VecIn1;
      ConcatOps[1] = VecIn2 ? VecIn2 : DAG.getUNDEF(InVT1);
      VecIn1 = DAG.getNode(ISD::CONCAT_VECTORS, DL, VT, ConcatOps);
      VecIn2 = SDValue();
    } else if (InVT1.getSizeInBits() == VT.getSizeInBits() * 2) {
      if (!TLI.isExtractSubvectorCheap(VT, InVT1, NumElems))
        return SDValue();

      if (!VecIn2.getNode()) {
        // If we only have one input vector, and it's twice the size of the
        // output, split it in two.
        VecIn2 = DAG.getNode(ISD::EXTRACT_SUBVECTOR, DL, VT, VecIn1,
                             DAG.getConstant(NumElems, DL, IdxTy));
        VecIn1 = DAG.getNode(ISD::EXTRACT_SUBVECTOR, DL, VT, VecIn1, ZeroIdx);
        // Since we now have shorter input vectors, adjust the offset of the
        // second vector's start.
        Vec2Offset = NumElems;
      } else if (InVT2.getSizeInBits() <= InVT1.getSizeInBits()) {
        // VecIn1 is wider than the output, and we have another, possibly
        // smaller input. Pad the smaller input with undefs, shuffle at the
        // input vector width, and extract the output.
        // The shuffle type is different than VT, so check legality again.
        if (LegalOperations &&
            !TLI.isOperationLegal(ISD::VECTOR_SHUFFLE, InVT1))
          return SDValue();

        // Legalizing INSERT_SUBVECTOR is tricky - you basically have to
        // lower it back into a BUILD_VECTOR. So if the inserted type is
        // illegal, don't even try.
        if (InVT1 != InVT2) {
          if (!TLI.isTypeLegal(InVT2))
            return SDValue();
          VecIn2 = DAG.getNode(ISD::INSERT_SUBVECTOR, DL, InVT1,
                               DAG.getUNDEF(InVT1), VecIn2, ZeroIdx);
        }
        ShuffleNumElems = NumElems * 2;
      } else {
        // Both VecIn1 and VecIn2 are wider than the output, and VecIn2 is wider
        // than VecIn1. We can't handle this for now - this case will disappear
        // when we start sorting the vectors by type.
        return SDValue();
      }
    } else if (InVT2.getSizeInBits() * 2 == VT.getSizeInBits() &&
               InVT1.getSizeInBits() == VT.getSizeInBits()) {
      SmallVector<SDValue, 2> ConcatOps(2, DAG.getUNDEF(InVT2));
      ConcatOps[0] = VecIn2;
      VecIn2 = DAG.getNode(ISD::CONCAT_VECTORS, DL, VT, ConcatOps);
    } else {
      // TODO: Support cases where the length mismatch isn't exactly by a
      // factor of 2.
      // TODO: Move this check upwards, so that if we have bad type
      // mismatches, we don't create any DAG nodes.
      return SDValue();
    }
  }

  // Initialize mask to undef.
  SmallVector<int, 8> Mask(ShuffleNumElems, -1);

  // Only need to run up to the number of elements actually used, not the
  // total number of elements in the shuffle - if we are shuffling a wider
  // vector, the high lanes should be set to undef.
  for (unsigned i = 0; i != NumElems; ++i) {
    if (VectorMask[i] <= 0)
      continue;

    unsigned ExtIndex = N->getOperand(i).getConstantOperandVal(1);
    if (VectorMask[i] == (int)LeftIdx) {
      Mask[i] = ExtIndex;
    } else if (VectorMask[i] == (int)LeftIdx + 1) {
      Mask[i] = Vec2Offset + ExtIndex;
    }
  }

  // The type the input vectors may have changed above.
  InVT1 = VecIn1.getValueType();

  // If we already have a VecIn2, it should have the same type as VecIn1.
  // If we don't, get an undef/zero vector of the appropriate type.
  VecIn2 = VecIn2.getNode() ? VecIn2 : DAG.getUNDEF(InVT1);
  assert(InVT1 == VecIn2.getValueType() && "Unexpected second input type.");

  SDValue Shuffle = DAG.getVectorShuffle(InVT1, DL, VecIn1, VecIn2, Mask);
  if (ShuffleNumElems > NumElems)
    Shuffle = DAG.getNode(ISD::EXTRACT_SUBVECTOR, DL, VT, Shuffle, ZeroIdx);

  return Shuffle;
}

static SDValue reduceBuildVecToShuffleWithZero(SDNode *BV, SelectionDAG &DAG) {
  assert(BV->getOpcode() == ISD::BUILD_VECTOR && "Expected build vector");

  // First, determine where the build vector is not undef.
  // TODO: We could extend this to handle zero elements as well as undefs.
  int NumBVOps = BV->getNumOperands();
  int ZextElt = -1;
  for (int i = 0; i != NumBVOps; ++i) {
    SDValue Op = BV->getOperand(i);
    if (Op.isUndef())
      continue;
    if (ZextElt == -1)
      ZextElt = i;
    else
      return SDValue();
  }
  // Bail out if there's no non-undef element.
  if (ZextElt == -1)
    return SDValue();

  // The build vector contains some number of undef elements and exactly
  // one other element. That other element must be a zero-extended scalar
  // extracted from a vector at a constant index to turn this into a shuffle.
  // Also, require that the build vector does not implicitly truncate/extend
  // its elements.
  // TODO: This could be enhanced to allow ANY_EXTEND as well as ZERO_EXTEND.
  EVT VT = BV->getValueType(0);
  SDValue Zext = BV->getOperand(ZextElt);
  if (Zext.getOpcode() != ISD::ZERO_EXTEND || !Zext.hasOneUse() ||
      Zext.getOperand(0).getOpcode() != ISD::EXTRACT_VECTOR_ELT ||
      !isa<ConstantSDNode>(Zext.getOperand(0).getOperand(1)) ||
      Zext.getValueSizeInBits() != VT.getScalarSizeInBits())
    return SDValue();

  // The zero-extend must be a multiple of the source size, and we must be
  // building a vector of the same size as the source of the extract element.
  SDValue Extract = Zext.getOperand(0);
  unsigned DestSize = Zext.getValueSizeInBits();
  unsigned SrcSize = Extract.getValueSizeInBits();
  if (DestSize % SrcSize != 0 ||
      Extract.getOperand(0).getValueSizeInBits() != VT.getSizeInBits())
    return SDValue();

  // Create a shuffle mask that will combine the extracted element with zeros
  // and undefs.
  int ZextRatio = DestSize / SrcSize;
  int NumMaskElts = NumBVOps * ZextRatio;
  SmallVector<int, 32> ShufMask(NumMaskElts, -1);
  for (int i = 0; i != NumMaskElts; ++i) {
    if (i / ZextRatio == ZextElt) {
      // The low bits of the (potentially translated) extracted element map to
      // the source vector. The high bits map to zero. We will use a zero vector
      // as the 2nd source operand of the shuffle, so use the 1st element of
      // that vector (mask value is number-of-elements) for the high bits.
      if (i % ZextRatio == 0)
        ShufMask[i] = Extract.getConstantOperandVal(1);
      else
        ShufMask[i] = NumMaskElts;
    }

    // Undef elements of the build vector remain undef because we initialize
    // the shuffle mask with -1.
  }

  // Turn this into a shuffle with zero if that's legal.
  EVT VecVT = Extract.getOperand(0).getValueType();
  if (!DAG.getTargetLoweringInfo().isShuffleMaskLegal(ShufMask, VecVT))
    return SDValue();

  // buildvec undef, ..., (zext (extractelt V, IndexC)), undef... -->
  // bitcast (shuffle V, ZeroVec, VectorMask)
  SDLoc DL(BV);
  SDValue ZeroVec = DAG.getConstant(0, DL, VecVT);
  SDValue Shuf = DAG.getVectorShuffle(VecVT, DL, Extract.getOperand(0), ZeroVec,
                                      ShufMask);
  return DAG.getBitcast(VT, Shuf);
}

// Check to see if this is a BUILD_VECTOR of a bunch of EXTRACT_VECTOR_ELT
// operations. If the types of the vectors we're extracting from allow it,
// turn this into a vector_shuffle node.
SDValue DAGCombiner::reduceBuildVecToShuffle(SDNode *N) {
  SDLoc DL(N);
  EVT VT = N->getValueType(0);

  // Only type-legal BUILD_VECTOR nodes are converted to shuffle nodes.
  if (!isTypeLegal(VT))
    return SDValue();

  if (SDValue V = reduceBuildVecToShuffleWithZero(N, DAG))
    return V;

  // May only combine to shuffle after legalize if shuffle is legal.
  if (LegalOperations && !TLI.isOperationLegal(ISD::VECTOR_SHUFFLE, VT))
    return SDValue();

  bool UsesZeroVector = false;
  unsigned NumElems = N->getNumOperands();

  // Record, for each element of the newly built vector, which input vector
  // that element comes from. -1 stands for undef, 0 for the zero vector,
  // and positive values for the input vectors.
  // VectorMask maps each element to its vector number, and VecIn maps vector
  // numbers to their initial SDValues.

  SmallVector<int, 8> VectorMask(NumElems, -1);
  SmallVector<SDValue, 8> VecIn;
  VecIn.push_back(SDValue());

  for (unsigned i = 0; i != NumElems; ++i) {
    SDValue Op = N->getOperand(i);

    if (Op.isUndef())
      continue;

    // See if we can use a blend with a zero vector.
    // TODO: Should we generalize this to a blend with an arbitrary constant
    // vector?
    if (isNullConstant(Op) || isNullFPConstant(Op)) {
      UsesZeroVector = true;
      VectorMask[i] = 0;
      continue;
    }

    // Not an undef or zero. If the input is something other than an
    // EXTRACT_VECTOR_ELT with an in-range constant index, bail out.
    if (Op.getOpcode() != ISD::EXTRACT_VECTOR_ELT ||
        !isa<ConstantSDNode>(Op.getOperand(1)))
      return SDValue();
    SDValue ExtractedFromVec = Op.getOperand(0);

    APInt ExtractIdx = cast<ConstantSDNode>(Op.getOperand(1))->getAPIntValue();
    if (ExtractIdx.uge(ExtractedFromVec.getValueType().getVectorNumElements()))
      return SDValue();

    // All inputs must have the same element type as the output.
    if (VT.getVectorElementType() !=
        ExtractedFromVec.getValueType().getVectorElementType())
      return SDValue();

    // Have we seen this input vector before?
    // The vectors are expected to be tiny (usually 1 or 2 elements), so using
    // a map back from SDValues to numbers isn't worth it.
    unsigned Idx = std::distance(
        VecIn.begin(), std::find(VecIn.begin(), VecIn.end(), ExtractedFromVec));
    if (Idx == VecIn.size())
      VecIn.push_back(ExtractedFromVec);

    VectorMask[i] = Idx;
  }

  // If we didn't find at least one input vector, bail out.
  if (VecIn.size() < 2)
    return SDValue();

  // If all the Operands of BUILD_VECTOR extract from same
  // vector, then split the vector efficiently based on the maximum
  // vector access index and adjust the VectorMask and
  // VecIn accordingly.
  if (VecIn.size() == 2) {
    unsigned MaxIndex = 0;
    unsigned NearestPow2 = 0;
    SDValue Vec = VecIn.back();
    EVT InVT = Vec.getValueType();
    MVT IdxTy = TLI.getVectorIdxTy(DAG.getDataLayout());
    SmallVector<unsigned, 8> IndexVec(NumElems, 0);

    for (unsigned i = 0; i < NumElems; i++) {
      if (VectorMask[i] <= 0)
        continue;
      unsigned Index = N->getOperand(i).getConstantOperandVal(1);
      IndexVec[i] = Index;
      MaxIndex = std::max(MaxIndex, Index);
    }

    NearestPow2 = PowerOf2Ceil(MaxIndex);
    if (InVT.isSimple() && NearestPow2 > 2 && MaxIndex < NearestPow2 &&
        NumElems * 2 < NearestPow2) {
      unsigned SplitSize = NearestPow2 / 2;
      EVT SplitVT = EVT::getVectorVT(*DAG.getContext(),
                                     InVT.getVectorElementType(), SplitSize);
      if (TLI.isTypeLegal(SplitVT)) {
        SDValue VecIn2 = DAG.getNode(ISD::EXTRACT_SUBVECTOR, DL, SplitVT, Vec,
                                     DAG.getConstant(SplitSize, DL, IdxTy));
        SDValue VecIn1 = DAG.getNode(ISD::EXTRACT_SUBVECTOR, DL, SplitVT, Vec,
                                     DAG.getConstant(0, DL, IdxTy));
        VecIn.pop_back();
        VecIn.push_back(VecIn1);
        VecIn.push_back(VecIn2);

        for (unsigned i = 0; i < NumElems; i++) {
          if (VectorMask[i] <= 0)
            continue;
          VectorMask[i] = (IndexVec[i] < SplitSize) ? 1 : 2;
        }
      }
    }
  }

  // TODO: We want to sort the vectors by descending length, so that adjacent
  // pairs have similar length, and the longer vector is always first in the
  // pair.

  // TODO: Should this fire if some of the input vectors has illegal type (like
  // it does now), or should we let legalization run its course first?

  // Shuffle phase:
  // Take pairs of vectors, and shuffle them so that the result has elements
  // from these vectors in the correct places.
  // For example, given:
  // t10: i32 = extract_vector_elt t1, Constant:i64<0>
  // t11: i32 = extract_vector_elt t2, Constant:i64<0>
  // t12: i32 = extract_vector_elt t3, Constant:i64<0>
  // t13: i32 = extract_vector_elt t1, Constant:i64<1>
  // t14: v4i32 = BUILD_VECTOR t10, t11, t12, t13
  // We will generate:
  // t20: v4i32 = vector_shuffle<0,4,u,1> t1, t2
  // t21: v4i32 = vector_shuffle<u,u,0,u> t3, undef
  SmallVector<SDValue, 4> Shuffles;
  for (unsigned In = 0, Len = (VecIn.size() / 2); In < Len; ++In) {
    unsigned LeftIdx = 2 * In + 1;
    SDValue VecLeft = VecIn[LeftIdx];
    SDValue VecRight =
        (LeftIdx + 1) < VecIn.size() ? VecIn[LeftIdx + 1] : SDValue();

    if (SDValue Shuffle = createBuildVecShuffle(DL, N, VectorMask, VecLeft,
                                                VecRight, LeftIdx))
      Shuffles.push_back(Shuffle);
    else
      return SDValue();
  }

  // If we need the zero vector as an "ingredient" in the blend tree, add it
  // to the list of shuffles.
  if (UsesZeroVector)
    Shuffles.push_back(VT.isInteger() ? DAG.getConstant(0, DL, VT)
                                      : DAG.getConstantFP(0.0, DL, VT));

  // If we only have one shuffle, we're done.
  if (Shuffles.size() == 1)
    return Shuffles[0];

  // Update the vector mask to point to the post-shuffle vectors.
  for (int &Vec : VectorMask)
    if (Vec == 0)
      Vec = Shuffles.size() - 1;
    else
      Vec = (Vec - 1) / 2;

  // More than one shuffle. Generate a binary tree of blends, e.g. if from
  // the previous step we got the set of shuffles t10, t11, t12, t13, we will
  // generate:
  // t10: v8i32 = vector_shuffle<0,8,u,u,u,u,u,u> t1, t2
  // t11: v8i32 = vector_shuffle<u,u,0,8,u,u,u,u> t3, t4
  // t12: v8i32 = vector_shuffle<u,u,u,u,0,8,u,u> t5, t6
  // t13: v8i32 = vector_shuffle<u,u,u,u,u,u,0,8> t7, t8
  // t20: v8i32 = vector_shuffle<0,1,10,11,u,u,u,u> t10, t11
  // t21: v8i32 = vector_shuffle<u,u,u,u,4,5,14,15> t12, t13
  // t30: v8i32 = vector_shuffle<0,1,2,3,12,13,14,15> t20, t21

  // Make sure the initial size of the shuffle list is even.
  if (Shuffles.size() % 2)
    Shuffles.push_back(DAG.getUNDEF(VT));

  for (unsigned CurSize = Shuffles.size(); CurSize > 1; CurSize /= 2) {
    if (CurSize % 2) {
      Shuffles[CurSize] = DAG.getUNDEF(VT);
      CurSize++;
    }
    for (unsigned In = 0, Len = CurSize / 2; In < Len; ++In) {
      int Left = 2 * In;
      int Right = 2 * In + 1;
      SmallVector<int, 8> Mask(NumElems, -1);
      for (unsigned i = 0; i != NumElems; ++i) {
        if (VectorMask[i] == Left) {
          Mask[i] = i;
          VectorMask[i] = In;
        } else if (VectorMask[i] == Right) {
          Mask[i] = i + NumElems;
          VectorMask[i] = In;
        }
      }

      Shuffles[In] =
          DAG.getVectorShuffle(VT, DL, Shuffles[Left], Shuffles[Right], Mask);
    }
  }
  return Shuffles[0];
}

// Try to turn a build vector of zero extends of extract vector elts into a
// a vector zero extend and possibly an extract subvector.
// TODO: Support sign extend or any extend?
// TODO: Allow undef elements?
// TODO: Don't require the extracts to start at element 0.
SDValue DAGCombiner::convertBuildVecZextToZext(SDNode *N) {
  if (LegalOperations)
    return SDValue();

  EVT VT = N->getValueType(0);

  SDValue Op0 = N->getOperand(0);
  auto checkElem = [&](SDValue Op) -> int64_t {
    if (Op.getOpcode() == ISD::ZERO_EXTEND &&
        Op.getOperand(0).getOpcode() == ISD::EXTRACT_VECTOR_ELT &&
        Op0.getOperand(0).getOperand(0) == Op.getOperand(0).getOperand(0))
      if (auto *C = dyn_cast<ConstantSDNode>(Op.getOperand(0).getOperand(1)))
        return C->getZExtValue();
    return -1;
  };

  // Make sure the first element matches
  // (zext (extract_vector_elt X, C))
  int64_t Offset = checkElem(Op0);
  if (Offset < 0)
    return SDValue();

  unsigned NumElems = N->getNumOperands();
  SDValue In = Op0.getOperand(0).getOperand(0);
  EVT InSVT = In.getValueType().getScalarType();
  EVT InVT = EVT::getVectorVT(*DAG.getContext(), InSVT, NumElems);

  // Don't create an illegal input type after type legalization.
  if (LegalTypes && !TLI.isTypeLegal(InVT))
    return SDValue();

  // Ensure all the elements come from the same vector and are adjacent.
  for (unsigned i = 1; i != NumElems; ++i) {
    if ((Offset + i) != checkElem(N->getOperand(i)))
      return SDValue();
  }

  SDLoc DL(N);
  In = DAG.getNode(ISD::EXTRACT_SUBVECTOR, DL, InVT, In,
                   Op0.getOperand(0).getOperand(1));
  return DAG.getNode(ISD::ZERO_EXTEND, DL, VT, In);
}

SDValue DAGCombiner::visitBUILD_VECTOR(SDNode *N) {
  EVT VT = N->getValueType(0);

  // A vector built entirely of undefs is undef.
  if (ISD::allOperandsUndef(N))
    return DAG.getUNDEF(VT);

  // If this is a splat of a bitcast from another vector, change to a
  // concat_vector.
  // For example:
  //   (build_vector (i64 (bitcast (v2i32 X))), (i64 (bitcast (v2i32 X)))) ->
  //     (v2i64 (bitcast (concat_vectors (v2i32 X), (v2i32 X))))
  //
  // If X is a build_vector itself, the concat can become a larger build_vector.
  // TODO: Maybe this is useful for non-splat too?
  if (!LegalOperations) {
    if (SDValue Splat = cast<BuildVectorSDNode>(N)->getSplatValue()) {
      Splat = peekThroughBitcasts(Splat);
      EVT SrcVT = Splat.getValueType();
      if (SrcVT.isVector()) {
        unsigned NumElts = N->getNumOperands() * SrcVT.getVectorNumElements();
        EVT NewVT = EVT::getVectorVT(*DAG.getContext(),
                                     SrcVT.getVectorElementType(), NumElts);
        if (!LegalTypes || TLI.isTypeLegal(NewVT)) {
          SmallVector<SDValue, 8> Ops(N->getNumOperands(), Splat);
          SDValue Concat = DAG.getNode(ISD::CONCAT_VECTORS, SDLoc(N),
                                       NewVT, Ops);
          return DAG.getBitcast(VT, Concat);
        }
      }
    }
  }

  // Check if we can express BUILD VECTOR via subvector extract.
  if (!LegalTypes && (N->getNumOperands() > 1)) {
    SDValue Op0 = N->getOperand(0);
    auto checkElem = [&](SDValue Op) -> uint64_t {
      if ((Op.getOpcode() == ISD::EXTRACT_VECTOR_ELT) &&
          (Op0.getOperand(0) == Op.getOperand(0)))
        if (auto CNode = dyn_cast<ConstantSDNode>(Op.getOperand(1)))
          return CNode->getZExtValue();
      return -1;
    };

    int Offset = checkElem(Op0);
    for (unsigned i = 0; i < N->getNumOperands(); ++i) {
      if (Offset + i != checkElem(N->getOperand(i))) {
        Offset = -1;
        break;
      }
    }

    if ((Offset == 0) &&
        (Op0.getOperand(0).getValueType() == N->getValueType(0)))
      return Op0.getOperand(0);
    if ((Offset != -1) &&
        ((Offset % N->getValueType(0).getVectorNumElements()) ==
         0)) // IDX must be multiple of output size.
      return DAG.getNode(ISD::EXTRACT_SUBVECTOR, SDLoc(N), N->getValueType(0),
                         Op0.getOperand(0), Op0.getOperand(1));
  }

  if (SDValue V = convertBuildVecZextToZext(N))
    return V;

  if (SDValue V = reduceBuildVecExtToExtBuildVec(N))
    return V;

  if (SDValue V = reduceBuildVecToShuffle(N))
    return V;

  return SDValue();
}

static SDValue combineConcatVectorOfScalars(SDNode *N, SelectionDAG &DAG) {
  const TargetLowering &TLI = DAG.getTargetLoweringInfo();
  EVT OpVT = N->getOperand(0).getValueType();

  // If the operands are legal vectors, leave them alone.
  if (TLI.isTypeLegal(OpVT))
    return SDValue();

  SDLoc DL(N);
  EVT VT = N->getValueType(0);
  SmallVector<SDValue, 8> Ops;

  EVT SVT = EVT::getIntegerVT(*DAG.getContext(), OpVT.getSizeInBits());
  SDValue ScalarUndef = DAG.getNode(ISD::UNDEF, DL, SVT);

  // Keep track of what we encounter.
  bool AnyInteger = false;
  bool AnyFP = false;
  for (const SDValue &Op : N->ops()) {
    if (ISD::BITCAST == Op.getOpcode() &&
        !Op.getOperand(0).getValueType().isVector())
      Ops.push_back(Op.getOperand(0));
    else if (ISD::UNDEF == Op.getOpcode())
      Ops.push_back(ScalarUndef);
    else
      return SDValue();

    // Note whether we encounter an integer or floating point scalar.
    // If it's neither, bail out, it could be something weird like x86mmx.
    EVT LastOpVT = Ops.back().getValueType();
    if (LastOpVT.isFloatingPoint())
      AnyFP = true;
    else if (LastOpVT.isInteger())
      AnyInteger = true;
    else
      return SDValue();
  }

  // If any of the operands is a floating point scalar bitcast to a vector,
  // use floating point types throughout, and bitcast everything.
  // Replace UNDEFs by another scalar UNDEF node, of the final desired type.
  if (AnyFP) {
    SVT = EVT::getFloatingPointVT(OpVT.getSizeInBits());
    ScalarUndef = DAG.getNode(ISD::UNDEF, DL, SVT);
    if (AnyInteger) {
      for (SDValue &Op : Ops) {
        if (Op.getValueType() == SVT)
          continue;
        if (Op.isUndef())
          Op = ScalarUndef;
        else
          Op = DAG.getBitcast(SVT, Op);
      }
    }
  }

  EVT VecVT = EVT::getVectorVT(*DAG.getContext(), SVT,
                               VT.getSizeInBits() / SVT.getSizeInBits());
  return DAG.getBitcast(VT, DAG.getBuildVector(VecVT, DL, Ops));
}

// Check to see if this is a CONCAT_VECTORS of a bunch of EXTRACT_SUBVECTOR
// operations. If so, and if the EXTRACT_SUBVECTOR vector inputs come from at
// most two distinct vectors the same size as the result, attempt to turn this
// into a legal shuffle.
static SDValue combineConcatVectorOfExtracts(SDNode *N, SelectionDAG &DAG) {
  EVT VT = N->getValueType(0);
  EVT OpVT = N->getOperand(0).getValueType();
  int NumElts = VT.getVectorNumElements();
  int NumOpElts = OpVT.getVectorNumElements();

  SDValue SV0 = DAG.getUNDEF(VT), SV1 = DAG.getUNDEF(VT);
  SmallVector<int, 8> Mask;

  for (SDValue Op : N->ops()) {
    Op = peekThroughBitcasts(Op);

    // UNDEF nodes convert to UNDEF shuffle mask values.
    if (Op.isUndef()) {
      Mask.append((unsigned)NumOpElts, -1);
      continue;
    }

    if (Op.getOpcode() != ISD::EXTRACT_SUBVECTOR)
      return SDValue();

    // What vector are we extracting the subvector from and at what index?
    SDValue ExtVec = Op.getOperand(0);

    // We want the EVT of the original extraction to correctly scale the
    // extraction index.
    EVT ExtVT = ExtVec.getValueType();
    ExtVec = peekThroughBitcasts(ExtVec);

    // UNDEF nodes convert to UNDEF shuffle mask values.
    if (ExtVec.isUndef()) {
      Mask.append((unsigned)NumOpElts, -1);
      continue;
    }

    if (!isa<ConstantSDNode>(Op.getOperand(1)))
      return SDValue();
    int ExtIdx = Op.getConstantOperandVal(1);

    // Ensure that we are extracting a subvector from a vector the same
    // size as the result.
    if (ExtVT.getSizeInBits() != VT.getSizeInBits())
      return SDValue();

    // Scale the subvector index to account for any bitcast.
    int NumExtElts = ExtVT.getVectorNumElements();
    if (0 == (NumExtElts % NumElts))
      ExtIdx /= (NumExtElts / NumElts);
    else if (0 == (NumElts % NumExtElts))
      ExtIdx *= (NumElts / NumExtElts);
    else
      return SDValue();

    // At most we can reference 2 inputs in the final shuffle.
    if (SV0.isUndef() || SV0 == ExtVec) {
      SV0 = ExtVec;
      for (int i = 0; i != NumOpElts; ++i)
        Mask.push_back(i + ExtIdx);
    } else if (SV1.isUndef() || SV1 == ExtVec) {
      SV1 = ExtVec;
      for (int i = 0; i != NumOpElts; ++i)
        Mask.push_back(i + ExtIdx + NumElts);
    } else {
      return SDValue();
    }
  }

  if (!DAG.getTargetLoweringInfo().isShuffleMaskLegal(Mask, VT))
    return SDValue();

  return DAG.getVectorShuffle(VT, SDLoc(N), DAG.getBitcast(VT, SV0),
                              DAG.getBitcast(VT, SV1), Mask);
}

SDValue DAGCombiner::visitCONCAT_VECTORS(SDNode *N) {
  // If we only have one input vector, we don't need to do any concatenation.
  if (N->getNumOperands() == 1)
    return N->getOperand(0);

  // Check if all of the operands are undefs.
  EVT VT = N->getValueType(0);
  if (ISD::allOperandsUndef(N))
    return DAG.getUNDEF(VT);

  // Optimize concat_vectors where all but the first of the vectors are undef.
  if (std::all_of(std::next(N->op_begin()), N->op_end(), [](const SDValue &Op) {
        return Op.isUndef();
      })) {
    SDValue In = N->getOperand(0);
    assert(In.getValueType().isVector() && "Must concat vectors");

    SDValue Scalar = peekThroughOneUseBitcasts(In);

    // concat_vectors(scalar_to_vector(scalar), undef) ->
    //     scalar_to_vector(scalar)
    if (!LegalOperations && Scalar.getOpcode() == ISD::SCALAR_TO_VECTOR &&
         Scalar.hasOneUse()) {
      EVT SVT = Scalar.getValueType().getVectorElementType();
      if (SVT == Scalar.getOperand(0).getValueType())
        Scalar = Scalar.getOperand(0);
    }

    // concat_vectors(scalar, undef) -> scalar_to_vector(scalar)
    if (!Scalar.getValueType().isVector()) {
      // If the bitcast type isn't legal, it might be a trunc of a legal type;
      // look through the trunc so we can still do the transform:
      //   concat_vectors(trunc(scalar), undef) -> scalar_to_vector(scalar)
      if (Scalar->getOpcode() == ISD::TRUNCATE &&
          !TLI.isTypeLegal(Scalar.getValueType()) &&
          TLI.isTypeLegal(Scalar->getOperand(0).getValueType()))
        Scalar = Scalar->getOperand(0);

      EVT SclTy = Scalar.getValueType();

      if (!SclTy.isFloatingPoint() && !SclTy.isInteger())
        return SDValue();

      // Bail out if the vector size is not a multiple of the scalar size.
      if (VT.getSizeInBits() % SclTy.getSizeInBits())
        return SDValue();

      unsigned VNTNumElms = VT.getSizeInBits() / SclTy.getSizeInBits();
      if (VNTNumElms < 2)
        return SDValue();

      EVT NVT = EVT::getVectorVT(*DAG.getContext(), SclTy, VNTNumElms);
      if (!TLI.isTypeLegal(NVT) || !TLI.isTypeLegal(Scalar.getValueType()))
        return SDValue();

      SDValue Res = DAG.getNode(ISD::SCALAR_TO_VECTOR, SDLoc(N), NVT, Scalar);
      return DAG.getBitcast(VT, Res);
    }
  }

  // Fold any combination of BUILD_VECTOR or UNDEF nodes into one BUILD_VECTOR.
  // We have already tested above for an UNDEF only concatenation.
  // fold (concat_vectors (BUILD_VECTOR A, B, ...), (BUILD_VECTOR C, D, ...))
  // -> (BUILD_VECTOR A, B, ..., C, D, ...)
  auto IsBuildVectorOrUndef = [](const SDValue &Op) {
    return ISD::UNDEF == Op.getOpcode() || ISD::BUILD_VECTOR == Op.getOpcode();
  };
  if (llvm::all_of(N->ops(), IsBuildVectorOrUndef)) {
    SmallVector<SDValue, 8> Opnds;
    EVT SVT = VT.getScalarType();

    EVT MinVT = SVT;
    if (!SVT.isFloatingPoint()) {
      // If BUILD_VECTOR are from built from integer, they may have different
      // operand types. Get the smallest type and truncate all operands to it.
      bool FoundMinVT = false;
      for (const SDValue &Op : N->ops())
        if (ISD::BUILD_VECTOR == Op.getOpcode()) {
          EVT OpSVT = Op.getOperand(0).getValueType();
          MinVT = (!FoundMinVT || OpSVT.bitsLE(MinVT)) ? OpSVT : MinVT;
          FoundMinVT = true;
        }
      assert(FoundMinVT && "Concat vector type mismatch");
    }

    for (const SDValue &Op : N->ops()) {
      EVT OpVT = Op.getValueType();
      unsigned NumElts = OpVT.getVectorNumElements();

      if (ISD::UNDEF == Op.getOpcode())
        Opnds.append(NumElts, DAG.getUNDEF(MinVT));

      if (ISD::BUILD_VECTOR == Op.getOpcode()) {
        if (SVT.isFloatingPoint()) {
          assert(SVT == OpVT.getScalarType() && "Concat vector type mismatch");
          Opnds.append(Op->op_begin(), Op->op_begin() + NumElts);
        } else {
          for (unsigned i = 0; i != NumElts; ++i)
            Opnds.push_back(
                DAG.getNode(ISD::TRUNCATE, SDLoc(N), MinVT, Op.getOperand(i)));
        }
      }
    }

    assert(VT.getVectorNumElements() == Opnds.size() &&
           "Concat vector type mismatch");
    return DAG.getBuildVector(VT, SDLoc(N), Opnds);
  }

  // Fold CONCAT_VECTORS of only bitcast scalars (or undef) to BUILD_VECTOR.
  if (SDValue V = combineConcatVectorOfScalars(N, DAG))
    return V;

  // Fold CONCAT_VECTORS of EXTRACT_SUBVECTOR (or undef) to VECTOR_SHUFFLE.
  if (Level < AfterLegalizeVectorOps && TLI.isTypeLegal(VT))
    if (SDValue V = combineConcatVectorOfExtracts(N, DAG))
      return V;

  // Type legalization of vectors and DAG canonicalization of SHUFFLE_VECTOR
  // nodes often generate nop CONCAT_VECTOR nodes.
  // Scan the CONCAT_VECTOR operands and look for a CONCAT operations that
  // place the incoming vectors at the exact same location.
  SDValue SingleSource = SDValue();
  unsigned PartNumElem = N->getOperand(0).getValueType().getVectorNumElements();

  for (unsigned i = 0, e = N->getNumOperands(); i != e; ++i) {
    SDValue Op = N->getOperand(i);

    if (Op.isUndef())
      continue;

    // Check if this is the identity extract:
    if (Op.getOpcode() != ISD::EXTRACT_SUBVECTOR)
      return SDValue();

    // Find the single incoming vector for the extract_subvector.
    if (SingleSource.getNode()) {
      if (Op.getOperand(0) != SingleSource)
        return SDValue();
    } else {
      SingleSource = Op.getOperand(0);

      // Check the source type is the same as the type of the result.
      // If not, this concat may extend the vector, so we can not
      // optimize it away.
      if (SingleSource.getValueType() != N->getValueType(0))
        return SDValue();
    }

    unsigned IdentityIndex = i * PartNumElem;
    ConstantSDNode *CS = dyn_cast<ConstantSDNode>(Op.getOperand(1));
    // The extract index must be constant.
    if (!CS)
      return SDValue();

    // Check that we are reading from the identity index.
    if (CS->getZExtValue() != IdentityIndex)
      return SDValue();
  }

  if (SingleSource.getNode())
    return SingleSource;

  return SDValue();
}

/// If we are extracting a subvector produced by a wide binary operator try
/// to use a narrow binary operator and/or avoid concatenation and extraction.
static SDValue narrowExtractedVectorBinOp(SDNode *Extract, SelectionDAG &DAG) {
  // TODO: Refactor with the caller (visitEXTRACT_SUBVECTOR), so we can share
  // some of these bailouts with other transforms.

  // The extract index must be a constant, so we can map it to a concat operand.
  auto *ExtractIndexC = dyn_cast<ConstantSDNode>(Extract->getOperand(1));
  if (!ExtractIndexC)
    return SDValue();

  // We are looking for an optionally bitcasted wide vector binary operator
  // feeding an extract subvector.
  SDValue BinOp = peekThroughBitcasts(Extract->getOperand(0));
  if (!ISD::isBinaryOp(BinOp.getNode()))
    return SDValue();

  // The binop must be a vector type, so we can extract some fraction of it.
  EVT WideBVT = BinOp.getValueType();
  if (!WideBVT.isVector())
    return SDValue();

  EVT VT = Extract->getValueType(0);
  unsigned ExtractIndex = ExtractIndexC->getZExtValue();
  assert(ExtractIndex % VT.getVectorNumElements() == 0 &&
         "Extract index is not a multiple of the vector length.");

  // Bail out if this is not a proper multiple width extraction.
  unsigned WideWidth = WideBVT.getSizeInBits();
  unsigned NarrowWidth = VT.getSizeInBits();
  if (WideWidth % NarrowWidth != 0)
    return SDValue();

  // Bail out if we are extracting a fraction of a single operation. This can
  // occur because we potentially looked through a bitcast of the binop.
  unsigned NarrowingRatio = WideWidth / NarrowWidth;
  unsigned WideNumElts = WideBVT.getVectorNumElements();
  if (WideNumElts % NarrowingRatio != 0)
    return SDValue();

  // Bail out if the target does not support a narrower version of the binop.
  EVT NarrowBVT = EVT::getVectorVT(*DAG.getContext(), WideBVT.getScalarType(),
                                   WideNumElts / NarrowingRatio);
  unsigned BOpcode = BinOp.getOpcode();
  const TargetLowering &TLI = DAG.getTargetLoweringInfo();
  if (!TLI.isOperationLegalOrCustomOrPromote(BOpcode, NarrowBVT))
    return SDValue();

  // If extraction is cheap, we don't need to look at the binop operands
  // for concat ops. The narrow binop alone makes this transform profitable.
  // We can't just reuse the original extract index operand because we may have
  // bitcasted.
  unsigned ConcatOpNum = ExtractIndex / VT.getVectorNumElements();
  unsigned ExtBOIdx = ConcatOpNum * NarrowBVT.getVectorNumElements();
  EVT ExtBOIdxVT = Extract->getOperand(1).getValueType();
  if (TLI.isExtractSubvectorCheap(NarrowBVT, WideBVT, ExtBOIdx) &&
      BinOp.hasOneUse() && Extract->getOperand(0)->hasOneUse()) {
    // extract (binop B0, B1), N --> binop (extract B0, N), (extract B1, N)
    SDLoc DL(Extract);
    SDValue NewExtIndex = DAG.getConstant(ExtBOIdx, DL, ExtBOIdxVT);
    SDValue X = DAG.getNode(ISD::EXTRACT_SUBVECTOR, DL, NarrowBVT,
                            BinOp.getOperand(0), NewExtIndex);
    SDValue Y = DAG.getNode(ISD::EXTRACT_SUBVECTOR, DL, NarrowBVT,
                            BinOp.getOperand(1), NewExtIndex);
    SDValue NarrowBinOp = DAG.getNode(BOpcode, DL, NarrowBVT, X, Y,
                                      BinOp.getNode()->getFlags());
    return DAG.getBitcast(VT, NarrowBinOp);
  }

  // Only handle the case where we are doubling and then halving. A larger ratio
  // may require more than two narrow binops to replace the wide binop.
  if (NarrowingRatio != 2)
    return SDValue();

  // TODO: The motivating case for this transform is an x86 AVX1 target. That
  // target has temptingly almost legal versions of bitwise logic ops in 256-bit
  // flavors, but no other 256-bit integer support. This could be extended to
  // handle any binop, but that may require fixing/adding other folds to avoid
  // codegen regressions.
  if (BOpcode != ISD::AND && BOpcode != ISD::OR && BOpcode != ISD::XOR)
    return SDValue();

  // We need at least one concatenation operation of a binop operand to make
  // this transform worthwhile. The concat must double the input vector sizes.
  // TODO: Should we also handle INSERT_SUBVECTOR patterns?
  SDValue LHS = peekThroughBitcasts(BinOp.getOperand(0));
  SDValue RHS = peekThroughBitcasts(BinOp.getOperand(1));
  bool ConcatL =
      LHS.getOpcode() == ISD::CONCAT_VECTORS && LHS.getNumOperands() == 2;
  bool ConcatR =
      RHS.getOpcode() == ISD::CONCAT_VECTORS && RHS.getNumOperands() == 2;
  if (!ConcatL && !ConcatR)
    return SDValue();

  // If one of the binop operands was not the result of a concat, we must
  // extract a half-sized operand for our new narrow binop.
  SDLoc DL(Extract);

  // extract (binop (concat X1, X2), (concat Y1, Y2)), N --> binop XN, YN
  // extract (binop (concat X1, X2), Y), N --> binop XN, (extract Y, N)
  // extract (binop X, (concat Y1, Y2)), N --> binop (extract X, N), YN
  SDValue X = ConcatL ? DAG.getBitcast(NarrowBVT, LHS.getOperand(ConcatOpNum))
                      : DAG.getNode(ISD::EXTRACT_SUBVECTOR, DL, NarrowBVT,
                                    BinOp.getOperand(0),
                                    DAG.getConstant(ExtBOIdx, DL, ExtBOIdxVT));

  SDValue Y = ConcatR ? DAG.getBitcast(NarrowBVT, RHS.getOperand(ConcatOpNum))
                      : DAG.getNode(ISD::EXTRACT_SUBVECTOR, DL, NarrowBVT,
                                    BinOp.getOperand(1),
                                    DAG.getConstant(ExtBOIdx, DL, ExtBOIdxVT));

  SDValue NarrowBinOp = DAG.getNode(BOpcode, DL, NarrowBVT, X, Y);
  return DAG.getBitcast(VT, NarrowBinOp);
}

/// If we are extracting a subvector from a wide vector load, convert to a
/// narrow load to eliminate the extraction:
/// (extract_subvector (load wide vector)) --> (load narrow vector)
static SDValue narrowExtractedVectorLoad(SDNode *Extract, SelectionDAG &DAG) {
  // TODO: Add support for big-endian. The offset calculation must be adjusted.
  if (DAG.getDataLayout().isBigEndian())
    return SDValue();

  auto *Ld = dyn_cast<LoadSDNode>(Extract->getOperand(0));
  auto *ExtIdx = dyn_cast<ConstantSDNode>(Extract->getOperand(1));
  if (!Ld || Ld->getExtensionType() || Ld->isVolatile() || !ExtIdx)
    return SDValue();

  // Allow targets to opt-out.
  EVT VT = Extract->getValueType(0);
  const TargetLowering &TLI = DAG.getTargetLoweringInfo();
  if (!TLI.shouldReduceLoadWidth(Ld, Ld->getExtensionType(), VT))
    return SDValue();

  // The narrow load will be offset from the base address of the old load if
  // we are extracting from something besides index 0 (little-endian).
  SDLoc DL(Extract);
  SDValue BaseAddr = Ld->getOperand(1);
  unsigned Offset = ExtIdx->getZExtValue() * VT.getScalarType().getStoreSize();

  // TODO: Use "BaseIndexOffset" to make this more effective.
  SDValue NewAddr = DAG.getMemBasePlusOffset(BaseAddr, Offset, DL);
  MachineFunction &MF = DAG.getMachineFunction();
  MachineMemOperand *MMO = MF.getMachineMemOperand(Ld->getMemOperand(), Offset,
                                                   VT.getStoreSize());
  SDValue NewLd = DAG.getLoad(VT, DL, Ld->getChain(), NewAddr, MMO);
  DAG.makeEquivalentMemoryOrdering(Ld, NewLd);
  return NewLd;
}

SDValue DAGCombiner::visitEXTRACT_SUBVECTOR(SDNode* N) {
  EVT NVT = N->getValueType(0);
  SDValue V = N->getOperand(0);

  // Extract from UNDEF is UNDEF.
  if (V.isUndef())
    return DAG.getUNDEF(NVT);

  if (TLI.isOperationLegalOrCustomOrPromote(ISD::LOAD, NVT))
    if (SDValue NarrowLoad = narrowExtractedVectorLoad(N, DAG))
      return NarrowLoad;

  // Combine:
  //    (extract_subvec (concat V1, V2, ...), i)
  // Into:
  //    Vi if possible
  // Only operand 0 is checked as 'concat' assumes all inputs of the same
  // type.
  if (V.getOpcode() == ISD::CONCAT_VECTORS &&
      isa<ConstantSDNode>(N->getOperand(1)) &&
      V.getOperand(0).getValueType() == NVT) {
    unsigned Idx = N->getConstantOperandVal(1);
    unsigned NumElems = NVT.getVectorNumElements();
    assert((Idx % NumElems) == 0 &&
           "IDX in concat is not a multiple of the result vector length.");
    return V->getOperand(Idx / NumElems);
  }

  V = peekThroughBitcasts(V);

  // If the input is a build vector. Try to make a smaller build vector.
  if (V.getOpcode() == ISD::BUILD_VECTOR) {
    if (auto *Idx = dyn_cast<ConstantSDNode>(N->getOperand(1))) {
      EVT InVT = V.getValueType();
      unsigned ExtractSize = NVT.getSizeInBits();
      unsigned EltSize = InVT.getScalarSizeInBits();
      // Only do this if we won't split any elements.
      if (ExtractSize % EltSize == 0) {
        unsigned NumElems = ExtractSize / EltSize;
        EVT EltVT = InVT.getVectorElementType();
        EVT ExtractVT = NumElems == 1 ? EltVT :
          EVT::getVectorVT(*DAG.getContext(), EltVT, NumElems);
        if ((Level < AfterLegalizeDAG ||
             (NumElems == 1 ||
              TLI.isOperationLegal(ISD::BUILD_VECTOR, ExtractVT))) &&
            (!LegalTypes || TLI.isTypeLegal(ExtractVT))) {
          unsigned IdxVal = (Idx->getZExtValue() * NVT.getScalarSizeInBits()) /
                            EltSize;
          if (NumElems == 1) {
            SDValue Src = V->getOperand(IdxVal);
            if (EltVT != Src.getValueType())
              Src = DAG.getNode(ISD::TRUNCATE, SDLoc(N), InVT, Src);

            return DAG.getBitcast(NVT, Src);
          }

          // Extract the pieces from the original build_vector.
          SDValue BuildVec = DAG.getBuildVector(ExtractVT, SDLoc(N),
                                            makeArrayRef(V->op_begin() + IdxVal,
                                                         NumElems));
          return DAG.getBitcast(NVT, BuildVec);
        }
      }
    }
  }

  if (V.getOpcode() == ISD::INSERT_SUBVECTOR) {
    // Handle only simple case where vector being inserted and vector
    // being extracted are of same size.
    EVT SmallVT = V.getOperand(1).getValueType();
    if (!NVT.bitsEq(SmallVT))
      return SDValue();

    // Only handle cases where both indexes are constants.
    auto *ExtIdx = dyn_cast<ConstantSDNode>(N->getOperand(1));
    auto *InsIdx = dyn_cast<ConstantSDNode>(V.getOperand(2));

    if (InsIdx && ExtIdx) {
      // Combine:
      //    (extract_subvec (insert_subvec V1, V2, InsIdx), ExtIdx)
      // Into:
      //    indices are equal or bit offsets are equal => V1
      //    otherwise => (extract_subvec V1, ExtIdx)
      if (InsIdx->getZExtValue() * SmallVT.getScalarSizeInBits() ==
          ExtIdx->getZExtValue() * NVT.getScalarSizeInBits())
        return DAG.getBitcast(NVT, V.getOperand(1));
      return DAG.getNode(
          ISD::EXTRACT_SUBVECTOR, SDLoc(N), NVT,
          DAG.getBitcast(N->getOperand(0).getValueType(), V.getOperand(0)),
                         N->getOperand(1));
    }
  }

  if (SDValue NarrowBOp = narrowExtractedVectorBinOp(N, DAG))
    return NarrowBOp;

  if (SimplifyDemandedVectorElts(SDValue(N, 0)))
    return SDValue(N, 0);

  return SDValue();
}

// Tries to turn a shuffle of two CONCAT_VECTORS into a single concat,
// or turn a shuffle of a single concat into simpler shuffle then concat.
static SDValue partitionShuffleOfConcats(SDNode *N, SelectionDAG &DAG) {
  EVT VT = N->getValueType(0);
  unsigned NumElts = VT.getVectorNumElements();

  SDValue N0 = N->getOperand(0);
  SDValue N1 = N->getOperand(1);
  ShuffleVectorSDNode *SVN = cast<ShuffleVectorSDNode>(N);

  SmallVector<SDValue, 4> Ops;
  EVT ConcatVT = N0.getOperand(0).getValueType();
  unsigned NumElemsPerConcat = ConcatVT.getVectorNumElements();
  unsigned NumConcats = NumElts / NumElemsPerConcat;

  // Special case: shuffle(concat(A,B)) can be more efficiently represented
  // as concat(shuffle(A,B),UNDEF) if the shuffle doesn't set any of the high
  // half vector elements.
  if (NumElemsPerConcat * 2 == NumElts && N1.isUndef() &&
      std::all_of(SVN->getMask().begin() + NumElemsPerConcat,
                  SVN->getMask().end(), [](int i) { return i == -1; })) {
    N0 = DAG.getVectorShuffle(ConcatVT, SDLoc(N), N0.getOperand(0), N0.getOperand(1),
                              makeArrayRef(SVN->getMask().begin(), NumElemsPerConcat));
    N1 = DAG.getUNDEF(ConcatVT);
    return DAG.getNode(ISD::CONCAT_VECTORS, SDLoc(N), VT, N0, N1);
  }

  // Look at every vector that's inserted. We're looking for exact
  // subvector-sized copies from a concatenated vector
  for (unsigned I = 0; I != NumConcats; ++I) {
    // Make sure we're dealing with a copy.
    unsigned Begin = I * NumElemsPerConcat;
    bool AllUndef = true, NoUndef = true;
    for (unsigned J = Begin; J != Begin + NumElemsPerConcat; ++J) {
      if (SVN->getMaskElt(J) >= 0)
        AllUndef = false;
      else
        NoUndef = false;
    }

    if (NoUndef) {
      if (SVN->getMaskElt(Begin) % NumElemsPerConcat != 0)
        return SDValue();

      for (unsigned J = 1; J != NumElemsPerConcat; ++J)
        if (SVN->getMaskElt(Begin + J - 1) + 1 != SVN->getMaskElt(Begin + J))
          return SDValue();

      unsigned FirstElt = SVN->getMaskElt(Begin) / NumElemsPerConcat;
      if (FirstElt < N0.getNumOperands())
        Ops.push_back(N0.getOperand(FirstElt));
      else
        Ops.push_back(N1.getOperand(FirstElt - N0.getNumOperands()));

    } else if (AllUndef) {
      Ops.push_back(DAG.getUNDEF(N0.getOperand(0).getValueType()));
    } else { // Mixed with general masks and undefs, can't do optimization.
      return SDValue();
    }
  }

  return DAG.getNode(ISD::CONCAT_VECTORS, SDLoc(N), VT, Ops);
}

// Attempt to combine a shuffle of 2 inputs of 'scalar sources' -
// BUILD_VECTOR or SCALAR_TO_VECTOR into a single BUILD_VECTOR.
//
// SHUFFLE(BUILD_VECTOR(), BUILD_VECTOR()) -> BUILD_VECTOR() is always
// a simplification in some sense, but it isn't appropriate in general: some
// BUILD_VECTORs are substantially cheaper than others. The general case
// of a BUILD_VECTOR requires inserting each element individually (or
// performing the equivalent in a temporary stack variable). A BUILD_VECTOR of
// all constants is a single constant pool load.  A BUILD_VECTOR where each
// element is identical is a splat.  A BUILD_VECTOR where most of the operands
// are undef lowers to a small number of element insertions.
//
// To deal with this, we currently use a bunch of mostly arbitrary heuristics.
// We don't fold shuffles where one side is a non-zero constant, and we don't
// fold shuffles if the resulting (non-splat) BUILD_VECTOR would have duplicate
// non-constant operands. This seems to work out reasonably well in practice.
static SDValue combineShuffleOfScalars(ShuffleVectorSDNode *SVN,
                                       SelectionDAG &DAG,
                                       const TargetLowering &TLI) {
  EVT VT = SVN->getValueType(0);
  unsigned NumElts = VT.getVectorNumElements();
  SDValue N0 = SVN->getOperand(0);
  SDValue N1 = SVN->getOperand(1);

  if (!N0->hasOneUse())
    return SDValue();

  // If only one of N1,N2 is constant, bail out if it is not ALL_ZEROS as
  // discussed above.
  if (!N1.isUndef()) {
    if (!N1->hasOneUse())
      return SDValue();

    bool N0AnyConst = isAnyConstantBuildVector(N0);
    bool N1AnyConst = isAnyConstantBuildVector(N1);
    if (N0AnyConst && !N1AnyConst && !ISD::isBuildVectorAllZeros(N0.getNode()))
      return SDValue();
    if (!N0AnyConst && N1AnyConst && !ISD::isBuildVectorAllZeros(N1.getNode()))
      return SDValue();
  }

  // If both inputs are splats of the same value then we can safely merge this
  // to a single BUILD_VECTOR with undef elements based on the shuffle mask.
  bool IsSplat = false;
  auto *BV0 = dyn_cast<BuildVectorSDNode>(N0);
  auto *BV1 = dyn_cast<BuildVectorSDNode>(N1);
  if (BV0 && BV1)
    if (SDValue Splat0 = BV0->getSplatValue())
      IsSplat = (Splat0 == BV1->getSplatValue());

  SmallVector<SDValue, 8> Ops;
  SmallSet<SDValue, 16> DuplicateOps;
  for (int M : SVN->getMask()) {
    SDValue Op = DAG.getUNDEF(VT.getScalarType());
    if (M >= 0) {
      int Idx = M < (int)NumElts ? M : M - NumElts;
      SDValue &S = (M < (int)NumElts ? N0 : N1);
      if (S.getOpcode() == ISD::BUILD_VECTOR) {
        Op = S.getOperand(Idx);
      } else if (S.getOpcode() == ISD::SCALAR_TO_VECTOR) {
        assert(Idx == 0 && "Unexpected SCALAR_TO_VECTOR operand index.");
        Op = S.getOperand(0);
      } else {
        // Operand can't be combined - bail out.
        return SDValue();
      }
    }

    // Don't duplicate a non-constant BUILD_VECTOR operand unless we're
    // generating a splat; semantically, this is fine, but it's likely to
    // generate low-quality code if the target can't reconstruct an appropriate
    // shuffle.
    if (!Op.isUndef() && !isa<ConstantSDNode>(Op) && !isa<ConstantFPSDNode>(Op))
      if (!IsSplat && !DuplicateOps.insert(Op).second)
        return SDValue();

    Ops.push_back(Op);
  }

  // BUILD_VECTOR requires all inputs to be of the same type, find the
  // maximum type and extend them all.
  EVT SVT = VT.getScalarType();
  if (SVT.isInteger())
    for (SDValue &Op : Ops)
      SVT = (SVT.bitsLT(Op.getValueType()) ? Op.getValueType() : SVT);
  if (SVT != VT.getScalarType())
    for (SDValue &Op : Ops)
      Op = TLI.isZExtFree(Op.getValueType(), SVT)
               ? DAG.getZExtOrTrunc(Op, SDLoc(SVN), SVT)
               : DAG.getSExtOrTrunc(Op, SDLoc(SVN), SVT);
  return DAG.getBuildVector(VT, SDLoc(SVN), Ops);
}

// Match shuffles that can be converted to any_vector_extend_in_reg.
// This is often generated during legalization.
// e.g. v4i32 <0,u,1,u> -> (v2i64 any_vector_extend_in_reg(v4i32 src))
// TODO Add support for ZERO_EXTEND_VECTOR_INREG when we have a test case.
static SDValue combineShuffleToVectorExtend(ShuffleVectorSDNode *SVN,
                                            SelectionDAG &DAG,
                                            const TargetLowering &TLI,
                                            bool LegalOperations) {
  EVT VT = SVN->getValueType(0);
  bool IsBigEndian = DAG.getDataLayout().isBigEndian();

  // TODO Add support for big-endian when we have a test case.
  if (!VT.isInteger() || IsBigEndian)
    return SDValue();

  unsigned NumElts = VT.getVectorNumElements();
  unsigned EltSizeInBits = VT.getScalarSizeInBits();
  ArrayRef<int> Mask = SVN->getMask();
  SDValue N0 = SVN->getOperand(0);

  // shuffle<0,-1,1,-1> == (v2i64 anyextend_vector_inreg(v4i32))
  auto isAnyExtend = [&Mask, &NumElts](unsigned Scale) {
    for (unsigned i = 0; i != NumElts; ++i) {
      if (Mask[i] < 0)
        continue;
      if ((i % Scale) == 0 && Mask[i] == (int)(i / Scale))
        continue;
      return false;
    }
    return true;
  };

  // Attempt to match a '*_extend_vector_inreg' shuffle, we just search for
  // power-of-2 extensions as they are the most likely.
  for (unsigned Scale = 2; Scale < NumElts; Scale *= 2) {
    // Check for non power of 2 vector sizes
    if (NumElts % Scale != 0)
      continue;
    if (!isAnyExtend(Scale))
      continue;

    EVT OutSVT = EVT::getIntegerVT(*DAG.getContext(), EltSizeInBits * Scale);
    EVT OutVT = EVT::getVectorVT(*DAG.getContext(), OutSVT, NumElts / Scale);
    // Never create an illegal type. Only create unsupported operations if we
    // are pre-legalization.
    if (TLI.isTypeLegal(OutVT))
      if (!LegalOperations ||
          TLI.isOperationLegalOrCustom(ISD::ANY_EXTEND_VECTOR_INREG, OutVT))
        return DAG.getBitcast(VT,
                              DAG.getNode(ISD::ANY_EXTEND_VECTOR_INREG,
                                          SDLoc(SVN), OutVT, N0));
  }

  return SDValue();
}

// Detect 'truncate_vector_inreg' style shuffles that pack the lower parts of
// each source element of a large type into the lowest elements of a smaller
// destination type. This is often generated during legalization.
// If the source node itself was a '*_extend_vector_inreg' node then we should
// then be able to remove it.
static SDValue combineTruncationShuffle(ShuffleVectorSDNode *SVN,
                                        SelectionDAG &DAG) {
  EVT VT = SVN->getValueType(0);
  bool IsBigEndian = DAG.getDataLayout().isBigEndian();

  // TODO Add support for big-endian when we have a test case.
  if (!VT.isInteger() || IsBigEndian)
    return SDValue();

  SDValue N0 = peekThroughBitcasts(SVN->getOperand(0));

  unsigned Opcode = N0.getOpcode();
  if (Opcode != ISD::ANY_EXTEND_VECTOR_INREG &&
      Opcode != ISD::SIGN_EXTEND_VECTOR_INREG &&
      Opcode != ISD::ZERO_EXTEND_VECTOR_INREG)
    return SDValue();

  SDValue N00 = N0.getOperand(0);
  ArrayRef<int> Mask = SVN->getMask();
  unsigned NumElts = VT.getVectorNumElements();
  unsigned EltSizeInBits = VT.getScalarSizeInBits();
  unsigned ExtSrcSizeInBits = N00.getScalarValueSizeInBits();
  unsigned ExtDstSizeInBits = N0.getScalarValueSizeInBits();

  if (ExtDstSizeInBits % ExtSrcSizeInBits != 0)
    return SDValue();
  unsigned ExtScale = ExtDstSizeInBits / ExtSrcSizeInBits;

  // (v4i32 truncate_vector_inreg(v2i64)) == shuffle<0,2-1,-1>
  // (v8i16 truncate_vector_inreg(v4i32)) == shuffle<0,2,4,6,-1,-1,-1,-1>
  // (v8i16 truncate_vector_inreg(v2i64)) == shuffle<0,4,-1,-1,-1,-1,-1,-1>
  auto isTruncate = [&Mask, &NumElts](unsigned Scale) {
    for (unsigned i = 0; i != NumElts; ++i) {
      if (Mask[i] < 0)
        continue;
      if ((i * Scale) < NumElts && Mask[i] == (int)(i * Scale))
        continue;
      return false;
    }
    return true;
  };

  // At the moment we just handle the case where we've truncated back to the
  // same size as before the extension.
  // TODO: handle more extension/truncation cases as cases arise.
  if (EltSizeInBits != ExtSrcSizeInBits)
    return SDValue();

  // We can remove *extend_vector_inreg only if the truncation happens at
  // the same scale as the extension.
  if (isTruncate(ExtScale))
    return DAG.getBitcast(VT, N00);

  return SDValue();
}

// Combine shuffles of splat-shuffles of the form:
// shuffle (shuffle V, undef, splat-mask), undef, M
// If splat-mask contains undef elements, we need to be careful about
// introducing undef's in the folded mask which are not the result of composing
// the masks of the shuffles.
static SDValue combineShuffleOfSplat(ArrayRef<int> UserMask,
                                     ShuffleVectorSDNode *Splat,
                                     SelectionDAG &DAG) {
  ArrayRef<int> SplatMask = Splat->getMask();
  assert(UserMask.size() == SplatMask.size() && "Mask length mismatch");

  // Prefer simplifying to the splat-shuffle, if possible. This is legal if
  // every undef mask element in the splat-shuffle has a corresponding undef
  // element in the user-shuffle's mask or if the composition of mask elements
  // would result in undef.
  // Examples for (shuffle (shuffle v, undef, SplatMask), undef, UserMask):
  // * UserMask=[0,2,u,u], SplatMask=[2,u,2,u] -> [2,2,u,u]
  //   In this case it is not legal to simplify to the splat-shuffle because we
  //   may be exposing the users of the shuffle an undef element at index 1
  //   which was not there before the combine.
  // * UserMask=[0,u,2,u], SplatMask=[2,u,2,u] -> [2,u,2,u]
  //   In this case the composition of masks yields SplatMask, so it's ok to
  //   simplify to the splat-shuffle.
  // * UserMask=[3,u,2,u], SplatMask=[2,u,2,u] -> [u,u,2,u]
  //   In this case the composed mask includes all undef elements of SplatMask
  //   and in addition sets element zero to undef. It is safe to simplify to
  //   the splat-shuffle.
  auto CanSimplifyToExistingSplat = [](ArrayRef<int> UserMask,
                                       ArrayRef<int> SplatMask) {
    for (unsigned i = 0, e = UserMask.size(); i != e; ++i)
      if (UserMask[i] != -1 && SplatMask[i] == -1 &&
          SplatMask[UserMask[i]] != -1)
        return false;
    return true;
  };
  if (CanSimplifyToExistingSplat(UserMask, SplatMask))
    return SDValue(Splat, 0);

  // Create a new shuffle with a mask that is composed of the two shuffles'
  // masks.
  SmallVector<int, 32> NewMask;
  for (int Idx : UserMask)
    NewMask.push_back(Idx == -1 ? -1 : SplatMask[Idx]);

  return DAG.getVectorShuffle(Splat->getValueType(0), SDLoc(Splat),
                              Splat->getOperand(0), Splat->getOperand(1),
                              NewMask);
}

/// If the shuffle mask is taking exactly one element from the first vector
/// operand and passing through all other elements from the second vector
/// operand, return the index of the mask element that is choosing an element
/// from the first operand. Otherwise, return -1.
static int getShuffleMaskIndexOfOneElementFromOp0IntoOp1(ArrayRef<int> Mask) {
  int MaskSize = Mask.size();
  int EltFromOp0 = -1;
  // TODO: This does not match if there are undef elements in the shuffle mask.
  // Should we ignore undefs in the shuffle mask instead? The trade-off is
  // removing an instruction (a shuffle), but losing the knowledge that some
  // vector lanes are not needed.
  for (int i = 0; i != MaskSize; ++i) {
    if (Mask[i] >= 0 && Mask[i] < MaskSize) {
      // We're looking for a shuffle of exactly one element from operand 0.
      if (EltFromOp0 != -1)
        return -1;
      EltFromOp0 = i;
    } else if (Mask[i] != i + MaskSize) {
      // Nothing from operand 1 can change lanes.
      return -1;
    }
  }
  return EltFromOp0;
}

/// If a shuffle inserts exactly one element from a source vector operand into
/// another vector operand and we can access the specified element as a scalar,
/// then we can eliminate the shuffle.
static SDValue replaceShuffleOfInsert(ShuffleVectorSDNode *Shuf,
                                      SelectionDAG &DAG) {
  // First, check if we are taking one element of a vector and shuffling that
  // element into another vector.
  ArrayRef<int> Mask = Shuf->getMask();
  SmallVector<int, 16> CommutedMask(Mask.begin(), Mask.end());
  SDValue Op0 = Shuf->getOperand(0);
  SDValue Op1 = Shuf->getOperand(1);
  int ShufOp0Index = getShuffleMaskIndexOfOneElementFromOp0IntoOp1(Mask);
  if (ShufOp0Index == -1) {
    // Commute mask and check again.
    ShuffleVectorSDNode::commuteMask(CommutedMask);
    ShufOp0Index = getShuffleMaskIndexOfOneElementFromOp0IntoOp1(CommutedMask);
    if (ShufOp0Index == -1)
      return SDValue();
    // Commute operands to match the commuted shuffle mask.
    std::swap(Op0, Op1);
    Mask = CommutedMask;
  }

  // The shuffle inserts exactly one element from operand 0 into operand 1.
  // Now see if we can access that element as a scalar via a real insert element
  // instruction.
  // TODO: We can try harder to locate the element as a scalar. Examples: it
  // could be an operand of SCALAR_TO_VECTOR, BUILD_VECTOR, or a constant.
  assert(Mask[ShufOp0Index] >= 0 && Mask[ShufOp0Index] < (int)Mask.size() &&
         "Shuffle mask value must be from operand 0");
  if (Op0.getOpcode() != ISD::INSERT_VECTOR_ELT)
    return SDValue();

  auto *InsIndexC = dyn_cast<ConstantSDNode>(Op0.getOperand(2));
  if (!InsIndexC || InsIndexC->getSExtValue() != Mask[ShufOp0Index])
    return SDValue();

  // There's an existing insertelement with constant insertion index, so we
  // don't need to check the legality/profitability of a replacement operation
  // that differs at most in the constant value. The target should be able to
  // lower any of those in a similar way. If not, legalization will expand this
  // to a scalar-to-vector plus shuffle.
  //
  // Note that the shuffle may move the scalar from the position that the insert
  // element used. Therefore, our new insert element occurs at the shuffle's
  // mask index value, not the insert's index value.
  // shuffle (insertelt v1, x, C), v2, mask --> insertelt v2, x, C'
  SDValue NewInsIndex = DAG.getConstant(ShufOp0Index, SDLoc(Shuf),
                                        Op0.getOperand(2).getValueType());
  return DAG.getNode(ISD::INSERT_VECTOR_ELT, SDLoc(Shuf), Op0.getValueType(),
                     Op1, Op0.getOperand(1), NewInsIndex);
}

SDValue DAGCombiner::visitVECTOR_SHUFFLE(SDNode *N) {
  EVT VT = N->getValueType(0);
  unsigned NumElts = VT.getVectorNumElements();

  SDValue N0 = N->getOperand(0);
  SDValue N1 = N->getOperand(1);

  assert(N0.getValueType() == VT && "Vector shuffle must be normalized in DAG");

  // Canonicalize shuffle undef, undef -> undef
  if (N0.isUndef() && N1.isUndef())
    return DAG.getUNDEF(VT);

  ShuffleVectorSDNode *SVN = cast<ShuffleVectorSDNode>(N);

  // Canonicalize shuffle v, v -> v, undef
  if (N0 == N1) {
    SmallVector<int, 8> NewMask;
    for (unsigned i = 0; i != NumElts; ++i) {
      int Idx = SVN->getMaskElt(i);
      if (Idx >= (int)NumElts) Idx -= NumElts;
      NewMask.push_back(Idx);
    }
    return DAG.getVectorShuffle(VT, SDLoc(N), N0, DAG.getUNDEF(VT), NewMask);
  }

  // Canonicalize shuffle undef, v -> v, undef.  Commute the shuffle mask.
  if (N0.isUndef())
    return DAG.getCommutedVectorShuffle(*SVN);

  // Remove references to rhs if it is undef
  if (N1.isUndef()) {
    bool Changed = false;
    SmallVector<int, 8> NewMask;
    for (unsigned i = 0; i != NumElts; ++i) {
      int Idx = SVN->getMaskElt(i);
      if (Idx >= (int)NumElts) {
        Idx = -1;
        Changed = true;
      }
      NewMask.push_back(Idx);
    }
    if (Changed)
      return DAG.getVectorShuffle(VT, SDLoc(N), N0, N1, NewMask);
  }

  if (SDValue InsElt = replaceShuffleOfInsert(SVN, DAG))
    return InsElt;

  // A shuffle of a single vector that is a splat can always be folded.
  if (auto *N0Shuf = dyn_cast<ShuffleVectorSDNode>(N0))
    if (N1->isUndef() && N0Shuf->isSplat())
      return combineShuffleOfSplat(SVN->getMask(), N0Shuf, DAG);

  // If it is a splat, check if the argument vector is another splat or a
  // build_vector.
  if (SVN->isSplat() && SVN->getSplatIndex() < (int)NumElts) {
    SDNode *V = N0.getNode();

    // If this is a bit convert that changes the element type of the vector but
    // not the number of vector elements, look through it.  Be careful not to
    // look though conversions that change things like v4f32 to v2f64.
    if (V->getOpcode() == ISD::BITCAST) {
      SDValue ConvInput = V->getOperand(0);
      if (ConvInput.getValueType().isVector() &&
          ConvInput.getValueType().getVectorNumElements() == NumElts)
        V = ConvInput.getNode();
    }

    if (V->getOpcode() == ISD::BUILD_VECTOR) {
      assert(V->getNumOperands() == NumElts &&
             "BUILD_VECTOR has wrong number of operands");
      SDValue Base;
      bool AllSame = true;
      for (unsigned i = 0; i != NumElts; ++i) {
        if (!V->getOperand(i).isUndef()) {
          Base = V->getOperand(i);
          break;
        }
      }
      // Splat of <u, u, u, u>, return <u, u, u, u>
      if (!Base.getNode())
        return N0;
      for (unsigned i = 0; i != NumElts; ++i) {
        if (V->getOperand(i) != Base) {
          AllSame = false;
          break;
        }
      }
      // Splat of <x, x, x, x>, return <x, x, x, x>
      if (AllSame)
        return N0;

      // Canonicalize any other splat as a build_vector.
      const SDValue &Splatted = V->getOperand(SVN->getSplatIndex());
      SmallVector<SDValue, 8> Ops(NumElts, Splatted);
      SDValue NewBV = DAG.getBuildVector(V->getValueType(0), SDLoc(N), Ops);

      // We may have jumped through bitcasts, so the type of the
      // BUILD_VECTOR may not match the type of the shuffle.
      if (V->getValueType(0) != VT)
        NewBV = DAG.getBitcast(VT, NewBV);
      return NewBV;
    }
  }

  // Simplify source operands based on shuffle mask.
  if (SimplifyDemandedVectorElts(SDValue(N, 0)))
    return SDValue(N, 0);

  // Match shuffles that can be converted to any_vector_extend_in_reg.
  if (SDValue V = combineShuffleToVectorExtend(SVN, DAG, TLI, LegalOperations))
    return V;

  // Combine "truncate_vector_in_reg" style shuffles.
  if (SDValue V = combineTruncationShuffle(SVN, DAG))
    return V;

  if (N0.getOpcode() == ISD::CONCAT_VECTORS &&
      Level < AfterLegalizeVectorOps &&
      (N1.isUndef() ||
      (N1.getOpcode() == ISD::CONCAT_VECTORS &&
       N0.getOperand(0).getValueType() == N1.getOperand(0).getValueType()))) {
    if (SDValue V = partitionShuffleOfConcats(N, DAG))
      return V;
  }

  // Attempt to combine a shuffle of 2 inputs of 'scalar sources' -
  // BUILD_VECTOR or SCALAR_TO_VECTOR into a single BUILD_VECTOR.
  if (Level < AfterLegalizeDAG && TLI.isTypeLegal(VT))
    if (SDValue Res = combineShuffleOfScalars(SVN, DAG, TLI))
      return Res;

  // If this shuffle only has a single input that is a bitcasted shuffle,
  // attempt to merge the 2 shuffles and suitably bitcast the inputs/output
  // back to their original types.
  if (N0.getOpcode() == ISD::BITCAST && N0.hasOneUse() &&
      N1.isUndef() && Level < AfterLegalizeVectorOps &&
      TLI.isTypeLegal(VT)) {
    auto ScaleShuffleMask = [](ArrayRef<int> Mask, int Scale) {
      if (Scale == 1)
        return SmallVector<int, 8>(Mask.begin(), Mask.end());

      SmallVector<int, 8> NewMask;
      for (int M : Mask)
        for (int s = 0; s != Scale; ++s)
          NewMask.push_back(M < 0 ? -1 : Scale * M + s);
      return NewMask;
    };
    
    SDValue BC0 = peekThroughOneUseBitcasts(N0);
    if (BC0.getOpcode() == ISD::VECTOR_SHUFFLE && BC0.hasOneUse()) {
      EVT SVT = VT.getScalarType();
      EVT InnerVT = BC0->getValueType(0);
      EVT InnerSVT = InnerVT.getScalarType();

      // Determine which shuffle works with the smaller scalar type.
      EVT ScaleVT = SVT.bitsLT(InnerSVT) ? VT : InnerVT;
      EVT ScaleSVT = ScaleVT.getScalarType();

      if (TLI.isTypeLegal(ScaleVT) &&
          0 == (InnerSVT.getSizeInBits() % ScaleSVT.getSizeInBits()) &&
          0 == (SVT.getSizeInBits() % ScaleSVT.getSizeInBits())) {
        int InnerScale = InnerSVT.getSizeInBits() / ScaleSVT.getSizeInBits();
        int OuterScale = SVT.getSizeInBits() / ScaleSVT.getSizeInBits();

        // Scale the shuffle masks to the smaller scalar type.
        ShuffleVectorSDNode *InnerSVN = cast<ShuffleVectorSDNode>(BC0);
        SmallVector<int, 8> InnerMask =
            ScaleShuffleMask(InnerSVN->getMask(), InnerScale);
        SmallVector<int, 8> OuterMask =
            ScaleShuffleMask(SVN->getMask(), OuterScale);

        // Merge the shuffle masks.
        SmallVector<int, 8> NewMask;
        for (int M : OuterMask)
          NewMask.push_back(M < 0 ? -1 : InnerMask[M]);

        // Test for shuffle mask legality over both commutations.
        SDValue SV0 = BC0->getOperand(0);
        SDValue SV1 = BC0->getOperand(1);
        bool LegalMask = TLI.isShuffleMaskLegal(NewMask, ScaleVT);
        if (!LegalMask) {
          std::swap(SV0, SV1);
          ShuffleVectorSDNode::commuteMask(NewMask);
          LegalMask = TLI.isShuffleMaskLegal(NewMask, ScaleVT);
        }

        if (LegalMask) {
          SV0 = DAG.getBitcast(ScaleVT, SV0);
          SV1 = DAG.getBitcast(ScaleVT, SV1);
          return DAG.getBitcast(
              VT, DAG.getVectorShuffle(ScaleVT, SDLoc(N), SV0, SV1, NewMask));
        }
      }
    }
  }

  // Canonicalize shuffles according to rules:
  //  shuffle(A, shuffle(A, B)) -> shuffle(shuffle(A,B), A)
  //  shuffle(B, shuffle(A, B)) -> shuffle(shuffle(A,B), B)
  //  shuffle(B, shuffle(A, Undef)) -> shuffle(shuffle(A, Undef), B)
  if (N1.getOpcode() == ISD::VECTOR_SHUFFLE &&
      N0.getOpcode() != ISD::VECTOR_SHUFFLE && Level < AfterLegalizeDAG &&
      TLI.isTypeLegal(VT)) {
    // The incoming shuffle must be of the same type as the result of the
    // current shuffle.
    assert(N1->getOperand(0).getValueType() == VT &&
           "Shuffle types don't match");

    SDValue SV0 = N1->getOperand(0);
    SDValue SV1 = N1->getOperand(1);
    bool HasSameOp0 = N0 == SV0;
    bool IsSV1Undef = SV1.isUndef();
    if (HasSameOp0 || IsSV1Undef || N0 == SV1)
      // Commute the operands of this shuffle so that next rule
      // will trigger.
      return DAG.getCommutedVectorShuffle(*SVN);
  }

  // Try to fold according to rules:
  //   shuffle(shuffle(A, B, M0), C, M1) -> shuffle(A, B, M2)
  //   shuffle(shuffle(A, B, M0), C, M1) -> shuffle(A, C, M2)
  //   shuffle(shuffle(A, B, M0), C, M1) -> shuffle(B, C, M2)
  // Don't try to fold shuffles with illegal type.
  // Only fold if this shuffle is the only user of the other shuffle.
  if (N0.getOpcode() == ISD::VECTOR_SHUFFLE && N->isOnlyUserOf(N0.getNode()) &&
      Level < AfterLegalizeDAG && TLI.isTypeLegal(VT)) {
    ShuffleVectorSDNode *OtherSV = cast<ShuffleVectorSDNode>(N0);

    // Don't try to fold splats; they're likely to simplify somehow, or they
    // might be free.
    if (OtherSV->isSplat())
      return SDValue();

    // The incoming shuffle must be of the same type as the result of the
    // current shuffle.
    assert(OtherSV->getOperand(0).getValueType() == VT &&
           "Shuffle types don't match");

    SDValue SV0, SV1;
    SmallVector<int, 4> Mask;
    // Compute the combined shuffle mask for a shuffle with SV0 as the first
    // operand, and SV1 as the second operand.
    for (unsigned i = 0; i != NumElts; ++i) {
      int Idx = SVN->getMaskElt(i);
      if (Idx < 0) {
        // Propagate Undef.
        Mask.push_back(Idx);
        continue;
      }

      SDValue CurrentVec;
      if (Idx < (int)NumElts) {
        // This shuffle index refers to the inner shuffle N0. Lookup the inner
        // shuffle mask to identify which vector is actually referenced.
        Idx = OtherSV->getMaskElt(Idx);
        if (Idx < 0) {
          // Propagate Undef.
          Mask.push_back(Idx);
          continue;
        }

        CurrentVec = (Idx < (int) NumElts) ? OtherSV->getOperand(0)
                                           : OtherSV->getOperand(1);
      } else {
        // This shuffle index references an element within N1.
        CurrentVec = N1;
      }

      // Simple case where 'CurrentVec' is UNDEF.
      if (CurrentVec.isUndef()) {
        Mask.push_back(-1);
        continue;
      }

      // Canonicalize the shuffle index. We don't know yet if CurrentVec
      // will be the first or second operand of the combined shuffle.
      Idx = Idx % NumElts;
      if (!SV0.getNode() || SV0 == CurrentVec) {
        // Ok. CurrentVec is the left hand side.
        // Update the mask accordingly.
        SV0 = CurrentVec;
        Mask.push_back(Idx);
        continue;
      }

      // Bail out if we cannot convert the shuffle pair into a single shuffle.
      if (SV1.getNode() && SV1 != CurrentVec)
        return SDValue();

      // Ok. CurrentVec is the right hand side.
      // Update the mask accordingly.
      SV1 = CurrentVec;
      Mask.push_back(Idx + NumElts);
    }

    // Check if all indices in Mask are Undef. In case, propagate Undef.
    bool isUndefMask = true;
    for (unsigned i = 0; i != NumElts && isUndefMask; ++i)
      isUndefMask &= Mask[i] < 0;

    if (isUndefMask)
      return DAG.getUNDEF(VT);

    if (!SV0.getNode())
      SV0 = DAG.getUNDEF(VT);
    if (!SV1.getNode())
      SV1 = DAG.getUNDEF(VT);

    // Avoid introducing shuffles with illegal mask.
    if (!TLI.isShuffleMaskLegal(Mask, VT)) {
      ShuffleVectorSDNode::commuteMask(Mask);

      if (!TLI.isShuffleMaskLegal(Mask, VT))
        return SDValue();

      //   shuffle(shuffle(A, B, M0), C, M1) -> shuffle(B, A, M2)
      //   shuffle(shuffle(A, B, M0), C, M1) -> shuffle(C, A, M2)
      //   shuffle(shuffle(A, B, M0), C, M1) -> shuffle(C, B, M2)
      std::swap(SV0, SV1);
    }

    //   shuffle(shuffle(A, B, M0), C, M1) -> shuffle(A, B, M2)
    //   shuffle(shuffle(A, B, M0), C, M1) -> shuffle(A, C, M2)
    //   shuffle(shuffle(A, B, M0), C, M1) -> shuffle(B, C, M2)
    return DAG.getVectorShuffle(VT, SDLoc(N), SV0, SV1, Mask);
  }

  return SDValue();
}

SDValue DAGCombiner::visitSCALAR_TO_VECTOR(SDNode *N) {
  SDValue InVal = N->getOperand(0);
  EVT VT = N->getValueType(0);

  // Replace a SCALAR_TO_VECTOR(EXTRACT_VECTOR_ELT(V,C0)) pattern
  // with a VECTOR_SHUFFLE and possible truncate.
  if (InVal.getOpcode() == ISD::EXTRACT_VECTOR_ELT) {
    SDValue InVec = InVal->getOperand(0);
    SDValue EltNo = InVal->getOperand(1);
    auto InVecT = InVec.getValueType();
    if (ConstantSDNode *C0 = dyn_cast<ConstantSDNode>(EltNo)) {
      SmallVector<int, 8> NewMask(InVecT.getVectorNumElements(), -1);
      int Elt = C0->getZExtValue();
      NewMask[0] = Elt;
      SDValue Val;
      // If we have an implict truncate do truncate here as long as it's legal.
      // if it's not legal, this should
      if (VT.getScalarType() != InVal.getValueType() &&
          InVal.getValueType().isScalarInteger() &&
          isTypeLegal(VT.getScalarType())) {
        Val =
            DAG.getNode(ISD::TRUNCATE, SDLoc(InVal), VT.getScalarType(), InVal);
        return DAG.getNode(ISD::SCALAR_TO_VECTOR, SDLoc(N), VT, Val);
      }
      if (VT.getScalarType() == InVecT.getScalarType() &&
          VT.getVectorNumElements() <= InVecT.getVectorNumElements() &&
          TLI.isShuffleMaskLegal(NewMask, VT)) {
        Val = DAG.getVectorShuffle(InVecT, SDLoc(N), InVec,
                                   DAG.getUNDEF(InVecT), NewMask);
        // If the initial vector is the correct size this shuffle is a
        // valid result.
        if (VT == InVecT)
          return Val;
        // If not we must truncate the vector.
        if (VT.getVectorNumElements() != InVecT.getVectorNumElements()) {
          MVT IdxTy = TLI.getVectorIdxTy(DAG.getDataLayout());
          SDValue ZeroIdx = DAG.getConstant(0, SDLoc(N), IdxTy);
          EVT SubVT =
              EVT::getVectorVT(*DAG.getContext(), InVecT.getVectorElementType(),
                               VT.getVectorNumElements());
          Val = DAG.getNode(ISD::EXTRACT_SUBVECTOR, SDLoc(N), SubVT, Val,
                            ZeroIdx);
          return Val;
        }
      }
    }
  }

  return SDValue();
}

SDValue DAGCombiner::visitINSERT_SUBVECTOR(SDNode *N) {
  EVT VT = N->getValueType(0);
  SDValue N0 = N->getOperand(0);
  SDValue N1 = N->getOperand(1);
  SDValue N2 = N->getOperand(2);

  // If inserting an UNDEF, just return the original vector.
  if (N1.isUndef())
    return N0;

  // If this is an insert of an extracted vector into an undef vector, we can
  // just use the input to the extract.
  if (N0.isUndef() && N1.getOpcode() == ISD::EXTRACT_SUBVECTOR &&
      N1.getOperand(1) == N2 && N1.getOperand(0).getValueType() == VT)
    return N1.getOperand(0);

  // If we are inserting a bitcast value into an undef, with the same
  // number of elements, just use the bitcast input of the extract.
  // i.e. INSERT_SUBVECTOR UNDEF (BITCAST N1) N2 ->
  //        BITCAST (INSERT_SUBVECTOR UNDEF N1 N2)
  if (N0.isUndef() && N1.getOpcode() == ISD::BITCAST &&
      N1.getOperand(0).getOpcode() == ISD::EXTRACT_SUBVECTOR &&
      N1.getOperand(0).getOperand(1) == N2 &&
      N1.getOperand(0).getOperand(0).getValueType().getVectorNumElements() ==
          VT.getVectorNumElements() &&
      N1.getOperand(0).getOperand(0).getValueType().getSizeInBits() ==
          VT.getSizeInBits()) {
    return DAG.getBitcast(VT, N1.getOperand(0).getOperand(0));
  }

  // If both N1 and N2 are bitcast values on which insert_subvector
  // would makes sense, pull the bitcast through.
  // i.e. INSERT_SUBVECTOR (BITCAST N0) (BITCAST N1) N2 ->
  //        BITCAST (INSERT_SUBVECTOR N0 N1 N2)
  if (N0.getOpcode() == ISD::BITCAST && N1.getOpcode() == ISD::BITCAST) {
    SDValue CN0 = N0.getOperand(0);
    SDValue CN1 = N1.getOperand(0);
    EVT CN0VT = CN0.getValueType();
    EVT CN1VT = CN1.getValueType();
    if (CN0VT.isVector() && CN1VT.isVector() &&
        CN0VT.getVectorElementType() == CN1VT.getVectorElementType() &&
        CN0VT.getVectorNumElements() == VT.getVectorNumElements()) {
      SDValue NewINSERT = DAG.getNode(ISD::INSERT_SUBVECTOR, SDLoc(N),
                                      CN0.getValueType(), CN0, CN1, N2);
      return DAG.getBitcast(VT, NewINSERT);
    }
  }

  // Combine INSERT_SUBVECTORs where we are inserting to the same index.
  // INSERT_SUBVECTOR( INSERT_SUBVECTOR( Vec, SubOld, Idx ), SubNew, Idx )
  // --> INSERT_SUBVECTOR( Vec, SubNew, Idx )
  if (N0.getOpcode() == ISD::INSERT_SUBVECTOR &&
      N0.getOperand(1).getValueType() == N1.getValueType() &&
      N0.getOperand(2) == N2)
    return DAG.getNode(ISD::INSERT_SUBVECTOR, SDLoc(N), VT, N0.getOperand(0),
                       N1, N2);

  // Eliminate an intermediate insert into an undef vector:
  // insert_subvector undef, (insert_subvector undef, X, 0), N2 -->
  // insert_subvector undef, X, N2
  if (N0.isUndef() && N1.getOpcode() == ISD::INSERT_SUBVECTOR &&
      N1.getOperand(0).isUndef() && isNullConstant(N1.getOperand(2)))
    return DAG.getNode(ISD::INSERT_SUBVECTOR, SDLoc(N), VT, N0,
                       N1.getOperand(1), N2);

  if (!isa<ConstantSDNode>(N2))
    return SDValue();

  unsigned InsIdx = cast<ConstantSDNode>(N2)->getZExtValue();

  // Canonicalize insert_subvector dag nodes.
  // Example:
  // (insert_subvector (insert_subvector A, Idx0), Idx1)
  // -> (insert_subvector (insert_subvector A, Idx1), Idx0)
  if (N0.getOpcode() == ISD::INSERT_SUBVECTOR && N0.hasOneUse() &&
      N1.getValueType() == N0.getOperand(1).getValueType() &&
      isa<ConstantSDNode>(N0.getOperand(2))) {
    unsigned OtherIdx = N0.getConstantOperandVal(2);
    if (InsIdx < OtherIdx) {
      // Swap nodes.
      SDValue NewOp = DAG.getNode(ISD::INSERT_SUBVECTOR, SDLoc(N), VT,
                                  N0.getOperand(0), N1, N2);
      AddToWorklist(NewOp.getNode());
      return DAG.getNode(ISD::INSERT_SUBVECTOR, SDLoc(N0.getNode()),
                         VT, NewOp, N0.getOperand(1), N0.getOperand(2));
    }
  }

  // If the input vector is a concatenation, and the insert replaces
  // one of the pieces, we can optimize into a single concat_vectors.
  if (N0.getOpcode() == ISD::CONCAT_VECTORS && N0.hasOneUse() &&
      N0.getOperand(0).getValueType() == N1.getValueType()) {
    unsigned Factor = N1.getValueType().getVectorNumElements();

    SmallVector<SDValue, 8> Ops(N0->op_begin(), N0->op_end());
    Ops[cast<ConstantSDNode>(N2)->getZExtValue() / Factor] = N1;

    return DAG.getNode(ISD::CONCAT_VECTORS, SDLoc(N), VT, Ops);
  }

  // Simplify source operands based on insertion.
  if (SimplifyDemandedVectorElts(SDValue(N, 0)))
    return SDValue(N, 0);

  return SDValue();
}

SDValue DAGCombiner::visitFP_TO_FP16(SDNode *N) {
  SDValue N0 = N->getOperand(0);

  // fold (fp_to_fp16 (fp16_to_fp op)) -> op
  if (N0->getOpcode() == ISD::FP16_TO_FP)
    return N0->getOperand(0);

  return SDValue();
}

SDValue DAGCombiner::visitFP16_TO_FP(SDNode *N) {
  SDValue N0 = N->getOperand(0);

  // fold fp16_to_fp(op & 0xffff) -> fp16_to_fp(op)
  if (N0->getOpcode() == ISD::AND) {
    ConstantSDNode *AndConst = getAsNonOpaqueConstant(N0.getOperand(1));
    if (AndConst && AndConst->getAPIntValue() == 0xffff) {
      return DAG.getNode(ISD::FP16_TO_FP, SDLoc(N), N->getValueType(0),
                         N0.getOperand(0));
    }
  }

  return SDValue();
}

/// Returns a vector_shuffle if it able to transform an AND to a vector_shuffle
/// with the destination vector and a zero vector.
/// e.g. AND V, <0xffffffff, 0, 0xffffffff, 0>. ==>
///      vector_shuffle V, Zero, <0, 4, 2, 4>
SDValue DAGCombiner::XformToShuffleWithZero(SDNode *N) {
  assert(N->getOpcode() == ISD::AND && "Unexpected opcode!");

  EVT VT = N->getValueType(0);
  SDValue LHS = N->getOperand(0);
  SDValue RHS = peekThroughBitcasts(N->getOperand(1));
  SDLoc DL(N);

  // Make sure we're not running after operation legalization where it
  // may have custom lowered the vector shuffles.
  if (LegalOperations)
    return SDValue();

  if (RHS.getOpcode() != ISD::BUILD_VECTOR)
    return SDValue();

  EVT RVT = RHS.getValueType();
  unsigned NumElts = RHS.getNumOperands();

  // Attempt to create a valid clear mask, splitting the mask into
  // sub elements and checking to see if each is
  // all zeros or all ones - suitable for shuffle masking.
  auto BuildClearMask = [&](int Split) {
    int NumSubElts = NumElts * Split;
    int NumSubBits = RVT.getScalarSizeInBits() / Split;

    SmallVector<int, 8> Indices;
    for (int i = 0; i != NumSubElts; ++i) {
      int EltIdx = i / Split;
      int SubIdx = i % Split;
      SDValue Elt = RHS.getOperand(EltIdx);
      if (Elt.isUndef()) {
        Indices.push_back(-1);
        continue;
      }

      APInt Bits;
      if (isa<ConstantSDNode>(Elt))
        Bits = cast<ConstantSDNode>(Elt)->getAPIntValue();
      else if (isa<ConstantFPSDNode>(Elt))
        Bits = cast<ConstantFPSDNode>(Elt)->getValueAPF().bitcastToAPInt();
      else
        return SDValue();

      // Extract the sub element from the constant bit mask.
      if (DAG.getDataLayout().isBigEndian()) {
        Bits.lshrInPlace((Split - SubIdx - 1) * NumSubBits);
      } else {
        Bits.lshrInPlace(SubIdx * NumSubBits);
      }

      if (Split > 1)
        Bits = Bits.trunc(NumSubBits);

      if (Bits.isAllOnesValue())
        Indices.push_back(i);
      else if (Bits == 0)
        Indices.push_back(i + NumSubElts);
      else
        return SDValue();
    }

    // Let's see if the target supports this vector_shuffle.
    EVT ClearSVT = EVT::getIntegerVT(*DAG.getContext(), NumSubBits);
    EVT ClearVT = EVT::getVectorVT(*DAG.getContext(), ClearSVT, NumSubElts);
    if (!TLI.isVectorClearMaskLegal(Indices, ClearVT))
      return SDValue();

    SDValue Zero = DAG.getConstant(0, DL, ClearVT);
    return DAG.getBitcast(VT, DAG.getVectorShuffle(ClearVT, DL,
                                                   DAG.getBitcast(ClearVT, LHS),
                                                   Zero, Indices));
  };

  // Determine maximum split level (byte level masking).
  int MaxSplit = 1;
  if (RVT.getScalarSizeInBits() % 8 == 0)
    MaxSplit = RVT.getScalarSizeInBits() / 8;

  for (int Split = 1; Split <= MaxSplit; ++Split)
    if (RVT.getScalarSizeInBits() % Split == 0)
      if (SDValue S = BuildClearMask(Split))
        return S;

  return SDValue();
}

/// Visit a binary vector operation, like ADD.
SDValue DAGCombiner::SimplifyVBinOp(SDNode *N) {
  assert(N->getValueType(0).isVector() &&
         "SimplifyVBinOp only works on vectors!");

  SDValue LHS = N->getOperand(0);
  SDValue RHS = N->getOperand(1);
  SDValue Ops[] = {LHS, RHS};

  // See if we can constant fold the vector operation.
  if (SDValue Fold = DAG.FoldConstantVectorArithmetic(
          N->getOpcode(), SDLoc(LHS), LHS.getValueType(), Ops, N->getFlags()))
    return Fold;

  // Type legalization might introduce new shuffles in the DAG.
  // Fold (VBinOp (shuffle (A, Undef, Mask)), (shuffle (B, Undef, Mask)))
  //   -> (shuffle (VBinOp (A, B)), Undef, Mask).
  if (LegalTypes && isa<ShuffleVectorSDNode>(LHS) &&
      isa<ShuffleVectorSDNode>(RHS) && LHS.hasOneUse() && RHS.hasOneUse() &&
      LHS.getOperand(1).isUndef() &&
      RHS.getOperand(1).isUndef()) {
    ShuffleVectorSDNode *SVN0 = cast<ShuffleVectorSDNode>(LHS);
    ShuffleVectorSDNode *SVN1 = cast<ShuffleVectorSDNode>(RHS);

    if (SVN0->getMask().equals(SVN1->getMask())) {
      EVT VT = N->getValueType(0);
      SDValue UndefVector = LHS.getOperand(1);
      SDValue NewBinOp = DAG.getNode(N->getOpcode(), SDLoc(N), VT,
                                     LHS.getOperand(0), RHS.getOperand(0),
                                     N->getFlags());
      AddUsersToWorklist(N);
      return DAG.getVectorShuffle(VT, SDLoc(N), NewBinOp, UndefVector,
                                  SVN0->getMask());
    }
  }

  return SDValue();
}

SDValue DAGCombiner::SimplifySelect(const SDLoc &DL, SDValue N0, SDValue N1,
                                    SDValue N2) {
  assert(N0.getOpcode() ==ISD::SETCC && "First argument must be a SetCC node!");

  SDValue SCC = SimplifySelectCC(DL, N0.getOperand(0), N0.getOperand(1), N1, N2,
                                 cast<CondCodeSDNode>(N0.getOperand(2))->get());

  // If we got a simplified select_cc node back from SimplifySelectCC, then
  // break it down into a new SETCC node, and a new SELECT node, and then return
  // the SELECT node, since we were called with a SELECT node.
  if (SCC.getNode()) {
    // Check to see if we got a select_cc back (to turn into setcc/select).
    // Otherwise, just return whatever node we got back, like fabs.
    if (SCC.getOpcode() == ISD::SELECT_CC) {
      SDValue SETCC = DAG.getNode(ISD::SETCC, SDLoc(N0),
                                  N0.getValueType(),
                                  SCC.getOperand(0), SCC.getOperand(1),
                                  SCC.getOperand(4));
      AddToWorklist(SETCC.getNode());
      return DAG.getSelect(SDLoc(SCC), SCC.getValueType(), SETCC,
                           SCC.getOperand(2), SCC.getOperand(3));
    }

    return SCC;
  }
  return SDValue();
}

/// Given a SELECT or a SELECT_CC node, where LHS and RHS are the two values
/// being selected between, see if we can simplify the select.  Callers of this
/// should assume that TheSelect is deleted if this returns true.  As such, they
/// should return the appropriate thing (e.g. the node) back to the top-level of
/// the DAG combiner loop to avoid it being looked at.
bool DAGCombiner::SimplifySelectOps(SDNode *TheSelect, SDValue LHS,
                                    SDValue RHS) {
  // fold (select (setcc x, [+-]0.0, *lt), NaN, (fsqrt x))
  // The select + setcc is redundant, because fsqrt returns NaN for X < 0.
  if (const ConstantFPSDNode *NaN = isConstOrConstSplatFP(LHS)) {
    if (NaN->isNaN() && RHS.getOpcode() == ISD::FSQRT) {
      // We have: (select (setcc ?, ?, ?), NaN, (fsqrt ?))
      SDValue Sqrt = RHS;
      ISD::CondCode CC;
      SDValue CmpLHS;
      const ConstantFPSDNode *Zero = nullptr;

      if (TheSelect->getOpcode() == ISD::SELECT_CC) {
        CC = cast<CondCodeSDNode>(TheSelect->getOperand(4))->get();
        CmpLHS = TheSelect->getOperand(0);
        Zero = isConstOrConstSplatFP(TheSelect->getOperand(1));
      } else {
        // SELECT or VSELECT
        SDValue Cmp = TheSelect->getOperand(0);
        if (Cmp.getOpcode() == ISD::SETCC) {
          CC = cast<CondCodeSDNode>(Cmp.getOperand(2))->get();
          CmpLHS = Cmp.getOperand(0);
          Zero = isConstOrConstSplatFP(Cmp.getOperand(1));
        }
      }
      if (Zero && Zero->isZero() &&
          Sqrt.getOperand(0) == CmpLHS && (CC == ISD::SETOLT ||
          CC == ISD::SETULT || CC == ISD::SETLT)) {
        // We have: (select (setcc x, [+-]0.0, *lt), NaN, (fsqrt x))
        CombineTo(TheSelect, Sqrt);
        return true;
      }
    }
  }
  // Cannot simplify select with vector condition
  if (TheSelect->getOperand(0).getValueType().isVector()) return false;

  // If this is a select from two identical things, try to pull the operation
  // through the select.
  if (LHS.getOpcode() != RHS.getOpcode() ||
      !LHS.hasOneUse() || !RHS.hasOneUse())
    return false;

  // If this is a load and the token chain is identical, replace the select
  // of two loads with a load through a select of the address to load from.
  // This triggers in things like "select bool X, 10.0, 123.0" after the FP
  // constants have been dropped into the constant pool.
  if (LHS.getOpcode() == ISD::LOAD) {
    LoadSDNode *LLD = cast<LoadSDNode>(LHS);
    LoadSDNode *RLD = cast<LoadSDNode>(RHS);

    // Token chains must be identical.
    if (LHS.getOperand(0) != RHS.getOperand(0) ||
        // Do not let this transformation reduce the number of volatile loads.
        LLD->isVolatile() || RLD->isVolatile() ||
        // FIXME: If either is a pre/post inc/dec load,
        // we'd need to split out the address adjustment.
        LLD->isIndexed() || RLD->isIndexed() ||
        // If this is an EXTLOAD, the VT's must match.
        LLD->getMemoryVT() != RLD->getMemoryVT() ||
        // If this is an EXTLOAD, the kind of extension must match.
        (LLD->getExtensionType() != RLD->getExtensionType() &&
         // The only exception is if one of the extensions is anyext.
         LLD->getExtensionType() != ISD::EXTLOAD &&
         RLD->getExtensionType() != ISD::EXTLOAD) ||
        // FIXME: this discards src value information.  This is
        // over-conservative. It would be beneficial to be able to remember
        // both potential memory locations.  Since we are discarding
        // src value info, don't do the transformation if the memory
        // locations are not in the default address space.
        LLD->getPointerInfo().getAddrSpace() != 0 ||
        RLD->getPointerInfo().getAddrSpace() != 0 ||
        !TLI.isOperationLegalOrCustom(TheSelect->getOpcode(),
                                      LLD->getBasePtr().getValueType()))
      return false;

    // The loads must not depend on one another.
    if (LLD->isPredecessorOf(RLD) || RLD->isPredecessorOf(LLD))
      return false;

    // Check that the select condition doesn't reach either load.  If so,
    // folding this will induce a cycle into the DAG.  If not, this is safe to
    // xform, so create a select of the addresses.

    SmallPtrSet<const SDNode *, 32> Visited;
    SmallVector<const SDNode *, 16> Worklist;

    // Always fail if LLD and RLD are not independent. TheSelect is a
    // predecessor to all Nodes in question so we need not search past it.

    Visited.insert(TheSelect);
    Worklist.push_back(LLD);
    Worklist.push_back(RLD);

    if (SDNode::hasPredecessorHelper(LLD, Visited, Worklist) ||
        SDNode::hasPredecessorHelper(RLD, Visited, Worklist))
      return false;

    SDValue Addr;
    if (TheSelect->getOpcode() == ISD::SELECT) {
      // We cannot do this optimization if any pair of {RLD, LLD} is a
      // predecessor to {RLD, LLD, CondNode}. As we've already compared the
      // Loads, we only need to check if CondNode is a successor to one of the
      // loads. We can further avoid this if there's no use of their chain
      // value.
      SDNode *CondNode = TheSelect->getOperand(0).getNode();
      Worklist.push_back(CondNode);

      if ((LLD->hasAnyUseOfValue(1) &&
           SDNode::hasPredecessorHelper(LLD, Visited, Worklist)) ||
          (RLD->hasAnyUseOfValue(1) &&
           SDNode::hasPredecessorHelper(RLD, Visited, Worklist)))
        return false;

      Addr = DAG.getSelect(SDLoc(TheSelect),
                           LLD->getBasePtr().getValueType(),
                           TheSelect->getOperand(0), LLD->getBasePtr(),
                           RLD->getBasePtr());
    } else {  // Otherwise SELECT_CC
      // We cannot do this optimization if any pair of {RLD, LLD} is a
      // predecessor to {RLD, LLD, CondLHS, CondRHS}. As we've already compared
      // the Loads, we only need to check if CondLHS/CondRHS is a successor to
      // one of the loads. We can further avoid this if there's no use of their
      // chain value.

      SDNode *CondLHS = TheSelect->getOperand(0).getNode();
      SDNode *CondRHS = TheSelect->getOperand(1).getNode();
      Worklist.push_back(CondLHS);
      Worklist.push_back(CondRHS);

      if ((LLD->hasAnyUseOfValue(1) &&
           SDNode::hasPredecessorHelper(LLD, Visited, Worklist)) ||
          (RLD->hasAnyUseOfValue(1) &&
           SDNode::hasPredecessorHelper(RLD, Visited, Worklist)))
        return false;

      Addr = DAG.getNode(ISD::SELECT_CC, SDLoc(TheSelect),
                         LLD->getBasePtr().getValueType(),
                         TheSelect->getOperand(0),
                         TheSelect->getOperand(1),
                         LLD->getBasePtr(), RLD->getBasePtr(),
                         TheSelect->getOperand(4));
    }

    SDValue Load;
    // It is safe to replace the two loads if they have different alignments,
    // but the new load must be the minimum (most restrictive) alignment of the
    // inputs.
    unsigned Alignment = std::min(LLD->getAlignment(), RLD->getAlignment());
    MachineMemOperand::Flags MMOFlags = LLD->getMemOperand()->getFlags();
    if (!RLD->isInvariant())
      MMOFlags &= ~MachineMemOperand::MOInvariant;
    if (!RLD->isDereferenceable())
      MMOFlags &= ~MachineMemOperand::MODereferenceable;
    if (LLD->getExtensionType() == ISD::NON_EXTLOAD) {
      // FIXME: Discards pointer and AA info.
      Load = DAG.getLoad(TheSelect->getValueType(0), SDLoc(TheSelect),
                         LLD->getChain(), Addr, MachinePointerInfo(), Alignment,
                         MMOFlags);
    } else {
      // FIXME: Discards pointer and AA info.
      Load = DAG.getExtLoad(
          LLD->getExtensionType() == ISD::EXTLOAD ? RLD->getExtensionType()
                                                  : LLD->getExtensionType(),
          SDLoc(TheSelect), TheSelect->getValueType(0), LLD->getChain(), Addr,
          MachinePointerInfo(), LLD->getMemoryVT(), Alignment, MMOFlags);
    }

    // Users of the select now use the result of the load.
    CombineTo(TheSelect, Load);

    // Users of the old loads now use the new load's chain.  We know the
    // old-load value is dead now.
    CombineTo(LHS.getNode(), Load.getValue(0), Load.getValue(1));
    CombineTo(RHS.getNode(), Load.getValue(0), Load.getValue(1));
    return true;
  }

  return false;
}

/// Try to fold an expression of the form (N0 cond N1) ? N2 : N3 to a shift and
/// bitwise 'and'.
SDValue DAGCombiner::foldSelectCCToShiftAnd(const SDLoc &DL, SDValue N0,
                                            SDValue N1, SDValue N2, SDValue N3,
                                            ISD::CondCode CC) {
  // If this is a select where the false operand is zero and the compare is a
  // check of the sign bit, see if we can perform the "gzip trick":
  // select_cc setlt X, 0, A, 0 -> and (sra X, size(X)-1), A
  // select_cc setgt X, 0, A, 0 -> and (not (sra X, size(X)-1)), A
  EVT XType = N0.getValueType();
  EVT AType = N2.getValueType();
  if (!isNullConstant(N3) || !XType.bitsGE(AType))
    return SDValue();

  // If the comparison is testing for a positive value, we have to invert
  // the sign bit mask, so only do that transform if the target has a bitwise
  // 'and not' instruction (the invert is free).
  if (CC == ISD::SETGT && TLI.hasAndNot(N2)) {
    // (X > -1) ? A : 0
    // (X >  0) ? X : 0 <-- This is canonical signed max.
    if (!(isAllOnesConstant(N1) || (isNullConstant(N1) && N0 == N2)))
      return SDValue();
  } else if (CC == ISD::SETLT) {
    // (X <  0) ? A : 0
    // (X <  1) ? X : 0 <-- This is un-canonicalized signed min.
    if (!(isNullConstant(N1) || (isOneConstant(N1) && N0 == N2)))
      return SDValue();
  } else {
    return SDValue();
  }

  // and (sra X, size(X)-1), A -> "and (srl X, C2), A" iff A is a single-bit
  // constant.
  EVT ShiftAmtTy = getShiftAmountTy(N0.getValueType());
  auto *N2C = dyn_cast<ConstantSDNode>(N2.getNode());
  if (N2C && ((N2C->getAPIntValue() & (N2C->getAPIntValue() - 1)) == 0)) {
    unsigned ShCt = XType.getSizeInBits() - N2C->getAPIntValue().logBase2() - 1;
    SDValue ShiftAmt = DAG.getConstant(ShCt, DL, ShiftAmtTy);
    SDValue Shift = DAG.getNode(ISD::SRL, DL, XType, N0, ShiftAmt);
    AddToWorklist(Shift.getNode());

    if (XType.bitsGT(AType)) {
      Shift = DAG.getNode(ISD::TRUNCATE, DL, AType, Shift);
      AddToWorklist(Shift.getNode());
    }

    if (CC == ISD::SETGT)
      Shift = DAG.getNOT(DL, Shift, AType);

    return DAG.getNode(ISD::AND, DL, AType, Shift, N2);
  }

  SDValue ShiftAmt = DAG.getConstant(XType.getSizeInBits() - 1, DL, ShiftAmtTy);
  SDValue Shift = DAG.getNode(ISD::SRA, DL, XType, N0, ShiftAmt);
  AddToWorklist(Shift.getNode());

  if (XType.bitsGT(AType)) {
    Shift = DAG.getNode(ISD::TRUNCATE, DL, AType, Shift);
    AddToWorklist(Shift.getNode());
  }

  if (CC == ISD::SETGT)
    Shift = DAG.getNOT(DL, Shift, AType);

  return DAG.getNode(ISD::AND, DL, AType, Shift, N2);
}

/// Turn "(a cond b) ? 1.0f : 2.0f" into "load (tmp + ((a cond b) ? 0 : 4)"
/// where "tmp" is a constant pool entry containing an array with 1.0 and 2.0
/// in it. This may be a win when the constant is not otherwise available
/// because it replaces two constant pool loads with one.
SDValue DAGCombiner::convertSelectOfFPConstantsToLoadOffset(
    const SDLoc &DL, SDValue N0, SDValue N1, SDValue N2, SDValue N3,
    ISD::CondCode CC) {
  if (!TLI.reduceSelectOfFPConstantLoads(N0.getValueType().isFloatingPoint()))
    return SDValue();

  // If we are before legalize types, we want the other legalization to happen
  // first (for example, to avoid messing with soft float).
  auto *TV = dyn_cast<ConstantFPSDNode>(N2);
  auto *FV = dyn_cast<ConstantFPSDNode>(N3);
  EVT VT = N2.getValueType();
  if (!TV || !FV || !TLI.isTypeLegal(VT))
    return SDValue();

  // If a constant can be materialized without loads, this does not make sense.
  if (TLI.getOperationAction(ISD::ConstantFP, VT) == TargetLowering::Legal ||
      TLI.isFPImmLegal(TV->getValueAPF(), TV->getValueType(0)) ||
      TLI.isFPImmLegal(FV->getValueAPF(), FV->getValueType(0)))
    return SDValue();

  // If both constants have multiple uses, then we won't need to do an extra
  // load. The values are likely around in registers for other users.
  if (!TV->hasOneUse() && !FV->hasOneUse())
    return SDValue();

  Constant *Elts[] = { const_cast<ConstantFP*>(FV->getConstantFPValue()),
                       const_cast<ConstantFP*>(TV->getConstantFPValue()) };
  Type *FPTy = Elts[0]->getType();
  const DataLayout &TD = DAG.getDataLayout();

  // Create a ConstantArray of the two constants.
  Constant *CA = ConstantArray::get(ArrayType::get(FPTy, 2), Elts);
  SDValue CPIdx = DAG.getConstantPool(CA, TLI.getPointerTy(DAG.getDataLayout()),
                                      TD.getPrefTypeAlignment(FPTy));
  unsigned Alignment = cast<ConstantPoolSDNode>(CPIdx)->getAlignment();

  // Get offsets to the 0 and 1 elements of the array, so we can select between
  // them.
  SDValue Zero = DAG.getIntPtrConstant(0, DL);
  unsigned EltSize = (unsigned)TD.getTypeAllocSize(Elts[0]->getType());
  SDValue One = DAG.getIntPtrConstant(EltSize, SDLoc(FV));
  SDValue Cond =
      DAG.getSetCC(DL, getSetCCResultType(N0.getValueType()), N0, N1, CC);
  AddToWorklist(Cond.getNode());
  SDValue CstOffset = DAG.getSelect(DL, Zero.getValueType(), Cond, One, Zero);
  AddToWorklist(CstOffset.getNode());
  CPIdx = DAG.getNode(ISD::ADD, DL, CPIdx.getValueType(), CPIdx, CstOffset);
  AddToWorklist(CPIdx.getNode());
  return DAG.getLoad(TV->getValueType(0), DL, DAG.getEntryNode(), CPIdx,
                     MachinePointerInfo::getConstantPool(
                         DAG.getMachineFunction()), Alignment);
}

/// Simplify an expression of the form (N0 cond N1) ? N2 : N3
/// where 'cond' is the comparison specified by CC.
SDValue DAGCombiner::SimplifySelectCC(const SDLoc &DL, SDValue N0, SDValue N1,
                                      SDValue N2, SDValue N3, ISD::CondCode CC,
                                      bool NotExtCompare) {
  // (x ? y : y) -> y.
  if (N2 == N3) return N2;

  EVT CmpOpVT = N0.getValueType();
  EVT VT = N2.getValueType();
  auto *N1C = dyn_cast<ConstantSDNode>(N1.getNode());
  auto *N2C = dyn_cast<ConstantSDNode>(N2.getNode());
  auto *N3C = dyn_cast<ConstantSDNode>(N3.getNode());

  // Determine if the condition we're dealing with is constant.
  SDValue SCC = SimplifySetCC(getSetCCResultType(CmpOpVT), N0, N1, CC, DL,
                              false);
  if (SCC.getNode()) AddToWorklist(SCC.getNode());

  if (auto *SCCC = dyn_cast_or_null<ConstantSDNode>(SCC.getNode())) {
    // fold select_cc true, x, y -> x
    // fold select_cc false, x, y -> y
    return !SCCC->isNullValue() ? N2 : N3;
  }

  if (SDValue V =
          convertSelectOfFPConstantsToLoadOffset(DL, N0, N1, N2, N3, CC))
    return V;

  if (SDValue V = foldSelectCCToShiftAnd(DL, N0, N1, N2, N3, CC))
    return V;

  // fold (select_cc seteq (and x, y), 0, 0, A) -> (and (shr (shl x)) A)
  // where y is has a single bit set.
  // A plaintext description would be, we can turn the SELECT_CC into an AND
  // when the condition can be materialized as an all-ones register.  Any
  // single bit-test can be materialized as an all-ones register with
  // shift-left and shift-right-arith.
  if (CC == ISD::SETEQ && N0->getOpcode() == ISD::AND &&
      N0->getValueType(0) == VT && isNullConstant(N1) && isNullConstant(N2)) {
    SDValue AndLHS = N0->getOperand(0);
    auto *ConstAndRHS = dyn_cast<ConstantSDNode>(N0->getOperand(1));
    if (ConstAndRHS && ConstAndRHS->getAPIntValue().countPopulation() == 1) {
      // Shift the tested bit over the sign bit.
      const APInt &AndMask = ConstAndRHS->getAPIntValue();
      SDValue ShlAmt =
        DAG.getConstant(AndMask.countLeadingZeros(), SDLoc(AndLHS),
                        getShiftAmountTy(AndLHS.getValueType()));
      SDValue Shl = DAG.getNode(ISD::SHL, SDLoc(N0), VT, AndLHS, ShlAmt);

      // Now arithmetic right shift it all the way over, so the result is either
      // all-ones, or zero.
      SDValue ShrAmt =
        DAG.getConstant(AndMask.getBitWidth() - 1, SDLoc(Shl),
                        getShiftAmountTy(Shl.getValueType()));
      SDValue Shr = DAG.getNode(ISD::SRA, SDLoc(N0), VT, Shl, ShrAmt);

      return DAG.getNode(ISD::AND, DL, VT, Shr, N3);
    }
  }

  // fold select C, 16, 0 -> shl C, 4
  bool Fold = N2C && isNullConstant(N3) && N2C->getAPIntValue().isPowerOf2();
  bool Swap = N3C && isNullConstant(N2) && N3C->getAPIntValue().isPowerOf2();

  if ((Fold || Swap) &&
      TLI.getBooleanContents(CmpOpVT) ==
          TargetLowering::ZeroOrOneBooleanContent &&
      (!LegalOperations || TLI.isOperationLegal(ISD::SETCC, CmpOpVT))) {

    if (Swap) {
      CC = ISD::getSetCCInverse(CC, CmpOpVT.isInteger());
      std::swap(N2C, N3C);
    }

    // If the caller doesn't want us to simplify this into a zext of a compare,
    // don't do it.
    if (NotExtCompare && N2C->isOne())
      return SDValue();

    SDValue Temp, SCC;
    // zext (setcc n0, n1)
    if (LegalTypes) {
      SCC = DAG.getSetCC(DL, getSetCCResultType(CmpOpVT), N0, N1, CC);
      if (VT.bitsLT(SCC.getValueType()))
        Temp = DAG.getZeroExtendInReg(SCC, SDLoc(N2), VT);
      else
        Temp = DAG.getNode(ISD::ZERO_EXTEND, SDLoc(N2), VT, SCC);
    } else {
      SCC = DAG.getSetCC(SDLoc(N0), MVT::i1, N0, N1, CC);
      Temp = DAG.getNode(ISD::ZERO_EXTEND, SDLoc(N2), VT, SCC);
    }

    AddToWorklist(SCC.getNode());
    AddToWorklist(Temp.getNode());

    if (N2C->isOne())
      return Temp;

    // shl setcc result by log2 n2c
    return DAG.getNode(ISD::SHL, DL, N2.getValueType(), Temp,
                       DAG.getConstant(N2C->getAPIntValue().logBase2(),
                                       SDLoc(Temp),
                                       getShiftAmountTy(Temp.getValueType())));
  }

  // Check to see if this is an integer abs.
  // select_cc setg[te] X,  0,  X, -X ->
  // select_cc setgt    X, -1,  X, -X ->
  // select_cc setl[te] X,  0, -X,  X ->
  // select_cc setlt    X,  1, -X,  X ->
  // Y = sra (X, size(X)-1); xor (add (X, Y), Y)
  if (N1C) {
    ConstantSDNode *SubC = nullptr;
    if (((N1C->isNullValue() && (CC == ISD::SETGT || CC == ISD::SETGE)) ||
         (N1C->isAllOnesValue() && CC == ISD::SETGT)) &&
        N0 == N2 && N3.getOpcode() == ISD::SUB && N0 == N3.getOperand(1))
      SubC = dyn_cast<ConstantSDNode>(N3.getOperand(0));
    else if (((N1C->isNullValue() && (CC == ISD::SETLT || CC == ISD::SETLE)) ||
              (N1C->isOne() && CC == ISD::SETLT)) &&
             N0 == N3 && N2.getOpcode() == ISD::SUB && N0 == N2.getOperand(1))
      SubC = dyn_cast<ConstantSDNode>(N2.getOperand(0));

    if (SubC && SubC->isNullValue() && CmpOpVT.isInteger()) {
      SDLoc DL(N0);
      SDValue Shift = DAG.getNode(ISD::SRA, DL, CmpOpVT, N0,
                                  DAG.getConstant(CmpOpVT.getSizeInBits() - 1,
                                                  DL,
                                                  getShiftAmountTy(CmpOpVT)));
      SDValue Add = DAG.getNode(ISD::ADD, DL, CmpOpVT, N0, Shift);
      AddToWorklist(Shift.getNode());
      AddToWorklist(Add.getNode());
      return DAG.getNode(ISD::XOR, DL, CmpOpVT, Add, Shift);
    }
  }

  // select_cc seteq X, 0, sizeof(X), ctlz(X) -> ctlz(X)
  // select_cc seteq X, 0, sizeof(X), ctlz_zero_undef(X) -> ctlz(X)
  // select_cc seteq X, 0, sizeof(X), cttz(X) -> cttz(X)
  // select_cc seteq X, 0, sizeof(X), cttz_zero_undef(X) -> cttz(X)
  // select_cc setne X, 0, ctlz(X), sizeof(X) -> ctlz(X)
  // select_cc setne X, 0, ctlz_zero_undef(X), sizeof(X) -> ctlz(X)
  // select_cc setne X, 0, cttz(X), sizeof(X) -> cttz(X)
  // select_cc setne X, 0, cttz_zero_undef(X), sizeof(X) -> cttz(X)
  if (N1C && N1C->isNullValue() && (CC == ISD::SETEQ || CC == ISD::SETNE)) {
    SDValue ValueOnZero = N2;
    SDValue Count = N3;
    // If the condition is NE instead of E, swap the operands.
    if (CC == ISD::SETNE)
      std::swap(ValueOnZero, Count);
    // Check if the value on zero is a constant equal to the bits in the type.
    if (auto *ValueOnZeroC = dyn_cast<ConstantSDNode>(ValueOnZero)) {
      if (ValueOnZeroC->getAPIntValue() == VT.getSizeInBits()) {
        // If the other operand is cttz/cttz_zero_undef of N0, and cttz is
        // legal, combine to just cttz.
        if ((Count.getOpcode() == ISD::CTTZ ||
             Count.getOpcode() == ISD::CTTZ_ZERO_UNDEF) &&
            N0 == Count.getOperand(0) &&
            (!LegalOperations || TLI.isOperationLegal(ISD::CTTZ, VT)))
          return DAG.getNode(ISD::CTTZ, DL, VT, N0);
        // If the other operand is ctlz/ctlz_zero_undef of N0, and ctlz is
        // legal, combine to just ctlz.
        if ((Count.getOpcode() == ISD::CTLZ ||
             Count.getOpcode() == ISD::CTLZ_ZERO_UNDEF) &&
            N0 == Count.getOperand(0) &&
            (!LegalOperations || TLI.isOperationLegal(ISD::CTLZ, VT)))
          return DAG.getNode(ISD::CTLZ, DL, VT, N0);
      }
    }
  }

  return SDValue();
}

/// This is a stub for TargetLowering::SimplifySetCC.
SDValue DAGCombiner::SimplifySetCC(EVT VT, SDValue N0, SDValue N1,
                                   ISD::CondCode Cond, const SDLoc &DL,
                                   bool foldBooleans) {
  TargetLowering::DAGCombinerInfo
    DagCombineInfo(DAG, Level, false, this);
  return TLI.SimplifySetCC(VT, N0, N1, Cond, foldBooleans, DagCombineInfo, DL);
}

/// Given an ISD::SDIV node expressing a divide by constant, return
/// a DAG expression to select that will generate the same value by multiplying
/// by a magic number.
/// Ref: "Hacker's Delight" or "The PowerPC Compiler Writer's Guide".
SDValue DAGCombiner::BuildSDIV(SDNode *N) {
  // when optimising for minimum size, we don't want to expand a div to a mul
  // and a shift.
  if (DAG.getMachineFunction().getFunction().optForMinSize())
    return SDValue();

  SmallVector<SDNode *, 8> Built;
  if (SDValue S = TLI.BuildSDIV(N, DAG, LegalOperations, Built)) {
    for (SDNode *N : Built)
      AddToWorklist(N);
    return S;
  }

  return SDValue();
}

/// Given an ISD::SDIV node expressing a divide by constant power of 2, return a
/// DAG expression that will generate the same value by right shifting.
SDValue DAGCombiner::BuildSDIVPow2(SDNode *N) {
  ConstantSDNode *C = isConstOrConstSplat(N->getOperand(1));
  if (!C)
    return SDValue();

  // Avoid division by zero.
  if (C->isNullValue())
    return SDValue();

  SmallVector<SDNode *, 8> Built;
  if (SDValue S = TLI.BuildSDIVPow2(N, C->getAPIntValue(), DAG, Built)) {
    for (SDNode *N : Built)
      AddToWorklist(N);
    return S;
  }

  return SDValue();
}

/// Given an ISD::UDIV node expressing a divide by constant, return a DAG
/// expression that will generate the same value by multiplying by a magic
/// number.
/// Ref: "Hacker's Delight" or "The PowerPC Compiler Writer's Guide".
SDValue DAGCombiner::BuildUDIV(SDNode *N) {
  // when optimising for minimum size, we don't want to expand a div to a mul
  // and a shift.
  if (DAG.getMachineFunction().getFunction().optForMinSize())
    return SDValue();

  SmallVector<SDNode *, 8> Built;
  if (SDValue S = TLI.BuildUDIV(N, DAG, LegalOperations, Built)) {
    for (SDNode *N : Built)
      AddToWorklist(N);
    return S;
  }

  return SDValue();
}

/// Determines the LogBase2 value for a non-null input value using the
/// transform: LogBase2(V) = (EltBits - 1) - ctlz(V).
SDValue DAGCombiner::BuildLogBase2(SDValue V, const SDLoc &DL) {
  EVT VT = V.getValueType();
  unsigned EltBits = VT.getScalarSizeInBits();
  SDValue Ctlz = DAG.getNode(ISD::CTLZ, DL, VT, V);
  SDValue Base = DAG.getConstant(EltBits - 1, DL, VT);
  SDValue LogBase2 = DAG.getNode(ISD::SUB, DL, VT, Base, Ctlz);
  return LogBase2;
}

/// Newton iteration for a function: F(X) is X_{i+1} = X_i - F(X_i)/F'(X_i)
/// For the reciprocal, we need to find the zero of the function:
///   F(X) = A X - 1 [which has a zero at X = 1/A]
///     =>
///   X_{i+1} = X_i (2 - A X_i) = X_i + X_i (1 - A X_i) [this second form
///     does not require additional intermediate precision]
SDValue DAGCombiner::BuildReciprocalEstimate(SDValue Op, SDNodeFlags Flags) {
  if (Level >= AfterLegalizeDAG)
    return SDValue();

  // TODO: Handle half and/or extended types?
  EVT VT = Op.getValueType();
  if (VT.getScalarType() != MVT::f32 && VT.getScalarType() != MVT::f64)
    return SDValue();

  // If estimates are explicitly disabled for this function, we're done.
  MachineFunction &MF = DAG.getMachineFunction();
  int Enabled = TLI.getRecipEstimateDivEnabled(VT, MF);
  if (Enabled == TLI.ReciprocalEstimate::Disabled)
    return SDValue();

  // Estimates may be explicitly enabled for this type with a custom number of
  // refinement steps.
  int Iterations = TLI.getDivRefinementSteps(VT, MF);
  if (SDValue Est = TLI.getRecipEstimate(Op, DAG, Enabled, Iterations)) {
    AddToWorklist(Est.getNode());

    if (Iterations) {
      EVT VT = Op.getValueType();
      SDLoc DL(Op);
      SDValue FPOne = DAG.getConstantFP(1.0, DL, VT);

      // Newton iterations: Est = Est + Est (1 - Arg * Est)
      for (int i = 0; i < Iterations; ++i) {
        SDValue NewEst = DAG.getNode(ISD::FMUL, DL, VT, Op, Est, Flags);
        AddToWorklist(NewEst.getNode());

        NewEst = DAG.getNode(ISD::FSUB, DL, VT, FPOne, NewEst, Flags);
        AddToWorklist(NewEst.getNode());

        NewEst = DAG.getNode(ISD::FMUL, DL, VT, Est, NewEst, Flags);
        AddToWorklist(NewEst.getNode());

        Est = DAG.getNode(ISD::FADD, DL, VT, Est, NewEst, Flags);
        AddToWorklist(Est.getNode());
      }
    }
    return Est;
  }

  return SDValue();
}

/// Newton iteration for a function: F(X) is X_{i+1} = X_i - F(X_i)/F'(X_i)
/// For the reciprocal sqrt, we need to find the zero of the function:
///   F(X) = 1/X^2 - A [which has a zero at X = 1/sqrt(A)]
///     =>
///   X_{i+1} = X_i (1.5 - A X_i^2 / 2)
/// As a result, we precompute A/2 prior to the iteration loop.
SDValue DAGCombiner::buildSqrtNROneConst(SDValue Arg, SDValue Est,
                                         unsigned Iterations,
                                         SDNodeFlags Flags, bool Reciprocal) {
  EVT VT = Arg.getValueType();
  SDLoc DL(Arg);
  SDValue ThreeHalves = DAG.getConstantFP(1.5, DL, VT);

  // We now need 0.5 * Arg which we can write as (1.5 * Arg - Arg) so that
  // this entire sequence requires only one FP constant.
  SDValue HalfArg = DAG.getNode(ISD::FMUL, DL, VT, ThreeHalves, Arg, Flags);
  AddToWorklist(HalfArg.getNode());

  HalfArg = DAG.getNode(ISD::FSUB, DL, VT, HalfArg, Arg, Flags);
  AddToWorklist(HalfArg.getNode());

  // Newton iterations: Est = Est * (1.5 - HalfArg * Est * Est)
  for (unsigned i = 0; i < Iterations; ++i) {
    SDValue NewEst = DAG.getNode(ISD::FMUL, DL, VT, Est, Est, Flags);
    AddToWorklist(NewEst.getNode());

    NewEst = DAG.getNode(ISD::FMUL, DL, VT, HalfArg, NewEst, Flags);
    AddToWorklist(NewEst.getNode());

    NewEst = DAG.getNode(ISD::FSUB, DL, VT, ThreeHalves, NewEst, Flags);
    AddToWorklist(NewEst.getNode());

    Est = DAG.getNode(ISD::FMUL, DL, VT, Est, NewEst, Flags);
    AddToWorklist(Est.getNode());
  }

  // If non-reciprocal square root is requested, multiply the result by Arg.
  if (!Reciprocal) {
    Est = DAG.getNode(ISD::FMUL, DL, VT, Est, Arg, Flags);
    AddToWorklist(Est.getNode());
  }

  return Est;
}

/// Newton iteration for a function: F(X) is X_{i+1} = X_i - F(X_i)/F'(X_i)
/// For the reciprocal sqrt, we need to find the zero of the function:
///   F(X) = 1/X^2 - A [which has a zero at X = 1/sqrt(A)]
///     =>
///   X_{i+1} = (-0.5 * X_i) * (A * X_i * X_i + (-3.0))
SDValue DAGCombiner::buildSqrtNRTwoConst(SDValue Arg, SDValue Est,
                                         unsigned Iterations,
                                         SDNodeFlags Flags, bool Reciprocal) {
  EVT VT = Arg.getValueType();
  SDLoc DL(Arg);
  SDValue MinusThree = DAG.getConstantFP(-3.0, DL, VT);
  SDValue MinusHalf = DAG.getConstantFP(-0.5, DL, VT);

  // This routine must enter the loop below to work correctly
  // when (Reciprocal == false).
  assert(Iterations > 0);

  // Newton iterations for reciprocal square root:
  // E = (E * -0.5) * ((A * E) * E + -3.0)
  for (unsigned i = 0; i < Iterations; ++i) {
    SDValue AE = DAG.getNode(ISD::FMUL, DL, VT, Arg, Est, Flags);
    AddToWorklist(AE.getNode());

    SDValue AEE = DAG.getNode(ISD::FMUL, DL, VT, AE, Est, Flags);
    AddToWorklist(AEE.getNode());

    SDValue RHS = DAG.getNode(ISD::FADD, DL, VT, AEE, MinusThree, Flags);
    AddToWorklist(RHS.getNode());

    // When calculating a square root at the last iteration build:
    // S = ((A * E) * -0.5) * ((A * E) * E + -3.0)
    // (notice a common subexpression)
    SDValue LHS;
    if (Reciprocal || (i + 1) < Iterations) {
      // RSQRT: LHS = (E * -0.5)
      LHS = DAG.getNode(ISD::FMUL, DL, VT, Est, MinusHalf, Flags);
    } else {
      // SQRT: LHS = (A * E) * -0.5
      LHS = DAG.getNode(ISD::FMUL, DL, VT, AE, MinusHalf, Flags);
    }
    AddToWorklist(LHS.getNode());

    Est = DAG.getNode(ISD::FMUL, DL, VT, LHS, RHS, Flags);
    AddToWorklist(Est.getNode());
  }

  return Est;
}

/// Build code to calculate either rsqrt(Op) or sqrt(Op). In the latter case
/// Op*rsqrt(Op) is actually computed, so additional postprocessing is needed if
/// Op can be zero.
SDValue DAGCombiner::buildSqrtEstimateImpl(SDValue Op, SDNodeFlags Flags,
                                           bool Reciprocal) {
  if (Level >= AfterLegalizeDAG)
    return SDValue();

  // TODO: Handle half and/or extended types?
  EVT VT = Op.getValueType();
  if (VT.getScalarType() != MVT::f32 && VT.getScalarType() != MVT::f64)
    return SDValue();

  // If estimates are explicitly disabled for this function, we're done.
  MachineFunction &MF = DAG.getMachineFunction();
  int Enabled = TLI.getRecipEstimateSqrtEnabled(VT, MF);
  if (Enabled == TLI.ReciprocalEstimate::Disabled)
    return SDValue();

  // Estimates may be explicitly enabled for this type with a custom number of
  // refinement steps.
  int Iterations = TLI.getSqrtRefinementSteps(VT, MF);

  bool UseOneConstNR = false;
  if (SDValue Est =
      TLI.getSqrtEstimate(Op, DAG, Enabled, Iterations, UseOneConstNR,
                          Reciprocal)) {
    AddToWorklist(Est.getNode());

    if (Iterations) {
      Est = UseOneConstNR
            ? buildSqrtNROneConst(Op, Est, Iterations, Flags, Reciprocal)
            : buildSqrtNRTwoConst(Op, Est, Iterations, Flags, Reciprocal);

      if (!Reciprocal) {
        // The estimate is now completely wrong if the input was exactly 0.0 or
        // possibly a denormal. Force the answer to 0.0 for those cases.
        EVT VT = Op.getValueType();
        SDLoc DL(Op);
        EVT CCVT = getSetCCResultType(VT);
        ISD::NodeType SelOpcode = VT.isVector() ? ISD::VSELECT : ISD::SELECT;
        const Function &F = DAG.getMachineFunction().getFunction();
        Attribute Denorms = F.getFnAttribute("denormal-fp-math");
        if (Denorms.getValueAsString().equals("ieee")) {
          // fabs(X) < SmallestNormal ? 0.0 : Est
          const fltSemantics &FltSem = DAG.EVTToAPFloatSemantics(VT);
          APFloat SmallestNorm = APFloat::getSmallestNormalized(FltSem);
          SDValue NormC = DAG.getConstantFP(SmallestNorm, DL, VT);
          SDValue FPZero = DAG.getConstantFP(0.0, DL, VT);
          SDValue Fabs = DAG.getNode(ISD::FABS, DL, VT, Op);
          SDValue IsDenorm = DAG.getSetCC(DL, CCVT, Fabs, NormC, ISD::SETLT);
          Est = DAG.getNode(SelOpcode, DL, VT, IsDenorm, FPZero, Est);
          AddToWorklist(Fabs.getNode());
          AddToWorklist(IsDenorm.getNode());
          AddToWorklist(Est.getNode());
        } else {
          // X == 0.0 ? 0.0 : Est
          SDValue FPZero = DAG.getConstantFP(0.0, DL, VT);
          SDValue IsZero = DAG.getSetCC(DL, CCVT, Op, FPZero, ISD::SETEQ);
          Est = DAG.getNode(SelOpcode, DL, VT, IsZero, FPZero, Est);
          AddToWorklist(IsZero.getNode());
          AddToWorklist(Est.getNode());
        }
      }
    }
    return Est;
  }

  return SDValue();
}

SDValue DAGCombiner::buildRsqrtEstimate(SDValue Op, SDNodeFlags Flags) {
  return buildSqrtEstimateImpl(Op, Flags, true);
}

SDValue DAGCombiner::buildSqrtEstimate(SDValue Op, SDNodeFlags Flags) {
  return buildSqrtEstimateImpl(Op, Flags, false);
}

/// Return true if there is any possibility that the two addresses overlap.
bool DAGCombiner::isAlias(LSBaseSDNode *Op0, LSBaseSDNode *Op1) const {
  // If they are the same then they must be aliases.
  if (Op0->getBasePtr() == Op1->getBasePtr()) return true;

  // If they are both volatile then they cannot be reordered.
  if (Op0->isVolatile() && Op1->isVolatile()) return true;

  // If one operation reads from invariant memory, and the other may store, they
  // cannot alias. These should really be checking the equivalent of mayWrite,
  // but it only matters for memory nodes other than load /store.
  if (Op0->isInvariant() && Op1->writeMem())
    return false;

  if (Op1->isInvariant() && Op0->writeMem())
    return false;

  unsigned NumBytes0 = Op0->getMemoryVT().getStoreSize();
  unsigned NumBytes1 = Op1->getMemoryVT().getStoreSize();

  // Check for BaseIndexOffset matching.
  BaseIndexOffset BasePtr0 = BaseIndexOffset::match(Op0, DAG);
  BaseIndexOffset BasePtr1 = BaseIndexOffset::match(Op1, DAG);
  int64_t PtrDiff;
  if (BasePtr0.getBase().getNode() && BasePtr1.getBase().getNode()) {
    if (BasePtr0.equalBaseIndex(BasePtr1, DAG, PtrDiff))
      return !((NumBytes0 <= PtrDiff) || (PtrDiff + NumBytes1 <= 0));

    // If both BasePtr0 and BasePtr1 are FrameIndexes, we will not be
    // able to calculate their relative offset if at least one arises
    // from an alloca. However, these allocas cannot overlap and we
    // can infer there is no alias.
    if (auto *A = dyn_cast<FrameIndexSDNode>(BasePtr0.getBase()))
      if (auto *B = dyn_cast<FrameIndexSDNode>(BasePtr1.getBase())) {
        MachineFrameInfo &MFI = DAG.getMachineFunction().getFrameInfo();
        // If the base are the same frame index but the we couldn't find a
        // constant offset, (indices are different) be conservative.
        if (A != B && (!MFI.isFixedObjectIndex(A->getIndex()) ||
                       !MFI.isFixedObjectIndex(B->getIndex())))
          return false;
      }

    bool IsFI0 = isa<FrameIndexSDNode>(BasePtr0.getBase());
    bool IsFI1 = isa<FrameIndexSDNode>(BasePtr1.getBase());
    bool IsGV0 = isa<GlobalAddressSDNode>(BasePtr0.getBase());
    bool IsGV1 = isa<GlobalAddressSDNode>(BasePtr1.getBase());
    bool IsCV0 = isa<ConstantPoolSDNode>(BasePtr0.getBase());
    bool IsCV1 = isa<ConstantPoolSDNode>(BasePtr1.getBase());

    // If of mismatched base types or checkable indices we can check
    // they do not alias.
    if ((BasePtr0.getIndex() == BasePtr1.getIndex() || (IsFI0 != IsFI1) ||
         (IsGV0 != IsGV1) || (IsCV0 != IsCV1)) &&
        (IsFI0 || IsGV0 || IsCV0) && (IsFI1 || IsGV1 || IsCV1))
      return false;
  }

  // If we know required SrcValue1 and SrcValue2 have relatively large
  // alignment compared to the size and offset of the access, we may be able
  // to prove they do not alias. This check is conservative for now to catch
  // cases created by splitting vector types.
  int64_t SrcValOffset0 = Op0->getSrcValueOffset();
  int64_t SrcValOffset1 = Op1->getSrcValueOffset();
  unsigned OrigAlignment0 = Op0->getOriginalAlignment();
  unsigned OrigAlignment1 = Op1->getOriginalAlignment();
  if (OrigAlignment0 == OrigAlignment1 && SrcValOffset0 != SrcValOffset1 &&
      NumBytes0 == NumBytes1 && OrigAlignment0 > NumBytes0) {
    int64_t OffAlign0 = SrcValOffset0 % OrigAlignment0;
    int64_t OffAlign1 = SrcValOffset1 % OrigAlignment1;

    // There is no overlap between these relatively aligned accesses of
    // similar size. Return no alias.
    if ((OffAlign0 + NumBytes0) <= OffAlign1 ||
        (OffAlign1 + NumBytes1) <= OffAlign0)
      return false;
  }

  bool UseAA = CombinerGlobalAA.getNumOccurrences() > 0
                   ? CombinerGlobalAA
                   : DAG.getSubtarget().useAA();
#ifndef NDEBUG
  if (CombinerAAOnlyFunc.getNumOccurrences() &&
      CombinerAAOnlyFunc != DAG.getMachineFunction().getName())
    UseAA = false;
#endif

  if (UseAA && AA &&
      Op0->getMemOperand()->getValue() && Op1->getMemOperand()->getValue()) {
    // Use alias analysis information.
    int64_t MinOffset = std::min(SrcValOffset0, SrcValOffset1);
    int64_t Overlap0 = NumBytes0 + SrcValOffset0 - MinOffset;
    int64_t Overlap1 = NumBytes1 + SrcValOffset1 - MinOffset;
    AliasResult AAResult =
        AA->alias(MemoryLocation(Op0->getMemOperand()->getValue(), Overlap0,
                                 UseTBAA ? Op0->getAAInfo() : AAMDNodes()),
                  MemoryLocation(Op1->getMemOperand()->getValue(), Overlap1,
                                 UseTBAA ? Op1->getAAInfo() : AAMDNodes()) );
    if (AAResult == NoAlias)
      return false;
  }

  // Otherwise we have to assume they alias.
  return true;
}

/// Walk up chain skipping non-aliasing memory nodes,
/// looking for aliasing nodes and adding them to the Aliases vector.
void DAGCombiner::GatherAllAliases(SDNode *N, SDValue OriginalChain,
                                   SmallVectorImpl<SDValue> &Aliases) {
  SmallVector<SDValue, 8> Chains;     // List of chains to visit.
  SmallPtrSet<SDNode *, 16> Visited;  // Visited node set.

  // Get alias information for node.
  bool IsLoad = isa<LoadSDNode>(N) && !cast<LSBaseSDNode>(N)->isVolatile();

  // Starting off.
  Chains.push_back(OriginalChain);
  unsigned Depth = 0;

  // Look at each chain and determine if it is an alias.  If so, add it to the
  // aliases list.  If not, then continue up the chain looking for the next
  // candidate.
  while (!Chains.empty()) {
    SDValue Chain = Chains.pop_back_val();

    // For TokenFactor nodes, look at each operand and only continue up the
    // chain until we reach the depth limit.
    //
    // FIXME: The depth check could be made to return the last non-aliasing
    // chain we found before we hit a tokenfactor rather than the original
    // chain.
    if (Depth > TLI.getGatherAllAliasesMaxDepth()) {
      Aliases.clear();
      Aliases.push_back(OriginalChain);
      return;
    }

    // Don't bother if we've been before.
    if (!Visited.insert(Chain.getNode()).second)
      continue;

    switch (Chain.getOpcode()) {
    case ISD::EntryToken:
      // Entry token is ideal chain operand, but handled in FindBetterChain.
      break;

    case ISD::LOAD:
    case ISD::STORE: {
      // Get alias information for Chain.
      bool IsOpLoad = isa<LoadSDNode>(Chain.getNode()) &&
          !cast<LSBaseSDNode>(Chain.getNode())->isVolatile();

      // If chain is alias then stop here.
      if (!(IsLoad && IsOpLoad) &&
          isAlias(cast<LSBaseSDNode>(N), cast<LSBaseSDNode>(Chain.getNode()))) {
        Aliases.push_back(Chain);
      } else {
        // Look further up the chain.
        Chains.push_back(Chain.getOperand(0));
        ++Depth;
      }
      break;
    }

    case ISD::TokenFactor:
      // We have to check each of the operands of the token factor for "small"
      // token factors, so we queue them up.  Adding the operands to the queue
      // (stack) in reverse order maintains the original order and increases the
      // likelihood that getNode will find a matching token factor (CSE.)
      if (Chain.getNumOperands() > 16) {
        Aliases.push_back(Chain);
        break;
      }
      for (unsigned n = Chain.getNumOperands(); n;)
        Chains.push_back(Chain.getOperand(--n));
      ++Depth;
      break;

    case ISD::CopyFromReg:
      // Forward past CopyFromReg.
      Chains.push_back(Chain.getOperand(0));
      ++Depth;
      break;

    default:
      // For all other instructions we will just have to take what we can get.
      Aliases.push_back(Chain);
      break;
    }
  }
}

/// Walk up chain skipping non-aliasing memory nodes, looking for a better chain
/// (aliasing node.)
SDValue DAGCombiner::FindBetterChain(SDNode *N, SDValue OldChain) {
  if (OptLevel == CodeGenOpt::None)
    return OldChain;

  // Ops for replacing token factor.
  SmallVector<SDValue, 8> Aliases;

  // Accumulate all the aliases to this node.
  GatherAllAliases(N, OldChain, Aliases);

  // If no operands then chain to entry token.
  if (Aliases.size() == 0)
    return DAG.getEntryNode();

  // If a single operand then chain to it.  We don't need to revisit it.
  if (Aliases.size() == 1)
    return Aliases[0];

  // Construct a custom tailored token factor.
  return DAG.getNode(ISD::TokenFactor, SDLoc(N), MVT::Other, Aliases);
}

// TODO: Replace with with std::monostate when we move to C++17.
struct UnitT { } Unit;
bool operator==(const UnitT &, const UnitT &) { return true; }
bool operator!=(const UnitT &, const UnitT &) { return false; }

// This function tries to collect a bunch of potentially interesting
// nodes to improve the chains of, all at once. This might seem
// redundant, as this function gets called when visiting every store
// node, so why not let the work be done on each store as it's visited?
//
// I believe this is mainly important because MergeConsecutiveStores
// is unable to deal with merging stores of different sizes, so unless
// we improve the chains of all the potential candidates up-front
// before running MergeConsecutiveStores, it might only see some of
// the nodes that will eventually be candidates, and then not be able
// to go from a partially-merged state to the desired final
// fully-merged state.

bool DAGCombiner::parallelizeChainedStores(StoreSDNode *St) {
  SmallVector<StoreSDNode *, 8> ChainedStores;
  StoreSDNode *STChain = St;
  // Intervals records which offsets from BaseIndex have been covered. In
  // the common case, every store writes to the immediately previous address
  // space and thus merged with the previous interval at insertion time.

  using IMap =
      llvm::IntervalMap<int64_t, UnitT, 8, IntervalMapHalfOpenInfo<int64_t>>;
  IMap::Allocator A;
  IMap Intervals(A);

  // This holds the base pointer, index, and the offset in bytes from the base
  // pointer.
  const BaseIndexOffset BasePtr = BaseIndexOffset::match(St, DAG);

  // We must have a base and an offset.
  if (!BasePtr.getBase().getNode())
    return false;

  // Do not handle stores to undef base pointers.
  if (BasePtr.getBase().isUndef())
    return false;

  // Add ST's interval.
  Intervals.insert(0, (St->getMemoryVT().getSizeInBits() + 7) / 8, Unit);

  while (StoreSDNode *Chain = dyn_cast<StoreSDNode>(STChain->getChain())) {
    // If the chain has more than one use, then we can't reorder the mem ops.
    if (!SDValue(Chain, 0)->hasOneUse())
      break;
    if (Chain->isVolatile() || Chain->isIndexed())
      break;

    // Find the base pointer and offset for this memory node.
    const BaseIndexOffset Ptr = BaseIndexOffset::match(Chain, DAG);
    // Check that the base pointer is the same as the original one.
    int64_t Offset;
    if (!BasePtr.equalBaseIndex(Ptr, DAG, Offset))
      break;
    int64_t Length = (Chain->getMemoryVT().getSizeInBits() + 7) / 8;
    // Make sure we don't overlap with other intervals by checking the ones to
    // the left or right before inserting.
    auto I = Intervals.find(Offset);
    // If there's a next interval, we should end before it.
    if (I != Intervals.end() && I.start() < (Offset + Length))
      break;
    // If there's a previous interval, we should start after it.
    if (I != Intervals.begin() && (--I).stop() <= Offset)
      break;
    Intervals.insert(Offset, Offset + Length, Unit);

    ChainedStores.push_back(Chain);
    STChain = Chain;
  }

  // If we didn't find a chained store, exit.
  if (ChainedStores.size() == 0)
    return false;

  // Improve all chained stores (St and ChainedStores members) starting from
  // where the store chain ended and return single TokenFactor.
  SDValue NewChain = STChain->getChain();
  SmallVector<SDValue, 8> TFOps;
  for (unsigned I = ChainedStores.size(); I;) {
    StoreSDNode *S = ChainedStores[--I];
    SDValue BetterChain = FindBetterChain(S, NewChain);
    S = cast<StoreSDNode>(DAG.UpdateNodeOperands(
        S, BetterChain, S->getOperand(1), S->getOperand(2), S->getOperand(3)));
    TFOps.push_back(SDValue(S, 0));
    ChainedStores[I] = S;
  }

  // Improve St's chain. Use a new node to avoid creating a loop from CombineTo.
  SDValue BetterChain = FindBetterChain(St, NewChain);
  SDValue NewST;
  if (St->isTruncatingStore())
    NewST = DAG.getTruncStore(BetterChain, SDLoc(St), St->getValue(),
                              St->getBasePtr(), St->getMemoryVT(),
                              St->getMemOperand());
  else
    NewST = DAG.getStore(BetterChain, SDLoc(St), St->getValue(),
                         St->getBasePtr(), St->getMemOperand());

  TFOps.push_back(NewST);

  // If we improved every element of TFOps, then we've lost the dependence on
  // NewChain to successors of St and we need to add it back to TFOps. Do so at
  // the beginning to keep relative order consistent with FindBetterChains.
  auto hasImprovedChain = [&](SDValue ST) -> bool {
    return ST->getOperand(0) != NewChain;
  };
  bool AddNewChain = llvm::all_of(TFOps, hasImprovedChain);
  if (AddNewChain)
    TFOps.insert(TFOps.begin(), NewChain);

  SDValue TF = DAG.getNode(ISD::TokenFactor, SDLoc(STChain), MVT::Other, TFOps);
  CombineTo(St, TF);

  AddToWorklist(STChain);
  // Add TF operands worklist in reverse order.
  for (auto I = TF->getNumOperands(); I;)
    AddToWorklist(TF->getOperand(--I).getNode());
  AddToWorklist(TF.getNode());
  return true;
}

bool DAGCombiner::findBetterNeighborChains(StoreSDNode *St) {
  if (OptLevel == CodeGenOpt::None)
    return false;

  const BaseIndexOffset BasePtr = BaseIndexOffset::match(St, DAG);

  // We must have a base and an offset.
  if (!BasePtr.getBase().getNode())
    return false;

  // Do not handle stores to undef base pointers.
  if (BasePtr.getBase().isUndef())
    return false;

  // Directly improve a chain of disjoint stores starting at St.
  if (parallelizeChainedStores(St))
    return true;

  // Improve St's Chain..
  SDValue BetterChain = FindBetterChain(St, St->getChain());
  if (St->getChain() != BetterChain) {
    replaceStoreChain(St, BetterChain);
    return true;
  }
  return false;
}

/// This is the entry point for the file.
void SelectionDAG::Combine(CombineLevel Level, AliasAnalysis *AA,
                           CodeGenOpt::Level OptLevel) {
  /// This is the main entry point to this class.
  DAGCombiner(*this, AA, OptLevel).Run(Level);
}
