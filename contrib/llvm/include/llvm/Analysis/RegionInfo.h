//===- RegionInfo.h - SESE region analysis ----------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// Calculate a program structure tree built out of single entry single exit
// regions.
// The basic ideas are taken from "The Program Structure Tree - Richard Johnson,
// David Pearson, Keshav Pingali - 1994", however enriched with ideas from "The
// Refined Process Structure Tree - Jussi Vanhatalo, Hagen Voelyer, Jana
// Koehler - 2009".
// The algorithm to calculate these data structures however is completely
// different, as it takes advantage of existing information already available
// in (Post)dominace tree and dominance frontier passes. This leads to a simpler
// and in practice hopefully better performing algorithm. The runtime of the
// algorithms described in the papers above are both linear in graph size,
// O(V+E), whereas this algorithm is not, as the dominance frontier information
// itself is not, but in practice runtime seems to be in the order of magnitude
// of dominance tree calculation.
//
// WARNING: LLVM is generally very concerned about compile time such that
//          the use of additional analysis passes in the default
//          optimization sequence is avoided as much as possible.
//          Specifically, if you do not need the RegionInfo, but dominance
//          information could be sufficient please base your work only on
//          the dominator tree. Most passes maintain it, such that using
//          it has often near zero cost. In contrast RegionInfo is by
//          default not available, is not maintained by existing
//          transformations and there is no intention to do so.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_ANALYSIS_REGIONINFO_H
#define LLVM_ANALYSIS_REGIONINFO_H

#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/DepthFirstIterator.h"
#include "llvm/ADT/GraphTraits.h"
#include "llvm/ADT/PointerIntPair.h"
#include "llvm/ADT/iterator_range.h"
#include "llvm/Config/llvm-config.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/Dominators.h"
#include "llvm/IR/PassManager.h"
#include "llvm/Pass.h"
#include "llvm/Support/raw_ostream.h"
#include <algorithm>
#include <cassert>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <type_traits>
#include <vector>

namespace llvm {

class DominanceFrontier;
class DominatorTree;
class Loop;
class LoopInfo;
class PostDominatorTree;
class Region;
template <class RegionTr> class RegionBase;
class RegionInfo;
template <class RegionTr> class RegionInfoBase;
class RegionNode;

// Class to be specialized for different users of RegionInfo
// (i.e. BasicBlocks or MachineBasicBlocks). This is only to avoid needing to
// pass around an unreasonable number of template parameters.
template <class FuncT_>
struct RegionTraits {
  // FuncT
  // BlockT
  // RegionT
  // RegionNodeT
  // RegionInfoT
  using BrokenT = typename FuncT_::UnknownRegionTypeError;
};

template <>
struct RegionTraits<Function> {
  using FuncT = Function;
  using BlockT = BasicBlock;
  using RegionT = Region;
  using RegionNodeT = RegionNode;
  using RegionInfoT = RegionInfo;
  using DomTreeT = DominatorTree;
  using DomTreeNodeT = DomTreeNode;
  using DomFrontierT = DominanceFrontier;
  using PostDomTreeT = PostDominatorTree;
  using InstT = Instruction;
  using LoopT = Loop;
  using LoopInfoT = LoopInfo;

  static unsigned getNumSuccessors(BasicBlock *BB) {
    return BB->getTerminator()->getNumSuccessors();
  }
};

/// Marker class to iterate over the elements of a Region in flat mode.
///
/// The class is used to either iterate in Flat mode or by not using it to not
/// iterate in Flat mode.  During a Flat mode iteration all Regions are entered
/// and the iteration returns every BasicBlock.  If the Flat mode is not
/// selected for SubRegions just one RegionNode containing the subregion is
/// returned.
template <class GraphType>
class FlatIt {};

/// A RegionNode represents a subregion or a BasicBlock that is part of a
/// Region.
template <class Tr>
class RegionNodeBase {
  friend class RegionBase<Tr>;

public:
  using BlockT = typename Tr::BlockT;
  using RegionT = typename Tr::RegionT;

private:
  /// This is the entry basic block that starts this region node.  If this is a
  /// BasicBlock RegionNode, then entry is just the basic block, that this
  /// RegionNode represents.  Otherwise it is the entry of this (Sub)RegionNode.
  ///
  /// In the BBtoRegionNode map of the parent of this node, BB will always map
  /// to this node no matter which kind of node this one is.
  ///
  /// The node can hold either a Region or a BasicBlock.
  /// Use one bit to save, if this RegionNode is a subregion or BasicBlock
  /// RegionNode.
  PointerIntPair<BlockT *, 1, bool> entry;

  /// The parent Region of this RegionNode.
  /// @see getParent()
  RegionT *parent;

protected:
  /// Create a RegionNode.
  ///
  /// @param Parent      The parent of this RegionNode.
  /// @param Entry       The entry BasicBlock of the RegionNode.  If this
  ///                    RegionNode represents a BasicBlock, this is the
  ///                    BasicBlock itself.  If it represents a subregion, this
  ///                    is the entry BasicBlock of the subregion.
  /// @param isSubRegion If this RegionNode represents a SubRegion.
  inline RegionNodeBase(RegionT *Parent, BlockT *Entry,
                        bool isSubRegion = false)
      : entry(Entry, isSubRegion), parent(Parent) {}

public:
  RegionNodeBase(const RegionNodeBase &) = delete;
  RegionNodeBase &operator=(const RegionNodeBase &) = delete;

  /// Get the parent Region of this RegionNode.
  ///
  /// The parent Region is the Region this RegionNode belongs to. If for
  /// example a BasicBlock is element of two Regions, there exist two
  /// RegionNodes for this BasicBlock. Each with the getParent() function
  /// pointing to the Region this RegionNode belongs to.
  ///
  /// @return Get the parent Region of this RegionNode.
  inline RegionT *getParent() const { return parent; }

  /// Get the entry BasicBlock of this RegionNode.
  ///
  /// If this RegionNode represents a BasicBlock this is just the BasicBlock
  /// itself, otherwise we return the entry BasicBlock of the Subregion
  ///
  /// @return The entry BasicBlock of this RegionNode.
  inline BlockT *getEntry() const { return entry.getPointer(); }

  /// Get the content of this RegionNode.
  ///
  /// This can be either a BasicBlock or a subregion. Before calling getNodeAs()
  /// check the type of the content with the isSubRegion() function call.
  ///
  /// @return The content of this RegionNode.
  template <class T> inline T *getNodeAs() const;

  /// Is this RegionNode a subregion?
  ///
  /// @return True if it contains a subregion. False if it contains a
  ///         BasicBlock.
  inline bool isSubRegion() const { return entry.getInt(); }
};

//===----------------------------------------------------------------------===//
/// A single entry single exit Region.
///
/// A Region is a connected subgraph of a control flow graph that has exactly
/// two connections to the remaining graph. It can be used to analyze or
/// optimize parts of the control flow graph.
///
/// A <em> simple Region </em> is connected to the remaining graph by just two
/// edges. One edge entering the Region and another one leaving the Region.
///
/// An <em> extended Region </em> (or just Region) is a subgraph that can be
/// transform into a simple Region. The transformation is done by adding
/// BasicBlocks that merge several entry or exit edges so that after the merge
/// just one entry and one exit edge exists.
///
/// The \e Entry of a Region is the first BasicBlock that is passed after
/// entering the Region. It is an element of the Region. The entry BasicBlock
/// dominates all BasicBlocks in the Region.
///
/// The \e Exit of a Region is the first BasicBlock that is passed after
/// leaving the Region. It is not an element of the Region. The exit BasicBlock,
/// postdominates all BasicBlocks in the Region.
///
/// A <em> canonical Region </em> cannot be constructed by combining smaller
/// Regions.
///
/// Region A is the \e parent of Region B, if B is completely contained in A.
///
/// Two canonical Regions either do not intersect at all or one is
/// the parent of the other.
///
/// The <em> Program Structure Tree</em> is a graph (V, E) where V is the set of
/// Regions in the control flow graph and E is the \e parent relation of these
/// Regions.
///
/// Example:
///
/// \verbatim
/// A simple control flow graph, that contains two regions.
///
///        1
///       / |
///      2   |
///     / \   3
///    4   5  |
///    |   |  |
///    6   7  8
///     \  | /
///      \ |/       Region A: 1 -> 9 {1,2,3,4,5,6,7,8}
///        9        Region B: 2 -> 9 {2,4,5,6,7}
/// \endverbatim
///
/// You can obtain more examples by either calling
///
/// <tt> "opt -regions -analyze anyprogram.ll" </tt>
/// or
/// <tt> "opt -view-regions-only anyprogram.ll" </tt>
///
/// on any LLVM file you are interested in.
///
/// The first call returns a textual representation of the program structure
/// tree, the second one creates a graphical representation using graphviz.
template <class Tr>
class RegionBase : public RegionNodeBase<Tr> {
  friend class RegionInfoBase<Tr>;

  using FuncT = typename Tr::FuncT;
  using BlockT = typename Tr::BlockT;
  using RegionInfoT = typename Tr::RegionInfoT;
  using RegionT = typename Tr::RegionT;
  using RegionNodeT = typename Tr::RegionNodeT;
  using DomTreeT = typename Tr::DomTreeT;
  using LoopT = typename Tr::LoopT;
  using LoopInfoT = typename Tr::LoopInfoT;
  using InstT = typename Tr::InstT;

  using BlockTraits = GraphTraits<BlockT *>;
  using InvBlockTraits = GraphTraits<Inverse<BlockT *>>;
  using SuccIterTy = typename BlockTraits::ChildIteratorType;
  using PredIterTy = typename InvBlockTraits::ChildIteratorType;

  // Information necessary to manage this Region.
  RegionInfoT *RI;
  DomTreeT *DT;

  // The exit BasicBlock of this region.
  // (The entry BasicBlock is part of RegionNode)
  BlockT *exit;

  using RegionSet = std::vector<std::unique_ptr<RegionT>>;

  // The subregions of this region.
  RegionSet children;

  using BBNodeMapT = std::map<BlockT *, std::unique_ptr<RegionNodeT>>;

  // Save the BasicBlock RegionNodes that are element of this Region.
  mutable BBNodeMapT BBNodeMap;

  /// Check if a BB is in this Region. This check also works
  /// if the region is incorrectly built. (EXPENSIVE!)
  void verifyBBInRegion(BlockT *BB) const;

  /// Walk over all the BBs of the region starting from BB and
  /// verify that all reachable basic blocks are elements of the region.
  /// (EXPENSIVE!)
  void verifyWalk(BlockT *BB, std::set<BlockT *> *visitedBB) const;

  /// Verify if the region and its children are valid regions (EXPENSIVE!)
  void verifyRegionNest() const;

public:
  /// Create a new region.
  ///
  /// @param Entry  The entry basic block of the region.
  /// @param Exit   The exit basic block of the region.
  /// @param RI     The region info object that is managing this region.
  /// @param DT     The dominator tree of the current function.
  /// @param Parent The surrounding region or NULL if this is a top level
  ///               region.
  RegionBase(BlockT *Entry, BlockT *Exit, RegionInfoT *RI, DomTreeT *DT,
             RegionT *Parent = nullptr);

  RegionBase(const RegionBase &) = delete;
  RegionBase &operator=(const RegionBase &) = delete;

  /// Delete the Region and all its subregions.
  ~RegionBase();

  /// Get the entry BasicBlock of the Region.
  /// @return The entry BasicBlock of the region.
  BlockT *getEntry() const {
    return RegionNodeBase<Tr>::getEntry();
  }

  /// Replace the entry basic block of the region with the new basic
  ///        block.
  ///
  /// @param BB  The new entry basic block of the region.
  void replaceEntry(BlockT *BB);

  /// Replace the exit basic block of the region with the new basic
  ///        block.
  ///
  /// @param BB  The new exit basic block of the region.
  void replaceExit(BlockT *BB);

  /// Recursively replace the entry basic block of the region.
  ///
  /// This function replaces the entry basic block with a new basic block. It
  /// also updates all child regions that have the same entry basic block as
  /// this region.
  ///
  /// @param NewEntry The new entry basic block.
  void replaceEntryRecursive(BlockT *NewEntry);

  /// Recursively replace the exit basic block of the region.
  ///
  /// This function replaces the exit basic block with a new basic block. It
  /// also updates all child regions that have the same exit basic block as
  /// this region.
  ///
  /// @param NewExit The new exit basic block.
  void replaceExitRecursive(BlockT *NewExit);

  /// Get the exit BasicBlock of the Region.
  /// @return The exit BasicBlock of the Region, NULL if this is the TopLevel
  ///         Region.
  BlockT *getExit() const { return exit; }

  /// Get the parent of the Region.
  /// @return The parent of the Region or NULL if this is a top level
  ///         Region.
  RegionT *getParent() const {
    return RegionNodeBase<Tr>::getParent();
  }

  /// Get the RegionNode representing the current Region.
  /// @return The RegionNode representing the current Region.
  RegionNodeT *getNode() const {
    return const_cast<RegionNodeT *>(
        reinterpret_cast<const RegionNodeT *>(this));
  }

  /// Get the nesting level of this Region.
  ///
  /// An toplevel Region has depth 0.
  ///
  /// @return The depth of the region.
  unsigned getDepth() const;

  /// Check if a Region is the TopLevel region.
  ///
  /// The toplevel region represents the whole function.
  bool isTopLevelRegion() const { return exit == nullptr; }

  /// Return a new (non-canonical) region, that is obtained by joining
  ///        this region with its predecessors.
  ///
  /// @return A region also starting at getEntry(), but reaching to the next
  ///         basic block that forms with getEntry() a (non-canonical) region.
  ///         NULL if such a basic block does not exist.
  RegionT *getExpandedRegion() const;

  /// Return the first block of this region's single entry edge,
  ///        if existing.
  ///
  /// @return The BasicBlock starting this region's single entry edge,
  ///         else NULL.
  BlockT *getEnteringBlock() const;

  /// Return the first block of this region's single exit edge,
  ///        if existing.
  ///
  /// @return The BasicBlock starting this region's single exit edge,
  ///         else NULL.
  BlockT *getExitingBlock() const;

  /// Collect all blocks of this region's single exit edge, if existing.
  ///
  /// @return True if this region contains all the predecessors of the exit.
  bool getExitingBlocks(SmallVectorImpl<BlockT *> &Exitings) const;

  /// Is this a simple region?
  ///
  /// A region is simple if it has exactly one exit and one entry edge.
  ///
  /// @return True if the Region is simple.
  bool isSimple() const;

  /// Returns the name of the Region.
  /// @return The Name of the Region.
  std::string getNameStr() const;

  /// Return the RegionInfo object, that belongs to this Region.
  RegionInfoT *getRegionInfo() const { return RI; }

  /// PrintStyle - Print region in difference ways.
  enum PrintStyle { PrintNone, PrintBB, PrintRN };

  /// Print the region.
  ///
  /// @param OS The output stream the Region is printed to.
  /// @param printTree Print also the tree of subregions.
  /// @param level The indentation level used for printing.
  void print(raw_ostream &OS, bool printTree = true, unsigned level = 0,
             PrintStyle Style = PrintNone) const;

#if !defined(NDEBUG) || defined(LLVM_ENABLE_DUMP)
  /// Print the region to stderr.
  void dump() const;
#endif

  /// Check if the region contains a BasicBlock.
  ///
  /// @param BB The BasicBlock that might be contained in this Region.
  /// @return True if the block is contained in the region otherwise false.
  bool contains(const BlockT *BB) const;

  /// Check if the region contains another region.
  ///
  /// @param SubRegion The region that might be contained in this Region.
  /// @return True if SubRegion is contained in the region otherwise false.
  bool contains(const RegionT *SubRegion) const {
    // Toplevel Region.
    if (!getExit())
      return true;

    return contains(SubRegion->getEntry()) &&
           (contains(SubRegion->getExit()) ||
            SubRegion->getExit() == getExit());
  }

  /// Check if the region contains an Instruction.
  ///
  /// @param Inst The Instruction that might be contained in this region.
  /// @return True if the Instruction is contained in the region otherwise
  /// false.
  bool contains(const InstT *Inst) const { return contains(Inst->getParent()); }

  /// Check if the region contains a loop.
  ///
  /// @param L The loop that might be contained in this region.
  /// @return True if the loop is contained in the region otherwise false.
  ///         In case a NULL pointer is passed to this function the result
  ///         is false, except for the region that describes the whole function.
  ///         In that case true is returned.
  bool contains(const LoopT *L) const;

  /// Get the outermost loop in the region that contains a loop.
  ///
  /// Find for a Loop L the outermost loop OuterL that is a parent loop of L
  /// and is itself contained in the region.
  ///
  /// @param L The loop the lookup is started.
  /// @return The outermost loop in the region, NULL if such a loop does not
  ///         exist or if the region describes the whole function.
  LoopT *outermostLoopInRegion(LoopT *L) const;

  /// Get the outermost loop in the region that contains a basic block.
  ///
  /// Find for a basic block BB the outermost loop L that contains BB and is
  /// itself contained in the region.
  ///
  /// @param LI A pointer to a LoopInfo analysis.
  /// @param BB The basic block surrounded by the loop.
  /// @return The outermost loop in the region, NULL if such a loop does not
  ///         exist or if the region describes the whole function.
  LoopT *outermostLoopInRegion(LoopInfoT *LI, BlockT *BB) const;

  /// Get the subregion that starts at a BasicBlock
  ///
  /// @param BB The BasicBlock the subregion should start.
  /// @return The Subregion if available, otherwise NULL.
  RegionT *getSubRegionNode(BlockT *BB) const;

  /// Get the RegionNode for a BasicBlock
  ///
  /// @param BB The BasicBlock at which the RegionNode should start.
  /// @return If available, the RegionNode that represents the subregion
  ///         starting at BB. If no subregion starts at BB, the RegionNode
  ///         representing BB.
  RegionNodeT *getNode(BlockT *BB) const;

  /// Get the BasicBlock RegionNode for a BasicBlock
  ///
  /// @param BB The BasicBlock for which the RegionNode is requested.
  /// @return The RegionNode representing the BB.
  RegionNodeT *getBBNode(BlockT *BB) const;

  /// Add a new subregion to this Region.
  ///
  /// @param SubRegion The new subregion that will be added.
  /// @param moveChildren Move the children of this region, that are also
  ///                     contained in SubRegion into SubRegion.
  void addSubRegion(RegionT *SubRegion, bool moveChildren = false);

  /// Remove a subregion from this Region.
  ///
  /// The subregion is not deleted, as it will probably be inserted into another
  /// region.
  /// @param SubRegion The SubRegion that will be removed.
  RegionT *removeSubRegion(RegionT *SubRegion);

  /// Move all direct child nodes of this Region to another Region.
  ///
  /// @param To The Region the child nodes will be transferred to.
  void transferChildrenTo(RegionT *To);

  /// Verify if the region is a correct region.
  ///
  /// Check if this is a correctly build Region. This is an expensive check, as
  /// the complete CFG of the Region will be walked.
  void verifyRegion() const;

  /// Clear the cache for BB RegionNodes.
  ///
  /// After calling this function the BasicBlock RegionNodes will be stored at
  /// different memory locations. RegionNodes obtained before this function is
  /// called are therefore not comparable to RegionNodes abtained afterwords.
  void clearNodeCache();

  /// @name Subregion Iterators
  ///
  /// These iterators iterator over all subregions of this Region.
  //@{
  using iterator = typename RegionSet::iterator;
  using const_iterator = typename RegionSet::const_iterator;

  iterator begin() { return children.begin(); }
  iterator end() { return children.end(); }

  const_iterator begin() const { return children.begin(); }
  const_iterator end() const { return children.end(); }
  //@}

  /// @name BasicBlock Iterators
  ///
  /// These iterators iterate over all BasicBlocks that are contained in this
  /// Region. The iterator also iterates over BasicBlocks that are elements of
  /// a subregion of this Region. It is therefore called a flat iterator.
  //@{
  template <bool IsConst>
  class block_iterator_wrapper
      : public df_iterator<
            typename std::conditional<IsConst, const BlockT, BlockT>::type *> {
    using super =
        df_iterator<
            typename std::conditional<IsConst, const BlockT, BlockT>::type *>;

  public:
    using Self = block_iterator_wrapper<IsConst>;
    using value_type = typename super::value_type;

    // Construct the begin iterator.
    block_iterator_wrapper(value_type Entry, value_type Exit)
        : super(df_begin(Entry)) {
      // Mark the exit of the region as visited, so that the children of the
      // exit and the exit itself, i.e. the block outside the region will never
      // be visited.
      super::Visited.insert(Exit);
    }

    // Construct the end iterator.
    block_iterator_wrapper() : super(df_end<value_type>((BlockT *)nullptr)) {}

    /*implicit*/ block_iterator_wrapper(super I) : super(I) {}

    // FIXME: Even a const_iterator returns a non-const BasicBlock pointer.
    //        This was introduced for backwards compatibility, but should
    //        be removed as soon as all users are fixed.
    BlockT *operator*() const {
      return const_cast<BlockT *>(super::operator*());
    }
  };

  using block_iterator = block_iterator_wrapper<false>;
  using const_block_iterator = block_iterator_wrapper<true>;

  block_iterator block_begin() { return block_iterator(getEntry(), getExit()); }

  block_iterator block_end() { return block_iterator(); }

  const_block_iterator block_begin() const {
    return const_block_iterator(getEntry(), getExit());
  }
  const_block_iterator block_end() const { return const_block_iterator(); }

  using block_range = iterator_range<block_iterator>;
  using const_block_range = iterator_range<const_block_iterator>;

  /// Returns a range view of the basic blocks in the region.
  inline block_range blocks() {
    return block_range(block_begin(), block_end());
  }

  /// Returns a range view of the basic blocks in the region.
  ///
  /// This is the 'const' version of the range view.
  inline const_block_range blocks() const {
    return const_block_range(block_begin(), block_end());
  }
  //@}

  /// @name Element Iterators
  ///
  /// These iterators iterate over all BasicBlock and subregion RegionNodes that
  /// are direct children of this Region. It does not iterate over any
  /// RegionNodes that are also element of a subregion of this Region.
  //@{
  using element_iterator =
      df_iterator<RegionNodeT *, df_iterator_default_set<RegionNodeT *>, false,
                  GraphTraits<RegionNodeT *>>;

  using const_element_iterator =
      df_iterator<const RegionNodeT *,
                  df_iterator_default_set<const RegionNodeT *>, false,
                  GraphTraits<const RegionNodeT *>>;

  element_iterator element_begin();
  element_iterator element_end();
  iterator_range<element_iterator> elements() {
    return make_range(element_begin(), element_end());
  }

  const_element_iterator element_begin() const;
  const_element_iterator element_end() const;
  iterator_range<const_element_iterator> elements() const {
    return make_range(element_begin(), element_end());
  }
  //@}
};

/// Print a RegionNode.
template <class Tr>
inline raw_ostream &operator<<(raw_ostream &OS, const RegionNodeBase<Tr> &Node);

//===----------------------------------------------------------------------===//
/// Analysis that detects all canonical Regions.
///
/// The RegionInfo pass detects all canonical regions in a function. The Regions
/// are connected using the parent relation. This builds a Program Structure
/// Tree.
template <class Tr>
class RegionInfoBase {
  friend class RegionInfo;
  friend class MachineRegionInfo;

  using BlockT = typename Tr::BlockT;
  using FuncT = typename Tr::FuncT;
  using RegionT = typename Tr::RegionT;
  using RegionInfoT = typename Tr::RegionInfoT;
  using DomTreeT = typename Tr::DomTreeT;
  using DomTreeNodeT = typename Tr::DomTreeNodeT;
  using PostDomTreeT = typename Tr::PostDomTreeT;
  using DomFrontierT = typename Tr::DomFrontierT;
  using BlockTraits = GraphTraits<BlockT *>;
  using InvBlockTraits = GraphTraits<Inverse<BlockT *>>;
  using SuccIterTy = typename BlockTraits::ChildIteratorType;
  using PredIterTy = typename InvBlockTraits::ChildIteratorType;

  using BBtoBBMap = DenseMap<BlockT *, BlockT *>;
  using BBtoRegionMap = DenseMap<BlockT *, RegionT *>;

  RegionInfoBase();

  RegionInfoBase(RegionInfoBase &&Arg)
    : DT(std::move(Arg.DT)), PDT(std::move(Arg.PDT)), DF(std::move(Arg.DF)),
      TopLevelRegion(std::move(Arg.TopLevelRegion)),
      BBtoRegion(std::move(Arg.BBtoRegion)) {
    Arg.wipe();
  }

  RegionInfoBase &operator=(RegionInfoBase &&RHS) {
    DT = std::move(RHS.DT);
    PDT = std::move(RHS.PDT);
    DF = std::move(RHS.DF);
    TopLevelRegion = std::move(RHS.TopLevelRegion);
    BBtoRegion = std::move(RHS.BBtoRegion);
    RHS.wipe();
    return *this;
  }

  virtual ~RegionInfoBase();

  DomTreeT *DT;
  PostDomTreeT *PDT;
  DomFrontierT *DF;

  /// The top level region.
  RegionT *TopLevelRegion = nullptr;

  /// Map every BB to the smallest region, that contains BB.
  BBtoRegionMap BBtoRegion;

protected:
  /// Update refences to a RegionInfoT held by the RegionT managed here
  ///
  /// This is a post-move helper. Regions hold references to the owning
  /// RegionInfo object. After a move these need to be fixed.
  template<typename TheRegionT>
  void updateRegionTree(RegionInfoT &RI, TheRegionT *R) {
    if (!R)
      return;
    R->RI = &RI;
    for (auto &SubR : *R)
      updateRegionTree(RI, SubR.get());
  }

private:
  /// Wipe this region tree's state without releasing any resources.
  ///
  /// This is essentially a post-move helper only. It leaves the object in an
  /// assignable and destroyable state, but otherwise invalid.
  void wipe() {
    DT = nullptr;
    PDT = nullptr;
    DF = nullptr;
    TopLevelRegion = nullptr;
    BBtoRegion.clear();
  }

  // Check whether the entries of BBtoRegion for the BBs of region
  // SR are correct. Triggers an assertion if not. Calls itself recursively for
  // subregions.
  void verifyBBMap(const RegionT *SR) const;

  // Returns true if BB is in the dominance frontier of
  // entry, because it was inherited from exit. In the other case there is an
  // edge going from entry to BB without passing exit.
  bool isCommonDomFrontier(BlockT *BB, BlockT *entry, BlockT *exit) const;

  // Check if entry and exit surround a valid region, based on
  // dominance tree and dominance frontier.
  bool isRegion(BlockT *entry, BlockT *exit) const;

  // Saves a shortcut pointing from entry to exit.
  // This function may extend this shortcut if possible.
  void insertShortCut(BlockT *entry, BlockT *exit, BBtoBBMap *ShortCut) const;

  // Returns the next BB that postdominates N, while skipping
  // all post dominators that cannot finish a canonical region.
  DomTreeNodeT *getNextPostDom(DomTreeNodeT *N, BBtoBBMap *ShortCut) const;

  // A region is trivial, if it contains only one BB.
  bool isTrivialRegion(BlockT *entry, BlockT *exit) const;

  // Creates a single entry single exit region.
  RegionT *createRegion(BlockT *entry, BlockT *exit);

  // Detect all regions starting with bb 'entry'.
  void findRegionsWithEntry(BlockT *entry, BBtoBBMap *ShortCut);

  // Detects regions in F.
  void scanForRegions(FuncT &F, BBtoBBMap *ShortCut);

  // Get the top most parent with the same entry block.
  RegionT *getTopMostParent(RegionT *region);

  // Build the region hierarchy after all region detected.
  void buildRegionsTree(DomTreeNodeT *N, RegionT *region);

  // Update statistic about created regions.
  virtual void updateStatistics(RegionT *R) = 0;

  // Detect all regions in function and build the region tree.
  void calculate(FuncT &F);

public:
  RegionInfoBase(const RegionInfoBase &) = delete;
  RegionInfoBase &operator=(const RegionInfoBase &) = delete;

  static bool VerifyRegionInfo;
  static typename RegionT::PrintStyle printStyle;

  void print(raw_ostream &OS) const;
#if !defined(NDEBUG) || defined(LLVM_ENABLE_DUMP)
  void dump() const;
#endif

  void releaseMemory();

  /// Get the smallest region that contains a BasicBlock.
  ///
  /// @param BB The basic block.
  /// @return The smallest region, that contains BB or NULL, if there is no
  /// region containing BB.
  RegionT *getRegionFor(BlockT *BB) const;

  ///  Set the smallest region that surrounds a basic block.
  ///
  /// @param BB The basic block surrounded by a region.
  /// @param R The smallest region that surrounds BB.
  void setRegionFor(BlockT *BB, RegionT *R);

  /// A shortcut for getRegionFor().
  ///
  /// @param BB The basic block.
  /// @return The smallest region, that contains BB or NULL, if there is no
  /// region containing BB.
  RegionT *operator[](BlockT *BB) const;

  /// Return the exit of the maximal refined region, that starts at a
  /// BasicBlock.
  ///
  /// @param BB The BasicBlock the refined region starts.
  BlockT *getMaxRegionExit(BlockT *BB) const;

  /// Find the smallest region that contains two regions.
  ///
  /// @param A The first region.
  /// @param B The second region.
  /// @return The smallest region containing A and B.
  RegionT *getCommonRegion(RegionT *A, RegionT *B) const;

  /// Find the smallest region that contains two basic blocks.
  ///
  /// @param A The first basic block.
  /// @param B The second basic block.
  /// @return The smallest region that contains A and B.
  RegionT *getCommonRegion(BlockT *A, BlockT *B) const {
    return getCommonRegion(getRegionFor(A), getRegionFor(B));
  }

  /// Find the smallest region that contains a set of regions.
  ///
  /// @param Regions A vector of regions.
  /// @return The smallest region that contains all regions in Regions.
  RegionT *getCommonRegion(SmallVectorImpl<RegionT *> &Regions) const;

  /// Find the smallest region that contains a set of basic blocks.
  ///
  /// @param BBs A vector of basic blocks.
  /// @return The smallest region that contains all basic blocks in BBS.
  RegionT *getCommonRegion(SmallVectorImpl<BlockT *> &BBs) const;

  RegionT *getTopLevelRegion() const { return TopLevelRegion; }

  /// Clear the Node Cache for all Regions.
  ///
  /// @see Region::clearNodeCache()
  void clearNodeCache() {
    if (TopLevelRegion)
      TopLevelRegion->clearNodeCache();
  }

  void verifyAnalysis() const;
};

class Region;

class RegionNode : public RegionNodeBase<RegionTraits<Function>> {
public:
  inline RegionNode(Region *Parent, BasicBlock *Entry, bool isSubRegion = false)
      : RegionNodeBase<RegionTraits<Function>>(Parent, Entry, isSubRegion) {}

  bool operator==(const Region &RN) const {
    return this == reinterpret_cast<const RegionNode *>(&RN);
  }
};

class Region : public RegionBase<RegionTraits<Function>> {
public:
  Region(BasicBlock *Entry, BasicBlock *Exit, RegionInfo *RI, DominatorTree *DT,
         Region *Parent = nullptr);
  ~Region();

  bool operator==(const RegionNode &RN) const {
    return &RN == reinterpret_cast<const RegionNode *>(this);
  }
};

class RegionInfo : public RegionInfoBase<RegionTraits<Function>> {
public:
  using Base = RegionInfoBase<RegionTraits<Function>>;

  explicit RegionInfo();

  RegionInfo(RegionInfo &&Arg) : Base(std::move(static_cast<Base &>(Arg))) {
    updateRegionTree(*this, TopLevelRegion);
  }

  RegionInfo &operator=(RegionInfo &&RHS) {
    Base::operator=(std::move(static_cast<Base &>(RHS)));
    updateRegionTree(*this, TopLevelRegion);
    return *this;
  }

  ~RegionInfo() override;

  /// Handle invalidation explicitly.
  bool invalidate(Function &F, const PreservedAnalyses &PA,
                  FunctionAnalysisManager::Invalidator &);

  // updateStatistics - Update statistic about created regions.
  void updateStatistics(Region *R) final;

  void recalculate(Function &F, DominatorTree *DT, PostDominatorTree *PDT,
                   DominanceFrontier *DF);

#ifndef NDEBUG
  /// Opens a viewer to show the GraphViz visualization of the regions.
  ///
  /// Useful during debugging as an alternative to dump().
  void view();

  /// Opens a viewer to show the GraphViz visualization of this region
  /// without instructions in the BasicBlocks.
  ///
  /// Useful during debugging as an alternative to dump().
  void viewOnly();
#endif
};

class RegionInfoPass : public FunctionPass {
  RegionInfo RI;

public:
  static char ID;

  explicit RegionInfoPass();
  ~RegionInfoPass() override;

  RegionInfo &getRegionInfo() { return RI; }

  const RegionInfo &getRegionInfo() const { return RI; }

  /// @name FunctionPass interface
  //@{
  bool runOnFunction(Function &F) override;
  void releaseMemory() override;
  void verifyAnalysis() const override;
  void getAnalysisUsage(AnalysisUsage &AU) const override;
  void print(raw_ostream &OS, const Module *) const override;
  void dump() const;
  //@}
};

/// Analysis pass that exposes the \c RegionInfo for a function.
class RegionInfoAnalysis : public AnalysisInfoMixin<RegionInfoAnalysis> {
  friend AnalysisInfoMixin<RegionInfoAnalysis>;

  static AnalysisKey Key;

public:
  using Result = RegionInfo;

  RegionInfo run(Function &F, FunctionAnalysisManager &AM);
};

/// Printer pass for the \c RegionInfo.
class RegionInfoPrinterPass : public PassInfoMixin<RegionInfoPrinterPass> {
  raw_ostream &OS;

public:
  explicit RegionInfoPrinterPass(raw_ostream &OS);

  PreservedAnalyses run(Function &F, FunctionAnalysisManager &AM);
};

/// Verifier pass for the \c RegionInfo.
struct RegionInfoVerifierPass : PassInfoMixin<RegionInfoVerifierPass> {
  PreservedAnalyses run(Function &F, FunctionAnalysisManager &AM);
};

template <>
template <>
inline BasicBlock *
RegionNodeBase<RegionTraits<Function>>::getNodeAs<BasicBlock>() const {
  assert(!isSubRegion() && "This is not a BasicBlock RegionNode!");
  return getEntry();
}

template <>
template <>
inline Region *
RegionNodeBase<RegionTraits<Function>>::getNodeAs<Region>() const {
  assert(isSubRegion() && "This is not a subregion RegionNode!");
  auto Unconst = const_cast<RegionNodeBase<RegionTraits<Function>> *>(this);
  return reinterpret_cast<Region *>(Unconst);
}

template <class Tr>
inline raw_ostream &operator<<(raw_ostream &OS,
                               const RegionNodeBase<Tr> &Node) {
  using BlockT = typename Tr::BlockT;
  using RegionT = typename Tr::RegionT;

  if (Node.isSubRegion())
    return OS << Node.template getNodeAs<RegionT>()->getNameStr();
  else
    return OS << Node.template getNodeAs<BlockT>()->getName();
}

extern template class RegionBase<RegionTraits<Function>>;
extern template class RegionNodeBase<RegionTraits<Function>>;
extern template class RegionInfoBase<RegionTraits<Function>>;

} // end namespace llvm

#endif // LLVM_ANALYSIS_REGIONINFO_H
