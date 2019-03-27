//===- llvm/CodeGen/SelectionDAG.h - InstSelection DAG ----------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file declares the SelectionDAG class, and transitively defines the
// SDNode class and subclasses.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CODEGEN_SELECTIONDAG_H
#define LLVM_CODEGEN_SELECTIONDAG_H

#include "llvm/ADT/APFloat.h"
#include "llvm/ADT/APInt.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/DenseSet.h"
#include "llvm/ADT/FoldingSet.h"
#include "llvm/ADT/SetVector.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringMap.h"
#include "llvm/ADT/ilist.h"
#include "llvm/ADT/iterator.h"
#include "llvm/ADT/iterator_range.h"
#include "llvm/Analysis/AliasAnalysis.h"
#include "llvm/Analysis/LegacyDivergenceAnalysis.h"
#include "llvm/CodeGen/DAGCombine.h"
#include "llvm/CodeGen/FunctionLoweringInfo.h"
#include "llvm/CodeGen/ISDOpcodes.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineMemOperand.h"
#include "llvm/CodeGen/SelectionDAGNodes.h"
#include "llvm/CodeGen/ValueTypes.h"
#include "llvm/IR/DebugLoc.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Metadata.h"
#include "llvm/Support/Allocator.h"
#include "llvm/Support/ArrayRecycler.h"
#include "llvm/Support/AtomicOrdering.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/CodeGen.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/MachineValueType.h"
#include "llvm/Support/RecyclingAllocator.h"
#include <algorithm>
#include <cassert>
#include <cstdint>
#include <functional>
#include <map>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

namespace llvm {

class BlockAddress;
class Constant;
class ConstantFP;
class ConstantInt;
class DataLayout;
struct fltSemantics;
class GlobalValue;
struct KnownBits;
class LLVMContext;
class MachineBasicBlock;
class MachineConstantPoolValue;
class MCSymbol;
class OptimizationRemarkEmitter;
class SDDbgValue;
class SDDbgLabel;
class SelectionDAG;
class SelectionDAGTargetInfo;
class TargetLibraryInfo;
class TargetLowering;
class TargetMachine;
class TargetSubtargetInfo;
class Value;

class SDVTListNode : public FoldingSetNode {
  friend struct FoldingSetTrait<SDVTListNode>;

  /// A reference to an Interned FoldingSetNodeID for this node.
  /// The Allocator in SelectionDAG holds the data.
  /// SDVTList contains all types which are frequently accessed in SelectionDAG.
  /// The size of this list is not expected to be big so it won't introduce
  /// a memory penalty.
  FoldingSetNodeIDRef FastID;
  const EVT *VTs;
  unsigned int NumVTs;
  /// The hash value for SDVTList is fixed, so cache it to avoid
  /// hash calculation.
  unsigned HashValue;

public:
  SDVTListNode(const FoldingSetNodeIDRef ID, const EVT *VT, unsigned int Num) :
      FastID(ID), VTs(VT), NumVTs(Num) {
    HashValue = ID.ComputeHash();
  }

  SDVTList getSDVTList() {
    SDVTList result = {VTs, NumVTs};
    return result;
  }
};

/// Specialize FoldingSetTrait for SDVTListNode
/// to avoid computing temp FoldingSetNodeID and hash value.
template<> struct FoldingSetTrait<SDVTListNode> : DefaultFoldingSetTrait<SDVTListNode> {
  static void Profile(const SDVTListNode &X, FoldingSetNodeID& ID) {
    ID = X.FastID;
  }

  static bool Equals(const SDVTListNode &X, const FoldingSetNodeID &ID,
                     unsigned IDHash, FoldingSetNodeID &TempID) {
    if (X.HashValue != IDHash)
      return false;
    return ID == X.FastID;
  }

  static unsigned ComputeHash(const SDVTListNode &X, FoldingSetNodeID &TempID) {
    return X.HashValue;
  }
};

template <> struct ilist_alloc_traits<SDNode> {
  static void deleteNode(SDNode *) {
    llvm_unreachable("ilist_traits<SDNode> shouldn't see a deleteNode call!");
  }
};

/// Keeps track of dbg_value information through SDISel.  We do
/// not build SDNodes for these so as not to perturb the generated code;
/// instead the info is kept off to the side in this structure. Each SDNode may
/// have one or more associated dbg_value entries. This information is kept in
/// DbgValMap.
/// Byval parameters are handled separately because they don't use alloca's,
/// which busts the normal mechanism.  There is good reason for handling all
/// parameters separately:  they may not have code generated for them, they
/// should always go at the beginning of the function regardless of other code
/// motion, and debug info for them is potentially useful even if the parameter
/// is unused.  Right now only byval parameters are handled separately.
class SDDbgInfo {
  BumpPtrAllocator Alloc;
  SmallVector<SDDbgValue*, 32> DbgValues;
  SmallVector<SDDbgValue*, 32> ByvalParmDbgValues;
  SmallVector<SDDbgLabel*, 4> DbgLabels;
  using DbgValMapType = DenseMap<const SDNode *, SmallVector<SDDbgValue *, 2>>;
  DbgValMapType DbgValMap;

public:
  SDDbgInfo() = default;
  SDDbgInfo(const SDDbgInfo &) = delete;
  SDDbgInfo &operator=(const SDDbgInfo &) = delete;

  void add(SDDbgValue *V, const SDNode *Node, bool isParameter) {
    if (isParameter) {
      ByvalParmDbgValues.push_back(V);
    } else     DbgValues.push_back(V);
    if (Node)
      DbgValMap[Node].push_back(V);
  }

  void add(SDDbgLabel *L) {
    DbgLabels.push_back(L);
  }

  /// Invalidate all DbgValues attached to the node and remove
  /// it from the Node-to-DbgValues map.
  void erase(const SDNode *Node);

  void clear() {
    DbgValMap.clear();
    DbgValues.clear();
    ByvalParmDbgValues.clear();
    DbgLabels.clear();
    Alloc.Reset();
  }

  BumpPtrAllocator &getAlloc() { return Alloc; }

  bool empty() const {
    return DbgValues.empty() && ByvalParmDbgValues.empty() && DbgLabels.empty();
  }

  ArrayRef<SDDbgValue*> getSDDbgValues(const SDNode *Node) const {
    auto I = DbgValMap.find(Node);
    if (I != DbgValMap.end())
      return I->second;
    return ArrayRef<SDDbgValue*>();
  }

  using DbgIterator = SmallVectorImpl<SDDbgValue*>::iterator;
  using DbgLabelIterator = SmallVectorImpl<SDDbgLabel*>::iterator;

  DbgIterator DbgBegin() { return DbgValues.begin(); }
  DbgIterator DbgEnd()   { return DbgValues.end(); }
  DbgIterator ByvalParmDbgBegin() { return ByvalParmDbgValues.begin(); }
  DbgIterator ByvalParmDbgEnd()   { return ByvalParmDbgValues.end(); }
  DbgLabelIterator DbgLabelBegin() { return DbgLabels.begin(); }
  DbgLabelIterator DbgLabelEnd()   { return DbgLabels.end(); }
};

void checkForCycles(const SelectionDAG *DAG, bool force = false);

/// This is used to represent a portion of an LLVM function in a low-level
/// Data Dependence DAG representation suitable for instruction selection.
/// This DAG is constructed as the first step of instruction selection in order
/// to allow implementation of machine specific optimizations
/// and code simplifications.
///
/// The representation used by the SelectionDAG is a target-independent
/// representation, which has some similarities to the GCC RTL representation,
/// but is significantly more simple, powerful, and is a graph form instead of a
/// linear form.
///
class SelectionDAG {
  const TargetMachine &TM;
  const SelectionDAGTargetInfo *TSI = nullptr;
  const TargetLowering *TLI = nullptr;
  const TargetLibraryInfo *LibInfo = nullptr;
  MachineFunction *MF;
  Pass *SDAGISelPass = nullptr;
  LLVMContext *Context;
  CodeGenOpt::Level OptLevel;

  LegacyDivergenceAnalysis * DA = nullptr;
  FunctionLoweringInfo * FLI = nullptr;

  /// The function-level optimization remark emitter.  Used to emit remarks
  /// whenever manipulating the DAG.
  OptimizationRemarkEmitter *ORE;

  /// The starting token.
  SDNode EntryNode;

  /// The root of the entire DAG.
  SDValue Root;

  /// A linked list of nodes in the current DAG.
  ilist<SDNode> AllNodes;

  /// The AllocatorType for allocating SDNodes. We use
  /// pool allocation with recycling.
  using NodeAllocatorType = RecyclingAllocator<BumpPtrAllocator, SDNode,
                                               sizeof(LargestSDNode),
                                               alignof(MostAlignedSDNode)>;

  /// Pool allocation for nodes.
  NodeAllocatorType NodeAllocator;

  /// This structure is used to memoize nodes, automatically performing
  /// CSE with existing nodes when a duplicate is requested.
  FoldingSet<SDNode> CSEMap;

  /// Pool allocation for machine-opcode SDNode operands.
  BumpPtrAllocator OperandAllocator;
  ArrayRecycler<SDUse> OperandRecycler;

  /// Pool allocation for misc. objects that are created once per SelectionDAG.
  BumpPtrAllocator Allocator;

  /// Tracks dbg_value and dbg_label information through SDISel.
  SDDbgInfo *DbgInfo;

  uint16_t NextPersistentId = 0;

public:
  /// Clients of various APIs that cause global effects on
  /// the DAG can optionally implement this interface.  This allows the clients
  /// to handle the various sorts of updates that happen.
  ///
  /// A DAGUpdateListener automatically registers itself with DAG when it is
  /// constructed, and removes itself when destroyed in RAII fashion.
  struct DAGUpdateListener {
    DAGUpdateListener *const Next;
    SelectionDAG &DAG;

    explicit DAGUpdateListener(SelectionDAG &D)
      : Next(D.UpdateListeners), DAG(D) {
      DAG.UpdateListeners = this;
    }

    virtual ~DAGUpdateListener() {
      assert(DAG.UpdateListeners == this &&
             "DAGUpdateListeners must be destroyed in LIFO order");
      DAG.UpdateListeners = Next;
    }

    /// The node N that was deleted and, if E is not null, an
    /// equivalent node E that replaced it.
    virtual void NodeDeleted(SDNode *N, SDNode *E);

    /// The node N that was updated.
    virtual void NodeUpdated(SDNode *N);
  };

  struct DAGNodeDeletedListener : public DAGUpdateListener {
    std::function<void(SDNode *, SDNode *)> Callback;

    DAGNodeDeletedListener(SelectionDAG &DAG,
                           std::function<void(SDNode *, SDNode *)> Callback)
        : DAGUpdateListener(DAG), Callback(std::move(Callback)) {}

    void NodeDeleted(SDNode *N, SDNode *E) override { Callback(N, E); }

   private:
    virtual void anchor();
  };

  /// When true, additional steps are taken to
  /// ensure that getConstant() and similar functions return DAG nodes that
  /// have legal types. This is important after type legalization since
  /// any illegally typed nodes generated after this point will not experience
  /// type legalization.
  bool NewNodesMustHaveLegalTypes = false;

private:
  /// DAGUpdateListener is a friend so it can manipulate the listener stack.
  friend struct DAGUpdateListener;

  /// Linked list of registered DAGUpdateListener instances.
  /// This stack is maintained by DAGUpdateListener RAII.
  DAGUpdateListener *UpdateListeners = nullptr;

  /// Implementation of setSubgraphColor.
  /// Return whether we had to truncate the search.
  bool setSubgraphColorHelper(SDNode *N, const char *Color,
                              DenseSet<SDNode *> &visited,
                              int level, bool &printed);

  template <typename SDNodeT, typename... ArgTypes>
  SDNodeT *newSDNode(ArgTypes &&... Args) {
    return new (NodeAllocator.template Allocate<SDNodeT>())
        SDNodeT(std::forward<ArgTypes>(Args)...);
  }

  /// Build a synthetic SDNodeT with the given args and extract its subclass
  /// data as an integer (e.g. for use in a folding set).
  ///
  /// The args to this function are the same as the args to SDNodeT's
  /// constructor, except the second arg (assumed to be a const DebugLoc&) is
  /// omitted.
  template <typename SDNodeT, typename... ArgTypes>
  static uint16_t getSyntheticNodeSubclassData(unsigned IROrder,
                                               ArgTypes &&... Args) {
    // The compiler can reduce this expression to a constant iff we pass an
    // empty DebugLoc.  Thankfully, the debug location doesn't have any bearing
    // on the subclass data.
    return SDNodeT(IROrder, DebugLoc(), std::forward<ArgTypes>(Args)...)
        .getRawSubclassData();
  }

  template <typename SDNodeTy>
  static uint16_t getSyntheticNodeSubclassData(unsigned Opc, unsigned Order,
                                                SDVTList VTs, EVT MemoryVT,
                                                MachineMemOperand *MMO) {
    return SDNodeTy(Opc, Order, DebugLoc(), VTs, MemoryVT, MMO)
         .getRawSubclassData();
  }

  void createOperands(SDNode *Node, ArrayRef<SDValue> Vals);

  void removeOperands(SDNode *Node) {
    if (!Node->OperandList)
      return;
    OperandRecycler.deallocate(
        ArrayRecycler<SDUse>::Capacity::get(Node->NumOperands),
        Node->OperandList);
    Node->NumOperands = 0;
    Node->OperandList = nullptr;
  }
  void CreateTopologicalOrder(std::vector<SDNode*>& Order);
public:
  explicit SelectionDAG(const TargetMachine &TM, CodeGenOpt::Level);
  SelectionDAG(const SelectionDAG &) = delete;
  SelectionDAG &operator=(const SelectionDAG &) = delete;
  ~SelectionDAG();

  /// Prepare this SelectionDAG to process code in the given MachineFunction.
  void init(MachineFunction &NewMF, OptimizationRemarkEmitter &NewORE,
            Pass *PassPtr, const TargetLibraryInfo *LibraryInfo,
            LegacyDivergenceAnalysis * Divergence);

  void setFunctionLoweringInfo(FunctionLoweringInfo * FuncInfo) {
    FLI = FuncInfo;
  }

  /// Clear state and free memory necessary to make this
  /// SelectionDAG ready to process a new block.
  void clear();

  MachineFunction &getMachineFunction() const { return *MF; }
  const Pass *getPass() const { return SDAGISelPass; }

  const DataLayout &getDataLayout() const { return MF->getDataLayout(); }
  const TargetMachine &getTarget() const { return TM; }
  const TargetSubtargetInfo &getSubtarget() const { return MF->getSubtarget(); }
  const TargetLowering &getTargetLoweringInfo() const { return *TLI; }
  const TargetLibraryInfo &getLibInfo() const { return *LibInfo; }
  const SelectionDAGTargetInfo &getSelectionDAGInfo() const { return *TSI; }
  LLVMContext *getContext() const {return Context; }
  OptimizationRemarkEmitter &getORE() const { return *ORE; }

  /// Pop up a GraphViz/gv window with the DAG rendered using 'dot'.
  void viewGraph(const std::string &Title);
  void viewGraph();

#ifndef NDEBUG
  std::map<const SDNode *, std::string> NodeGraphAttrs;
#endif

  /// Clear all previously defined node graph attributes.
  /// Intended to be used from a debugging tool (eg. gdb).
  void clearGraphAttrs();

  /// Set graph attributes for a node. (eg. "color=red".)
  void setGraphAttrs(const SDNode *N, const char *Attrs);

  /// Get graph attributes for a node. (eg. "color=red".)
  /// Used from getNodeAttributes.
  const std::string getGraphAttrs(const SDNode *N) const;

  /// Convenience for setting node color attribute.
  void setGraphColor(const SDNode *N, const char *Color);

  /// Convenience for setting subgraph color attribute.
  void setSubgraphColor(SDNode *N, const char *Color);

  using allnodes_const_iterator = ilist<SDNode>::const_iterator;

  allnodes_const_iterator allnodes_begin() const { return AllNodes.begin(); }
  allnodes_const_iterator allnodes_end() const { return AllNodes.end(); }

  using allnodes_iterator = ilist<SDNode>::iterator;

  allnodes_iterator allnodes_begin() { return AllNodes.begin(); }
  allnodes_iterator allnodes_end() { return AllNodes.end(); }

  ilist<SDNode>::size_type allnodes_size() const {
    return AllNodes.size();
  }

  iterator_range<allnodes_iterator> allnodes() {
    return make_range(allnodes_begin(), allnodes_end());
  }
  iterator_range<allnodes_const_iterator> allnodes() const {
    return make_range(allnodes_begin(), allnodes_end());
  }

  /// Return the root tag of the SelectionDAG.
  const SDValue &getRoot() const { return Root; }

  /// Return the token chain corresponding to the entry of the function.
  SDValue getEntryNode() const {
    return SDValue(const_cast<SDNode *>(&EntryNode), 0);
  }

  /// Set the current root tag of the SelectionDAG.
  ///
  const SDValue &setRoot(SDValue N) {
    assert((!N.getNode() || N.getValueType() == MVT::Other) &&
           "DAG root value is not a chain!");
    if (N.getNode())
      checkForCycles(N.getNode(), this);
    Root = N;
    if (N.getNode())
      checkForCycles(this);
    return Root;
  }

#ifndef NDEBUG
  void VerifyDAGDiverence();
#endif

  /// This iterates over the nodes in the SelectionDAG, folding
  /// certain types of nodes together, or eliminating superfluous nodes.  The
  /// Level argument controls whether Combine is allowed to produce nodes and
  /// types that are illegal on the target.
  void Combine(CombineLevel Level, AliasAnalysis *AA,
               CodeGenOpt::Level OptLevel);

  /// This transforms the SelectionDAG into a SelectionDAG that
  /// only uses types natively supported by the target.
  /// Returns "true" if it made any changes.
  ///
  /// Note that this is an involved process that may invalidate pointers into
  /// the graph.
  bool LegalizeTypes();

  /// This transforms the SelectionDAG into a SelectionDAG that is
  /// compatible with the target instruction selector, as indicated by the
  /// TargetLowering object.
  ///
  /// Note that this is an involved process that may invalidate pointers into
  /// the graph.
  void Legalize();

  /// Transforms a SelectionDAG node and any operands to it into a node
  /// that is compatible with the target instruction selector, as indicated by
  /// the TargetLowering object.
  ///
  /// \returns true if \c N is a valid, legal node after calling this.
  ///
  /// This essentially runs a single recursive walk of the \c Legalize process
  /// over the given node (and its operands). This can be used to incrementally
  /// legalize the DAG. All of the nodes which are directly replaced,
  /// potentially including N, are added to the output parameter \c
  /// UpdatedNodes so that the delta to the DAG can be understood by the
  /// caller.
  ///
  /// When this returns false, N has been legalized in a way that make the
  /// pointer passed in no longer valid. It may have even been deleted from the
  /// DAG, and so it shouldn't be used further. When this returns true, the
  /// N passed in is a legal node, and can be immediately processed as such.
  /// This may still have done some work on the DAG, and will still populate
  /// UpdatedNodes with any new nodes replacing those originally in the DAG.
  bool LegalizeOp(SDNode *N, SmallSetVector<SDNode *, 16> &UpdatedNodes);

  /// This transforms the SelectionDAG into a SelectionDAG
  /// that only uses vector math operations supported by the target.  This is
  /// necessary as a separate step from Legalize because unrolling a vector
  /// operation can introduce illegal types, which requires running
  /// LegalizeTypes again.
  ///
  /// This returns true if it made any changes; in that case, LegalizeTypes
  /// is called again before Legalize.
  ///
  /// Note that this is an involved process that may invalidate pointers into
  /// the graph.
  bool LegalizeVectors();

  /// This method deletes all unreachable nodes in the SelectionDAG.
  void RemoveDeadNodes();

  /// Remove the specified node from the system.  This node must
  /// have no referrers.
  void DeleteNode(SDNode *N);

  /// Return an SDVTList that represents the list of values specified.
  SDVTList getVTList(EVT VT);
  SDVTList getVTList(EVT VT1, EVT VT2);
  SDVTList getVTList(EVT VT1, EVT VT2, EVT VT3);
  SDVTList getVTList(EVT VT1, EVT VT2, EVT VT3, EVT VT4);
  SDVTList getVTList(ArrayRef<EVT> VTs);

  //===--------------------------------------------------------------------===//
  // Node creation methods.

  /// Create a ConstantSDNode wrapping a constant value.
  /// If VT is a vector type, the constant is splatted into a BUILD_VECTOR.
  ///
  /// If only legal types can be produced, this does the necessary
  /// transformations (e.g., if the vector element type is illegal).
  /// @{
  SDValue getConstant(uint64_t Val, const SDLoc &DL, EVT VT,
                      bool isTarget = false, bool isOpaque = false);
  SDValue getConstant(const APInt &Val, const SDLoc &DL, EVT VT,
                      bool isTarget = false, bool isOpaque = false);

  SDValue getAllOnesConstant(const SDLoc &DL, EVT VT, bool IsTarget = false,
                             bool IsOpaque = false) {
    return getConstant(APInt::getAllOnesValue(VT.getScalarSizeInBits()), DL,
                       VT, IsTarget, IsOpaque);
  }

  SDValue getConstant(const ConstantInt &Val, const SDLoc &DL, EVT VT,
                      bool isTarget = false, bool isOpaque = false);
  SDValue getIntPtrConstant(uint64_t Val, const SDLoc &DL,
                            bool isTarget = false);
  SDValue getTargetConstant(uint64_t Val, const SDLoc &DL, EVT VT,
                            bool isOpaque = false) {
    return getConstant(Val, DL, VT, true, isOpaque);
  }
  SDValue getTargetConstant(const APInt &Val, const SDLoc &DL, EVT VT,
                            bool isOpaque = false) {
    return getConstant(Val, DL, VT, true, isOpaque);
  }
  SDValue getTargetConstant(const ConstantInt &Val, const SDLoc &DL, EVT VT,
                            bool isOpaque = false) {
    return getConstant(Val, DL, VT, true, isOpaque);
  }

  /// Create a true or false constant of type \p VT using the target's
  /// BooleanContent for type \p OpVT.
  SDValue getBoolConstant(bool V, const SDLoc &DL, EVT VT, EVT OpVT);
  /// @}

  /// Create a ConstantFPSDNode wrapping a constant value.
  /// If VT is a vector type, the constant is splatted into a BUILD_VECTOR.
  ///
  /// If only legal types can be produced, this does the necessary
  /// transformations (e.g., if the vector element type is illegal).
  /// The forms that take a double should only be used for simple constants
  /// that can be exactly represented in VT.  No checks are made.
  /// @{
  SDValue getConstantFP(double Val, const SDLoc &DL, EVT VT,
                        bool isTarget = false);
  SDValue getConstantFP(const APFloat &Val, const SDLoc &DL, EVT VT,
                        bool isTarget = false);
  SDValue getConstantFP(const ConstantFP &V, const SDLoc &DL, EVT VT,
                        bool isTarget = false);
  SDValue getTargetConstantFP(double Val, const SDLoc &DL, EVT VT) {
    return getConstantFP(Val, DL, VT, true);
  }
  SDValue getTargetConstantFP(const APFloat &Val, const SDLoc &DL, EVT VT) {
    return getConstantFP(Val, DL, VT, true);
  }
  SDValue getTargetConstantFP(const ConstantFP &Val, const SDLoc &DL, EVT VT) {
    return getConstantFP(Val, DL, VT, true);
  }
  /// @}

  SDValue getGlobalAddress(const GlobalValue *GV, const SDLoc &DL, EVT VT,
                           int64_t offset = 0, bool isTargetGA = false,
                           unsigned char TargetFlags = 0);
  SDValue getTargetGlobalAddress(const GlobalValue *GV, const SDLoc &DL, EVT VT,
                                 int64_t offset = 0,
                                 unsigned char TargetFlags = 0) {
    return getGlobalAddress(GV, DL, VT, offset, true, TargetFlags);
  }
  SDValue getFrameIndex(int FI, EVT VT, bool isTarget = false);
  SDValue getTargetFrameIndex(int FI, EVT VT) {
    return getFrameIndex(FI, VT, true);
  }
  SDValue getJumpTable(int JTI, EVT VT, bool isTarget = false,
                       unsigned char TargetFlags = 0);
  SDValue getTargetJumpTable(int JTI, EVT VT, unsigned char TargetFlags = 0) {
    return getJumpTable(JTI, VT, true, TargetFlags);
  }
  SDValue getConstantPool(const Constant *C, EVT VT,
                          unsigned Align = 0, int Offs = 0, bool isT=false,
                          unsigned char TargetFlags = 0);
  SDValue getTargetConstantPool(const Constant *C, EVT VT,
                                unsigned Align = 0, int Offset = 0,
                                unsigned char TargetFlags = 0) {
    return getConstantPool(C, VT, Align, Offset, true, TargetFlags);
  }
  SDValue getConstantPool(MachineConstantPoolValue *C, EVT VT,
                          unsigned Align = 0, int Offs = 0, bool isT=false,
                          unsigned char TargetFlags = 0);
  SDValue getTargetConstantPool(MachineConstantPoolValue *C,
                                  EVT VT, unsigned Align = 0,
                                  int Offset = 0, unsigned char TargetFlags=0) {
    return getConstantPool(C, VT, Align, Offset, true, TargetFlags);
  }
  SDValue getTargetIndex(int Index, EVT VT, int64_t Offset = 0,
                         unsigned char TargetFlags = 0);
  // When generating a branch to a BB, we don't in general know enough
  // to provide debug info for the BB at that time, so keep this one around.
  SDValue getBasicBlock(MachineBasicBlock *MBB);
  SDValue getBasicBlock(MachineBasicBlock *MBB, SDLoc dl);
  SDValue getExternalSymbol(const char *Sym, EVT VT);
  SDValue getExternalSymbol(const char *Sym, const SDLoc &dl, EVT VT);
  SDValue getTargetExternalSymbol(const char *Sym, EVT VT,
                                  unsigned char TargetFlags = 0);
  SDValue getMCSymbol(MCSymbol *Sym, EVT VT);

  SDValue getValueType(EVT);
  SDValue getRegister(unsigned Reg, EVT VT);
  SDValue getRegisterMask(const uint32_t *RegMask);
  SDValue getEHLabel(const SDLoc &dl, SDValue Root, MCSymbol *Label);
  SDValue getLabelNode(unsigned Opcode, const SDLoc &dl, SDValue Root,
                       MCSymbol *Label);
  SDValue getBlockAddress(const BlockAddress *BA, EVT VT,
                          int64_t Offset = 0, bool isTarget = false,
                          unsigned char TargetFlags = 0);
  SDValue getTargetBlockAddress(const BlockAddress *BA, EVT VT,
                                int64_t Offset = 0,
                                unsigned char TargetFlags = 0) {
    return getBlockAddress(BA, VT, Offset, true, TargetFlags);
  }

  SDValue getCopyToReg(SDValue Chain, const SDLoc &dl, unsigned Reg,
                       SDValue N) {
    return getNode(ISD::CopyToReg, dl, MVT::Other, Chain,
                   getRegister(Reg, N.getValueType()), N);
  }

  // This version of the getCopyToReg method takes an extra operand, which
  // indicates that there is potentially an incoming glue value (if Glue is not
  // null) and that there should be a glue result.
  SDValue getCopyToReg(SDValue Chain, const SDLoc &dl, unsigned Reg, SDValue N,
                       SDValue Glue) {
    SDVTList VTs = getVTList(MVT::Other, MVT::Glue);
    SDValue Ops[] = { Chain, getRegister(Reg, N.getValueType()), N, Glue };
    return getNode(ISD::CopyToReg, dl, VTs,
                   makeArrayRef(Ops, Glue.getNode() ? 4 : 3));
  }

  // Similar to last getCopyToReg() except parameter Reg is a SDValue
  SDValue getCopyToReg(SDValue Chain, const SDLoc &dl, SDValue Reg, SDValue N,
                       SDValue Glue) {
    SDVTList VTs = getVTList(MVT::Other, MVT::Glue);
    SDValue Ops[] = { Chain, Reg, N, Glue };
    return getNode(ISD::CopyToReg, dl, VTs,
                   makeArrayRef(Ops, Glue.getNode() ? 4 : 3));
  }

  SDValue getCopyFromReg(SDValue Chain, const SDLoc &dl, unsigned Reg, EVT VT) {
    SDVTList VTs = getVTList(VT, MVT::Other);
    SDValue Ops[] = { Chain, getRegister(Reg, VT) };
    return getNode(ISD::CopyFromReg, dl, VTs, Ops);
  }

  // This version of the getCopyFromReg method takes an extra operand, which
  // indicates that there is potentially an incoming glue value (if Glue is not
  // null) and that there should be a glue result.
  SDValue getCopyFromReg(SDValue Chain, const SDLoc &dl, unsigned Reg, EVT VT,
                         SDValue Glue) {
    SDVTList VTs = getVTList(VT, MVT::Other, MVT::Glue);
    SDValue Ops[] = { Chain, getRegister(Reg, VT), Glue };
    return getNode(ISD::CopyFromReg, dl, VTs,
                   makeArrayRef(Ops, Glue.getNode() ? 3 : 2));
  }

  SDValue getCondCode(ISD::CondCode Cond);

  /// Return an ISD::VECTOR_SHUFFLE node. The number of elements in VT,
  /// which must be a vector type, must match the number of mask elements
  /// NumElts. An integer mask element equal to -1 is treated as undefined.
  SDValue getVectorShuffle(EVT VT, const SDLoc &dl, SDValue N1, SDValue N2,
                           ArrayRef<int> Mask);

  /// Return an ISD::BUILD_VECTOR node. The number of elements in VT,
  /// which must be a vector type, must match the number of operands in Ops.
  /// The operands must have the same type as (or, for integers, a type wider
  /// than) VT's element type.
  SDValue getBuildVector(EVT VT, const SDLoc &DL, ArrayRef<SDValue> Ops) {
    // VerifySDNode (via InsertNode) checks BUILD_VECTOR later.
    return getNode(ISD::BUILD_VECTOR, DL, VT, Ops);
  }

  /// Return an ISD::BUILD_VECTOR node. The number of elements in VT,
  /// which must be a vector type, must match the number of operands in Ops.
  /// The operands must have the same type as (or, for integers, a type wider
  /// than) VT's element type.
  SDValue getBuildVector(EVT VT, const SDLoc &DL, ArrayRef<SDUse> Ops) {
    // VerifySDNode (via InsertNode) checks BUILD_VECTOR later.
    return getNode(ISD::BUILD_VECTOR, DL, VT, Ops);
  }

  /// Return a splat ISD::BUILD_VECTOR node, consisting of Op splatted to all
  /// elements. VT must be a vector type. Op's type must be the same as (or,
  /// for integers, a type wider than) VT's element type.
  SDValue getSplatBuildVector(EVT VT, const SDLoc &DL, SDValue Op) {
    // VerifySDNode (via InsertNode) checks BUILD_VECTOR later.
    if (Op.getOpcode() == ISD::UNDEF) {
      assert((VT.getVectorElementType() == Op.getValueType() ||
              (VT.isInteger() &&
               VT.getVectorElementType().bitsLE(Op.getValueType()))) &&
             "A splatted value must have a width equal or (for integers) "
             "greater than the vector element type!");
      return getNode(ISD::UNDEF, SDLoc(), VT);
    }

    SmallVector<SDValue, 16> Ops(VT.getVectorNumElements(), Op);
    return getNode(ISD::BUILD_VECTOR, DL, VT, Ops);
  }

  /// Returns an ISD::VECTOR_SHUFFLE node semantically equivalent to
  /// the shuffle node in input but with swapped operands.
  ///
  /// Example: shuffle A, B, <0,5,2,7> -> shuffle B, A, <4,1,6,3>
  SDValue getCommutedVectorShuffle(const ShuffleVectorSDNode &SV);

  /// Convert Op, which must be of float type, to the
  /// float type VT, by either extending or rounding (by truncation).
  SDValue getFPExtendOrRound(SDValue Op, const SDLoc &DL, EVT VT);

  /// Convert Op, which must be of integer type, to the
  /// integer type VT, by either any-extending or truncating it.
  SDValue getAnyExtOrTrunc(SDValue Op, const SDLoc &DL, EVT VT);

  /// Convert Op, which must be of integer type, to the
  /// integer type VT, by either sign-extending or truncating it.
  SDValue getSExtOrTrunc(SDValue Op, const SDLoc &DL, EVT VT);

  /// Convert Op, which must be of integer type, to the
  /// integer type VT, by either zero-extending or truncating it.
  SDValue getZExtOrTrunc(SDValue Op, const SDLoc &DL, EVT VT);

  /// Return the expression required to zero extend the Op
  /// value assuming it was the smaller SrcTy value.
  SDValue getZeroExtendInReg(SDValue Op, const SDLoc &DL, EVT VT);

  /// Convert Op, which must be of integer type, to the integer type VT,
  /// by using an extension appropriate for the target's
  /// BooleanContent for type OpVT or truncating it.
  SDValue getBoolExtOrTrunc(SDValue Op, const SDLoc &SL, EVT VT, EVT OpVT);

  /// Create a bitwise NOT operation as (XOR Val, -1).
  SDValue getNOT(const SDLoc &DL, SDValue Val, EVT VT);

  /// Create a logical NOT operation as (XOR Val, BooleanOne).
  SDValue getLogicalNOT(const SDLoc &DL, SDValue Val, EVT VT);

  /// Create an add instruction with appropriate flags when used for
  /// addressing some offset of an object. i.e. if a load is split into multiple
  /// components, create an add nuw from the base pointer to the offset.
  SDValue getObjectPtrOffset(const SDLoc &SL, SDValue Op, int64_t Offset) {
    EVT VT = Op.getValueType();
    return getObjectPtrOffset(SL, Op, getConstant(Offset, SL, VT));
  }

  SDValue getObjectPtrOffset(const SDLoc &SL, SDValue Op, SDValue Offset) {
    EVT VT = Op.getValueType();

    // The object itself can't wrap around the address space, so it shouldn't be
    // possible for the adds of the offsets to the split parts to overflow.
    SDNodeFlags Flags;
    Flags.setNoUnsignedWrap(true);
    return getNode(ISD::ADD, SL, VT, Op, Offset, Flags);
  }

  /// Return a new CALLSEQ_START node, that starts new call frame, in which
  /// InSize bytes are set up inside CALLSEQ_START..CALLSEQ_END sequence and
  /// OutSize specifies part of the frame set up prior to the sequence.
  SDValue getCALLSEQ_START(SDValue Chain, uint64_t InSize, uint64_t OutSize,
                           const SDLoc &DL) {
    SDVTList VTs = getVTList(MVT::Other, MVT::Glue);
    SDValue Ops[] = { Chain,
                      getIntPtrConstant(InSize, DL, true),
                      getIntPtrConstant(OutSize, DL, true) };
    return getNode(ISD::CALLSEQ_START, DL, VTs, Ops);
  }

  /// Return a new CALLSEQ_END node, which always must have a
  /// glue result (to ensure it's not CSE'd).
  /// CALLSEQ_END does not have a useful SDLoc.
  SDValue getCALLSEQ_END(SDValue Chain, SDValue Op1, SDValue Op2,
                         SDValue InGlue, const SDLoc &DL) {
    SDVTList NodeTys = getVTList(MVT::Other, MVT::Glue);
    SmallVector<SDValue, 4> Ops;
    Ops.push_back(Chain);
    Ops.push_back(Op1);
    Ops.push_back(Op2);
    if (InGlue.getNode())
      Ops.push_back(InGlue);
    return getNode(ISD::CALLSEQ_END, DL, NodeTys, Ops);
  }

  /// Return true if the result of this operation is always undefined.
  bool isUndef(unsigned Opcode, ArrayRef<SDValue> Ops);

  /// Return an UNDEF node. UNDEF does not have a useful SDLoc.
  SDValue getUNDEF(EVT VT) {
    return getNode(ISD::UNDEF, SDLoc(), VT);
  }

  /// Return a GLOBAL_OFFSET_TABLE node. This does not have a useful SDLoc.
  SDValue getGLOBAL_OFFSET_TABLE(EVT VT) {
    return getNode(ISD::GLOBAL_OFFSET_TABLE, SDLoc(), VT);
  }

  /// Gets or creates the specified node.
  ///
  SDValue getNode(unsigned Opcode, const SDLoc &DL, EVT VT,
                  ArrayRef<SDUse> Ops);
  SDValue getNode(unsigned Opcode, const SDLoc &DL, EVT VT,
                  ArrayRef<SDValue> Ops, const SDNodeFlags Flags = SDNodeFlags());
  SDValue getNode(unsigned Opcode, const SDLoc &DL, ArrayRef<EVT> ResultTys,
                  ArrayRef<SDValue> Ops);
  SDValue getNode(unsigned Opcode, const SDLoc &DL, SDVTList VTList,
                  ArrayRef<SDValue> Ops);

  // Specialize based on number of operands.
  SDValue getNode(unsigned Opcode, const SDLoc &DL, EVT VT);
  SDValue getNode(unsigned Opcode, const SDLoc &DL, EVT VT, SDValue Operand,
                  const SDNodeFlags Flags = SDNodeFlags());
  SDValue getNode(unsigned Opcode, const SDLoc &DL, EVT VT, SDValue N1,
                  SDValue N2, const SDNodeFlags Flags = SDNodeFlags());
  SDValue getNode(unsigned Opcode, const SDLoc &DL, EVT VT, SDValue N1,
                  SDValue N2, SDValue N3,
                  const SDNodeFlags Flags = SDNodeFlags());
  SDValue getNode(unsigned Opcode, const SDLoc &DL, EVT VT, SDValue N1,
                  SDValue N2, SDValue N3, SDValue N4);
  SDValue getNode(unsigned Opcode, const SDLoc &DL, EVT VT, SDValue N1,
                  SDValue N2, SDValue N3, SDValue N4, SDValue N5);

  // Specialize again based on number of operands for nodes with a VTList
  // rather than a single VT.
  SDValue getNode(unsigned Opcode, const SDLoc &DL, SDVTList VTList);
  SDValue getNode(unsigned Opcode, const SDLoc &DL, SDVTList VTList, SDValue N);
  SDValue getNode(unsigned Opcode, const SDLoc &DL, SDVTList VTList, SDValue N1,
                  SDValue N2);
  SDValue getNode(unsigned Opcode, const SDLoc &DL, SDVTList VTList, SDValue N1,
                  SDValue N2, SDValue N3);
  SDValue getNode(unsigned Opcode, const SDLoc &DL, SDVTList VTList, SDValue N1,
                  SDValue N2, SDValue N3, SDValue N4);
  SDValue getNode(unsigned Opcode, const SDLoc &DL, SDVTList VTList, SDValue N1,
                  SDValue N2, SDValue N3, SDValue N4, SDValue N5);

  /// Compute a TokenFactor to force all the incoming stack arguments to be
  /// loaded from the stack. This is used in tail call lowering to protect
  /// stack arguments from being clobbered.
  SDValue getStackArgumentTokenFactor(SDValue Chain);

  SDValue getMemcpy(SDValue Chain, const SDLoc &dl, SDValue Dst, SDValue Src,
                    SDValue Size, unsigned Align, bool isVol, bool AlwaysInline,
                    bool isTailCall, MachinePointerInfo DstPtrInfo,
                    MachinePointerInfo SrcPtrInfo);

  SDValue getMemmove(SDValue Chain, const SDLoc &dl, SDValue Dst, SDValue Src,
                     SDValue Size, unsigned Align, bool isVol, bool isTailCall,
                     MachinePointerInfo DstPtrInfo,
                     MachinePointerInfo SrcPtrInfo);

  SDValue getMemset(SDValue Chain, const SDLoc &dl, SDValue Dst, SDValue Src,
                    SDValue Size, unsigned Align, bool isVol, bool isTailCall,
                    MachinePointerInfo DstPtrInfo);

  SDValue getAtomicMemcpy(SDValue Chain, const SDLoc &dl, SDValue Dst,
                          unsigned DstAlign, SDValue Src, unsigned SrcAlign,
                          SDValue Size, Type *SizeTy, unsigned ElemSz,
                          bool isTailCall, MachinePointerInfo DstPtrInfo,
                          MachinePointerInfo SrcPtrInfo);

  SDValue getAtomicMemmove(SDValue Chain, const SDLoc &dl, SDValue Dst,
                           unsigned DstAlign, SDValue Src, unsigned SrcAlign,
                           SDValue Size, Type *SizeTy, unsigned ElemSz,
                           bool isTailCall, MachinePointerInfo DstPtrInfo,
                           MachinePointerInfo SrcPtrInfo);

  SDValue getAtomicMemset(SDValue Chain, const SDLoc &dl, SDValue Dst,
                          unsigned DstAlign, SDValue Value, SDValue Size,
                          Type *SizeTy, unsigned ElemSz, bool isTailCall,
                          MachinePointerInfo DstPtrInfo);

  /// Helper function to make it easier to build SetCC's if you just have an
  /// ISD::CondCode instead of an SDValue.
  SDValue getSetCC(const SDLoc &DL, EVT VT, SDValue LHS, SDValue RHS,
                   ISD::CondCode Cond) {
    assert(LHS.getValueType().isVector() == RHS.getValueType().isVector() &&
           "Cannot compare scalars to vectors");
    assert(LHS.getValueType().isVector() == VT.isVector() &&
           "Cannot compare scalars to vectors");
    assert(Cond != ISD::SETCC_INVALID &&
           "Cannot create a setCC of an invalid node.");
    return getNode(ISD::SETCC, DL, VT, LHS, RHS, getCondCode(Cond));
  }

  /// Helper function to make it easier to build Select's if you just have
  /// operands and don't want to check for vector.
  SDValue getSelect(const SDLoc &DL, EVT VT, SDValue Cond, SDValue LHS,
                    SDValue RHS) {
    assert(LHS.getValueType() == RHS.getValueType() &&
           "Cannot use select on differing types");
    assert(VT.isVector() == LHS.getValueType().isVector() &&
           "Cannot mix vectors and scalars");
    auto Opcode = Cond.getValueType().isVector() ? ISD::VSELECT : ISD::SELECT;
    return getNode(Opcode, DL, VT, Cond, LHS, RHS);
  }

  /// Helper function to make it easier to build SelectCC's if you just have an
  /// ISD::CondCode instead of an SDValue.
  SDValue getSelectCC(const SDLoc &DL, SDValue LHS, SDValue RHS, SDValue True,
                      SDValue False, ISD::CondCode Cond) {
    return getNode(ISD::SELECT_CC, DL, True.getValueType(), LHS, RHS, True,
                   False, getCondCode(Cond));
  }

  /// Try to simplify a select/vselect into 1 of its operands or a constant.
  SDValue simplifySelect(SDValue Cond, SDValue TVal, SDValue FVal);

  /// Try to simplify a shift into 1 of its operands or a constant.
  SDValue simplifyShift(SDValue X, SDValue Y);

  /// VAArg produces a result and token chain, and takes a pointer
  /// and a source value as input.
  SDValue getVAArg(EVT VT, const SDLoc &dl, SDValue Chain, SDValue Ptr,
                   SDValue SV, unsigned Align);

  /// Gets a node for an atomic cmpxchg op. There are two
  /// valid Opcodes. ISD::ATOMIC_CMO_SWAP produces the value loaded and a
  /// chain result. ISD::ATOMIC_CMP_SWAP_WITH_SUCCESS produces the value loaded,
  /// a success flag (initially i1), and a chain.
  SDValue getAtomicCmpSwap(unsigned Opcode, const SDLoc &dl, EVT MemVT,
                           SDVTList VTs, SDValue Chain, SDValue Ptr,
                           SDValue Cmp, SDValue Swp, MachinePointerInfo PtrInfo,
                           unsigned Alignment, AtomicOrdering SuccessOrdering,
                           AtomicOrdering FailureOrdering,
                           SyncScope::ID SSID);
  SDValue getAtomicCmpSwap(unsigned Opcode, const SDLoc &dl, EVT MemVT,
                           SDVTList VTs, SDValue Chain, SDValue Ptr,
                           SDValue Cmp, SDValue Swp, MachineMemOperand *MMO);

  /// Gets a node for an atomic op, produces result (if relevant)
  /// and chain and takes 2 operands.
  SDValue getAtomic(unsigned Opcode, const SDLoc &dl, EVT MemVT, SDValue Chain,
                    SDValue Ptr, SDValue Val, const Value *PtrVal,
                    unsigned Alignment, AtomicOrdering Ordering,
                    SyncScope::ID SSID);
  SDValue getAtomic(unsigned Opcode, const SDLoc &dl, EVT MemVT, SDValue Chain,
                    SDValue Ptr, SDValue Val, MachineMemOperand *MMO);

  /// Gets a node for an atomic op, produces result and chain and
  /// takes 1 operand.
  SDValue getAtomic(unsigned Opcode, const SDLoc &dl, EVT MemVT, EVT VT,
                    SDValue Chain, SDValue Ptr, MachineMemOperand *MMO);

  /// Gets a node for an atomic op, produces result and chain and takes N
  /// operands.
  SDValue getAtomic(unsigned Opcode, const SDLoc &dl, EVT MemVT,
                    SDVTList VTList, ArrayRef<SDValue> Ops,
                    MachineMemOperand *MMO);

  /// Creates a MemIntrinsicNode that may produce a
  /// result and takes a list of operands. Opcode may be INTRINSIC_VOID,
  /// INTRINSIC_W_CHAIN, or a target-specific opcode with a value not
  /// less than FIRST_TARGET_MEMORY_OPCODE.
  SDValue getMemIntrinsicNode(
    unsigned Opcode, const SDLoc &dl, SDVTList VTList,
    ArrayRef<SDValue> Ops, EVT MemVT,
    MachinePointerInfo PtrInfo,
    unsigned Align = 0,
    MachineMemOperand::Flags Flags
    = MachineMemOperand::MOLoad | MachineMemOperand::MOStore,
    unsigned Size = 0);

  SDValue getMemIntrinsicNode(unsigned Opcode, const SDLoc &dl, SDVTList VTList,
                              ArrayRef<SDValue> Ops, EVT MemVT,
                              MachineMemOperand *MMO);

  /// Create a MERGE_VALUES node from the given operands.
  SDValue getMergeValues(ArrayRef<SDValue> Ops, const SDLoc &dl);

  /// Loads are not normal binary operators: their result type is not
  /// determined by their operands, and they produce a value AND a token chain.
  ///
  /// This function will set the MOLoad flag on MMOFlags, but you can set it if
  /// you want.  The MOStore flag must not be set.
  SDValue getLoad(EVT VT, const SDLoc &dl, SDValue Chain, SDValue Ptr,
                  MachinePointerInfo PtrInfo, unsigned Alignment = 0,
                  MachineMemOperand::Flags MMOFlags = MachineMemOperand::MONone,
                  const AAMDNodes &AAInfo = AAMDNodes(),
                  const MDNode *Ranges = nullptr);
  SDValue getLoad(EVT VT, const SDLoc &dl, SDValue Chain, SDValue Ptr,
                  MachineMemOperand *MMO);
  SDValue
  getExtLoad(ISD::LoadExtType ExtType, const SDLoc &dl, EVT VT, SDValue Chain,
             SDValue Ptr, MachinePointerInfo PtrInfo, EVT MemVT,
             unsigned Alignment = 0,
             MachineMemOperand::Flags MMOFlags = MachineMemOperand::MONone,
             const AAMDNodes &AAInfo = AAMDNodes());
  SDValue getExtLoad(ISD::LoadExtType ExtType, const SDLoc &dl, EVT VT,
                     SDValue Chain, SDValue Ptr, EVT MemVT,
                     MachineMemOperand *MMO);
  SDValue getIndexedLoad(SDValue OrigLoad, const SDLoc &dl, SDValue Base,
                         SDValue Offset, ISD::MemIndexedMode AM);
  SDValue getLoad(ISD::MemIndexedMode AM, ISD::LoadExtType ExtType, EVT VT,
                  const SDLoc &dl, SDValue Chain, SDValue Ptr, SDValue Offset,
                  MachinePointerInfo PtrInfo, EVT MemVT, unsigned Alignment = 0,
                  MachineMemOperand::Flags MMOFlags = MachineMemOperand::MONone,
                  const AAMDNodes &AAInfo = AAMDNodes(),
                  const MDNode *Ranges = nullptr);
  SDValue getLoad(ISD::MemIndexedMode AM, ISD::LoadExtType ExtType, EVT VT,
                  const SDLoc &dl, SDValue Chain, SDValue Ptr, SDValue Offset,
                  EVT MemVT, MachineMemOperand *MMO);

  /// Helper function to build ISD::STORE nodes.
  ///
  /// This function will set the MOStore flag on MMOFlags, but you can set it if
  /// you want.  The MOLoad and MOInvariant flags must not be set.
  SDValue
  getStore(SDValue Chain, const SDLoc &dl, SDValue Val, SDValue Ptr,
           MachinePointerInfo PtrInfo, unsigned Alignment = 0,
           MachineMemOperand::Flags MMOFlags = MachineMemOperand::MONone,
           const AAMDNodes &AAInfo = AAMDNodes());
  SDValue getStore(SDValue Chain, const SDLoc &dl, SDValue Val, SDValue Ptr,
                   MachineMemOperand *MMO);
  SDValue
  getTruncStore(SDValue Chain, const SDLoc &dl, SDValue Val, SDValue Ptr,
                MachinePointerInfo PtrInfo, EVT SVT, unsigned Alignment = 0,
                MachineMemOperand::Flags MMOFlags = MachineMemOperand::MONone,
                const AAMDNodes &AAInfo = AAMDNodes());
  SDValue getTruncStore(SDValue Chain, const SDLoc &dl, SDValue Val,
                        SDValue Ptr, EVT SVT, MachineMemOperand *MMO);
  SDValue getIndexedStore(SDValue OrigStore, const SDLoc &dl, SDValue Base,
                          SDValue Offset, ISD::MemIndexedMode AM);

  /// Returns sum of the base pointer and offset.
  SDValue getMemBasePlusOffset(SDValue Base, unsigned Offset, const SDLoc &DL);

  SDValue getMaskedLoad(EVT VT, const SDLoc &dl, SDValue Chain, SDValue Ptr,
                        SDValue Mask, SDValue Src0, EVT MemVT,
                        MachineMemOperand *MMO, ISD::LoadExtType,
                        bool IsExpanding = false);
  SDValue getMaskedStore(SDValue Chain, const SDLoc &dl, SDValue Val,
                         SDValue Ptr, SDValue Mask, EVT MemVT,
                         MachineMemOperand *MMO, bool IsTruncating = false,
                         bool IsCompressing = false);
  SDValue getMaskedGather(SDVTList VTs, EVT VT, const SDLoc &dl,
                          ArrayRef<SDValue> Ops, MachineMemOperand *MMO);
  SDValue getMaskedScatter(SDVTList VTs, EVT VT, const SDLoc &dl,
                           ArrayRef<SDValue> Ops, MachineMemOperand *MMO);

  /// Return (create a new or find existing) a target-specific node.
  /// TargetMemSDNode should be derived class from MemSDNode.
  template <class TargetMemSDNode>
  SDValue getTargetMemSDNode(SDVTList VTs, ArrayRef<SDValue> Ops,
                             const SDLoc &dl, EVT MemVT,
                             MachineMemOperand *MMO);

  /// Construct a node to track a Value* through the backend.
  SDValue getSrcValue(const Value *v);

  /// Return an MDNodeSDNode which holds an MDNode.
  SDValue getMDNode(const MDNode *MD);

  /// Return a bitcast using the SDLoc of the value operand, and casting to the
  /// provided type. Use getNode to set a custom SDLoc.
  SDValue getBitcast(EVT VT, SDValue V);

  /// Return an AddrSpaceCastSDNode.
  SDValue getAddrSpaceCast(const SDLoc &dl, EVT VT, SDValue Ptr, unsigned SrcAS,
                           unsigned DestAS);

  /// Return the specified value casted to
  /// the target's desired shift amount type.
  SDValue getShiftAmountOperand(EVT LHSTy, SDValue Op);

  /// Expand the specified \c ISD::VAARG node as the Legalize pass would.
  SDValue expandVAArg(SDNode *Node);

  /// Expand the specified \c ISD::VACOPY node as the Legalize pass would.
  SDValue expandVACopy(SDNode *Node);

  /// Returs an GlobalAddress of the function from the current module with
  /// name matching the given ExternalSymbol. Additionally can provide the
  /// matched function.
  /// Panics the function doesn't exists.
  SDValue getSymbolFunctionGlobalAddress(SDValue Op,
                                         Function **TargetFunction = nullptr);

  /// *Mutate* the specified node in-place to have the
  /// specified operands.  If the resultant node already exists in the DAG,
  /// this does not modify the specified node, instead it returns the node that
  /// already exists.  If the resultant node does not exist in the DAG, the
  /// input node is returned.  As a degenerate case, if you specify the same
  /// input operands as the node already has, the input node is returned.
  SDNode *UpdateNodeOperands(SDNode *N, SDValue Op);
  SDNode *UpdateNodeOperands(SDNode *N, SDValue Op1, SDValue Op2);
  SDNode *UpdateNodeOperands(SDNode *N, SDValue Op1, SDValue Op2,
                               SDValue Op3);
  SDNode *UpdateNodeOperands(SDNode *N, SDValue Op1, SDValue Op2,
                               SDValue Op3, SDValue Op4);
  SDNode *UpdateNodeOperands(SDNode *N, SDValue Op1, SDValue Op2,
                               SDValue Op3, SDValue Op4, SDValue Op5);
  SDNode *UpdateNodeOperands(SDNode *N, ArrayRef<SDValue> Ops);

  /// *Mutate* the specified machine node's memory references to the provided
  /// list.
  void setNodeMemRefs(MachineSDNode *N,
                      ArrayRef<MachineMemOperand *> NewMemRefs);

  // Propagates the change in divergence to users
  void updateDivergence(SDNode * N);

  /// These are used for target selectors to *mutate* the
  /// specified node to have the specified return type, Target opcode, and
  /// operands.  Note that target opcodes are stored as
  /// ~TargetOpcode in the node opcode field.  The resultant node is returned.
  SDNode *SelectNodeTo(SDNode *N, unsigned MachineOpc, EVT VT);
  SDNode *SelectNodeTo(SDNode *N, unsigned MachineOpc, EVT VT, SDValue Op1);
  SDNode *SelectNodeTo(SDNode *N, unsigned MachineOpc, EVT VT,
                       SDValue Op1, SDValue Op2);
  SDNode *SelectNodeTo(SDNode *N, unsigned MachineOpc, EVT VT,
                       SDValue Op1, SDValue Op2, SDValue Op3);
  SDNode *SelectNodeTo(SDNode *N, unsigned MachineOpc, EVT VT,
                       ArrayRef<SDValue> Ops);
  SDNode *SelectNodeTo(SDNode *N, unsigned MachineOpc, EVT VT1, EVT VT2);
  SDNode *SelectNodeTo(SDNode *N, unsigned MachineOpc, EVT VT1,
                       EVT VT2, ArrayRef<SDValue> Ops);
  SDNode *SelectNodeTo(SDNode *N, unsigned MachineOpc, EVT VT1,
                       EVT VT2, EVT VT3, ArrayRef<SDValue> Ops);
  SDNode *SelectNodeTo(SDNode *N, unsigned TargetOpc, EVT VT1,
                       EVT VT2, SDValue Op1);
  SDNode *SelectNodeTo(SDNode *N, unsigned MachineOpc, EVT VT1,
                       EVT VT2, SDValue Op1, SDValue Op2);
  SDNode *SelectNodeTo(SDNode *N, unsigned MachineOpc, SDVTList VTs,
                       ArrayRef<SDValue> Ops);

  /// This *mutates* the specified node to have the specified
  /// return type, opcode, and operands.
  SDNode *MorphNodeTo(SDNode *N, unsigned Opc, SDVTList VTs,
                      ArrayRef<SDValue> Ops);

  /// Mutate the specified strict FP node to its non-strict equivalent,
  /// unlinking the node from its chain and dropping the metadata arguments.
  /// The node must be a strict FP node.
  SDNode *mutateStrictFPToFP(SDNode *Node);

  /// These are used for target selectors to create a new node
  /// with specified return type(s), MachineInstr opcode, and operands.
  ///
  /// Note that getMachineNode returns the resultant node.  If there is already
  /// a node of the specified opcode and operands, it returns that node instead
  /// of the current one.
  MachineSDNode *getMachineNode(unsigned Opcode, const SDLoc &dl, EVT VT);
  MachineSDNode *getMachineNode(unsigned Opcode, const SDLoc &dl, EVT VT,
                                SDValue Op1);
  MachineSDNode *getMachineNode(unsigned Opcode, const SDLoc &dl, EVT VT,
                                SDValue Op1, SDValue Op2);
  MachineSDNode *getMachineNode(unsigned Opcode, const SDLoc &dl, EVT VT,
                                SDValue Op1, SDValue Op2, SDValue Op3);
  MachineSDNode *getMachineNode(unsigned Opcode, const SDLoc &dl, EVT VT,
                                ArrayRef<SDValue> Ops);
  MachineSDNode *getMachineNode(unsigned Opcode, const SDLoc &dl, EVT VT1,
                                EVT VT2, SDValue Op1, SDValue Op2);
  MachineSDNode *getMachineNode(unsigned Opcode, const SDLoc &dl, EVT VT1,
                                EVT VT2, SDValue Op1, SDValue Op2, SDValue Op3);
  MachineSDNode *getMachineNode(unsigned Opcode, const SDLoc &dl, EVT VT1,
                                EVT VT2, ArrayRef<SDValue> Ops);
  MachineSDNode *getMachineNode(unsigned Opcode, const SDLoc &dl, EVT VT1,
                                EVT VT2, EVT VT3, SDValue Op1, SDValue Op2);
  MachineSDNode *getMachineNode(unsigned Opcode, const SDLoc &dl, EVT VT1,
                                EVT VT2, EVT VT3, SDValue Op1, SDValue Op2,
                                SDValue Op3);
  MachineSDNode *getMachineNode(unsigned Opcode, const SDLoc &dl, EVT VT1,
                                EVT VT2, EVT VT3, ArrayRef<SDValue> Ops);
  MachineSDNode *getMachineNode(unsigned Opcode, const SDLoc &dl,
                                ArrayRef<EVT> ResultTys, ArrayRef<SDValue> Ops);
  MachineSDNode *getMachineNode(unsigned Opcode, const SDLoc &dl, SDVTList VTs,
                                ArrayRef<SDValue> Ops);

  /// A convenience function for creating TargetInstrInfo::EXTRACT_SUBREG nodes.
  SDValue getTargetExtractSubreg(int SRIdx, const SDLoc &DL, EVT VT,
                                 SDValue Operand);

  /// A convenience function for creating TargetInstrInfo::INSERT_SUBREG nodes.
  SDValue getTargetInsertSubreg(int SRIdx, const SDLoc &DL, EVT VT,
                                SDValue Operand, SDValue Subreg);

  /// Get the specified node if it's already available, or else return NULL.
  SDNode *getNodeIfExists(unsigned Opcode, SDVTList VTList, ArrayRef<SDValue> Ops,
                          const SDNodeFlags Flags = SDNodeFlags());

  /// Creates a SDDbgValue node.
  SDDbgValue *getDbgValue(DIVariable *Var, DIExpression *Expr, SDNode *N,
                          unsigned R, bool IsIndirect, const DebugLoc &DL,
                          unsigned O);

  /// Creates a constant SDDbgValue node.
  SDDbgValue *getConstantDbgValue(DIVariable *Var, DIExpression *Expr,
                                  const Value *C, const DebugLoc &DL,
                                  unsigned O);

  /// Creates a FrameIndex SDDbgValue node.
  SDDbgValue *getFrameIndexDbgValue(DIVariable *Var, DIExpression *Expr,
                                    unsigned FI, bool IsIndirect,
                                    const DebugLoc &DL, unsigned O);

  /// Creates a VReg SDDbgValue node.
  SDDbgValue *getVRegDbgValue(DIVariable *Var, DIExpression *Expr,
                              unsigned VReg, bool IsIndirect,
                              const DebugLoc &DL, unsigned O);

  /// Creates a SDDbgLabel node.
  SDDbgLabel *getDbgLabel(DILabel *Label, const DebugLoc &DL, unsigned O);

  /// Transfer debug values from one node to another, while optionally
  /// generating fragment expressions for split-up values. If \p InvalidateDbg
  /// is set, debug values are invalidated after they are transferred.
  void transferDbgValues(SDValue From, SDValue To, unsigned OffsetInBits = 0,
                         unsigned SizeInBits = 0, bool InvalidateDbg = true);

  /// Remove the specified node from the system. If any of its
  /// operands then becomes dead, remove them as well. Inform UpdateListener
  /// for each node deleted.
  void RemoveDeadNode(SDNode *N);

  /// This method deletes the unreachable nodes in the
  /// given list, and any nodes that become unreachable as a result.
  void RemoveDeadNodes(SmallVectorImpl<SDNode *> &DeadNodes);

  /// Modify anything using 'From' to use 'To' instead.
  /// This can cause recursive merging of nodes in the DAG.  Use the first
  /// version if 'From' is known to have a single result, use the second
  /// if you have two nodes with identical results (or if 'To' has a superset
  /// of the results of 'From'), use the third otherwise.
  ///
  /// These methods all take an optional UpdateListener, which (if not null) is
  /// informed about nodes that are deleted and modified due to recursive
  /// changes in the dag.
  ///
  /// These functions only replace all existing uses. It's possible that as
  /// these replacements are being performed, CSE may cause the From node
  /// to be given new uses. These new uses of From are left in place, and
  /// not automatically transferred to To.
  ///
  void ReplaceAllUsesWith(SDValue From, SDValue To);
  void ReplaceAllUsesWith(SDNode *From, SDNode *To);
  void ReplaceAllUsesWith(SDNode *From, const SDValue *To);

  /// Replace any uses of From with To, leaving
  /// uses of other values produced by From.getNode() alone.
  void ReplaceAllUsesOfValueWith(SDValue From, SDValue To);

  /// Like ReplaceAllUsesOfValueWith, but for multiple values at once.
  /// This correctly handles the case where
  /// there is an overlap between the From values and the To values.
  void ReplaceAllUsesOfValuesWith(const SDValue *From, const SDValue *To,
                                  unsigned Num);

  /// If an existing load has uses of its chain, create a token factor node with
  /// that chain and the new memory node's chain and update users of the old
  /// chain to the token factor. This ensures that the new memory node will have
  /// the same relative memory dependency position as the old load. Returns the
  /// new merged load chain.
  SDValue makeEquivalentMemoryOrdering(LoadSDNode *Old, SDValue New);

  /// Topological-sort the AllNodes list and a
  /// assign a unique node id for each node in the DAG based on their
  /// topological order. Returns the number of nodes.
  unsigned AssignTopologicalOrder();

  /// Move node N in the AllNodes list to be immediately
  /// before the given iterator Position. This may be used to update the
  /// topological ordering when the list of nodes is modified.
  void RepositionNode(allnodes_iterator Position, SDNode *N) {
    AllNodes.insert(Position, AllNodes.remove(N));
  }

  /// Returns an APFloat semantics tag appropriate for the given type. If VT is
  /// a vector type, the element semantics are returned.
  static const fltSemantics &EVTToAPFloatSemantics(EVT VT) {
    switch (VT.getScalarType().getSimpleVT().SimpleTy) {
    default: llvm_unreachable("Unknown FP format");
    case MVT::f16:     return APFloat::IEEEhalf();
    case MVT::f32:     return APFloat::IEEEsingle();
    case MVT::f64:     return APFloat::IEEEdouble();
    case MVT::f80:     return APFloat::x87DoubleExtended();
    case MVT::f128:    return APFloat::IEEEquad();
    case MVT::ppcf128: return APFloat::PPCDoubleDouble();
    }
  }

  /// Add a dbg_value SDNode. If SD is non-null that means the
  /// value is produced by SD.
  void AddDbgValue(SDDbgValue *DB, SDNode *SD, bool isParameter);

  /// Add a dbg_label SDNode.
  void AddDbgLabel(SDDbgLabel *DB);

  /// Get the debug values which reference the given SDNode.
  ArrayRef<SDDbgValue*> GetDbgValues(const SDNode* SD) const {
    return DbgInfo->getSDDbgValues(SD);
  }

public:
  /// Return true if there are any SDDbgValue nodes associated
  /// with this SelectionDAG.
  bool hasDebugValues() const { return !DbgInfo->empty(); }

  SDDbgInfo::DbgIterator DbgBegin() { return DbgInfo->DbgBegin(); }
  SDDbgInfo::DbgIterator DbgEnd()   { return DbgInfo->DbgEnd(); }

  SDDbgInfo::DbgIterator ByvalParmDbgBegin() {
    return DbgInfo->ByvalParmDbgBegin();
  }

  SDDbgInfo::DbgIterator ByvalParmDbgEnd()   {
    return DbgInfo->ByvalParmDbgEnd();
  }

  SDDbgInfo::DbgLabelIterator DbgLabelBegin() {
    return DbgInfo->DbgLabelBegin();
  }
  SDDbgInfo::DbgLabelIterator DbgLabelEnd() {
    return DbgInfo->DbgLabelEnd();
  }

  /// To be invoked on an SDNode that is slated to be erased. This
  /// function mirrors \c llvm::salvageDebugInfo.
  void salvageDebugInfo(SDNode &N);

  void dump() const;

  /// Create a stack temporary, suitable for holding the specified value type.
  /// If minAlign is specified, the slot size will have at least that alignment.
  SDValue CreateStackTemporary(EVT VT, unsigned minAlign = 1);

  /// Create a stack temporary suitable for holding either of the specified
  /// value types.
  SDValue CreateStackTemporary(EVT VT1, EVT VT2);

  SDValue FoldSymbolOffset(unsigned Opcode, EVT VT,
                           const GlobalAddressSDNode *GA,
                           const SDNode *N2);

  SDValue FoldConstantArithmetic(unsigned Opcode, const SDLoc &DL, EVT VT,
                                 SDNode *Cst1, SDNode *Cst2);

  SDValue FoldConstantArithmetic(unsigned Opcode, const SDLoc &DL, EVT VT,
                                 const ConstantSDNode *Cst1,
                                 const ConstantSDNode *Cst2);

  SDValue FoldConstantVectorArithmetic(unsigned Opcode, const SDLoc &DL, EVT VT,
                                       ArrayRef<SDValue> Ops,
                                       const SDNodeFlags Flags = SDNodeFlags());

  /// Constant fold a setcc to true or false.
  SDValue FoldSetCC(EVT VT, SDValue N1, SDValue N2, ISD::CondCode Cond,
                    const SDLoc &dl);

  /// See if the specified operand can be simplified with the knowledge that only
  /// the bits specified by Mask are used.  If so, return the simpler operand,
  /// otherwise return a null SDValue.
  ///
  /// (This exists alongside SimplifyDemandedBits because GetDemandedBits can
  /// simplify nodes with multiple uses more aggressively.)
  SDValue GetDemandedBits(SDValue V, const APInt &Mask);

  /// Return true if the sign bit of Op is known to be zero.
  /// We use this predicate to simplify operations downstream.
  bool SignBitIsZero(SDValue Op, unsigned Depth = 0) const;

  /// Return true if 'Op & Mask' is known to be zero.  We
  /// use this predicate to simplify operations downstream.  Op and Mask are
  /// known to be the same type.
  bool MaskedValueIsZero(SDValue Op, const APInt &Mask, unsigned Depth = 0)
    const;

  /// Determine which bits of Op are known to be either zero or one and return
  /// them in Known. For vectors, the known bits are those that are shared by
  /// every vector element.
  /// Targets can implement the computeKnownBitsForTargetNode method in the
  /// TargetLowering class to allow target nodes to be understood.
  KnownBits computeKnownBits(SDValue Op, unsigned Depth = 0) const;

  /// Determine which bits of Op are known to be either zero or one and return
  /// them in Known. The DemandedElts argument allows us to only collect the
  /// known bits that are shared by the requested vector elements.
  /// Targets can implement the computeKnownBitsForTargetNode method in the
  /// TargetLowering class to allow target nodes to be understood.
  KnownBits computeKnownBits(SDValue Op, const APInt &DemandedElts,
                             unsigned Depth = 0) const;

  /// Used to represent the possible overflow behavior of an operation.
  /// Never: the operation cannot overflow.
  /// Always: the operation will always overflow.
  /// Sometime: the operation may or may not overflow.
  enum OverflowKind {
    OFK_Never,
    OFK_Sometime,
    OFK_Always,
  };

  /// Determine if the result of the addition of 2 node can overflow.
  OverflowKind computeOverflowKind(SDValue N0, SDValue N1) const;

  /// Test if the given value is known to have exactly one bit set. This differs
  /// from computeKnownBits in that it doesn't necessarily determine which bit
  /// is set.
  bool isKnownToBeAPowerOfTwo(SDValue Val) const;

  /// Return the number of times the sign bit of the register is replicated into
  /// the other bits. We know that at least 1 bit is always equal to the sign
  /// bit (itself), but other cases can give us information. For example,
  /// immediately after an "SRA X, 2", we know that the top 3 bits are all equal
  /// to each other, so we return 3. Targets can implement the
  /// ComputeNumSignBitsForTarget method in the TargetLowering class to allow
  /// target nodes to be understood.
  unsigned ComputeNumSignBits(SDValue Op, unsigned Depth = 0) const;

  /// Return the number of times the sign bit of the register is replicated into
  /// the other bits. We know that at least 1 bit is always equal to the sign
  /// bit (itself), but other cases can give us information. For example,
  /// immediately after an "SRA X, 2", we know that the top 3 bits are all equal
  /// to each other, so we return 3. The DemandedElts argument allows
  /// us to only collect the minimum sign bits of the requested vector elements.
  /// Targets can implement the ComputeNumSignBitsForTarget method in the
  /// TargetLowering class to allow target nodes to be understood.
  unsigned ComputeNumSignBits(SDValue Op, const APInt &DemandedElts,
                              unsigned Depth = 0) const;

  /// Return true if the specified operand is an ISD::ADD with a ConstantSDNode
  /// on the right-hand side, or if it is an ISD::OR with a ConstantSDNode that
  /// is guaranteed to have the same semantics as an ADD. This handles the
  /// equivalence:
  ///     X|Cst == X+Cst iff X&Cst = 0.
  bool isBaseWithConstantOffset(SDValue Op) const;

  /// Test whether the given SDValue is known to never be NaN. If \p SNaN is
  /// true, returns if \p Op is known to never be a signaling NaN (it may still
  /// be a qNaN).
  bool isKnownNeverNaN(SDValue Op, bool SNaN = false, unsigned Depth = 0) const;

  /// \returns true if \p Op is known to never be a signaling NaN.
  bool isKnownNeverSNaN(SDValue Op, unsigned Depth = 0) const {
    return isKnownNeverNaN(Op, true, Depth);
  }

  /// Test whether the given floating point SDValue is known to never be
  /// positive or negative zero.
  bool isKnownNeverZeroFloat(SDValue Op) const;

  /// Test whether the given SDValue is known to contain non-zero value(s).
  bool isKnownNeverZero(SDValue Op) const;

  /// Test whether two SDValues are known to compare equal. This
  /// is true if they are the same value, or if one is negative zero and the
  /// other positive zero.
  bool isEqualTo(SDValue A, SDValue B) const;

  /// Return true if A and B have no common bits set. As an example, this can
  /// allow an 'add' to be transformed into an 'or'.
  bool haveNoCommonBitsSet(SDValue A, SDValue B) const;

  /// Test whether \p V has a splatted value for all the demanded elements.
  ///
  /// On success \p UndefElts will indicate the elements that have UNDEF
  /// values instead of the splat value, this is only guaranteed to be correct
  /// for \p DemandedElts.
  ///
  /// NOTE: The function will return true for a demanded splat of UNDEF values.
  bool isSplatValue(SDValue V, const APInt &DemandedElts, APInt &UndefElts);

  /// Test whether \p V has a splatted value.
  bool isSplatValue(SDValue V, bool AllowUndefs = false);

  /// Match a binop + shuffle pyramid that represents a horizontal reduction
  /// over the elements of a vector starting from the EXTRACT_VECTOR_ELT node /p
  /// Extract. The reduction must use one of the opcodes listed in /p
  /// CandidateBinOps and on success /p BinOp will contain the matching opcode.
  /// Returns the vector that is being reduced on, or SDValue() if a reduction
  /// was not matched.
  SDValue matchBinOpReduction(SDNode *Extract, ISD::NodeType &BinOp,
                              ArrayRef<ISD::NodeType> CandidateBinOps);

  /// Utility function used by legalize and lowering to
  /// "unroll" a vector operation by splitting out the scalars and operating
  /// on each element individually.  If the ResNE is 0, fully unroll the vector
  /// op. If ResNE is less than the width of the vector op, unroll up to ResNE.
  /// If the  ResNE is greater than the width of the vector op, unroll the
  /// vector op and fill the end of the resulting vector with UNDEFS.
  SDValue UnrollVectorOp(SDNode *N, unsigned ResNE = 0);

  /// Return true if loads are next to each other and can be
  /// merged. Check that both are nonvolatile and if LD is loading
  /// 'Bytes' bytes from a location that is 'Dist' units away from the
  /// location that the 'Base' load is loading from.
  bool areNonVolatileConsecutiveLoads(LoadSDNode *LD, LoadSDNode *Base,
                                      unsigned Bytes, int Dist) const;

  /// Infer alignment of a load / store address. Return 0 if
  /// it cannot be inferred.
  unsigned InferPtrAlignment(SDValue Ptr) const;

  /// Compute the VTs needed for the low/hi parts of a type
  /// which is split (or expanded) into two not necessarily identical pieces.
  std::pair<EVT, EVT> GetSplitDestVTs(const EVT &VT) const;

  /// Split the vector with EXTRACT_SUBVECTOR using the provides
  /// VTs and return the low/high part.
  std::pair<SDValue, SDValue> SplitVector(const SDValue &N, const SDLoc &DL,
                                          const EVT &LoVT, const EVT &HiVT);

  /// Split the vector with EXTRACT_SUBVECTOR and return the low/high part.
  std::pair<SDValue, SDValue> SplitVector(const SDValue &N, const SDLoc &DL) {
    EVT LoVT, HiVT;
    std::tie(LoVT, HiVT) = GetSplitDestVTs(N.getValueType());
    return SplitVector(N, DL, LoVT, HiVT);
  }

  /// Split the node's operand with EXTRACT_SUBVECTOR and
  /// return the low/high part.
  std::pair<SDValue, SDValue> SplitVectorOperand(const SDNode *N, unsigned OpNo)
  {
    return SplitVector(N->getOperand(OpNo), SDLoc(N));
  }

  /// Append the extracted elements from Start to Count out of the vector Op
  /// in Args. If Count is 0, all of the elements will be extracted.
  void ExtractVectorElements(SDValue Op, SmallVectorImpl<SDValue> &Args,
                             unsigned Start = 0, unsigned Count = 0);

  /// Compute the default alignment value for the given type.
  unsigned getEVTAlignment(EVT MemoryVT) const;

  /// Test whether the given value is a constant int or similar node.
  SDNode *isConstantIntBuildVectorOrConstantInt(SDValue N);

  /// Test whether the given value is a constant FP or similar node.
  SDNode *isConstantFPBuildVectorOrConstantFP(SDValue N);

  /// \returns true if \p N is any kind of constant or build_vector of
  /// constants, int or float. If a vector, it may not necessarily be a splat.
  inline bool isConstantValueOfAnyType(SDValue N) {
    return isConstantIntBuildVectorOrConstantInt(N) ||
           isConstantFPBuildVectorOrConstantFP(N);
  }

private:
  void InsertNode(SDNode *N);
  bool RemoveNodeFromCSEMaps(SDNode *N);
  void AddModifiedNodeToCSEMaps(SDNode *N);
  SDNode *FindModifiedNodeSlot(SDNode *N, SDValue Op, void *&InsertPos);
  SDNode *FindModifiedNodeSlot(SDNode *N, SDValue Op1, SDValue Op2,
                               void *&InsertPos);
  SDNode *FindModifiedNodeSlot(SDNode *N, ArrayRef<SDValue> Ops,
                               void *&InsertPos);
  SDNode *UpdateSDLocOnMergeSDNode(SDNode *N, const SDLoc &loc);

  void DeleteNodeNotInCSEMaps(SDNode *N);
  void DeallocateNode(SDNode *N);

  void allnodes_clear();

  /// Look up the node specified by ID in CSEMap.  If it exists, return it.  If
  /// not, return the insertion token that will make insertion faster.  This
  /// overload is for nodes other than Constant or ConstantFP, use the other one
  /// for those.
  SDNode *FindNodeOrInsertPos(const FoldingSetNodeID &ID, void *&InsertPos);

  /// Look up the node specified by ID in CSEMap.  If it exists, return it.  If
  /// not, return the insertion token that will make insertion faster.  Performs
  /// additional processing for constant nodes.
  SDNode *FindNodeOrInsertPos(const FoldingSetNodeID &ID, const SDLoc &DL,
                              void *&InsertPos);

  /// List of non-single value types.
  FoldingSet<SDVTListNode> VTListMap;

  /// Maps to auto-CSE operations.
  std::vector<CondCodeSDNode*> CondCodeNodes;

  std::vector<SDNode*> ValueTypeNodes;
  std::map<EVT, SDNode*, EVT::compareRawBits> ExtendedValueTypeNodes;
  StringMap<SDNode*> ExternalSymbols;

  std::map<std::pair<std::string, unsigned char>,SDNode*> TargetExternalSymbols;
  DenseMap<MCSymbol *, SDNode *> MCSymbols;
};

template <> struct GraphTraits<SelectionDAG*> : public GraphTraits<SDNode*> {
  using nodes_iterator = pointer_iterator<SelectionDAG::allnodes_iterator>;

  static nodes_iterator nodes_begin(SelectionDAG *G) {
    return nodes_iterator(G->allnodes_begin());
  }

  static nodes_iterator nodes_end(SelectionDAG *G) {
    return nodes_iterator(G->allnodes_end());
  }
};

template <class TargetMemSDNode>
SDValue SelectionDAG::getTargetMemSDNode(SDVTList VTs,
                                         ArrayRef<SDValue> Ops,
                                         const SDLoc &dl, EVT MemVT,
                                         MachineMemOperand *MMO) {
  /// Compose node ID and try to find an existing node.
  FoldingSetNodeID ID;
  unsigned Opcode =
    TargetMemSDNode(dl.getIROrder(), DebugLoc(), VTs, MemVT, MMO).getOpcode();
  ID.AddInteger(Opcode);
  ID.AddPointer(VTs.VTs);
  for (auto& Op : Ops) {
    ID.AddPointer(Op.getNode());
    ID.AddInteger(Op.getResNo());
  }
  ID.AddInteger(MemVT.getRawBits());
  ID.AddInteger(MMO->getPointerInfo().getAddrSpace());
  ID.AddInteger(getSyntheticNodeSubclassData<TargetMemSDNode>(
    dl.getIROrder(), VTs, MemVT, MMO));

  void *IP = nullptr;
  if (SDNode *E = FindNodeOrInsertPos(ID, dl, IP)) {
    cast<TargetMemSDNode>(E)->refineAlignment(MMO);
    return SDValue(E, 0);
  }

  /// Existing node was not found. Create a new one.
  auto *N = newSDNode<TargetMemSDNode>(dl.getIROrder(), dl.getDebugLoc(), VTs,
                                       MemVT, MMO);
  createOperands(N, Ops);
  CSEMap.InsertNode(N, IP);
  InsertNode(N);
  return SDValue(N, 0);
}

} // end namespace llvm

#endif // LLVM_CODEGEN_SELECTIONDAG_H
