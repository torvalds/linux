//===---- MachineOutliner.cpp - Outline instructions -----------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
///
/// \file
/// Replaces repeated sequences of instructions with function calls.
///
/// This works by placing every instruction from every basic block in a
/// suffix tree, and repeatedly querying that tree for repeated sequences of
/// instructions. If a sequence of instructions appears often, then it ought
/// to be beneficial to pull out into a function.
///
/// The MachineOutliner communicates with a given target using hooks defined in
/// TargetInstrInfo.h. The target supplies the outliner with information on how
/// a specific sequence of instructions should be outlined. This information
/// is used to deduce the number of instructions necessary to
///
/// * Create an outlined function
/// * Call that outlined function
///
/// Targets must implement
///   * getOutliningCandidateInfo
///   * buildOutlinedFrame
///   * insertOutlinedCall
///   * isFunctionSafeToOutlineFrom
///
/// in order to make use of the MachineOutliner.
///
/// This was originally presented at the 2016 LLVM Developers' Meeting in the
/// talk "Reducing Code Size Using Outlining". For a high-level overview of
/// how this pass works, the talk is available on YouTube at
///
/// https://www.youtube.com/watch?v=yorld-WSOeU
///
/// The slides for the talk are available at
///
/// http://www.llvm.org/devmtg/2016-11/Slides/Paquette-Outliner.pdf
///
/// The talk provides an overview of how the outliner finds candidates and
/// ultimately outlines them. It describes how the main data structure for this
/// pass, the suffix tree, is queried and purged for candidates. It also gives
/// a simplified suffix tree construction algorithm for suffix trees based off
/// of the algorithm actually used here, Ukkonen's algorithm.
///
/// For the original RFC for this pass, please see
///
/// http://lists.llvm.org/pipermail/llvm-dev/2016-August/104170.html
///
/// For more information on the suffix tree data structure, please see
/// https://www.cs.helsinki.fi/u/ukkonen/SuffixT1withFigs.pdf
///
//===----------------------------------------------------------------------===//
#include "llvm/CodeGen/MachineOutliner.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/ADT/Twine.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineModuleInfo.h"
#include "llvm/CodeGen/MachineOptimizationRemarkEmitter.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/CodeGen/Passes.h"
#include "llvm/CodeGen/TargetInstrInfo.h"
#include "llvm/CodeGen/TargetSubtargetInfo.h"
#include "llvm/IR/DIBuilder.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/Mangler.h"
#include "llvm/Support/Allocator.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/raw_ostream.h"
#include <functional>
#include <map>
#include <sstream>
#include <tuple>
#include <vector>

#define DEBUG_TYPE "machine-outliner"

using namespace llvm;
using namespace ore;
using namespace outliner;

STATISTIC(NumOutlined, "Number of candidates outlined");
STATISTIC(FunctionsCreated, "Number of functions created");

// Set to true if the user wants the outliner to run on linkonceodr linkage
// functions. This is false by default because the linker can dedupe linkonceodr
// functions. Since the outliner is confined to a single module (modulo LTO),
// this is off by default. It should, however, be the default behaviour in
// LTO.
static cl::opt<bool> EnableLinkOnceODROutlining(
    "enable-linkonceodr-outlining",
    cl::Hidden,
    cl::desc("Enable the machine outliner on linkonceodr functions"),
    cl::init(false));

namespace {

/// Represents an undefined index in the suffix tree.
const unsigned EmptyIdx = -1;

/// A node in a suffix tree which represents a substring or suffix.
///
/// Each node has either no children or at least two children, with the root
/// being a exception in the empty tree.
///
/// Children are represented as a map between unsigned integers and nodes. If
/// a node N has a child M on unsigned integer k, then the mapping represented
/// by N is a proper prefix of the mapping represented by M. Note that this,
/// although similar to a trie is somewhat different: each node stores a full
/// substring of the full mapping rather than a single character state.
///
/// Each internal node contains a pointer to the internal node representing
/// the same string, but with the first character chopped off. This is stored
/// in \p Link. Each leaf node stores the start index of its respective
/// suffix in \p SuffixIdx.
struct SuffixTreeNode {

  /// The children of this node.
  ///
  /// A child existing on an unsigned integer implies that from the mapping
  /// represented by the current node, there is a way to reach another
  /// mapping by tacking that character on the end of the current string.
  DenseMap<unsigned, SuffixTreeNode *> Children;

  /// The start index of this node's substring in the main string.
  unsigned StartIdx = EmptyIdx;

  /// The end index of this node's substring in the main string.
  ///
  /// Every leaf node must have its \p EndIdx incremented at the end of every
  /// step in the construction algorithm. To avoid having to update O(N)
  /// nodes individually at the end of every step, the end index is stored
  /// as a pointer.
  unsigned *EndIdx = nullptr;

  /// For leaves, the start index of the suffix represented by this node.
  ///
  /// For all other nodes, this is ignored.
  unsigned SuffixIdx = EmptyIdx;

  /// For internal nodes, a pointer to the internal node representing
  /// the same sequence with the first character chopped off.
  ///
  /// This acts as a shortcut in Ukkonen's algorithm. One of the things that
  /// Ukkonen's algorithm does to achieve linear-time construction is
  /// keep track of which node the next insert should be at. This makes each
  /// insert O(1), and there are a total of O(N) inserts. The suffix link
  /// helps with inserting children of internal nodes.
  ///
  /// Say we add a child to an internal node with associated mapping S. The
  /// next insertion must be at the node representing S - its first character.
  /// This is given by the way that we iteratively build the tree in Ukkonen's
  /// algorithm. The main idea is to look at the suffixes of each prefix in the
  /// string, starting with the longest suffix of the prefix, and ending with
  /// the shortest. Therefore, if we keep pointers between such nodes, we can
  /// move to the next insertion point in O(1) time. If we don't, then we'd
  /// have to query from the root, which takes O(N) time. This would make the
  /// construction algorithm O(N^2) rather than O(N).
  SuffixTreeNode *Link = nullptr;

  /// The length of the string formed by concatenating the edge labels from the
  /// root to this node.
  unsigned ConcatLen = 0;

  /// Returns true if this node is a leaf.
  bool isLeaf() const { return SuffixIdx != EmptyIdx; }

  /// Returns true if this node is the root of its owning \p SuffixTree.
  bool isRoot() const { return StartIdx == EmptyIdx; }

  /// Return the number of elements in the substring associated with this node.
  size_t size() const {

    // Is it the root? If so, it's the empty string so return 0.
    if (isRoot())
      return 0;

    assert(*EndIdx != EmptyIdx && "EndIdx is undefined!");

    // Size = the number of elements in the string.
    // For example, [0 1 2 3] has length 4, not 3. 3-0 = 3, so we have 3-0+1.
    return *EndIdx - StartIdx + 1;
  }

  SuffixTreeNode(unsigned StartIdx, unsigned *EndIdx, SuffixTreeNode *Link)
      : StartIdx(StartIdx), EndIdx(EndIdx), Link(Link) {}

  SuffixTreeNode() {}
};

/// A data structure for fast substring queries.
///
/// Suffix trees represent the suffixes of their input strings in their leaves.
/// A suffix tree is a type of compressed trie structure where each node
/// represents an entire substring rather than a single character. Each leaf
/// of the tree is a suffix.
///
/// A suffix tree can be seen as a type of state machine where each state is a
/// substring of the full string. The tree is structured so that, for a string
/// of length N, there are exactly N leaves in the tree. This structure allows
/// us to quickly find repeated substrings of the input string.
///
/// In this implementation, a "string" is a vector of unsigned integers.
/// These integers may result from hashing some data type. A suffix tree can
/// contain 1 or many strings, which can then be queried as one large string.
///
/// The suffix tree is implemented using Ukkonen's algorithm for linear-time
/// suffix tree construction. Ukkonen's algorithm is explained in more detail
/// in the paper by Esko Ukkonen "On-line construction of suffix trees. The
/// paper is available at
///
/// https://www.cs.helsinki.fi/u/ukkonen/SuffixT1withFigs.pdf
class SuffixTree {
public:
  /// Each element is an integer representing an instruction in the module.
  ArrayRef<unsigned> Str;

  /// A repeated substring in the tree.
  struct RepeatedSubstring {
    /// The length of the string.
    unsigned Length;

    /// The start indices of each occurrence.
    std::vector<unsigned> StartIndices;
  };

private:
  /// Maintains each node in the tree.
  SpecificBumpPtrAllocator<SuffixTreeNode> NodeAllocator;

  /// The root of the suffix tree.
  ///
  /// The root represents the empty string. It is maintained by the
  /// \p NodeAllocator like every other node in the tree.
  SuffixTreeNode *Root = nullptr;

  /// Maintains the end indices of the internal nodes in the tree.
  ///
  /// Each internal node is guaranteed to never have its end index change
  /// during the construction algorithm; however, leaves must be updated at
  /// every step. Therefore, we need to store leaf end indices by reference
  /// to avoid updating O(N) leaves at every step of construction. Thus,
  /// every internal node must be allocated its own end index.
  BumpPtrAllocator InternalEndIdxAllocator;

  /// The end index of each leaf in the tree.
  unsigned LeafEndIdx = -1;

  /// Helper struct which keeps track of the next insertion point in
  /// Ukkonen's algorithm.
  struct ActiveState {
    /// The next node to insert at.
    SuffixTreeNode *Node;

    /// The index of the first character in the substring currently being added.
    unsigned Idx = EmptyIdx;

    /// The length of the substring we have to add at the current step.
    unsigned Len = 0;
  };

  /// The point the next insertion will take place at in the
  /// construction algorithm.
  ActiveState Active;

  /// Allocate a leaf node and add it to the tree.
  ///
  /// \param Parent The parent of this node.
  /// \param StartIdx The start index of this node's associated string.
  /// \param Edge The label on the edge leaving \p Parent to this node.
  ///
  /// \returns A pointer to the allocated leaf node.
  SuffixTreeNode *insertLeaf(SuffixTreeNode &Parent, unsigned StartIdx,
                             unsigned Edge) {

    assert(StartIdx <= LeafEndIdx && "String can't start after it ends!");

    SuffixTreeNode *N = new (NodeAllocator.Allocate())
        SuffixTreeNode(StartIdx, &LeafEndIdx, nullptr);
    Parent.Children[Edge] = N;

    return N;
  }

  /// Allocate an internal node and add it to the tree.
  ///
  /// \param Parent The parent of this node. Only null when allocating the root.
  /// \param StartIdx The start index of this node's associated string.
  /// \param EndIdx The end index of this node's associated string.
  /// \param Edge The label on the edge leaving \p Parent to this node.
  ///
  /// \returns A pointer to the allocated internal node.
  SuffixTreeNode *insertInternalNode(SuffixTreeNode *Parent, unsigned StartIdx,
                                     unsigned EndIdx, unsigned Edge) {

    assert(StartIdx <= EndIdx && "String can't start after it ends!");
    assert(!(!Parent && StartIdx != EmptyIdx) &&
           "Non-root internal nodes must have parents!");

    unsigned *E = new (InternalEndIdxAllocator) unsigned(EndIdx);
    SuffixTreeNode *N = new (NodeAllocator.Allocate())
        SuffixTreeNode(StartIdx, E, Root);
    if (Parent)
      Parent->Children[Edge] = N;

    return N;
  }

  /// Set the suffix indices of the leaves to the start indices of their
  /// respective suffixes.
  ///
  /// \param[in] CurrNode The node currently being visited.
  /// \param CurrNodeLen The concatenation of all node sizes from the root to
  /// this node. Used to produce suffix indices.
  void setSuffixIndices(SuffixTreeNode &CurrNode, unsigned CurrNodeLen) {

    bool IsLeaf = CurrNode.Children.size() == 0 && !CurrNode.isRoot();

    // Store the concatenation of lengths down from the root.
    CurrNode.ConcatLen = CurrNodeLen;
    // Traverse the tree depth-first.
    for (auto &ChildPair : CurrNode.Children) {
      assert(ChildPair.second && "Node had a null child!");
      setSuffixIndices(*ChildPair.second,
                       CurrNodeLen + ChildPair.second->size());
    }

    // Is this node a leaf? If it is, give it a suffix index.
    if (IsLeaf)
      CurrNode.SuffixIdx = Str.size() - CurrNodeLen;
  }

  /// Construct the suffix tree for the prefix of the input ending at
  /// \p EndIdx.
  ///
  /// Used to construct the full suffix tree iteratively. At the end of each
  /// step, the constructed suffix tree is either a valid suffix tree, or a
  /// suffix tree with implicit suffixes. At the end of the final step, the
  /// suffix tree is a valid tree.
  ///
  /// \param EndIdx The end index of the current prefix in the main string.
  /// \param SuffixesToAdd The number of suffixes that must be added
  /// to complete the suffix tree at the current phase.
  ///
  /// \returns The number of suffixes that have not been added at the end of
  /// this step.
  unsigned extend(unsigned EndIdx, unsigned SuffixesToAdd) {
    SuffixTreeNode *NeedsLink = nullptr;

    while (SuffixesToAdd > 0) {

      // Are we waiting to add anything other than just the last character?
      if (Active.Len == 0) {
        // If not, then say the active index is the end index.
        Active.Idx = EndIdx;
      }

      assert(Active.Idx <= EndIdx && "Start index can't be after end index!");

      // The first character in the current substring we're looking at.
      unsigned FirstChar = Str[Active.Idx];

      // Have we inserted anything starting with FirstChar at the current node?
      if (Active.Node->Children.count(FirstChar) == 0) {
        // If not, then we can just insert a leaf and move too the next step.
        insertLeaf(*Active.Node, EndIdx, FirstChar);

        // The active node is an internal node, and we visited it, so it must
        // need a link if it doesn't have one.
        if (NeedsLink) {
          NeedsLink->Link = Active.Node;
          NeedsLink = nullptr;
        }
      } else {
        // There's a match with FirstChar, so look for the point in the tree to
        // insert a new node.
        SuffixTreeNode *NextNode = Active.Node->Children[FirstChar];

        unsigned SubstringLen = NextNode->size();

        // Is the current suffix we're trying to insert longer than the size of
        // the child we want to move to?
        if (Active.Len >= SubstringLen) {
          // If yes, then consume the characters we've seen and move to the next
          // node.
          Active.Idx += SubstringLen;
          Active.Len -= SubstringLen;
          Active.Node = NextNode;
          continue;
        }

        // Otherwise, the suffix we're trying to insert must be contained in the
        // next node we want to move to.
        unsigned LastChar = Str[EndIdx];

        // Is the string we're trying to insert a substring of the next node?
        if (Str[NextNode->StartIdx + Active.Len] == LastChar) {
          // If yes, then we're done for this step. Remember our insertion point
          // and move to the next end index. At this point, we have an implicit
          // suffix tree.
          if (NeedsLink && !Active.Node->isRoot()) {
            NeedsLink->Link = Active.Node;
            NeedsLink = nullptr;
          }

          Active.Len++;
          break;
        }

        // The string we're trying to insert isn't a substring of the next node,
        // but matches up to a point. Split the node.
        //
        // For example, say we ended our search at a node n and we're trying to
        // insert ABD. Then we'll create a new node s for AB, reduce n to just
        // representing C, and insert a new leaf node l to represent d. This
        // allows us to ensure that if n was a leaf, it remains a leaf.
        //
        //   | ABC  ---split--->  | AB
        //   n                    s
        //                     C / \ D
        //                      n   l

        // The node s from the diagram
        SuffixTreeNode *SplitNode =
            insertInternalNode(Active.Node, NextNode->StartIdx,
                               NextNode->StartIdx + Active.Len - 1, FirstChar);

        // Insert the new node representing the new substring into the tree as
        // a child of the split node. This is the node l from the diagram.
        insertLeaf(*SplitNode, EndIdx, LastChar);

        // Make the old node a child of the split node and update its start
        // index. This is the node n from the diagram.
        NextNode->StartIdx += Active.Len;
        SplitNode->Children[Str[NextNode->StartIdx]] = NextNode;

        // SplitNode is an internal node, update the suffix link.
        if (NeedsLink)
          NeedsLink->Link = SplitNode;

        NeedsLink = SplitNode;
      }

      // We've added something new to the tree, so there's one less suffix to
      // add.
      SuffixesToAdd--;

      if (Active.Node->isRoot()) {
        if (Active.Len > 0) {
          Active.Len--;
          Active.Idx = EndIdx - SuffixesToAdd + 1;
        }
      } else {
        // Start the next phase at the next smallest suffix.
        Active.Node = Active.Node->Link;
      }
    }

    return SuffixesToAdd;
  }

public:
  /// Construct a suffix tree from a sequence of unsigned integers.
  ///
  /// \param Str The string to construct the suffix tree for.
  SuffixTree(const std::vector<unsigned> &Str) : Str(Str) {
    Root = insertInternalNode(nullptr, EmptyIdx, EmptyIdx, 0);
    Active.Node = Root;

    // Keep track of the number of suffixes we have to add of the current
    // prefix.
    unsigned SuffixesToAdd = 0;
    Active.Node = Root;

    // Construct the suffix tree iteratively on each prefix of the string.
    // PfxEndIdx is the end index of the current prefix.
    // End is one past the last element in the string.
    for (unsigned PfxEndIdx = 0, End = Str.size(); PfxEndIdx < End;
         PfxEndIdx++) {
      SuffixesToAdd++;
      LeafEndIdx = PfxEndIdx; // Extend each of the leaves.
      SuffixesToAdd = extend(PfxEndIdx, SuffixesToAdd);
    }

    // Set the suffix indices of each leaf.
    assert(Root && "Root node can't be nullptr!");
    setSuffixIndices(*Root, 0);
  }


  /// Iterator for finding all repeated substrings in the suffix tree.
  struct RepeatedSubstringIterator {
    private:
    /// The current node we're visiting.
    SuffixTreeNode *N = nullptr;

    /// The repeated substring associated with this node.
    RepeatedSubstring RS;

    /// The nodes left to visit.
    std::vector<SuffixTreeNode *> ToVisit;

    /// The minimum length of a repeated substring to find.
    /// Since we're outlining, we want at least two instructions in the range.
    /// FIXME: This may not be true for targets like X86 which support many
    /// instruction lengths.
    const unsigned MinLength = 2;

    /// Move the iterator to the next repeated substring.
    void advance() {
      // Clear the current state. If we're at the end of the range, then this
      // is the state we want to be in.
      RS = RepeatedSubstring();
      N = nullptr;

      // Each leaf node represents a repeat of a string.
      std::vector<SuffixTreeNode *> LeafChildren;

      // Continue visiting nodes until we find one which repeats more than once.
      while (!ToVisit.empty()) {
        SuffixTreeNode *Curr = ToVisit.back();
        ToVisit.pop_back();
        LeafChildren.clear();

        // Keep track of the length of the string associated with the node. If
        // it's too short, we'll quit.
        unsigned Length = Curr->ConcatLen;

        // Iterate over each child, saving internal nodes for visiting, and
        // leaf nodes in LeafChildren. Internal nodes represent individual
        // strings, which may repeat.
        for (auto &ChildPair : Curr->Children) {
          // Save all of this node's children for processing.
          if (!ChildPair.second->isLeaf())
            ToVisit.push_back(ChildPair.second);

          // It's not an internal node, so it must be a leaf. If we have a
          // long enough string, then save the leaf children.
          else if (Length >= MinLength)
            LeafChildren.push_back(ChildPair.second);
        }

        // The root never represents a repeated substring. If we're looking at
        // that, then skip it.
        if (Curr->isRoot())
          continue;

        // Do we have any repeated substrings?
        if (LeafChildren.size() >= 2) {
          // Yes. Update the state to reflect this, and then bail out.
          N = Curr;
          RS.Length = Length;
          for (SuffixTreeNode *Leaf : LeafChildren)
            RS.StartIndices.push_back(Leaf->SuffixIdx);
          break;
        }
      }

      // At this point, either NewRS is an empty RepeatedSubstring, or it was
      // set in the above loop. Similarly, N is either nullptr, or the node
      // associated with NewRS.
    }

  public:
    /// Return the current repeated substring.
    RepeatedSubstring &operator*() { return RS; }

    RepeatedSubstringIterator &operator++() {
      advance();
      return *this;
    }

    RepeatedSubstringIterator operator++(int I) {
      RepeatedSubstringIterator It(*this);
      advance();
      return It;
    }

    bool operator==(const RepeatedSubstringIterator &Other) {
      return N == Other.N;
    }
    bool operator!=(const RepeatedSubstringIterator &Other) {
      return !(*this == Other);
    }

    RepeatedSubstringIterator(SuffixTreeNode *N) : N(N) {
      // Do we have a non-null node?
      if (N) {
        // Yes. At the first step, we need to visit all of N's children.
        // Note: This means that we visit N last.
        ToVisit.push_back(N);
        advance();
      }
    }
};

  typedef RepeatedSubstringIterator iterator;
  iterator begin() { return iterator(Root); }
  iterator end() { return iterator(nullptr); }
};

/// Maps \p MachineInstrs to unsigned integers and stores the mappings.
struct InstructionMapper {

  /// The next available integer to assign to a \p MachineInstr that
  /// cannot be outlined.
  ///
  /// Set to -3 for compatability with \p DenseMapInfo<unsigned>.
  unsigned IllegalInstrNumber = -3;

  /// The next available integer to assign to a \p MachineInstr that can
  /// be outlined.
  unsigned LegalInstrNumber = 0;

  /// Correspondence from \p MachineInstrs to unsigned integers.
  DenseMap<MachineInstr *, unsigned, MachineInstrExpressionTrait>
      InstructionIntegerMap;

  /// Correspondence between \p MachineBasicBlocks and target-defined flags.
  DenseMap<MachineBasicBlock *, unsigned> MBBFlagsMap;

  /// The vector of unsigned integers that the module is mapped to.
  std::vector<unsigned> UnsignedVec;

  /// Stores the location of the instruction associated with the integer
  /// at index i in \p UnsignedVec for each index i.
  std::vector<MachineBasicBlock::iterator> InstrList;

  // Set if we added an illegal number in the previous step.
  // Since each illegal number is unique, we only need one of them between
  // each range of legal numbers. This lets us make sure we don't add more
  // than one illegal number per range.
  bool AddedIllegalLastTime = false;

  /// Maps \p *It to a legal integer.
  ///
  /// Updates \p CanOutlineWithPrevInstr, \p HaveLegalRange, \p InstrListForMBB,
  /// \p UnsignedVecForMBB, \p InstructionIntegerMap, and \p LegalInstrNumber.
  ///
  /// \returns The integer that \p *It was mapped to.
  unsigned mapToLegalUnsigned(
      MachineBasicBlock::iterator &It, bool &CanOutlineWithPrevInstr,
      bool &HaveLegalRange, unsigned &NumLegalInBlock,
      std::vector<unsigned> &UnsignedVecForMBB,
      std::vector<MachineBasicBlock::iterator> &InstrListForMBB) {
    // We added something legal, so we should unset the AddedLegalLastTime
    // flag.
    AddedIllegalLastTime = false;

    // If we have at least two adjacent legal instructions (which may have
    // invisible instructions in between), remember that.
    if (CanOutlineWithPrevInstr)
      HaveLegalRange = true;
    CanOutlineWithPrevInstr = true;

    // Keep track of the number of legal instructions we insert.
    NumLegalInBlock++;

    // Get the integer for this instruction or give it the current
    // LegalInstrNumber.
    InstrListForMBB.push_back(It);
    MachineInstr &MI = *It;
    bool WasInserted;
    DenseMap<MachineInstr *, unsigned, MachineInstrExpressionTrait>::iterator
        ResultIt;
    std::tie(ResultIt, WasInserted) =
        InstructionIntegerMap.insert(std::make_pair(&MI, LegalInstrNumber));
    unsigned MINumber = ResultIt->second;

    // There was an insertion.
    if (WasInserted)
      LegalInstrNumber++;

    UnsignedVecForMBB.push_back(MINumber);

    // Make sure we don't overflow or use any integers reserved by the DenseMap.
    if (LegalInstrNumber >= IllegalInstrNumber)
      report_fatal_error("Instruction mapping overflow!");

    assert(LegalInstrNumber != DenseMapInfo<unsigned>::getEmptyKey() &&
           "Tried to assign DenseMap tombstone or empty key to instruction.");
    assert(LegalInstrNumber != DenseMapInfo<unsigned>::getTombstoneKey() &&
           "Tried to assign DenseMap tombstone or empty key to instruction.");

    return MINumber;
  }

  /// Maps \p *It to an illegal integer.
  ///
  /// Updates \p InstrListForMBB, \p UnsignedVecForMBB, and \p
  /// IllegalInstrNumber.
  ///
  /// \returns The integer that \p *It was mapped to.
  unsigned mapToIllegalUnsigned(MachineBasicBlock::iterator &It,
  bool &CanOutlineWithPrevInstr, std::vector<unsigned> &UnsignedVecForMBB,
  std::vector<MachineBasicBlock::iterator> &InstrListForMBB) {
    // Can't outline an illegal instruction. Set the flag.
    CanOutlineWithPrevInstr = false;

    // Only add one illegal number per range of legal numbers.
    if (AddedIllegalLastTime)
      return IllegalInstrNumber;

    // Remember that we added an illegal number last time.
    AddedIllegalLastTime = true;
    unsigned MINumber = IllegalInstrNumber;

    InstrListForMBB.push_back(It);
    UnsignedVecForMBB.push_back(IllegalInstrNumber);
    IllegalInstrNumber--;

    assert(LegalInstrNumber < IllegalInstrNumber &&
           "Instruction mapping overflow!");

    assert(IllegalInstrNumber != DenseMapInfo<unsigned>::getEmptyKey() &&
           "IllegalInstrNumber cannot be DenseMap tombstone or empty key!");

    assert(IllegalInstrNumber != DenseMapInfo<unsigned>::getTombstoneKey() &&
           "IllegalInstrNumber cannot be DenseMap tombstone or empty key!");

    return MINumber;
  }

  /// Transforms a \p MachineBasicBlock into a \p vector of \p unsigneds
  /// and appends it to \p UnsignedVec and \p InstrList.
  ///
  /// Two instructions are assigned the same integer if they are identical.
  /// If an instruction is deemed unsafe to outline, then it will be assigned an
  /// unique integer. The resulting mapping is placed into a suffix tree and
  /// queried for candidates.
  ///
  /// \param MBB The \p MachineBasicBlock to be translated into integers.
  /// \param TII \p TargetInstrInfo for the function.
  void convertToUnsignedVec(MachineBasicBlock &MBB,
                            const TargetInstrInfo &TII) {
    unsigned Flags = 0;

    // Don't even map in this case.
    if (!TII.isMBBSafeToOutlineFrom(MBB, Flags))
      return;

    // Store info for the MBB for later outlining.
    MBBFlagsMap[&MBB] = Flags;

    MachineBasicBlock::iterator It = MBB.begin();

    // The number of instructions in this block that will be considered for
    // outlining.
    unsigned NumLegalInBlock = 0;

    // True if we have at least two legal instructions which aren't separated
    // by an illegal instruction.
    bool HaveLegalRange = false;

    // True if we can perform outlining given the last mapped (non-invisible)
    // instruction. This lets us know if we have a legal range.
    bool CanOutlineWithPrevInstr = false;

    // FIXME: Should this all just be handled in the target, rather than using
    // repeated calls to getOutliningType?
    std::vector<unsigned> UnsignedVecForMBB;
    std::vector<MachineBasicBlock::iterator> InstrListForMBB;

    for (MachineBasicBlock::iterator Et = MBB.end(); It != Et; It++) {
      // Keep track of where this instruction is in the module.
      switch (TII.getOutliningType(It, Flags)) {
      case InstrType::Illegal:
        mapToIllegalUnsigned(It, CanOutlineWithPrevInstr,
                             UnsignedVecForMBB, InstrListForMBB);
        break;

      case InstrType::Legal:
        mapToLegalUnsigned(It, CanOutlineWithPrevInstr, HaveLegalRange,
                           NumLegalInBlock, UnsignedVecForMBB, InstrListForMBB);
        break;

      case InstrType::LegalTerminator:
        mapToLegalUnsigned(It, CanOutlineWithPrevInstr, HaveLegalRange,
                           NumLegalInBlock, UnsignedVecForMBB, InstrListForMBB);
        // The instruction also acts as a terminator, so we have to record that
        // in the string.
        mapToIllegalUnsigned(It, CanOutlineWithPrevInstr, UnsignedVecForMBB,
        InstrListForMBB);
        break;

      case InstrType::Invisible:
        // Normally this is set by mapTo(Blah)Unsigned, but we just want to
        // skip this instruction. So, unset the flag here.
        AddedIllegalLastTime = false;
        break;
      }
    }

    // Are there enough legal instructions in the block for outlining to be
    // possible?
    if (HaveLegalRange) {
      // After we're done every insertion, uniquely terminate this part of the
      // "string". This makes sure we won't match across basic block or function
      // boundaries since the "end" is encoded uniquely and thus appears in no
      // repeated substring.
      mapToIllegalUnsigned(It, CanOutlineWithPrevInstr, UnsignedVecForMBB,
      InstrListForMBB);
      InstrList.insert(InstrList.end(), InstrListForMBB.begin(),
                       InstrListForMBB.end());
      UnsignedVec.insert(UnsignedVec.end(), UnsignedVecForMBB.begin(),
                         UnsignedVecForMBB.end());
    }
  }

  InstructionMapper() {
    // Make sure that the implementation of DenseMapInfo<unsigned> hasn't
    // changed.
    assert(DenseMapInfo<unsigned>::getEmptyKey() == (unsigned)-1 &&
           "DenseMapInfo<unsigned>'s empty key isn't -1!");
    assert(DenseMapInfo<unsigned>::getTombstoneKey() == (unsigned)-2 &&
           "DenseMapInfo<unsigned>'s tombstone key isn't -2!");
  }
};

/// An interprocedural pass which finds repeated sequences of
/// instructions and replaces them with calls to functions.
///
/// Each instruction is mapped to an unsigned integer and placed in a string.
/// The resulting mapping is then placed in a \p SuffixTree. The \p SuffixTree
/// is then repeatedly queried for repeated sequences of instructions. Each
/// non-overlapping repeated sequence is then placed in its own
/// \p MachineFunction and each instance is then replaced with a call to that
/// function.
struct MachineOutliner : public ModulePass {

  static char ID;

  /// Set to true if the outliner should consider functions with
  /// linkonceodr linkage.
  bool OutlineFromLinkOnceODRs = false;

  /// Set to true if the outliner should run on all functions in the module
  /// considered safe for outlining.
  /// Set to true by default for compatibility with llc's -run-pass option.
  /// Set when the pass is constructed in TargetPassConfig.
  bool RunOnAllFunctions = true;

  StringRef getPassName() const override { return "Machine Outliner"; }

  void getAnalysisUsage(AnalysisUsage &AU) const override {
    AU.addRequired<MachineModuleInfo>();
    AU.addPreserved<MachineModuleInfo>();
    AU.setPreservesAll();
    ModulePass::getAnalysisUsage(AU);
  }

  MachineOutliner() : ModulePass(ID) {
    initializeMachineOutlinerPass(*PassRegistry::getPassRegistry());
  }

  /// Remark output explaining that not outlining a set of candidates would be
  /// better than outlining that set.
  void emitNotOutliningCheaperRemark(
      unsigned StringLen, std::vector<Candidate> &CandidatesForRepeatedSeq,
      OutlinedFunction &OF);

  /// Remark output explaining that a function was outlined.
  void emitOutlinedFunctionRemark(OutlinedFunction &OF);

  /// Find all repeated substrings that satisfy the outlining cost model by
  /// constructing a suffix tree.
  ///
  /// If a substring appears at least twice, then it must be represented by
  /// an internal node which appears in at least two suffixes. Each suffix
  /// is represented by a leaf node. To do this, we visit each internal node
  /// in the tree, using the leaf children of each internal node. If an
  /// internal node represents a beneficial substring, then we use each of
  /// its leaf children to find the locations of its substring.
  ///
  /// \param Mapper Contains outlining mapping information.
  /// \param[out] FunctionList Filled with a list of \p OutlinedFunctions
  /// each type of candidate.
  void findCandidates(InstructionMapper &Mapper,
                      std::vector<OutlinedFunction> &FunctionList);

  /// Replace the sequences of instructions represented by \p OutlinedFunctions
  /// with calls to functions.
  ///
  /// \param M The module we are outlining from.
  /// \param FunctionList A list of functions to be inserted into the module.
  /// \param Mapper Contains the instruction mappings for the module.
  bool outline(Module &M, std::vector<OutlinedFunction> &FunctionList,
               InstructionMapper &Mapper);

  /// Creates a function for \p OF and inserts it into the module.
  MachineFunction *createOutlinedFunction(Module &M, OutlinedFunction &OF,
                                          InstructionMapper &Mapper,
                                          unsigned Name);

  /// Construct a suffix tree on the instructions in \p M and outline repeated
  /// strings from that tree.
  bool runOnModule(Module &M) override;

  /// Return a DISubprogram for OF if one exists, and null otherwise. Helper
  /// function for remark emission.
  DISubprogram *getSubprogramOrNull(const OutlinedFunction &OF) {
    DISubprogram *SP;
    for (const Candidate &C : OF.Candidates)
      if (C.getMF() && (SP = C.getMF()->getFunction().getSubprogram()))
        return SP;
    return nullptr;
  }

  /// Populate and \p InstructionMapper with instruction-to-integer mappings.
  /// These are used to construct a suffix tree.
  void populateMapper(InstructionMapper &Mapper, Module &M,
                      MachineModuleInfo &MMI);

  /// Initialize information necessary to output a size remark.
  /// FIXME: This should be handled by the pass manager, not the outliner.
  /// FIXME: This is nearly identical to the initSizeRemarkInfo in the legacy
  /// pass manager.
  void initSizeRemarkInfo(
      const Module &M, const MachineModuleInfo &MMI,
      StringMap<unsigned> &FunctionToInstrCount);

  /// Emit the remark.
  // FIXME: This should be handled by the pass manager, not the outliner.
  void emitInstrCountChangedRemark(
      const Module &M, const MachineModuleInfo &MMI,
      const StringMap<unsigned> &FunctionToInstrCount);
};
} // Anonymous namespace.

char MachineOutliner::ID = 0;

namespace llvm {
ModulePass *createMachineOutlinerPass(bool RunOnAllFunctions) {
  MachineOutliner *OL = new MachineOutliner();
  OL->RunOnAllFunctions = RunOnAllFunctions;
  return OL;
}

} // namespace llvm

INITIALIZE_PASS(MachineOutliner, DEBUG_TYPE, "Machine Function Outliner", false,
                false)

void MachineOutliner::emitNotOutliningCheaperRemark(
    unsigned StringLen, std::vector<Candidate> &CandidatesForRepeatedSeq,
    OutlinedFunction &OF) {
  // FIXME: Right now, we arbitrarily choose some Candidate from the
  // OutlinedFunction. This isn't necessarily fixed, nor does it have to be.
  // We should probably sort these by function name or something to make sure
  // the remarks are stable.
  Candidate &C = CandidatesForRepeatedSeq.front();
  MachineOptimizationRemarkEmitter MORE(*(C.getMF()), nullptr);
  MORE.emit([&]() {
    MachineOptimizationRemarkMissed R(DEBUG_TYPE, "NotOutliningCheaper",
                                      C.front()->getDebugLoc(), C.getMBB());
    R << "Did not outline " << NV("Length", StringLen) << " instructions"
      << " from " << NV("NumOccurrences", CandidatesForRepeatedSeq.size())
      << " locations."
      << " Bytes from outlining all occurrences ("
      << NV("OutliningCost", OF.getOutliningCost()) << ")"
      << " >= Unoutlined instruction bytes ("
      << NV("NotOutliningCost", OF.getNotOutlinedCost()) << ")"
      << " (Also found at: ";

    // Tell the user the other places the candidate was found.
    for (unsigned i = 1, e = CandidatesForRepeatedSeq.size(); i < e; i++) {
      R << NV((Twine("OtherStartLoc") + Twine(i)).str(),
              CandidatesForRepeatedSeq[i].front()->getDebugLoc());
      if (i != e - 1)
        R << ", ";
    }

    R << ")";
    return R;
  });
}

void MachineOutliner::emitOutlinedFunctionRemark(OutlinedFunction &OF) {
  MachineBasicBlock *MBB = &*OF.MF->begin();
  MachineOptimizationRemarkEmitter MORE(*OF.MF, nullptr);
  MachineOptimizationRemark R(DEBUG_TYPE, "OutlinedFunction",
                              MBB->findDebugLoc(MBB->begin()), MBB);
  R << "Saved " << NV("OutliningBenefit", OF.getBenefit()) << " bytes by "
    << "outlining " << NV("Length", OF.getNumInstrs()) << " instructions "
    << "from " << NV("NumOccurrences", OF.getOccurrenceCount())
    << " locations. "
    << "(Found at: ";

  // Tell the user the other places the candidate was found.
  for (size_t i = 0, e = OF.Candidates.size(); i < e; i++) {

    R << NV((Twine("StartLoc") + Twine(i)).str(),
            OF.Candidates[i].front()->getDebugLoc());
    if (i != e - 1)
      R << ", ";
  }

  R << ")";

  MORE.emit(R);
}

void
MachineOutliner::findCandidates(InstructionMapper &Mapper,
                                std::vector<OutlinedFunction> &FunctionList) {
  FunctionList.clear();
  SuffixTree ST(Mapper.UnsignedVec);

  // First, find dall of the repeated substrings in the tree of minimum length
  // 2.
  std::vector<Candidate> CandidatesForRepeatedSeq;
  for (auto It = ST.begin(), Et = ST.end(); It != Et; ++It) {
    CandidatesForRepeatedSeq.clear();
    SuffixTree::RepeatedSubstring RS = *It;
    unsigned StringLen = RS.Length;
    for (const unsigned &StartIdx : RS.StartIndices) {
      unsigned EndIdx = StartIdx + StringLen - 1;
      // Trick: Discard some candidates that would be incompatible with the
      // ones we've already found for this sequence. This will save us some
      // work in candidate selection.
      //
      // If two candidates overlap, then we can't outline them both. This
      // happens when we have candidates that look like, say
      //
      // AA (where each "A" is an instruction).
      //
      // We might have some portion of the module that looks like this:
      // AAAAAA (6 A's)
      //
      // In this case, there are 5 different copies of "AA" in this range, but
      // at most 3 can be outlined. If only outlining 3 of these is going to
      // be unbeneficial, then we ought to not bother.
      //
      // Note that two things DON'T overlap when they look like this:
      // start1...end1 .... start2...end2
      // That is, one must either
      // * End before the other starts
      // * Start after the other ends
      if (std::all_of(
              CandidatesForRepeatedSeq.begin(), CandidatesForRepeatedSeq.end(),
              [&StartIdx, &EndIdx](const Candidate &C) {
                return (EndIdx < C.getStartIdx() || StartIdx > C.getEndIdx());
              })) {
        // It doesn't overlap with anything, so we can outline it.
        // Each sequence is over [StartIt, EndIt].
        // Save the candidate and its location.

        MachineBasicBlock::iterator StartIt = Mapper.InstrList[StartIdx];
        MachineBasicBlock::iterator EndIt = Mapper.InstrList[EndIdx];
        MachineBasicBlock *MBB = StartIt->getParent();

        CandidatesForRepeatedSeq.emplace_back(StartIdx, StringLen, StartIt,
                                              EndIt, MBB, FunctionList.size(),
                                              Mapper.MBBFlagsMap[MBB]);
      }
    }

    // We've found something we might want to outline.
    // Create an OutlinedFunction to store it and check if it'd be beneficial
    // to outline.
    if (CandidatesForRepeatedSeq.size() < 2)
      continue;

    // Arbitrarily choose a TII from the first candidate.
    // FIXME: Should getOutliningCandidateInfo move to TargetMachine?
    const TargetInstrInfo *TII =
        CandidatesForRepeatedSeq[0].getMF()->getSubtarget().getInstrInfo();

    OutlinedFunction OF =
        TII->getOutliningCandidateInfo(CandidatesForRepeatedSeq);

    // If we deleted too many candidates, then there's nothing worth outlining.
    // FIXME: This should take target-specified instruction sizes into account.
    if (OF.Candidates.size() < 2)
      continue;

    // Is it better to outline this candidate than not?
    if (OF.getBenefit() < 1) {
      emitNotOutliningCheaperRemark(StringLen, CandidatesForRepeatedSeq, OF);
      continue;
    }

    FunctionList.push_back(OF);
  }
}

MachineFunction *
MachineOutliner::createOutlinedFunction(Module &M, OutlinedFunction &OF,
                                        InstructionMapper &Mapper,
                                        unsigned Name) {

  // Create the function name. This should be unique. For now, just hash the
  // module name and include it in the function name plus the number of this
  // function.
  std::ostringstream NameStream;
  // FIXME: We should have a better naming scheme. This should be stable,
  // regardless of changes to the outliner's cost model/traversal order.
  NameStream << "OUTLINED_FUNCTION_" << Name;

  // Create the function using an IR-level function.
  LLVMContext &C = M.getContext();
  Function *F = dyn_cast<Function>(
      M.getOrInsertFunction(NameStream.str(), Type::getVoidTy(C)));
  assert(F && "Function was null!");

  // NOTE: If this is linkonceodr, then we can take advantage of linker deduping
  // which gives us better results when we outline from linkonceodr functions.
  F->setLinkage(GlobalValue::InternalLinkage);
  F->setUnnamedAddr(GlobalValue::UnnamedAddr::Global);

  // FIXME: Set nounwind, so we don't generate eh_frame? Haven't verified it's
  // necessary.

  // Set optsize/minsize, so we don't insert padding between outlined
  // functions.
  F->addFnAttr(Attribute::OptimizeForSize);
  F->addFnAttr(Attribute::MinSize);

  // Include target features from an arbitrary candidate for the outlined
  // function. This makes sure the outlined function knows what kinds of
  // instructions are going into it. This is fine, since all parent functions
  // must necessarily support the instructions that are in the outlined region.
  Candidate &FirstCand = OF.Candidates.front();
  const Function &ParentFn = FirstCand.getMF()->getFunction();
  if (ParentFn.hasFnAttribute("target-features"))
    F->addFnAttr(ParentFn.getFnAttribute("target-features"));

  BasicBlock *EntryBB = BasicBlock::Create(C, "entry", F);
  IRBuilder<> Builder(EntryBB);
  Builder.CreateRetVoid();

  MachineModuleInfo &MMI = getAnalysis<MachineModuleInfo>();
  MachineFunction &MF = MMI.getOrCreateMachineFunction(*F);
  MachineBasicBlock &MBB = *MF.CreateMachineBasicBlock();
  const TargetSubtargetInfo &STI = MF.getSubtarget();
  const TargetInstrInfo &TII = *STI.getInstrInfo();

  // Insert the new function into the module.
  MF.insert(MF.begin(), &MBB);

  for (auto I = FirstCand.front(), E = std::next(FirstCand.back()); I != E;
       ++I) {
    MachineInstr *NewMI = MF.CloneMachineInstr(&*I);
    NewMI->dropMemRefs(MF);

    // Don't keep debug information for outlined instructions.
    NewMI->setDebugLoc(DebugLoc());
    MBB.insert(MBB.end(), NewMI);
  }

  TII.buildOutlinedFrame(MBB, MF, OF);

  // Outlined functions shouldn't preserve liveness.
  MF.getProperties().reset(MachineFunctionProperties::Property::TracksLiveness);
  MF.getRegInfo().freezeReservedRegs(MF);

  // If there's a DISubprogram associated with this outlined function, then
  // emit debug info for the outlined function.
  if (DISubprogram *SP = getSubprogramOrNull(OF)) {
    // We have a DISubprogram. Get its DICompileUnit.
    DICompileUnit *CU = SP->getUnit();
    DIBuilder DB(M, true, CU);
    DIFile *Unit = SP->getFile();
    Mangler Mg;
    // Get the mangled name of the function for the linkage name.
    std::string Dummy;
    llvm::raw_string_ostream MangledNameStream(Dummy);
    Mg.getNameWithPrefix(MangledNameStream, F, false);

    DISubprogram *OutlinedSP = DB.createFunction(
        Unit /* Context */, F->getName(), StringRef(MangledNameStream.str()),
        Unit /* File */,
        0 /* Line 0 is reserved for compiler-generated code. */,
        DB.createSubroutineType(DB.getOrCreateTypeArray(None)), /* void type */
        0, /* Line 0 is reserved for compiler-generated code. */
        DINode::DIFlags::FlagArtificial /* Compiler-generated code. */,
        /* Outlined code is optimized code by definition. */
        DISubprogram::SPFlagDefinition | DISubprogram::SPFlagOptimized);

    // Don't add any new variables to the subprogram.
    DB.finalizeSubprogram(OutlinedSP);

    // Attach subprogram to the function.
    F->setSubprogram(OutlinedSP);
    // We're done with the DIBuilder.
    DB.finalize();
  }

  return &MF;
}

bool MachineOutliner::outline(Module &M,
                              std::vector<OutlinedFunction> &FunctionList,
                              InstructionMapper &Mapper) {

  bool OutlinedSomething = false;

  // Number to append to the current outlined function.
  unsigned OutlinedFunctionNum = 0;

  // Sort by benefit. The most beneficial functions should be outlined first.
  std::stable_sort(
      FunctionList.begin(), FunctionList.end(),
      [](const OutlinedFunction &LHS, const OutlinedFunction &RHS) {
        return LHS.getBenefit() > RHS.getBenefit();
      });

  // Walk over each function, outlining them as we go along. Functions are
  // outlined greedily, based off the sort above.
  for (OutlinedFunction &OF : FunctionList) {
    // If we outlined something that overlapped with a candidate in a previous
    // step, then we can't outline from it.
    erase_if(OF.Candidates, [&Mapper](Candidate &C) {
      return std::any_of(
          Mapper.UnsignedVec.begin() + C.getStartIdx(),
          Mapper.UnsignedVec.begin() + C.getEndIdx() + 1,
          [](unsigned I) { return (I == static_cast<unsigned>(-1)); });
    });

    // If we made it unbeneficial to outline this function, skip it.
    if (OF.getBenefit() < 1)
      continue;

    // It's beneficial. Create the function and outline its sequence's
    // occurrences.
    OF.MF = createOutlinedFunction(M, OF, Mapper, OutlinedFunctionNum);
    emitOutlinedFunctionRemark(OF);
    FunctionsCreated++;
    OutlinedFunctionNum++; // Created a function, move to the next name.
    MachineFunction *MF = OF.MF;
    const TargetSubtargetInfo &STI = MF->getSubtarget();
    const TargetInstrInfo &TII = *STI.getInstrInfo();

    // Replace occurrences of the sequence with calls to the new function.
    for (Candidate &C : OF.Candidates) {
      MachineBasicBlock &MBB = *C.getMBB();
      MachineBasicBlock::iterator StartIt = C.front();
      MachineBasicBlock::iterator EndIt = C.back();

      // Insert the call.
      auto CallInst = TII.insertOutlinedCall(M, MBB, StartIt, *MF, C);

      // If the caller tracks liveness, then we need to make sure that
      // anything we outline doesn't break liveness assumptions. The outlined
      // functions themselves currently don't track liveness, but we should
      // make sure that the ranges we yank things out of aren't wrong.
      if (MBB.getParent()->getProperties().hasProperty(
              MachineFunctionProperties::Property::TracksLiveness)) {
        // Helper lambda for adding implicit def operands to the call
        // instruction.
        auto CopyDefs = [&CallInst](MachineInstr &MI) {
          for (MachineOperand &MOP : MI.operands()) {
            // Skip over anything that isn't a register.
            if (!MOP.isReg())
              continue;

            // If it's a def, add it to the call instruction.
            if (MOP.isDef())
              CallInst->addOperand(MachineOperand::CreateReg(
                  MOP.getReg(), true, /* isDef = true */
                  true /* isImp = true */));
          }
        };
        // Copy over the defs in the outlined range.
        // First inst in outlined range <-- Anything that's defined in this
        // ...                           .. range has to be added as an
        // implicit Last inst in outlined range  <-- def to the call
        // instruction.
        std::for_each(CallInst, std::next(EndIt), CopyDefs);
      }

      // Erase from the point after where the call was inserted up to, and
      // including, the final instruction in the sequence.
      // Erase needs one past the end, so we need std::next there too.
      MBB.erase(std::next(StartIt), std::next(EndIt));

      // Keep track of what we removed by marking them all as -1.
      std::for_each(Mapper.UnsignedVec.begin() + C.getStartIdx(),
                    Mapper.UnsignedVec.begin() + C.getEndIdx() + 1,
                    [](unsigned &I) { I = static_cast<unsigned>(-1); });
      OutlinedSomething = true;

      // Statistics.
      NumOutlined++;
    }
  }

  LLVM_DEBUG(dbgs() << "OutlinedSomething = " << OutlinedSomething << "\n";);

  return OutlinedSomething;
}

void MachineOutliner::populateMapper(InstructionMapper &Mapper, Module &M,
                                     MachineModuleInfo &MMI) {
  // Build instruction mappings for each function in the module. Start by
  // iterating over each Function in M.
  for (Function &F : M) {

    // If there's nothing in F, then there's no reason to try and outline from
    // it.
    if (F.empty())
      continue;

    // There's something in F. Check if it has a MachineFunction associated with
    // it.
    MachineFunction *MF = MMI.getMachineFunction(F);

    // If it doesn't, then there's nothing to outline from. Move to the next
    // Function.
    if (!MF)
      continue;

    const TargetInstrInfo *TII = MF->getSubtarget().getInstrInfo();

    if (!RunOnAllFunctions && !TII->shouldOutlineFromFunctionByDefault(*MF))
      continue;

    // We have a MachineFunction. Ask the target if it's suitable for outlining.
    // If it isn't, then move on to the next Function in the module.
    if (!TII->isFunctionSafeToOutlineFrom(*MF, OutlineFromLinkOnceODRs))
      continue;

    // We have a function suitable for outlining. Iterate over every
    // MachineBasicBlock in MF and try to map its instructions to a list of
    // unsigned integers.
    for (MachineBasicBlock &MBB : *MF) {
      // If there isn't anything in MBB, then there's no point in outlining from
      // it.
      // If there are fewer than 2 instructions in the MBB, then it can't ever
      // contain something worth outlining.
      // FIXME: This should be based off of the maximum size in B of an outlined
      // call versus the size in B of the MBB.
      if (MBB.empty() || MBB.size() < 2)
        continue;

      // Check if MBB could be the target of an indirect branch. If it is, then
      // we don't want to outline from it.
      if (MBB.hasAddressTaken())
        continue;

      // MBB is suitable for outlining. Map it to a list of unsigneds.
      Mapper.convertToUnsignedVec(MBB, *TII);
    }
  }
}

void MachineOutliner::initSizeRemarkInfo(
    const Module &M, const MachineModuleInfo &MMI,
    StringMap<unsigned> &FunctionToInstrCount) {
  // Collect instruction counts for every function. We'll use this to emit
  // per-function size remarks later.
  for (const Function &F : M) {
    MachineFunction *MF = MMI.getMachineFunction(F);

    // We only care about MI counts here. If there's no MachineFunction at this
    // point, then there won't be after the outliner runs, so let's move on.
    if (!MF)
      continue;
    FunctionToInstrCount[F.getName().str()] = MF->getInstructionCount();
  }
}

void MachineOutliner::emitInstrCountChangedRemark(
    const Module &M, const MachineModuleInfo &MMI,
    const StringMap<unsigned> &FunctionToInstrCount) {
  // Iterate over each function in the module and emit remarks.
  // Note that we won't miss anything by doing this, because the outliner never
  // deletes functions.
  for (const Function &F : M) {
    MachineFunction *MF = MMI.getMachineFunction(F);

    // The outliner never deletes functions. If we don't have a MF here, then we
    // didn't have one prior to outlining either.
    if (!MF)
      continue;

    std::string Fname = F.getName();
    unsigned FnCountAfter = MF->getInstructionCount();
    unsigned FnCountBefore = 0;

    // Check if the function was recorded before.
    auto It = FunctionToInstrCount.find(Fname);

    // Did we have a previously-recorded size? If yes, then set FnCountBefore
    // to that.
    if (It != FunctionToInstrCount.end())
      FnCountBefore = It->second;

    // Compute the delta and emit a remark if there was a change.
    int64_t FnDelta = static_cast<int64_t>(FnCountAfter) -
                      static_cast<int64_t>(FnCountBefore);
    if (FnDelta == 0)
      continue;

    MachineOptimizationRemarkEmitter MORE(*MF, nullptr);
    MORE.emit([&]() {
      MachineOptimizationRemarkAnalysis R("size-info", "FunctionMISizeChange",
                                          DiagnosticLocation(),
                                          &MF->front());
      R << DiagnosticInfoOptimizationBase::Argument("Pass", "Machine Outliner")
        << ": Function: "
        << DiagnosticInfoOptimizationBase::Argument("Function", F.getName())
        << ": MI instruction count changed from "
        << DiagnosticInfoOptimizationBase::Argument("MIInstrsBefore",
                                                    FnCountBefore)
        << " to "
        << DiagnosticInfoOptimizationBase::Argument("MIInstrsAfter",
                                                    FnCountAfter)
        << "; Delta: "
        << DiagnosticInfoOptimizationBase::Argument("Delta", FnDelta);
      return R;
    });
  }
}

bool MachineOutliner::runOnModule(Module &M) {
  // Check if there's anything in the module. If it's empty, then there's
  // nothing to outline.
  if (M.empty())
    return false;

  MachineModuleInfo &MMI = getAnalysis<MachineModuleInfo>();

  // If the user passed -enable-machine-outliner=always or
  // -enable-machine-outliner, the pass will run on all functions in the module.
  // Otherwise, if the target supports default outlining, it will run on all
  // functions deemed by the target to be worth outlining from by default. Tell
  // the user how the outliner is running.
  LLVM_DEBUG(
    dbgs() << "Machine Outliner: Running on ";
    if (RunOnAllFunctions)
      dbgs() << "all functions";
    else
      dbgs() << "target-default functions";
    dbgs() << "\n"
  );

  // If the user specifies that they want to outline from linkonceodrs, set
  // it here.
  OutlineFromLinkOnceODRs = EnableLinkOnceODROutlining;
  InstructionMapper Mapper;

  // Prepare instruction mappings for the suffix tree.
  populateMapper(Mapper, M, MMI);
  std::vector<OutlinedFunction> FunctionList;

  // Find all of the outlining candidates.
  findCandidates(Mapper, FunctionList);

  // If we've requested size remarks, then collect the MI counts of every
  // function before outlining, and the MI counts after outlining.
  // FIXME: This shouldn't be in the outliner at all; it should ultimately be
  // the pass manager's responsibility.
  // This could pretty easily be placed in outline instead, but because we
  // really ultimately *don't* want this here, it's done like this for now
  // instead.

  // Check if we want size remarks.
  bool ShouldEmitSizeRemarks = M.shouldEmitInstrCountChangedRemark();
  StringMap<unsigned> FunctionToInstrCount;
  if (ShouldEmitSizeRemarks)
    initSizeRemarkInfo(M, MMI, FunctionToInstrCount);

  // Outline each of the candidates and return true if something was outlined.
  bool OutlinedSomething = outline(M, FunctionList, Mapper);

  // If we outlined something, we definitely changed the MI count of the
  // module. If we've asked for size remarks, then output them.
  // FIXME: This should be in the pass manager.
  if (ShouldEmitSizeRemarks && OutlinedSomething)
    emitInstrCountChangedRemark(M, MMI, FunctionToInstrCount);

  return OutlinedSomething;
}
