//===- llvm/CodeGen/SelectionDAGNodes.h - SelectionDAG Nodes ----*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file declares the SDNode class and derived classes, which are used to
// represent the nodes and operations present in a SelectionDAG.  These nodes
// and operations are machine code level operations, with some similarities to
// the GCC RTL representation.
//
// Clients should include the SelectionDAG.h file instead of this file directly.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CODEGEN_SELECTIONDAGNODES_H
#define LLVM_CODEGEN_SELECTIONDAGNODES_H

#include "llvm/ADT/APFloat.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/BitVector.h"
#include "llvm/ADT/FoldingSet.h"
#include "llvm/ADT/GraphTraits.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/ilist_node.h"
#include "llvm/ADT/iterator.h"
#include "llvm/ADT/iterator_range.h"
#include "llvm/CodeGen/ISDOpcodes.h"
#include "llvm/CodeGen/MachineMemOperand.h"
#include "llvm/CodeGen/ValueTypes.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DebugLoc.h"
#include "llvm/IR/Instruction.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Metadata.h"
#include "llvm/IR/Operator.h"
#include "llvm/Support/AlignOf.h"
#include "llvm/Support/AtomicOrdering.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/MachineValueType.h"
#include <algorithm>
#include <cassert>
#include <climits>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <iterator>
#include <string>
#include <tuple>

namespace llvm {

class APInt;
class Constant;
template <typename T> struct DenseMapInfo;
class GlobalValue;
class MachineBasicBlock;
class MachineConstantPoolValue;
class MCSymbol;
class raw_ostream;
class SDNode;
class SelectionDAG;
class Type;
class Value;

void checkForCycles(const SDNode *N, const SelectionDAG *DAG = nullptr,
                    bool force = false);

/// This represents a list of ValueType's that has been intern'd by
/// a SelectionDAG.  Instances of this simple value class are returned by
/// SelectionDAG::getVTList(...).
///
struct SDVTList {
  const EVT *VTs;
  unsigned int NumVTs;
};

namespace ISD {

  /// Node predicates

  /// If N is a BUILD_VECTOR node whose elements are all the same constant or
  /// undefined, return true and return the constant value in \p SplatValue.
  bool isConstantSplatVector(const SDNode *N, APInt &SplatValue);

  /// Return true if the specified node is a BUILD_VECTOR where all of the
  /// elements are ~0 or undef.
  bool isBuildVectorAllOnes(const SDNode *N);

  /// Return true if the specified node is a BUILD_VECTOR where all of the
  /// elements are 0 or undef.
  bool isBuildVectorAllZeros(const SDNode *N);

  /// Return true if the specified node is a BUILD_VECTOR node of all
  /// ConstantSDNode or undef.
  bool isBuildVectorOfConstantSDNodes(const SDNode *N);

  /// Return true if the specified node is a BUILD_VECTOR node of all
  /// ConstantFPSDNode or undef.
  bool isBuildVectorOfConstantFPSDNodes(const SDNode *N);

  /// Return true if the node has at least one operand and all operands of the
  /// specified node are ISD::UNDEF.
  bool allOperandsUndef(const SDNode *N);

} // end namespace ISD

//===----------------------------------------------------------------------===//
/// Unlike LLVM values, Selection DAG nodes may return multiple
/// values as the result of a computation.  Many nodes return multiple values,
/// from loads (which define a token and a return value) to ADDC (which returns
/// a result and a carry value), to calls (which may return an arbitrary number
/// of values).
///
/// As such, each use of a SelectionDAG computation must indicate the node that
/// computes it as well as which return value to use from that node.  This pair
/// of information is represented with the SDValue value type.
///
class SDValue {
  friend struct DenseMapInfo<SDValue>;

  SDNode *Node = nullptr; // The node defining the value we are using.
  unsigned ResNo = 0;     // Which return value of the node we are using.

public:
  SDValue() = default;
  SDValue(SDNode *node, unsigned resno);

  /// get the index which selects a specific result in the SDNode
  unsigned getResNo() const { return ResNo; }

  /// get the SDNode which holds the desired result
  SDNode *getNode() const { return Node; }

  /// set the SDNode
  void setNode(SDNode *N) { Node = N; }

  inline SDNode *operator->() const { return Node; }

  bool operator==(const SDValue &O) const {
    return Node == O.Node && ResNo == O.ResNo;
  }
  bool operator!=(const SDValue &O) const {
    return !operator==(O);
  }
  bool operator<(const SDValue &O) const {
    return std::tie(Node, ResNo) < std::tie(O.Node, O.ResNo);
  }
  explicit operator bool() const {
    return Node != nullptr;
  }

  SDValue getValue(unsigned R) const {
    return SDValue(Node, R);
  }

  /// Return true if this node is an operand of N.
  bool isOperandOf(const SDNode *N) const;

  /// Return the ValueType of the referenced return value.
  inline EVT getValueType() const;

  /// Return the simple ValueType of the referenced return value.
  MVT getSimpleValueType() const {
    return getValueType().getSimpleVT();
  }

  /// Returns the size of the value in bits.
  unsigned getValueSizeInBits() const {
    return getValueType().getSizeInBits();
  }

  unsigned getScalarValueSizeInBits() const {
    return getValueType().getScalarType().getSizeInBits();
  }

  // Forwarding methods - These forward to the corresponding methods in SDNode.
  inline unsigned getOpcode() const;
  inline unsigned getNumOperands() const;
  inline const SDValue &getOperand(unsigned i) const;
  inline uint64_t getConstantOperandVal(unsigned i) const;
  inline bool isTargetMemoryOpcode() const;
  inline bool isTargetOpcode() const;
  inline bool isMachineOpcode() const;
  inline bool isUndef() const;
  inline unsigned getMachineOpcode() const;
  inline const DebugLoc &getDebugLoc() const;
  inline void dump() const;
  inline void dump(const SelectionDAG *G) const;
  inline void dumpr() const;
  inline void dumpr(const SelectionDAG *G) const;

  /// Return true if this operand (which must be a chain) reaches the
  /// specified operand without crossing any side-effecting instructions.
  /// In practice, this looks through token factors and non-volatile loads.
  /// In order to remain efficient, this only
  /// looks a couple of nodes in, it does not do an exhaustive search.
  bool reachesChainWithoutSideEffects(SDValue Dest,
                                      unsigned Depth = 2) const;

  /// Return true if there are no nodes using value ResNo of Node.
  inline bool use_empty() const;

  /// Return true if there is exactly one node using value ResNo of Node.
  inline bool hasOneUse() const;
};

template<> struct DenseMapInfo<SDValue> {
  static inline SDValue getEmptyKey() {
    SDValue V;
    V.ResNo = -1U;
    return V;
  }

  static inline SDValue getTombstoneKey() {
    SDValue V;
    V.ResNo = -2U;
    return V;
  }

  static unsigned getHashValue(const SDValue &Val) {
    return ((unsigned)((uintptr_t)Val.getNode() >> 4) ^
            (unsigned)((uintptr_t)Val.getNode() >> 9)) + Val.getResNo();
  }

  static bool isEqual(const SDValue &LHS, const SDValue &RHS) {
    return LHS == RHS;
  }
};
template <> struct isPodLike<SDValue> { static const bool value = true; };

/// Allow casting operators to work directly on
/// SDValues as if they were SDNode*'s.
template<> struct simplify_type<SDValue> {
  using SimpleType = SDNode *;

  static SimpleType getSimplifiedValue(SDValue &Val) {
    return Val.getNode();
  }
};
template<> struct simplify_type<const SDValue> {
  using SimpleType = /*const*/ SDNode *;

  static SimpleType getSimplifiedValue(const SDValue &Val) {
    return Val.getNode();
  }
};

/// Represents a use of a SDNode. This class holds an SDValue,
/// which records the SDNode being used and the result number, a
/// pointer to the SDNode using the value, and Next and Prev pointers,
/// which link together all the uses of an SDNode.
///
class SDUse {
  /// Val - The value being used.
  SDValue Val;
  /// User - The user of this value.
  SDNode *User = nullptr;
  /// Prev, Next - Pointers to the uses list of the SDNode referred by
  /// this operand.
  SDUse **Prev = nullptr;
  SDUse *Next = nullptr;

public:
  SDUse() = default;
  SDUse(const SDUse &U) = delete;
  SDUse &operator=(const SDUse &) = delete;

  /// Normally SDUse will just implicitly convert to an SDValue that it holds.
  operator const SDValue&() const { return Val; }

  /// If implicit conversion to SDValue doesn't work, the get() method returns
  /// the SDValue.
  const SDValue &get() const { return Val; }

  /// This returns the SDNode that contains this Use.
  SDNode *getUser() { return User; }

  /// Get the next SDUse in the use list.
  SDUse *getNext() const { return Next; }

  /// Convenience function for get().getNode().
  SDNode *getNode() const { return Val.getNode(); }
  /// Convenience function for get().getResNo().
  unsigned getResNo() const { return Val.getResNo(); }
  /// Convenience function for get().getValueType().
  EVT getValueType() const { return Val.getValueType(); }

  /// Convenience function for get().operator==
  bool operator==(const SDValue &V) const {
    return Val == V;
  }

  /// Convenience function for get().operator!=
  bool operator!=(const SDValue &V) const {
    return Val != V;
  }

  /// Convenience function for get().operator<
  bool operator<(const SDValue &V) const {
    return Val < V;
  }

private:
  friend class SelectionDAG;
  friend class SDNode;
  // TODO: unfriend HandleSDNode once we fix its operand handling.
  friend class HandleSDNode;

  void setUser(SDNode *p) { User = p; }

  /// Remove this use from its existing use list, assign it the
  /// given value, and add it to the new value's node's use list.
  inline void set(const SDValue &V);
  /// Like set, but only supports initializing a newly-allocated
  /// SDUse with a non-null value.
  inline void setInitial(const SDValue &V);
  /// Like set, but only sets the Node portion of the value,
  /// leaving the ResNo portion unmodified.
  inline void setNode(SDNode *N);

  void addToList(SDUse **List) {
    Next = *List;
    if (Next) Next->Prev = &Next;
    Prev = List;
    *List = this;
  }

  void removeFromList() {
    *Prev = Next;
    if (Next) Next->Prev = Prev;
  }
};

/// simplify_type specializations - Allow casting operators to work directly on
/// SDValues as if they were SDNode*'s.
template<> struct simplify_type<SDUse> {
  using SimpleType = SDNode *;

  static SimpleType getSimplifiedValue(SDUse &Val) {
    return Val.getNode();
  }
};

/// These are IR-level optimization flags that may be propagated to SDNodes.
/// TODO: This data structure should be shared by the IR optimizer and the
/// the backend.
struct SDNodeFlags {
private:
  // This bit is used to determine if the flags are in a defined state.
  // Flag bits can only be masked out during intersection if the masking flags
  // are defined.
  bool AnyDefined : 1;

  bool NoUnsignedWrap : 1;
  bool NoSignedWrap : 1;
  bool Exact : 1;
  bool NoNaNs : 1;
  bool NoInfs : 1;
  bool NoSignedZeros : 1;
  bool AllowReciprocal : 1;
  bool VectorReduction : 1;
  bool AllowContract : 1;
  bool ApproximateFuncs : 1;
  bool AllowReassociation : 1;

public:
  /// Default constructor turns off all optimization flags.
  SDNodeFlags()
      : AnyDefined(false), NoUnsignedWrap(false), NoSignedWrap(false),
        Exact(false), NoNaNs(false), NoInfs(false),
        NoSignedZeros(false), AllowReciprocal(false), VectorReduction(false),
        AllowContract(false), ApproximateFuncs(false),
        AllowReassociation(false) {}

  /// Propagate the fast-math-flags from an IR FPMathOperator.
  void copyFMF(const FPMathOperator &FPMO) {
    setNoNaNs(FPMO.hasNoNaNs());
    setNoInfs(FPMO.hasNoInfs());
    setNoSignedZeros(FPMO.hasNoSignedZeros());
    setAllowReciprocal(FPMO.hasAllowReciprocal());
    setAllowContract(FPMO.hasAllowContract());
    setApproximateFuncs(FPMO.hasApproxFunc());
    setAllowReassociation(FPMO.hasAllowReassoc());
  }

  /// Sets the state of the flags to the defined state.
  void setDefined() { AnyDefined = true; }
  /// Returns true if the flags are in a defined state.
  bool isDefined() const { return AnyDefined; }

  // These are mutators for each flag.
  void setNoUnsignedWrap(bool b) {
    setDefined();
    NoUnsignedWrap = b;
  }
  void setNoSignedWrap(bool b) {
    setDefined();
    NoSignedWrap = b;
  }
  void setExact(bool b) {
    setDefined();
    Exact = b;
  }
  void setNoNaNs(bool b) {
    setDefined();
    NoNaNs = b;
  }
  void setNoInfs(bool b) {
    setDefined();
    NoInfs = b;
  }
  void setNoSignedZeros(bool b) {
    setDefined();
    NoSignedZeros = b;
  }
  void setAllowReciprocal(bool b) {
    setDefined();
    AllowReciprocal = b;
  }
  void setVectorReduction(bool b) {
    setDefined();
    VectorReduction = b;
  }
  void setAllowContract(bool b) {
    setDefined();
    AllowContract = b;
  }
  void setApproximateFuncs(bool b) {
    setDefined();
    ApproximateFuncs = b;
  }
  void setAllowReassociation(bool b) {
    setDefined();
    AllowReassociation = b;
  }

  // These are accessors for each flag.
  bool hasNoUnsignedWrap() const { return NoUnsignedWrap; }
  bool hasNoSignedWrap() const { return NoSignedWrap; }
  bool hasExact() const { return Exact; }
  bool hasNoNaNs() const { return NoNaNs; }
  bool hasNoInfs() const { return NoInfs; }
  bool hasNoSignedZeros() const { return NoSignedZeros; }
  bool hasAllowReciprocal() const { return AllowReciprocal; }
  bool hasVectorReduction() const { return VectorReduction; }
  bool hasAllowContract() const { return AllowContract; }
  bool hasApproximateFuncs() const { return ApproximateFuncs; }
  bool hasAllowReassociation() const { return AllowReassociation; }

  bool isFast() const {
    return NoSignedZeros && AllowReciprocal && NoNaNs && NoInfs &&
           AllowContract && ApproximateFuncs && AllowReassociation;
  }

  /// Clear any flags in this flag set that aren't also set in Flags.
  /// If the given Flags are undefined then don't do anything.
  void intersectWith(const SDNodeFlags Flags) {
    if (!Flags.isDefined())
      return;
    NoUnsignedWrap &= Flags.NoUnsignedWrap;
    NoSignedWrap &= Flags.NoSignedWrap;
    Exact &= Flags.Exact;
    NoNaNs &= Flags.NoNaNs;
    NoInfs &= Flags.NoInfs;
    NoSignedZeros &= Flags.NoSignedZeros;
    AllowReciprocal &= Flags.AllowReciprocal;
    VectorReduction &= Flags.VectorReduction;
    AllowContract &= Flags.AllowContract;
    ApproximateFuncs &= Flags.ApproximateFuncs;
    AllowReassociation &= Flags.AllowReassociation;
  }
};

/// Represents one node in the SelectionDAG.
///
class SDNode : public FoldingSetNode, public ilist_node<SDNode> {
private:
  /// The operation that this node performs.
  int16_t NodeType;

protected:
  // We define a set of mini-helper classes to help us interpret the bits in our
  // SubclassData.  These are designed to fit within a uint16_t so they pack
  // with NodeType.

  class SDNodeBitfields {
    friend class SDNode;
    friend class MemIntrinsicSDNode;
    friend class MemSDNode;
    friend class SelectionDAG;

    uint16_t HasDebugValue : 1;
    uint16_t IsMemIntrinsic : 1;
    uint16_t IsDivergent : 1;
  };
  enum { NumSDNodeBits = 3 };

  class ConstantSDNodeBitfields {
    friend class ConstantSDNode;

    uint16_t : NumSDNodeBits;

    uint16_t IsOpaque : 1;
  };

  class MemSDNodeBitfields {
    friend class MemSDNode;
    friend class MemIntrinsicSDNode;
    friend class AtomicSDNode;

    uint16_t : NumSDNodeBits;

    uint16_t IsVolatile : 1;
    uint16_t IsNonTemporal : 1;
    uint16_t IsDereferenceable : 1;
    uint16_t IsInvariant : 1;
  };
  enum { NumMemSDNodeBits = NumSDNodeBits + 4 };

  class LSBaseSDNodeBitfields {
    friend class LSBaseSDNode;

    uint16_t : NumMemSDNodeBits;

    uint16_t AddressingMode : 3; // enum ISD::MemIndexedMode
  };
  enum { NumLSBaseSDNodeBits = NumMemSDNodeBits + 3 };

  class LoadSDNodeBitfields {
    friend class LoadSDNode;
    friend class MaskedLoadSDNode;

    uint16_t : NumLSBaseSDNodeBits;

    uint16_t ExtTy : 2; // enum ISD::LoadExtType
    uint16_t IsExpanding : 1;
  };

  class StoreSDNodeBitfields {
    friend class StoreSDNode;
    friend class MaskedStoreSDNode;

    uint16_t : NumLSBaseSDNodeBits;

    uint16_t IsTruncating : 1;
    uint16_t IsCompressing : 1;
  };

  union {
    char RawSDNodeBits[sizeof(uint16_t)];
    SDNodeBitfields SDNodeBits;
    ConstantSDNodeBitfields ConstantSDNodeBits;
    MemSDNodeBitfields MemSDNodeBits;
    LSBaseSDNodeBitfields LSBaseSDNodeBits;
    LoadSDNodeBitfields LoadSDNodeBits;
    StoreSDNodeBitfields StoreSDNodeBits;
  };

  // RawSDNodeBits must cover the entirety of the union.  This means that all of
  // the union's members must have size <= RawSDNodeBits.  We write the RHS as
  // "2" instead of sizeof(RawSDNodeBits) because MSVC can't handle the latter.
  static_assert(sizeof(SDNodeBitfields) <= 2, "field too wide");
  static_assert(sizeof(ConstantSDNodeBitfields) <= 2, "field too wide");
  static_assert(sizeof(MemSDNodeBitfields) <= 2, "field too wide");
  static_assert(sizeof(LSBaseSDNodeBitfields) <= 2, "field too wide");
  static_assert(sizeof(LoadSDNodeBitfields) <= 2, "field too wide");
  static_assert(sizeof(StoreSDNodeBitfields) <= 2, "field too wide");

private:
  friend class SelectionDAG;
  // TODO: unfriend HandleSDNode once we fix its operand handling.
  friend class HandleSDNode;

  /// Unique id per SDNode in the DAG.
  int NodeId = -1;

  /// The values that are used by this operation.
  SDUse *OperandList = nullptr;

  /// The types of the values this node defines.  SDNode's may
  /// define multiple values simultaneously.
  const EVT *ValueList;

  /// List of uses for this SDNode.
  SDUse *UseList = nullptr;

  /// The number of entries in the Operand/Value list.
  unsigned short NumOperands = 0;
  unsigned short NumValues;

  // The ordering of the SDNodes. It roughly corresponds to the ordering of the
  // original LLVM instructions.
  // This is used for turning off scheduling, because we'll forgo
  // the normal scheduling algorithms and output the instructions according to
  // this ordering.
  unsigned IROrder;

  /// Source line information.
  DebugLoc debugLoc;

  /// Return a pointer to the specified value type.
  static const EVT *getValueTypeList(EVT VT);

  SDNodeFlags Flags;

public:
  /// Unique and persistent id per SDNode in the DAG.
  /// Used for debug printing.
  uint16_t PersistentId;

  //===--------------------------------------------------------------------===//
  //  Accessors
  //

  /// Return the SelectionDAG opcode value for this node. For
  /// pre-isel nodes (those for which isMachineOpcode returns false), these
  /// are the opcode values in the ISD and <target>ISD namespaces. For
  /// post-isel opcodes, see getMachineOpcode.
  unsigned getOpcode()  const { return (unsigned short)NodeType; }

  /// Test if this node has a target-specific opcode (in the
  /// \<target\>ISD namespace).
  bool isTargetOpcode() const { return NodeType >= ISD::BUILTIN_OP_END; }

  /// Test if this node has a target-specific
  /// memory-referencing opcode (in the \<target\>ISD namespace and
  /// greater than FIRST_TARGET_MEMORY_OPCODE).
  bool isTargetMemoryOpcode() const {
    return NodeType >= ISD::FIRST_TARGET_MEMORY_OPCODE;
  }

  /// Return true if the type of the node type undefined.
  bool isUndef() const { return NodeType == ISD::UNDEF; }

  /// Test if this node is a memory intrinsic (with valid pointer information).
  /// INTRINSIC_W_CHAIN and INTRINSIC_VOID nodes are sometimes created for
  /// non-memory intrinsics (with chains) that are not really instances of
  /// MemSDNode. For such nodes, we need some extra state to determine the
  /// proper classof relationship.
  bool isMemIntrinsic() const {
    return (NodeType == ISD::INTRINSIC_W_CHAIN ||
            NodeType == ISD::INTRINSIC_VOID) &&
           SDNodeBits.IsMemIntrinsic;
  }

  /// Test if this node is a strict floating point pseudo-op.
  bool isStrictFPOpcode() {
    switch (NodeType) {
      default:
        return false;
      case ISD::STRICT_FADD:
      case ISD::STRICT_FSUB:
      case ISD::STRICT_FMUL:
      case ISD::STRICT_FDIV:
      case ISD::STRICT_FREM:
      case ISD::STRICT_FMA:
      case ISD::STRICT_FSQRT:
      case ISD::STRICT_FPOW:
      case ISD::STRICT_FPOWI:
      case ISD::STRICT_FSIN:
      case ISD::STRICT_FCOS:
      case ISD::STRICT_FEXP:
      case ISD::STRICT_FEXP2:
      case ISD::STRICT_FLOG:
      case ISD::STRICT_FLOG10:
      case ISD::STRICT_FLOG2:
      case ISD::STRICT_FRINT:
      case ISD::STRICT_FNEARBYINT:
      case ISD::STRICT_FMAXNUM:
      case ISD::STRICT_FMINNUM:
      case ISD::STRICT_FCEIL:
      case ISD::STRICT_FFLOOR:
      case ISD::STRICT_FROUND:
      case ISD::STRICT_FTRUNC:
        return true;
    }
  }

  /// Test if this node has a post-isel opcode, directly
  /// corresponding to a MachineInstr opcode.
  bool isMachineOpcode() const { return NodeType < 0; }

  /// This may only be called if isMachineOpcode returns
  /// true. It returns the MachineInstr opcode value that the node's opcode
  /// corresponds to.
  unsigned getMachineOpcode() const {
    assert(isMachineOpcode() && "Not a MachineInstr opcode!");
    return ~NodeType;
  }

  bool getHasDebugValue() const { return SDNodeBits.HasDebugValue; }
  void setHasDebugValue(bool b) { SDNodeBits.HasDebugValue = b; }

  bool isDivergent() const { return SDNodeBits.IsDivergent; }

  /// Return true if there are no uses of this node.
  bool use_empty() const { return UseList == nullptr; }

  /// Return true if there is exactly one use of this node.
  bool hasOneUse() const {
    return !use_empty() && std::next(use_begin()) == use_end();
  }

  /// Return the number of uses of this node. This method takes
  /// time proportional to the number of uses.
  size_t use_size() const { return std::distance(use_begin(), use_end()); }

  /// Return the unique node id.
  int getNodeId() const { return NodeId; }

  /// Set unique node id.
  void setNodeId(int Id) { NodeId = Id; }

  /// Return the node ordering.
  unsigned getIROrder() const { return IROrder; }

  /// Set the node ordering.
  void setIROrder(unsigned Order) { IROrder = Order; }

  /// Return the source location info.
  const DebugLoc &getDebugLoc() const { return debugLoc; }

  /// Set source location info.  Try to avoid this, putting
  /// it in the constructor is preferable.
  void setDebugLoc(DebugLoc dl) { debugLoc = std::move(dl); }

  /// This class provides iterator support for SDUse
  /// operands that use a specific SDNode.
  class use_iterator
    : public std::iterator<std::forward_iterator_tag, SDUse, ptrdiff_t> {
    friend class SDNode;

    SDUse *Op = nullptr;

    explicit use_iterator(SDUse *op) : Op(op) {}

  public:
    using reference = std::iterator<std::forward_iterator_tag,
                                    SDUse, ptrdiff_t>::reference;
    using pointer = std::iterator<std::forward_iterator_tag,
                                  SDUse, ptrdiff_t>::pointer;

    use_iterator() = default;
    use_iterator(const use_iterator &I) : Op(I.Op) {}

    bool operator==(const use_iterator &x) const {
      return Op == x.Op;
    }
    bool operator!=(const use_iterator &x) const {
      return !operator==(x);
    }

    /// Return true if this iterator is at the end of uses list.
    bool atEnd() const { return Op == nullptr; }

    // Iterator traversal: forward iteration only.
    use_iterator &operator++() {          // Preincrement
      assert(Op && "Cannot increment end iterator!");
      Op = Op->getNext();
      return *this;
    }

    use_iterator operator++(int) {        // Postincrement
      use_iterator tmp = *this; ++*this; return tmp;
    }

    /// Retrieve a pointer to the current user node.
    SDNode *operator*() const {
      assert(Op && "Cannot dereference end iterator!");
      return Op->getUser();
    }

    SDNode *operator->() const { return operator*(); }

    SDUse &getUse() const { return *Op; }

    /// Retrieve the operand # of this use in its user.
    unsigned getOperandNo() const {
      assert(Op && "Cannot dereference end iterator!");
      return (unsigned)(Op - Op->getUser()->OperandList);
    }
  };

  /// Provide iteration support to walk over all uses of an SDNode.
  use_iterator use_begin() const {
    return use_iterator(UseList);
  }

  static use_iterator use_end() { return use_iterator(nullptr); }

  inline iterator_range<use_iterator> uses() {
    return make_range(use_begin(), use_end());
  }
  inline iterator_range<use_iterator> uses() const {
    return make_range(use_begin(), use_end());
  }

  /// Return true if there are exactly NUSES uses of the indicated value.
  /// This method ignores uses of other values defined by this operation.
  bool hasNUsesOfValue(unsigned NUses, unsigned Value) const;

  /// Return true if there are any use of the indicated value.
  /// This method ignores uses of other values defined by this operation.
  bool hasAnyUseOfValue(unsigned Value) const;

  /// Return true if this node is the only use of N.
  bool isOnlyUserOf(const SDNode *N) const;

  /// Return true if this node is an operand of N.
  bool isOperandOf(const SDNode *N) const;

  /// Return true if this node is a predecessor of N.
  /// NOTE: Implemented on top of hasPredecessor and every bit as
  /// expensive. Use carefully.
  bool isPredecessorOf(const SDNode *N) const {
    return N->hasPredecessor(this);
  }

  /// Return true if N is a predecessor of this node.
  /// N is either an operand of this node, or can be reached by recursively
  /// traversing up the operands.
  /// NOTE: This is an expensive method. Use it carefully.
  bool hasPredecessor(const SDNode *N) const;

  /// Returns true if N is a predecessor of any node in Worklist. This
  /// helper keeps Visited and Worklist sets externally to allow unions
  /// searches to be performed in parallel, caching of results across
  /// queries and incremental addition to Worklist. Stops early if N is
  /// found but will resume. Remember to clear Visited and Worklists
  /// if DAG changes. MaxSteps gives a maximum number of nodes to visit before
  /// giving up. The TopologicalPrune flag signals that positive NodeIds are
  /// topologically ordered (Operands have strictly smaller node id) and search
  /// can be pruned leveraging this.
  static bool hasPredecessorHelper(const SDNode *N,
                                   SmallPtrSetImpl<const SDNode *> &Visited,
                                   SmallVectorImpl<const SDNode *> &Worklist,
                                   unsigned int MaxSteps = 0,
                                   bool TopologicalPrune = false) {
    SmallVector<const SDNode *, 8> DeferredNodes;
    if (Visited.count(N))
      return true;

    // Node Id's are assigned in three places: As a topological
    // ordering (> 0), during legalization (results in values set to
    // 0), new nodes (set to -1). If N has a topolgical id then we
    // know that all nodes with ids smaller than it cannot be
    // successors and we need not check them. Filter out all node
    // that can't be matches. We add them to the worklist before exit
    // in case of multiple calls. Note that during selection the topological id
    // may be violated if a node's predecessor is selected before it. We mark
    // this at selection negating the id of unselected successors and
    // restricting topological pruning to positive ids.

    int NId = N->getNodeId();
    // If we Invalidated the Id, reconstruct original NId.
    if (NId < -1)
      NId = -(NId + 1);

    bool Found = false;
    while (!Worklist.empty()) {
      const SDNode *M = Worklist.pop_back_val();
      int MId = M->getNodeId();
      if (TopologicalPrune && M->getOpcode() != ISD::TokenFactor && (NId > 0) &&
          (MId > 0) && (MId < NId)) {
        DeferredNodes.push_back(M);
        continue;
      }
      for (const SDValue &OpV : M->op_values()) {
        SDNode *Op = OpV.getNode();
        if (Visited.insert(Op).second)
          Worklist.push_back(Op);
        if (Op == N)
          Found = true;
      }
      if (Found)
        break;
      if (MaxSteps != 0 && Visited.size() >= MaxSteps)
        break;
    }
    // Push deferred nodes back on worklist.
    Worklist.append(DeferredNodes.begin(), DeferredNodes.end());
    // If we bailed early, conservatively return found.
    if (MaxSteps != 0 && Visited.size() >= MaxSteps)
      return true;
    return Found;
  }

  /// Return true if all the users of N are contained in Nodes.
  /// NOTE: Requires at least one match, but doesn't require them all.
  static bool areOnlyUsersOf(ArrayRef<const SDNode *> Nodes, const SDNode *N);

  /// Return the number of values used by this operation.
  unsigned getNumOperands() const { return NumOperands; }

  /// Helper method returns the integer value of a ConstantSDNode operand.
  inline uint64_t getConstantOperandVal(unsigned Num) const;

  const SDValue &getOperand(unsigned Num) const {
    assert(Num < NumOperands && "Invalid child # of SDNode!");
    return OperandList[Num];
  }

  using op_iterator = SDUse *;

  op_iterator op_begin() const { return OperandList; }
  op_iterator op_end() const { return OperandList+NumOperands; }
  ArrayRef<SDUse> ops() const { return makeArrayRef(op_begin(), op_end()); }

  /// Iterator for directly iterating over the operand SDValue's.
  struct value_op_iterator
      : iterator_adaptor_base<value_op_iterator, op_iterator,
                              std::random_access_iterator_tag, SDValue,
                              ptrdiff_t, value_op_iterator *,
                              value_op_iterator *> {
    explicit value_op_iterator(SDUse *U = nullptr)
      : iterator_adaptor_base(U) {}

    const SDValue &operator*() const { return I->get(); }
  };

  iterator_range<value_op_iterator> op_values() const {
    return make_range(value_op_iterator(op_begin()),
                      value_op_iterator(op_end()));
  }

  SDVTList getVTList() const {
    SDVTList X = { ValueList, NumValues };
    return X;
  }

  /// If this node has a glue operand, return the node
  /// to which the glue operand points. Otherwise return NULL.
  SDNode *getGluedNode() const {
    if (getNumOperands() != 0 &&
        getOperand(getNumOperands()-1).getValueType() == MVT::Glue)
      return getOperand(getNumOperands()-1).getNode();
    return nullptr;
  }

  /// If this node has a glue value with a user, return
  /// the user (there is at most one). Otherwise return NULL.
  SDNode *getGluedUser() const {
    for (use_iterator UI = use_begin(), UE = use_end(); UI != UE; ++UI)
      if (UI.getUse().get().getValueType() == MVT::Glue)
        return *UI;
    return nullptr;
  }

  const SDNodeFlags getFlags() const { return Flags; }
  void setFlags(SDNodeFlags NewFlags) { Flags = NewFlags; }
  bool isFast() { return Flags.isFast(); }

  /// Clear any flags in this node that aren't also set in Flags.
  /// If Flags is not in a defined state then this has no effect.
  void intersectFlagsWith(const SDNodeFlags Flags);

  /// Return the number of values defined/returned by this operator.
  unsigned getNumValues() const { return NumValues; }

  /// Return the type of a specified result.
  EVT getValueType(unsigned ResNo) const {
    assert(ResNo < NumValues && "Illegal result number!");
    return ValueList[ResNo];
  }

  /// Return the type of a specified result as a simple type.
  MVT getSimpleValueType(unsigned ResNo) const {
    return getValueType(ResNo).getSimpleVT();
  }

  /// Returns MVT::getSizeInBits(getValueType(ResNo)).
  unsigned getValueSizeInBits(unsigned ResNo) const {
    return getValueType(ResNo).getSizeInBits();
  }

  using value_iterator = const EVT *;

  value_iterator value_begin() const { return ValueList; }
  value_iterator value_end() const { return ValueList+NumValues; }

  /// Return the opcode of this operation for printing.
  std::string getOperationName(const SelectionDAG *G = nullptr) const;
  static const char* getIndexedModeName(ISD::MemIndexedMode AM);
  void print_types(raw_ostream &OS, const SelectionDAG *G) const;
  void print_details(raw_ostream &OS, const SelectionDAG *G) const;
  void print(raw_ostream &OS, const SelectionDAG *G = nullptr) const;
  void printr(raw_ostream &OS, const SelectionDAG *G = nullptr) const;

  /// Print a SelectionDAG node and all children down to
  /// the leaves.  The given SelectionDAG allows target-specific nodes
  /// to be printed in human-readable form.  Unlike printr, this will
  /// print the whole DAG, including children that appear multiple
  /// times.
  ///
  void printrFull(raw_ostream &O, const SelectionDAG *G = nullptr) const;

  /// Print a SelectionDAG node and children up to
  /// depth "depth."  The given SelectionDAG allows target-specific
  /// nodes to be printed in human-readable form.  Unlike printr, this
  /// will print children that appear multiple times wherever they are
  /// used.
  ///
  void printrWithDepth(raw_ostream &O, const SelectionDAG *G = nullptr,
                       unsigned depth = 100) const;

  /// Dump this node, for debugging.
  void dump() const;

  /// Dump (recursively) this node and its use-def subgraph.
  void dumpr() const;

  /// Dump this node, for debugging.
  /// The given SelectionDAG allows target-specific nodes to be printed
  /// in human-readable form.
  void dump(const SelectionDAG *G) const;

  /// Dump (recursively) this node and its use-def subgraph.
  /// The given SelectionDAG allows target-specific nodes to be printed
  /// in human-readable form.
  void dumpr(const SelectionDAG *G) const;

  /// printrFull to dbgs().  The given SelectionDAG allows
  /// target-specific nodes to be printed in human-readable form.
  /// Unlike dumpr, this will print the whole DAG, including children
  /// that appear multiple times.
  void dumprFull(const SelectionDAG *G = nullptr) const;

  /// printrWithDepth to dbgs().  The given
  /// SelectionDAG allows target-specific nodes to be printed in
  /// human-readable form.  Unlike dumpr, this will print children
  /// that appear multiple times wherever they are used.
  ///
  void dumprWithDepth(const SelectionDAG *G = nullptr,
                      unsigned depth = 100) const;

  /// Gather unique data for the node.
  void Profile(FoldingSetNodeID &ID) const;

  /// This method should only be used by the SDUse class.
  void addUse(SDUse &U) { U.addToList(&UseList); }

protected:
  static SDVTList getSDVTList(EVT VT) {
    SDVTList Ret = { getValueTypeList(VT), 1 };
    return Ret;
  }

  /// Create an SDNode.
  ///
  /// SDNodes are created without any operands, and never own the operand
  /// storage. To add operands, see SelectionDAG::createOperands.
  SDNode(unsigned Opc, unsigned Order, DebugLoc dl, SDVTList VTs)
      : NodeType(Opc), ValueList(VTs.VTs), NumValues(VTs.NumVTs),
        IROrder(Order), debugLoc(std::move(dl)) {
    memset(&RawSDNodeBits, 0, sizeof(RawSDNodeBits));
    assert(debugLoc.hasTrivialDestructor() && "Expected trivial destructor");
    assert(NumValues == VTs.NumVTs &&
           "NumValues wasn't wide enough for its operands!");
  }

  /// Release the operands and set this node to have zero operands.
  void DropOperands();
};

/// Wrapper class for IR location info (IR ordering and DebugLoc) to be passed
/// into SDNode creation functions.
/// When an SDNode is created from the DAGBuilder, the DebugLoc is extracted
/// from the original Instruction, and IROrder is the ordinal position of
/// the instruction.
/// When an SDNode is created after the DAG is being built, both DebugLoc and
/// the IROrder are propagated from the original SDNode.
/// So SDLoc class provides two constructors besides the default one, one to
/// be used by the DAGBuilder, the other to be used by others.
class SDLoc {
private:
  DebugLoc DL;
  int IROrder = 0;

public:
  SDLoc() = default;
  SDLoc(const SDNode *N) : DL(N->getDebugLoc()), IROrder(N->getIROrder()) {}
  SDLoc(const SDValue V) : SDLoc(V.getNode()) {}
  SDLoc(const Instruction *I, int Order) : IROrder(Order) {
    assert(Order >= 0 && "bad IROrder");
    if (I)
      DL = I->getDebugLoc();
  }

  unsigned getIROrder() const { return IROrder; }
  const DebugLoc &getDebugLoc() const { return DL; }
};

// Define inline functions from the SDValue class.

inline SDValue::SDValue(SDNode *node, unsigned resno)
    : Node(node), ResNo(resno) {
  // Explicitly check for !ResNo to avoid use-after-free, because there are
  // callers that use SDValue(N, 0) with a deleted N to indicate successful
  // combines.
  assert((!Node || !ResNo || ResNo < Node->getNumValues()) &&
         "Invalid result number for the given node!");
  assert(ResNo < -2U && "Cannot use result numbers reserved for DenseMaps.");
}

inline unsigned SDValue::getOpcode() const {
  return Node->getOpcode();
}

inline EVT SDValue::getValueType() const {
  return Node->getValueType(ResNo);
}

inline unsigned SDValue::getNumOperands() const {
  return Node->getNumOperands();
}

inline const SDValue &SDValue::getOperand(unsigned i) const {
  return Node->getOperand(i);
}

inline uint64_t SDValue::getConstantOperandVal(unsigned i) const {
  return Node->getConstantOperandVal(i);
}

inline bool SDValue::isTargetOpcode() const {
  return Node->isTargetOpcode();
}

inline bool SDValue::isTargetMemoryOpcode() const {
  return Node->isTargetMemoryOpcode();
}

inline bool SDValue::isMachineOpcode() const {
  return Node->isMachineOpcode();
}

inline unsigned SDValue::getMachineOpcode() const {
  return Node->getMachineOpcode();
}

inline bool SDValue::isUndef() const {
  return Node->isUndef();
}

inline bool SDValue::use_empty() const {
  return !Node->hasAnyUseOfValue(ResNo);
}

inline bool SDValue::hasOneUse() const {
  return Node->hasNUsesOfValue(1, ResNo);
}

inline const DebugLoc &SDValue::getDebugLoc() const {
  return Node->getDebugLoc();
}

inline void SDValue::dump() const {
  return Node->dump();
}

inline void SDValue::dump(const SelectionDAG *G) const {
  return Node->dump(G);
}

inline void SDValue::dumpr() const {
  return Node->dumpr();
}

inline void SDValue::dumpr(const SelectionDAG *G) const {
  return Node->dumpr(G);
}

// Define inline functions from the SDUse class.

inline void SDUse::set(const SDValue &V) {
  if (Val.getNode()) removeFromList();
  Val = V;
  if (V.getNode()) V.getNode()->addUse(*this);
}

inline void SDUse::setInitial(const SDValue &V) {
  Val = V;
  V.getNode()->addUse(*this);
}

inline void SDUse::setNode(SDNode *N) {
  if (Val.getNode()) removeFromList();
  Val.setNode(N);
  if (N) N->addUse(*this);
}

/// This class is used to form a handle around another node that
/// is persistent and is updated across invocations of replaceAllUsesWith on its
/// operand.  This node should be directly created by end-users and not added to
/// the AllNodes list.
class HandleSDNode : public SDNode {
  SDUse Op;

public:
  explicit HandleSDNode(SDValue X)
    : SDNode(ISD::HANDLENODE, 0, DebugLoc(), getSDVTList(MVT::Other)) {
    // HandleSDNodes are never inserted into the DAG, so they won't be
    // auto-numbered. Use ID 65535 as a sentinel.
    PersistentId = 0xffff;

    // Manually set up the operand list. This node type is special in that it's
    // always stack allocated and SelectionDAG does not manage its operands.
    // TODO: This should either (a) not be in the SDNode hierarchy, or (b) not
    // be so special.
    Op.setUser(this);
    Op.setInitial(X);
    NumOperands = 1;
    OperandList = &Op;
  }
  ~HandleSDNode();

  const SDValue &getValue() const { return Op; }
};

class AddrSpaceCastSDNode : public SDNode {
private:
  unsigned SrcAddrSpace;
  unsigned DestAddrSpace;

public:
  AddrSpaceCastSDNode(unsigned Order, const DebugLoc &dl, EVT VT,
                      unsigned SrcAS, unsigned DestAS);

  unsigned getSrcAddressSpace() const { return SrcAddrSpace; }
  unsigned getDestAddressSpace() const { return DestAddrSpace; }

  static bool classof(const SDNode *N) {
    return N->getOpcode() == ISD::ADDRSPACECAST;
  }
};

/// This is an abstract virtual class for memory operations.
class MemSDNode : public SDNode {
private:
  // VT of in-memory value.
  EVT MemoryVT;

protected:
  /// Memory reference information.
  MachineMemOperand *MMO;

public:
  MemSDNode(unsigned Opc, unsigned Order, const DebugLoc &dl, SDVTList VTs,
            EVT memvt, MachineMemOperand *MMO);

  bool readMem() const { return MMO->isLoad(); }
  bool writeMem() const { return MMO->isStore(); }

  /// Returns alignment and volatility of the memory access
  unsigned getOriginalAlignment() const {
    return MMO->getBaseAlignment();
  }
  unsigned getAlignment() const {
    return MMO->getAlignment();
  }

  /// Return the SubclassData value, without HasDebugValue. This contains an
  /// encoding of the volatile flag, as well as bits used by subclasses. This
  /// function should only be used to compute a FoldingSetNodeID value.
  /// The HasDebugValue bit is masked out because CSE map needs to match
  /// nodes with debug info with nodes without debug info. Same is about
  /// isDivergent bit.
  unsigned getRawSubclassData() const {
    uint16_t Data;
    union {
      char RawSDNodeBits[sizeof(uint16_t)];
      SDNodeBitfields SDNodeBits;
    };
    memcpy(&RawSDNodeBits, &this->RawSDNodeBits, sizeof(this->RawSDNodeBits));
    SDNodeBits.HasDebugValue = 0;
    SDNodeBits.IsDivergent = false;
    memcpy(&Data, &RawSDNodeBits, sizeof(RawSDNodeBits));
    return Data;
  }

  bool isVolatile() const { return MemSDNodeBits.IsVolatile; }
  bool isNonTemporal() const { return MemSDNodeBits.IsNonTemporal; }
  bool isDereferenceable() const { return MemSDNodeBits.IsDereferenceable; }
  bool isInvariant() const { return MemSDNodeBits.IsInvariant; }

  // Returns the offset from the location of the access.
  int64_t getSrcValueOffset() const { return MMO->getOffset(); }

  /// Returns the AA info that describes the dereference.
  AAMDNodes getAAInfo() const { return MMO->getAAInfo(); }

  /// Returns the Ranges that describes the dereference.
  const MDNode *getRanges() const { return MMO->getRanges(); }

  /// Returns the synchronization scope ID for this memory operation.
  SyncScope::ID getSyncScopeID() const { return MMO->getSyncScopeID(); }

  /// Return the atomic ordering requirements for this memory operation. For
  /// cmpxchg atomic operations, return the atomic ordering requirements when
  /// store occurs.
  AtomicOrdering getOrdering() const { return MMO->getOrdering(); }

  /// Return the type of the in-memory value.
  EVT getMemoryVT() const { return MemoryVT; }

  /// Return a MachineMemOperand object describing the memory
  /// reference performed by operation.
  MachineMemOperand *getMemOperand() const { return MMO; }

  const MachinePointerInfo &getPointerInfo() const {
    return MMO->getPointerInfo();
  }

  /// Return the address space for the associated pointer
  unsigned getAddressSpace() const {
    return getPointerInfo().getAddrSpace();
  }

  /// Update this MemSDNode's MachineMemOperand information
  /// to reflect the alignment of NewMMO, if it has a greater alignment.
  /// This must only be used when the new alignment applies to all users of
  /// this MachineMemOperand.
  void refineAlignment(const MachineMemOperand *NewMMO) {
    MMO->refineAlignment(NewMMO);
  }

  const SDValue &getChain() const { return getOperand(0); }
  const SDValue &getBasePtr() const {
    return getOperand(getOpcode() == ISD::STORE ? 2 : 1);
  }

  // Methods to support isa and dyn_cast
  static bool classof(const SDNode *N) {
    // For some targets, we lower some target intrinsics to a MemIntrinsicNode
    // with either an intrinsic or a target opcode.
    return N->getOpcode() == ISD::LOAD                ||
           N->getOpcode() == ISD::STORE               ||
           N->getOpcode() == ISD::PREFETCH            ||
           N->getOpcode() == ISD::ATOMIC_CMP_SWAP     ||
           N->getOpcode() == ISD::ATOMIC_CMP_SWAP_WITH_SUCCESS ||
           N->getOpcode() == ISD::ATOMIC_SWAP         ||
           N->getOpcode() == ISD::ATOMIC_LOAD_ADD     ||
           N->getOpcode() == ISD::ATOMIC_LOAD_SUB     ||
           N->getOpcode() == ISD::ATOMIC_LOAD_AND     ||
           N->getOpcode() == ISD::ATOMIC_LOAD_CLR     ||
           N->getOpcode() == ISD::ATOMIC_LOAD_OR      ||
           N->getOpcode() == ISD::ATOMIC_LOAD_XOR     ||
           N->getOpcode() == ISD::ATOMIC_LOAD_NAND    ||
           N->getOpcode() == ISD::ATOMIC_LOAD_MIN     ||
           N->getOpcode() == ISD::ATOMIC_LOAD_MAX     ||
           N->getOpcode() == ISD::ATOMIC_LOAD_UMIN    ||
           N->getOpcode() == ISD::ATOMIC_LOAD_UMAX    ||
           N->getOpcode() == ISD::ATOMIC_LOAD         ||
           N->getOpcode() == ISD::ATOMIC_STORE        ||
           N->getOpcode() == ISD::MLOAD               ||
           N->getOpcode() == ISD::MSTORE              ||
           N->getOpcode() == ISD::MGATHER             ||
           N->getOpcode() == ISD::MSCATTER            ||
           N->isMemIntrinsic()                        ||
           N->isTargetMemoryOpcode();
  }
};

/// This is an SDNode representing atomic operations.
class AtomicSDNode : public MemSDNode {
public:
  AtomicSDNode(unsigned Opc, unsigned Order, const DebugLoc &dl, SDVTList VTL,
               EVT MemVT, MachineMemOperand *MMO)
      : MemSDNode(Opc, Order, dl, VTL, MemVT, MMO) {}

  const SDValue &getBasePtr() const { return getOperand(1); }
  const SDValue &getVal() const { return getOperand(2); }

  /// Returns true if this SDNode represents cmpxchg atomic operation, false
  /// otherwise.
  bool isCompareAndSwap() const {
    unsigned Op = getOpcode();
    return Op == ISD::ATOMIC_CMP_SWAP ||
           Op == ISD::ATOMIC_CMP_SWAP_WITH_SUCCESS;
  }

  /// For cmpxchg atomic operations, return the atomic ordering requirements
  /// when store does not occur.
  AtomicOrdering getFailureOrdering() const {
    assert(isCompareAndSwap() && "Must be cmpxchg operation");
    return MMO->getFailureOrdering();
  }

  // Methods to support isa and dyn_cast
  static bool classof(const SDNode *N) {
    return N->getOpcode() == ISD::ATOMIC_CMP_SWAP     ||
           N->getOpcode() == ISD::ATOMIC_CMP_SWAP_WITH_SUCCESS ||
           N->getOpcode() == ISD::ATOMIC_SWAP         ||
           N->getOpcode() == ISD::ATOMIC_LOAD_ADD     ||
           N->getOpcode() == ISD::ATOMIC_LOAD_SUB     ||
           N->getOpcode() == ISD::ATOMIC_LOAD_AND     ||
           N->getOpcode() == ISD::ATOMIC_LOAD_CLR     ||
           N->getOpcode() == ISD::ATOMIC_LOAD_OR      ||
           N->getOpcode() == ISD::ATOMIC_LOAD_XOR     ||
           N->getOpcode() == ISD::ATOMIC_LOAD_NAND    ||
           N->getOpcode() == ISD::ATOMIC_LOAD_MIN     ||
           N->getOpcode() == ISD::ATOMIC_LOAD_MAX     ||
           N->getOpcode() == ISD::ATOMIC_LOAD_UMIN    ||
           N->getOpcode() == ISD::ATOMIC_LOAD_UMAX    ||
           N->getOpcode() == ISD::ATOMIC_LOAD         ||
           N->getOpcode() == ISD::ATOMIC_STORE;
  }
};

/// This SDNode is used for target intrinsics that touch
/// memory and need an associated MachineMemOperand. Its opcode may be
/// INTRINSIC_VOID, INTRINSIC_W_CHAIN, PREFETCH, or a target-specific opcode
/// with a value not less than FIRST_TARGET_MEMORY_OPCODE.
class MemIntrinsicSDNode : public MemSDNode {
public:
  MemIntrinsicSDNode(unsigned Opc, unsigned Order, const DebugLoc &dl,
                     SDVTList VTs, EVT MemoryVT, MachineMemOperand *MMO)
      : MemSDNode(Opc, Order, dl, VTs, MemoryVT, MMO) {
    SDNodeBits.IsMemIntrinsic = true;
  }

  // Methods to support isa and dyn_cast
  static bool classof(const SDNode *N) {
    // We lower some target intrinsics to their target opcode
    // early a node with a target opcode can be of this class
    return N->isMemIntrinsic()             ||
           N->getOpcode() == ISD::PREFETCH ||
           N->isTargetMemoryOpcode();
  }
};

/// This SDNode is used to implement the code generator
/// support for the llvm IR shufflevector instruction.  It combines elements
/// from two input vectors into a new input vector, with the selection and
/// ordering of elements determined by an array of integers, referred to as
/// the shuffle mask.  For input vectors of width N, mask indices of 0..N-1
/// refer to elements from the LHS input, and indices from N to 2N-1 the RHS.
/// An index of -1 is treated as undef, such that the code generator may put
/// any value in the corresponding element of the result.
class ShuffleVectorSDNode : public SDNode {
  // The memory for Mask is owned by the SelectionDAG's OperandAllocator, and
  // is freed when the SelectionDAG object is destroyed.
  const int *Mask;

protected:
  friend class SelectionDAG;

  ShuffleVectorSDNode(EVT VT, unsigned Order, const DebugLoc &dl, const int *M)
      : SDNode(ISD::VECTOR_SHUFFLE, Order, dl, getSDVTList(VT)), Mask(M) {}

public:
  ArrayRef<int> getMask() const {
    EVT VT = getValueType(0);
    return makeArrayRef(Mask, VT.getVectorNumElements());
  }

  int getMaskElt(unsigned Idx) const {
    assert(Idx < getValueType(0).getVectorNumElements() && "Idx out of range!");
    return Mask[Idx];
  }

  bool isSplat() const { return isSplatMask(Mask, getValueType(0)); }

  int  getSplatIndex() const {
    assert(isSplat() && "Cannot get splat index for non-splat!");
    EVT VT = getValueType(0);
    for (unsigned i = 0, e = VT.getVectorNumElements(); i != e; ++i) {
      if (Mask[i] >= 0)
        return Mask[i];
    }
    llvm_unreachable("Splat with all undef indices?");
  }

  static bool isSplatMask(const int *Mask, EVT VT);

  /// Change values in a shuffle permute mask assuming
  /// the two vector operands have swapped position.
  static void commuteMask(MutableArrayRef<int> Mask) {
    unsigned NumElems = Mask.size();
    for (unsigned i = 0; i != NumElems; ++i) {
      int idx = Mask[i];
      if (idx < 0)
        continue;
      else if (idx < (int)NumElems)
        Mask[i] = idx + NumElems;
      else
        Mask[i] = idx - NumElems;
    }
  }

  static bool classof(const SDNode *N) {
    return N->getOpcode() == ISD::VECTOR_SHUFFLE;
  }
};

class ConstantSDNode : public SDNode {
  friend class SelectionDAG;

  const ConstantInt *Value;

  ConstantSDNode(bool isTarget, bool isOpaque, const ConstantInt *val, EVT VT)
      : SDNode(isTarget ? ISD::TargetConstant : ISD::Constant, 0, DebugLoc(),
               getSDVTList(VT)),
        Value(val) {
    ConstantSDNodeBits.IsOpaque = isOpaque;
  }

public:
  const ConstantInt *getConstantIntValue() const { return Value; }
  const APInt &getAPIntValue() const { return Value->getValue(); }
  uint64_t getZExtValue() const { return Value->getZExtValue(); }
  int64_t getSExtValue() const { return Value->getSExtValue(); }
  uint64_t getLimitedValue(uint64_t Limit = UINT64_MAX) {
    return Value->getLimitedValue(Limit);
  }

  bool isOne() const { return Value->isOne(); }
  bool isNullValue() const { return Value->isZero(); }
  bool isAllOnesValue() const { return Value->isMinusOne(); }

  bool isOpaque() const { return ConstantSDNodeBits.IsOpaque; }

  static bool classof(const SDNode *N) {
    return N->getOpcode() == ISD::Constant ||
           N->getOpcode() == ISD::TargetConstant;
  }
};

uint64_t SDNode::getConstantOperandVal(unsigned Num) const {
  return cast<ConstantSDNode>(getOperand(Num))->getZExtValue();
}

class ConstantFPSDNode : public SDNode {
  friend class SelectionDAG;

  const ConstantFP *Value;

  ConstantFPSDNode(bool isTarget, const ConstantFP *val, EVT VT)
      : SDNode(isTarget ? ISD::TargetConstantFP : ISD::ConstantFP, 0,
               DebugLoc(), getSDVTList(VT)),
        Value(val) {}

public:
  const APFloat& getValueAPF() const { return Value->getValueAPF(); }
  const ConstantFP *getConstantFPValue() const { return Value; }

  /// Return true if the value is positive or negative zero.
  bool isZero() const { return Value->isZero(); }

  /// Return true if the value is a NaN.
  bool isNaN() const { return Value->isNaN(); }

  /// Return true if the value is an infinity
  bool isInfinity() const { return Value->isInfinity(); }

  /// Return true if the value is negative.
  bool isNegative() const { return Value->isNegative(); }

  /// We don't rely on operator== working on double values, as
  /// it returns true for things that are clearly not equal, like -0.0 and 0.0.
  /// As such, this method can be used to do an exact bit-for-bit comparison of
  /// two floating point values.

  /// We leave the version with the double argument here because it's just so
  /// convenient to write "2.0" and the like.  Without this function we'd
  /// have to duplicate its logic everywhere it's called.
  bool isExactlyValue(double V) const {
    return Value->getValueAPF().isExactlyValue(V);
  }
  bool isExactlyValue(const APFloat& V) const;

  static bool isValueValidForType(EVT VT, const APFloat& Val);

  static bool classof(const SDNode *N) {
    return N->getOpcode() == ISD::ConstantFP ||
           N->getOpcode() == ISD::TargetConstantFP;
  }
};

/// Returns true if \p V is a constant integer zero.
bool isNullConstant(SDValue V);

/// Returns true if \p V is an FP constant with a value of positive zero.
bool isNullFPConstant(SDValue V);

/// Returns true if \p V is an integer constant with all bits set.
bool isAllOnesConstant(SDValue V);

/// Returns true if \p V is a constant integer one.
bool isOneConstant(SDValue V);

/// Return the non-bitcasted source operand of \p V if it exists.
/// If \p V is not a bitcasted value, it is returned as-is.
SDValue peekThroughBitcasts(SDValue V);

/// Return the non-bitcasted and one-use source operand of \p V if it exists.
/// If \p V is not a bitcasted one-use value, it is returned as-is.
SDValue peekThroughOneUseBitcasts(SDValue V);

/// Returns true if \p V is a bitwise not operation. Assumes that an all ones
/// constant is canonicalized to be operand 1.
bool isBitwiseNot(SDValue V);

/// Returns the SDNode if it is a constant splat BuildVector or constant int.
ConstantSDNode *isConstOrConstSplat(SDValue N, bool AllowUndefs = false);

/// Returns the SDNode if it is a constant splat BuildVector or constant float.
ConstantFPSDNode *isConstOrConstSplatFP(SDValue N, bool AllowUndefs = false);

/// Return true if the value is a constant 0 integer or a splatted vector of
/// a constant 0 integer (with no undefs).
/// Build vector implicit truncation is not an issue for null values.
bool isNullOrNullSplat(SDValue V);

/// Return true if the value is a constant 1 integer or a splatted vector of a
/// constant 1 integer (with no undefs).
/// Does not permit build vector implicit truncation.
bool isOneOrOneSplat(SDValue V);

/// Return true if the value is a constant -1 integer or a splatted vector of a
/// constant -1 integer (with no undefs).
/// Does not permit build vector implicit truncation.
bool isAllOnesOrAllOnesSplat(SDValue V);

class GlobalAddressSDNode : public SDNode {
  friend class SelectionDAG;

  const GlobalValue *TheGlobal;
  int64_t Offset;
  unsigned char TargetFlags;

  GlobalAddressSDNode(unsigned Opc, unsigned Order, const DebugLoc &DL,
                      const GlobalValue *GA, EVT VT, int64_t o,
                      unsigned char TF);

public:
  const GlobalValue *getGlobal() const { return TheGlobal; }
  int64_t getOffset() const { return Offset; }
  unsigned char getTargetFlags() const { return TargetFlags; }
  // Return the address space this GlobalAddress belongs to.
  unsigned getAddressSpace() const;

  static bool classof(const SDNode *N) {
    return N->getOpcode() == ISD::GlobalAddress ||
           N->getOpcode() == ISD::TargetGlobalAddress ||
           N->getOpcode() == ISD::GlobalTLSAddress ||
           N->getOpcode() == ISD::TargetGlobalTLSAddress;
  }
};

class FrameIndexSDNode : public SDNode {
  friend class SelectionDAG;

  int FI;

  FrameIndexSDNode(int fi, EVT VT, bool isTarg)
    : SDNode(isTarg ? ISD::TargetFrameIndex : ISD::FrameIndex,
      0, DebugLoc(), getSDVTList(VT)), FI(fi) {
  }

public:
  int getIndex() const { return FI; }

  static bool classof(const SDNode *N) {
    return N->getOpcode() == ISD::FrameIndex ||
           N->getOpcode() == ISD::TargetFrameIndex;
  }
};

class JumpTableSDNode : public SDNode {
  friend class SelectionDAG;

  int JTI;
  unsigned char TargetFlags;

  JumpTableSDNode(int jti, EVT VT, bool isTarg, unsigned char TF)
    : SDNode(isTarg ? ISD::TargetJumpTable : ISD::JumpTable,
      0, DebugLoc(), getSDVTList(VT)), JTI(jti), TargetFlags(TF) {
  }

public:
  int getIndex() const { return JTI; }
  unsigned char getTargetFlags() const { return TargetFlags; }

  static bool classof(const SDNode *N) {
    return N->getOpcode() == ISD::JumpTable ||
           N->getOpcode() == ISD::TargetJumpTable;
  }
};

class ConstantPoolSDNode : public SDNode {
  friend class SelectionDAG;

  union {
    const Constant *ConstVal;
    MachineConstantPoolValue *MachineCPVal;
  } Val;
  int Offset;  // It's a MachineConstantPoolValue if top bit is set.
  unsigned Alignment;  // Minimum alignment requirement of CP (not log2 value).
  unsigned char TargetFlags;

  ConstantPoolSDNode(bool isTarget, const Constant *c, EVT VT, int o,
                     unsigned Align, unsigned char TF)
    : SDNode(isTarget ? ISD::TargetConstantPool : ISD::ConstantPool, 0,
             DebugLoc(), getSDVTList(VT)), Offset(o), Alignment(Align),
             TargetFlags(TF) {
    assert(Offset >= 0 && "Offset is too large");
    Val.ConstVal = c;
  }

  ConstantPoolSDNode(bool isTarget, MachineConstantPoolValue *v,
                     EVT VT, int o, unsigned Align, unsigned char TF)
    : SDNode(isTarget ? ISD::TargetConstantPool : ISD::ConstantPool, 0,
             DebugLoc(), getSDVTList(VT)), Offset(o), Alignment(Align),
             TargetFlags(TF) {
    assert(Offset >= 0 && "Offset is too large");
    Val.MachineCPVal = v;
    Offset |= 1 << (sizeof(unsigned)*CHAR_BIT-1);
  }

public:
  bool isMachineConstantPoolEntry() const {
    return Offset < 0;
  }

  const Constant *getConstVal() const {
    assert(!isMachineConstantPoolEntry() && "Wrong constantpool type");
    return Val.ConstVal;
  }

  MachineConstantPoolValue *getMachineCPVal() const {
    assert(isMachineConstantPoolEntry() && "Wrong constantpool type");
    return Val.MachineCPVal;
  }

  int getOffset() const {
    return Offset & ~(1 << (sizeof(unsigned)*CHAR_BIT-1));
  }

  // Return the alignment of this constant pool object, which is either 0 (for
  // default alignment) or the desired value.
  unsigned getAlignment() const { return Alignment; }
  unsigned char getTargetFlags() const { return TargetFlags; }

  Type *getType() const;

  static bool classof(const SDNode *N) {
    return N->getOpcode() == ISD::ConstantPool ||
           N->getOpcode() == ISD::TargetConstantPool;
  }
};

/// Completely target-dependent object reference.
class TargetIndexSDNode : public SDNode {
  friend class SelectionDAG;

  unsigned char TargetFlags;
  int Index;
  int64_t Offset;

public:
  TargetIndexSDNode(int Idx, EVT VT, int64_t Ofs, unsigned char TF)
    : SDNode(ISD::TargetIndex, 0, DebugLoc(), getSDVTList(VT)),
      TargetFlags(TF), Index(Idx), Offset(Ofs) {}

  unsigned char getTargetFlags() const { return TargetFlags; }
  int getIndex() const { return Index; }
  int64_t getOffset() const { return Offset; }

  static bool classof(const SDNode *N) {
    return N->getOpcode() == ISD::TargetIndex;
  }
};

class BasicBlockSDNode : public SDNode {
  friend class SelectionDAG;

  MachineBasicBlock *MBB;

  /// Debug info is meaningful and potentially useful here, but we create
  /// blocks out of order when they're jumped to, which makes it a bit
  /// harder.  Let's see if we need it first.
  explicit BasicBlockSDNode(MachineBasicBlock *mbb)
    : SDNode(ISD::BasicBlock, 0, DebugLoc(), getSDVTList(MVT::Other)), MBB(mbb)
  {}

public:
  MachineBasicBlock *getBasicBlock() const { return MBB; }

  static bool classof(const SDNode *N) {
    return N->getOpcode() == ISD::BasicBlock;
  }
};

/// A "pseudo-class" with methods for operating on BUILD_VECTORs.
class BuildVectorSDNode : public SDNode {
public:
  // These are constructed as SDNodes and then cast to BuildVectorSDNodes.
  explicit BuildVectorSDNode() = delete;

  /// Check if this is a constant splat, and if so, find the
  /// smallest element size that splats the vector.  If MinSplatBits is
  /// nonzero, the element size must be at least that large.  Note that the
  /// splat element may be the entire vector (i.e., a one element vector).
  /// Returns the splat element value in SplatValue.  Any undefined bits in
  /// that value are zero, and the corresponding bits in the SplatUndef mask
  /// are set.  The SplatBitSize value is set to the splat element size in
  /// bits.  HasAnyUndefs is set to true if any bits in the vector are
  /// undefined.  isBigEndian describes the endianness of the target.
  bool isConstantSplat(APInt &SplatValue, APInt &SplatUndef,
                       unsigned &SplatBitSize, bool &HasAnyUndefs,
                       unsigned MinSplatBits = 0,
                       bool isBigEndian = false) const;

  /// Returns the splatted value or a null value if this is not a splat.
  ///
  /// If passed a non-null UndefElements bitvector, it will resize it to match
  /// the vector width and set the bits where elements are undef.
  SDValue getSplatValue(BitVector *UndefElements = nullptr) const;

  /// Returns the splatted constant or null if this is not a constant
  /// splat.
  ///
  /// If passed a non-null UndefElements bitvector, it will resize it to match
  /// the vector width and set the bits where elements are undef.
  ConstantSDNode *
  getConstantSplatNode(BitVector *UndefElements = nullptr) const;

  /// Returns the splatted constant FP or null if this is not a constant
  /// FP splat.
  ///
  /// If passed a non-null UndefElements bitvector, it will resize it to match
  /// the vector width and set the bits where elements are undef.
  ConstantFPSDNode *
  getConstantFPSplatNode(BitVector *UndefElements = nullptr) const;

  /// If this is a constant FP splat and the splatted constant FP is an
  /// exact power or 2, return the log base 2 integer value.  Otherwise,
  /// return -1.
  ///
  /// The BitWidth specifies the necessary bit precision.
  int32_t getConstantFPSplatPow2ToLog2Int(BitVector *UndefElements,
                                          uint32_t BitWidth) const;

  bool isConstant() const;

  static bool classof(const SDNode *N) {
    return N->getOpcode() == ISD::BUILD_VECTOR;
  }
};

/// An SDNode that holds an arbitrary LLVM IR Value. This is
/// used when the SelectionDAG needs to make a simple reference to something
/// in the LLVM IR representation.
///
class SrcValueSDNode : public SDNode {
  friend class SelectionDAG;

  const Value *V;

  /// Create a SrcValue for a general value.
  explicit SrcValueSDNode(const Value *v)
    : SDNode(ISD::SRCVALUE, 0, DebugLoc(), getSDVTList(MVT::Other)), V(v) {}

public:
  /// Return the contained Value.
  const Value *getValue() const { return V; }

  static bool classof(const SDNode *N) {
    return N->getOpcode() == ISD::SRCVALUE;
  }
};

class MDNodeSDNode : public SDNode {
  friend class SelectionDAG;

  const MDNode *MD;

  explicit MDNodeSDNode(const MDNode *md)
  : SDNode(ISD::MDNODE_SDNODE, 0, DebugLoc(), getSDVTList(MVT::Other)), MD(md)
  {}

public:
  const MDNode *getMD() const { return MD; }

  static bool classof(const SDNode *N) {
    return N->getOpcode() == ISD::MDNODE_SDNODE;
  }
};

class RegisterSDNode : public SDNode {
  friend class SelectionDAG;

  unsigned Reg;

  RegisterSDNode(unsigned reg, EVT VT)
    : SDNode(ISD::Register, 0, DebugLoc(), getSDVTList(VT)), Reg(reg) {}

public:
  unsigned getReg() const { return Reg; }

  static bool classof(const SDNode *N) {
    return N->getOpcode() == ISD::Register;
  }
};

class RegisterMaskSDNode : public SDNode {
  friend class SelectionDAG;

  // The memory for RegMask is not owned by the node.
  const uint32_t *RegMask;

  RegisterMaskSDNode(const uint32_t *mask)
    : SDNode(ISD::RegisterMask, 0, DebugLoc(), getSDVTList(MVT::Untyped)),
      RegMask(mask) {}

public:
  const uint32_t *getRegMask() const { return RegMask; }

  static bool classof(const SDNode *N) {
    return N->getOpcode() == ISD::RegisterMask;
  }
};

class BlockAddressSDNode : public SDNode {
  friend class SelectionDAG;

  const BlockAddress *BA;
  int64_t Offset;
  unsigned char TargetFlags;

  BlockAddressSDNode(unsigned NodeTy, EVT VT, const BlockAddress *ba,
                     int64_t o, unsigned char Flags)
    : SDNode(NodeTy, 0, DebugLoc(), getSDVTList(VT)),
             BA(ba), Offset(o), TargetFlags(Flags) {}

public:
  const BlockAddress *getBlockAddress() const { return BA; }
  int64_t getOffset() const { return Offset; }
  unsigned char getTargetFlags() const { return TargetFlags; }

  static bool classof(const SDNode *N) {
    return N->getOpcode() == ISD::BlockAddress ||
           N->getOpcode() == ISD::TargetBlockAddress;
  }
};

class LabelSDNode : public SDNode {
  friend class SelectionDAG;

  MCSymbol *Label;

  LabelSDNode(unsigned Order, const DebugLoc &dl, MCSymbol *L)
      : SDNode(ISD::EH_LABEL, Order, dl, getSDVTList(MVT::Other)), Label(L) {}

public:
  MCSymbol *getLabel() const { return Label; }

  static bool classof(const SDNode *N) {
    return N->getOpcode() == ISD::EH_LABEL ||
           N->getOpcode() == ISD::ANNOTATION_LABEL;
  }
};

class ExternalSymbolSDNode : public SDNode {
  friend class SelectionDAG;

  const char *Symbol;
  unsigned char TargetFlags;

  ExternalSymbolSDNode(bool isTarget, const char *Sym, unsigned char TF, EVT VT)
    : SDNode(isTarget ? ISD::TargetExternalSymbol : ISD::ExternalSymbol,
             0, DebugLoc(), getSDVTList(VT)), Symbol(Sym), TargetFlags(TF) {}

public:
  const char *getSymbol() const { return Symbol; }
  unsigned char getTargetFlags() const { return TargetFlags; }

  static bool classof(const SDNode *N) {
    return N->getOpcode() == ISD::ExternalSymbol ||
           N->getOpcode() == ISD::TargetExternalSymbol;
  }
};

class MCSymbolSDNode : public SDNode {
  friend class SelectionDAG;

  MCSymbol *Symbol;

  MCSymbolSDNode(MCSymbol *Symbol, EVT VT)
      : SDNode(ISD::MCSymbol, 0, DebugLoc(), getSDVTList(VT)), Symbol(Symbol) {}

public:
  MCSymbol *getMCSymbol() const { return Symbol; }

  static bool classof(const SDNode *N) {
    return N->getOpcode() == ISD::MCSymbol;
  }
};

class CondCodeSDNode : public SDNode {
  friend class SelectionDAG;

  ISD::CondCode Condition;

  explicit CondCodeSDNode(ISD::CondCode Cond)
    : SDNode(ISD::CONDCODE, 0, DebugLoc(), getSDVTList(MVT::Other)),
      Condition(Cond) {}

public:
  ISD::CondCode get() const { return Condition; }

  static bool classof(const SDNode *N) {
    return N->getOpcode() == ISD::CONDCODE;
  }
};

/// This class is used to represent EVT's, which are used
/// to parameterize some operations.
class VTSDNode : public SDNode {
  friend class SelectionDAG;

  EVT ValueType;

  explicit VTSDNode(EVT VT)
    : SDNode(ISD::VALUETYPE, 0, DebugLoc(), getSDVTList(MVT::Other)),
      ValueType(VT) {}

public:
  EVT getVT() const { return ValueType; }

  static bool classof(const SDNode *N) {
    return N->getOpcode() == ISD::VALUETYPE;
  }
};

/// Base class for LoadSDNode and StoreSDNode
class LSBaseSDNode : public MemSDNode {
public:
  LSBaseSDNode(ISD::NodeType NodeTy, unsigned Order, const DebugLoc &dl,
               SDVTList VTs, ISD::MemIndexedMode AM, EVT MemVT,
               MachineMemOperand *MMO)
      : MemSDNode(NodeTy, Order, dl, VTs, MemVT, MMO) {
    LSBaseSDNodeBits.AddressingMode = AM;
    assert(getAddressingMode() == AM && "Value truncated");
  }

  const SDValue &getOffset() const {
    return getOperand(getOpcode() == ISD::LOAD ? 2 : 3);
  }

  /// Return the addressing mode for this load or store:
  /// unindexed, pre-inc, pre-dec, post-inc, or post-dec.
  ISD::MemIndexedMode getAddressingMode() const {
    return static_cast<ISD::MemIndexedMode>(LSBaseSDNodeBits.AddressingMode);
  }

  /// Return true if this is a pre/post inc/dec load/store.
  bool isIndexed() const { return getAddressingMode() != ISD::UNINDEXED; }

  /// Return true if this is NOT a pre/post inc/dec load/store.
  bool isUnindexed() const { return getAddressingMode() == ISD::UNINDEXED; }

  static bool classof(const SDNode *N) {
    return N->getOpcode() == ISD::LOAD ||
           N->getOpcode() == ISD::STORE;
  }
};

/// This class is used to represent ISD::LOAD nodes.
class LoadSDNode : public LSBaseSDNode {
  friend class SelectionDAG;

  LoadSDNode(unsigned Order, const DebugLoc &dl, SDVTList VTs,
             ISD::MemIndexedMode AM, ISD::LoadExtType ETy, EVT MemVT,
             MachineMemOperand *MMO)
      : LSBaseSDNode(ISD::LOAD, Order, dl, VTs, AM, MemVT, MMO) {
    LoadSDNodeBits.ExtTy = ETy;
    assert(readMem() && "Load MachineMemOperand is not a load!");
    assert(!writeMem() && "Load MachineMemOperand is a store!");
  }

public:
  /// Return whether this is a plain node,
  /// or one of the varieties of value-extending loads.
  ISD::LoadExtType getExtensionType() const {
    return static_cast<ISD::LoadExtType>(LoadSDNodeBits.ExtTy);
  }

  const SDValue &getBasePtr() const { return getOperand(1); }
  const SDValue &getOffset() const { return getOperand(2); }

  static bool classof(const SDNode *N) {
    return N->getOpcode() == ISD::LOAD;
  }
};

/// This class is used to represent ISD::STORE nodes.
class StoreSDNode : public LSBaseSDNode {
  friend class SelectionDAG;

  StoreSDNode(unsigned Order, const DebugLoc &dl, SDVTList VTs,
              ISD::MemIndexedMode AM, bool isTrunc, EVT MemVT,
              MachineMemOperand *MMO)
      : LSBaseSDNode(ISD::STORE, Order, dl, VTs, AM, MemVT, MMO) {
    StoreSDNodeBits.IsTruncating = isTrunc;
    assert(!readMem() && "Store MachineMemOperand is a load!");
    assert(writeMem() && "Store MachineMemOperand is not a store!");
  }

public:
  /// Return true if the op does a truncation before store.
  /// For integers this is the same as doing a TRUNCATE and storing the result.
  /// For floats, it is the same as doing an FP_ROUND and storing the result.
  bool isTruncatingStore() const { return StoreSDNodeBits.IsTruncating; }
  void setTruncatingStore(bool Truncating) {
    StoreSDNodeBits.IsTruncating = Truncating;
  }

  const SDValue &getValue() const { return getOperand(1); }
  const SDValue &getBasePtr() const { return getOperand(2); }
  const SDValue &getOffset() const { return getOperand(3); }

  static bool classof(const SDNode *N) {
    return N->getOpcode() == ISD::STORE;
  }
};

/// This base class is used to represent MLOAD and MSTORE nodes
class MaskedLoadStoreSDNode : public MemSDNode {
public:
  friend class SelectionDAG;

  MaskedLoadStoreSDNode(ISD::NodeType NodeTy, unsigned Order,
                        const DebugLoc &dl, SDVTList VTs, EVT MemVT,
                        MachineMemOperand *MMO)
      : MemSDNode(NodeTy, Order, dl, VTs, MemVT, MMO) {}

  // MaskedLoadSDNode (Chain, ptr, mask, passthru)
  // MaskedStoreSDNode (Chain, data, ptr, mask)
  // Mask is a vector of i1 elements
  const SDValue &getBasePtr() const {
    return getOperand(getOpcode() == ISD::MLOAD ? 1 : 2);
  }
  const SDValue &getMask() const {
    return getOperand(getOpcode() == ISD::MLOAD ? 2 : 3);
  }

  static bool classof(const SDNode *N) {
    return N->getOpcode() == ISD::MLOAD ||
           N->getOpcode() == ISD::MSTORE;
  }
};

/// This class is used to represent an MLOAD node
class MaskedLoadSDNode : public MaskedLoadStoreSDNode {
public:
  friend class SelectionDAG;

  MaskedLoadSDNode(unsigned Order, const DebugLoc &dl, SDVTList VTs,
                   ISD::LoadExtType ETy, bool IsExpanding, EVT MemVT,
                   MachineMemOperand *MMO)
      : MaskedLoadStoreSDNode(ISD::MLOAD, Order, dl, VTs, MemVT, MMO) {
    LoadSDNodeBits.ExtTy = ETy;
    LoadSDNodeBits.IsExpanding = IsExpanding;
  }

  ISD::LoadExtType getExtensionType() const {
    return static_cast<ISD::LoadExtType>(LoadSDNodeBits.ExtTy);
  }

  const SDValue &getBasePtr() const { return getOperand(1); }
  const SDValue &getMask() const    { return getOperand(2); }
  const SDValue &getPassThru() const { return getOperand(3); }

  static bool classof(const SDNode *N) {
    return N->getOpcode() == ISD::MLOAD;
  }

  bool isExpandingLoad() const { return LoadSDNodeBits.IsExpanding; }
};

/// This class is used to represent an MSTORE node
class MaskedStoreSDNode : public MaskedLoadStoreSDNode {
public:
  friend class SelectionDAG;

  MaskedStoreSDNode(unsigned Order, const DebugLoc &dl, SDVTList VTs,
                    bool isTrunc, bool isCompressing, EVT MemVT,
                    MachineMemOperand *MMO)
      : MaskedLoadStoreSDNode(ISD::MSTORE, Order, dl, VTs, MemVT, MMO) {
    StoreSDNodeBits.IsTruncating = isTrunc;
    StoreSDNodeBits.IsCompressing = isCompressing;
  }

  /// Return true if the op does a truncation before store.
  /// For integers this is the same as doing a TRUNCATE and storing the result.
  /// For floats, it is the same as doing an FP_ROUND and storing the result.
  bool isTruncatingStore() const { return StoreSDNodeBits.IsTruncating; }

  /// Returns true if the op does a compression to the vector before storing.
  /// The node contiguously stores the active elements (integers or floats)
  /// in src (those with their respective bit set in writemask k) to unaligned
  /// memory at base_addr.
  bool isCompressingStore() const { return StoreSDNodeBits.IsCompressing; }

  const SDValue &getValue() const   { return getOperand(1); }
  const SDValue &getBasePtr() const { return getOperand(2); }
  const SDValue &getMask() const    { return getOperand(3); }

  static bool classof(const SDNode *N) {
    return N->getOpcode() == ISD::MSTORE;
  }
};

/// This is a base class used to represent
/// MGATHER and MSCATTER nodes
///
class MaskedGatherScatterSDNode : public MemSDNode {
public:
  friend class SelectionDAG;

  MaskedGatherScatterSDNode(ISD::NodeType NodeTy, unsigned Order,
                            const DebugLoc &dl, SDVTList VTs, EVT MemVT,
                            MachineMemOperand *MMO)
      : MemSDNode(NodeTy, Order, dl, VTs, MemVT, MMO) {}

  // In the both nodes address is Op1, mask is Op2:
  // MaskedGatherSDNode  (Chain, passthru, mask, base, index, scale)
  // MaskedScatterSDNode (Chain, value, mask, base, index, scale)
  // Mask is a vector of i1 elements
  const SDValue &getBasePtr() const { return getOperand(3); }
  const SDValue &getIndex()   const { return getOperand(4); }
  const SDValue &getMask()    const { return getOperand(2); }
  const SDValue &getScale()   const { return getOperand(5); }

  static bool classof(const SDNode *N) {
    return N->getOpcode() == ISD::MGATHER ||
           N->getOpcode() == ISD::MSCATTER;
  }
};

/// This class is used to represent an MGATHER node
///
class MaskedGatherSDNode : public MaskedGatherScatterSDNode {
public:
  friend class SelectionDAG;

  MaskedGatherSDNode(unsigned Order, const DebugLoc &dl, SDVTList VTs,
                     EVT MemVT, MachineMemOperand *MMO)
      : MaskedGatherScatterSDNode(ISD::MGATHER, Order, dl, VTs, MemVT, MMO) {}

  const SDValue &getPassThru() const { return getOperand(1); }

  static bool classof(const SDNode *N) {
    return N->getOpcode() == ISD::MGATHER;
  }
};

/// This class is used to represent an MSCATTER node
///
class MaskedScatterSDNode : public MaskedGatherScatterSDNode {
public:
  friend class SelectionDAG;

  MaskedScatterSDNode(unsigned Order, const DebugLoc &dl, SDVTList VTs,
                      EVT MemVT, MachineMemOperand *MMO)
      : MaskedGatherScatterSDNode(ISD::MSCATTER, Order, dl, VTs, MemVT, MMO) {}

  const SDValue &getValue() const { return getOperand(1); }

  static bool classof(const SDNode *N) {
    return N->getOpcode() == ISD::MSCATTER;
  }
};

/// An SDNode that represents everything that will be needed
/// to construct a MachineInstr. These nodes are created during the
/// instruction selection proper phase.
///
/// Note that the only supported way to set the `memoperands` is by calling the
/// `SelectionDAG::setNodeMemRefs` function as the memory management happens
/// inside the DAG rather than in the node.
class MachineSDNode : public SDNode {
private:
  friend class SelectionDAG;

  MachineSDNode(unsigned Opc, unsigned Order, const DebugLoc &DL, SDVTList VTs)
      : SDNode(Opc, Order, DL, VTs) {}

  // We use a pointer union between a single `MachineMemOperand` pointer and
  // a pointer to an array of `MachineMemOperand` pointers. This is null when
  // the number of these is zero, the single pointer variant used when the
  // number is one, and the array is used for larger numbers.
  //
  // The array is allocated via the `SelectionDAG`'s allocator and so will
  // always live until the DAG is cleaned up and doesn't require ownership here.
  //
  // We can't use something simpler like `TinyPtrVector` here because `SDNode`
  // subclasses aren't managed in a conforming C++ manner. See the comments on
  // `SelectionDAG::MorphNodeTo` which details what all goes on, but the
  // constraint here is that these don't manage memory with their constructor or
  // destructor and can be initialized to a good state even if they start off
  // uninitialized.
  PointerUnion<MachineMemOperand *, MachineMemOperand **> MemRefs = {};

  // Note that this could be folded into the above `MemRefs` member if doing so
  // is advantageous at some point. We don't need to store this in most cases.
  // However, at the moment this doesn't appear to make the allocation any
  // smaller and makes the code somewhat simpler to read.
  int NumMemRefs = 0;

public:
  using mmo_iterator = ArrayRef<MachineMemOperand *>::const_iterator;

  ArrayRef<MachineMemOperand *> memoperands() const {
    // Special case the common cases.
    if (NumMemRefs == 0)
      return {};
    if (NumMemRefs == 1)
      return makeArrayRef(MemRefs.getAddrOfPtr1(), 1);

    // Otherwise we have an actual array.
    return makeArrayRef(MemRefs.get<MachineMemOperand **>(), NumMemRefs);
  }
  mmo_iterator memoperands_begin() const { return memoperands().begin(); }
  mmo_iterator memoperands_end() const { return memoperands().end(); }
  bool memoperands_empty() const { return memoperands().empty(); }

  /// Clear out the memory reference descriptor list.
  void clearMemRefs() {
    MemRefs = nullptr;
    NumMemRefs = 0;
  }

  static bool classof(const SDNode *N) {
    return N->isMachineOpcode();
  }
};

class SDNodeIterator : public std::iterator<std::forward_iterator_tag,
                                            SDNode, ptrdiff_t> {
  const SDNode *Node;
  unsigned Operand;

  SDNodeIterator(const SDNode *N, unsigned Op) : Node(N), Operand(Op) {}

public:
  bool operator==(const SDNodeIterator& x) const {
    return Operand == x.Operand;
  }
  bool operator!=(const SDNodeIterator& x) const { return !operator==(x); }

  pointer operator*() const {
    return Node->getOperand(Operand).getNode();
  }
  pointer operator->() const { return operator*(); }

  SDNodeIterator& operator++() {                // Preincrement
    ++Operand;
    return *this;
  }
  SDNodeIterator operator++(int) { // Postincrement
    SDNodeIterator tmp = *this; ++*this; return tmp;
  }
  size_t operator-(SDNodeIterator Other) const {
    assert(Node == Other.Node &&
           "Cannot compare iterators of two different nodes!");
    return Operand - Other.Operand;
  }

  static SDNodeIterator begin(const SDNode *N) { return SDNodeIterator(N, 0); }
  static SDNodeIterator end  (const SDNode *N) {
    return SDNodeIterator(N, N->getNumOperands());
  }

  unsigned getOperand() const { return Operand; }
  const SDNode *getNode() const { return Node; }
};

template <> struct GraphTraits<SDNode*> {
  using NodeRef = SDNode *;
  using ChildIteratorType = SDNodeIterator;

  static NodeRef getEntryNode(SDNode *N) { return N; }

  static ChildIteratorType child_begin(NodeRef N) {
    return SDNodeIterator::begin(N);
  }

  static ChildIteratorType child_end(NodeRef N) {
    return SDNodeIterator::end(N);
  }
};

/// A representation of the largest SDNode, for use in sizeof().
///
/// This needs to be a union because the largest node differs on 32 bit systems
/// with 4 and 8 byte pointer alignment, respectively.
using LargestSDNode = AlignedCharArrayUnion<AtomicSDNode, TargetIndexSDNode,
                                            BlockAddressSDNode,
                                            GlobalAddressSDNode>;

/// The SDNode class with the greatest alignment requirement.
using MostAlignedSDNode = GlobalAddressSDNode;

namespace ISD {

  /// Returns true if the specified node is a non-extending and unindexed load.
  inline bool isNormalLoad(const SDNode *N) {
    const LoadSDNode *Ld = dyn_cast<LoadSDNode>(N);
    return Ld && Ld->getExtensionType() == ISD::NON_EXTLOAD &&
      Ld->getAddressingMode() == ISD::UNINDEXED;
  }

  /// Returns true if the specified node is a non-extending load.
  inline bool isNON_EXTLoad(const SDNode *N) {
    return isa<LoadSDNode>(N) &&
      cast<LoadSDNode>(N)->getExtensionType() == ISD::NON_EXTLOAD;
  }

  /// Returns true if the specified node is a EXTLOAD.
  inline bool isEXTLoad(const SDNode *N) {
    return isa<LoadSDNode>(N) &&
      cast<LoadSDNode>(N)->getExtensionType() == ISD::EXTLOAD;
  }

  /// Returns true if the specified node is a SEXTLOAD.
  inline bool isSEXTLoad(const SDNode *N) {
    return isa<LoadSDNode>(N) &&
      cast<LoadSDNode>(N)->getExtensionType() == ISD::SEXTLOAD;
  }

  /// Returns true if the specified node is a ZEXTLOAD.
  inline bool isZEXTLoad(const SDNode *N) {
    return isa<LoadSDNode>(N) &&
      cast<LoadSDNode>(N)->getExtensionType() == ISD::ZEXTLOAD;
  }

  /// Returns true if the specified node is an unindexed load.
  inline bool isUNINDEXEDLoad(const SDNode *N) {
    return isa<LoadSDNode>(N) &&
      cast<LoadSDNode>(N)->getAddressingMode() == ISD::UNINDEXED;
  }

  /// Returns true if the specified node is a non-truncating
  /// and unindexed store.
  inline bool isNormalStore(const SDNode *N) {
    const StoreSDNode *St = dyn_cast<StoreSDNode>(N);
    return St && !St->isTruncatingStore() &&
      St->getAddressingMode() == ISD::UNINDEXED;
  }

  /// Returns true if the specified node is a non-truncating store.
  inline bool isNON_TRUNCStore(const SDNode *N) {
    return isa<StoreSDNode>(N) && !cast<StoreSDNode>(N)->isTruncatingStore();
  }

  /// Returns true if the specified node is a truncating store.
  inline bool isTRUNCStore(const SDNode *N) {
    return isa<StoreSDNode>(N) && cast<StoreSDNode>(N)->isTruncatingStore();
  }

  /// Returns true if the specified node is an unindexed store.
  inline bool isUNINDEXEDStore(const SDNode *N) {
    return isa<StoreSDNode>(N) &&
      cast<StoreSDNode>(N)->getAddressingMode() == ISD::UNINDEXED;
  }

  /// Return true if the node is a math/logic binary operator. This corresponds
  /// to the IR function of the same name.
  inline bool isBinaryOp(const SDNode *N) {
    auto Op = N->getOpcode();
    return (Op == ISD::ADD || Op == ISD::SUB || Op == ISD::MUL ||
            Op == ISD::AND || Op == ISD::OR || Op == ISD::XOR ||
            Op == ISD::SHL || Op == ISD::SRL || Op == ISD::SRA ||
            Op == ISD::SDIV || Op == ISD::UDIV || Op == ISD::SREM ||
            Op == ISD::UREM || Op == ISD::FADD || Op == ISD::FSUB ||
            Op == ISD::FMUL || Op == ISD::FDIV || Op == ISD::FREM);
  }

  /// Attempt to match a unary predicate against a scalar/splat constant or
  /// every element of a constant BUILD_VECTOR.
  /// If AllowUndef is true, then UNDEF elements will pass nullptr to Match.
  bool matchUnaryPredicate(SDValue Op,
                           std::function<bool(ConstantSDNode *)> Match,
                           bool AllowUndefs = false);

  /// Attempt to match a binary predicate against a pair of scalar/splat
  /// constants or every element of a pair of constant BUILD_VECTORs.
  /// If AllowUndef is true, then UNDEF elements will pass nullptr to Match.
  bool matchBinaryPredicate(
      SDValue LHS, SDValue RHS,
      std::function<bool(ConstantSDNode *, ConstantSDNode *)> Match,
      bool AllowUndefs = false);
} // end namespace ISD

} // end namespace llvm

#endif // LLVM_CODEGEN_SELECTIONDAGNODES_H
