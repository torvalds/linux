//==- BlockFrequencyInfoImpl.h - Block Frequency Implementation --*- C++ -*-==//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// Shared implementation of BlockFrequency for IR and Machine Instructions.
// See the documentation below for BlockFrequencyInfoImpl for details.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_ANALYSIS_BLOCKFREQUENCYINFOIMPL_H
#define LLVM_ANALYSIS_BLOCKFREQUENCYINFOIMPL_H

#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/DenseSet.h"
#include "llvm/ADT/GraphTraits.h"
#include "llvm/ADT/Optional.h"
#include "llvm/ADT/PostOrderIterator.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/SparseBitVector.h"
#include "llvm/ADT/Twine.h"
#include "llvm/ADT/iterator_range.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/Support/BlockFrequency.h"
#include "llvm/Support/BranchProbability.h"
#include "llvm/Support/DOTGraphTraits.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/Format.h"
#include "llvm/Support/ScaledNumber.h"
#include "llvm/Support/raw_ostream.h"
#include <algorithm>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <iterator>
#include <limits>
#include <list>
#include <string>
#include <utility>
#include <vector>

#define DEBUG_TYPE "block-freq"

namespace llvm {

class BranchProbabilityInfo;
class Function;
class Loop;
class LoopInfo;
class MachineBasicBlock;
class MachineBranchProbabilityInfo;
class MachineFunction;
class MachineLoop;
class MachineLoopInfo;

namespace bfi_detail {

struct IrreducibleGraph;

// This is part of a workaround for a GCC 4.7 crash on lambdas.
template <class BT> struct BlockEdgesAdder;

/// Mass of a block.
///
/// This class implements a sort of fixed-point fraction always between 0.0 and
/// 1.0.  getMass() == std::numeric_limits<uint64_t>::max() indicates a value of
/// 1.0.
///
/// Masses can be added and subtracted.  Simple saturation arithmetic is used,
/// so arithmetic operations never overflow or underflow.
///
/// Masses can be multiplied.  Multiplication treats full mass as 1.0 and uses
/// an inexpensive floating-point algorithm that's off-by-one (almost, but not
/// quite, maximum precision).
///
/// Masses can be scaled by \a BranchProbability at maximum precision.
class BlockMass {
  uint64_t Mass = 0;

public:
  BlockMass() = default;
  explicit BlockMass(uint64_t Mass) : Mass(Mass) {}

  static BlockMass getEmpty() { return BlockMass(); }

  static BlockMass getFull() {
    return BlockMass(std::numeric_limits<uint64_t>::max());
  }

  uint64_t getMass() const { return Mass; }

  bool isFull() const { return Mass == std::numeric_limits<uint64_t>::max(); }
  bool isEmpty() const { return !Mass; }

  bool operator!() const { return isEmpty(); }

  /// Add another mass.
  ///
  /// Adds another mass, saturating at \a isFull() rather than overflowing.
  BlockMass &operator+=(BlockMass X) {
    uint64_t Sum = Mass + X.Mass;
    Mass = Sum < Mass ? std::numeric_limits<uint64_t>::max() : Sum;
    return *this;
  }

  /// Subtract another mass.
  ///
  /// Subtracts another mass, saturating at \a isEmpty() rather than
  /// undeflowing.
  BlockMass &operator-=(BlockMass X) {
    uint64_t Diff = Mass - X.Mass;
    Mass = Diff > Mass ? 0 : Diff;
    return *this;
  }

  BlockMass &operator*=(BranchProbability P) {
    Mass = P.scale(Mass);
    return *this;
  }

  bool operator==(BlockMass X) const { return Mass == X.Mass; }
  bool operator!=(BlockMass X) const { return Mass != X.Mass; }
  bool operator<=(BlockMass X) const { return Mass <= X.Mass; }
  bool operator>=(BlockMass X) const { return Mass >= X.Mass; }
  bool operator<(BlockMass X) const { return Mass < X.Mass; }
  bool operator>(BlockMass X) const { return Mass > X.Mass; }

  /// Convert to scaled number.
  ///
  /// Convert to \a ScaledNumber.  \a isFull() gives 1.0, while \a isEmpty()
  /// gives slightly above 0.0.
  ScaledNumber<uint64_t> toScaled() const;

  void dump() const;
  raw_ostream &print(raw_ostream &OS) const;
};

inline BlockMass operator+(BlockMass L, BlockMass R) {
  return BlockMass(L) += R;
}
inline BlockMass operator-(BlockMass L, BlockMass R) {
  return BlockMass(L) -= R;
}
inline BlockMass operator*(BlockMass L, BranchProbability R) {
  return BlockMass(L) *= R;
}
inline BlockMass operator*(BranchProbability L, BlockMass R) {
  return BlockMass(R) *= L;
}

inline raw_ostream &operator<<(raw_ostream &OS, BlockMass X) {
  return X.print(OS);
}

} // end namespace bfi_detail

template <> struct isPodLike<bfi_detail::BlockMass> {
  static const bool value = true;
};

/// Base class for BlockFrequencyInfoImpl
///
/// BlockFrequencyInfoImplBase has supporting data structures and some
/// algorithms for BlockFrequencyInfoImplBase.  Only algorithms that depend on
/// the block type (or that call such algorithms) are skipped here.
///
/// Nevertheless, the majority of the overall algorithm documention lives with
/// BlockFrequencyInfoImpl.  See there for details.
class BlockFrequencyInfoImplBase {
public:
  using Scaled64 = ScaledNumber<uint64_t>;
  using BlockMass = bfi_detail::BlockMass;

  /// Representative of a block.
  ///
  /// This is a simple wrapper around an index into the reverse-post-order
  /// traversal of the blocks.
  ///
  /// Unlike a block pointer, its order has meaning (location in the
  /// topological sort) and it's class is the same regardless of block type.
  struct BlockNode {
    using IndexType = uint32_t;

    IndexType Index = std::numeric_limits<uint32_t>::max();

    BlockNode() = default;
    BlockNode(IndexType Index) : Index(Index) {}

    bool operator==(const BlockNode &X) const { return Index == X.Index; }
    bool operator!=(const BlockNode &X) const { return Index != X.Index; }
    bool operator<=(const BlockNode &X) const { return Index <= X.Index; }
    bool operator>=(const BlockNode &X) const { return Index >= X.Index; }
    bool operator<(const BlockNode &X) const { return Index < X.Index; }
    bool operator>(const BlockNode &X) const { return Index > X.Index; }

    bool isValid() const { return Index <= getMaxIndex(); }

    static size_t getMaxIndex() {
       return std::numeric_limits<uint32_t>::max() - 1;
    }
  };

  /// Stats about a block itself.
  struct FrequencyData {
    Scaled64 Scaled;
    uint64_t Integer;
  };

  /// Data about a loop.
  ///
  /// Contains the data necessary to represent a loop as a pseudo-node once it's
  /// packaged.
  struct LoopData {
    using ExitMap = SmallVector<std::pair<BlockNode, BlockMass>, 4>;
    using NodeList = SmallVector<BlockNode, 4>;
    using HeaderMassList = SmallVector<BlockMass, 1>;

    LoopData *Parent;            ///< The parent loop.
    bool IsPackaged = false;     ///< Whether this has been packaged.
    uint32_t NumHeaders = 1;     ///< Number of headers.
    ExitMap Exits;               ///< Successor edges (and weights).
    NodeList Nodes;              ///< Header and the members of the loop.
    HeaderMassList BackedgeMass; ///< Mass returned to each loop header.
    BlockMass Mass;
    Scaled64 Scale;

    LoopData(LoopData *Parent, const BlockNode &Header)
      : Parent(Parent), Nodes(1, Header), BackedgeMass(1) {}

    template <class It1, class It2>
    LoopData(LoopData *Parent, It1 FirstHeader, It1 LastHeader, It2 FirstOther,
             It2 LastOther)
        : Parent(Parent), Nodes(FirstHeader, LastHeader) {
      NumHeaders = Nodes.size();
      Nodes.insert(Nodes.end(), FirstOther, LastOther);
      BackedgeMass.resize(NumHeaders);
    }

    bool isHeader(const BlockNode &Node) const {
      if (isIrreducible())
        return std::binary_search(Nodes.begin(), Nodes.begin() + NumHeaders,
                                  Node);
      return Node == Nodes[0];
    }

    BlockNode getHeader() const { return Nodes[0]; }
    bool isIrreducible() const { return NumHeaders > 1; }

    HeaderMassList::difference_type getHeaderIndex(const BlockNode &B) {
      assert(isHeader(B) && "this is only valid on loop header blocks");
      if (isIrreducible())
        return std::lower_bound(Nodes.begin(), Nodes.begin() + NumHeaders, B) -
               Nodes.begin();
      return 0;
    }

    NodeList::const_iterator members_begin() const {
      return Nodes.begin() + NumHeaders;
    }

    NodeList::const_iterator members_end() const { return Nodes.end(); }
    iterator_range<NodeList::const_iterator> members() const {
      return make_range(members_begin(), members_end());
    }
  };

  /// Index of loop information.
  struct WorkingData {
    BlockNode Node;           ///< This node.
    LoopData *Loop = nullptr; ///< The loop this block is inside.
    BlockMass Mass;           ///< Mass distribution from the entry block.

    WorkingData(const BlockNode &Node) : Node(Node) {}

    bool isLoopHeader() const { return Loop && Loop->isHeader(Node); }

    bool isDoubleLoopHeader() const {
      return isLoopHeader() && Loop->Parent && Loop->Parent->isIrreducible() &&
             Loop->Parent->isHeader(Node);
    }

    LoopData *getContainingLoop() const {
      if (!isLoopHeader())
        return Loop;
      if (!isDoubleLoopHeader())
        return Loop->Parent;
      return Loop->Parent->Parent;
    }

    /// Resolve a node to its representative.
    ///
    /// Get the node currently representing Node, which could be a containing
    /// loop.
    ///
    /// This function should only be called when distributing mass.  As long as
    /// there are no irreducible edges to Node, then it will have complexity
    /// O(1) in this context.
    ///
    /// In general, the complexity is O(L), where L is the number of loop
    /// headers Node has been packaged into.  Since this method is called in
    /// the context of distributing mass, L will be the number of loop headers
    /// an early exit edge jumps out of.
    BlockNode getResolvedNode() const {
      auto L = getPackagedLoop();
      return L ? L->getHeader() : Node;
    }

    LoopData *getPackagedLoop() const {
      if (!Loop || !Loop->IsPackaged)
        return nullptr;
      auto L = Loop;
      while (L->Parent && L->Parent->IsPackaged)
        L = L->Parent;
      return L;
    }

    /// Get the appropriate mass for a node.
    ///
    /// Get appropriate mass for Node.  If Node is a loop-header (whose loop
    /// has been packaged), returns the mass of its pseudo-node.  If it's a
    /// node inside a packaged loop, it returns the loop's mass.
    BlockMass &getMass() {
      if (!isAPackage())
        return Mass;
      if (!isADoublePackage())
        return Loop->Mass;
      return Loop->Parent->Mass;
    }

    /// Has ContainingLoop been packaged up?
    bool isPackaged() const { return getResolvedNode() != Node; }

    /// Has Loop been packaged up?
    bool isAPackage() const { return isLoopHeader() && Loop->IsPackaged; }

    /// Has Loop been packaged up twice?
    bool isADoublePackage() const {
      return isDoubleLoopHeader() && Loop->Parent->IsPackaged;
    }
  };

  /// Unscaled probability weight.
  ///
  /// Probability weight for an edge in the graph (including the
  /// successor/target node).
  ///
  /// All edges in the original function are 32-bit.  However, exit edges from
  /// loop packages are taken from 64-bit exit masses, so we need 64-bits of
  /// space in general.
  ///
  /// In addition to the raw weight amount, Weight stores the type of the edge
  /// in the current context (i.e., the context of the loop being processed).
  /// Is this a local edge within the loop, an exit from the loop, or a
  /// backedge to the loop header?
  struct Weight {
    enum DistType { Local, Exit, Backedge };
    DistType Type = Local;
    BlockNode TargetNode;
    uint64_t Amount = 0;

    Weight() = default;
    Weight(DistType Type, BlockNode TargetNode, uint64_t Amount)
        : Type(Type), TargetNode(TargetNode), Amount(Amount) {}
  };

  /// Distribution of unscaled probability weight.
  ///
  /// Distribution of unscaled probability weight to a set of successors.
  ///
  /// This class collates the successor edge weights for later processing.
  ///
  /// \a DidOverflow indicates whether \a Total did overflow while adding to
  /// the distribution.  It should never overflow twice.
  struct Distribution {
    using WeightList = SmallVector<Weight, 4>;

    WeightList Weights;       ///< Individual successor weights.
    uint64_t Total = 0;       ///< Sum of all weights.
    bool DidOverflow = false; ///< Whether \a Total did overflow.

    Distribution() = default;

    void addLocal(const BlockNode &Node, uint64_t Amount) {
      add(Node, Amount, Weight::Local);
    }

    void addExit(const BlockNode &Node, uint64_t Amount) {
      add(Node, Amount, Weight::Exit);
    }

    void addBackedge(const BlockNode &Node, uint64_t Amount) {
      add(Node, Amount, Weight::Backedge);
    }

    /// Normalize the distribution.
    ///
    /// Combines multiple edges to the same \a Weight::TargetNode and scales
    /// down so that \a Total fits into 32-bits.
    ///
    /// This is linear in the size of \a Weights.  For the vast majority of
    /// cases, adjacent edge weights are combined by sorting WeightList and
    /// combining adjacent weights.  However, for very large edge lists an
    /// auxiliary hash table is used.
    void normalize();

  private:
    void add(const BlockNode &Node, uint64_t Amount, Weight::DistType Type);
  };

  /// Data about each block.  This is used downstream.
  std::vector<FrequencyData> Freqs;

  /// Whether each block is an irreducible loop header.
  /// This is used downstream.
  SparseBitVector<> IsIrrLoopHeader;

  /// Loop data: see initializeLoops().
  std::vector<WorkingData> Working;

  /// Indexed information about loops.
  std::list<LoopData> Loops;

  /// Virtual destructor.
  ///
  /// Need a virtual destructor to mask the compiler warning about
  /// getBlockName().
  virtual ~BlockFrequencyInfoImplBase() = default;

  /// Add all edges out of a packaged loop to the distribution.
  ///
  /// Adds all edges from LocalLoopHead to Dist.  Calls addToDist() to add each
  /// successor edge.
  ///
  /// \return \c true unless there's an irreducible backedge.
  bool addLoopSuccessorsToDist(const LoopData *OuterLoop, LoopData &Loop,
                               Distribution &Dist);

  /// Add an edge to the distribution.
  ///
  /// Adds an edge to Succ to Dist.  If \c LoopHead.isValid(), then whether the
  /// edge is local/exit/backedge is in the context of LoopHead.  Otherwise,
  /// every edge should be a local edge (since all the loops are packaged up).
  ///
  /// \return \c true unless aborted due to an irreducible backedge.
  bool addToDist(Distribution &Dist, const LoopData *OuterLoop,
                 const BlockNode &Pred, const BlockNode &Succ, uint64_t Weight);

  LoopData &getLoopPackage(const BlockNode &Head) {
    assert(Head.Index < Working.size());
    assert(Working[Head.Index].isLoopHeader());
    return *Working[Head.Index].Loop;
  }

  /// Analyze irreducible SCCs.
  ///
  /// Separate irreducible SCCs from \c G, which is an explict graph of \c
  /// OuterLoop (or the top-level function, if \c OuterLoop is \c nullptr).
  /// Insert them into \a Loops before \c Insert.
  ///
  /// \return the \c LoopData nodes representing the irreducible SCCs.
  iterator_range<std::list<LoopData>::iterator>
  analyzeIrreducible(const bfi_detail::IrreducibleGraph &G, LoopData *OuterLoop,
                     std::list<LoopData>::iterator Insert);

  /// Update a loop after packaging irreducible SCCs inside of it.
  ///
  /// Update \c OuterLoop.  Before finding irreducible control flow, it was
  /// partway through \a computeMassInLoop(), so \a LoopData::Exits and \a
  /// LoopData::BackedgeMass need to be reset.  Also, nodes that were packaged
  /// up need to be removed from \a OuterLoop::Nodes.
  void updateLoopWithIrreducible(LoopData &OuterLoop);

  /// Distribute mass according to a distribution.
  ///
  /// Distributes the mass in Source according to Dist.  If LoopHead.isValid(),
  /// backedges and exits are stored in its entry in Loops.
  ///
  /// Mass is distributed in parallel from two copies of the source mass.
  void distributeMass(const BlockNode &Source, LoopData *OuterLoop,
                      Distribution &Dist);

  /// Compute the loop scale for a loop.
  void computeLoopScale(LoopData &Loop);

  /// Adjust the mass of all headers in an irreducible loop.
  ///
  /// Initially, irreducible loops are assumed to distribute their mass
  /// equally among its headers. This can lead to wrong frequency estimates
  /// since some headers may be executed more frequently than others.
  ///
  /// This adjusts header mass distribution so it matches the weights of
  /// the backedges going into each of the loop headers.
  void adjustLoopHeaderMass(LoopData &Loop);

  void distributeIrrLoopHeaderMass(Distribution &Dist);

  /// Package up a loop.
  void packageLoop(LoopData &Loop);

  /// Unwrap loops.
  void unwrapLoops();

  /// Finalize frequency metrics.
  ///
  /// Calculates final frequencies and cleans up no-longer-needed data
  /// structures.
  void finalizeMetrics();

  /// Clear all memory.
  void clear();

  virtual std::string getBlockName(const BlockNode &Node) const;
  std::string getLoopName(const LoopData &Loop) const;

  virtual raw_ostream &print(raw_ostream &OS) const { return OS; }
  void dump() const { print(dbgs()); }

  Scaled64 getFloatingBlockFreq(const BlockNode &Node) const;

  BlockFrequency getBlockFreq(const BlockNode &Node) const;
  Optional<uint64_t> getBlockProfileCount(const Function &F,
                                          const BlockNode &Node) const;
  Optional<uint64_t> getProfileCountFromFreq(const Function &F,
                                             uint64_t Freq) const;
  bool isIrrLoopHeader(const BlockNode &Node);

  void setBlockFreq(const BlockNode &Node, uint64_t Freq);

  raw_ostream &printBlockFreq(raw_ostream &OS, const BlockNode &Node) const;
  raw_ostream &printBlockFreq(raw_ostream &OS,
                              const BlockFrequency &Freq) const;

  uint64_t getEntryFreq() const {
    assert(!Freqs.empty());
    return Freqs[0].Integer;
  }
};

namespace bfi_detail {

template <class BlockT> struct TypeMap {};
template <> struct TypeMap<BasicBlock> {
  using BlockT = BasicBlock;
  using FunctionT = Function;
  using BranchProbabilityInfoT = BranchProbabilityInfo;
  using LoopT = Loop;
  using LoopInfoT = LoopInfo;
};
template <> struct TypeMap<MachineBasicBlock> {
  using BlockT = MachineBasicBlock;
  using FunctionT = MachineFunction;
  using BranchProbabilityInfoT = MachineBranchProbabilityInfo;
  using LoopT = MachineLoop;
  using LoopInfoT = MachineLoopInfo;
};

/// Get the name of a MachineBasicBlock.
///
/// Get the name of a MachineBasicBlock.  It's templated so that including from
/// CodeGen is unnecessary (that would be a layering issue).
///
/// This is used mainly for debug output.  The name is similar to
/// MachineBasicBlock::getFullName(), but skips the name of the function.
template <class BlockT> std::string getBlockName(const BlockT *BB) {
  assert(BB && "Unexpected nullptr");
  auto MachineName = "BB" + Twine(BB->getNumber());
  if (BB->getBasicBlock())
    return (MachineName + "[" + BB->getName() + "]").str();
  return MachineName.str();
}
/// Get the name of a BasicBlock.
template <> inline std::string getBlockName(const BasicBlock *BB) {
  assert(BB && "Unexpected nullptr");
  return BB->getName().str();
}

/// Graph of irreducible control flow.
///
/// This graph is used for determining the SCCs in a loop (or top-level
/// function) that has irreducible control flow.
///
/// During the block frequency algorithm, the local graphs are defined in a
/// light-weight way, deferring to the \a BasicBlock or \a MachineBasicBlock
/// graphs for most edges, but getting others from \a LoopData::ExitMap.  The
/// latter only has successor information.
///
/// \a IrreducibleGraph makes this graph explicit.  It's in a form that can use
/// \a GraphTraits (so that \a analyzeIrreducible() can use \a scc_iterator),
/// and it explicitly lists predecessors and successors.  The initialization
/// that relies on \c MachineBasicBlock is defined in the header.
struct IrreducibleGraph {
  using BFIBase = BlockFrequencyInfoImplBase;

  BFIBase &BFI;

  using BlockNode = BFIBase::BlockNode;
  struct IrrNode {
    BlockNode Node;
    unsigned NumIn = 0;
    std::deque<const IrrNode *> Edges;

    IrrNode(const BlockNode &Node) : Node(Node) {}

    using iterator = std::deque<const IrrNode *>::const_iterator;

    iterator pred_begin() const { return Edges.begin(); }
    iterator succ_begin() const { return Edges.begin() + NumIn; }
    iterator pred_end() const { return succ_begin(); }
    iterator succ_end() const { return Edges.end(); }
  };
  BlockNode Start;
  const IrrNode *StartIrr = nullptr;
  std::vector<IrrNode> Nodes;
  SmallDenseMap<uint32_t, IrrNode *, 4> Lookup;

  /// Construct an explicit graph containing irreducible control flow.
  ///
  /// Construct an explicit graph of the control flow in \c OuterLoop (or the
  /// top-level function, if \c OuterLoop is \c nullptr).  Uses \c
  /// addBlockEdges to add block successors that have not been packaged into
  /// loops.
  ///
  /// \a BlockFrequencyInfoImpl::computeIrreducibleMass() is the only expected
  /// user of this.
  template <class BlockEdgesAdder>
  IrreducibleGraph(BFIBase &BFI, const BFIBase::LoopData *OuterLoop,
                   BlockEdgesAdder addBlockEdges) : BFI(BFI) {
    initialize(OuterLoop, addBlockEdges);
  }

  template <class BlockEdgesAdder>
  void initialize(const BFIBase::LoopData *OuterLoop,
                  BlockEdgesAdder addBlockEdges);
  void addNodesInLoop(const BFIBase::LoopData &OuterLoop);
  void addNodesInFunction();

  void addNode(const BlockNode &Node) {
    Nodes.emplace_back(Node);
    BFI.Working[Node.Index].getMass() = BlockMass::getEmpty();
  }

  void indexNodes();
  template <class BlockEdgesAdder>
  void addEdges(const BlockNode &Node, const BFIBase::LoopData *OuterLoop,
                BlockEdgesAdder addBlockEdges);
  void addEdge(IrrNode &Irr, const BlockNode &Succ,
               const BFIBase::LoopData *OuterLoop);
};

template <class BlockEdgesAdder>
void IrreducibleGraph::initialize(const BFIBase::LoopData *OuterLoop,
                                  BlockEdgesAdder addBlockEdges) {
  if (OuterLoop) {
    addNodesInLoop(*OuterLoop);
    for (auto N : OuterLoop->Nodes)
      addEdges(N, OuterLoop, addBlockEdges);
  } else {
    addNodesInFunction();
    for (uint32_t Index = 0; Index < BFI.Working.size(); ++Index)
      addEdges(Index, OuterLoop, addBlockEdges);
  }
  StartIrr = Lookup[Start.Index];
}

template <class BlockEdgesAdder>
void IrreducibleGraph::addEdges(const BlockNode &Node,
                                const BFIBase::LoopData *OuterLoop,
                                BlockEdgesAdder addBlockEdges) {
  auto L = Lookup.find(Node.Index);
  if (L == Lookup.end())
    return;
  IrrNode &Irr = *L->second;
  const auto &Working = BFI.Working[Node.Index];

  if (Working.isAPackage())
    for (const auto &I : Working.Loop->Exits)
      addEdge(Irr, I.first, OuterLoop);
  else
    addBlockEdges(*this, Irr, OuterLoop);
}

} // end namespace bfi_detail

/// Shared implementation for block frequency analysis.
///
/// This is a shared implementation of BlockFrequencyInfo and
/// MachineBlockFrequencyInfo, and calculates the relative frequencies of
/// blocks.
///
/// LoopInfo defines a loop as a "non-trivial" SCC dominated by a single block,
/// which is called the header.  A given loop, L, can have sub-loops, which are
/// loops within the subgraph of L that exclude its header.  (A "trivial" SCC
/// consists of a single block that does not have a self-edge.)
///
/// In addition to loops, this algorithm has limited support for irreducible
/// SCCs, which are SCCs with multiple entry blocks.  Irreducible SCCs are
/// discovered on they fly, and modelled as loops with multiple headers.
///
/// The headers of irreducible sub-SCCs consist of its entry blocks and all
/// nodes that are targets of a backedge within it (excluding backedges within
/// true sub-loops).  Block frequency calculations act as if a block is
/// inserted that intercepts all the edges to the headers.  All backedges and
/// entries point to this block.  Its successors are the headers, which split
/// the frequency evenly.
///
/// This algorithm leverages BlockMass and ScaledNumber to maintain precision,
/// separates mass distribution from loop scaling, and dithers to eliminate
/// probability mass loss.
///
/// The implementation is split between BlockFrequencyInfoImpl, which knows the
/// type of graph being modelled (BasicBlock vs. MachineBasicBlock), and
/// BlockFrequencyInfoImplBase, which doesn't.  The base class uses \a
/// BlockNode, a wrapper around a uint32_t.  BlockNode is numbered from 0 in
/// reverse-post order.  This gives two advantages:  it's easy to compare the
/// relative ordering of two nodes, and maps keyed on BlockT can be represented
/// by vectors.
///
/// This algorithm is O(V+E), unless there is irreducible control flow, in
/// which case it's O(V*E) in the worst case.
///
/// These are the main stages:
///
///  0. Reverse post-order traversal (\a initializeRPOT()).
///
///     Run a single post-order traversal and save it (in reverse) in RPOT.
///     All other stages make use of this ordering.  Save a lookup from BlockT
///     to BlockNode (the index into RPOT) in Nodes.
///
///  1. Loop initialization (\a initializeLoops()).
///
///     Translate LoopInfo/MachineLoopInfo into a form suitable for the rest of
///     the algorithm.  In particular, store the immediate members of each loop
///     in reverse post-order.
///
///  2. Calculate mass and scale in loops (\a computeMassInLoops()).
///
///     For each loop (bottom-up), distribute mass through the DAG resulting
///     from ignoring backedges and treating sub-loops as a single pseudo-node.
///     Track the backedge mass distributed to the loop header, and use it to
///     calculate the loop scale (number of loop iterations).  Immediate
///     members that represent sub-loops will already have been visited and
///     packaged into a pseudo-node.
///
///     Distributing mass in a loop is a reverse-post-order traversal through
///     the loop.  Start by assigning full mass to the Loop header.  For each
///     node in the loop:
///
///         - Fetch and categorize the weight distribution for its successors.
///           If this is a packaged-subloop, the weight distribution is stored
///           in \a LoopData::Exits.  Otherwise, fetch it from
///           BranchProbabilityInfo.
///
///         - Each successor is categorized as \a Weight::Local, a local edge
///           within the current loop, \a Weight::Backedge, a backedge to the
///           loop header, or \a Weight::Exit, any successor outside the loop.
///           The weight, the successor, and its category are stored in \a
///           Distribution.  There can be multiple edges to each successor.
///
///         - If there's a backedge to a non-header, there's an irreducible SCC.
///           The usual flow is temporarily aborted.  \a
///           computeIrreducibleMass() finds the irreducible SCCs within the
///           loop, packages them up, and restarts the flow.
///
///         - Normalize the distribution:  scale weights down so that their sum
///           is 32-bits, and coalesce multiple edges to the same node.
///
///         - Distribute the mass accordingly, dithering to minimize mass loss,
///           as described in \a distributeMass().
///
///     In the case of irreducible loops, instead of a single loop header,
///     there will be several. The computation of backedge masses is similar
///     but instead of having a single backedge mass, there will be one
///     backedge per loop header. In these cases, each backedge will carry
///     a mass proportional to the edge weights along the corresponding
///     path.
///
///     At the end of propagation, the full mass assigned to the loop will be
///     distributed among the loop headers proportionally according to the
///     mass flowing through their backedges.
///
///     Finally, calculate the loop scale from the accumulated backedge mass.
///
///  3. Distribute mass in the function (\a computeMassInFunction()).
///
///     Finally, distribute mass through the DAG resulting from packaging all
///     loops in the function.  This uses the same algorithm as distributing
///     mass in a loop, except that there are no exit or backedge edges.
///
///  4. Unpackage loops (\a unwrapLoops()).
///
///     Initialize each block's frequency to a floating point representation of
///     its mass.
///
///     Visit loops top-down, scaling the frequencies of its immediate members
///     by the loop's pseudo-node's frequency.
///
///  5. Convert frequencies to a 64-bit range (\a finalizeMetrics()).
///
///     Using the min and max frequencies as a guide, translate floating point
///     frequencies to an appropriate range in uint64_t.
///
/// It has some known flaws.
///
///   - The model of irreducible control flow is a rough approximation.
///
///     Modelling irreducible control flow exactly involves setting up and
///     solving a group of infinite geometric series.  Such precision is
///     unlikely to be worthwhile, since most of our algorithms give up on
///     irreducible control flow anyway.
///
///     Nevertheless, we might find that we need to get closer.  Here's a sort
///     of TODO list for the model with diminishing returns, to be completed as
///     necessary.
///
///       - The headers for the \a LoopData representing an irreducible SCC
///         include non-entry blocks.  When these extra blocks exist, they
///         indicate a self-contained irreducible sub-SCC.  We could treat them
///         as sub-loops, rather than arbitrarily shoving the problematic
///         blocks into the headers of the main irreducible SCC.
///
///       - Entry frequencies are assumed to be evenly split between the
///         headers of a given irreducible SCC, which is the only option if we
///         need to compute mass in the SCC before its parent loop.  Instead,
///         we could partially compute mass in the parent loop, and stop when
///         we get to the SCC.  Here, we have the correct ratio of entry
///         masses, which we can use to adjust their relative frequencies.
///         Compute mass in the SCC, and then continue propagation in the
///         parent.
///
///       - We can propagate mass iteratively through the SCC, for some fixed
///         number of iterations.  Each iteration starts by assigning the entry
///         blocks their backedge mass from the prior iteration.  The final
///         mass for each block (and each exit, and the total backedge mass
///         used for computing loop scale) is the sum of all iterations.
///         (Running this until fixed point would "solve" the geometric
///         series by simulation.)
template <class BT> class BlockFrequencyInfoImpl : BlockFrequencyInfoImplBase {
  // This is part of a workaround for a GCC 4.7 crash on lambdas.
  friend struct bfi_detail::BlockEdgesAdder<BT>;

  using BlockT = typename bfi_detail::TypeMap<BT>::BlockT;
  using FunctionT = typename bfi_detail::TypeMap<BT>::FunctionT;
  using BranchProbabilityInfoT =
      typename bfi_detail::TypeMap<BT>::BranchProbabilityInfoT;
  using LoopT = typename bfi_detail::TypeMap<BT>::LoopT;
  using LoopInfoT = typename bfi_detail::TypeMap<BT>::LoopInfoT;
  using Successor = GraphTraits<const BlockT *>;
  using Predecessor = GraphTraits<Inverse<const BlockT *>>;

  const BranchProbabilityInfoT *BPI = nullptr;
  const LoopInfoT *LI = nullptr;
  const FunctionT *F = nullptr;

  // All blocks in reverse postorder.
  std::vector<const BlockT *> RPOT;
  DenseMap<const BlockT *, BlockNode> Nodes;

  using rpot_iterator = typename std::vector<const BlockT *>::const_iterator;

  rpot_iterator rpot_begin() const { return RPOT.begin(); }
  rpot_iterator rpot_end() const { return RPOT.end(); }

  size_t getIndex(const rpot_iterator &I) const { return I - rpot_begin(); }

  BlockNode getNode(const rpot_iterator &I) const {
    return BlockNode(getIndex(I));
  }
  BlockNode getNode(const BlockT *BB) const { return Nodes.lookup(BB); }

  const BlockT *getBlock(const BlockNode &Node) const {
    assert(Node.Index < RPOT.size());
    return RPOT[Node.Index];
  }

  /// Run (and save) a post-order traversal.
  ///
  /// Saves a reverse post-order traversal of all the nodes in \a F.
  void initializeRPOT();

  /// Initialize loop data.
  ///
  /// Build up \a Loops using \a LoopInfo.  \a LoopInfo gives us a mapping from
  /// each block to the deepest loop it's in, but we need the inverse.  For each
  /// loop, we store in reverse post-order its "immediate" members, defined as
  /// the header, the headers of immediate sub-loops, and all other blocks in
  /// the loop that are not in sub-loops.
  void initializeLoops();

  /// Propagate to a block's successors.
  ///
  /// In the context of distributing mass through \c OuterLoop, divide the mass
  /// currently assigned to \c Node between its successors.
  ///
  /// \return \c true unless there's an irreducible backedge.
  bool propagateMassToSuccessors(LoopData *OuterLoop, const BlockNode &Node);

  /// Compute mass in a particular loop.
  ///
  /// Assign mass to \c Loop's header, and then for each block in \c Loop in
  /// reverse post-order, distribute mass to its successors.  Only visits nodes
  /// that have not been packaged into sub-loops.
  ///
  /// \pre \a computeMassInLoop() has been called for each subloop of \c Loop.
  /// \return \c true unless there's an irreducible backedge.
  bool computeMassInLoop(LoopData &Loop);

  /// Try to compute mass in the top-level function.
  ///
  /// Assign mass to the entry block, and then for each block in reverse
  /// post-order, distribute mass to its successors.  Skips nodes that have
  /// been packaged into loops.
  ///
  /// \pre \a computeMassInLoops() has been called.
  /// \return \c true unless there's an irreducible backedge.
  bool tryToComputeMassInFunction();

  /// Compute mass in (and package up) irreducible SCCs.
  ///
  /// Find the irreducible SCCs in \c OuterLoop, add them to \a Loops (in front
  /// of \c Insert), and call \a computeMassInLoop() on each of them.
  ///
  /// If \c OuterLoop is \c nullptr, it refers to the top-level function.
  ///
  /// \pre \a computeMassInLoop() has been called for each subloop of \c
  /// OuterLoop.
  /// \pre \c Insert points at the last loop successfully processed by \a
  /// computeMassInLoop().
  /// \pre \c OuterLoop has irreducible SCCs.
  void computeIrreducibleMass(LoopData *OuterLoop,
                              std::list<LoopData>::iterator Insert);

  /// Compute mass in all loops.
  ///
  /// For each loop bottom-up, call \a computeMassInLoop().
  ///
  /// \a computeMassInLoop() aborts (and returns \c false) on loops that
  /// contain a irreducible sub-SCCs.  Use \a computeIrreducibleMass() and then
  /// re-enter \a computeMassInLoop().
  ///
  /// \post \a computeMassInLoop() has returned \c true for every loop.
  void computeMassInLoops();

  /// Compute mass in the top-level function.
  ///
  /// Uses \a tryToComputeMassInFunction() and \a computeIrreducibleMass() to
  /// compute mass in the top-level function.
  ///
  /// \post \a tryToComputeMassInFunction() has returned \c true.
  void computeMassInFunction();

  std::string getBlockName(const BlockNode &Node) const override {
    return bfi_detail::getBlockName(getBlock(Node));
  }

public:
  BlockFrequencyInfoImpl() = default;

  const FunctionT *getFunction() const { return F; }

  void calculate(const FunctionT &F, const BranchProbabilityInfoT &BPI,
                 const LoopInfoT &LI);

  using BlockFrequencyInfoImplBase::getEntryFreq;

  BlockFrequency getBlockFreq(const BlockT *BB) const {
    return BlockFrequencyInfoImplBase::getBlockFreq(getNode(BB));
  }

  Optional<uint64_t> getBlockProfileCount(const Function &F,
                                          const BlockT *BB) const {
    return BlockFrequencyInfoImplBase::getBlockProfileCount(F, getNode(BB));
  }

  Optional<uint64_t> getProfileCountFromFreq(const Function &F,
                                             uint64_t Freq) const {
    return BlockFrequencyInfoImplBase::getProfileCountFromFreq(F, Freq);
  }

  bool isIrrLoopHeader(const BlockT *BB) {
    return BlockFrequencyInfoImplBase::isIrrLoopHeader(getNode(BB));
  }

  void setBlockFreq(const BlockT *BB, uint64_t Freq);

  Scaled64 getFloatingBlockFreq(const BlockT *BB) const {
    return BlockFrequencyInfoImplBase::getFloatingBlockFreq(getNode(BB));
  }

  const BranchProbabilityInfoT &getBPI() const { return *BPI; }

  /// Print the frequencies for the current function.
  ///
  /// Prints the frequencies for the blocks in the current function.
  ///
  /// Blocks are printed in the natural iteration order of the function, rather
  /// than reverse post-order.  This provides two advantages:  writing -analyze
  /// tests is easier (since blocks come out in source order), and even
  /// unreachable blocks are printed.
  ///
  /// \a BlockFrequencyInfoImplBase::print() only knows reverse post-order, so
  /// we need to override it here.
  raw_ostream &print(raw_ostream &OS) const override;

  using BlockFrequencyInfoImplBase::dump;
  using BlockFrequencyInfoImplBase::printBlockFreq;

  raw_ostream &printBlockFreq(raw_ostream &OS, const BlockT *BB) const {
    return BlockFrequencyInfoImplBase::printBlockFreq(OS, getNode(BB));
  }
};

template <class BT>
void BlockFrequencyInfoImpl<BT>::calculate(const FunctionT &F,
                                           const BranchProbabilityInfoT &BPI,
                                           const LoopInfoT &LI) {
  // Save the parameters.
  this->BPI = &BPI;
  this->LI = &LI;
  this->F = &F;

  // Clean up left-over data structures.
  BlockFrequencyInfoImplBase::clear();
  RPOT.clear();
  Nodes.clear();

  // Initialize.
  LLVM_DEBUG(dbgs() << "\nblock-frequency: " << F.getName()
                    << "\n================="
                    << std::string(F.getName().size(), '=') << "\n");
  initializeRPOT();
  initializeLoops();

  // Visit loops in post-order to find the local mass distribution, and then do
  // the full function.
  computeMassInLoops();
  computeMassInFunction();
  unwrapLoops();
  finalizeMetrics();
}

template <class BT>
void BlockFrequencyInfoImpl<BT>::setBlockFreq(const BlockT *BB, uint64_t Freq) {
  if (Nodes.count(BB))
    BlockFrequencyInfoImplBase::setBlockFreq(getNode(BB), Freq);
  else {
    // If BB is a newly added block after BFI is done, we need to create a new
    // BlockNode for it assigned with a new index. The index can be determined
    // by the size of Freqs.
    BlockNode NewNode(Freqs.size());
    Nodes[BB] = NewNode;
    Freqs.emplace_back();
    BlockFrequencyInfoImplBase::setBlockFreq(NewNode, Freq);
  }
}

template <class BT> void BlockFrequencyInfoImpl<BT>::initializeRPOT() {
  const BlockT *Entry = &F->front();
  RPOT.reserve(F->size());
  std::copy(po_begin(Entry), po_end(Entry), std::back_inserter(RPOT));
  std::reverse(RPOT.begin(), RPOT.end());

  assert(RPOT.size() - 1 <= BlockNode::getMaxIndex() &&
         "More nodes in function than Block Frequency Info supports");

  LLVM_DEBUG(dbgs() << "reverse-post-order-traversal\n");
  for (rpot_iterator I = rpot_begin(), E = rpot_end(); I != E; ++I) {
    BlockNode Node = getNode(I);
    LLVM_DEBUG(dbgs() << " - " << getIndex(I) << ": " << getBlockName(Node)
                      << "\n");
    Nodes[*I] = Node;
  }

  Working.reserve(RPOT.size());
  for (size_t Index = 0; Index < RPOT.size(); ++Index)
    Working.emplace_back(Index);
  Freqs.resize(RPOT.size());
}

template <class BT> void BlockFrequencyInfoImpl<BT>::initializeLoops() {
  LLVM_DEBUG(dbgs() << "loop-detection\n");
  if (LI->empty())
    return;

  // Visit loops top down and assign them an index.
  std::deque<std::pair<const LoopT *, LoopData *>> Q;
  for (const LoopT *L : *LI)
    Q.emplace_back(L, nullptr);
  while (!Q.empty()) {
    const LoopT *Loop = Q.front().first;
    LoopData *Parent = Q.front().second;
    Q.pop_front();

    BlockNode Header = getNode(Loop->getHeader());
    assert(Header.isValid());

    Loops.emplace_back(Parent, Header);
    Working[Header.Index].Loop = &Loops.back();
    LLVM_DEBUG(dbgs() << " - loop = " << getBlockName(Header) << "\n");

    for (const LoopT *L : *Loop)
      Q.emplace_back(L, &Loops.back());
  }

  // Visit nodes in reverse post-order and add them to their deepest containing
  // loop.
  for (size_t Index = 0; Index < RPOT.size(); ++Index) {
    // Loop headers have already been mostly mapped.
    if (Working[Index].isLoopHeader()) {
      LoopData *ContainingLoop = Working[Index].getContainingLoop();
      if (ContainingLoop)
        ContainingLoop->Nodes.push_back(Index);
      continue;
    }

    const LoopT *Loop = LI->getLoopFor(RPOT[Index]);
    if (!Loop)
      continue;

    // Add this node to its containing loop's member list.
    BlockNode Header = getNode(Loop->getHeader());
    assert(Header.isValid());
    const auto &HeaderData = Working[Header.Index];
    assert(HeaderData.isLoopHeader());

    Working[Index].Loop = HeaderData.Loop;
    HeaderData.Loop->Nodes.push_back(Index);
    LLVM_DEBUG(dbgs() << " - loop = " << getBlockName(Header)
                      << ": member = " << getBlockName(Index) << "\n");
  }
}

template <class BT> void BlockFrequencyInfoImpl<BT>::computeMassInLoops() {
  // Visit loops with the deepest first, and the top-level loops last.
  for (auto L = Loops.rbegin(), E = Loops.rend(); L != E; ++L) {
    if (computeMassInLoop(*L))
      continue;
    auto Next = std::next(L);
    computeIrreducibleMass(&*L, L.base());
    L = std::prev(Next);
    if (computeMassInLoop(*L))
      continue;
    llvm_unreachable("unhandled irreducible control flow");
  }
}

template <class BT>
bool BlockFrequencyInfoImpl<BT>::computeMassInLoop(LoopData &Loop) {
  // Compute mass in loop.
  LLVM_DEBUG(dbgs() << "compute-mass-in-loop: " << getLoopName(Loop) << "\n");

  if (Loop.isIrreducible()) {
    LLVM_DEBUG(dbgs() << "isIrreducible = true\n");
    Distribution Dist;
    unsigned NumHeadersWithWeight = 0;
    Optional<uint64_t> MinHeaderWeight;
    DenseSet<uint32_t> HeadersWithoutWeight;
    HeadersWithoutWeight.reserve(Loop.NumHeaders);
    for (uint32_t H = 0; H < Loop.NumHeaders; ++H) {
      auto &HeaderNode = Loop.Nodes[H];
      const BlockT *Block = getBlock(HeaderNode);
      IsIrrLoopHeader.set(Loop.Nodes[H].Index);
      Optional<uint64_t> HeaderWeight = Block->getIrrLoopHeaderWeight();
      if (!HeaderWeight) {
        LLVM_DEBUG(dbgs() << "Missing irr loop header metadata on "
                          << getBlockName(HeaderNode) << "\n");
        HeadersWithoutWeight.insert(H);
        continue;
      }
      LLVM_DEBUG(dbgs() << getBlockName(HeaderNode)
                        << " has irr loop header weight "
                        << HeaderWeight.getValue() << "\n");
      NumHeadersWithWeight++;
      uint64_t HeaderWeightValue = HeaderWeight.getValue();
      if (!MinHeaderWeight || HeaderWeightValue < MinHeaderWeight)
        MinHeaderWeight = HeaderWeightValue;
      if (HeaderWeightValue) {
        Dist.addLocal(HeaderNode, HeaderWeightValue);
      }
    }
    // As a heuristic, if some headers don't have a weight, give them the
    // minimium weight seen (not to disrupt the existing trends too much by
    // using a weight that's in the general range of the other headers' weights,
    // and the minimum seems to perform better than the average.)
    // FIXME: better update in the passes that drop the header weight.
    // If no headers have a weight, give them even weight (use weight 1).
    if (!MinHeaderWeight)
      MinHeaderWeight = 1;
    for (uint32_t H : HeadersWithoutWeight) {
      auto &HeaderNode = Loop.Nodes[H];
      assert(!getBlock(HeaderNode)->getIrrLoopHeaderWeight() &&
             "Shouldn't have a weight metadata");
      uint64_t MinWeight = MinHeaderWeight.getValue();
      LLVM_DEBUG(dbgs() << "Giving weight " << MinWeight << " to "
                        << getBlockName(HeaderNode) << "\n");
      if (MinWeight)
        Dist.addLocal(HeaderNode, MinWeight);
    }
    distributeIrrLoopHeaderMass(Dist);
    for (const BlockNode &M : Loop.Nodes)
      if (!propagateMassToSuccessors(&Loop, M))
        llvm_unreachable("unhandled irreducible control flow");
    if (NumHeadersWithWeight == 0)
      // No headers have a metadata. Adjust header mass.
      adjustLoopHeaderMass(Loop);
  } else {
    Working[Loop.getHeader().Index].getMass() = BlockMass::getFull();
    if (!propagateMassToSuccessors(&Loop, Loop.getHeader()))
      llvm_unreachable("irreducible control flow to loop header!?");
    for (const BlockNode &M : Loop.members())
      if (!propagateMassToSuccessors(&Loop, M))
        // Irreducible backedge.
        return false;
  }

  computeLoopScale(Loop);
  packageLoop(Loop);
  return true;
}

template <class BT>
bool BlockFrequencyInfoImpl<BT>::tryToComputeMassInFunction() {
  // Compute mass in function.
  LLVM_DEBUG(dbgs() << "compute-mass-in-function\n");
  assert(!Working.empty() && "no blocks in function");
  assert(!Working[0].isLoopHeader() && "entry block is a loop header");

  Working[0].getMass() = BlockMass::getFull();
  for (rpot_iterator I = rpot_begin(), IE = rpot_end(); I != IE; ++I) {
    // Check for nodes that have been packaged.
    BlockNode Node = getNode(I);
    if (Working[Node.Index].isPackaged())
      continue;

    if (!propagateMassToSuccessors(nullptr, Node))
      return false;
  }
  return true;
}

template <class BT> void BlockFrequencyInfoImpl<BT>::computeMassInFunction() {
  if (tryToComputeMassInFunction())
    return;
  computeIrreducibleMass(nullptr, Loops.begin());
  if (tryToComputeMassInFunction())
    return;
  llvm_unreachable("unhandled irreducible control flow");
}

/// \note This should be a lambda, but that crashes GCC 4.7.
namespace bfi_detail {

template <class BT> struct BlockEdgesAdder {
  using BlockT = BT;
  using LoopData = BlockFrequencyInfoImplBase::LoopData;
  using Successor = GraphTraits<const BlockT *>;

  const BlockFrequencyInfoImpl<BT> &BFI;

  explicit BlockEdgesAdder(const BlockFrequencyInfoImpl<BT> &BFI)
      : BFI(BFI) {}

  void operator()(IrreducibleGraph &G, IrreducibleGraph::IrrNode &Irr,
                  const LoopData *OuterLoop) {
    const BlockT *BB = BFI.RPOT[Irr.Node.Index];
    for (const auto Succ : children<const BlockT *>(BB))
      G.addEdge(Irr, BFI.getNode(Succ), OuterLoop);
  }
};

} // end namespace bfi_detail

template <class BT>
void BlockFrequencyInfoImpl<BT>::computeIrreducibleMass(
    LoopData *OuterLoop, std::list<LoopData>::iterator Insert) {
  LLVM_DEBUG(dbgs() << "analyze-irreducible-in-";
             if (OuterLoop) dbgs()
             << "loop: " << getLoopName(*OuterLoop) << "\n";
             else dbgs() << "function\n");

  using namespace bfi_detail;

  // Ideally, addBlockEdges() would be declared here as a lambda, but that
  // crashes GCC 4.7.
  BlockEdgesAdder<BT> addBlockEdges(*this);
  IrreducibleGraph G(*this, OuterLoop, addBlockEdges);

  for (auto &L : analyzeIrreducible(G, OuterLoop, Insert))
    computeMassInLoop(L);

  if (!OuterLoop)
    return;
  updateLoopWithIrreducible(*OuterLoop);
}

// A helper function that converts a branch probability into weight.
inline uint32_t getWeightFromBranchProb(const BranchProbability Prob) {
  return Prob.getNumerator();
}

template <class BT>
bool
BlockFrequencyInfoImpl<BT>::propagateMassToSuccessors(LoopData *OuterLoop,
                                                      const BlockNode &Node) {
  LLVM_DEBUG(dbgs() << " - node: " << getBlockName(Node) << "\n");
  // Calculate probability for successors.
  Distribution Dist;
  if (auto *Loop = Working[Node.Index].getPackagedLoop()) {
    assert(Loop != OuterLoop && "Cannot propagate mass in a packaged loop");
    if (!addLoopSuccessorsToDist(OuterLoop, *Loop, Dist))
      // Irreducible backedge.
      return false;
  } else {
    const BlockT *BB = getBlock(Node);
    for (auto SI = GraphTraits<const BlockT *>::child_begin(BB),
              SE = GraphTraits<const BlockT *>::child_end(BB);
         SI != SE; ++SI)
      if (!addToDist(
              Dist, OuterLoop, Node, getNode(*SI),
              getWeightFromBranchProb(BPI->getEdgeProbability(BB, SI))))
        // Irreducible backedge.
        return false;
  }

  // Distribute mass to successors, saving exit and backedge data in the
  // loop header.
  distributeMass(Node, OuterLoop, Dist);
  return true;
}

template <class BT>
raw_ostream &BlockFrequencyInfoImpl<BT>::print(raw_ostream &OS) const {
  if (!F)
    return OS;
  OS << "block-frequency-info: " << F->getName() << "\n";
  for (const BlockT &BB : *F) {
    OS << " - " << bfi_detail::getBlockName(&BB) << ": float = ";
    getFloatingBlockFreq(&BB).print(OS, 5)
        << ", int = " << getBlockFreq(&BB).getFrequency();
    if (Optional<uint64_t> ProfileCount =
        BlockFrequencyInfoImplBase::getBlockProfileCount(
            F->getFunction(), getNode(&BB)))
      OS << ", count = " << ProfileCount.getValue();
    if (Optional<uint64_t> IrrLoopHeaderWeight =
        BB.getIrrLoopHeaderWeight())
      OS << ", irr_loop_header_weight = " << IrrLoopHeaderWeight.getValue();
    OS << "\n";
  }

  // Add an extra newline for readability.
  OS << "\n";
  return OS;
}

// Graph trait base class for block frequency information graph
// viewer.

enum GVDAGType { GVDT_None, GVDT_Fraction, GVDT_Integer, GVDT_Count };

template <class BlockFrequencyInfoT, class BranchProbabilityInfoT>
struct BFIDOTGraphTraitsBase : public DefaultDOTGraphTraits {
  using GTraits = GraphTraits<BlockFrequencyInfoT *>;
  using NodeRef = typename GTraits::NodeRef;
  using EdgeIter = typename GTraits::ChildIteratorType;
  using NodeIter = typename GTraits::nodes_iterator;

  uint64_t MaxFrequency = 0;

  explicit BFIDOTGraphTraitsBase(bool isSimple = false)
      : DefaultDOTGraphTraits(isSimple) {}

  static std::string getGraphName(const BlockFrequencyInfoT *G) {
    return G->getFunction()->getName();
  }

  std::string getNodeAttributes(NodeRef Node, const BlockFrequencyInfoT *Graph,
                                unsigned HotPercentThreshold = 0) {
    std::string Result;
    if (!HotPercentThreshold)
      return Result;

    // Compute MaxFrequency on the fly:
    if (!MaxFrequency) {
      for (NodeIter I = GTraits::nodes_begin(Graph),
                    E = GTraits::nodes_end(Graph);
           I != E; ++I) {
        NodeRef N = *I;
        MaxFrequency =
            std::max(MaxFrequency, Graph->getBlockFreq(N).getFrequency());
      }
    }
    BlockFrequency Freq = Graph->getBlockFreq(Node);
    BlockFrequency HotFreq =
        (BlockFrequency(MaxFrequency) *
         BranchProbability::getBranchProbability(HotPercentThreshold, 100));

    if (Freq < HotFreq)
      return Result;

    raw_string_ostream OS(Result);
    OS << "color=\"red\"";
    OS.flush();
    return Result;
  }

  std::string getNodeLabel(NodeRef Node, const BlockFrequencyInfoT *Graph,
                           GVDAGType GType, int layout_order = -1) {
    std::string Result;
    raw_string_ostream OS(Result);

    if (layout_order != -1)
      OS << Node->getName() << "[" << layout_order << "] : ";
    else
      OS << Node->getName() << " : ";
    switch (GType) {
    case GVDT_Fraction:
      Graph->printBlockFreq(OS, Node);
      break;
    case GVDT_Integer:
      OS << Graph->getBlockFreq(Node).getFrequency();
      break;
    case GVDT_Count: {
      auto Count = Graph->getBlockProfileCount(Node);
      if (Count)
        OS << Count.getValue();
      else
        OS << "Unknown";
      break;
    }
    case GVDT_None:
      llvm_unreachable("If we are not supposed to render a graph we should "
                       "never reach this point.");
    }
    return Result;
  }

  std::string getEdgeAttributes(NodeRef Node, EdgeIter EI,
                                const BlockFrequencyInfoT *BFI,
                                const BranchProbabilityInfoT *BPI,
                                unsigned HotPercentThreshold = 0) {
    std::string Str;
    if (!BPI)
      return Str;

    BranchProbability BP = BPI->getEdgeProbability(Node, EI);
    uint32_t N = BP.getNumerator();
    uint32_t D = BP.getDenominator();
    double Percent = 100.0 * N / D;
    raw_string_ostream OS(Str);
    OS << format("label=\"%.1f%%\"", Percent);

    if (HotPercentThreshold) {
      BlockFrequency EFreq = BFI->getBlockFreq(Node) * BP;
      BlockFrequency HotFreq = BlockFrequency(MaxFrequency) *
                               BranchProbability(HotPercentThreshold, 100);

      if (EFreq >= HotFreq) {
        OS << ",color=\"red\"";
      }
    }

    OS.flush();
    return Str;
  }
};

} // end namespace llvm

#undef DEBUG_TYPE

#endif // LLVM_ANALYSIS_BLOCKFREQUENCYINFOIMPL_H
